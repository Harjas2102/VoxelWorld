// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainWorldStore.h"

#include "TerrainCore.h"

/**
 * A world directory (P-004 §11, P-003 §4, §5).
 *
 * Every write order in this file is load-bearing, and each one is annotated with what a crash
 * at that exact point leaves behind. That is the only way P-004 §12's containment argument is
 * checkable rather than asserted: "the worst case is losing a checkpoint, not corrupting a
 * world" is a claim about orderings, and it is true only if the orderings are these.
 */

FTerrainWorldStore::FTerrainWorldStore(ITerrainStorageDevice& InDevice)
	: Device(InDevice)
	, Objects(InDevice)
	, RootSlots(InDevice, ETerrainPersistObjectType::RootSlot,
	            TerrainStoragePaths::RootSlot(0), TerrainStoragePaths::RootSlot(1))
{
}

FTerrainStoreResult FTerrainWorldStore::StoreCheckpointObject(
	const FTerrainCheckpointDescriptor& Checkpoint, FTerrainDigest& OutDigest, uint32& OutLength)
{
	TArray<uint8> Body;
	const ETerrainPersistError BodyError = TerrainPersistEncodeCheckpointBody(Checkpoint, Body);
	if (BodyError != ETerrainPersistError::None)
	{
		return FTerrainStoreResult::Bad(BodyError);
	}

	TArray<uint8> Object;
	const ETerrainPersistError ObjectError = TerrainPersistEncodeObject(
		ETerrainPersistObjectType::CheckpointDescriptor, State.Identity, Body, Object);
	if (ObjectError != ETerrainPersistError::None)
	{
		return FTerrainStoreResult::Bad(ObjectError);
	}

	OutDigest = TerrainPersistDigest(Object);
	OutLength = static_cast<uint32>(Object.Num());

	if (!Objects.StoreObject(OutDigest, Object))
	{
		return FTerrainStoreResult::Io(ETerrainStorageResult::IoError);
	}
	return FTerrainStoreResult::Ok();
}

FTerrainStoreResult FTerrainWorldStore::SyncBootstrapDirectories()
{
	// The world directory itself holds base.tobj and the top-level directories; each of these
	// holds the fixed files bootstrap created. Nothing else in a world is ever a new name.
	for (const TCHAR* Directory : { TEXT(""),
	                                TerrainStoragePaths::RootsDirectory,
	                                TerrainStoragePaths::JournalDirectory,
	                                TerrainStoragePaths::ContainersDirectory })
	{
		const ETerrainStorageResult Result = Device.SyncDirectory(Directory);
		if (Result != ETerrainStorageResult::Ok)
		{
			return FTerrainStoreResult::Io(Result);
		}
	}
	return FTerrainStoreResult::Ok();
}

FTerrainStoreResult FTerrainWorldStore::Create(
	const FTerrainBaseDescriptor& Base,
	const FTerrainWorldId& World,
	const FTerrainStoreEpoch& Epoch,
	int64 UtcMillis)
{
	if (Device.Exists(TerrainStoragePaths::BaseDescriptor))
	{
		// Creating over an existing world would mint a new identity for files that still carry
		// the old one, and every one of them would then fail its identity check.
		return FTerrainStoreResult::Io(ETerrainStorageResult::AlreadyExists);
	}

	for (const TCHAR* Directory : { TerrainStoragePaths::RootsDirectory,
	                                TerrainStoragePaths::JournalDirectory })
	{
		const ETerrainStorageResult Result = Device.EnsureDirectory(Directory);
		if (Result != ETerrainStorageResult::Ok)
		{
			return FTerrainStoreResult::Io(Result);
		}
	}

	// --- 0. the container pool, before any object exists to go in it (P-005 §4) ---------------
	// Bootstrap: these are the last object-store names this world will ever create. Every
	// object from here on -- starting with the G=0 descriptor below -- is a frame in one of them.
	const ETerrainStorageResult LayoutResult = Objects.EnsureLayout();
	if (LayoutResult != ETerrainStorageResult::Ok)
	{
		return FTerrainStoreResult::Io(LayoutResult);
	}

	// --- 1. the base descriptor, because everything else binds to its digest ---------------
	TArray<uint8> BaseObject;
	FTerrainDigest BaseDigest;
	const ETerrainPersistError BaseError =
		TerrainPersistEncodeBaseDescriptorObject(Base, World, Epoch, BaseObject, BaseDigest);
	if (BaseError != ETerrainPersistError::None)
	{
		return FTerrainStoreResult::Bad(BaseError);
	}

	const ETerrainStorageResult BaseWrite =
		Device.WriteNew(TerrainStoragePaths::BaseDescriptor, BaseObject);
	if (BaseWrite != ETerrainStorageResult::Ok)
	{
		// A crash here leaves a directory with no base descriptor, which Open() reports as
		// NotFound. Nothing has an identity to be inconsistent with yet.
		return FTerrainStoreResult::Io(BaseWrite);
	}

	State.Identity.World      = World;
	State.Identity.Epoch      = Epoch;
	State.Identity.BaseDigest = BaseDigest;
	State.Base                = Base;

	// --- 2. the empty G=0 checkpoint, before the root that names it -------------------------
	FTerrainCheckpointDescriptor Empty;
	Empty.G                = 0;
	Empty.Generation       = 1;
	Empty.CreatedUtcMillis = UtcMillis;
	Empty.bHasRootPage     = false;   // a world that has been created and never edited

	FTerrainDigest CheckpointDigest;
	uint32 CheckpointLength = 0;
	const FTerrainStoreResult CheckpointResult =
		StoreCheckpointObject(Empty, CheckpointDigest, CheckpointLength);
	if (!CheckpointResult.IsOk())
	{
		// A crash here leaves a base descriptor and no roots. Open() reports the roots as
		// absent, and the world is unopenable rather than wrong -- which is the right failure
		// for a world that was never finished being created.
		return CheckpointResult;
	}

	// --- 3. both root slots -----------------------------------------------------------------
	FTerrainRootSlot Root;
	Root.Generation         = 1;
	Root.G                  = 0;
	Root.DescriptorDigest   = CheckpointDigest;
	Root.DescriptorLength   = CheckpointLength;
	Root.PublishedUtcMillis = UtcMillis;

	TArray<uint8> RootBody;
	const ETerrainPersistError RootError = TerrainPersistEncodeRootSlotBody(Root, RootBody);
	if (RootError != ETerrainPersistError::None)
	{
		return FTerrainStoreResult::Bad(RootError);
	}

	const ETerrainStorageResult RootWrite = RootSlots.Create(State.Identity, RootBody);
	if (RootWrite != ETerrainStorageResult::Ok)
	{
		return FTerrainStoreResult::Io(RootWrite);
	}

	// --- 4. the journal: first segment, then both anchors ------------------------------------
	Journal = MakeUnique<FTerrainJournalWriter>(Device, State.Identity);
	const FTerrainStoreResult JournalResult = Journal->Create(/*SegmentId=*/1, /*FirstOpSeq=*/1, UtcMillis);
	if (!JournalResult.IsOk())
	{
		Journal.Reset();
		return JournalResult;
	}

	// --- 5. make the bootstrap names as durable as the platform allows (P-005 §6) ------------
	// Everything above created a name, and nothing below will. On Unix this is fsync on each
	// directory, which closes the window; on Windows it is best effort and says so in the log.
	const FTerrainStoreResult Synced = SyncBootstrapDirectories();
	if (!Synced.IsOk())
	{
		return Synced;
	}

	// Open rather than assume: creating a world and then reading it back through the ordinary
	// path is the cheapest possible check that the two agree.
	return Open();
}

FTerrainStoreResult FTerrainWorldStore::Open()
{
	bOpen = false;
	Journal.Reset();
	State = FTerrainWorldStoreState();

	// --- the base descriptor defines the identity everything else is checked against ---------
	TArray<uint8> BaseObject;
	const ETerrainStorageResult BaseRead =
		Device.Read(TerrainStoragePaths::BaseDescriptor, BaseObject);
	if (BaseRead != ETerrainStorageResult::Ok)
	{
		return FTerrainStoreResult::Io(BaseRead);
	}

	const ETerrainPersistError BaseError =
		TerrainPersistDecodeBaseDescriptorObject(BaseObject, State.Identity, State.Base);
	if (BaseError != ETerrainPersistError::None)
	{
		return FTerrainStoreResult::Bad(BaseError);
	}

	// --- the container pool: bootstrap for a world written before P-005 ----------------------
	// A world created since P-005 already has all four and this creates nothing. An older world
	// gets its pool here, before the store admits anything, which keeps the rule that an open
	// world creates no names. Its loose objects and packs stay readable, and retention moves
	// the live ones into containers.
	bool bCreatedLayout = false;
	const ETerrainStorageResult LayoutResult = Objects.EnsureLayout(&bCreatedLayout);
	if (LayoutResult != ETerrainStorageResult::Ok)
	{
		return FTerrainStoreResult::Io(LayoutResult);
	}
	if (bCreatedLayout)
	{
		const FTerrainStoreResult Synced = SyncBootstrapDirectories();
		if (!Synced.IsOk())
		{
			return Synced;
		}
	}

	// --- containers and packs, before anything can try to resolve an object ------------------
	// The descriptor and every index page a previous capture wrote live inside a frame (or, in
	// an older world, a pack), so the location map has to exist before the first LoadObject.
	const ETerrainStorageResult PackResult = Objects.LoadPacks();
	if (PackResult != ETerrainStorageResult::Ok)
	{
		return FTerrainStoreResult::Io(PackResult);
	}

	// --- the root slots ----------------------------------------------------------------------
	int32 BestIndex = INDEX_NONE;
	const ETerrainStorageResult RootRead =
		RootSlots.Read(State.Identity, State.RootSlots[0], State.RootSlots[1], BestIndex);
	if (RootRead != ETerrainStorageResult::Ok)
	{
		return FTerrainStoreResult::Io(RootRead);
	}
	if (BestIndex == INDEX_NONE)
	{
		if (!State.RootSlots[0].bPresent && !State.RootSlots[1].bPresent)
		{
			return FTerrainStoreResult::Io(ETerrainStorageResult::NotFound);
		}
		return FTerrainStoreResult::Bad(
			State.RootSlots[0].bPresent ? State.RootSlots[0].Error : State.RootSlots[1].Error);
	}

	State.bRootRedundancyIntact = State.RootSlots[0].bValid && State.RootSlots[1].bValid;
	if (!State.bRootRedundancyIntact)
	{
		// P-004 §8: with one root left, reclamation must stop until redundancy is repaired.
		// Said loudly because the world still opens and plays, and the danger is invisible.
		UE_LOG(LogTerrainCore, Warning,
			TEXT("World store: only one root slot is valid. Reclamation must stay disabled until ")
			TEXT("the other is republished."));
	}

	const ETerrainPersistError RootError =
		TerrainPersistDecodeRootSlotBody(State.RootSlots[BestIndex].Body, State.Root);
	if (RootError != ETerrainPersistError::None)
	{
		return FTerrainStoreResult::Bad(RootError);
	}

	// --- the checkpoint that root names -------------------------------------------------------
	TArray<uint8> CheckpointObject;
	if (!Objects.LoadObject(State.Root.DescriptorDigest, CheckpointObject))
	{
		// The root named an object that is absent or does not hash to its name. This is the
		// case P-004 §12's containment argument is about: the answer is to fall back to the
		// other root, and choosing WHICH root belongs to the recovery increment, so for now
		// this is reported rather than worked around.
		UE_LOG(LogTerrainCore, Error,
			TEXT("World store: root generation %llu names a checkpoint object that is missing or ")
			TEXT("damaged. Falling back to an older root is the recovery path and is not built."),
			State.Root.Generation);
		return FTerrainStoreResult::Io(ETerrainStorageResult::NotFound);
	}
	if (static_cast<uint32>(CheckpointObject.Num()) != State.Root.DescriptorLength)
	{
		return FTerrainStoreResult::Bad(ETerrainPersistError::BodyLengthOutOfRange);
	}

	FTerrainPersistObjectHeader CheckpointHeader;
	TArrayView<const uint8> CheckpointBody;
	const ETerrainPersistError CheckpointObjectError = TerrainPersistDecodeObject(
		CheckpointObject, ETerrainPersistObjectType::CheckpointDescriptor,
		&State.Identity, CheckpointHeader, CheckpointBody);
	if (CheckpointObjectError != ETerrainPersistError::None)
	{
		return FTerrainStoreResult::Bad(CheckpointObjectError);
	}

	const ETerrainPersistError CheckpointError =
		TerrainPersistDecodeCheckpointBody(CheckpointBody, State.Checkpoint);
	if (CheckpointError != ETerrainPersistError::None)
	{
		return FTerrainStoreResult::Bad(CheckpointError);
	}

	if (State.Checkpoint.G != State.Root.G || State.Checkpoint.Generation != State.Root.Generation)
	{
		// The root and the descriptor it names must agree about which cut this is. They are
		// written together, so disagreement means one of them is not the one that was written.
		return FTerrainStoreResult::Bad(ETerrainPersistError::FieldOutOfRange);
	}

	// --- the journal --------------------------------------------------------------------------
	Journal = MakeUnique<FTerrainJournalWriter>(Device, State.Identity);
	const FTerrainStoreResult JournalResult = Journal->Open();
	if (!JournalResult.IsOk())
	{
		Journal.Reset();
		return JournalResult;
	}

	// P-003 §3 requires G <= H. A checkpoint from beyond the journal head means the journal
	// was replaced with an older one, and no amount of replay reconstructs what is missing.
	const FTerrainOpSeq Head = Journal->GetHead();
	if (State.Checkpoint.G > Head)
	{
		UE_LOG(LogTerrainCore, Error,
			TEXT("World store: checkpoint G=%llu is beyond journal head H=%llu. The terrain cut ")
			TEXT("records edits the journal does not, which is corruption, not a recoverable gap."),
			State.Checkpoint.G, Head);
		Journal.Reset();
		return FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
	}

	bOpen = true;
	UE_LOG(LogTerrainCore, Log,
		TEXT("World store opened: G=%llu generation=%llu H=%llu segment=%llu roots=%s"),
		State.Checkpoint.G, State.Root.Generation, Head,
		Journal->GetState().ActiveSegmentId,
		State.bRootRedundancyIntact ? TEXT("both valid") : TEXT("ONE VALID"));
	return FTerrainStoreResult::Ok();
}

FTerrainStoreResult FTerrainWorldStore::PublishCheckpoint(
	const FTerrainCheckpointDescriptor& Checkpoint, int64 UtcMillis)
{
	if (!bOpen)
	{
		return FTerrainStoreResult::Io(ETerrainStorageResult::NotFound);
	}

	FTerrainCheckpointDescriptor ToPublish = Checkpoint;
	ToPublish.Generation       = State.Root.Generation + 1;
	ToPublish.CreatedUtcMillis = UtcMillis;

	if (ToPublish.G < State.Checkpoint.G)
	{
		// A cut may repeat -- nothing changed since the last one -- but it may not go backwards.
		return FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
	}

	// The descriptor object FIRST. A crash between here and the root slot leaves an
	// unreferenced object that the old root does not name: garbage to be collected, not a
	// broken world. That is the whole of P-004 §12's containment argument, in two statements.
	FTerrainDigest Digest;
	uint32 Length = 0;
	const FTerrainStoreResult ObjectResult = StoreCheckpointObject(ToPublish, Digest, Length);
	if (!ObjectResult.IsOk())
	{
		return ObjectResult;
	}

	// Everything this capture wrote -- payload objects, index pages and the descriptor above --
	// becomes durable HERE, in one flush, and strictly before the root slot that names it. The
	// ordering is the same as it always was; only the number of files changed. A crash before
	// this leaves nothing; a crash after it leaves a pack no root names, which is the
	// unreferenced garbage P-004 §12 already accounts for.
	const ETerrainStorageResult BatchResult = Objects.CommitBatch();
	if (BatchResult != ETerrainStorageResult::Ok)
	{
		return FTerrainStoreResult::Io(BatchResult);
	}

	FTerrainRootSlot Root;
	Root.Generation         = ToPublish.Generation;
	Root.G                  = ToPublish.G;
	Root.DescriptorDigest   = Digest;
	Root.DescriptorLength   = Length;
	Root.PublishedUtcMillis = UtcMillis;

	TArray<uint8> RootBody;
	const ETerrainPersistError RootError = TerrainPersistEncodeRootSlotBody(Root, RootBody);
	if (RootError != ETerrainPersistError::None)
	{
		return FTerrainStoreResult::Bad(RootError);
	}

	// ...and only then the root slot, into the slot that is NOT current.
	const ETerrainStorageResult PublishResult = RootSlots.Publish(State.Identity, RootBody);
	if (PublishResult != ETerrainStorageResult::Ok)
	{
		// The other slot still names the previous checkpoint and is untouched. A lost
		// checkpoint, not a lost world.
		UE_LOG(LogTerrainCore, Error,
			TEXT("World store: publishing root generation %llu failed (%s). The previous root is ")
			TEXT("intact and remains current."),
			Root.Generation, TerrainStorageResultName(PublishResult));
		return FTerrainStoreResult::Io(PublishResult);
	}

	State.Root       = Root;
	State.Checkpoint = ToPublish;

	// **Re-read both slots, do not assume what they now hold.** Publication wrote the inactive
	// slot, so the pair has changed: the slot that was current is now the previous generation,
	// and the one just written is current. `State.RootSlots` was previously filled only by
	// Open(), which meant that after any capture it described the world as it was at startup.
	//
	// Keep diagnostic state current. Retention also reads the disk slots independently before
	// each pass, because later damage or a failed publication can invalidate this cache.
	int32 BestIndex = INDEX_NONE;
	const ETerrainStorageResult Reread =
		RootSlots.Read(State.Identity, State.RootSlots[0], State.RootSlots[1], BestIndex);
	if (Reread == ETerrainStorageResult::Ok)
	{
		State.bRootRedundancyIntact = State.RootSlots[0].bValid && State.RootSlots[1].bValid;
	}
	else
	{
		// The checkpoint IS published and durable; only our picture of the slots is uncertain.
		// Stop claiming redundancy; reclamation must validate the actual pair (P-004 §8).
		State.bRootRedundancyIntact = false;
		UE_LOG(LogTerrainCore, Warning,
			TEXT("World store: root generation %llu published, but the slot pair could not be ")
			TEXT("re-read (%s). Reclamation must revalidate both on-disk slots before deletion."),
			Root.Generation, TerrainStorageResultName(Reread));
	}

	return FTerrainStoreResult::Ok();
}

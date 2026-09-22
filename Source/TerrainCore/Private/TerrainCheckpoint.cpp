// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainCheckpoint.h"

#include "TerrainCore.h"
#include "TerrainChunk.h"
#include "TerrainPersistenceIndex.h"

/**
 * Capturing and restoring a consistent global cut (P-003 §4, §3).
 */

void FTerrainResidencyPins::Pin(
	ITerrainBackend& Backend, const FTerrainChunkKey& Key, const FTerrainBaseDescriptor& Base)
{
	if (Pinned.Contains(Key))
	{
		return;
	}

	const uint32 InterestId = BaseInterestId + static_cast<uint32>(Pinned.Num());
	Pinned.Add(Key, InterestId);

	const double VoxelCm = double(Base.VoxelSizeMicrometres) / 10000.0;
	const FTerrainBox Bounds = TerrainChunkBounds(Key);

	FTerrainStreamingInterest Interest;
	Interest.InterestId = InterestId;
	Interest.WorldLocation = FVector(
		double(Base.OriginWorldMicrometres[0]) / 10000.0
			+ (double(Bounds.Min.X) + TerrainChunkSizeVox * 0.5) * VoxelCm,
		double(Base.OriginWorldMicrometres[1]) / 10000.0
			+ (double(Bounds.Min.Y) + TerrainChunkSizeVox * 0.5) * VoxelCm,
		double(Base.OriginWorldMicrometres[2]) / 10000.0
			+ (double(Bounds.Min.Z) + TerrainChunkSizeVox * 0.5) * VoxelCm);
	// Half a chunk's diagonal, so the sphere contains the whole cube.
	Interest.RadiusCm   = TerrainChunkSizeVox * VoxelCm * 0.87;
	// Residency only. A pin exists so a dirty chunk can still be READ by capture, restore and
	// replay; it is not a player, and nothing needs collision or navmesh there. Asking for
	// collision made the plugin cook and keep collision and navmesh for every chunk ever edited,
	// and under capture that contention produced 160-310 ms server frames (T-132).
	Interest.bCollision = false;
	Interest.bRender    = false;

	Backend.SetStreamingInterest(Interest);
}

// ---- the incremental capture pump (P-003 §4, DEF-2) ---------------------

FTerrainStoreResult FTerrainCapturePump::Begin(
	FTerrainWorldStore& InStore,
	ITerrainBackend& InBackend,
	const FTerrainRevisionIndex& InRevisions,
	TMap<FTerrainChunkKey, FTerrainOpSeq>&& DirtyKeys,
	FTerrainOpSeq InG,
	FTerrainOpSeq SettledThrough,
	int64 InUtcMillis)
{
	checkf(!bActive, TEXT("A capture is already in progress on this pump."));

	CaptureStats = FTerrainCheckpointStats();
	CaptureStats.G = InG;
	CaptureStats.DirtyKeys = DirtyKeys.Num();
	FinalResult = FTerrainStoreResult::Ok();
	bFinished = false;
	bCompletionPending = false;
	Updates.Reset();

	// Owned from here on, whatever happens next: a refusal below still has to hand these back,
	// because the caller has already cleared its own dirty set.
	CutKeys = MoveTemp(DirtyKeys);

	const auto Refuse = [this](const FTerrainStoreResult& Why)
	{
		FinalResult = Why;
		bFinished = true;
		bCompletionPending = true;
		return Why;
	};

	if (!InStore.IsOpen())
	{
		return Refuse(FTerrainStoreResult::Io(ETerrainStorageResult::NotFound));
	}

	// A cut may repeat when nothing changed, but it may never go backwards -- a checkpoint
	// older than the current one would make replay redo work it has already durably passed.
	if (InG < InStore.GetState().Checkpoint.G
		|| !InStore.GetJournal()
		|| InG != InStore.GetJournal()->GetHead())
	{
		return Refuse(FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation));
	}
	if (CutKeys.Num() > TerrainCheckpointDirtyHardBound)
	{
		return Refuse(FTerrainStoreResult::Bad(ETerrainPersistError::FieldOutOfRange));
	}

	Store     = &InStore;
	Backend   = &InBackend;
	Revisions = &InRevisions;
	G         = InG;
	PrevG     = InStore.GetState().Checkpoint.G;
	UtcMillis = InUtcMillis;
	Pending   = CutKeys;

	StartedAt = FPlatformTime::Seconds();

	// Everything this capture writes goes into ONE pack with ONE flush, committed by
	// PublishCheckpoint immediately before the root slot (P-004 §13). The batch stays open
	// across frames for as long as the capture runs.
	Store->GetObjects().BeginBatch();

	bActive = true;

	// An empty dirty set is a legitimate capture: it republishes the same logical checkpoint
	// at a newer G, which is what lets a quiet world stop replaying the tail it has already
	// checkpointed. It still publishes a root, so it still waits for W: committed no-change
	// edits make an empty cut ahead of settlement an ordinary case, not a corner.
	if (Pending.Num() == 0)
	{
		TryPublish(SettledThrough);
	}
	return FinalResult;
}

bool FTerrainCapturePump::TryPublish(FTerrainOpSeq SettledThrough)
{
	check(bActive && Pending.Num() == 0);
	if (SettledThrough < G)
	{
		return false;
	}
	Finish();
	return true;
}

void FTerrainCapturePump::NoticeWrite(TConstArrayView<FTerrainChunkKey> Keys)
{
	if (!bActive || Pending.Num() == 0)
	{
		return;
	}

	for (const FTerrainChunkKey& Key : Keys)
	{
		if (const FTerrainOpSeq* LastOpSeq = Pending.Find(Key))
		{
			// Encoded from its PRE-EDIT state, because the caller has not applied the edit yet.
			// This is copy-before-write: the chunk leaves the pending set having been recorded
			// as it was at G, and the edit is then free to change it.
			const FTerrainOpSeq Seq = *LastOpSeq;
			if (!CaptureOne(Key, Seq))
			{
				return;   // Fail() has already ended the capture
			}
			++CaptureStats.CopiedBeforeWrite;
		}
	}
}

bool FTerrainCapturePump::Advance(double BudgetSeconds, FTerrainOpSeq SettledThrough)
{
	if (!bActive)
	{
		return bFinished;
	}

	const double Deadline = FPlatformTime::Seconds() + FMath::Max(0.0, BudgetSeconds);
	bool bDidOne = false;
	while (Pending.Num() > 0)
	{
		// **At least one chunk per call, always.** The budget is checked between chunks -- a
		// chunk is never half encoded, so one chunk may overrun it -- but a pump that can make
		// zero progress is a pump that can never finish, and a budget smaller than one chunk's
		// cost would starve the capture forever. Forward progress is not negotiable; the budget
		// only decides how much MORE than one chunk a frame does.
		if (bDidOne && FPlatformTime::Seconds() >= Deadline)
		{
			return false;
		}
		bDidOne = true;

		const auto It = Pending.CreateConstIterator();
		const FTerrainChunkKey Key = It.Key();
		const FTerrainOpSeq    Seq = It.Value();
		if (!CaptureOne(Key, Seq))
		{
			return true;   // failed and finished
		}
	}

	// Every chunk is encoded. Publication waits, if it must, for settlement to reach the cut.
	// Finish can still fail, and that is an end too: either way the capture is over.
	return TryPublish(SettledThrough);
}

bool FTerrainCapturePump::CaptureOne(const FTerrainChunkKey& Key, FTerrainOpSeq LastOpSeq)
{
	check(bActive);

	const FTerrainPersistIdentity& Identity = Store->GetState().Identity;
	const FTerrainBaseDescriptor&  Base     = Store->GetState().Base;

	// P-003 §4's sentinel trap, guarded in the only place it can be: a nonresident chunk reads
	// as a successful default Empty on one backend and as a failure on another, and NEITHER
	// means "this chunk still matches the base".
	if (!Backend->IsRegionResident(Key))
	{
		UE_LOG(LogTerrainCore, Error,
			TEXT("Checkpoint: chunk (%d,%d,%d) is dirty but not resident. It cannot be read, ")
			TEXT("and publishing it as unchanged would lose the edits that dirtied it."),
			Key.X, Key.Y, Key.Z);
		Fail(FTerrainStoreResult::Bad(ETerrainPersistError::EncodingNotPermitted));
		return false;
	}

	FTerrainRegionData Region;
	const double ReadStarted = FPlatformTime::Seconds();
	const bool bRead = Backend->ReadRegion(Key, Region);
	CaptureStats.ReadSeconds += FPlatformTime::Seconds() - ReadStarted;

	if (!bRead
		|| Region.Encoding != ETerrainRegionEncoding::Dense
		|| Region.Payload.Num() != TerrainPersistDenseBytes)
	{
		UE_LOG(LogTerrainCore, Error,
			TEXT("Checkpoint: chunk (%d,%d,%d) did not read back as a complete Dense region."),
			Key.X, Key.Y, Key.Z);
		Fail(FTerrainStoreResult::Bad(ETerrainPersistError::EncodingNotPermitted));
		return false;
	}

	const double EncodeStarted = FPlatformTime::Seconds();

	FTerrainChunkPayloadRecord Record;
	Record.Key              = Key;
	Record.Rev              = Revisions->GetRevision(Key);
	Record.LastOpSeq        = LastOpSeq;
	Record.Encoding         = ETerrainRegionEncoding::Dense;
	Record.GeneratorVersion = Base.GeneratorVersion;
	Record.ValueConfig      = Base.ValueConfig;
	Record.Dense            = MoveTemp(Region.Payload);

	if (Record.Rev == 0 || LastOpSeq <= PrevG || LastOpSeq > G)
	{
		Fail(FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation));
		return false;
	}

	TArray<uint8> Body;
	const ETerrainPersistError BodyError = TerrainPersistEncodeChunkPayloadBody(Record, Body);
	if (BodyError != ETerrainPersistError::None)
	{
		Fail(FTerrainStoreResult::Bad(BodyError));
		return false;
	}

	TArray<uint8> Object;
	const ETerrainPersistError ObjectError = TerrainPersistEncodeObject(
		ETerrainPersistObjectType::ChunkPayload, Identity, Body, Object);
	if (ObjectError != ETerrainPersistError::None)
	{
		Fail(FTerrainStoreResult::Bad(ObjectError));
		return false;
	}

	const FTerrainDigest Digest = TerrainPersistDigest(Object);
	CaptureStats.EncodeSeconds += FPlatformTime::Seconds() - EncodeStarted;

	const double StoreStarted = FPlatformTime::Seconds();
	const bool bStored = Store->GetObjects().StoreObject(Digest, Object);
	CaptureStats.StoreSeconds += FPlatformTime::Seconds() - StoreStarted;
	if (!bStored)
	{
		Fail(FTerrainStoreResult::Io(ETerrainStorageResult::IoError));
		return false;
	}

	FTerrainIndexUpdate Update;
	Update.Key                 = Key;
	Update.Value.Encoding      = ETerrainRegionEncoding::Dense;
	Update.Value.Rev           = Record.Rev;
	Update.Value.LastOpSeq     = Record.LastOpSeq;
	Update.Value.PayloadLength = static_cast<uint32>(Object.Num());
	Update.Value.PayloadDigest = Digest;
	Updates.Add(Update);

	++CaptureStats.ChunksWritten;
	CaptureStats.PayloadBytes += Object.Num();

	Pending.Remove(Key);
	return true;
}

void FTerrainCapturePump::Finish()
{
	check(bActive);

	const FTerrainPersistIdentity& Identity = Store->GetState().Identity;

	// --- the index, path-copied from the checkpoint currently published ---------------------
	FTerrainIndexRoot OldRoot;
	OldRoot.bHasRootPage   = Store->GetState().Checkpoint.bHasRootPage;
	OldRoot.RootPageDigest = Store->GetState().Checkpoint.RootPageDigest;
	OldRoot.RootPageLength = Store->GetState().Checkpoint.RootPageLength;

	FTerrainIndexRoot NewRoot;
	const double IndexStarted = FPlatformTime::Seconds();
	const ETerrainPersistError IndexError = TerrainIndexApply(
		Identity, Store->GetObjects(), Store->GetObjects(), OldRoot, Updates,
		NewRoot, CaptureStats.IndexPagesWritten);
	CaptureStats.IndexSeconds = FPlatformTime::Seconds() - IndexStarted;
	if (IndexError != ETerrainPersistError::None)
	{
		Fail(FTerrainStoreResult::Bad(IndexError));
		return;
	}

	// --- the descriptor, and only then the root slot ------------------------------------------
	int64 LeafCount = 0;
	int64 TotalPayloadBytes = 0;
	const ETerrainPersistError WalkError = TerrainIndexEnumerate(
		Identity, Store->GetObjects(), NewRoot,
		[&LeafCount, &TotalPayloadBytes](const FTerrainChunkKey&, const FTerrainIndexLeafValue& Value)
		{
			++LeafCount;
			TotalPayloadBytes += Value.PayloadLength;
			return true;
		});
	if (WalkError != ETerrainPersistError::None)
	{
		Fail(FTerrainStoreResult::Bad(WalkError));
		return;
	}

	FTerrainCheckpointDescriptor Descriptor;
	Descriptor.G                 = G;
	Descriptor.bHasRootPage      = NewRoot.bHasRootPage;
	Descriptor.RootPageDigest    = NewRoot.RootPageDigest;
	Descriptor.RootPageLength    = NewRoot.RootPageLength;
	Descriptor.LeafKeyCount      = static_cast<uint64>(LeafCount);
	Descriptor.TotalPayloadBytes = static_cast<uint64>(TotalPayloadBytes);

	const double PublishStarted = FPlatformTime::Seconds();
	const FTerrainStoreResult Published = Store->PublishCheckpoint(Descriptor, UtcMillis);
	CaptureStats.PublishSeconds = FPlatformTime::Seconds() - PublishStarted;
	if (!Published.IsOk())
	{
		// Everything written above is unreferenced by any root: garbage to be collected, not a
		// broken world. That is P-004 §12's containment argument, and this is where it applies.
		Fail(Published);
		return;
	}

	CaptureStats.Generation = Store->GetState().Root.Generation;
	CaptureStats.Seconds    = FPlatformTime::Seconds() - StartedAt;

	UE_LOG(LogTerrainCore, Log,
		TEXT("Checkpoint published at G=%llu generation=%llu: %d chunks (%lld bytes), %d index pages, ")
		TEXT("%lld keys total, in %.3f s of work over %.3f s wall (read %.3f, encode %.3f, ")
		TEXT("store %.3f, index %.3f, publish %.3f), %d taken by copy-before-write."),
		G, CaptureStats.Generation, CaptureStats.ChunksWritten, CaptureStats.PayloadBytes,
		CaptureStats.IndexPagesWritten, LeafCount, CaptureStats.WorkSeconds(), CaptureStats.Seconds,
		CaptureStats.ReadSeconds, CaptureStats.EncodeSeconds, CaptureStats.StoreSeconds,
		CaptureStats.IndexSeconds, CaptureStats.PublishSeconds, CaptureStats.CopiedBeforeWrite);

	if (CaptureStats.UnspreadSeconds() > 0.5)
	{
		// The pump spreads reading, encoding and buffering across frames; the index path-copy
		// and publication are still one unbroken step, so they are the only part that can be
		// felt. At the 256-chunk trigger that measured 0.041 s. Warning above half a second
		// leaves room for a much larger dirty set before crying wolf, and stays well under the
		// multi-second stall P-003 §4 actually fails.
		UE_LOG(LogTerrainCore, Warning,
			TEXT("Checkpoint publication held the game thread for %.2f s (index %.3f, publish ")
			TEXT("%.3f) over %d chunks. Reading and encoding were spread across frames, so this ")
			TEXT("is the part that is still one step. P-003 §4 fails a visible multi-second ")
			TEXT("stall, so this is heading for it."),
			CaptureStats.UnspreadSeconds(), CaptureStats.IndexSeconds,
			CaptureStats.PublishSeconds, CaptureStats.ChunksWritten);
	}

	bActive = false;
	bFinished = true;
	bCompletionPending = true;
	FinalResult = FTerrainStoreResult::Ok();
	CutKeys.Reset();   // published: the cut's history is durable in the new root
}

void FTerrainCapturePump::Fail(const FTerrainStoreResult& Why)
{
	FinalResult = Why;
	CaptureStats.Seconds = FPlatformTime::Seconds() - StartedAt;
	bActive = false;
	bFinished = true;
	bCompletionPending = true;
	// CutKeys is deliberately kept: nothing of this cut was published, and the owner must take
	// it back (TakeCut) or those chunks would be dirty nowhere.
	Pending.Reset();
	Updates.Reset();
	if (Store != nullptr)
	{
		Store->GetObjects().AbandonBatch();
	}
}

void FTerrainCapturePump::Abandon()
{
	if (!bActive)
	{
		return;
	}
	bActive = false;
	bFinished = false;
	bCompletionPending = false;
	CutKeys.Reset();   // only at teardown: the world is closing, and the journal holds it all
	Pending.Reset();
	Updates.Reset();
	if (Store != nullptr)
	{
		Store->GetObjects().AbandonBatch();
	}
}

// ---- the synchronous entry point, which is the pump run to completion ---

FTerrainStoreResult TerrainCaptureCheckpoint(
	FTerrainWorldStore& Store,
	ITerrainBackend& Backend,
	const FTerrainRevisionIndex& Revisions,
	const TMap<FTerrainChunkKey, FTerrainOpSeq>& DirtyKeys,
	FTerrainOpSeq G,
	int64 UtcMillis,
	FTerrainCheckpointStats& OutStats)
{
	// Deliberately the same code path as the incremental pump, with an unlimited budget, so the
	// two cannot drift apart. Everything that tested synchronous capture now tests the pump.
	FTerrainCapturePump Pump;
	TMap<FTerrainChunkKey, FTerrainOpSeq> Keys = DirtyKeys;

	// No ledger, so nothing to wait for (see the header).
	const FTerrainStoreResult Started =
		Pump.Begin(Store, Backend, Revisions, MoveTemp(Keys), G, MAX_uint64, UtcMillis);
	if (!Started.IsOk())
	{
		OutStats = Pump.Stats();
		return Started;
	}

	while (!Pump.Advance(TNumericLimits<double>::Max(), MAX_uint64))
	{
	}

	OutStats = Pump.Stats();
	return Pump.Result();
}

FTerrainStoreResult TerrainRestoreCheckpoint(
	FTerrainWorldStore& Store,
	ITerrainBackend& Backend,
	FTerrainRevisionIndex& Revisions,
	FTerrainRestoreStats& OutStats)
{
	OutStats = FTerrainRestoreStats();
	const double Started = FPlatformTime::Seconds();

	if (!Store.IsOpen())
	{
		return FTerrainStoreResult::Io(ETerrainStorageResult::NotFound);
	}

	const FTerrainPersistIdentity& Identity = Store.GetState().Identity;
	const FTerrainBaseDescriptor&  Base     = Store.GetState().Base;

	FTerrainIndexRoot Root;
	Root.bHasRootPage   = Store.GetState().Checkpoint.bHasRootPage;
	Root.RootPageDigest = Store.GetState().Checkpoint.RootPageDigest;
	Root.RootPageLength = Store.GetState().Checkpoint.RootPageLength;
	if (!Root.bHasRootPage)
	{
		// No root can represent an untouched world, including committed no-change ops.
		if (Store.GetState().Checkpoint.LeafKeyCount != 0 || Store.GetState().Checkpoint.TotalPayloadBytes != 0)
			return FTerrainStoreResult::Bad(ETerrainPersistError::FieldOutOfRange);
		return FTerrainStoreResult::Ok();
	}

	// Collect first, restore second. The walk holds pages open, and WriteRegion can reach into
	// a backend that allocates -- keeping the two apart keeps each one's failure its own.
	TArray<TPair<FTerrainChunkKey, FTerrainIndexLeafValue>> Leaves;
	const ETerrainPersistError WalkError = TerrainIndexEnumerate(
		Identity, Store.GetObjects(), Root,
		[&Leaves](const FTerrainChunkKey& Key, const FTerrainIndexLeafValue& Value)
		{
			Leaves.Emplace(Key, Value);
			return true;
		});
	if (WalkError != ETerrainPersistError::None)
	{
		return FTerrainStoreResult::Bad(WalkError);
	}

	uint64 TotalPayloadBytes = 0;
	for (const auto& Leaf : Leaves)
	{
		TotalPayloadBytes += Leaf.Value.PayloadLength;
		const int32 Coords[] = {Leaf.Key.X, Leaf.Key.Y, Leaf.Key.Z};
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const int64 Min = int64(Coords[Axis]) * TerrainChunkSizeVox;
			if (Min >= Base.WorldBoundsVox.Max[Axis] || Min + TerrainChunkSizeVox <= Base.WorldBoundsVox.Min[Axis])
				return FTerrainStoreResult::Bad(ETerrainPersistError::FieldOutOfRange);
		}
	}
	if (uint64(Leaves.Num()) != Store.GetState().Checkpoint.LeafKeyCount
		|| TotalPayloadBytes != Store.GetState().Checkpoint.TotalPayloadBytes)
	{
		return FTerrainStoreResult::Bad(ETerrainPersistError::FieldOutOfRange);
	}

	TArray<TPair<FTerrainChunkKey, FTerrainRev>> RestoredRevisions;
	RestoredRevisions.Reserve(Leaves.Num());

	// One retained pin per chunk. A single interest walked across the chunks would leave every
	// one behind it unresident, and unresident is evictable -- the restore would discard itself
	// as it went. See FTerrainResidencyPins.
	FTerrainResidencyPins Pins(TerrainRestoreInterestId);

	for (const TPair<FTerrainChunkKey, FTerrainIndexLeafValue>& Leaf : Leaves)
	{
		if (Leaf.Value.Rev == 0 || Leaf.Value.LastOpSeq == 0
			|| Leaf.Value.LastOpSeq > Store.GetState().Checkpoint.G)
		{
			return FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
		}
		RestoredRevisions.Emplace(Leaf.Key, Leaf.Value.Rev);

		if (Leaf.Value.Encoding != ETerrainRegionEncoding::Dense)
		{
			// This prototype has no exact baseline decoder for Empty or SparseDiff.
			return FTerrainStoreResult::Bad(ETerrainPersistError::EncodingNotPermitted);
		}

		TArray<uint8> Object;
		if (!Store.GetObjects().LoadObject(Leaf.Value.PayloadDigest, Object))
		{
			UE_LOG(LogTerrainCore, Error,
				TEXT("Checkpoint restore: chunk (%d,%d,%d) references a payload that is missing ")
				TEXT("or does not hash to its name."),
				Leaf.Key.X, Leaf.Key.Y, Leaf.Key.Z);
			return FTerrainStoreResult::Io(ETerrainStorageResult::NotFound);
		}
		if (static_cast<uint32>(Object.Num()) != Leaf.Value.PayloadLength)
		{
			return FTerrainStoreResult::Bad(ETerrainPersistError::BodyLengthOutOfRange);
		}

		FTerrainPersistObjectHeader Header;
		TArrayView<const uint8> Body;
		const ETerrainPersistError ObjectError = TerrainPersistDecodeObject(
			Object, ETerrainPersistObjectType::ChunkPayload, &Identity, Header, Body);
		if (ObjectError != ETerrainPersistError::None)
		{
			return FTerrainStoreResult::Bad(ObjectError);
		}

		FTerrainChunkPayloadRecord Record;
		const ETerrainPersistError BodyError = TerrainPersistDecodeChunkPayloadBody(
			Body, Base.GeneratorVersion, Base.ValueConfig, Record);
		if (BodyError != ETerrainPersistError::None)
		{
			return FTerrainStoreResult::Bad(BodyError);
		}
		if (!(Record.Key == Leaf.Key) || Record.Rev != Leaf.Value.Rev
			|| Record.LastOpSeq != Leaf.Value.LastOpSeq || Record.Encoding != Leaf.Value.Encoding)
		{
			// The index said this payload belongs to a different chunk than it claims.
			return FTerrainStoreResult::Bad(ETerrainPersistError::FieldOutOfRange);
		}

		Pins.Pin(Backend, Leaf.Key, Base);

		// §4.2: SparseDiff and Empty decode to full samples before a Dense-only backend is
		// called. Only Dense is written today (see the header), so this is a direct handoff
		// and the decode step arrives with the encodings that need it.
		FTerrainRegionData Region;
		Region.Key              = Leaf.Key;
		Region.Rev              = Record.Rev;
		Region.LastOpSeq        = Record.LastOpSeq;
		Region.Encoding         = ETerrainRegionEncoding::Dense;
		Region.GeneratorVersion = Base.GeneratorVersion;
		Region.ValueConfig      = Base.ValueConfig;
		Region.Payload          = MoveTemp(Record.Dense);

		if (!Backend.WriteRegion(Region))
		{
			UE_LOG(LogTerrainCore, Error,
				TEXT("Checkpoint restore: the backend refused chunk (%d,%d,%d)."),
				Leaf.Key.X, Leaf.Key.Y, Leaf.Key.Z);
			return FTerrainStoreResult::Bad(ETerrainPersistError::BaseMismatch);
		}

		++OutStats.ChunksRestored;
		OutStats.PayloadBytes += Object.Num();
	}

	if (!Revisions.SeedRevisions(RestoredRevisions))
	{
		// Either the index was not empty, or a checkpoint recorded a chunk at revision 0.
		return FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
	}

	OutStats.Seconds = FPlatformTime::Seconds() - Started;
	UE_LOG(LogTerrainCore, Log,
		TEXT("Checkpoint restored at G=%llu: %d chunks (%lld bytes) of %d recorded keys in %.3f s."),
		Store.GetState().Checkpoint.G, OutStats.ChunksRestored, OutStats.PayloadBytes,
		Leaves.Num(), OutStats.Seconds);
	return FTerrainStoreResult::Ok();
}

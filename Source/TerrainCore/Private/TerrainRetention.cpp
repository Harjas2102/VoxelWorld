// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainRetention.h"

#include "TerrainCore.h"
#include "TerrainPersistenceIndex.h"
#include "TerrainPersistenceRecords.h"

namespace
{
	// Use the same structural traversal as restore, recording every successfully loaded
	// dependency. A second, looser page walker must not authorize deletion of recovery data.
	class FMarkingObjectStore final : public ITerrainObjectStore
	{
	public:
		FMarkingObjectStore(const ITerrainObjectStore& InSource, TSet<FTerrainDigest>& InLive)
			: Source(InSource), Live(InLive) {}

		virtual bool LoadObject(const FTerrainDigest& Digest, TArray<uint8>& Out) const override
		{
			if (!Source.LoadObject(Digest, Out)) { return false; }
			Live.Add(Digest);
			return true;
		}
		virtual bool StoreObject(const FTerrainDigest&, TArrayView<const uint8>) override
		{
			return false;
		}
	private:
		const ITerrainObjectStore& Source;
		TSet<FTerrainDigest>& Live;
	};

	ETerrainPersistError MarkCheckpoint(
		const FTerrainPersistIdentity& Identity,
		const FTerrainBaseDescriptor& Base,
		const ITerrainObjectStore& Objects,
		const FTerrainRootSlot& Slot,
		TSet<FTerrainDigest>& Live)
	{
		FMarkingObjectStore Marked(Objects, Live);
		TArray<uint8> Object;
		if (!Marked.LoadObject(Slot.DescriptorDigest, Object))
		{
			return ETerrainPersistError::ShortBuffer;
		}
		if (uint32(Object.Num()) != Slot.DescriptorLength)
		{
			return ETerrainPersistError::BodyLengthOutOfRange;
		}
		FTerrainPersistObjectHeader Header;
		TArrayView<const uint8> Body;
		ETerrainPersistError Error = TerrainPersistDecodeObject(
			Object, ETerrainPersistObjectType::CheckpointDescriptor, &Identity, Header, Body);
		if (Error != ETerrainPersistError::None) { return Error; }
		FTerrainCheckpointDescriptor Descriptor;
		Error = TerrainPersistDecodeCheckpointBody(Body, Descriptor);
		if (Error != ETerrainPersistError::None) { return Error; }
		if (Descriptor.G != Slot.G || Descriptor.Generation != Slot.Generation)
		{
			return ETerrainPersistError::FieldOutOfRange;
		}

		FTerrainIndexRoot Root;
		Root.bHasRootPage = Descriptor.bHasRootPage;
		Root.RootPageDigest = Descriptor.RootPageDigest;
		Root.RootPageLength = Descriptor.RootPageLength;
		ETerrainPersistError PayloadError = ETerrainPersistError::None;
		const ETerrainPersistError WalkError = TerrainIndexEnumerate(
			Identity, Marked, Root,
			[&](const FTerrainChunkKey& Key, const FTerrainIndexLeafValue& Value)
			{
				if (Value.Rev == 0 || Value.LastOpSeq == 0 || Value.LastOpSeq > Descriptor.G)
				{
					PayloadError = ETerrainPersistError::OrderViolation;
					return false;
				}
				const int32 Coordinates[] = {Key.X, Key.Y, Key.Z};
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					const int64 Min = int64(Coordinates[Axis]) * TerrainChunkSizeVox;
					if (Min >= Base.WorldBoundsVox.Max[Axis]
						|| Min + TerrainChunkSizeVox <= Base.WorldBoundsVox.Min[Axis])
					{
						PayloadError = ETerrainPersistError::FieldOutOfRange;
						return false;
					}
				}
				if (Value.Encoding == ETerrainRegionEncoding::Empty) { return true; }
				TArray<uint8> Payload;
				if (!Marked.LoadObject(Value.PayloadDigest, Payload))
				{
					PayloadError = ETerrainPersistError::ShortBuffer;
					return false;
				}
				if (uint32(Payload.Num()) != Value.PayloadLength)
				{
					PayloadError = ETerrainPersistError::BodyLengthOutOfRange;
					return false;
				}
				FTerrainPersistObjectHeader PayloadHeader;
				TArrayView<const uint8> PayloadBody;
				PayloadError = TerrainPersistDecodeObject(Payload,
					ETerrainPersistObjectType::ChunkPayload, &Identity, PayloadHeader, PayloadBody);
				if (PayloadError != ETerrainPersistError::None) { return false; }
				FTerrainChunkPayloadRecord Record;
				PayloadError = TerrainPersistDecodeChunkPayloadBody(
					PayloadBody, Base.GeneratorVersion, Base.ValueConfig, Record);
				if (PayloadError != ETerrainPersistError::None) { return false; }
				if (!(Record.Key == Key) || Record.Rev != Value.Rev
					|| Record.LastOpSeq != Value.LastOpSeq || Record.Encoding != Value.Encoding)
				{
					PayloadError = ETerrainPersistError::FieldOutOfRange;
					return false;
				}
				return true;
			});
		return WalkError != ETerrainPersistError::None ? WalkError : PayloadError;
	}
}

FTerrainStoreResult TerrainReclaimStore(
	FTerrainWorldStore& Store, FTerrainRetentionStats& OutStats)
{
	OutStats = FTerrainRetentionStats();
	const double Started = FPlatformTime::Seconds();

	if (!Store.IsOpen())
	{
		return FTerrainStoreResult::Io(ETerrainStorageResult::NotFound);
	}

	// Rule 3, checked first because it is the cheapest and the most embarrassing to get wrong:
	// a capture in flight has objects buffered and pages written that no root names yet.
	if (Store.GetObjects().IsBatchOpen())
	{
		UE_LOG(LogTerrainCore, Warning,
			TEXT("Retention refused: a checkpoint capture is in flight. Its objects are not yet ")
			TEXT("named by any root, so a sweep would be entitled to delete them."));
		return FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
	}

	const FTerrainPersistIdentity& Identity = Store.GetState().Identity;
	FTerrainFileObjectStore& Objects = Store.GetObjects();

	// Publication can fail or a slot can be damaged since Open(). Read the actual pair
	// before every pass; cached redundancy cannot authorize a destructive operation.
	FTerrainSlotPair DiskRoots(Store.GetDevice(), ETerrainPersistObjectType::RootSlot,
		TerrainStoragePaths::RootSlot(0), TerrainStoragePaths::RootSlot(1));
	FTerrainSlotState Slots[2];
	int32 Best = INDEX_NONE;
	const ETerrainStorageResult Read = DiskRoots.Read(Identity, Slots[0], Slots[1], Best);
	if (Read != ETerrainStorageResult::Ok) { return FTerrainStoreResult::Io(Read); }
	if (!Slots[0].bValid || !Slots[1].bValid)
	{
		UE_LOG(LogTerrainCore, Warning, TEXT("Retention refused: both root slots must validate."));
		return FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
	}

	// --- mark, from BOTH slots (rule 2) -------------------------------------------------------
	TSet<FTerrainDigest> Live;
	for (int32 SlotIndex = 0; SlotIndex < 2; ++SlotIndex)
	{
		const FTerrainSlotState& Slot = Slots[SlotIndex];
		if (!Slot.bValid)
		{
			// Unreachable while redundancy is intact, but stated rather than assumed.
			continue;
		}

		FTerrainRootSlot Root;
		const ETerrainPersistError RootError = TerrainPersistDecodeRootSlotBody(Slot.Body, Root);
		if (RootError != ETerrainPersistError::None)
		{
			return FTerrainStoreResult::Bad(RootError);
		}
		if (!Store.GetJournal() || Root.G > Store.GetJournal()->GetHead())
		{
			return FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
		}

		const ETerrainPersistError MarkError =
			MarkCheckpoint(Identity, Store.GetState().Base, Objects, Root, Live);
		if (MarkError != ETerrainPersistError::None)
		{
			// Nothing has been deleted. An incomplete mark looks exactly like a small live set,
			// and sweeping on one would delete live data -- so a failed mark fails the pass.
			UE_LOG(LogTerrainCore, Error,
				TEXT("Retention refused: the live set could not be walked from root slot %d ")
				TEXT("(%s). Nothing was deleted."),
				SlotIndex, TerrainPersistErrorName(MarkError));
			return FTerrainStoreResult::Bad(MarkError);
		}
	}
	OutStats.LiveObjects = Live.Num();

	/**
	 * The mark must contain the store's own current checkpoint. This is cheap, and it is the
	 * check that turns a silent catastrophe into a refusal.
	 *
	 * The first time reclamation ran, it marked from `State.RootSlots`, which at that point was
	 * filled only by `Open()` -- so after three captures it still described the empty checkpoint
	 * the world was created with. One object was marked live and every pack was deleted. The
	 * root cause is fixed (publication now re-reads the pair), but a sweep that deletes data on
	 * the strength of cached state should verify that state against something it holds
	 * independently, and `State.Root` is exactly that.
	 */
	if (!Live.Contains(Store.GetState().Root.DescriptorDigest))
	{
		UE_LOG(LogTerrainCore, Error,
			TEXT("Retention refused: the live set walked from the root slots does not contain the ")
			TEXT("current checkpoint (generation %llu, G=%llu). The slots and the open store ")
			TEXT("disagree, so nothing here can be trusted to be garbage. Nothing was deleted."),
			Store.GetState().Root.Generation, Store.GetState().Root.G);
		return FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
	}

	// --- 1. pre-P-005 files: copy what is live into a container, then remove them --------------
	// Reclaimed is NET: the live objects are re-written into a container, so deleting the old
	// files frees only what was dead in them. Counting the deletions alone would report a world
	// that merely changed layout as having shrunk by its whole size.
	int64 LegacyBytes = 0;
	const int32 MigrationTarget = Objects.GetActiveContainer();
	const int64 TargetBefore = Objects.ContainerValidEnd(MigrationTarget);
	const ETerrainStorageResult Migrated = Objects.MigrateLegacy(
		Live, OutStats.LegacyObjectsMigrated, OutStats.LegacyFilesDeleted, LegacyBytes);
	const int64 MigrationGrowth = Objects.ContainerValidEnd(MigrationTarget) - TargetBefore;
	OutStats.BytesReclaimed += FMath::Max<int64>(LegacyBytes - MigrationGrowth, 0);
	if (Migrated != ETerrainStorageResult::Ok)
	{
		return FTerrainStoreResult::Io(Migrated);
	}

	// --- 2. move writing off a container that holds garbage, so it can be compacted -----------
	// Compaction never touches the active container: it would copy into itself and then cut
	// itself. With no empty container to move to, the active one simply waits for a later pass.
	if (Objects.ContainerDeadBytes(Objects.GetActiveContainer(), Live) > 0)
	{
		OutStats.bRotated = Objects.RotateActiveToEmpty();
	}

	// --- 3. compact every other container that holds dead bytes -------------------------------
	for (int32 Index = 0; Index < TerrainStoragePaths::ContainerCount; ++Index)
	{
		const int32 Active = Objects.GetActiveContainer();
		const int64 Before = Objects.ContainerValidEnd(Index);
		if (Index == Active || Before <= 0)
		{
			continue;
		}
		if (Objects.ContainerDeadBytes(Index, Live) == 0)
		{
			++OutStats.ContainersKept;
			continue;
		}

		const int64 ActiveBefore = Objects.ContainerValidEnd(Active);
		int32 Kept = 0, Dropped = 0;
		const ETerrainStorageResult Compacted = Objects.CompactContainer(Index, Live, Kept, Dropped);
		if (Compacted != ETerrainStorageResult::Ok)
		{
			return FTerrainStoreResult::Io(Compacted);
		}

		++OutStats.ContainersCompacted;
		OutStats.ObjectsDropped += Dropped;
		const int64 Grown = Objects.ContainerValidEnd(Active) - ActiveBefore;
		if (Before > Grown)
		{
			OutStats.BytesReclaimed += Before - Grown;
		}
	}

	OutStats.Seconds = FPlatformTime::Seconds() - Started;

	UE_LOG(LogTerrainCore, Log,
		TEXT("Retention: %d live objects; %d migrated from %d pre-P-005 files; rotated=%s; ")
		TEXT("%d containers compacted (%d dead objects dropped), %d untouched; %lld bytes reclaimed ")
		TEXT("in %.3f s."),
		OutStats.LiveObjects, OutStats.LegacyObjectsMigrated, OutStats.LegacyFilesDeleted,
		OutStats.bRotated ? TEXT("yes") : TEXT("no"), OutStats.ContainersCompacted,
		OutStats.ObjectsDropped, OutStats.ContainersKept, OutStats.BytesReclaimed, OutStats.Seconds);

	return FTerrainStoreResult::Ok();
}

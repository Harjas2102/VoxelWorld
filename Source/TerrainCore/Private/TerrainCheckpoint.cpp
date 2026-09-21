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
	Interest.bCollision = true;
	Interest.bRender    = false;

	Backend.SetStreamingInterest(Interest);
}

FTerrainStoreResult TerrainCaptureCheckpoint(
	FTerrainWorldStore& Store,
	ITerrainBackend& Backend,
	const FTerrainRevisionIndex& Revisions,
	const TMap<FTerrainChunkKey, FTerrainOpSeq>& DirtyKeys,
	FTerrainOpSeq G,
	int64 UtcMillis,
	FTerrainCheckpointStats& OutStats)
{
	OutStats = FTerrainCheckpointStats();
	OutStats.G = G;
	OutStats.DirtyKeys = DirtyKeys.Num();
	const double Started = FPlatformTime::Seconds();

	if (!Store.IsOpen())
	{
		return FTerrainStoreResult::Io(ETerrainStorageResult::NotFound);
	}

	const FTerrainPersistIdentity& Identity = Store.GetState().Identity;
	const FTerrainBaseDescriptor&  Base     = Store.GetState().Base;

	// A cut may repeat when nothing changed, but it may never go backwards -- a checkpoint
	// older than the current one would make replay redo work it has already durably passed.
	if (G < Store.GetState().Checkpoint.G || !Store.GetJournal() || G != Store.GetJournal()->GetHead())
	{
		return FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
	}
	if (DirtyKeys.Num() > TerrainCheckpointDirtyHardBound)
	{
		return FTerrainStoreResult::Bad(ETerrainPersistError::FieldOutOfRange);
	}

	// --- 1. every dirty chunk becomes an immutable payload object ---------------------------
	TArray<FTerrainIndexUpdate> Updates;
	Updates.Reserve(DirtyKeys.Num());

	for (const auto& Dirty : DirtyKeys)
	{
		const FTerrainChunkKey& Key = Dirty.Key;

		// P-003 §4's sentinel trap, guarded in the only place it can be: a nonresident chunk
		// reads as a successful default Empty on one backend and as a failure on another, and
		// NEITHER means "this chunk still matches the base".
		if (!Backend.IsRegionResident(Key))
		{
			UE_LOG(LogTerrainCore, Error,
				TEXT("Checkpoint: chunk (%d,%d,%d) is dirty but not resident. It cannot be read, ")
				TEXT("and publishing it as unchanged would lose the edits that dirtied it."),
				Key.X, Key.Y, Key.Z);
			return FTerrainStoreResult::Bad(ETerrainPersistError::EncodingNotPermitted);
		}

		FTerrainRegionData Region;
		const double ReadStarted = FPlatformTime::Seconds();
		const bool bRead = Backend.ReadRegion(Key, Region);
		OutStats.ReadSeconds += FPlatformTime::Seconds() - ReadStarted;
		if (!bRead
			|| Region.Encoding != ETerrainRegionEncoding::Dense
			|| Region.Payload.Num() != TerrainPersistDenseBytes)
		{
			UE_LOG(LogTerrainCore, Error,
				TEXT("Checkpoint: chunk (%d,%d,%d) did not read back as a complete Dense region."),
				Key.X, Key.Y, Key.Z);
			return FTerrainStoreResult::Bad(ETerrainPersistError::EncodingNotPermitted);
		}

		FTerrainChunkPayloadRecord Record;
		Record.Key              = Key;
		Record.Rev              = Revisions.GetRevision(Key);
		Record.LastOpSeq        = Dirty.Value;
		Record.Encoding         = ETerrainRegionEncoding::Dense;
		Record.GeneratorVersion = Base.GeneratorVersion;
		Record.ValueConfig      = Base.ValueConfig;
		Record.Dense            = MoveTemp(Region.Payload);
		if (Record.Rev == 0 || Dirty.Value <= Store.GetState().Checkpoint.G || Dirty.Value > G)
		{
			return FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
		}

		const double EncodeStarted = FPlatformTime::Seconds();
		TArray<uint8> Body;
		const ETerrainPersistError BodyError = TerrainPersistEncodeChunkPayloadBody(Record, Body);
		if (BodyError != ETerrainPersistError::None)
		{
			return FTerrainStoreResult::Bad(BodyError);
		}

		TArray<uint8> Object;
		const ETerrainPersistError ObjectError = TerrainPersistEncodeObject(
			ETerrainPersistObjectType::ChunkPayload, Identity, Body, Object);
		if (ObjectError != ETerrainPersistError::None)
		{
			return FTerrainStoreResult::Bad(ObjectError);
		}

		const FTerrainDigest Digest = TerrainPersistDigest(Object);
		OutStats.EncodeSeconds += FPlatformTime::Seconds() - EncodeStarted;

		const double StoreStarted = FPlatformTime::Seconds();
		const bool bStored = Store.GetObjects().StoreObject(Digest, Object);
		OutStats.StoreSeconds += FPlatformTime::Seconds() - StoreStarted;
		if (!bStored)
		{
			return FTerrainStoreResult::Io(ETerrainStorageResult::IoError);
		}

		FTerrainIndexUpdate Update;
		Update.Key                 = Key;
		Update.Value.Encoding      = ETerrainRegionEncoding::Dense;
		Update.Value.Rev           = Record.Rev;
		Update.Value.LastOpSeq     = Record.LastOpSeq;
		Update.Value.PayloadLength = static_cast<uint32>(Object.Num());
		Update.Value.PayloadDigest = Digest;
		Updates.Add(Update);

		++OutStats.ChunksWritten;
		OutStats.PayloadBytes += Object.Num();
	}

	// --- 2. the index, path-copied from the checkpoint currently published --------------------
	FTerrainIndexRoot OldRoot;
	OldRoot.bHasRootPage   = Store.GetState().Checkpoint.bHasRootPage;
	OldRoot.RootPageDigest = Store.GetState().Checkpoint.RootPageDigest;
	OldRoot.RootPageLength = Store.GetState().Checkpoint.RootPageLength;

	FTerrainIndexRoot NewRoot;
	const double IndexStarted = FPlatformTime::Seconds();
	const ETerrainPersistError IndexError = TerrainIndexApply(
		Identity, Store.GetObjects(), Store.GetObjects(), OldRoot, Updates,
		NewRoot, OutStats.IndexPagesWritten);
	OutStats.IndexSeconds = FPlatformTime::Seconds() - IndexStarted;
	if (IndexError != ETerrainPersistError::None)
	{
		return FTerrainStoreResult::Bad(IndexError);
	}

	// --- 3. the descriptor, and only then the root slot ----------------------------------------
	int64 LeafCount = 0;
	int64 TotalPayloadBytes = 0;
	const ETerrainPersistError WalkError = TerrainIndexEnumerate(
		Identity, Store.GetObjects(), NewRoot,
		[&LeafCount, &TotalPayloadBytes](const FTerrainChunkKey&, const FTerrainIndexLeafValue& Value)
		{
			++LeafCount;
			TotalPayloadBytes += Value.PayloadLength;
			return true;
		});
	if (WalkError != ETerrainPersistError::None)
	{
		return FTerrainStoreResult::Bad(WalkError);
	}

	FTerrainCheckpointDescriptor Descriptor;
	Descriptor.G                 = G;
	Descriptor.bHasRootPage      = NewRoot.bHasRootPage;
	Descriptor.RootPageDigest    = NewRoot.RootPageDigest;
	Descriptor.RootPageLength    = NewRoot.RootPageLength;
	Descriptor.LeafKeyCount      = static_cast<uint64>(LeafCount);
	Descriptor.TotalPayloadBytes = static_cast<uint64>(TotalPayloadBytes);

	const double PublishStarted = FPlatformTime::Seconds();
	const FTerrainStoreResult Published = Store.PublishCheckpoint(Descriptor, UtcMillis);
	OutStats.PublishSeconds = FPlatformTime::Seconds() - PublishStarted;
	if (!Published.IsOk())
	{
		// Everything written above is unreferenced by any root: garbage to be collected, not a
		// broken world. That is P-004 §12's containment argument, and this is where it applies.
		return Published;
	}

	OutStats.Generation = Store.GetState().Root.Generation;
	OutStats.Seconds    = FPlatformTime::Seconds() - Started;

	UE_LOG(LogTerrainCore, Log,
		TEXT("Checkpoint published at G=%llu generation=%llu: %d chunks (%lld bytes), %d index pages, ")
		TEXT("%lld keys total, in %.3f s ")
		TEXT("(read %.3f, encode %.3f, store %.3f, index %.3f, publish %.3f)."),
		G, OutStats.Generation, OutStats.ChunksWritten, OutStats.PayloadBytes,
		OutStats.IndexPagesWritten, LeafCount, OutStats.Seconds,
		OutStats.ReadSeconds, OutStats.EncodeSeconds, OutStats.StoreSeconds,
		OutStats.IndexSeconds, OutStats.PublishSeconds);

	if (OutStats.Seconds > 0.1 && OutStats.ChunksWritten > 0)
	{
		// P-003 §4: a visible multi-second stall under the supported workload FAILS. Capture is
		// synchronous, so this is the number that decides whether the incremental
		// copy-before-write pump can keep being deferred -- and the per-chunk rate is what
		// makes the projection to a full trigger obvious rather than something to work out.
		// The dominant phase is named explicitly because the obvious culprit was the wrong one.
		// The bulk adapter ReadRegion landed and capture time barely moved: measured warm, read
		// is 0.003 s of a 0.197 s capture while the index path-copy is 0.168 s. An incremental
		// pump that spreads chunk reads and payload writes -- which is what P-003 §4 describes
		// -- would therefore spread about a fifth of this and leave the rest a synchronous
		// stall. Whatever fixes capture has to fix the index write amplification: 49 durably
		// written pages for 8 changed keys.
		const double MillisPerChunk = OutStats.Seconds * 1000.0 / double(OutStats.ChunksWritten);
		UE_LOG(LogTerrainCore, Warning,
			TEXT("Checkpoint stalled the game thread for %.2f s over %d chunks (%.1f ms/chunk); ")
			TEXT("%d index pages cost %.3f s of it. P-003 §4 fails a visible multi-second stall. ")
			TEXT("The bulk adapter read is done and was not the cost; the index write path is."),
			OutStats.Seconds, OutStats.ChunksWritten, MillisPerChunk,
			OutStats.IndexPagesWritten, OutStats.IndexSeconds);
	}
	return FTerrainStoreResult::Ok();
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

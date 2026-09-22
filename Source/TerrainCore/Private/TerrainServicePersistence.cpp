// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainService.h"
#include "TerrainCommitJournal.h"
#include "TerrainJournalReplay.h"
#include "TerrainCheckpoint.h"
#include "TerrainRetention.h"
#include "TerrainWorldField.h"
#include "TerrainSettings.h"
#include "TerrainCore.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "Engine/World.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"

/**
 * Attaching a world's durable history to a running server (P-003 §2, §3, §5).
 *
 * This is the file where the persistence layer stops being a library and starts being the
 * game's memory. Everything under it has been built and tested on its own; here it is pointed
 * at a real directory on the server's disk, at the one moment the backend exists and nothing
 * has been edited yet.
 *
 * THE ORDER, and each step is a precondition for the next:
 *   1. only the authority persists -- a client that wrote a journal would be writing fiction;
 *   2. describe the world exactly (base descriptor) BEFORE opening anything, because the
 *      descriptor is the identity every stored object is checked against;
 *  2b. take the exclusive writer lease, or refuse -- a second server never writes beside the first;
 *   3. open the store, or create it if the directory is new;
 *   4. if it already existed, REFUSE it unless its recorded base matches the one this process
 *      would produce -- a mismatch means these edits belong to a different world;
 *   5. replay the journal onto the fresh backend, before a single client can connect;
 *   6. only then attach the journal, so the first thing recorded is the first NEW edit.
 *
 * THE FAILURE POLICY, and it is not the one this file started with. If the lease is held
 * elsewhere, the store will not open, the recorded base is not this world's, restore fails,
 * replay fails or the sequence cannot be seeded, **terrain access is closed** -- the world boots and terrain refuses edits
 * with `ShuttingDown`.
 *
 * The earlier policy was to run unsaved, on the reasoning that a server which will not start
 * is worse than one which does not save. That reasoning was wrong about what the player
 * experiences. A saved world that fails to load and then runs unsaved presents a PRISTINE
 * world as though it were theirs: they dig for an hour, nothing is recorded, and the only
 * warning was a log line nobody was reading. Refusing the edit is noticed in seconds, and it
 * cannot make the situation worse. P-003 §3 says the same thing about a base mismatch --
 * refuse boot, never reinterpret through the currently configured generator.
 *
 * A failed CHECKPOINT is deliberately not in that list: see MaybeCaptureCheckpoint.
 */

namespace
{
	/** Everything about the world's shape that a saved object binds to (P-004 §4). */
	FTerrainBaseDescriptor DescribeWorld(const UTerrainSettings& Settings, const FName& BackendName)
	{
		FTerrainWorldFieldParams FieldParams;
		FieldParams.Seed = Settings.Seed;

		FTerrainBaseDescriptor Base;
		Base.Seed                  = Settings.Seed;
		Base.GeneratorVersion      = static_cast<uint32>(FMath::Max(0, Settings.GeneratorVersion));
		Base.BackendKernelVersion  = 1;
		Base.GeneratorParamsDigest = TerrainWorldFieldParamsDigest(FieldParams);

		// No floating point is persisted (P-004 §1 rule 2): centimetres become micrometres.
		const FVector Origin = Settings.TerrainOriginWorld;
		Base.OriginWorldMicrometres[0] = static_cast<int64>(FMath::RoundToDouble(Origin.X * 10000.0));
		Base.OriginWorldMicrometres[1] = static_cast<int64>(FMath::RoundToDouble(Origin.Y * 10000.0));
		Base.OriginWorldMicrometres[2] = static_cast<int64>(FMath::RoundToDouble(Origin.Z * 10000.0));
		Base.VoxelSizeMicrometres      = static_cast<int64>(FMath::RoundToDouble(double(Settings.VoxelSizeCm) * 10000.0));

		Base.ChunkSizeVox           = TerrainChunkSizeVox;
		Base.WorldBoundsVox         = Settings.GetWorldBoundsVox();
		Base.ValueConfig            = 0;
		Base.EncodingRulesVersion   = 1;
		Base.MaterialCatalogVersion = 1;
		Base.MaterialCatalogCount   = static_cast<uint16>(ETerrainMaterial::Count);
		Base.GeneratorName          = TEXT("FTerrainWorldField");
		Base.BackendName            = BackendName.ToString();
		return Base;
	}

	/** Compares the two things that must agree for a saved world to be THIS world. */
	bool DescribesSameWorld(const FTerrainBaseDescriptor& A, const FTerrainBaseDescriptor& B)
	{
		return A.Seed == B.Seed
			&& A.GeneratorVersion == B.GeneratorVersion
			&& A.BackendKernelVersion == B.BackendKernelVersion
			&& A.GeneratorParamsDigest == B.GeneratorParamsDigest
			&& A.OriginWorldMicrometres[0] == B.OriginWorldMicrometres[0]
			&& A.OriginWorldMicrometres[1] == B.OriginWorldMicrometres[1]
			&& A.OriginWorldMicrometres[2] == B.OriginWorldMicrometres[2]
			&& A.VoxelSizeMicrometres == B.VoxelSizeMicrometres
			&& A.ChunkSizeVox == B.ChunkSizeVox
			&& A.WorldBoundsVox == B.WorldBoundsVox
			&& A.ValueConfig == B.ValueConfig
			&& A.EncodingRulesVersion == B.EncodingRulesVersion
			&& A.MaterialCatalogVersion == B.MaterialCatalogVersion
			// Ids are append-only and never reused (TerrainMaterials.h), so a world saved with a
			// smaller catalog means exactly what it meant; a LARGER saved catalog is a newer build's.
			&& A.MaterialCatalogCount <= B.MaterialCatalogCount
			&& A.GeneratorName == B.GeneratorName
			&& A.BackendName == B.BackendName;
	}
}

void UTerrainService::OpenWorldStore(UWorld& InWorld)
{
	check(IsInGameThread());
	CloseWorldStore();

	const UTerrainSettings* Settings = GetDefault<UTerrainSettings>();
	if (!Settings->bPersistEdits)
	{
		UE_LOG(LogTerrainCore, Warning,
			TEXT("Terrain persistence is OFF (bPersistEdits=false). Edits will be lost at shutdown."));
		return;
	}

	// 1. Only the authority. A client replaying a journal would be inventing a world the
	//    server never told it about, which is the drift D-002 exists to prevent.
	if (ActiveInit.Role != ETerrainRole::Server || InWorld.GetNetMode() == NM_Client)
	{
		return;
	}
	if (!Backend || State != ETerrainServiceState::Ready)
	{
		return;
	}

	const FString Directory = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("Worlds") / Settings->WorldStoreName);

	// 2. Describe the world before opening anything.
	const FTerrainBaseDescriptor Base = DescribeWorld(*Settings, BackendName);
	const int64 UtcMillis = FDateTime::UtcNow().ToUnixTimestamp() * 1000;

	StorageDevice = MakeUnique<FTerrainPlatformStorageDevice>(Directory);
	WorldStore    = MakeUnique<FTerrainWorldStore>(*StorageDevice);

	// 2b. The exclusive writer lease, BEFORE asking whether the world exists (P-003 §5). Two
	//     servers that both looked first could both see "no world" and both create one. A
	//     refusal has read nothing and written nothing, so the other server's world is exactly
	//     as it was.
	const FTerrainStoreResult Leased = WorldStore->AcquireWriterLease();
	if (!Leased.IsOk())
	{
		if (Leased.Storage == ETerrainStorageResult::Busy)
		{
			UE_LOG(LogTerrainCore, Error,
				TEXT("Terrain world at '%s' is already open for writing by another server (StoreBusy). ")
				TEXT("Two writers would corrupt it, so this server leaves it untouched and TERRAIN ACCESS ")
				TEXT("IS CLOSED. Stop the other server, or start this one with a different WorldStoreName."),
				*Directory);
		}
		else
		{
			UE_LOG(LogTerrainCore, Error,
				TEXT("Terrain world at '%s': the writer lease could not be taken (%s). TERRAIN ACCESS ")
				TEXT("IS CLOSED; restart after repairing the save directory."),
				*Directory, *Leased.ToString());
		}
		CloseWorldStore();
		bStorageFaulted = true;
		return;
	}

	const bool bExisting = StorageDevice->Exists(TerrainStoragePaths::BaseDescriptor);

	// 3. Open, or create a new world.
	FTerrainStoreResult Result;
	if (bExisting)
	{
		Result = WorldStore->Open();
	}
	else
	{
		// A world identity is minted once and never inferred from the directory name
		// (P-003 §1), so a copied directory is still the world it was.
		FTerrainWorldId  World;
		FTerrainStoreEpoch Epoch;
		const FGuid WorldGuid = FGuid::NewGuid();
		const FGuid EpochGuid = FGuid::NewGuid();
		FMemory::Memcpy(World.Bytes, &WorldGuid, TerrainPersistIdBytes);
		FMemory::Memcpy(Epoch.Bytes, &EpochGuid, TerrainPersistIdBytes);

		UE_LOG(LogTerrainCore, Log, TEXT("Creating a new terrain world at '%s'."), *Directory);
		Result = WorldStore->Create(Base, World, Epoch, UtcMillis);
	}

	if (!Result.IsOk())
	{
		UE_LOG(LogTerrainCore, Error,
			TEXT("Terrain world store at '%s' could not be %s (%s). TERRAIN ACCESS IS CLOSED; restart after repairing the save or configuration."),
			*Directory, bExisting ? TEXT("opened") : TEXT("created"), *Result.ToString());
		CloseWorldStore();
		bStorageFaulted = true;
		return;
	}

	// 4. An existing world must be THIS world. P-003 §3: an exact generator/kernel/config
	//    mismatch refuses boot and invokes the offline migration procedure -- it never
	//    reinterprets saved edits through a different world's shape.
	if (bExisting && !DescribesSameWorld(WorldStore->GetState().Base, Base))
	{
		UE_LOG(LogTerrainCore, Error,
			TEXT("Terrain world at '%s' was made by a different world shape (seed, generator, ")
			TEXT("voxel size, bounds or material catalog). Replaying its edits would corrupt it, ")
			TEXT("so it is left untouched and TERRAIN ACCESS IS CLOSED; restart after repairing the save or configuration. Migration is offline ")
			TEXT("and is not built; start a different WorldStoreName to play."),
			*Directory);
		CloseWorldStore();
		bStorageFaulted = true;
		return;
	}

	// 5. Restore the checkpoint, THEN replay what came after it. P-003 §3's terrain pass in
	//    order: without the restore, replay would rebuild from the beginning of history even
	//    though a cut exists, and with it replay only covers (G, H].
	if (bExisting)
	{
		FTerrainRestoreStats RestoreStats;
		const FTerrainStoreResult Restored =
			TerrainRestoreCheckpoint(*WorldStore, *Backend, *RevisionIndex, RestoreStats);
		if (!Restored.IsOk())
		{
			UE_LOG(LogTerrainCore, Error,
				TEXT("Terrain world at '%s' could not restore its checkpoint (%s). The world on ")
				TEXT("disk is left untouched and TERRAIN ACCESS IS CLOSED; restart after repairing the save or configuration."),
				*Directory, *Restored.ToString());
			CloseWorldStore();
			bStorageFaulted = true;
			return;
		}

		FTerrainReplayStats Stats;
		const FTerrainStoreResult Replayed =
			TerrainReplayJournal(*WorldStore, *Backend, *RevisionIndex, Stats);
		if (!Replayed.IsOk())
		{
			UE_LOG(LogTerrainCore, Error,
				TEXT("Terrain world at '%s' could not be replayed (%s). The world on disk is left ")
				TEXT("untouched and TERRAIN ACCESS IS CLOSED; restart after repairing the save or configuration."),
				*Directory, *Replayed.ToString());
			CloseWorldStore();
			bStorageFaulted = true;
			return;
		}

		// Tail edits still belong to the next cut, including chunks not touched this session.
		DirtyChunks = MoveTemp(Stats.DirtyChunks);
		if (Settings->bCheckpointCapture && DirtyChunks.Num() > TerrainCheckpointDirtyHardBound)
		{
			UE_LOG(LogTerrainCore, Error, TEXT("Checkpoint capture cannot boot with %d dirty chunks "
				"(limit %d). Restart with bCheckpointCapture=false; offline compaction is not built."),
				DirtyChunks.Num(), TerrainCheckpointDirtyHardBound);
			CloseWorldStore();
			bStorageFaulted = true;
			return;
		}

		UE_LOG(LogTerrainCore, Log,
			TEXT("Terrain world '%s' restored: %d chunks from the checkpoint at G=%llu, then %d ")
			TEXT("edits replayed to OpSeq %llu (%.3f s restore + %.3f s replay)."),
			*Settings->WorldStoreName, RestoreStats.ChunksRestored,
			WorldStore->GetState().Checkpoint.G, Stats.OpsApplied, Stats.LastOpSeq,
			RestoreStats.Seconds, Stats.Seconds);

		if (Stats.Seconds > 5.0)
		{
			// The cost the missing capture pump is deferring. Said out loud so it is noticed
			// as it grows rather than after a startup becomes unbearable.
			UE_LOG(LogTerrainCore, Warning,
				TEXT("Terrain replay took %.1f s for %d edits. Capture is opt-in and synchronous; ")
				TEXT("journal scanning and retention remain unbounded (DEF-2)."),
				Stats.Seconds, Stats.OpsApplied);
		}
	}

	// 6. Only now does anything start being recorded, so the first record is the first NEW edit.
	WorldJournal = MakeUnique<FTerrainWorldStoreJournal>(*WorldStore);
	// P-009: a backend that measures yield under the shared definition gets its Removed list
	// recorded as Measured; any other records Unavailable, never a measured zero (P-003 §2).
	WorldJournal->bPhysicalMeasured = Backend->MeasuresPhysicalYield();
	SetCommitJournal(WorldJournal.Get());

	// 7. The ledger (P-010): open or bootstrap it, settle whatever the journal holds beyond its
	//    watermark, and only then let anything be admitted.
	if (!OpenLedger(Directory))
	{
		CloseWorldStore();
		bStorageFaulted = true;
		return;
	}

	// The journal continues the sequence the world already reached, so replayed history is
	// never overwritten by a fresh session starting again at 1.
	//
	// THE QUEUE is what assigns sequences (P-004 §7 names it as the one recovered-sequence
	// owner); the service's own counter is a mirror. Seeding only the mirror leaves the queue
	// handing out sequences the journal has already used, which it refuses as an
	// OrderViolation -- and the first time this was wired that is exactly what happened, on the
	// first dig after the first restart.
	const FTerrainOpSeq Resume = WorldStore->GetJournal()->GetHead() + 1;
	if (!EditQueue.SeedSequence(Resume))
	{
		UE_LOG(LogTerrainCore, Error,
			TEXT("Terrain world '%s' could not resume at OpSeq %llu: the edit queue has already ")
			TEXT("assigned sequences. TERRAIN ACCESS IS CLOSED; restart after repairing the save or configuration."),
			*Settings->WorldStoreName, Resume);
		CloseWorldStore();
		bStorageFaulted = true;
		return;
	}
	NextOpSeq = Resume;

	UE_LOG(LogTerrainCore, Log,
		TEXT("Terrain world '%s' is recording at '%s'. Next OpSeq %llu."),
		*Settings->WorldStoreName, *Directory, NextOpSeq);
}

void UTerrainService::CloseWorldStore()
{
	check(IsInGameThread());

	// Retention first: its worker reads the store's device. Abandon waits for it and deletes
	// nothing; any copies it made are harmless duplicates.
	RetentionCollector.Abandon();
	bRetentionOwed = false;

	// Abandon any capture BEFORE the store goes away: the pump holds raw pointers to the store
	// and backend for as long as it is running, and it has an open pack batch to discard.
	// Nothing it buffered was ever durable, so the world simply still owes a checkpoint.
	CapturePump.Abandon();

	// The ledger next: a clean stop settles what is queued, so W reaches H; a crash would leave
	// the rest to the boot pass instead, which is what the journal is for.
	CloseLedger();

	// Detach FIRST: the commit path must never hold a pointer to a store that is going away,
	// and teardown can run while the queue still has work to cancel.
	SetCommitJournal(nullptr);
	WorldJournal.Reset();
	WorldStore.Reset();
	StorageDevice.Reset();
	DirtyChunks.Reset();
	LivePersistencePins.Reset();
	bCheckpointDisabled = false;
}

void UTerrainService::ReclaimStore()
{
	check(IsInGameThread());

	if (WorldStore == nullptr || !WorldStore->IsOpen())
	{
		UE_LOG(LogTerrainCore, Warning, TEXT("Terrain.Reclaim: this world has no open store."));
		return;
	}
	if (CapturePump.IsActive())
	{
		// Retention would refuse anyway; saying so here is clearer than reporting its error.
		UE_LOG(LogTerrainCore, Warning,
			TEXT("Terrain.Reclaim: a checkpoint capture is in flight. Try again once it lands."));
		return;
	}
	if (RetentionCollector.IsActive())
	{
		UE_LOG(LogTerrainCore, Warning,
			TEXT("Terrain.Reclaim: a background retention cycle is already running."));
		return;
	}

	// A whole cycle, synchronously: the diagnostic form. It stalls this frame for as long as the
	// cycle takes; the background collector exists so that play never has to.
	FTerrainRetentionStats Stats;
	const FTerrainStoreResult Result = TerrainReclaimStore(*WorldStore, Stats);
	if (!Result.IsOk())
	{
		// A validation failure precedes deletion, but an I/O failure can follow a partial
		// sweep. Report the completed work instead of claiming the device is unchanged.
		UE_LOG(LogTerrainCore, Warning,
			TEXT("Terrain.Reclaim: stopped (%s); completed %d migrations, %d legacy deletions, ")
			TEXT("%d compactions. Recovery dependencies are retained."), *Result.ToString(),
			Stats.LegacyObjectsMigrated, Stats.LegacyFilesDeleted, Stats.ContainersCompacted);
		return;
	}

	UE_LOG(LogTerrainCore, Display,
		TEXT("**** Terrain.Reclaim: %d live objects; %d migrated from %d legacy files; ")
		TEXT("%d containers compacted (%d dead objects dropped), rotated=%s; %lld bytes reclaimed, ")
		TEXT("in %.3f s. ****"),
		Stats.LiveObjects, Stats.LegacyObjectsMigrated, Stats.LegacyFilesDeleted,
		Stats.ContainersCompacted, Stats.ObjectsDropped, Stats.bRotated ? TEXT("yes") : TEXT("no"),
		Stats.BytesReclaimed, Stats.Seconds);
}

void UTerrainService::MaybeCollect()
{
	check(IsInGameThread());

	if (RetentionCollector.IsActive())
	{
		if (!RetentionCollector.Tick())
		{
			return;
		}
		const FTerrainRetentionStats& Stats = RetentionCollector.Stats();
		if (!RetentionCollector.Result().IsOk())
		{
			// Not a storage fault: a failed cycle deletes nothing it has not already copied and
			// verified. It is not retried this session, so a persistent error cannot loop.
			bRetentionOwed = false;
			UE_LOG(LogTerrainCore, Warning,
				TEXT("Terrain retention cycle failed (%s). Nothing live was lost; the save keeps ")
				TEXT("its garbage until the next session."), *RetentionCollector.Result().ToString());
			return;
		}
		UE_LOG(LogTerrainCore, Display,
			TEXT("**** Terrain.Retention: %s; %d live, %d containers compacted, %d migrated, %lld bytes ")
			TEXT("reclaimed in %.3f s; %d game-thread steps, longest %.4f s ****"),
			Stats.bAbandonedForEpoch ? TEXT("abandoned (a capture moved the epoch)") : TEXT("complete"),
			Stats.LiveObjects, Stats.ContainersCompacted, Stats.LegacyObjectsMigrated,
			Stats.BytesReclaimed, Stats.Seconds, Stats.GameThreadSteps,
			Stats.LongestGameThreadStepSeconds);
		if (Stats.bAbandonedForEpoch)
		{
			bRetentionOwed = true;   // try again after the capture that interrupted it
		}
		return;
	}

	const UTerrainSettings* Settings = GetDefault<UTerrainSettings>();
	if (!bRetentionOwed || !Settings->bBackgroundRetention || CapturePump.IsActive()
		|| WorldStore == nullptr || !WorldStore->IsOpen() || bStorageFaulted)
	{
		return;
	}

	FTerrainRetentionSettings Retention;
	Retention.MaxFrameBytes = static_cast<int64>(
		FMath::Clamp(Settings->RetentionFrameMegabytes, 0.0625, 256.0) * 1024.0 * 1024.0);
	const FTerrainStoreResult Started = RetentionCollector.Begin(*WorldStore, Retention);
	bRetentionOwed = false;
	if (!Started.IsOk())
	{
		UE_LOG(LogTerrainCore, Warning,
			TEXT("Terrain retention did not start (%s); the save keeps its garbage for now."),
			*Started.ToString());
	}
}

void UTerrainService::MaybeCaptureCheckpoint()
{
	check(IsInGameThread());

	const UTerrainSettings* Settings = GetDefault<UTerrainSettings>();

	// --- 0. a capture that ended somewhere else is handled before anything else -------------
	//
	// Copy-before-write can fail a capture from inside an edit (Codex review F2). Apply consumes
	// that at once; this is the backstop, so that no path can reach a new cut below while an
	// ended one has not been handled -- that was how a failed cut's history was published past.
	ConsumeCaptureCompletion();

	// P-003 §3's settlement invariant, G <= W at every durable root. The pump enforces it at its
	// one publication boundary, and it is handed W on every call because it cannot find it out:
	// T-132's setter was never called in normal play, so the guard was dead (Codex review F1).
	const FTerrainOpSeq SettledThrough = CaptureSettledThrough();

	// --- 1. a capture already in flight gets its slice of this frame ------------------------
	//
	// This runs BEFORE the start gate and regardless of the queue, because the whole point of
	// the pump is that a capture makes progress while edits keep flowing. It does not need a
	// quiescent moment: every chunk it still owes is protected by copy-before-write.
	if (CapturePump.IsActive())
	{
		const double BudgetSeconds =
			FMath::Clamp(Settings->CheckpointPumpMillisPerFrame, 0.05, 50.0) / 1000.0;
		CapturePump.Advance(BudgetSeconds, SettledThrough);
		ConsumeCaptureCompletion();
		return;
	}

	// --- 2. otherwise, decide whether to take a new cut --------------------------------------
	// Nothing to capture into, nothing to capture, or something still executing. The last is
	// the one that matters: a cut taken while a transaction is part-applied would record a
	// world that never existed at any single sequence.
	if (WorldStore == nullptr || !WorldStore->IsOpen() || CommitJournal == nullptr || bStorageFaulted)
	{
		return;
	}
	// G <= W is enforced at PUBLICATION, in the pump, not here: under sustained load some records
	// are always unsettled, and gating the START on "none unsettled" starved checkpoints
	// completely (T-132: 0 in 7,871 edits).
	//
	// And the cut needs no transaction half-executed -- not an empty queue. Waiting jobs commit at
	// later sequences; under sustained load the queue is never empty, which was the other half of
	// the starvation.
	if (!EditQueue.IsBetweenTransactions())
	{
		return;
	}
	if (!Settings->bCheckpointCapture || bCheckpointDisabled)
	{
		// On by default since the cost was measured at the trigger this fires at (D-037). See
		// the setting for the trade.
		return;
	}

	const int32 Trigger = FMath::Clamp(Settings->CheckpointDirtyChunkTrigger, 1, TerrainCheckpointDirtyHardBound);
	if (DirtyChunks.Num() < Trigger
		&& WorldStore->GetJournal()->GetHead() - WorldStore->GetState().Checkpoint.G
			< static_cast<uint64>(FMath::Max(1, Settings->CheckpointOpTrigger)))
	{
		return;
	}

	// G is the committed journal head. A cut claiming to include operations the journal has
	// not recorded would make replay start after edits nobody wrote down.
	const FTerrainOpSeq G = WorldStore->GetJournal()->GetHead();

	// The dirty set is handed over wholesale and ours is cleared: from this instant, edits
	// accumulate for the NEXT checkpoint. A chunk edited during the capture belongs to both --
	// this one records its state at G, the next records what the edit made of it.
	TMap<FTerrainChunkKey, FTerrainOpSeq> Cut = MoveTemp(DirtyChunks);
	DirtyChunks.Reset();

	CapturePump.Begin(
		*WorldStore, *Backend, *RevisionIndex, MoveTemp(Cut), G, SettledThrough,
		FDateTime::UtcNow().ToUnixTimestamp() * 1000);

	// It may have refused to start, or published an empty cut at once. Either is an end.
	ConsumeCaptureCompletion();
}

FTerrainOpSeq UTerrainService::CaptureSettledThrough() const
{
	// No ledger (SettlementModule=None, or a world with no journal): nothing to be ahead of.
	return Settlement ? Settlement->GetWatermark() : MAX_uint64;
}

void UTerrainService::ConsumeCaptureCompletion()
{
	if (CapturePump.ConsumeCompletion())
	{
		FinishCapture();
	}
}

void UTerrainService::FinishCapture()
{
	if (CapturePump.Result().IsOk())
	{
		// A new root supersedes the older retained one: what only that one named is now garbage.
		bRetentionOwed = true;
		return;
	}

	// Nothing of the cut was published, and our dirty set was cleared when it was taken. Put it
	// back, so every chunk changed since the published root is dirty SOMEWHERE. A chunk edited
	// since the cut is already here at a newer OpSeq, which wins. Checkpoints stop below anyway;
	// this keeps the dirty set honest regardless, so no later policy can publish past it.
	for (const TPair<FTerrainChunkKey, FTerrainOpSeq>& Owed : CapturePump.TakeCut())
	{
		FTerrainOpSeq& Seq = DirtyChunks.FindOrAdd(Owed.Key, Owed.Value);
		Seq = FMath::Max(Seq, Owed.Value);
	}

	// Deliberately NOT a storage fault. The previous root slot is untouched -- publication
	// writes the inactive one -- and the journal is intact, so the world is still fully
	// recoverable and simply has more to replay. Closing terrain access would cost the player
	// their session over the cheapest failure in the system.
	//
	// Retrying is not an option either: the trigger is still satisfied, so a capture would
	// start again every tick. Try once, say so once, keep playing.
	bCheckpointDisabled = true;
	UE_LOG(LogTerrainCore, Error,
		TEXT("Terrain checkpoint at G=%llu failed (%s). The previous checkpoint and the journal ")
		TEXT("are both intact, so nothing is lost and the world stays playable -- but this ")
		TEXT("session will take no further checkpoints, and the next startup will have more to ")
		TEXT("replay."),
		CapturePump.Stats().G, *CapturePump.Result().ToString());
}

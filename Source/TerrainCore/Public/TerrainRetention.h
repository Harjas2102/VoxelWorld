// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "TerrainWorldStore.h"
#include "Tasks/Task.h"

/**
 * TerrainRetention.h -- reclaiming what no live checkpoint refers to (P-004 §8, DEF-9).
 *
 * THE PROBLEM THIS SOLVES. Every checkpoint writes a fresh payload object for each chunk it
 * captures, and path-copies a fresh page for every index node on the way to a changed leaf.
 * The previous checkpoint's versions of both become unreachable the moment a new root slot
 * names the new ones -- and nothing has ever deleted them. Measured at the 256-chunk trigger
 * that is **33 MB of payloads per checkpoint**, almost all of it superseded by the next one.
 * A world played for a week would be mostly garbage.
 *
 * WHAT IS RECLAIMED, and what is deliberately not. This is a mark-and-sweep over the object
 * store. It never touches `base.tobj`, the root slots or the journal: journal deletion requires
 * coverage by the older retained cut, live W and all active pins (P-003 section 5), and
 * trimming it is a separate job, worth roughly a thousandth of this one -- about 49 KB per
 * checkpoint interval against 33 MB.
 *
 * THE THREE RULES THAT MAKE IT SAFE. Each exists because breaking it destroys a world.
 *
 * 1. **Both root slots must be valid.** P-004 §8: *"with one root left, reclamation must stop
 *    until redundancy is repaired"*. Deleting what the surviving root does not reference
 *    removes the only evidence that would rebuild the world if that root is next to fail.
 *
 * 2. **Both root slots are roots of the mark.** Not just the current one. The slot pair *is*
 *    the recovery mechanism: if the newest checkpoint is later found damaged, the other slot's
 *    checkpoint is what the world falls back to, and a fallback whose payloads have been
 *    deleted is not a fallback. So the live set is the union of two checkpoints, and a
 *    generation survives exactly as long as a root slot still names it.
 *
 * 3. **No capture may be in flight.** A running capture has objects buffered in an open pack
 *    batch and index pages written but not yet named by any root. They are unreachable by
 *    construction and a sweep would be right to delete them and wrong to have run at all.
 *
 * WHAT PACKS COST HERE, and why compaction is not optional (P-004 §13.6). A packed object
 * cannot be deleted on its own, and index path-copying **shares** pages between generations by
 * design, so old packs almost never become entirely dead. The first working sweep over a
 * three-generation world reclaimed 176 bytes and stranded 757 KB. So partly dead storage is
 * **rewritten without its garbage** rather than waited on.
 *
 * HOW IT IS REWRITTEN, since P-005. Objects live in a fixed pool of pre-created containers. A
 * container holding dead bytes has its live objects copied into one frame in the active
 * container, flushed and read back, and only then is it truncated to zero. No name is created
 * or removed, so a power cut can at most undo the truncation and leave a duplicate -- which is
 * what lifts the R-015 gate. Before P-005, compaction wrote a replacement pack under a new name
 * and deleted the original; losing that new name could take objects both roots share.
 *
 * Objects still in pre-P-005 loose files or packs are migrated the same way: copied into a
 * container first, and the old files removed only after the copy is durable.
 *
 * PRODUCTION SINCE T-127 (DEF-9). FTerrainRetentionCollector below runs the pass incrementally:
 * marking and copying off the game thread, destructive steps on it, each one guarded by P-003
 * §5's epoch rule. Backup, migration and sync consumers do not exist yet; when they do, they must
 * take an explicit retention pin before relying on any object (P-003 §5), and that pin must also
 * move the epoch.
 */

struct FTerrainRetentionStats
{
	/** Objects reachable from either root slot's checkpoint. */
	int32 LiveObjects = 0;

	/** Live objects copied out of pre-P-005 loose files and packs into a container. */
	int32 LegacyObjectsMigrated = 0;
	/** Pre-P-005 files removed after their live contents were durable in a container. */
	int32 LegacyFilesDeleted = 0;

	/** Whether the pass moved writing to an empty container so the old one could be compacted. */
	bool bRotated = false;

	int32 ContainersCompacted = 0;   // live objects copied out, then truncated to zero
	int32 ContainersKept = 0;        // non-empty and entirely live: nothing to do

	int32 ObjectsDropped = 0;
	int32 FramesAppended = 0;

	int64 BytesReclaimed = 0;

	/**
	 * The cycle stopped before deleting anything further because the reference epoch moved --
	 * a capture stored or reused an object, or a root was published. Not a failure: every copy
	 * already made is a harmless duplicate, and the next cycle marks afresh.
	 */
	bool bAbandonedForEpoch = false;

	/** Game-thread steps taken, and the longest one: the number a player could feel. */
	int32  GameThreadSteps = 0;
	double LongestGameThreadStepSeconds = 0.0;

	/** Wall time from Begin to the end of the cycle. */
	double Seconds = 0.0;
};

/** How a collector cycle runs. */
struct FTerrainRetentionSettings
{
	/**
	 * Largest copy frame, in bytes of object data. Bounds both the worker's buffer and the
	 * game-thread append step (P-003 §5: "bounded I/O buffers"). A single larger object still
	 * goes, alone.
	 */
	int64 MaxFrameBytes = 4 * 1024 * 1024;

	/**
	 * A container is compacted only when at least this fraction of its object bytes is dead.
	 * Compaction copies everything live, so compacting a mostly-live container for a sliver of
	 * garbage costs a full copy to save almost nothing -- measured: 67 MB copied to reclaim 0.
	 * Pre-P-005 files are always migrated regardless.
	 */
	double MinDeadFraction = 0.25;

	/** Pre-P-005 files removed per game-thread step. */
	int32 MaxDeletesPerStep = 64;

	/**
	 * Run the worker's share on the calling thread, synchronously. For tests over the in-memory
	 * device, which is not thread-safe, and for the diagnostic console command. The steps and
	 * their order are identical either way.
	 */
	bool bInline = false;
};

/**
 * Production retention (DEF-9, P-003 §5): an incremental collector that runs beside play.
 *
 * WHAT RUNS WHERE. The expensive work -- marking both roots, loading and verifying the objects
 * to be moved, building copy frames, and reading every copy back -- runs on a worker thread
 * against an FTerrainObjectSnapshot, never against the live store. The game thread does only
 * short, bounded steps, one per Tick: capture the epoch, plan, append one prepared frame, cut
 * one container, or delete a handful of pre-P-005 files. Nothing it does is proportional to
 * the size of the world except planning, which walks the in-memory location map.
 *
 * THE EPOCH RULE, which is the whole safety argument. At Begin the collector records the object
 * store's reference epoch. Every StoreObject -- including a deduplicated hit -- and every root
 * publication moves it. **Before every destructive step the collector checks the epoch is
 * unchanged and that no capture batch is open, and abandons the cycle otherwise.** An unchanged
 * epoch means no root has been published and no object referenced since the mark, so the mark
 * still describes everything any root can reach, and no object in a source was created after
 * it. Copies already appended are harmless duplicates.
 *
 * A cycle is expected to run between captures: the service starts one after each publication,
 * and captures are minutes apart. A capture that starts mid-cycle simply ends it.
 */
class TERRAINCORE_API FTerrainRetentionCollector
{
public:
	FTerrainRetentionCollector() = default;
	~FTerrainRetentionCollector() { Abandon(); }
	FTerrainRetentionCollector(const FTerrainRetentionCollector&) = delete;
	FTerrainRetentionCollector& operator=(const FTerrainRetentionCollector&) = delete;

	/**
	 * Starts a cycle, or refuses under the rules in this file's header: a store that is not open,
	 * a capture in flight, or root slots that do not both validate on disk. Game thread.
	 */
	FTerrainStoreResult Begin(FTerrainWorldStore& InStore, const FTerrainRetentionSettings& InSettings);

	/** Takes at most one game-thread step. Returns true once the cycle has ended (or was idle). */
	bool Tick();

	/** Ends a cycle now, waiting for any worker task. Safe at any point; deletes nothing. */
	void Abandon();

	bool IsActive() const { return Phase != EPhase::Idle; }
	const FTerrainRetentionStats& Stats() const { return CycleStats; }
	const FTerrainStoreResult& Result() const { return CycleResult; }

private:
	enum class EPhase : uint8 { Idle, Marking, Copying, Verifying, Cutting };

	/** One source to empty: a container, or (Container == INDEX_NONE) every pre-P-005 file. */
	struct FJob
	{
		int32 Container = INDEX_NONE;
		TArray<FTerrainDigest> Survivors;
		int32 Copied = 0;
		int32 Dropped = 0;
		int64 SourceBytes = 0;
		int64 TargetBytesBefore = 0;
		TArray<FString> LegacyFiles;
		int32 Deleted = 0;
	};

	/** What a worker hands back. Written only by the worker, read only after it completes. */
	struct FWork
	{
		ETerrainPersistError Error = ETerrainPersistError::None;
		bool bIoError = false;
		TSet<FTerrainDigest> Live;
		TArray<FString> LegacyFiles;
		int64 LegacyBytes = 0;
		TArray<uint8> Image;
		int32 Consumed = 0;
	};

	/** One game-thread step of the state machine; true when the cycle has ended. */
	bool Step();
	void LogCycle() const;
	void Launch(TUniqueFunction<void(FWork&)> Work);
	bool EpochUnchanged() const;
	bool Plan();
	void Finish(const FTerrainStoreResult& Result);
	void AbandonForEpoch();

	FTerrainWorldStore* Store = nullptr;
	FTerrainRetentionSettings Settings;
	EPhase Phase = EPhase::Idle;

	uint64 Epoch = 0;
	TSharedPtr<FTerrainObjectSnapshot> Snapshot;
	TSet<FTerrainDigest> Live;
	TArray<FJob> Jobs;
	int32 JobIndex = 0;

	TSharedPtr<FWork> Work;
	UE::Tasks::FTask Task;
	bool bTaskInFlight = false;

	double StartedSeconds = 0.0;
	FTerrainRetentionStats CycleStats;
	FTerrainStoreResult CycleResult = FTerrainStoreResult::Ok();
};

/**
 * One whole cycle, synchronously, on the calling thread: FTerrainRetentionCollector with
 * `bInline`, ticked to the end. The diagnostic console command and the tests use it; the
 * service uses the collector itself, in the background.
 *
 * Refuses rather than guessing when the store is not in a state where deletion is provably
 * safe: a store that is not open, root redundancy that is not intact, or a capture in flight.
 * A page that cannot be read during the mark fails the whole pass **without deleting anything**
 * -- an incomplete mark is indistinguishable from a small live set.
 */
TERRAINCORE_API FTerrainStoreResult TerrainReclaimStore(
	FTerrainWorldStore& Store,
	FTerrainRetentionStats& OutStats,
	const FTerrainRetentionSettings& Settings = FTerrainRetentionSettings());

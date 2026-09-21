// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "TerrainWorldStore.h"
#include "TerrainRevisionIndex.h"
#include "ITerrainBackend.h"
#include "TerrainPersistenceIndex.h"

/**
 * TerrainCheckpoint.h -- capturing a consistent global cut, and restoring one (P-003 §4, §3).
 *
 * THE PROBLEM THIS SOLVES. Without a checkpoint, G is 0 and every startup replays every edit
 * a world has ever received. That cost grows without bound, and no amount of fast journalling
 * fixes it: a world a group plays for months would take longer to load than to play. A
 * checkpoint is the cut that lets replay start somewhere other than the beginning.
 *
 * WHAT IS IMPLEMENTED HERE, honestly. Capture is **synchronous and taken at a quiescent
 * moment** -- nothing is executing, so the cut is trivially consistent and P-003 §4's
 * copy-before-write fence, dirty banks and background pump are all unnecessary. They exist so
 * that edits can CONTINUE during a capture, which this does not attempt. The cost is a stall
 * proportional to the number of dirty chunks, and it is measured and reported rather than
 * assumed small. **P-003 §4 is explicit that a visible multi-second stall under the supported
 * workload fails**, so the incremental pump is required before this is playable at scale, and
 * that is a gate rather than a nicety.
 *
 * **Payloads are always Dense.** P-004 §5.3 defines an encoding choice -- Empty when a chunk
 * matches the base, SparseDiff when few samples differ -- and choosing it requires comparing
 * against the canonically encoded base, which is the BACKEND's encoding and which the game
 * cannot reproduce. P-003 §6 already rules on this: *"The current production adapter has
 * density-only transfer and no exact material baseline: its prototype cannot claim
 * full-state persistence or enable sparse/pristine compaction."* So every captured chunk
 * costs 131,200 bytes, and compaction waits for a backend that can state its own base.
 */

/**
 * Keeps restored and replayed chunks resident, one retained interest per chunk.
 *
 * **Why this is not one interest that moves.** Residency is "a current interest covers this
 * chunk", not "this chunk has been loaded" -- `FMemoryTerrainBackend::IsRegionResident`
 * requires both. A single interest walked from chunk to chunk therefore leaves every chunk
 * behind it unresident, and on a backend that actually evicts, unresident means *gone*. The
 * restored world would be discarded chunk by chunk as it was being restored.
 *
 * That is exactly the hazard P-003 §4's **residency pins** exist to prevent: "Loss of a
 * player's interest cannot evict authoritative dirty data." `ITerrainBackend` has no pin, only
 * streaming interests, so a retained interest per chunk is what a pin has to be built from
 * today.
 *
 * **The cost is real and is stated rather than hidden.** One interest per restored chunk means
 * a thousand-chunk world holds a thousand interests, and on the production adapter each one is
 * an invoker component. A proper pin -- one flag on a chunk, not an interest -- is what P-003
 * §4 asks for, and it needs a backend-interface change that has not been made.
 */
class TERRAINCORE_API FTerrainResidencyPins
{
public:
	explicit FTerrainResidencyPins(uint32 InBaseInterestId) : BaseInterestId(InBaseInterestId) {}

	/** Pins one chunk. Repeat calls for the same chunk are free. */
	void Pin(ITerrainBackend& Backend, const FTerrainChunkKey& Key, const FTerrainBaseDescriptor& Base);

	int32 Num() const { return Pinned.Num(); }
	void Reset() { Pinned.Reset(); }

private:
	uint32 BaseInterestId;
	TMap<FTerrainChunkKey, uint32> Pinned;
};

/**
 * P-003 §4's hard bound on the dirty-key set: 4,096 keys.
 *
 * Admission closes before it is exceeded, so a world can never owe a checkpoint more work
 * than one capture agreed to handle. At Dense-only payloads that is a bounded 537 MB of
 * writes in the worst case, which is exactly the kind of number the incremental pump exists
 * to spread out.
 */
inline constexpr int32 TerrainCheckpointDirtyHardBound = 4096;

struct FTerrainCheckpointStats
{
	FTerrainOpSeq G = 0;
	uint64        Generation = 0;

	int32 DirtyKeys = 0;
	int32 ChunksWritten = 0;
	int32 IndexPagesWritten = 0;
	int64 PayloadBytes = 0;

	/**
	 * Wall-clock from the cut being taken to publication.
	 *
	 * Since the pump, this is NOT the stall: most of it is frames in which the capture did a
	 * few chunks and the game carried on. `WorkSeconds()` is the game-thread time actually
	 * spent, and `IndexSeconds + PublishSeconds` is the part that is still one unbroken step.
	 */
	double Seconds = 0.0;

	/** Chunks taken early by copy-before-write, because an edit was about to change them. */
	int32 CopiedBeforeWrite = 0;

	/** Game-thread time actually spent on this capture, spread across however many frames. */
	double WorkSeconds() const
	{
		return ReadSeconds + EncodeSeconds + StoreSeconds + IndexSeconds + PublishSeconds;
	}

	/**
	 * The part that is still one unbroken step on the game thread, and therefore the only part
	 * that can still be seen as a hitch: the index path-copy and publication.
	 */
	double UnspreadSeconds() const { return IndexSeconds + PublishSeconds; }

	/**
	 * Where the stall actually goes. These exist because the first optimisation aimed at the
	 * wrong half: the bulk adapter ReadRegion cut per-voxel reads by orders of magnitude and
	 * capture time barely moved, because reading was never the expensive part. Splitting the
	 * phases is what turns "capture costs 0.2 s" into a statement the incremental pump can be
	 * designed against, since a pump can only spread work it can identify.
	 *
	 * They sum to slightly less than Seconds; the remainder is the descriptor walk and the
	 * bookkeeping between phases.
	 *
	 * Since packs (P-004 §13), `StoreSeconds` and `IndexSeconds` only measure *buffering* --
	 * the single durable write for everything they produced is inside `PublishSeconds`.
	 */
	double ReadSeconds = 0.0;      // ITerrainBackend::ReadRegion for every dirty chunk
	double EncodeSeconds = 0.0;    // body/object encoding and the BLAKE3 content digest
	double StoreSeconds = 0.0;     // buffering the payload objects into the capture's pack
	double IndexSeconds = 0.0;     // path-copying the radix index, including its own objects
	double PublishSeconds = 0.0;   // ONE pack write and flush, then the root slot pair
};

/**
 * The incremental capture pump (P-003 §4, DEF-2).
 *
 * WHAT THIS FIXES. Capture used to be one synchronous call: every dirty chunk read, encoded and
 * buffered while the game thread waited. Measured at the real 256-chunk trigger that is 0.162 s
 * (D-037) -- inside P-003 §4's multi-second gate, but a visible hitch, and it scales with the
 * dirty set. Admission closes at `TerrainCheckpointDirtyHardBound`, so the worst case a server
 * can reach is sixteen times that, which is back inside the territory §4 fails.
 *
 * WHAT IT DOES. The dirty set is taken once, at the cut G. The chunks are then read and encoded
 * a few per frame under a time budget, and the checkpoint publishes when the last one is done.
 * Edits keep being admitted, committed and broadcast throughout.
 *
 * **Copy-before-write, and why it is simpler here than P-003 §4 describes.** §4 specifies
 * stashing a copy of a chunk before an edit touches it, so the capture can encode the copy
 * later. This does not need the copy: when an edit is about to modify a chunk the capture still
 * owes, the pump simply **captures that chunk immediately, out of order**, and drops it from the
 * pending set. The pre-edit state is encoded before the backend is allowed to change it, which
 * is the property §4's dirty banks exist to provide, without a second copy of the payload or a
 * bank to reconcile. The cost is one chunk read inside that edit, bounded by the edit's own
 * validated footprint.
 *
 * WHY THE CUT IS STILL CONSISTENT. Every chunk in the checkpoint is encoded from its state at
 * G, and nothing else can be:
 *
 *   - a chunk the pump reaches on its own has not been edited since G, because any edit would
 *     have gone through NoticeWrite first and taken it;
 *   - a chunk an edit touches is encoded by NoticeWrite before `ApplyOp` runs, so what is
 *     encoded is its pre-edit state;
 *   - revisions agree, because NoticeWrite runs before the revision index advances, so the
 *     revision encoded is the one the chunk had at G.
 *
 * Edits after G accumulate into a **fresh** dirty set for the next checkpoint. A chunk edited
 * during a capture is therefore in both: this checkpoint records its state at G, and the next
 * one records what the edit made of it.
 *
 * WHAT IS STILL SYNCHRONOUS. Publication -- the index path-copy, the descriptor, the single pack
 * write and the root slot -- is one step at the end and is not spread. At the 256-chunk trigger
 * that measured 0.041 s of the 0.162 s. Spreading it would mean interleaving a path-copy with
 * edits that are changing the very set being indexed, which is a much harder problem than the
 * one this solves, and it is not worth it until the number says otherwise.
 */
class TERRAINCORE_API FTerrainCapturePump
{
public:
	~FTerrainCapturePump() { Abandon(); }

	bool IsActive() const { return bActive; }
	int32 Remaining() const { return Pending.Num(); }
	FTerrainOpSeq GetCut() const { return G; }

	/**
	 * Takes the cut and begins a capture over exactly these keys.
	 *
	 * `InG` must be the committed journal head at this moment, for the same reason it always
	 * had to be: a cut claiming to include operations the journal has not recorded would make
	 * replay start after edits nobody wrote down. The caller hands over the dirty set and must
	 * clear its own, so that edits from here on accumulate for the NEXT checkpoint.
	 */
	FTerrainStoreResult Begin(
		FTerrainWorldStore& InStore,
		ITerrainBackend& InBackend,
		const FTerrainRevisionIndex& InRevisions,
		TMap<FTerrainChunkKey, FTerrainOpSeq>&& DirtyKeys,
		FTerrainOpSeq InG,
		int64 InUtcMillis);

	/**
	 * Called with an edit's validated footprint **before the backend applies it**.
	 *
	 * Any of those chunks the capture still owes are encoded here and now, from their pre-edit
	 * state. Cheap and usually nothing: a chunk is captured at most once per checkpoint, and
	 * most edits touch chunks the capture has already taken or never owed.
	 */
	void NoticeWrite(TConstArrayView<FTerrainChunkKey> Keys);

	/**
	 * Spends up to `BudgetSeconds` encoding pending chunks, and publishes when the last one is
	 * done. Returns true once the capture has finished, successfully or not.
	 *
	 * The budget is honoured between chunks, not within one: a chunk is never half encoded, so
	 * a single chunk can overrun it. At the measured 0.35 ms per chunk that overrun is small.
	 *
	 * **At least one chunk is captured per call, whatever the budget.** A pump that can make
	 * zero progress is a pump that can never finish, and a budget smaller than one chunk would
	 * starve the capture forever.
	 */
	bool Advance(double BudgetSeconds);

	/** Valid once Advance has returned true. */
	const FTerrainStoreResult&     Result() const { return FinalResult; }
	const FTerrainCheckpointStats& Stats()  const { return CaptureStats; }

	/**
	 * Drops the capture and everything it had buffered.
	 *
	 * Nothing buffered was ever durable -- it lives in the store's open pack batch, which is
	 * discarded -- so an abandoned capture leaves the store exactly as it found it. The world
	 * simply still owes a checkpoint, and has more to replay if it restarts.
	 */
	void Abandon();

private:
	bool CaptureOne(const FTerrainChunkKey& Key, FTerrainOpSeq LastOpSeq);
	void Finish();
	void Fail(const FTerrainStoreResult& Why);

	FTerrainWorldStore*          Store = nullptr;
	ITerrainBackend*             Backend = nullptr;
	const FTerrainRevisionIndex* Revisions = nullptr;

	bool bActive = false;
	bool bFinished = false;

	FTerrainOpSeq G = 0;
	FTerrainOpSeq PrevG = 0;
	int64         UtcMillis = 0;

	TMap<FTerrainChunkKey, FTerrainOpSeq> Pending;
	TArray<FTerrainIndexUpdate>           Updates;

	double StartedAt = 0.0;

	FTerrainCheckpointStats CaptureStats;
	FTerrainStoreResult     FinalResult;
};

/**
 * Captures a checkpoint at G over exactly the chunks that have changed since the last one.
 *
 * **The caller must ensure nothing is executing** -- no queued transaction part part-applied,
 * no backend mutation in flight -- because this takes the cut by simply reading the world as
 * it stands. G must be the committed journal head at that moment: a cut that claimed to
 * include operations the journal has not recorded would make replay start after edits that
 * were never written down.
 *
 * Every dirty chunk must be **resident and fully readable**. P-003 §4 names the trap exactly:
 * `FMemoryTerrainBackend::ReadRegion` returns success with a default Empty for a nonresident
 * chunk while `FVPLegacyBackend` returns false, and **neither is evidence of pristine
 * equality**. A chunk that cannot be read in full fails the capture rather than being
 * published as unchanged.
 *
 * Publication order is the store's: payload objects, then index pages, then the descriptor,
 * then the root slot (P-004 §12). A crash anywhere before the root leaves unreferenced
 * objects -- garbage, not a broken world.
 */
TERRAINCORE_API FTerrainStoreResult TerrainCaptureCheckpoint(
	FTerrainWorldStore& Store,
	ITerrainBackend& Backend,
	const FTerrainRevisionIndex& Revisions,
	const TMap<FTerrainChunkKey, FTerrainOpSeq>& DirtyKeys,
	FTerrainOpSeq G,
	int64 UtcMillis,
	FTerrainCheckpointStats& OutStats);

struct FTerrainRestoreStats
{
	int32  ChunksRestored = 0;
	int64  PayloadBytes = 0;
	double Seconds = 0.0;
};

/**
 * Restores every chunk in the open checkpoint onto a freshly initialised backend.
 *
 * P-003 §3's terrain pass begins here: the complete logical checkpoint at G is restored, and
 * only then are ops `G < OpSeq <= H` replayed. Restoring is **eager and complete** -- every
 * chunk in the index, not merely those replay will touch -- which is the model P-003 §4
 * describes: *"It matches the initial recovery model, which already restores every edited
 * chunk before login."*
 *
 * `Revisions` must be empty and is seeded from the checkpoint, so a returning client is not
 * told that a chunk it has already seen edits for is at revision 0.
 *
 * Each restored chunk is made resident first and the interest is retained afterwards, for the
 * same reason replay retains its own: without a live pin, a backend that evicts would discard
 * the restored state.
 */
TERRAINCORE_API FTerrainStoreResult TerrainRestoreCheckpoint(
	FTerrainWorldStore& Store,
	ITerrainBackend& Backend,
	FTerrainRevisionIndex& Revisions,
	FTerrainRestoreStats& OutStats);

/**
 * Base interest id for restore's pins. Ids run upward from here, one per restored chunk, and
 * none is ever released -- see FTerrainResidencyPins for why.
 */
inline constexpr uint32 TerrainRestoreInterestId = 0x52000000;   // 'R' + room to count

/** The same, for replay. Disjoint from restore's range so the two cannot collide. */
inline constexpr uint32 TerrainReplayPinBaseId = 0x50000000;     // 'P' + room to count

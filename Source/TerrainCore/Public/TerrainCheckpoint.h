// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "TerrainWorldStore.h"
#include "TerrainRevisionIndex.h"
#include "ITerrainBackend.h"

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

	/** The stall. P-003 §4 makes this an acceptance gate, not a curiosity. */
	double Seconds = 0.0;

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

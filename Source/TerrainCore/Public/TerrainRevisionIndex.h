// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "TerrainTypes.h"
#include "Containers/ArrayView.h"

/**
 * In-memory per-chunk revisions (AR-4). Externally serialized by its owner.
 * No backend, world, sequencing or persistence ownership. An index lives for one
 * session; it cannot be assigned/reset to silently discard revision history.
 */
class TERRAINCORE_API FTerrainRevisionIndex
{
public:
	FTerrainRevisionIndex() = default;
	FTerrainRevisionIndex(const FTerrainRevisionIndex&) = delete;
	FTerrainRevisionIndex& operator=(const FTerrainRevisionIndex&) = delete;
	FTerrainRevisionIndex(FTerrainRevisionIndex&&) = delete;
	FTerrainRevisionIndex& operator=(FTerrainRevisionIndex&&) = delete;

	/** Unseen keys read as zero without creating an entry. */
	FTerrainRev GetRevision(const FTerrainChunkKey& Key) const;

	/**
	 * Increment each distinct key once. Empty input succeeds without changes.
	 * If any revision would overflow, return false without changing ANY entry.
	 * Repeated calls are separate updates; this is not operation/retry deduplication.
	 */
	bool TryBumpRevisions(TConstArrayView<FTerrainChunkKey> AffectedChunks);

	/**
	 * Seeds revisions from a restored checkpoint (P-003 section 3).
	 *
	 * The class refuses assignment and reset so that revision history cannot be silently
	 * discarded; seeding an EMPTY index from disk is the opposite of discarding, and without
	 * it a restored world would tell a returning client that every chunk is at revision 0
	 * while its terrain says otherwise.
	 *
	 * Refuses unless the index is empty. Refuses a zero revision: 0 means "never edited", and
	 * a chunk in a checkpoint has been.
	 */
	bool SeedRevisions(TConstArrayView<TPair<FTerrainChunkKey, FTerrainRev>> Restored);

private:
	TMap<FTerrainChunkKey, FTerrainRev> Revisions;

#if WITH_DEV_AUTOMATION_TESTS
	friend class FTerrainRevisionMonotonicTest;
#endif
};

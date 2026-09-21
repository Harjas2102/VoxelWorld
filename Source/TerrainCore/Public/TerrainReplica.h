// Copyright VoxelWorld. See Docs/proposals/P-008-join-in-progress.md.

#pragma once

#include "CoreMinimal.h"
#include "TerrainTypes.h"
#include "TerrainOp.h"
#include "TerrainStreamComponent.h"

class ITerrainBackend;
class FTerrainRevisionIndex;

/**
 * TerrainReplica.h -- what a client knows about which of its chunks match the server (P-008 §3).
 *
 * This is the DEF-3 resolution, as code. A replica holds a set of SYNCED chunks: chunks whose
 * data and revision are known to equal the server's, because the server said "pristine" and we
 * agreed, or because we installed the server's snapshot. Everything else is unsynced, however it
 * looks.
 *
 * An authoritative op is applied to the backend over its whole footprint -- the kernel is
 * pointwise (ARCHITECTURE §4.10.3), so what lands in an unsynced chunk cannot leak into a synced
 * neighbour -- but revision checks and revision bumps apply ONLY to synced chunks. A synced chunk
 * whose revision does not match the op's before-revision has missed an op; it is demoted and
 * reported, and the rest of the op still applies. An unsynced chunk stays unsynced until a
 * snapshot replaces it wholesale, so nothing ever has to decide whether an op is "already in"
 * a chunk: the snapshot's position in the ordered stream decides it.
 *
 * No UObject, no world, no transport: every rule here tests headless against the reference
 * backend (TerrainCore.Replication.JoinInProgress).
 */
class TERRAINCORE_API FTerrainReplica
{
public:
	/**
	 * Applies an authoritative op. Returns false, changing nothing, when the op and its
	 * revision list do not describe each other (wrong keys, duplicates, a revision that moves by
	 * more than one). Otherwise returns true and lists in OutLost every synced chunk that turned
	 * out not to match; those are now unsynced and must be requested again.
	 */
	bool ApplyOp(ITerrainBackend& Backend, FTerrainRevisionIndex& Revisions, const FTerrainOp& Op,
	             TConstArrayView<FTerrainChunkRevision> OpRevisions, TArray<FTerrainChunkKey>& OutLost);

	/** Installs an authoritative snapshot and marks the chunk synced. False leaves it unsynced. */
	bool ApplySnapshot(ITerrainBackend& Backend, FTerrainRevisionIndex& Revisions, const FTerrainChunkKey& Key,
	                   FTerrainRev Rev, TArrayView<const uint8> Compressed);

	/**
	 * The server says these chunks were never edited. Accepted only if none of them has a
	 * revision here or is awaiting resync; then all of them are synced.
	 */
	bool AcceptPristine(const FTerrainRevisionIndex& Revisions, TConstArrayView<FTerrainChunkKey> Keys);

	void MarkUnsynced(const FTerrainChunkKey& Key) { Synced.Remove(Key); AwaitingResync.Add(Key); }
	bool IsSynced(const FTerrainChunkKey& Key) const { return Synced.Contains(Key); }
	int32 NumSynced() const { return Synced.Num(); }

private:
	TSet<FTerrainChunkKey> Synced;
	TSet<FTerrainChunkKey> AwaitingResync;
};

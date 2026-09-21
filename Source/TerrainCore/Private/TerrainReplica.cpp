// Copyright VoxelWorld. See Docs/proposals/P-008-join-in-progress.md.

#include "TerrainReplica.h"

#include "ITerrainBackend.h"
#include "TerrainChunk.h"
#include "TerrainChunkSnapshot.h"
#include "TerrainOpGeometry.h"
#include "TerrainRevisionIndex.h"

bool FTerrainReplica::ApplyOp(ITerrainBackend& Backend, FTerrainRevisionIndex& Revisions, const FTerrainOp& Op,
	TConstArrayView<FTerrainChunkRevision> OpRevisions, TArray<FTerrainChunkKey>& OutLost)
{
	OutLost.Reset();

	FTerrainBox Bounds;
	TArray<FTerrainChunkKey> Keys;
	if (!TerrainOpBounds(Op, Bounds) || !TerrainChunkKeysForBox(Bounds, Keys) || Keys.Num() != OpRevisions.Num())
	{
		return false;
	}

	// Validate the whole description before touching anything.
	TSet<FTerrainChunkKey> Seen, Checked, Changed, Lost;
	for (const FTerrainChunkRevision& V : OpRevisions)
	{
		const FTerrainChunkKey K(V.Key.X, V.Key.Y, V.Key.Z);
		if (Seen.Contains(K) || !Keys.Contains(K) || V.After < V.Before || uint64(V.After) > uint64(V.Before) + 1)
		{
			return false;
		}
		Seen.Add(K);
		if (!Synced.Contains(K))
		{
			continue;   // unsynced: written below, never checked, never bumped
		}
		if (Revisions.GetRevision(K) != V.Before)
		{
			Lost.Add(K);   // this chunk missed an op: a gap
			continue;
		}
		Checked.Add(K);
		if (V.After != V.Before)
		{
			Changed.Add(K);
		}
	}

	FTerrainEditResult Result;
	if (!Backend.ApplyOp(Op, Result) || Result.bTruncated)
	{
		Lost.Append(Checked);
	}
	else
	{
		// For every chunk still trusted, "the server changed it" must equal "we changed it".
		const TSet<FTerrainChunkKey> Affected(Result.AffectedChunks);
		for (const FTerrainChunkKey& K : Checked)
		{
			if (Affected.Contains(K) != Changed.Contains(K))
			{
				Lost.Add(K);
			}
		}
	}

	TArray<FTerrainChunkKey> Bump;
	for (const FTerrainChunkKey& K : Changed)
	{
		if (!Lost.Contains(K))
		{
			Bump.Add(K);
		}
	}
	if (!Revisions.TryBumpRevisions(Bump))
	{
		Lost.Append(Bump);
	}

	for (const FTerrainChunkKey& K : Lost)
	{
		MarkUnsynced(K);
		OutLost.Add(K);
	}
	return true;
}

bool FTerrainReplica::ApplySnapshot(ITerrainBackend& Backend, FTerrainRevisionIndex& Revisions,
	const FTerrainChunkKey& Key, FTerrainRev Rev, TArrayView<const uint8> Compressed)
{
	FTerrainRegionData Region;
	// Header fields from this backend's own read, so the snapshot is written in exactly the
	// representation (generator version, value config) this backend reads back.
	FTerrainRegionData Probe;
	if (Rev == 0 || !Backend.IsRegionResident(Key) || !Backend.ReadRegion(Key, Probe)
		|| !TerrainDecodeChunkSnapshot(Compressed, Region.Payload))
	{
		MarkUnsynced(Key);
		return false;
	}
	Region.Key              = Key;
	Region.Rev              = Rev;
	Region.Encoding         = ETerrainRegionEncoding::Dense;
	Region.GeneratorVersion = Probe.GeneratorVersion;
	Region.ValueConfig      = Probe.ValueConfig;
	if (!Backend.WriteRegion(Region))
	{
		MarkUnsynced(Key);
		return false;
	}
	Revisions.AssignReplicaRevision(Key, Rev);
	AwaitingResync.Remove(Key);
	Synced.Add(Key);
	return true;
}

bool FTerrainReplica::AcceptPristine(const FTerrainRevisionIndex& Revisions, TConstArrayView<FTerrainChunkKey> Keys)
{
	for (const FTerrainChunkKey& K : Keys)
	{
		if (Revisions.GetRevision(K) != 0 || AwaitingResync.Contains(K))
		{
			return false;
		}
	}
	for (const FTerrainChunkKey& K : Keys)
	{
		Synced.Add(K);
	}
	return true;
}

// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainRevisionIndex.h"

FTerrainRev FTerrainRevisionIndex::GetRevision(const FTerrainChunkKey& Key) const
{
	const FTerrainRev* Revision = Revisions.Find(Key);
	return Revision ? *Revision : 0;
}

bool FTerrainRevisionIndex::TryBumpRevisions(TConstArrayView<FTerrainChunkKey> AffectedChunks)
{
	TSet<FTerrainChunkKey> UniqueKeys;
	UniqueKeys.Reserve(AffectedChunks.Num());
	for (const FTerrainChunkKey& Key : AffectedChunks)
	{
		if (GetRevision(Key) == MAX_uint32)
		{
			return false;
		}
		UniqueKeys.Add(Key);
	}

	// All validation precedes mutation, including insertion of previously unseen keys.
	for (const FTerrainChunkKey& Key : UniqueKeys)
	{
		++Revisions.FindOrAdd(Key);
	}
	return true;
}

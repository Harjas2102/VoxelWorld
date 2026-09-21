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

bool FTerrainRevisionIndex::SeedRevisions(TConstArrayView<TPair<FTerrainChunkKey, FTerrainRev>> Restored)
{
	// Only onto an empty index. This class deliberately refuses assignment and reset so that
	// revision history cannot be silently discarded; seeding a world that already has history
	// would be that discard wearing a different name.
	if (Revisions.Num() != 0)
	{
		return false;
	}

	// Validate everything before writing anything, so a bad entry cannot leave the index
	// half-seeded -- the same all-or-nothing rule TryBumpRevisions follows.
	TSet<FTerrainChunkKey> Seen;
	for (const TPair<FTerrainChunkKey, FTerrainRev>& Entry : Restored)
	{
		if (Entry.Value == 0 || Seen.Contains(Entry.Key))
		{
			// 0 means "never edited". A chunk recorded in a checkpoint has been.
			return false;
		}
		Seen.Add(Entry.Key);
	}

	Revisions.Reserve(Restored.Num());
	for (const TPair<FTerrainChunkKey, FTerrainRev>& Entry : Restored)
	{
		Revisions.Add(Entry.Key, Entry.Value);
	}
	return true;
}

void FTerrainRevisionIndex::AssignReplicaRevision(const FTerrainChunkKey& Key, FTerrainRev Rev)
{
	if (Rev == 0)
	{
		Revisions.Remove(Key);
	}
	else
	{
		Revisions.Add(Key, Rev);
	}
}

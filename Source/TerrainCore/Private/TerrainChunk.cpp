// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainChunk.h"

bool TerrainChunkKeysForBox(const FTerrainBox& Box, TArray<FTerrainChunkKey>& OutKeys, int32 MaxKeys)
{
	if (Box.IsEmpty())
	{
		return true;
	}
	if (MaxKeys <= 0)
	{
		return false;
	}

	// Max is EXCLUSIVE, so the last voxel on each axis is Max - 1.
	const FTerrainChunkKey MinKey = TerrainChunkKeyForVoxel(Box.Min);
	const FTerrainChunkKey MaxKey = TerrainChunkKeyForVoxel(Box.Max - FIntVector(1));

	const int64 CountX = static_cast<int64>(MaxKey.X) - MinKey.X + 1;
	const int64 CountY = static_cast<int64>(MaxKey.Y) - MinKey.Y + 1;
	const int64 CountZ = static_cast<int64>(MaxKey.Z) - MinKey.Z + 1;
	if (CountX <= 0 || CountY <= 0 || CountZ <= 0)
	{
		return false;
	}
	// Multiply in int64 and test each step: the product of three int32 ranges overflows.
	const int64 Total = CountX * CountY * CountZ;
	if (CountX > MaxKeys || CountY > MaxKeys || CountZ > MaxKeys || Total > MaxKeys)
	{
		return false;
	}

	OutKeys.Reserve(OutKeys.Num() + static_cast<int32>(Total));
	for (int32 Z = MinKey.Z; Z <= MaxKey.Z; ++Z)
	for (int32 Y = MinKey.Y; Y <= MaxKey.Y; ++Y)
	for (int32 X = MinKey.X; X <= MaxKey.X; ++X)
	{
		OutKeys.Emplace(X, Y, Z);
	}
	return true;
}

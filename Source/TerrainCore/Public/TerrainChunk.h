// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "TerrainTypes.h"

/**
 * TerrainChunk.h — chunk-space arithmetic (ARCHITECTURE.md §4.2, fork K2).
 *
 * "Chunk keys and coordinate policy" is GAME-owned (§8.1). Every module that has to turn a
 * voxel coordinate into a chunk key uses these helpers, so there is exactly one definition
 * of the mapping in the codebase and a backend cannot quietly disagree with the service
 * about which chunk a voxel is in.
 *
 * 32 voxels per chunk is fork K2, ruled at CP-005 by D-024: authority chunks align to whole
 * data leaves (DATA_CHUNK_SIZE 16) and whole render chunks (RENDER_CHUNK_SIZE 32) of the
 * current backend. §2.3 records that as an alignment choice, not an API requirement.
 *
 * FMemoryTerrainBackend keeps its own private copies of these (T-112.2, CP-008) and is left
 * untouched here: it is verified code, and duplicating four lines is cheaper than reopening
 * it. The constants are identical and TerrainChunk.ChunkKeys asserts they agree.
 */

/** Voxels per chunk edge. K2, ruled by D-024. */
inline constexpr int32 TerrainChunkSizeVox = 32;

/** Samples per chunk. */
inline constexpr int32 TerrainChunkSampleCount = TerrainChunkSizeVox * TerrainChunkSizeVox * TerrainChunkSizeVox;

/** Floor division towards negative infinity — NOT C++ truncation, which mirrors around zero. */
inline int32 TerrainChunkCoordinate(int32 VoxelCoordinate)
{
	return VoxelCoordinate / TerrainChunkSizeVox - (VoxelCoordinate % TerrainChunkSizeVox < 0 ? 1 : 0);
}

inline FTerrainChunkKey TerrainChunkKeyForVoxel(const FIntVector& Voxel)
{
	return FTerrainChunkKey(
		TerrainChunkCoordinate(Voxel.X),
		TerrainChunkCoordinate(Voxel.Y),
		TerrainChunkCoordinate(Voxel.Z));
}

/** The half-open voxel box a chunk covers: Min inclusive, Max exclusive (FTerrainBox). */
inline FTerrainBox TerrainChunkBounds(const FTerrainChunkKey& Key)
{
	const FIntVector Min(Key.X * TerrainChunkSizeVox, Key.Y * TerrainChunkSizeVox, Key.Z * TerrainChunkSizeVox);
	return FTerrainBox(Min, Min + FIntVector(TerrainChunkSizeVox));
}

/**
 * Every distinct chunk key a half-open voxel box touches, appended to OutKeys.
 *
 * An empty box appends nothing and succeeds. Returns false without appending anything when
 * the box would enumerate more than MaxKeys chunks, so a caller cannot be made to allocate
 * without bound by a malformed op; callers on the authoritative path treat false as a
 * rejected request.
 */
TERRAINCORE_API bool TerrainChunkKeysForBox(const FTerrainBox& Box, TArray<FTerrainChunkKey>& OutKeys, int32 MaxKeys = 4096);

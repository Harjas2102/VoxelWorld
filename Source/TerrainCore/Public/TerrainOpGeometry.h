// Copyright VoxelWorld. See ARCHITECTURE §4.10.2 and P-002.
#pragma once
#include "TerrainOp.h"

TERRAINCORE_API bool TerrainOpBounds(const FTerrainOp& Op, FTerrainBox& Out);
TERRAINCORE_API bool TerrainOpContains(const FTerrainOp& Op, const FIntVector& Position);
/** Exact counts, bounded before scanning. False leaves both counts zero. */
TERRAINCORE_API bool TerrainOpCounts(const FTerrainOp& Op, int64 MaxWrites, int64& Writes, int64& Scans);
/** Atomic output; capacity bounds both the subdivision frontier and result. Spheres never split. */
TERRAINCORE_API bool SplitTerrainOp(const FTerrainOp& Op, int64 MaxWrites, int32 MaxParts, TArray<FTerrainOp>& Out);

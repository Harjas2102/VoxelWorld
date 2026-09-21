// Copyright VoxelWorld. See ARCHITECTURE §4.10.2 and P-002.
#pragma once
#include "TerrainOp.h"

TERRAINCORE_API bool TerrainOpBounds(const FTerrainOp& Op, FTerrainBox& Out);
TERRAINCORE_API bool TerrainOpContains(const FTerrainOp& Op, const FIntVector& Position);
/** Exact counts, bounded before scanning. False leaves both counts zero. */
TERRAINCORE_API bool TerrainOpCounts(const FTerrainOp& Op, int64 MaxWrites, int64& Writes, int64& Scans);
/** Atomic output; capacity bounds both the subdivision frontier and result. Spheres never split. */
TERRAINCORE_API bool SplitTerrainOp(const FTerrainOp& Op, int64 MaxWrites, int32 MaxParts, TArray<FTerrainOp>& Out);

/**
 * Occupancy of one int16 density sample (§4.9, P-009 §2): 1 fully solid (-32767), 0 fully empty
 * (+32767), linear between. ONE definition, shared by every backend, so two backends measuring
 * the same change can only disagree about the change -- never about what "solid" means.
 * Whether linear is accurate in the plugin's transition band is E-1, measured in the adapter checks.
 */
inline double TerrainOccupancy(int16 Value)
{
	return FMath::Clamp((1.0 - static_cast<double>(Value) / 32767.0) * 0.5, 0.0, 1.0);
}

/** One voxel's volume in microlitres: cm^3 is mL, and 1 mL is 1000 uL. */
inline double TerrainVoxelMicroLitres(double VoxelSizeCm)
{
	return VoxelSizeCm * VoxelSizeCm * VoxelSizeCm * 1000.0;
}

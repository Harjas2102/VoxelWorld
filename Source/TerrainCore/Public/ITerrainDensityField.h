// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "TerrainTypes.h"

/** Density is normalised to [-1, 1]; negative is solid. Material is a game id. */
struct FTerrainDensitySample
{
	float Density = 0.f;
	FTerrainMatId MaterialId = 0;
};

/**
 * Conservative bounds on Sample().Density over a voxel box: every sample inside the box
 * must satisfy Min <= Density <= Max. Widening it is always correct; narrowing it wrongly
 * is a hole in the world. See ITerrainDensityField::SampleRange.
 */
struct FTerrainDensityRange
{
	float Min = -1.f;
	float Max =  1.f;
};

/**
 * R-011 determination, T-112.2, 2026-09-06, Director-authorised in session:
 * the packet's single integer-position Sample supersedes ARCHITECTURE.md 4.6
 * lines 536-538 (Density/Material/Version). GeneratorVersion is already supplied
 * by FTerrainBackendInit (AR-2). A sample returns density and material together;
 * residency belongs to the backend, not the analytical field.
 * The caller owns the field's lifetime.
 *
 * THREADING. Implementations must be safe for unsynchronised concurrent const calls from
 * any thread and must hold no mutable state, memoisation included. The plugin's octree
 * queries its generator from mesher worker threads, so a field that cached anything would
 * be a data race in the one place DEF-4 already says we cannot see into.
 *
 * AR-6 (Architect determination, T-108, 2026-09-07) adds SampleRange. §4.6 declared Sample
 * alone, which is enough to fill a chunk and not enough to SKIP one: a backend octree that
 * cannot ask "is this whole region certainly solid, or certainly empty?" has to sample every
 * voxel of every region at every LOD, and a 512 m world of 50 cm voxels makes that a
 * measurable cost rather than a theoretical one. The default implementation returns the full
 * [-1, 1] range, which is always correct and merely forfeits the skip, so this widens §4.6
 * without breaking any existing implementer and without adding a required method.
 */
class TERRAINCORE_API ITerrainDensityField
{
public:
	virtual ~ITerrainDensityField() = default;

	virtual FTerrainDensitySample Sample(FIntVector Position) const = 0;

	/**
	 * Bounds on Density over a half-open voxel box (FTerrainBox: Min inclusive, Max
	 * exclusive). The default is the safe answer: "it could be anything."
	 */
	virtual FTerrainDensityRange SampleRange(const FTerrainBox& Box) const
	{
		(void)Box;
		return FTerrainDensityRange();
	}
};

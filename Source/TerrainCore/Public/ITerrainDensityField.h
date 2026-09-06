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
 * R-011 determination, T-112.2, 2026-09-06, Director-authorised in session:
 * the packet's single integer-position Sample supersedes ARCHITECTURE.md 4.6
 * lines 536-538 (Density/Material/Version). GeneratorVersion is already supplied
 * by FTerrainBackendInit (AR-2). A sample returns density and material together;
 * residency belongs to the backend, not the analytical field.
 * Declaration only: no field implementers until T-108. The caller owns its lifetime.
 */
class TERRAINCORE_API ITerrainDensityField
{
public:
	virtual ~ITerrainDensityField() = default;
	virtual FTerrainDensitySample Sample(FIntVector Position) const = 0;
};

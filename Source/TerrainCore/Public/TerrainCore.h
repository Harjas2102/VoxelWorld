// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * TerrainCore — the game-owned terrain layer (ARCHITECTURE.md §4.1).
 *
 * Holds UTerrainService, ITerrainBackend, ITerrainDensityField, the journal, the snapshot
 * store, the revision index, the streaming component and FMemoryTerrainBackend. No plugin
 * type appears in any of them, and the module compiles without a renderer.
 */
class FTerrainCoreModule : public IModuleInterface
{
};

/**
 * One log category for the whole game-owned terrain layer. Deliberately not LogVoxel: a
 * reader of the log has to be able to tell our decisions from the backend's, and after a
 * backend swap LogTerrainCore is the half that still means the same thing.
 */
TERRAINCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogTerrainCore, Log, All);

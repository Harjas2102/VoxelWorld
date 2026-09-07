// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * TerrainBackendVPLegacy — the adapter module (ARCHITECTURE.md §4.1).
 *
 * Registers FVPLegacyBackend's factory with FTerrainBackendRegistry under this module's own
 * name at StartupModule, and withdraws it at ShutdownModule. UTerrainService then finds it by
 * the name in config:
 *
 *   [/Script/TerrainCore.TerrainSettings]
 *   BackendModule=TerrainBackendVPLegacy
 *
 * That indirection is the whole reason this module exists as a separate module rather than a
 * folder: it is the only place in the project that may include a plugin header, and nothing
 * above it links against it.
 */
class FTerrainBackendVPLegacyModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** The registry key, identical to the module name. */
	static FName GetBackendName();
};

/** Adapter-side log category, so a backend's own decisions read separately from the game's. */
DECLARE_LOG_CATEGORY_EXTERN(LogTerrainBackendVPLegacy, Log, All);

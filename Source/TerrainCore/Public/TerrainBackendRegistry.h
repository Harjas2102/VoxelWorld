// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Templates/UniquePtr.h"

class ITerrainBackend;

/** Returns a fresh, uninitialised backend. Same shape as FTerrainBackendFactory in the tests. */
using FTerrainBackendCreator = TFunction<TUniquePtr<ITerrainBackend>()>;

/**
 * FTerrainBackendRegistry — how a backend module reaches the service without the service
 * knowing it exists (ARCHITECTURE.md §4.1, §10).
 *
 * THE DIRECTION OF THE DEPENDENCY IS THE POINT. TerrainCore.Build.cs lists no backend, and
 * §4.1 requires that "a plugin include in gameplay code is a compile error rather than a
 * code-review finding". So the adapter module depends on TerrainCore, not the reverse, and
 * registers its creator from its own StartupModule. UTerrainService loads the module named
 * by UTerrainSettings::BackendModule and then looks the name up here. If TerrainCore ever
 * needs to #include an adapter header to construct a backend, the boundary has failed and
 * §10's last line applies: that is a bug in the architecture, not in the new backend.
 *
 * Game thread only, and in practice module startup and world start only. It holds creators,
 * never backends: instance lifetime belongs to whoever called Create.
 */
class TERRAINCORE_API FTerrainBackendRegistry
{
public:
	static FTerrainBackendRegistry& Get();

	/**
	 * Registers a creator under a module name. Re-registering the same name replaces the
	 * creator and warns: two modules answering to one name is a configuration error that
	 * would otherwise decide which backend runs by module load order.
	 */
	void Register(FName BackendName, FTerrainBackendCreator Creator);

	/** Removes a registration. Safe for a name that was never registered. */
	void Unregister(FName BackendName);

	bool IsRegistered(FName BackendName) const;

	/** Null when the name is unregistered, or when its creator returned null. */
	TUniquePtr<ITerrainBackend> Create(FName BackendName) const;

	/** Every registered name, for diagnostics when a lookup fails. */
	TArray<FName> GetRegisteredNames() const;

private:
	TMap<FName, FTerrainBackendCreator> Creators;
};

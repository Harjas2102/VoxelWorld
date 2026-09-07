// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainBackendRegistry.h"
#include "ITerrainBackend.h"
#include "TerrainCore.h"

FTerrainBackendRegistry& FTerrainBackendRegistry::Get()
{
	// Function-local static: a backend module's StartupModule can run before any TerrainCore
	// code has, and a file-scope static would not be guaranteed constructed by then.
	static FTerrainBackendRegistry Registry;
	return Registry;
}

void FTerrainBackendRegistry::Register(FName BackendName, FTerrainBackendCreator Creator)
{
	check(IsInGameThread());
	if (BackendName.IsNone() || !Creator)
	{
		UE_LOG(LogTerrainCore, Error, TEXT("Refusing to register a terrain backend with no name or no creator."));
		return;
	}
	if (Creators.Contains(BackendName))
	{
		UE_LOG(LogTerrainCore, Warning,
			TEXT("Terrain backend '%s' was already registered; replacing it. Two modules under one name "
				 "would let module load order pick the backend."), *BackendName.ToString());
	}
	Creators.Add(BackendName, MoveTemp(Creator));
	UE_LOG(LogTerrainCore, Log, TEXT("Terrain backend registered: %s"), *BackendName.ToString());
}

void FTerrainBackendRegistry::Unregister(FName BackendName)
{
	check(IsInGameThread());
	Creators.Remove(BackendName);
}

bool FTerrainBackendRegistry::IsRegistered(FName BackendName) const
{
	return Creators.Contains(BackendName);
}

TUniquePtr<ITerrainBackend> FTerrainBackendRegistry::Create(FName BackendName) const
{
	check(IsInGameThread());
	const FTerrainBackendCreator* Creator = Creators.Find(BackendName);
	return Creator && *Creator ? (*Creator)() : nullptr;
}

TArray<FName> FTerrainBackendRegistry::GetRegisteredNames() const
{
	TArray<FName> Names;
	Creators.GetKeys(Names);
	return Names;
}

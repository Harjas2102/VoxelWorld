// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainBackendVPLegacy.h"
#include "VPLegacyBackend.h"
#include "TerrainBackendRegistry.h"

DEFINE_LOG_CATEGORY(LogTerrainBackendVPLegacy);

FName FTerrainBackendVPLegacyModule::GetBackendName()
{
	// Identical to the module name, because UTerrainService loads the module by the same
	// string it then looks up. One name, one config line, one place to be wrong (§10).
	return TEXT("TerrainBackendVPLegacy");
}

void FTerrainBackendVPLegacyModule::StartupModule()
{
	FTerrainBackendRegistry::Get().Register(GetBackendName(), []() -> TUniquePtr<ITerrainBackend>
	{
		return MakeUnique<FVPLegacyBackend>();
	});
}

void FTerrainBackendVPLegacyModule::ShutdownModule()
{
	// Withdraw before the module's code is unloaded: a creator left behind would be a
	// dangling lambda the service could still call.
	FTerrainBackendRegistry::Get().Unregister(GetBackendName());
}

IMPLEMENT_MODULE(FTerrainBackendVPLegacyModule, TerrainBackendVPLegacy);

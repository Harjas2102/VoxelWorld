// Copyright VoxelWorld. See Docs/proposals/P-010-settlement-ledger.md.

#include "EntityLedger.h"
#include "Modules/ModuleManager.h"

/**
 * Registers the SQLite ledger with TerrainCore at startup, under this module's name -- the
 * name UTerrainSettings::SettlementModule selects. TerrainCore loads the module by that name
 * and asks the registry; it never includes a header from here (P-010 §1).
 */
class FEntityStoreModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FTerrainSettlementRegistry::Get().Register(TEXT("EntityStore"),
			[]() -> TUniquePtr<ITerrainSettlementLedger> { return MakeUnique<FEntityLedger>(); });
	}

	virtual void ShutdownModule() override
	{
		FTerrainSettlementRegistry::Get().Unregister(TEXT("EntityStore"));
	}
};

IMPLEMENT_MODULE(FEntityStoreModule, EntityStore)

// Copyright VoxelWorld. See Docs/proposals/P-010-settlement-ledger.md.

using UnrealBuildTool;

public class EntityStore : ModuleRules
{
	public EntityStore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// D-012: SQLite holds entities. This is the ONLY module that links it, for the same reason
		// TerrainBackendVPLegacy is the only one that links the voxel plugin: TerrainCore reaches
		// the ledger through FTerrainSettlementRegistry and never includes a header from here, so
		// swapping the entity store is a config line and a module, not an edit to the core.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"TerrainCore",
		});

		// The persistence test fixtures (identities, digests, a valid base descriptor) are shared
		// with TerrainCore's own tests rather than copied; headers only, tests only.
		PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "..", "TerrainCore", "Private", "Tests"));

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"SQLiteCore",
		});
	}
}

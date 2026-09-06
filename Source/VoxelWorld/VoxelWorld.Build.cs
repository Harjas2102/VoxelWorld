// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

using UnrealBuildTool;

public class VoxelWorld : ModuleRules
{
	public VoxelWorld(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// ARCHITECTURE.md §4.1: "VoxelWorld depends on TerrainCore only."
		// The terrain backend module is never listed here. Gameplay reaches terrain
		// through TerrainCore's game-owned types, never through a plugin header (D-011,
		// AGENTS.md §4 and §9).
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"TerrainCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}

// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

using UnrealBuildTool;

public class TerrainCore : ModuleRules
{
	public TerrainCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// ARCHITECTURE.md §4.1 — THE BOUNDARY IS THIS FILE.
		//
		//   "TerrainCore does not list the plugin in its Build.cs, so a plugin include in
		//    gameplay code is a compile error rather than a code-review finding."
		//
		// This module is game-owned, holds no plugin type, and compiles headless. Only
		// TerrainBackendVPLegacy (ARCHITECTURE.md §4.1, not yet built) ever depends on the
		// voxel plugin. Adding a plugin dependency to this list breaks D-011, AGENTS.md §4
		// and the AGENTS.md §9 drift guard, and requires a numbered decision — not an edit.
		// §4.1.0 is the argument for why, when someone proposes the shortcut.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}

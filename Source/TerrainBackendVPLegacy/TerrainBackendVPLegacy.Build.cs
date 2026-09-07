// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

using UnrealBuildTool;

public class TerrainBackendVPLegacy : ModuleRules
{
	public TerrainBackendVPLegacy(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// The Voxel module is compiled as C++20 and its public headers assume it.
		CppStandard = CppStandardVersion.Cpp20;

		// ARCHITECTURE.md §4.1 — THIS IS THE ONLY MODULE ALLOWED TO LIST THE PLUGIN.
		//
		//   "TerrainBackendVPLegacy  (the ONLY module that includes plugin headers)"
		//
		// TerrainCore and VoxelWorld both forbid "Voxel" in this list, and that build-system
		// fact — not review, not discipline — is what makes a plugin include in gameplay code
		// a compile error (§4.1.0, D-011, AGENTS.md §4 and §9). The boundary is one-directional:
		// this module depends on TerrainCore, TerrainCore never depends on this module. It
		// reaches the service by registering a factory with FTerrainBackendRegistry at startup,
		// which is what lets §10 make a backend swap a config line plus a new module.
		//
		// Nothing here may be added to VoxelWorld.Build.cs or TerrainCore.Build.cs to "share"
		// a type. If a plugin type needs to reach gameplay, the answer is a game-owned type in
		// TerrainCore, or an escalation under AGENTS.md §10 — never a dependency edit.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"TerrainCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Voxel",
		});
	}
}

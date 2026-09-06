// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

using UnrealBuildTool;
using System.Collections.Generic;

public class VoxelWorldTarget : TargetRules
{
	public VoxelWorldTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;

		// ARCHITECTURE.md §4.1. TerrainCore is listed explicitly so the module exists in
		// every target, including a dedicated server, independently of the game module.
		ExtraModuleNames.AddRange(new string[] { "TerrainCore", "VoxelWorld" });
	}
}

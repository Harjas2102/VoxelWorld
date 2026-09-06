// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

using UnrealBuildTool;
using System.Collections.Generic;

public class VoxelWorldEditorTarget : TargetRules
{
	public VoxelWorldEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;

		ExtraModuleNames.AddRange(new string[] { "TerrainCore", "VoxelWorld" });
	}
}

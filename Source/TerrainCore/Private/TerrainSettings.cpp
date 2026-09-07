// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainSettings.h"
#include "TerrainChunk.h"

FTerrainBox UTerrainSettings::GetWorldBoundsVox() const
{
	// Centred on voxel (0,0,0), half-open. An odd size loses the extra voxel on the max
	// side rather than producing an asymmetric world the quantiser would have to know about.
	const int32 Half = WorldSizeVoxels / 2;
	return FTerrainBox(FIntVector(-Half), FIntVector(Half));
}

FTransform UTerrainSettings::GetTerrainOrigin() const
{
	return FTransform(FQuat::Identity, TerrainOriginWorld, FVector::OneVector);
}

bool UTerrainSettings::IsValid() const
{
	return !BackendModule.IsNone()
		&& VoxelSizeCm > 0.f && FMath::IsFinite(VoxelSizeCm)
		&& WorldSizeVoxels >= 2 * TerrainChunkSizeVox
		&& TerrainOriginWorld.ContainsNaN() == false
		&& GeneratorVersion >= 0
		&& MaxEditRadiusCm > 0.0 && FMath::IsFinite(MaxEditRadiusCm)
		&& MaxVoxelsPerOp > 0
		&& DefaultInterestRadiusCm >= 0.0 && FMath::IsFinite(DefaultInterestRadiusCm);
}

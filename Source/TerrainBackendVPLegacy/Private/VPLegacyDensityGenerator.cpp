// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "VPLegacyDensityGenerator.h"

FLinearColor TerrainMaterialDebugColor(FTerrainMatId Id)
{
	// Chosen to be distinguishable in a cliff face at fifty metres, not to be pretty. See the
	// header: this is a debug palette for T-108, not the K9 material catalog.
	switch (Id)
	{
	case ETerrainMaterial::Topsoil:   return FLinearColor(0.16f, 0.34f, 0.10f);   // green
	case ETerrainMaterial::Dirt:      return FLinearColor(0.36f, 0.24f, 0.13f);   // brown
	case ETerrainMaterial::Stone:     return FLinearColor(0.47f, 0.47f, 0.49f);   // grey
	case ETerrainMaterial::DeepStone: return FLinearColor(0.26f, 0.27f, 0.31f);   // dark grey
	case ETerrainMaterial::Bedrock:   return FLinearColor(0.11f, 0.11f, 0.12f);   // near black
	case ETerrainMaterial::IronOre:   return FLinearColor(0.62f, 0.28f, 0.13f);   // rust
	case ETerrainMaterial::Air:       return FLinearColor(0.00f, 0.00f, 0.00f);
	default:                          return FLinearColor(1.00f, 0.00f, 1.00f);   // magenta: unmapped
	}
}

TVoxelSharedRef<FVoxelGeneratorInstance> UVPLegacyDensityGenerator::GetInstance()
{
	return MakeVoxelShared<FVPLegacyDensityGeneratorInstance>(*this);
}

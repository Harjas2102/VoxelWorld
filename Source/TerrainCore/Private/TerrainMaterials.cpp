// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainMaterials.h"

const TCHAR* TerrainMaterialName(FTerrainMatId Id)
{
	switch (Id)
	{
	case ETerrainMaterial::Unknown:   return TEXT("Unknown");
	case ETerrainMaterial::Air:       return TEXT("Air");
	case ETerrainMaterial::Topsoil:   return TEXT("Topsoil");
	case ETerrainMaterial::Dirt:      return TEXT("Dirt");
	case ETerrainMaterial::Stone:     return TEXT("Stone");
	case ETerrainMaterial::DeepStone: return TEXT("DeepStone");
	case ETerrainMaterial::Bedrock:   return TEXT("Bedrock");
	case ETerrainMaterial::IronOre:   return TEXT("IronOre");
	default:                          return TEXT("<unregistered>");
	}
}

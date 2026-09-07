// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "TerrainTypes.h"

/**
 * TerrainMaterials.h — the GAME's material id catalog (ARCHITECTURE.md §4.2, §8.1).
 *
 * §4.2: "FTerrainMatId = uint16 — GAME material id. Never a plugin index." §8.1 assigns
 * "material identity, ore grade, catalog versioning" to the game. This header is that
 * catalog's first entry, introduced at T-108 because a density field that returns strata
 * has to name them.
 *
 * WHAT THIS DOES NOT DECIDE. Fork K9 — the mapping from a game material id to a plugin
 * material index, and the yield economy built on it — is build step 6 and DEF-6 is open.
 * Nothing here assigns a hardness, a yield, a stack size, an ore grade or a plugin index.
 * The adapter renders these ids as flat colours so the strata are VISIBLE; that colour
 * table is cosmetic and is not the K9 mapping (see VPLegacyDensityGenerator.h).
 *
 * IDS ARE PERMANENT. An id written into a saved chunk is read back by number, so a value
 * here may be added to but never reused for a different material and never renumbered.
 * Changing what an existing id MEANS is a save migration, not an edit to this file.
 */
namespace ETerrainMaterial
{
	enum Type : FTerrainMatId
	{
		/** Not "air". Zero is what a backend returns when it does not know (§4.3, K9). */
		Unknown   = 0,

		Air       = 1,
		Topsoil   = 2,
		Dirt      = 3,
		Stone     = 4,
		DeepStone = 5,
		Bedrock   = 6,
		IronOre   = 7,

		/** One past the last id in use. Not a material. */
		Count     = 8,
	};
}

/** Debug/log name for a material id. Never parsed, never persisted. */
TERRAINCORE_API const TCHAR* TerrainMaterialName(FTerrainMatId Id);

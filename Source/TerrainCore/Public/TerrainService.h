// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TerrainService.generated.h"

/**
 * UTerrainService — the authoritative terrain service (ARCHITECTURE.md §4.1, §4.4).
 *
 * A UWorldSubsystem, not an actor: fork K7, ruled at CP-005 by D-024 — "the service is
 * authority, not a thing in the world". It exists on both server and client; HasAuthority
 * gates the authoritative half.
 *
 * Empty at build step 0 (ARCHITECTURE.md §9). Validation, sequencing, revisions, journalling
 * and yield arrive at build steps 1 and 3.
 */
UCLASS()
class TERRAINCORE_API UTerrainService : public UWorldSubsystem
{
	GENERATED_BODY()
};

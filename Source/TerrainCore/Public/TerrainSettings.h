// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "TerrainTypes.h"
#include "TerrainSettings.generated.h"

/**
 * UTerrainSettings — the config-owned terrain parameters (ARCHITECTURE.md §4.1, §10).
 *
 *   [/Script/TerrainCore.TerrainSettings]
 *   BackendModule=TerrainBackendVPLegacy
 *
 * §4.1 names that exact section and key, and §10 step 4 makes it the whole of a backend
 * swap: "Set BackendModule=TerrainBackendX in config. No gameplay code changes, no asset
 * changes, no save format changes." Nothing in TerrainCore links the adapter; the service
 * loads the named module and asks FTerrainBackendRegistry for its factory.
 *
 * THE COORDINATE POLICY LIVES HERE, NOT IN THE BACKEND. §8.1 assigns "chunk keys and
 * coordinate policy" to the game. VoxelSizeCm and TerrainOriginWorld are what the service
 * quantises against, and the adapter is required to conform its actor to them — a backend
 * that disagreed would move every existing save by a voxel. Changing either value after a
 * world has been dug is a save-format change (FM-9 resample migration), not a tuning knob.
 *
 * A plain config UObject, not a UDeveloperSettings: that class lives in the DeveloperSettings
 * module, and TerrainCore's dependency list is the D-011 boundary (§4.1.0). Adding a module
 * to it to gain a settings page is not a trade this file is worth making.
 */
UCLASS(config = Engine, defaultconfig)
class TERRAINCORE_API UTerrainSettings : public UObject
{
	GENERATED_BODY()

public:
	/** The module implementing ITerrainBackend. Loaded by name; never a build dependency. */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|Backend")
	FName BackendModule = TEXT("TerrainBackendVPLegacy");

	/** Centimetres per voxel. T-101A ran at 50: at the default 100 a 200 uu brush reads as nothing. */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|World")
	float VoxelSizeCm = 50.f;

	/** World edge in voxels, centred on the origin. 1024 * 50 cm = 512 m, the T-101A world. */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|World")
	int32 WorldSizeVoxels = 1024;

	/** Voxel (0,0,0) in world space. The adapter conforms its actor to this. */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|World")
	FVector TerrainOriginWorld = FVector::ZeroVector;

	/** Generator seed, forwarded to ITerrainBackend::Initialize. No implementer until T-108. */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|World")
	int32 Seed = 0;

	/**
	 * Version of the world's shape. Recorded in every snapshot header (§4.2) so a chunk
	 * written by one generator is never silently reinterpreted by another. Stored as int32
	 * because UHT has no uint32 property type; the value is non-negative.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|World", meta = (ClampMin = "0"))
	int32 GeneratorVersion = 0;

	/** Largest single edit radius a request may carry, in centimetres. Admission check only. */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|Edit", meta = (ClampMin = "1"))
	double MaxEditRadiusCm = 1000.0;

	/**
	 * ARCHITECTURE.md §7.1: MaxVoxelsPerOp = 65,536 written, about a 25-voxel radius.
	 * Larger requests split into sub-ops sharing a TransactionId — and splitting is build
	 * step 3 work under DEF-7, so until then an over-large request is rejected, not split.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|Edit", meta = (ClampMin = "1"))
	int32 MaxVoxelsPerOp = 65536;

	/** Default streaming interest radius for UTerrainStreamingComponent, in centimetres. */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|Streaming", meta = (ClampMin = "0"))
	double DefaultInterestRadiusCm = 10000.0;

	/** Voxel-space world bounds implied by WorldSizeVoxels, centred on voxel (0,0,0). */
	FTerrainBox GetWorldBoundsVox() const;

	/** Terrain-local to world. Identity rotation and scale: the grid is axis-aligned (§4.3). */
	FTransform GetTerrainOrigin() const;

	/** True when every value above is usable by the service and the backend. */
	bool IsValid() const;
};

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

	/**
	 * Whether the server durably records terrain edits and replays them at startup.
	 *
	 * **Default true: a world that does not remember is not this game** (Pillar 1). Turn it
	 * off to run the pre-persistence behaviour, which is what every build through step 3 did.
	 *
	 * Known limits while DEF-1/2/9 are open, and they are real: checkpoint
	 * capture is opt-in, so default startup re-applies every edit; scanning remains unbounded;
	 * there is no retention, so the journal only grows; and the crash matrix has not been
	 * run. Watch the replay time logged at startup.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|Persistence")
	bool bPersistEdits = true;

	/**
	 * The world directory under `Saved/Worlds/`. One name is one world's permanent history.
	 *
	 * Changing it starts a different world rather than migrating this one, and pointing two
	 * running servers at one name is unsupported (P-003 section 5 requires exclusive writer
	 * ownership, and the lease that would enforce it is not built).
	 */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|Persistence")
	FString WorldStoreName = TEXT("Default");

	/**
	 * Whether the server takes checkpoints. **Default false, and the reason is a measurement.**
	 *
	 * Checkpoints are what stop startup replay growing without bound, and they work: with them
	 * on, a restart restores the cut and replays nothing. But capture is currently SYNCHRONOUS,
	 * and measured on the production adapter it costs **~42 ms per chunk** — 0.36 s to capture
	 * 8 chunks. At the soft trigger below that projects to roughly **11 seconds of frozen
	 * game**, and P-003 section 4 is explicit that a visible multi-second stall under the
	 * supported workload FAILS.
	 *
	 * P-003 section 4 also named the cause before it was measured: *"the current
	 * 32,768-per-voxel-call adapter path must gain a measured bulk-read implementation before
	 * production integration."* Two things fix this — a bulk `ReadRegion` in the adapter, and
	 * the incremental copy-before-write pump — and neither is built.
	 *
	 * Turning it on is a real trade and it is yours to make: an occasional multi-second freeze
	 * during play, in exchange for a startup that does not slow down forever. Journalling is
	 * unaffected either way, so nothing is lost by leaving this off — only replay stays
	 * unbounded (DEF-2).
	 */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|Persistence")
	bool bCheckpointCapture = false;

	/**
	 * Dirty chunks that trigger a checkpoint, when `bCheckpointCapture` is on.
	 *
	 * P-003 section 4's soft trigger is 256. A larger value means rarer, longer stalls and a
	 * smaller one means more frequent, shorter ones; at ~42 ms per chunk neither is good, which
	 * is the point the measurement is making.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|Persistence", meta = (ClampMin = "1"))
	int32 CheckpointDirtyChunkTrigger = 256;

	/** Also capture after this many commits, so repeated edits to a few chunks trigger a cut. */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|Persistence", meta = (ClampMin = "1"))
	int32 CheckpointOpTrigger = 256;

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

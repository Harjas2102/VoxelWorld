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
	 * Whether the server takes checkpoints. **Default true, and the reason is a measurement
	 * taken at the trigger this actually fires at.**
	 *
	 * Checkpoints are what stop startup replay growing without bound: with them on, a restart
	 * restores the cut and replays nothing. Capture is SYNCHRONOUS, so the only question that
	 * ever mattered is what it costs, and P-003 section 4 is explicit that a visible
	 * multi-second stall under the supported workload FAILS.
	 *
	 * It used to cost ~42 ms per chunk, which projected to roughly 22 seconds of frozen game at
	 * the trigger below. Two fixes landed. The adapter gained a bulk `ReadRegion` (**D-035**),
	 * and objects are now written in packs — one file and one flush per capture instead of 59
	 * (**D-036**, P-004 section 13).
	 *
	 * **Measured at the real 256-chunk trigger, not extrapolated: 0.162 s.** 256 chunks,
	 * 33,587,200 bytes, 1,155 index pages, at 0.6 ms per chunk — read 0.089, encode 0.005,
	 * store 0.026, index 0.016, publish 0.025. Note the index: 1,155 durable pages in 0.016 s
	 * because they share one pack, where before they would have been about 3.5 seconds of
	 * `fsync` on their own.
	 *
	 * That is an order of magnitude inside the gate, so the trade flips. Leaving capture off
	 * buys a world whose startup slows down forever; turning it on costs an occasional stall
	 * of about a sixth of a second. `Terrain.StressCapture` reproduces the measurement, and
	 * every capture logs its own phase times, so this can be rechecked rather than believed.
	 *
	 * **The tail is real and is not fixed.** Capture only runs when the queue is empty, and
	 * admission closes at `TerrainCheckpointDirtyHardBound` (4,096) dirty chunks. A server busy
	 * enough that the queue never drains would accumulate toward that bound and then take a
	 * capture roughly sixteen times this one — back into multi-second territory — while
	 * refusing edits until it drained. That is what the incremental copy-before-write pump is
	 * for (DEF-2), and it is the next thing capture needs. Set this to false to opt out; the
	 * journal is unaffected either way and nothing is lost but bounded replay.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|Persistence")
	bool bCheckpointCapture = true;

	/**
	 * Game-thread milliseconds a checkpoint may spend per frame reading and encoding chunks.
	 *
	 * The pump spreads capture across frames (P-003 section 4, DEF-2), so this is the knob that
	 * decides how visible it is. At the measured ~0.35 ms per chunk, 2 ms buys roughly five or
	 * six chunks a frame and clears a full 256-chunk trigger in about fifty frames -- under a
	 * second of wall time, none of it a hitch.
	 *
	 * Raising it finishes captures sooner at the cost of a bigger per-frame bite; lowering it
	 * makes captures gentler and longer. It does **not** bound publication, which is one
	 * unbroken step at the end and measured 0.041 s at the 256-chunk trigger.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|Persistence", meta = (ClampMin = "0.05", ClampMax = "50.0"))
	double CheckpointPumpMillisPerFrame = 2.0;

	/**
	 * Dirty chunks that trigger a checkpoint, when `bCheckpointCapture` is on.
	 *
	 * P-003 section 4's soft trigger is 256. A larger value means rarer, longer stalls and a
	 * smaller one means more frequent, shorter ones; at ~42 ms per chunk neither is good, which
	 * is the point the measurement is making.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|Persistence", meta = (ClampMin = "1"))
	int32 CheckpointDirtyChunkTrigger = 256;

	/**
	 * The fewest commits since the last cut before the edit-count trigger may fire, so repeated
	 * edits to a few chunks still get a cut. It fires only when CheckpointOpsPerDirtyChunk agrees.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|Persistence", meta = (ClampMin = "1"))
	int32 CheckpointOpTrigger = 256;

	/**
	 * The edit-count trigger's price, in edits per dirty chunk (P-012 §5): a capture re-reads
	 * every dirty chunk, and is worth taking only once the replay it saves costs as much.
	 *
	 * Measured on the development machine: a chunk read costs about 2.8 ms and replaying one edit
	 * at boot about 0.35 ms, so one chunk is worth about 8 edits. With 140 dirty chunks the cut
	 * waits for 1,120 edits instead of 256, and a quiet world editing 32 chunks behaves as before.
	 * 0 disables the pricing and restores the plain edit count.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|Persistence", meta = (ClampMin = "0"))
	int32 CheckpointOpsPerDirtyChunk = 8;

	/**
	 * Always capture once the replay tail reaches this many edits, whatever the pricing says:
	 * it bounds boot time (about 1.6 s of replay at the measured cost), so a wrong price can
	 * never make startup unbounded.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|Persistence", meta = (ClampMin = "1"))
	int32 CheckpointMaxReplayOps = 4096;

	/**
	 * Reclaim superseded checkpoint data in the background after each checkpoint (DEF-9).
	 *
	 * Without it the save only grows: each checkpoint at the 256-chunk trigger leaves about 33 MB
	 * of superseded payloads behind. The collector marks and copies on a worker thread and does
	 * only short steps on the game thread; it deletes nothing if a capture references anything
	 * mid-cycle (P-003 §5's epoch rule). Turning it off loses nothing but disk space.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|Persistence")
	bool bBackgroundRetention = true;

	/**
	 * The module that provides the settlement ledger (P-010), loaded by name like BackendModule.
	 * None disables settlement: every edit then records NoEconomy and nothing is paid.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|Persistence")
	FName SettlementModule = TEXT("EntityStore");

	/**
	 * Largest retention copy frame, in megabytes. Bounds the worker's buffer and the one
	 * game-thread append per step -- the only retention step whose cost scales with data.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Terrain|Persistence", meta = (ClampMin = "0.0625", ClampMax = "256.0"))
	double RetentionFrameMegabytes = 4.0;

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

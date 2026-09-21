// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "CoreMinimal.h"
#include "ITerrainBackend.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/StrongObjectPtr.h"

// The scratch array below holds FModifiedVoxelValue by value, so this header needs the
// complete type. That is legal HERE and nowhere else: this header lives in Private/, so no
// other module can include it and pick the plugin up through it (§4.1).
#include "VoxelTools/Gen/VoxelToolsBase.h"

// TStrongObjectPtr requires a complete UObject type, so a forward declaration will not do.
// Legal for the same reason as the include above: both headers are private to this module.
#include "VPLegacyDensityGenerator.h"

class AVoxelWorld;
class UVoxelSimpleInvokerComponent;

/**
 * FVPLegacyBackend — ITerrainBackend on Voxel Plugin Free Legacy (ARCHITECTURE.md §4.1, §4.3).
 *
 * THIS HEADER IS PRIVATE ON PURPOSE. Putting it in Public/ would let another module include
 * it, and through it the plugin types it forward-declares. Nothing outside this module ever
 * names this class: UTerrainService receives it as a TUniquePtr<ITerrainBackend> from
 * FTerrainBackendRegistry, and that is the only way it is ever constructed.
 *
 * WHAT IT OWNS (§8.1, "Plugin" column): density storage and the octree, meshing, LOD,
 * collision cooking, the sphere edit kernels, and invoker mechanics. Everything else —
 * edit semantics, ordering, revisions, validation, material identity, yield, persistence,
 * relevancy, chunk keys and streaming INTENT — belongs to the game and is not decided here.
 *
 * BUILD STEP 3 SUPPORT AND REMAINING LIMITS:
 *
 *  - ApplyOp: canonical Remove and Add spheres and boxes, the two operations T-101A proved and the two the
 *    rewired dig Blueprint issues. Flatten and Smooth are unsupported under DEF-5, which
 *    records that they "are named without plane, strength, iteration or falloff semantics";
 *    Paint is unsupported because a game material id has no plugin index to map onto until
 *    fork K9 lands at build step 6. An unsupported op is refused, never approximated.
 *
 *  - FTerrainEditResult::Removed is MEASURED (P-009, T-130): signed per game material, from the
 *    plugin's own per-voxel old/new values and one bulk material read, with the occupancy
 *    function the reference backend uses. Physical volume only: tool efficiency and every other
 *    economic conversion stay above the backend (§4.2, DEF-6's economic half).
 *
 *  - Game material ids are exact. The world is on the RGB config and the generator colours each
 *    voxel by its game id, one distinct colour per catalog entry, so the adapter inverts that
 *    same colour function (P-009 §3). ReadRegion, WriteRegion, QueryPoint and Add all carry ids.
 *
 *  - HashRegion hashes DENSITY ONLY, deliberately: Adapter.DensityContract's pinned fixtures are
 *    density hashes, and keeping them unchanged is what proves P-009 moved no density sample.
 *    Materials are verified by the adapter checks' direct ReadRegion comparisons instead.
 *
 *  - FlushPendingWork is a no-op by design, not by omission. §4.5: "rendering and collision
 *    updates remain the plugin's own async work and are explicitly not serialised by us."
 *    There is no plugin call that makes them synchronous, and pretending otherwise would
 *    hide the exact seam DEF-8 is about.
 *
 * Because of the above, this backend does NOT pass the full §6.1 Backend.Conformance suite
 * yet; the density-only step-3 evidence is narrower. §10 requires that pass before this is a proven
 * replaceable backend; the material and snapshot halves of the contract arrive with the
 * steps that decide them.
 *
 * THREADING. Game thread only. §4.5 ruled the serialised execution path onto the game thread
 * (K4, D-024); DEF-4 specifies lifecycle safety in section 4.5.1.
 * Every entry point here checks IsInGameThread rather than
 * relying on callers, and the edits run single-threaded (bMultiThreaded = false) until E-2
 * has shown the plugin's parallel mode to be deterministic — client results supply collision
 * and are not cosmetic, so a nondeterministic mode cannot be kept on either side (DEF-5).
 */
class FVPLegacyBackend final : public ITerrainBackend
{
public:
	FVPLegacyBackend() = default;
	virtual ~FVPLegacyBackend() override;

	//~ Begin ITerrainBackend Interface — the eleven methods of §4.3
	virtual bool Initialize(const FTerrainBackendInit& InInit) override;
	virtual void Shutdown() override;
	virtual bool ApplyOp(const FTerrainOp& Op, FTerrainEditResult& Out) override;
	virtual bool ReadRegion(const FTerrainChunkKey& Key, FTerrainRegionData& Out) override;
	virtual bool WriteRegion(const FTerrainRegionData& In) override;
	virtual uint64 HashRegion(const FTerrainChunkKey& Key) const override;
	virtual bool IsRegionResident(const FTerrainChunkKey& Key) const override;
	virtual void FlushPendingWork() override;
	virtual bool QueryPoint(const FIntVector& VoxelPos, FTerrainPointSample& Out) const override;
	virtual void SetStreamingInterest(const FTerrainStreamingInterest& In) override;
	virtual void ClearStreamingInterest(uint32 InterestId) override;
	virtual bool MeasuresPhysicalYield() const override;   // P-009
	//~ End ITerrainBackend Interface

private:
	/** Finds the level's voxel world, or spawns one. Null on failure; never returns a stale actor. */
	AVoxelWorld* AcquireVoxelWorld();

	/**
	 * Forces the actor onto the GAME's grid: origin, voxel size and world size come from
	 * FTerrainBackendInit, never from whatever the actor was authored or dragged to (AR-5,
	 * §8.1). Returns false if the actor cannot be made to agree.
	 */
	bool ConformVoxelWorld(AVoxelWorld& Actor);

	/** Valid, created actor, or null. Every method goes through this rather than the raw pointer. */
	AVoxelWorld* GetLiveVoxelWorld() const;

	/** True when the whole half-open voxel box lies inside the configured world bounds. */
	bool IsBoxInWorld(const FTerrainBox& Box) const;

	bool bInitialized = false;
	FTerrainBackendInit Init;

	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<AVoxelWorld> VoxelWorld;

	/** True only for an actor this backend spawned, which is the only one it may destroy. */
	bool bSpawnedVoxelWorld = false;

	/** Per-op yield accumulator, reused so an edit does not allocate a map (P-009). */
	TMap<FTerrainMatId, double> ScratchVolumes;

	/**
	 * The plugin-side generator, forwarding to FTerrainBackendInit::DensityField (T-108, §4.6).
	 * Strong, not weak: the actor's own FVoxelGeneratorPicker would keep it alive on a level
	 * actor, but a transient spawned world and a failed create both leave windows where it
	 * would not, and a collected generator mid-mesh is a crash rather than a wrong shape.
	 */
	TStrongObjectPtr<UVPLegacyDensityGenerator> Generator;

	/** One invoker component per streaming interest, owned by the voxel world actor. */
	TMap<uint32, TWeakObjectPtr<UVoxelSimpleInvokerComponent>> Invokers;

	/**
	 * Reused across ops to avoid reallocating a large array per edit. §4.3: "the C++ tool
	 * overload APPENDS to the out-array. The adapter's scratch array is reset per op,
	 * explicitly. A missed reset is a yield-inflation bug that grows over a session."
	 */
	TArray<FModifiedVoxelValue> ScratchModified;
};

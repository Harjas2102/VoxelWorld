// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "CoreMinimal.h"
#include "ITerrainBackend.h"
#include "UObject/WeakObjectPtr.h"

// The scratch array below holds FModifiedVoxelValue by value, so this header needs the
// complete type. That is legal HERE and nowhere else: this header lives in Private/, so no
// other module can include it and pick the plugin up through it (§4.1).
#include "VoxelTools/Gen/VoxelToolsBase.h"

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
 * WHAT BUILD STEP 2 IMPLEMENTS, AND WHAT IT HONESTLY DOES NOT:
 *
 *  - ApplyOp: Remove and Add spheres, the two operations T-101A proved and the two the
 *    rewired dig Blueprint issues. Flatten and Smooth are unsupported under DEF-5, which
 *    records that they "are named without plane, strength, iteration or falloff semantics";
 *    Paint is unsupported because a game material id has no plugin index to map onto until
 *    fork K9 lands at build step 6. An unsupported op is refused, never approximated.
 *
 *  - FTerrainEditResult::Removed is left EMPTY. Yield is build step 6 and DEF-6 is open;
 *    §2.3 records that FModifiedVoxelValue "carries no material", so producing per-material
 *    volumes needs a separate bulk material read AND the K9 id/index table. Returning a
 *    plausible-looking number from the RGB material config the world currently uses would be
 *    inventing the economy, which §4.2 explicitly moved above the backend to prevent.
 *    bRecordModifiedValues is nonetheless always on (§4.3) and VoxelsTouched is real.
 *
 *  - ReadRegion / WriteRegion / HashRegion move DENSITY ONLY, through the plugin's public
 *    per-voxel data tools, in the §4.7 dense layout. Materials are written as zero for the
 *    same K9 reason. This is a working convergence oracle and a working region transfer for
 *    density; it is NOT the snapshot format, which is build step 4 under K3 and DEF-9. It is
 *    also O(32768) locked accesses per chunk, which is fine for a test and not for a server.
 *
 *  - FlushPendingWork is a no-op by design, not by omission. §4.5: "rendering and collision
 *    updates remain the plugin's own async work and are explicitly not serialised by us."
 *    There is no plugin call that makes them synchronous, and pretending otherwise would
 *    hide the exact seam DEF-8 is about.
 *
 * Because of the above, this backend does NOT pass the full §6.1 Backend.Conformance suite
 * yet, and nothing in T-113 claims it does. §10 requires that pass before this is a proven
 * replaceable backend; the material and snapshot halves of the contract arrive with the
 * steps that decide them.
 *
 * THREADING. Game thread only. §4.5 ruled the serialised execution path onto the game thread
 * (K4, D-024) and DEF-4 remains open: "a serialised execution path does not establish plugin
 * thread or lifetime safety". Every entry point here checks IsInGameThread rather than
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

	/** One invoker component per streaming interest, owned by the voxel world actor. */
	TMap<uint32, TWeakObjectPtr<UVoxelSimpleInvokerComponent>> Invokers;

	/**
	 * Reused across ops to avoid reallocating a large array per edit. §4.3: "the C++ tool
	 * overload APPENDS to the out-array. The adapter's scratch array is reset per op,
	 * explicitly. A missed reset is a yield-inflation bug that grows over a session."
	 */
	TArray<FModifiedVoxelValue> ScratchModified;
};

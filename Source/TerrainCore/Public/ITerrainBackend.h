// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "TerrainOp.h"

class UWorld;

class ITerrainDensityField;

/** ARCHITECTURE.md header ruling AR-2. The density field is borrowed until Shutdown. */
struct FTerrainBackendInit
{
	int32 Seed = 0;
	uint32 GeneratorVersion = 0;
	float VoxelSizeCm = 50.f;
	FTerrainBox WorldBoundsVox;
	const ITerrainDensityField* DensityField = nullptr;
	ETerrainRole Role = ETerrainRole::Server;

	/**
	 * AR-5 (T-113, 2026-09-06). §4.3 listed Initialize's inputs as "seed, gen version, voxel
	 * size, bounds, density field, role" and stopped there, which is complete for a backend
	 * that owns nothing but memory. FVPLegacyBackend has to find or spawn an AVoxelWorld
	 * actor, attach invoker components to it and destroy them on Shutdown, and every one of
	 * those needs a UWorld. Without this the adapter would have to reach for
	 * GWorld or GEngine->GetWorldContexts() and guess — which is worse than saying so.
	 *
	 * UWorld and FTransform are ENGINE types, not plugin types, so this widens what the game
	 * tells a backend without widening what a backend may tell the game. FMemoryTerrainBackend
	 * ignores both fields and still compiles and runs with no engine world (§6.1); the
	 * conformance suite leaves them defaulted.
	 *
	 * Null World is legal and means "headless": a backend that needs one fails Initialize.
	 */
	UWorld* World = nullptr;

	/**
	 * AR-5. Voxel (0,0,0) in world space, with identity rotation and scale.
	 *
	 * §8.1 assigns coordinate policy to the GAME, and §4.3 requires the server to quantise
	 * exactly once. Both are only true if the service and the backend agree on where the
	 * grid starts, so the game states the origin and the backend conforms its actor to it.
	 * A backend that read the origin off its own actor instead would silently move every
	 * existing edit by whatever the actor had been dragged to.
	 */
	FTransform OriginTransform = FTransform::Identity;
};

/** The eleven methods of ARCHITECTURE.md 4.3; no plugin or world dependencies. */
class TERRAINCORE_API ITerrainBackend
{
public:
	virtual ~ITerrainBackend() = default;

	virtual bool Initialize(const FTerrainBackendInit& Init) = 0;
	virtual void Shutdown() = 0;

	// Authoritative mutation. Server: full result. Client: result ignored.
	virtual bool ApplyOp(const FTerrainOp& Op, FTerrainEditResult& Out) = 0;

	// Region transfer — snapshot capture and restore.
	virtual bool ReadRegion (const FTerrainChunkKey& Key, FTerrainRegionData& Out) = 0;
	virtual bool WriteRegion(const FTerrainRegionData& In) = 0;

	// Position-sensitive values + materials; independent of traversal order (DEF-5).
	virtual uint64 HashRegion(const FTerrainChunkKey& Key) const = 0;
	virtual bool IsRegionResident(const FTerrainChunkKey& Key) const = 0;
	virtual void FlushPendingWork() = 0;

	virtual bool QueryPoint(const FIntVector& VoxelPos,
	                        FTerrainPointSample& Out) const = 0;

	virtual void SetStreamingInterest  (const FTerrainStreamingInterest& In) = 0;
	virtual void ClearStreamingInterest(uint32 InterestId) = 0;
};

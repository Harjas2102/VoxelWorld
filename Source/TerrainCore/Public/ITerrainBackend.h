// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "TerrainOp.h"

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

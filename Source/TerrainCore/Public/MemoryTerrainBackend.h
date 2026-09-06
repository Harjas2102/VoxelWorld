// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "ITerrainBackend.h"

/**
 * Dense, synchronous reference backend. No renderer, plugin or engine world.
 *
 * R-011 determinations, T-112.2, 2026-09-06, Director-authorised in session:
 * - AR-2's unknown-space rule takes precedence over allocating air on interest.
 *   Residency requires BOTH interest and data (WriteRegion or a supplied field).
 *   WriteRegion can stage data but cannot create an interest. Clearing interest
 *   retains stored data until Shutdown; overlapping interests form a union.
 * - WorldLocation uses an identity terrain origin, scaled by VoxelSizeCm. Interest
 *   is a sphere intersecting half-open chunk boxes; zero radius selects its cell.
 * - Step-1 reference kernels: sphere includes integer samples at distance <= R;
 *   box is [Centre-Extent, Centre+Extent). Remove sets density to +32767, Add to
 *   -32767 and its requested material, Paint changes material only (including air).
 *   Touched means an actual value/material change; no-op success has empty bounds.
 *   Bounds are the tight half-open envelope of changed samples. Missing data or
 *   any footprint outside WorldBounds rejects the entire op, without mutation.
 *   Work is bounded: 65,536 changed samples (7.1), 262,144 candidate reads. Larger
 *   operations return false; the later service owns splitting, never this backend.
 * - Physical reference occupancy is clamp((1-density)/2, 0, 1). Volumes aggregate
 *   before rounding to integer microlitres; Remove uses old material, Add uses new.
 *   This is test-backend accounting, not E-1 calibration or DEF-6 economic policy.
 * - Dense transfer uses 4.7's LE int16[N] then uint16[N], local index x+32*y+1024*z.
 *   ValueConfig 0 means int16. Rev/LastOpSeq are caller-owned metadata; this backend
 *   neither assigns nor bumps them. SparseDiff and Empty restoration need a base
 *   field, so step 1 rejects them; nonresident ReadRegion returns Empty.
 * - Init while running fails without resetting data. Failure outputs reset. Hash
 *   of a nonresident chunk is zero. All methods are safe before init/after shutdown.
 *
 * These reference choices do not close DEF-5: production kernel compatibility,
 * golden fixtures and cross-platform replay remain build-step-3 work. Flatten and
 * Smooth are unsupported. The reusable tests assert contract properties, rather
 * than requiring another backend to reproduce this backend's density kernel.
 */
class TERRAINCORE_API FMemoryTerrainBackend final : public ITerrainBackend
{
public:
	virtual bool Initialize(const FTerrainBackendInit& InInit) override;
	virtual void Shutdown() override;
	virtual bool ApplyOp(const FTerrainOp& Op, FTerrainEditResult& Out) override;
	virtual bool ReadRegion(const FTerrainChunkKey& Key, FTerrainRegionData& Out) override;
	virtual bool WriteRegion(const FTerrainRegionData& In) override;
	virtual uint64 HashRegion(const FTerrainChunkKey& Key) const override;
	virtual bool IsRegionResident(const FTerrainChunkKey& Key) const override;
	virtual void FlushPendingWork() override;
	virtual bool QueryPoint(const FIntVector& Position, FTerrainPointSample& Out) const override;
	virtual void SetStreamingInterest(const FTerrainStreamingInterest& In) override;
	virtual void ClearStreamingInterest(uint32 InterestId) override;

private:
	bool IsKeyInWorld(const FTerrainChunkKey& Key) const;
	bool HasInterest(const FTerrainChunkKey& Key) const;
	void GenerateInterestedChunks();

	bool bInitialized = false;
	FTerrainBackendInit Init;
	TMap<FTerrainChunkKey, TArray<int16>> Values;
	TMap<FTerrainChunkKey, TArray<FTerrainMatId>> Materials;
	TMap<FTerrainChunkKey, FTerrainRegionData> Metadata;
	TMap<uint32, FTerrainStreamingInterest> Interests;
};

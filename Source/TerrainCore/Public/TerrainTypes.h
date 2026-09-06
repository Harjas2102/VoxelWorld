// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "CoreMinimal.h"

/**
 * TerrainTypes.h — the TerrainCore value types (ARCHITECTURE.md §4.2, plus
 * FTerrainPointSample from §4.3).
 *
 * These are plain C++ value types, not USTRUCTs. No plugin type appears in any of them
 * (§4.2, D-011). This header must never include a plugin header; TerrainCore.Build.cs is
 * the boundary that enforces it (§4.1.0).
 *
 * Field order here is the declaration order of §4.2 and is load-bearing: the wire codec in
 * TerrainOp.h serialises in declaration order.
 */

// ---- identity ----------------------------------------------------------
using FTerrainOpSeq = uint64;   // global, monotonic, server-assigned
using FTerrainRev   = uint32;   // per chunk, monotonic
using FTerrainMatId = uint16;   // GAME material id. Never a plugin index.

/** Chunk space. 32 voxels per unit (K2, ruled by D-024). */
struct FTerrainChunkKey
{
	int32 X = 0;
	int32 Y = 0;
	int32 Z = 0;

	FTerrainChunkKey() = default;
	FTerrainChunkKey(int32 InX, int32 InY, int32 InZ) : X(InX), Y(InY), Z(InZ) {}

	friend bool operator==(const FTerrainChunkKey& A, const FTerrainChunkKey& B)
	{
		return A.X == B.X && A.Y == B.Y && A.Z == B.Z;
	}

	friend bool operator!=(const FTerrainChunkKey& A, const FTerrainChunkKey& B)
	{
		return !(A == B);
	}

	friend uint32 GetTypeHash(const FTerrainChunkKey& Key)
	{
		return HashCombine(HashCombine(::GetTypeHash(Key.X), ::GetTypeHash(Key.Y)), ::GetTypeHash(Key.Z));
	}
};

/**
 * Voxel-space axis-aligned box. Game type — never a plugin box (§4.2, §4.3).
 *
 * Min is INCLUSIVE, Max is EXCLUSIVE. The box is empty iff Max[i] <= Min[i] on any axis.
 */
struct FTerrainBox
{
	FIntVector Min = FIntVector::ZeroValue;   // inclusive
	FIntVector Max = FIntVector::ZeroValue;   // EXCLUSIVE

	FTerrainBox() = default;
	FTerrainBox(const FIntVector& InMin, const FIntVector& InMax) : Min(InMin), Max(InMax) {}

	bool IsEmpty() const
	{
		return Max.X <= Min.X || Max.Y <= Min.Y || Max.Z <= Min.Z;
	}

	/** Inclusive-min / exclusive-max containment. */
	bool Contains(const FIntVector& Voxel) const
	{
		return Voxel.X >= Min.X && Voxel.X < Max.X
			&& Voxel.Y >= Min.Y && Voxel.Y < Max.Y
			&& Voxel.Z >= Min.Z && Voxel.Z < Max.Z;
	}

	friend bool operator==(const FTerrainBox& A, const FTerrainBox& B)
	{
		return A.Min == B.Min && A.Max == B.Max;
	}

	friend bool operator!=(const FTerrainBox& A, const FTerrainBox& B)
	{
		return !(A == B);
	}
};

/**
 * Which half of the service this backend instance serves (§4.3 Initialize, "... role").
 *
 * AMBIGUITY, NAMED (R-011): ARCHITECTURE.md mentions `role` only in the §4.3 Initialize
 * comment (line 325) and never enumerates its values. The two values below are the only
 * ones the document's own language supports — §4.3 "Server: full result. Client: result
 * ignored." and §4.4 "exists on both server and client; HasAuthority gates the
 * authoritative half." No dedicated/listen distinction is implied here: the dedicated-server
 * case is carried by FTerrainStreamingInterest::bRender (§4.2). If the Architect intends a
 * wider set, this enum is the one thing in this increment that changes.
 */
enum class ETerrainRole : uint8
{
	Server,
	Client,
};

// ---- what the backend gives back ---------------------------------------
struct FTerrainMaterialVolume
{
	FTerrainMatId MaterialId = 0;
	int64         MicroLitres = 0;   // signed: + removed, - placed.
	                                 // int64 µL spans ±9.2e12 m³. See DEF-9.
};

struct FTerrainEditResult
{
	FTerrainBox                    EditedBounds;   // voxel space, game type
	TArray<FTerrainChunkKey>       AffectedChunks;
	TArray<FTerrainMaterialVolume> Removed;        // physical material only
	int64                          VoxelsTouched = 0;
	int64                          VoxelsScanned = 0;  // read footprint, not write
	bool                           bTruncated    = false;
};

// ---- region transfer (snapshots and JIP) --------------------------------
enum class ETerrainRegionEncoding : uint8
{
	Dense,
	SparseDiff,
	Empty,
};

struct FTerrainRegionData
{
	FTerrainChunkKey       Key;
	FTerrainRev            Rev = 0;
	FTerrainOpSeq          LastOpSeq = 0;
	ETerrainRegionEncoding Encoding = ETerrainRegionEncoding::Empty;
	uint32                 GeneratorVersion = 0;
	uint8                  ValueConfig = 0;   // mirrors the plugin's value config flag
	TArray<uint8>          Payload;           // see §4.7
};

// ---- streaming interest (see §7.4 / DEF-10) -----------------------------
struct FTerrainStreamingInterest
{
	uint32  InterestId = 0;   // game-owned handle
	FVector WorldLocation = FVector::ZeroVector;
	double  RadiusCm = 0.0;
	bool    bCollision = true;
	bool    bRender = false;  // false on a dedicated server
};

// ---- point query (§4.3) -------------------------------------------------
struct FTerrainPointSample
{
	float         Density    = 0.f;  // < 0 solid, normalised
	FTerrainMatId MaterialId = 0;
	bool          bResident  = false;
};

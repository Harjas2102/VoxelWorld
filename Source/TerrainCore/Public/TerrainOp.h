// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "CoreMinimal.h"
#include "TerrainTypes.h"

/**
 * TerrainOp.h — the terrain operation and its wire codec (ARCHITECTURE.md §4.2).
 *
 * FTerrainOp is both the replicated wire format AND the journal record body. Its encoding is
 * therefore permanent: a change to it is a save-format change and needs a numbered decision,
 * not an edit.
 *
 * "Serialisation is explicit and little-endian, field by field in declaration order, with no
 *  struct padding on the wire and no reliance on sizeof. The encoded body is 58 bytes."
 *  — §4.2. `TerrainCore.Op.Codec.RoundTrip` (§6.1) is the authority on the encoding.
 */

enum class ETerrainOpKind : uint8
{
	Remove,
	Add,
	Flatten,
	Smooth,
	Paint,
};

enum class ETerrainShape : uint8
{
	Sphere,
	Box,
};

enum class ETerrainSource : uint8
{
	Player,
	Machine,
	Admin,
	Worldgen,
};

/**
 * The operation. Voxel space, integer — never a world-space float (§4.3, "Why the wire op is
 * integer voxel space"): the server quantises once at validation time and the quantised
 * integers are the op, for the wire, for the journal, and for its own application.
 */
struct FTerrainOp
{
	FTerrainOpSeq   OpSeq         = 0;
	uint64          TransactionId = 0;  // equal across sub-ops of one split edit
	ETerrainOpKind  Kind          = ETerrainOpKind::Remove;
	ETerrainShape   Shape         = ETerrainShape::Sphere;
	ETerrainSource  Source        = ETerrainSource::Player;
	uint32          SourceId      = 0;  // PlayerId or MachineId
	uint32          ToolId        = 0;  // tool/tier — affects yield, not geometry
	FIntVector      CentreVox     = {}; // VOXEL SPACE. Integer. See §4.3.
	int32           RadiusVoxQ16  = 0;  // voxels, 16.16 fixed point
	FIntVector      ExtentVox     = {}; // box ops only
	FTerrainMatId   MaterialId    = 0;  // Add / Paint only
	uint8           Flags         = 0;
};

/**
 * The encoded body size, in bytes. Permanent (§4.2).
 *
 *   OpSeq 8 + TransactionId 8 + Kind 1 + Shape 1 + Source 1 + SourceId 4 + ToolId 4
 * + CentreVox 12 + RadiusVoxQ16 4 + ExtentVox 12 + MaterialId 2 + Flags 1 = 58
 */
inline constexpr int32 TerrainOpEncodedSize = 58;

/** Highest valid enumerator of each wire enum. An unknown byte fails deserialisation. */
inline constexpr uint8 TerrainOpKindMaxValue   = static_cast<uint8>(ETerrainOpKind::Paint);
inline constexpr uint8 TerrainShapeMaxValue    = static_cast<uint8>(ETerrainShape::Box);
inline constexpr uint8 TerrainSourceMaxValue   = static_cast<uint8>(ETerrainSource::Worldgen);

/**
 * Appends exactly TerrainOpEncodedSize bytes to OutBytes: explicit little-endian, field by
 * field, in declaration order. Never memcpys the struct and never depends on its layout.
 */
TERRAINCORE_API void SerializeTerrainOp(const FTerrainOp& Op, TArray<uint8>& OutBytes);

/**
 * Reads one op from the front of Bytes.
 *
 * Returns false — without reading out of bounds and without touching OutOp meaningfully —
 * when Bytes holds fewer than TerrainOpEncodedSize bytes, or when any enum field carries a
 * value this build does not define. A longer buffer is accepted; only the first 58 bytes are
 * consumed.
 */
TERRAINCORE_API bool DeserializeTerrainOp(TArrayView<const uint8> Bytes, FTerrainOp& OutOp);

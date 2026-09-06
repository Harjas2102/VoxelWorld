// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "CoreMinimal.h"

/**
 * TerrainQuantise.h — world space to voxel space, once, on the server (ARCHITECTURE.md §4.3,
 * "Why the wire op is integer voxel space").
 *
 * The hazard this exists to remove: if the server broadcast a world position, server and
 * client would each perform the float conversion independently, against possibly differing
 * actor transforms, and round differently at voxel boundaries. The divergence would be one
 * voxel wide, invisible for a week, and then a client would see a wall where the server sees
 * air. So the server quantises ONCE, at validation time, and the quantised integers are the
 * op — for the wire, for the journal, and for its own application.
 *
 * THE ROUNDING RULE IS FIXED (§4.3). Transform to terrain-local in double, then floor.
 * Not round. Floor is what makes a voxel a half-open cell [n, n+1) whose index is stable
 * under the FTerrainBox convention (Min inclusive, Max exclusive); round would split every
 * cell across two indices and put the seam in the middle of a voxel instead of at its face.
 * Substituting RoundToInt here is a correctness change, not a style change.
 *
 * This removes one conversion hazard. It does NOT by itself establish deterministic replay
 * across builds, platforms or backends — see DEF-5.
 */

/**
 * Quantises a world-space position to the voxel whose half-open cell contains it.
 *
 * Returns false, leaving OutVoxel zeroed, when VoxelSizeCm is not a positive finite number,
 * when WorldPos or the transform produce a non-finite terrain-local position, or when the
 * result would not fit in int32. Callers on the authoritative path treat false as a rejected
 * edit request; it is never a silently clamped one.
 */
TERRAINCORE_API bool QuantiseEdit(const FVector& WorldPos,
                                  const FTransform& TerrainOrigin,
                                  float VoxelSizeCm,
                                  FIntVector& OutVoxel);

/**
 * The world-space position of a voxel — its CENTRE, not its minimum corner.
 *
 * Centre is required, not cosmetic. QuantiseEdit(DequantiseVoxel(Q)) == Q must hold for every
 * Q, and the corner does not survive that round trip: a transform is not exactly invertible
 * in floating point, so a corner that lands one ULP below its own boundary floors to Q-1.
 * The centre sits half a voxel from either face, which is many orders of magnitude more slack
 * than the transform's error, so the identity holds for every transform the game can produce.
 */
TERRAINCORE_API FVector DequantiseVoxel(const FIntVector& Voxel,
                                        const FTransform& TerrainOrigin,
                                        float VoxelSizeCm);

/**
 * A radius in centimetres as voxels in 16.16 fixed point — the units of
 * FTerrainOp::RadiusVoxQ16 (§4.2).
 *
 * Fixed point, not float, because the radius is on the wire and in the journal: two builds
 * must agree on it bit for bit. Returns 0 for a non-positive or non-finite input and
 * saturates at MAX_int32 rather than wrapping.
 */
TERRAINCORE_API int32 QuantiseRadiusQ16(double RadiusCm, float VoxelSizeCm);

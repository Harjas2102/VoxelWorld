// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainQuantise.h"

namespace
{
	/** 16.16 fixed point: one voxel is 65536. */
	constexpr double TerrainQ16Scale = 65536.0;
}

bool QuantiseEdit(const FVector& WorldPos,
                  const FTransform& TerrainOrigin,
                  float VoxelSizeCm,
                  FIntVector& OutVoxel)
{
	OutVoxel = FIntVector::ZeroValue;

	if (!(VoxelSizeCm > 0.f) || !FMath::IsFinite(VoxelSizeCm))
	{
		return false;
	}

	if (WorldPos.ContainsNaN())
	{
		return false;
	}

	// Terrain-local, in double. FVector and FTransform are double-precision under large world
	// coordinates, so this is a double transform even though VoxelSizeCm is a float.
	const FVector Local = TerrainOrigin.InverseTransformPosition(WorldPos);
	if (Local.ContainsNaN())
	{
		return false;
	}

	const double Size = static_cast<double>(VoxelSizeCm);

	// Floor in double first, then range-check, then narrow. FMath::FloorToInt32 on a value
	// outside int32 is undefined, so the check has to happen before the call, not after.
	const double Floored[3] =
	{
		FMath::Floor(Local.X / Size),
		FMath::Floor(Local.Y / Size),
		FMath::Floor(Local.Z / Size),
	};

	constexpr double MinRepresentable = static_cast<double>(MIN_int32);
	constexpr double MaxRepresentable = static_cast<double>(MAX_int32);

	for (const double Component : Floored)
	{
		if (!FMath::IsFinite(Component) || Component < MinRepresentable || Component > MaxRepresentable)
		{
			return false;
		}
	}

	// The rule of record (§4.3): FloorToInt32 of the terrain-local position over the voxel
	// size. Applied to the same quotient already floored above, so the two cannot disagree.
	OutVoxel = FIntVector(
		FMath::FloorToInt32(Local.X / Size),
		FMath::FloorToInt32(Local.Y / Size),
		FMath::FloorToInt32(Local.Z / Size));

	return true;
}

FVector DequantiseVoxel(const FIntVector& Voxel,
                        const FTransform& TerrainOrigin,
                        float VoxelSizeCm)
{
	const double Size = static_cast<double>(VoxelSizeCm);

	// Voxel centre. See the header: the corner does not survive the round trip.
	const FVector Local(
		(static_cast<double>(Voxel.X) + 0.5) * Size,
		(static_cast<double>(Voxel.Y) + 0.5) * Size,
		(static_cast<double>(Voxel.Z) + 0.5) * Size);

	return TerrainOrigin.TransformPosition(Local);
}

int32 QuantiseRadiusQ16(double RadiusCm, float VoxelSizeCm)
{
	if (!(VoxelSizeCm > 0.f) || !FMath::IsFinite(VoxelSizeCm))
	{
		return 0;
	}

	if (!FMath::IsFinite(RadiusCm) || RadiusCm <= 0.0)
	{
		return 0;
	}

	const double RadiusVox = RadiusCm / static_cast<double>(VoxelSizeCm);
	const double Q16 = FMath::RoundToDouble(RadiusVox * TerrainQ16Scale);

	if (Q16 <= 0.0)
	{
		return 0;
	}

	// Saturate. A wrapped radius is a negative radius, and a negative radius on the wire is a
	// server-side crash waiting for the first player with a big enough tool.
	if (Q16 >= static_cast<double>(MAX_int32))
	{
		return MAX_int32;
	}

	return static_cast<int32>(Q16);
}

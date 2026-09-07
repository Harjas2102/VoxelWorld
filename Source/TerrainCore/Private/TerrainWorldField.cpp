// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainWorldField.h"

namespace
{
	/** Hermite smoothstep on an already-clamped [0, 1] input. */
	FORCEINLINE double SmoothStep01(double T)
	{
		return T * T * (3.0 - 2.0 * T);
	}

	/**
	 * Integer coordinate hash. Deliberately integer-only: the DEF-5 hazard is that a
	 * generator produces different bits on a different build, and every float trick that
	 * makes noise cheap (bit-casting, an RNG's internal double, a fast reciprocal) is a way
	 * to acquire exactly that. Unsigned wraparound is defined by the standard, so this
	 * returns the same value everywhere the ints are the same.
	 */
	FORCEINLINE uint32 HashCoords(int32 X, int32 Y, uint32 Seed)
	{
		uint32 H = Seed;
		H ^= static_cast<uint32>(X) * 0x9E3779B1u;
		H ^= static_cast<uint32>(Y) * 0x85EBCA77u;
		H ^= H >> 15;
		H *= 0x2545F491u;
		H ^= H >> 13;
		H *= 0x27D4EB2Fu;
		H ^= H >> 16;
		return H;
	}

	/** Hash to [0, 1). Uses the top 24 bits, which are the well-mixed ones. */
	FORCEINLINE double HashUnit(int32 X, int32 Y, uint32 Seed)
	{
		return static_cast<double>(HashCoords(X, Y, Seed) >> 8) * (1.0 / 16777216.0);
	}

	/** Bilinear value noise on the integer lattice, returned in [-1, 1]. */
	double ValueNoise2D(double X, double Y, uint32 Seed)
	{
		const double Fx = FMath::FloorToDouble(X);
		const double Fy = FMath::FloorToDouble(Y);
		const int32 X0 = static_cast<int32>(Fx);
		const int32 Y0 = static_cast<int32>(Fy);
		const double Tx = SmoothStep01(FMath::Clamp(X - Fx, 0.0, 1.0));
		const double Ty = SmoothStep01(FMath::Clamp(Y - Fy, 0.0, 1.0));

		const double N00 = HashUnit(X0,     Y0,     Seed);
		const double N10 = HashUnit(X0 + 1, Y0,     Seed);
		const double N01 = HashUnit(X0,     Y0 + 1, Seed);
		const double N11 = HashUnit(X0 + 1, Y0 + 1, Seed);

		const double A = N00 + (N10 - N00) * Tx;
		const double B = N01 + (N11 - N01) * Tx;
		return (A + (B - A) * Ty) * 2.0 - 1.0;
	}

	/**
	 * A radial bump: full height at the centre, zero at Radius and beyond, smooth between.
	 * MONOTONICALLY DECREASING in Distance, which is the property SampleRange relies on to
	 * bound the hill and the basin exactly rather than conservatively.
	 */
	FORCEINLINE double RadialFalloff(double Distance, double Radius)
	{
		if (Radius <= 0.0)
		{
			return 0.0;
		}
		return SmoothStep01(1.0 - FMath::Clamp(Distance / Radius, 0.0, 1.0));
	}

	/** Smallest distance from a point to an axis-aligned XY rectangle. Zero if inside. */
	double MinDistanceToRect(double Px, double Py, double MinX, double MinY, double MaxX, double MaxY)
	{
		const double Dx = FMath::Max3(MinX - Px, 0.0, Px - MaxX);
		const double Dy = FMath::Max3(MinY - Py, 0.0, Py - MaxY);
		return FMath::Sqrt(Dx * Dx + Dy * Dy);
	}

	/** Largest distance from a point to an axis-aligned XY rectangle: the far corner. */
	double MaxDistanceToRect(double Px, double Py, double MinX, double MinY, double MaxX, double MaxY)
	{
		const double Dx = FMath::Max(FMath::Abs(Px - MinX), FMath::Abs(Px - MaxX));
		const double Dy = FMath::Max(FMath::Abs(Py - MinY), FMath::Abs(Py - MaxY));
		return FMath::Sqrt(Dx * Dx + Dy * Dy);
	}
}

FTerrainWorldField::FTerrainWorldField(const FTerrainWorldFieldParams& InParams)
	: Params(InParams)
{
	// One hash of the seed up front so the per-octave seeds are decorrelated even when the
	// configured seed is 0, which is the default and therefore the common case.
	SeedHash = HashCoords(Params.Seed, 0x5BD1E995, 0xA3C59AC3u);

	// Guards, not preferences. SampleRange divides by the first two, and its answer is what
	// the octree trusts when it decides NOT to look at a region.
	Params.SurfaceWidthVox = FMath::Max(Params.SurfaceWidthVox, UE_KINDA_SMALL_NUMBER);
	Params.MaxSlopeFactor = FMath::Max(Params.MaxSlopeFactor, 1.0);
	Params.RoughOctaves = FMath::Clamp(Params.RoughOctaves, 0, 8);
	Params.RoughFeatureVox = FMath::Max(Params.RoughFeatureVox, 1.0);
	Params.RoughAmplitudeVox = FMath::Max(Params.RoughAmplitudeVox, 0.0);
}

double FTerrainWorldField::Roughness(double X, double Y) const
{
	if (Params.RoughOctaves <= 0 || Params.RoughAmplitudeVox <= 0.0)
	{
		return 0.0;
	}

	double Sum = 0.0;
	double Norm = 0.0;
	double Amplitude = 1.0;
	double Frequency = 1.0 / Params.RoughFeatureVox;

	for (int32 Octave = 0; Octave < Params.RoughOctaves; ++Octave)
	{
		Sum += Amplitude * ValueNoise2D(
			X * Frequency, Y * Frequency, SeedHash + static_cast<uint32>(Octave) * 0x9E3779B1u);
		Norm += Amplitude;
		Amplitude *= 0.5;
		Frequency *= 2.0;
	}

	// Sum/Norm is in [-1, 1] by construction, so the result never leaves the amplitude.
	// SampleRange uses exactly that bound; breaking it here would not look like a bug, it
	// would look like terrain with holes in it at distance.
	return (Sum / Norm) * Params.RoughAmplitudeVox;
}

double FTerrainWorldField::SurfaceHeightVox(double X, double Y) const
{
	// --- the hill ---------------------------------------------------------
	const double HillDist = FMath::Sqrt(
		FMath::Square(X - Params.HillCentreXVox) + FMath::Square(Y - Params.HillCentreYVox));
	const double Hill = Params.HillHeightVox * RadialFalloff(HillDist, Params.HillRadiusVox);

	// --- the cliff: west of the plane, cut the hill down to a shelf --------
	// A blend towards min(Hill, Shelf) rather than towards Shelf, so the escarpment exists
	// only where there is hill to cut. Out on the plain the cut is a no-op and the ground
	// stays flat, which is what keeps the player start and the world origin usable.
	const double CutWeight = SmoothStep01(FMath::Clamp(
		(Params.CliffPlaneXVox - X) / FMath::Max(Params.CliffBlendVox, UE_KINDA_SMALL_NUMBER), 0.0, 1.0));
	const double Shelved = FMath::Min(Hill, Params.CliffShelfVox);
	const double Relief = Hill + (Shelved - Hill) * CutWeight;

	// --- the basin --------------------------------------------------------
	const double BasinDist = FMath::Sqrt(
		FMath::Square(X - Params.BasinCentreXVox) + FMath::Square(Y - Params.BasinCentreYVox));
	const double Basin = Params.BasinDepthVox * RadialFalloff(BasinDist, Params.BasinRadiusVox);

	return Params.PlainHeightVox + Relief - Basin + Roughness(X, Y);
}

void FTerrainWorldField::SurfaceHeightRange(const FTerrainBox& Box, double& OutMin, double& OutMax) const
{
	// Widened by one voxel on each axis: Sample takes a central difference at +/-1 to measure
	// the slope, so a height one voxel outside the box can still influence a sample inside it.
	// Max is exclusive, so the last sample is at Max - 1 and Max itself is already the +1.
	const double MinX = static_cast<double>(Box.Min.X) - 1.0;
	const double MinY = static_cast<double>(Box.Min.Y) - 1.0;
	const double MaxX = static_cast<double>(Box.Max.X);
	const double MaxY = static_cast<double>(Box.Max.Y);

	// Hill and basin are monotonically decreasing in distance, so the nearest and farthest
	// points of the rectangle give their true extremes over the whole box.
	const double HillNear = MinDistanceToRect(Params.HillCentreXVox, Params.HillCentreYVox, MinX, MinY, MaxX, MaxY);
	const double HillFar  = MaxDistanceToRect(Params.HillCentreXVox, Params.HillCentreYVox, MinX, MinY, MaxX, MaxY);
	const double HillMax = Params.HillHeightVox * RadialFalloff(HillNear, Params.HillRadiusVox);
	const double HillMin = Params.HillHeightVox * RadialFalloff(HillFar,  Params.HillRadiusVox);

	// The cut moves relief into [min(Hill, Shelf), Hill] pointwise. Where the box lies wholly
	// on one side of the escarpment that collapses to one of the two, which is worth the two
	// comparisons: an unnecessarily wide range here is a region the octree cannot skip.
	const double CliffFullyCutX = Params.CliffPlaneXVox - FMath::Max(Params.CliffBlendVox, 0.0);
	double ReliefMin = FMath::Min(HillMin, Params.CliffShelfVox);
	double ReliefMax = HillMax;
	if (MinX >= Params.CliffPlaneXVox)
	{
		ReliefMin = HillMin;                                      // wholly east: never cut
	}
	else if (MaxX <= CliffFullyCutX)
	{
		ReliefMax = FMath::Min(HillMax, Params.CliffShelfVox);    // wholly west: always cut
	}

	const double BasinNear = MinDistanceToRect(Params.BasinCentreXVox, Params.BasinCentreYVox, MinX, MinY, MaxX, MaxY);
	const double BasinFar  = MaxDistanceToRect(Params.BasinCentreXVox, Params.BasinCentreYVox, MinX, MinY, MaxX, MaxY);
	const double BasinMax = Params.BasinDepthVox * RadialFalloff(BasinNear, Params.BasinRadiusVox);
	const double BasinMin = Params.BasinDepthVox * RadialFalloff(BasinFar,  Params.BasinRadiusVox);

	const double Rough = (Params.RoughOctaves > 0) ? Params.RoughAmplitudeVox : 0.0;

	OutMin = Params.PlainHeightVox + ReliefMin - BasinMax - Rough;
	OutMax = Params.PlainHeightVox + ReliefMax - BasinMin + Rough;
}

FTerrainMatId FTerrainWorldField::MaterialAt(double X, double Y, double Z, double SurfaceZ) const
{
	if (Z > SurfaceZ)
	{
		return ETerrainMaterial::Air;
	}

	if (Z < Params.BedrockTopZVox)
	{
		return ETerrainMaterial::Bedrock;
	}

	const double Depth = SurfaceZ - Z;

	// The ore body wins over the depth strata inside its ellipsoid, but never breaks the
	// surface: an outcrop the player could pick up without digging would make the mining loop
	// optional, and GDD "Mining IS terraforming" is the pillar that forbids that.
	if (Depth >= Params.TopsoilThicknessVox)
	{
		const double Ox = (X - Params.OreCentreXVox) / FMath::Max(Params.OreRadiusXVox, UE_KINDA_SMALL_NUMBER);
		const double Oy = (Y - Params.OreCentreYVox) / FMath::Max(Params.OreRadiusYVox, UE_KINDA_SMALL_NUMBER);
		const double Oz = (Z - Params.OreCentreZVox) / FMath::Max(Params.OreRadiusZVox, UE_KINDA_SMALL_NUMBER);
		if (Ox * Ox + Oy * Oy + Oz * Oz <= 1.0)
		{
			return ETerrainMaterial::IronOre;
		}
	}

	if (Depth < Params.TopsoilThicknessVox)
	{
		return ETerrainMaterial::Topsoil;
	}
	if (Depth < Params.TopsoilThicknessVox + Params.DirtThicknessVox)
	{
		return ETerrainMaterial::Dirt;
	}
	if (Depth < Params.TopsoilThicknessVox + Params.DirtThicknessVox + Params.StoneThicknessVox)
	{
		return ETerrainMaterial::Stone;
	}
	return ETerrainMaterial::DeepStone;
}

FTerrainDensitySample FTerrainWorldField::Sample(FIntVector Position) const
{
	const double X = static_cast<double>(Position.X);
	const double Y = static_cast<double>(Position.Y);
	const double Z = static_cast<double>(Position.Z);

	const double SurfaceZ = SurfaceHeightVox(X, Y);
	const double Delta = Z - SurfaceZ;   // positive is above ground

	FTerrainDensitySample Out;

	// Outside the certainty band the answer saturates whatever the slope is, and the four
	// extra height evaluations the slope correction costs would be spent computing a value
	// that is about to be clamped anyway. Almost every voxel in a 512 m world is here.
	const double Band = Params.SurfaceWidthVox * Params.MaxSlopeFactor;
	if (Delta >= Band)
	{
		Out.Density = 1.f;
		Out.MaterialId = ETerrainMaterial::Air;
		return Out;
	}
	if (Delta <= -Band)
	{
		Out.Density = -1.f;
		Out.MaterialId = MaterialAt(X, Y, Z, SurfaceZ);
		return Out;
	}

	// Vertical distance overstates the distance to a sloped surface, and on the cliff face it
	// overstates it by a factor of twenty. Dividing by sqrt(1 + |grad H|^2) turns it back into
	// an approximate true distance, which is what stops a near-vertical face from meshing as a
	// staircase. Capping the factor keeps SampleRange's band honest; the cost of the cap is a
	// slightly harder edge on the very steepest ground, never a hole.
	const double Gx = (SurfaceHeightVox(X + 1.0, Y) - SurfaceHeightVox(X - 1.0, Y)) * 0.5;
	const double Gy = (SurfaceHeightVox(X, Y + 1.0) - SurfaceHeightVox(X, Y - 1.0)) * 0.5;
	const double Slope = FMath::Clamp(FMath::Sqrt(1.0 + Gx * Gx + Gy * Gy), 1.0, Params.MaxSlopeFactor);

	Out.Density = static_cast<float>(FMath::Clamp(Delta / (Params.SurfaceWidthVox * Slope), -1.0, 1.0));
	Out.MaterialId = MaterialAt(X, Y, Z, SurfaceZ);
	return Out;
}

FTerrainDensityRange FTerrainWorldField::SampleRange(const FTerrainBox& Box) const
{
	FTerrainDensityRange Out;
	if (Box.IsEmpty())
	{
		return Out;
	}

	double HeightMin = 0.0;
	double HeightMax = 0.0;
	SurfaceHeightRange(Box, HeightMin, HeightMax);

	// Max is exclusive, so the last sampled plane is Max.Z - 1.
	const double ZLow  = static_cast<double>(Box.Min.Z);
	const double ZHigh = static_cast<double>(Box.Max.Z) - 1.0;

	const double DeltaLow  = ZLow  - HeightMax;   // most negative Delta anywhere in the box
	const double DeltaHigh = ZHigh - HeightMin;   // most positive Delta anywhere in the box

	// Delta is divided by SurfaceWidth * Slope, and Slope is somewhere in [1, MaxSlopeFactor].
	// Dividing by the SMALL divisor exaggerates, by the LARGE one flattens; pick whichever
	// pushes each end of the range outwards, so the bound holds for every slope in between.
	const double Narrow = Params.SurfaceWidthVox;
	const double Wide   = Params.SurfaceWidthVox * Params.MaxSlopeFactor;

	const double Low  = DeltaLow  >= 0.0 ? DeltaLow  / Wide   : DeltaLow  / Narrow;
	const double High = DeltaHigh >= 0.0 ? DeltaHigh / Narrow : DeltaHigh / Wide;

	Out.Min = static_cast<float>(FMath::Clamp(Low, -1.0, 1.0));
	Out.Max = static_cast<float>(FMath::Clamp(High, -1.0, 1.0));
	if (Out.Min > Out.Max)
	{
		Out.Min = Out.Max;   // degenerate boxes only; never widen a range by accident
	}
	return Out;
}

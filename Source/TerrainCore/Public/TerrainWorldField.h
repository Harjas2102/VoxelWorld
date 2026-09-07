// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "CoreMinimal.h"
#include "ITerrainDensityField.h"
#include "TerrainMaterials.h"

/**
 * The shape numbers for FTerrainWorldField, in VOXELS and voxel space.
 *
 * Voxel space, not centimetres, on purpose: the field is the one part of terrain generation
 * that must not know how big a voxel is. §8.1 gives coordinate policy to the service, which
 * already converts; a field that also converted would be a second opinion about the grid.
 * At the T-101A/CP-012 setting of 50 cm per voxel, 2 voxels = 1 m.
 *
 * Every value here is an input to the world's SHAPE, so changing any of them changes what a
 * saved chunk would regenerate as. That is what UTerrainSettings::GeneratorVersion is for
 * (§4.2, K3): bump it when these change, or a chunk written by the old world is silently
 * reinterpreted by the new one.
 */
struct FTerrainWorldFieldParams
{
	/** Forwarded from UTerrainSettings::Seed. Drives the surface roughness only. */
	int32 Seed = 0;

	// ---- the plain -------------------------------------------------------
	/**
	 * Ground height of the lowland, in voxels.
	 *
	 * NEGATIVE BY EXACTLY RoughAmplitudeVox, on purpose: it makes "the open plain never rises
	 * above world Z = 0" a property of the world rather than a hope. PlayerStart sits at world
	 * Z = 150 cm, and a surface that could bulge above zero would spawn the player inside the
	 * ground somewhere between one boot and the next, depending on the seed. The Field.Shape
	 * test asserts the invariant so a later change to either number cannot quietly break it.
	 */
	double PlainHeightVox = -6.0;

	// ---- the hill (GDD "First test world": a 256-512 m hill) --------------
	/**
	 * Centred east of the origin, not on it. The origin is where eight chunks meet, where
	 * Terrain.SelfTest digs, and where PlayerStart looks from -82 m; burying all three
	 * inside the hill would make the cheapest smoke test in the project ambiguous.
	 */
	double HillCentreXVox = 140.0;
	double HillCentreYVox = 0.0;
	/**
	 * 280 voxels = 140 m, so a 280 m hill: inside the GDD's 256-512 m band with margin on the
	 * low side, and its west edge still 24 voxels clear of the player start at X = -164.
	 */
	double HillRadiusVox = 280.0;
	/** 130 voxels = 65 m of relief. */
	double HillHeightVox = 130.0;

	// ---- the exposed cliff (GDD: "an exposed cliff") ----------------------
	/**
	 * A straight escarpment at X = CliffPlaneXVox. West of it the hill is cut down to a
	 * shelf; east of it it stands full height. The face therefore points WEST, at the
	 * player start, which is the whole reason the cliff exists: strata you can see without
	 * digging for them.
	 */
	double CliffPlaneXVox = 20.0;
	double CliffShelfVox = 24.0;
	/** Blend width. Small is near-vertical; below ~1 voxel the mesher cannot resolve it. */
	double CliffBlendVox = 2.0;

	// ---- the lowland basin (GDD: "water or lowland") ---------------------
	double BasinCentreXVox = -260.0;
	double BasinCentreYVox = -260.0;
	double BasinRadiusVox = 150.0;
	double BasinDepthVox = 24.0;

	// ---- surface roughness ------------------------------------------------
	/** Peak height of the fBm term, in voxels. Bounds SampleRange, so it must be a true max. */
	double RoughAmplitudeVox = 6.0;
	/** Wavelength of the coarsest octave, in voxels. */
	double RoughFeatureVox = 48.0;
	int32 RoughOctaves = 3;

	// ---- strata, measured DOWN from the local surface ---------------------
	double TopsoilThicknessVox = 4.0;    //  2 m
	double DirtThicknessVox    = 20.0;   // 10 m
	double StoneThicknessVox   = 90.0;   // 45 m, then DeepStone
	/** Absolute Z below which everything is Bedrock, whatever the depth says. */
	double BedrockTopZVox = -420.0;

	// ---- the ore body (GDD: "an underground ore body") --------------------
	/**
	 * An ellipsoid under the hill. Deep enough to need a real tunnel, shallow enough that
	 * the cliff face is a plausible way in.
	 */
	double OreCentreXVox = 140.0;
	double OreCentreYVox = 0.0;
	double OreCentreZVox = -30.0;
	double OreRadiusXVox = 60.0;
	double OreRadiusYVox = 40.0;
	double OreRadiusZVox = 25.0;

	// ---- density shaping --------------------------------------------------
	/**
	 * Voxels of vertical distance that map to full density. 1.0 reproduces the plugin's own
	 * VoxelFlatGenerator, which returned raw Z and let FVoxelValue clamp it (T-101A).
	 */
	double SurfaceWidthVox = 1.0;
	/**
	 * Cap on the slope correction below. It bounds SampleRange's certainty band and it is
	 * why a box far from the surface can be declared solid or empty without sampling it.
	 */
	double MaxSlopeFactor = 24.0;
};

/**
 * FTerrainWorldField — the game's world shape (T-108, ARCHITECTURE.md §4.6, build step 8).
 *
 * §4.6: "The world's shape - strata, ore bodies, the authored hill - is plain C++ that
 * unit-tests without an engine and survives a backend swap untouched." This is that class.
 * It includes no plugin header, holds no UObject, touches no actor and needs no UWorld; the
 * adapter's UVPLegacyDensityGenerator is a thin forwarder onto it, and FMemoryTerrainBackend
 * can sample the identical field headless.
 *
 * WHY THIS EXISTS AT ALL. Voxel Graphs are Pro-gated and fail SILENTLY (T-101A finding 2b,
 * R-008): all 106 shipped graph assets produce an empty world on Free. The only runnable
 * stock generators are VoxelFlatGenerator and VoxelEmptyGenerator, which is why the world
 * has been a flat plane since T-101A and why the T-101A hill had to be SCULPTED by script -
 * and a sculpted hill is not reproducible from a seed and does not survive a map load
 * (finding 2e, R-003). Everything this class returns is a pure function of position and
 * seed, so the hill comes back on every load, in every process, with no save file.
 *
 * DETERMINISM, STATED HONESTLY. The noise is integer-hashed, so it carries no dependence on
 * float bit layout or on any RNG. The interpolation and shaping are ordinary double
 * arithmetic, which is IEEE-reproducible for a fixed order of operations but is NOT proof
 * against a compiler contracting a multiply-add or a different -ffast-math setting. DEF-5
 * is the defect that owns that question ("integer inputs remove one hazard but do not prove
 * identical generator output across builds, platforms and backends"); this class does not
 * close it, and the Field.Determinism test below only pins same-build reproducibility.
 *
 * THREADING. Immutable after construction and free of memoisation, so concurrent const calls
 * from the plugin's mesher threads are safe by construction rather than by lock. See the
 * threading note on ITerrainDensityField.
 */
class TERRAINCORE_API FTerrainWorldField final : public ITerrainDensityField
{
public:
	explicit FTerrainWorldField(const FTerrainWorldFieldParams& InParams);

	//~ Begin ITerrainDensityField Interface
	virtual FTerrainDensitySample Sample(FIntVector Position) const override;
	virtual FTerrainDensityRange SampleRange(const FTerrainBox& Box) const override;
	//~ End ITerrainDensityField Interface

	const FTerrainWorldFieldParams& GetParams() const { return Params; }

	/** Ground height in voxels at a horizontal position. The whole world's shape is here. */
	double SurfaceHeightVox(double X, double Y) const;

	/** The material at a position, given the surface height there. Air above the surface. */
	FTerrainMatId MaterialAt(double X, double Y, double Z, double SurfaceZ) const;

private:
	/** Conservative [min, max] of SurfaceHeightVox over a half-open voxel box's XY footprint. */
	void SurfaceHeightRange(const FTerrainBox& Box, double& OutMin, double& OutMax) const;

	/** fBm value noise in [-1, 1]. Integer-hashed; no RNG, no float bit tricks. */
	double Roughness(double X, double Y) const;

	FTerrainWorldFieldParams Params;
	uint32 SeedHash = 0;
};

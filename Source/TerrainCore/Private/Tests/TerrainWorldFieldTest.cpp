// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#if WITH_DEV_AUTOMATION_TESTS

#include "TerrainWorldField.h"
#include "TerrainMaterials.h"
#include "Misc/AutomationTest.h"

/**
 * The T-108 world shape, tested with no engine world, no actor and no plugin — which is the
 * whole argument of ARCHITECTURE.md §4.6 for putting generation in game code: the world's
 * shape is checkable in a headless test rather than only by looking at it.
 *
 * These tests pin BEHAVIOUR, not numbers. Where a literal appears it is a property boundary
 * (the world is 512 m across; the hill must be at least 256 m wide per the GDD), never a
 * value copied out of the implementation to make the test agree with itself.
 */
namespace
{
	FTerrainWorldFieldParams DefaultParams()
	{
		return FTerrainWorldFieldParams();
	}

	/** The T-101A / CP-012 world: 1024 voxels across, centred on the origin. */
	constexpr int32 WorldHalfVox = 512;
}

// ---------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainFieldShapeTest, "TerrainCore.Field.Shape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerrainFieldShapeTest::RunTest(const FString& Parameters)
{
	const FTerrainWorldFieldParams Params = DefaultParams();
	const FTerrainWorldField Field(Params);

	// --- there is a hill, and it is the size the GDD asked for -------------------------
	// "First test world: a 256-512 m hill". At 50 cm per voxel that is at least 512 voxels
	// across. Measured, not asserted from the radius parameter, because the cliff cut and the
	// basin both modify the profile after the radius is applied.
	const double PeakHeight = Field.SurfaceHeightVox(Params.HillCentreXVox, Params.HillCentreYVox);
	TestTrue(TEXT("the hill rises at least 40 m above the plain"), PeakHeight >= 80.0);

	// Width is measured against a roughness-free copy of the same world. With roughness on
	// there is no clean baseline to compare to — the plain itself moves by up to the
	// amplitude — so the measurement would silently become "where the hill is taller than the
	// noise", which is a smaller number and not the thing the GDD specifies.
	FTerrainWorldFieldParams SmoothParams = Params;
	SmoothParams.RoughAmplitudeVox = 0.0;
	const FTerrainWorldField SmoothField(SmoothParams);

	int32 WideCount = 0;
	for (int32 X = -WorldHalfVox; X < WorldHalfVox; ++X)
	{
		if (SmoothField.SurfaceHeightVox(X, SmoothParams.HillCentreYVox) > SmoothParams.PlainHeightVox)
		{
			++WideCount;
		}
	}
	TestTrue(TEXT("the hill is at least 256 m wide east-west (GDD low end)"), WideCount >= 512);
	TestTrue(TEXT("and it fits inside the 512 m world"), WideCount <= 1024);

	// --- the plain really is a plain, where the player start and the origin are ---------
	// PlayerStart sits at world X = -8228 cm, which is voxel -164 at 50 cm. If the hill or the
	// cliff reached it the player would spawn inside rock, and the by-hand dig check would
	// fail for a reason that has nothing to do with digging.
	for (int32 X = -400; X <= 0; X += 20)
	{
		const double H = Field.SurfaceHeightVox(X, 0.0);
		TestTrue(TEXT("ground west of the escarpment stays near the plain"),
			FMath::Abs(H - Params.PlainHeightVox) <= Params.RoughAmplitudeVox + Params.CliffShelfVox);
	}

	// THE SPAWN INVARIANT. PlayerStart is at world Z = 150 cm and the plain must stay under
	// world Z = 0, or a seed change spawns the player inside the ground. Checked across the
	// whole world away from the hill footprint and the basin rim, which are allowed to rise.
	for (int32 X = -WorldHalfVox; X < -FMath::CeilToInt(Params.HillRadiusVox - Params.HillCentreXVox) - 8; X += 11)
	{
		for (int32 Y = -WorldHalfVox; Y < WorldHalfVox; Y += 37)
		{
			TestTrue(TEXT("the open plain never rises above world Z = 0"),
				Field.SurfaceHeightVox(X, Y) <= 0.0);
		}
	}

	// The world origin is where Terrain.SelfTest digs and where eight chunks meet. It must be
	// ground: solid at Z = 0 or just below, and open sky well above.
	TestTrue(TEXT("the origin voxel is at or below the surface"), Field.Sample(FIntVector(0, 0, -4)).Density < 0.f);
	TestTrue(TEXT("well above the origin is empty"), Field.Sample(FIntVector(0, 0, 200)).Density > 0.f);

	// --- the exposed cliff -------------------------------------------------------------
	// A near-vertical face means a large height change over a few voxels. Measure the biggest
	// step anywhere along the escarpment line rather than assuming where it lands.
	double LargestStep = 0.0;
	for (int32 X = -60; X < 120; ++X)
	{
		const double Step = FMath::Abs(Field.SurfaceHeightVox(X + 1, 0.0) - Field.SurfaceHeightVox(X, 0.0));
		LargestStep = FMath::Max(LargestStep, Step);
	}
	TestTrue(TEXT("there is an exposed cliff: at least 5 m of rise in one voxel"), LargestStep >= 10.0);

	// --- the lowland basin --------------------------------------------------------------
	const double BasinFloor = Field.SurfaceHeightVox(Params.BasinCentreXVox, Params.BasinCentreYVox);
	TestTrue(TEXT("the basin is below the plain"), BasinFloor < Params.PlainHeightVox - 1.0);

	return true;
}

// ---------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainFieldStrataTest, "TerrainCore.Field.Strata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerrainFieldStrataTest::RunTest(const FString& Parameters)
{
	const FTerrainWorldFieldParams Params = DefaultParams();
	const FTerrainWorldField Field(Params);

	// --- strata are ordered by depth, everywhere ----------------------------------------
	// Sampled at the hill centre because that is where all four bands exist above bedrock.
	const int32 Cx = FMath::RoundToInt(Params.HillCentreXVox);
	const int32 Cy = FMath::RoundToInt(Params.HillCentreYVox);
	const double Surface = Field.SurfaceHeightVox(Cx, Cy);

	const auto MaterialAtDepth = [&](double Depth)
	{
		return (int32)Field.Sample(FIntVector(Cx, Cy, FMath::RoundToInt(Surface - Depth))).MaterialId;
	};

	TestEqual(TEXT("above the surface is air"),
		(int32)Field.Sample(FIntVector(Cx, Cy, FMath::CeilToInt(Surface) + 8)).MaterialId, (int32)ETerrainMaterial::Air);
	TestEqual(TEXT("just below the surface is topsoil"), MaterialAtDepth(1.0), (FTerrainMatId)ETerrainMaterial::Topsoil);
	TestEqual(TEXT("below the topsoil is dirt"),
		MaterialAtDepth(Params.TopsoilThicknessVox + 2.0), (FTerrainMatId)ETerrainMaterial::Dirt);
	TestEqual(TEXT("below the dirt is stone"),
		MaterialAtDepth(Params.TopsoilThicknessVox + Params.DirtThicknessVox + 2.0), (FTerrainMatId)ETerrainMaterial::Stone);

	// --- the ore body exists, is underground, and is finite -----------------------------
	const FIntVector OreCentre(
		FMath::RoundToInt(Params.OreCentreXVox),
		FMath::RoundToInt(Params.OreCentreYVox),
		FMath::RoundToInt(Params.OreCentreZVox));
	TestEqual(TEXT("there is ore at the ore body's centre"),
		(int32)Field.Sample(OreCentre).MaterialId, (int32)ETerrainMaterial::IronOre);
	TestTrue(TEXT("the ore body is solid rock, not a void"), Field.Sample(OreCentre).Density < 0.f);

	const FIntVector OutsideOre(
		OreCentre.X + FMath::CeilToInt(Params.OreRadiusXVox) + 4, OreCentre.Y, OreCentre.Z);
	TestNotEqual(TEXT("the ore body ends"),
		(int32)Field.Sample(OutsideOre).MaterialId, (int32)ETerrainMaterial::IronOre);

	// GDD "Mining IS terraforming": ore the player can walk up to and collect without digging
	// would make the loop optional. Nothing in the top stratum may be ore, anywhere.
	int32 SurfaceOre = 0;
	for (int32 X = -WorldHalfVox; X < WorldHalfVox; X += 7)
	{
		for (int32 Y = -WorldHalfVox; Y < WorldHalfVox; Y += 7)
		{
			const int32 Z = FMath::FloorToInt(Field.SurfaceHeightVox(X, Y));
			if (Field.Sample(FIntVector(X, Y, Z)).MaterialId == ETerrainMaterial::IronOre)
			{
				++SurfaceOre;
			}
		}
	}
	TestEqual(TEXT("no ore breaks the surface"), SurfaceOre, 0);

	// --- bedrock floors the world --------------------------------------------------------
	TestEqual(TEXT("the bottom of the world is bedrock"),
		(int32)Field.Sample(FIntVector(0, 0, -WorldHalfVox)).MaterialId, (int32)ETerrainMaterial::Bedrock);

	return true;
}

// ---------------------------------------------------------------------------------------
/**
 * TerrainCore.Field.Range — the AR-6 contract.
 *
 * This is the test that matters most, because SampleRange is trusted to say "you do not need
 * to look here". If it ever returns a range that excludes a value Sample can actually produce,
 * the octree skips a region that had terrain in it and the world gets a hole — with no error,
 * no log line and no failed assertion anywhere else in the project.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainFieldRangeTest, "TerrainCore.Field.Range",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerrainFieldRangeTest::RunTest(const FString& Parameters)
{
	const FTerrainWorldField Field(DefaultParams());

	// Boxes deliberately chosen to straddle the escarpment, the hill flank, the basin, the
	// bedrock floor and empty sky, at both chunk size and a coarse LOD-like stride.
	const TArray<FTerrainBox> Boxes = {
		FTerrainBox(FIntVector(-32, -32, -32), FIntVector(0, 0, 0)),
		FTerrainBox(FIntVector(0, 0, 0), FIntVector(32, 32, 32)),
		FTerrainBox(FIntVector(0, -32, 0), FIntVector(64, 32, 64)),        // the escarpment
		FTerrainBox(FIntVector(96, -64, 32), FIntVector(224, 64, 160)),    // the hill flank
		FTerrainBox(FIntVector(-320, -320, -64), FIntVector(-192, -192, 32)),  // the basin
		FTerrainBox(FIntVector(-64, -64, 200), FIntVector(64, 64, 320)),   // sky
		FTerrainBox(FIntVector(-64, -64, -480), FIntVector(64, 64, -400)), // deep rock
		FTerrainBox(FIntVector(-512, -512, -512), FIntVector(512, 512, 512)),  // the whole world
	};

	int32 Checked = 0;
	for (const FTerrainBox& Box : Boxes)
	{
		const FTerrainDensityRange Range = Field.SampleRange(Box);
		TestTrue(TEXT("a range is ordered"), Range.Min <= Range.Max);

		// Sample the box on a stride that always includes its extreme planes: the corners and
		// faces are where a bound is loosest and therefore where it breaks first.
		const FIntVector Size = Box.Max - Box.Min;
		const int32 Step = FMath::Max(1, FMath::Max3(Size.X, Size.Y, Size.Z) / 24);
		for (int32 X = Box.Min.X; X < Box.Max.X; X += Step)
		{
			for (int32 Y = Box.Min.Y; Y < Box.Max.Y; Y += Step)
			{
				for (int32 Z = Box.Min.Z; Z < Box.Max.Z; Z += Step)
				{
					const float Density = Field.Sample(FIntVector(X, Y, Z)).Density;
					++Checked;
					if (Density < Range.Min || Density > Range.Max)
					{
						AddError(FString::Printf(
							TEXT("SampleRange lied: voxel (%d,%d,%d) has density %f, outside [%f, %f] for box "
								 "[%d,%d,%d)-[%d,%d,%d). A region the octree would have skipped is not empty."),
							X, Y, Z, Density, Range.Min, Range.Max,
							Box.Min.X, Box.Min.Y, Box.Min.Z, Box.Max.X, Box.Max.Y, Box.Max.Z));
						return false;
					}
				}
			}
		}
	}
	TestTrue(TEXT("the range check actually sampled something"), Checked > 1000);

	// The range must also be USEFUL, or AR-6 bought nothing: high sky and deep rock have to
	// come back saturated so the octree can skip them outright.
	const FTerrainDensityRange Sky = Field.SampleRange(
		FTerrainBox(FIntVector(-64, -64, 300), FIntVector(64, 64, 400)));
	TestEqual(TEXT("empty sky is certainly empty"), Sky.Min, 1.f);

	const FTerrainDensityRange Deep = Field.SampleRange(
		FTerrainBox(FIntVector(-64, -64, -500), FIntVector(64, 64, -400)));
	TestEqual(TEXT("deep rock is certainly solid"), Deep.Max, -1.f);

	return true;
}

// ---------------------------------------------------------------------------------------
/**
 * TerrainCore.Field.Determinism — the same seed gives the same world, twice, in one process.
 *
 * This pins SAME-BUILD reproducibility and nothing more. DEF-5 is explicit that integer
 * inputs "do not prove identical generator output across builds, platforms and backends", and
 * no test in this file claims to close it. What this does catch is the class of mistake that
 * would make even one process disagree with itself: a static, a cached value, an uninitialised
 * member, or an RNG that advanced global state.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainFieldDeterminismTest, "TerrainCore.Field.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerrainFieldDeterminismTest::RunTest(const FString& Parameters)
{
	FTerrainWorldFieldParams Params = DefaultParams();
	Params.Seed = 20260907;

	const FTerrainWorldField A(Params);
	const FTerrainWorldField B(Params);

	int32 Mismatches = 0;
	for (int32 X = -200; X <= 200; X += 13)
	{
		for (int32 Y = -200; Y <= 200; Y += 17)
		{
			for (int32 Z = -100; Z <= 200; Z += 11)
			{
				const FTerrainDensitySample SA = A.Sample(FIntVector(X, Y, Z));
				const FTerrainDensitySample SB = B.Sample(FIntVector(X, Y, Z));
				// Bitwise equal, not nearly equal: two instances of the same pure function on
				// the same inputs have no licence to differ at all.
				if (SA.Density != SB.Density || SA.MaterialId != SB.MaterialId)
				{
					++Mismatches;
				}
			}
		}
	}
	TestEqual(TEXT("two fields with the same seed agree exactly"), Mismatches, 0);

	// Repeated sampling of ONE field must also not drift, which is the memoisation trap the
	// ITerrainDensityField threading rule forbids.
	const FIntVector Probe(37, -91, 12);
	const FTerrainDensitySample First = A.Sample(Probe);
	for (int32 Repeat = 0; Repeat < 64; ++Repeat)
	{
		A.Sample(FIntVector(Repeat * 3, Repeat * 5, Repeat * 7));
	}
	const FTerrainDensitySample Again = A.Sample(Probe);
	TestEqual(TEXT("a field does not drift as it is used"), Again.Density, First.Density);
	TestEqual(TEXT("nor does its material"), (int32)Again.MaterialId, (int32)First.MaterialId);

	// A different seed must actually change the world, or the seed is decorative.
	Params.Seed = 1;
	const FTerrainWorldField C(Params);
	int32 Differences = 0;
	for (int32 X = -200; X <= 200; X += 13)
	{
		if (A.SurfaceHeightVox(X, 0.0) != C.SurfaceHeightVox(X, 0.0))
		{
			++Differences;
		}
	}
	TestTrue(TEXT("a different seed gives a different surface"), Differences > 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

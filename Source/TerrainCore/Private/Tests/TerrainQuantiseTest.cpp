// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "Misc/AutomationTest.h"
#include "TerrainQuantise.h"

#include <cmath>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

/**
 * TerrainCore.Op.Quantisation.Stable -- ARCHITECTURE.md 6.1.
 *
 * "The same world-space request quantises to the same integers across 10,000 randomised
 *  transforms."
 *
 * What this is actually defending, from 4.3: server and client must never each perform the
 * world-to-voxel conversion independently, because they would round differently at voxel
 * boundaries and the divergence would be one voxel wide and invisible until a client saw a
 * wall where the server saw air. The server quantises once. This test is the evidence that
 * "once" produces the same answer every time it is asked, that the inverse is exact, and
 * that the boundary itself is a clean edge rather than a coin flip.
 *
 * It does NOT establish deterministic replay across builds, platforms or backends. That is
 * DEF-5 and remains open.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerrainQuantisationStableTest,
	"TerrainCore.Op.Quantisation.Stable",
	EAutomationTestFlags::EditorContext
	| EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext
	| EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

namespace
{
	/** Moves Value by UlpCount representable doubles, in the direction of the sign. */
	double NudgeUlps(double Value, int32 UlpCount)
	{
		const double Direction = (UlpCount >= 0)
			? std::numeric_limits<double>::infinity()
			: -std::numeric_limits<double>::infinity();

		double Result = Value;
		for (int32 Step = 0; Step < FMath::Abs(UlpCount); ++Step)
		{
			Result = std::nextafter(Result, Direction);
		}
		return Result;
	}
}

bool FTerrainQuantisationStableTest::RunTest(const FString& Parameters)
{
	// --------------------------------------------------------------------------------------
	// 1. Repeatability and idempotence over 10,000 randomised (WorldPos, TerrainOrigin) pairs
	// --------------------------------------------------------------------------------------
	{
		// Fixed seed. A quantiser test that is itself nondeterministic proves nothing, and a
		// failure has to be reproducible from the log alone.
		FRandomStream Random(0x7E44A1u);

		constexpr int32 SampleCount = 10000;

		int32 RejectedCount    = 0;
		int32 UnstableCount    = 0;
		int32 NonIdempotent    = 0;
		int32 NegativeVoxels   = 0;   // coverage check: negative coordinates were exercised
		int32 ReportedFailures = 0;

		for (int32 Sample = 0; Sample < SampleCount; ++Sample)
		{
			// Deliberately spans negative coordinates on every axis, for both the position
			// and the terrain origin. The T-101A PlayerStart is at x = -8228; the negative
			// half of the world is the ordinary case here, not an edge case.
			const FVector WorldPos(
				Random.FRandRange(-50000.0, 50000.0),
				Random.FRandRange(-50000.0, 50000.0),
				Random.FRandRange(-50000.0, 50000.0));

			const FVector Translation(
				Random.FRandRange(-20000.0, 20000.0),
				Random.FRandRange(-20000.0, 20000.0),
				Random.FRandRange(-20000.0, 20000.0));

			const FRotator Rotation(
				Random.FRandRange(-180.0, 180.0),
				Random.FRandRange(-180.0, 180.0),
				Random.FRandRange(-180.0, 180.0));

			// Uniform scale only. A non-uniform terrain scale would mean non-cubic voxels,
			// which the design does not have.
			const double UniformScale = Random.FRandRange(0.25, 4.0);

			const FTransform TerrainOrigin(Rotation, Translation, FVector(UniformScale));

			const float VoxelSizeCm = static_cast<float>(Random.FRandRange(10.0, 200.0));

			FIntVector First(0, 0, 0);
			if (!QuantiseEdit(WorldPos, TerrainOrigin, VoxelSizeCm, First))
			{
				++RejectedCount;
				continue;
			}

			if (First.X < 0 || First.Y < 0 || First.Z < 0)
			{
				++NegativeVoxels;
			}

			// Repeatability: the same request, asked again, is the same answer.
			for (int32 Repeat = 0; Repeat < 3; ++Repeat)
			{
				FIntVector Again(0, 0, 0);
				const bool bOk = QuantiseEdit(WorldPos, TerrainOrigin, VoxelSizeCm, Again);
				if (!bOk || Again != First)
				{
					++UnstableCount;
					if (ReportedFailures++ < 8)
					{
						AddError(FString::Printf(
							TEXT("Sample %d: repeat %d returned (%d,%d,%d), first call returned (%d,%d,%d)"),
							Sample, Repeat, Again.X, Again.Y, Again.Z, First.X, First.Y, First.Z));
					}
					break;
				}
			}

			// Idempotence: the voxel's own world position quantises back to that voxel.
			const FVector RoundTripped = DequantiseVoxel(First, TerrainOrigin, VoxelSizeCm);

			FIntVector BackAgain(0, 0, 0);
			const bool bBackOk = QuantiseEdit(RoundTripped, TerrainOrigin, VoxelSizeCm, BackAgain);
			if (!bBackOk || BackAgain != First)
			{
				++NonIdempotent;
				if (ReportedFailures++ < 8)
				{
					AddError(FString::Printf(
						TEXT("Sample %d: Quantise(Dequantise((%d,%d,%d))) = (%d,%d,%d), ok=%d, voxel size %f"),
						Sample, First.X, First.Y, First.Z,
						BackAgain.X, BackAgain.Y, BackAgain.Z, bBackOk ? 1 : 0, VoxelSizeCm));
				}
			}
		}

		TestEqual(TEXT("No randomised sample was rejected"), RejectedCount, 0);
		TestEqual(TEXT("Every randomised sample quantised identically on repeat"), UnstableCount, 0);
		TestEqual(TEXT("Quantise(Dequantise(Q)) == Q for every Q produced"), NonIdempotent, 0);
		TestTrue(TEXT("The randomised set actually covered negative voxel coordinates"),
			NegativeVoxels > SampleCount / 10);
	}

	// --------------------------------------------------------------------------------------
	// 2. Boundary stability -- positions nudged by a few ULP do not straddle
	//
	// What "a few ULP" can mean here has a floating-point floor to it, and the test says so
	// rather than pretending otherwise. The quantiser transforms the world position into
	// terrain-local space before flooring, and that transform rounds: a world-space
	// perturbation smaller than one ULP of the terrain-LOCAL coordinate is not representable
	// after the subtraction and collapses back onto the boundary exactly. That is a property
	// of double arithmetic, not a defect in the quantiser -- no rounding rule can separate two
	// world positions that map to the same local double. So the claim is tested in the two
	// forms in which it is well posed:
	//
	//   (a) under an origin where world and terrain-local coordinates are identical, a raw
	//       1-ULP nudge is exact and the seam must sit precisely on the voxel face; and
	//   (b) under a translated origin, with the nudge sized in ULPs of the coarser of the two
	//       magnitudes, so the perturbation provably survives the transform.
	//
	// Then the same claim well away from any exactness argument: a tenth of a voxel inside a
	// cell, where a handful of ULP must not move the answer at all.
	// --------------------------------------------------------------------------------------
	{
		const float VoxelSizeCm = 50.f;
		const double VoxelSize = static_cast<double>(VoxelSizeCm);

		// Unit scale, no rotation. With a rotation in play no world position lands exactly on
		// a boundary and the at-the-seam question is not well posed at all.
		const FTransform Origins[2] =
		{
			FTransform::Identity,
			FTransform(FQuat::Identity, FVector(1024.0, -2048.0, 512.0), FVector(1.0)),
		};

		const int32 TestIndices[] = { -2000, -513, -64, -1, 0, 1, 64, 513, 2000 };

		int32 StraddleCount = 0;
		int32 Reported = 0;

		for (int32 OriginIndex = 0; OriginIndex < 2; ++OriginIndex)
		{
			const FTransform& TerrainOrigin = Origins[OriginIndex];

			for (const int32 Index : TestIndices)
			{
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					// The exact world position of this voxel's minimum face on this axis.
					const double LocalBoundary = static_cast<double>(Index) * VoxelSize;
					FVector Local(0.0, 0.0, 0.0);
					Local[Axis] = LocalBoundary;
					const FVector Boundary = TerrainOrigin.TransformPosition(Local);

					// One ULP of the coarser of the two magnitudes bounds the transform's own
					// rounding error, so a nudge of two or more of them cannot be swallowed.
					// VoxelSize is the floor of the reference so that the origin voxel, whose
					// coordinates are both zero, does not produce a denormal nudge that
					// underflows to zero on the divide.
					const double Reference = FMath::Max3(
						FMath::Abs(Boundary[Axis]), FMath::Abs(LocalBoundary), VoxelSize);
					const double OneUlp =
						std::nextafter(Reference, std::numeric_limits<double>::infinity()) - Reference;

					for (int32 Ulps = 2; Ulps <= 8; ++Ulps)
					{
						const double Delta = static_cast<double>(Ulps) * OneUlp;

						FVector Above = Boundary;
						Above[Axis] = Boundary[Axis] + Delta;

						FVector Below = Boundary;
						Below[Axis] = Boundary[Axis] - Delta;

						FIntVector OnIt(0, 0, 0), Up(0, 0, 0), Down(0, 0, 0);
						const bool bOk =
							  QuantiseEdit(Boundary, TerrainOrigin, VoxelSizeCm, OnIt)
							& QuantiseEdit(Above,    TerrainOrigin, VoxelSizeCm, Up)
							& QuantiseEdit(Below,    TerrainOrigin, VoxelSizeCm, Down);

						// Min is inclusive: the boundary itself belongs to the voxel above it,
						// and so does everything just above. Everything just below belongs to
						// the previous voxel, and to exactly one previous voxel -- that is what
						// "does not straddle" means. Floor is what delivers it; RoundToInt would
						// put the seam in the middle of the cell instead.
						const bool bStable =
							   bOk
							&& OnIt[Axis] == Index
							&& Up[Axis]   == Index
							&& Down[Axis] == Index - 1;

						if (!bStable)
						{
							++StraddleCount;
							if (Reported++ < 8)
							{
								AddError(FString::Printf(
									TEXT("Origin %d, boundary voxel %d, axis %d, %d ULP: on=%d up=%d down=%d (ok=%d)"),
									OriginIndex, Index, Axis, Ulps,
									OnIt[Axis], Up[Axis], Down[Axis], bOk ? 1 : 0));
							}
						}
					}

					// The identity origin is exactly invertible, so there the strongest form of
					// the claim holds: a single raw ULP either side of the face is enough.
					//
					// Except at the origin voxel itself, and for a reason worth recording. One
					// ULP below 0.0 is the smallest denormal, -4.9e-324. The fixed rounding rule
					// (§4.3) divides by the voxel size before flooring, and that divide
					// underflows to -0.0, whose floor is 0 rather than -1. No divide-then-floor
					// rule can do otherwise: the two positions are not separable after the
					// division. It is a property of doubles near zero, not a defect, and it has
					// no reachable consequence -- 5e-324 cm is not a position anything in the
					// game can produce. Voxel 0 is still covered by the scaled-nudge loop above,
					// whose reference magnitude is floored at the voxel size precisely so that
					// its nudge stays normal.
					if (OriginIndex == 0 && Index != 0)
					{
						for (int32 Ulps = 1; Ulps <= 4; ++Ulps)
						{
							FVector Above = Boundary;
							Above[Axis] = NudgeUlps(Boundary[Axis], Ulps);

							FVector Below = Boundary;
							Below[Axis] = NudgeUlps(Boundary[Axis], -Ulps);

							FIntVector Up(0, 0, 0), Down(0, 0, 0);
							const bool bOk =
								  QuantiseEdit(Above, TerrainOrigin, VoxelSizeCm, Up)
								& QuantiseEdit(Below, TerrainOrigin, VoxelSizeCm, Down);

							if (!bOk || Up[Axis] != Index || Down[Axis] != Index - 1)
							{
								++StraddleCount;
								if (Reported++ < 8)
								{
									AddError(FString::Printf(
										TEXT("Identity origin, boundary voxel %d, axis %d, %d raw ULP: up=%d down=%d (ok=%d)"),
										Index, Axis, Ulps, Up[Axis], Down[Axis], bOk ? 1 : 0));
								}
							}
						}
					}

					// The same claim one tenth of a voxel inside each neighbouring cell, where
					// no exact-arithmetic argument is needed at all: a handful of ULP must not
					// move the answer anywhere near a boundary.
					for (const double Offset : { -0.1 * VoxelSize, 0.1 * VoxelSize })
					{
						FVector Local2(0.0, 0.0, 0.0);
						Local2[Axis] = LocalBoundary + Offset;
						const FVector Inside = TerrainOrigin.TransformPosition(Local2);

						FIntVector Expected(0, 0, 0);
						if (!QuantiseEdit(Inside, TerrainOrigin, VoxelSizeCm, Expected))
						{
							++StraddleCount;
							continue;
						}

						for (int32 Ulps = -4; Ulps <= 4; ++Ulps)
						{
							FVector Nudged = Inside;
							Nudged[Axis] = NudgeUlps(Inside[Axis], Ulps);

							FIntVector Got(0, 0, 0);
							if (!QuantiseEdit(Nudged, TerrainOrigin, VoxelSizeCm, Got) || Got != Expected)
							{
								++StraddleCount;
								if (Reported++ < 8)
								{
									AddError(FString::Printf(
										TEXT("Origin %d, near-boundary voxel %d, axis %d, offset %f, %d ULP moved to %d (expected %d)"),
										OriginIndex, Index, Axis, Offset, Ulps, Got[Axis], Expected[Axis]));
								}
							}
						}
					}
				}
			}
		}

		TestEqual(TEXT("No ULP-scale nudge straddled a voxel boundary"), StraddleCount, 0);
	}

	// --------------------------------------------------------------------------------------
	// 3. The rounding rule is Floor, not Round -- asserted, so a future edit cannot swap it
	//    quietly. Floor is what makes a voxel the half-open cell [n, n+1).
	// --------------------------------------------------------------------------------------
	{
		const FTransform Identity = FTransform::Identity;
		const float VoxelSizeCm = 50.f;

		FIntVector Voxel(0, 0, 0);

		// 0.9 of a voxel is still voxel 0 under Floor; it would be voxel 1 under Round.
		TestTrue(TEXT("45 cm quantises"), QuantiseEdit(FVector(45.0, 45.0, 45.0), Identity, VoxelSizeCm, Voxel));
		TestEqual(TEXT("45 cm is voxel 0, not voxel 1 (Floor, not Round)"), Voxel.X, 0);

		// -1 cm is voxel -1 under Floor; it would be voxel 0 under Round or truncation.
		TestTrue(TEXT("-1 cm quantises"), QuantiseEdit(FVector(-1.0, -1.0, -1.0), Identity, VoxelSizeCm, Voxel));
		TestEqual(TEXT("-1 cm is voxel -1, not voxel 0 (Floor, not truncation)"), Voxel.X, -1);

		TestTrue(TEXT("-50 cm quantises"), QuantiseEdit(FVector(-50.0, -50.0, -50.0), Identity, VoxelSizeCm, Voxel));
		TestEqual(TEXT("-50 cm is voxel -1 (Min inclusive)"), Voxel.X, -1);

		TestTrue(TEXT("-51 cm quantises"), QuantiseEdit(FVector(-51.0, -51.0, -51.0), Identity, VoxelSizeCm, Voxel));
		TestEqual(TEXT("-51 cm is voxel -2"), Voxel.X, -2);
	}

	// --------------------------------------------------------------------------------------
	// 4. Rejection cases. False must mean rejected, never silently clamped -- a clamped edit
	//    is an edit at the wrong place, which is worse than no edit at all.
	// --------------------------------------------------------------------------------------
	{
		const FTransform Identity = FTransform::Identity;
		FIntVector Voxel(1, 2, 3);

		TestFalse(TEXT("zero voxel size is rejected"),
			QuantiseEdit(FVector::ZeroVector, Identity, 0.f, Voxel));
		TestFalse(TEXT("negative voxel size is rejected"),
			QuantiseEdit(FVector::ZeroVector, Identity, -50.f, Voxel));
		TestFalse(TEXT("a position beyond int32 voxel range is rejected"),
			QuantiseEdit(FVector(1.0e30, 0.0, 0.0), Identity, 50.f, Voxel));
	}

	// --------------------------------------------------------------------------------------
	// 5. QuantiseRadiusQ16 -- 16.16 fixed point, saturating, never wrapping
	// --------------------------------------------------------------------------------------
	{
		// The T-101A dig radius: 200 uu with 50 cm voxels is 4 voxels (7.1).
		TestEqual(TEXT("200 cm at 50 cm voxels is 4 voxels in Q16"),
			QuantiseRadiusQ16(200.0, 50.f), 4 * 65536);

		TestEqual(TEXT("25 cm at 50 cm voxels is half a voxel in Q16"),
			QuantiseRadiusQ16(25.0, 50.f), 32768);

		TestEqual(TEXT("zero radius is zero"), QuantiseRadiusQ16(0.0, 50.f), 0);
		TestEqual(TEXT("negative radius is zero"), QuantiseRadiusQ16(-10.0, 50.f), 0);
		TestEqual(TEXT("zero voxel size gives zero"), QuantiseRadiusQ16(200.0, 0.f), 0);

		// Saturation, not wrap-around. A wrapped radius is a negative radius on the wire.
		TestEqual(TEXT("an absurd radius saturates at MAX_int32"),
			QuantiseRadiusQ16(1.0e12, 1.f), MAX_int32);
		TestTrue(TEXT("a saturated radius is still positive"),
			QuantiseRadiusQ16(1.0e12, 1.f) > 0);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

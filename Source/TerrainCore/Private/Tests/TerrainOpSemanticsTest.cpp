// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#if WITH_DEV_AUTOMATION_TESTS

#include "MemoryTerrainBackend.h"
#include "TerrainChunk.h"
#include "TerrainMaterials.h"
#include "Misc/AutomationTest.h"

/**
 * The DEF-5 evidence (ARCHITECTURE.md §4.10). Two tests:
 *
 *  - `TerrainCore.Op.Semantics.Contract` asserts the canonical geometry and per-operation
 *    meaning of §4.10.2 and §4.10.3 — the write set, the read bounds, monotonicity,
 *    idempotence, the material rules, the refusal of the reserved kinds, and §4.11.6's rule
 *    that a failed ApplyOp changes nothing.
 *
 *  - `TerrainCore.Op.Semantics.Golden` is the committed fixture set of §4.10.7: a fixed op
 *    script from a fixed starting state, compared against hashes recorded in this file.
 *
 * Both run against FMemoryTerrainBackend, which §4.10.5 names as the reference. Neither
 * compares anything to the plugin adapter: §4.10.4(c) rules cross-backend value identity out
 * of scope, so a test that demanded it would be asserting something the architecture says is
 * false.
 */
namespace
{
	constexpr int32 SemChunkSize = 32;
	constexpr int32 SemSampleCount = SemChunkSize * SemChunkSize * SemChunkSize;
	constexpr uint32 SemGeneratorVersion = 3;

	int32 RadiusQ16(double Voxels)
	{
		return static_cast<int32>(Voxels * 65536.0);
	}

	void Store16(TArray<uint8>& Payload, int32 Offset, uint16 Value)
	{
		Payload[Offset] = static_cast<uint8>(Value);
		Payload[Offset + 1] = static_cast<uint8>(Value >> 8);
	}

	/**
	 * A starting region whose contents VARY WITH POSITION. A uniform fixture would pass a
	 * position-insensitive hash (§4.10.5) and a kernel that wrote to the wrong index, so the
	 * fixture has to be as position-dependent as the property being tested.
	 */
	FTerrainRegionData MakeVariedRegion(const FTerrainChunkKey& Key)
	{
		FTerrainRegionData Data;
		Data.Key = Key;
		Data.GeneratorVersion = SemGeneratorVersion;
		Data.Encoding = ETerrainRegionEncoding::Dense;
		Data.Payload.SetNumUninitialized(SemSampleCount * 4);

		const int32 KeySalt = Key.X * 7 + Key.Y * 13 + Key.Z * 29;
		for (int32 Index = 0; Index < SemSampleCount; ++Index)
		{
			// Solid everywhere, but not identically solid: the low bits move with the index.
			const int16 Density = static_cast<int16>(-32767 + ((Index + KeySalt) & 0x3F));
			const FTerrainMatId Material = static_cast<FTerrainMatId>(
				ETerrainMaterial::Stone + ((Index / 97 + KeySalt) % 3));
			Store16(Data.Payload, Index * 2, static_cast<uint16>(Density));
			Store16(Data.Payload, SemSampleCount * 2 + Index * 2, Material);
		}
		return Data;
	}

	/** The eight chunks meeting at the world origin — the ones every op below touches. */
	TArray<FTerrainChunkKey> OriginChunks()
	{
		TArray<FTerrainChunkKey> Keys;
		for (int32 Z = -1; Z <= 0; ++Z)
		for (int32 Y = -1; Y <= 0; ++Y)
		for (int32 X = -1; X <= 0; ++X)
		{
			Keys.Emplace(X, Y, Z);
		}
		return Keys;
	}

	TUniquePtr<FMemoryTerrainBackend> StartBackend(FAutomationTestBase& Test)
	{
		FTerrainBackendInit Init;
		Init.Seed = 4242;
		Init.GeneratorVersion = SemGeneratorVersion;
		Init.VoxelSizeCm = 50.f;
		Init.WorldBoundsVox = FTerrainBox(FIntVector(-64), FIntVector(64));
		Init.Role = ETerrainRole::Server;

		TUniquePtr<FMemoryTerrainBackend> Backend = MakeUnique<FMemoryTerrainBackend>();
		if (!Test.TestTrue(TEXT("the reference backend initialises"), Backend->Initialize(Init)))
		{
			return nullptr;
		}

		FTerrainStreamingInterest Interest;
		Interest.InterestId = 1;
		Interest.RadiusCm = 6000.0;   // covers the whole fixture world
		Backend->SetStreamingInterest(Interest);

		for (const FTerrainChunkKey& Key : OriginChunks())
		{
			if (!Test.TestTrue(TEXT("the fixture region is accepted"), Backend->WriteRegion(MakeVariedRegion(Key))))
			{
				return nullptr;
			}
		}
		return Backend;
	}

	/** One number standing for the whole eight-chunk state. Order-fixed, so it is comparable. */
	uint64 StateHash(const ITerrainBackend& Backend)
	{
		uint64 Combined = 0;
		for (const FTerrainChunkKey& Key : OriginChunks())
		{
			Combined = Combined * 0x100000001b3ULL ^ Backend.HashRegion(Key);
		}
		return Combined;
	}

	FTerrainOp MakeSphere(ETerrainOpKind Kind, const FIntVector& Centre, double RadiusVox, FTerrainMatId Material = 0)
	{
		FTerrainOp Op;
		Op.Kind = Kind;
		Op.Shape = ETerrainShape::Sphere;
		Op.Source = ETerrainSource::Player;
		Op.CentreVox = Centre;
		Op.RadiusVoxQ16 = RadiusQ16(RadiusVox);
		Op.MaterialId = Material;
		return Op;
	}
}

// ---------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainOpSemanticsContractTest, "TerrainCore.Op.Semantics.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerrainOpSemanticsContractTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FMemoryTerrainBackend> Backend = StartBackend(*this);
	if (!Backend)
	{
		return false;
	}

	const auto Density = [&](const FIntVector& Position)
	{
		FTerrainPointSample Sample;
		Backend->QueryPoint(Position, Sample);
		return Sample.Density;
	};
	const auto Material = [&](const FIntVector& Position)
	{
		FTerrainPointSample Sample;
		Backend->QueryPoint(Position, Sample);
		return Sample.MaterialId;
	};

	// --- the write set boundary is inclusive, and exact -------------------------------
	// §4.10.2: W = { v : |v - C|^2 <= r^2 }, compared in double with <=, no epsilon. At
	// r = 3 exactly, the voxel at distance 3 is IN and the voxel at distance 4 is OUT. This
	// is the assertion that would fail first if anyone "helpfully" added a tolerance.
	{
		const FIntVector Centre(0, 0, 0);
		const FTerrainMatId BeforeIn = Material(FIntVector(3, 0, 0));
		const float OutsideBefore = Density(FIntVector(4, 0, 0));

		FTerrainEditResult Result;
		TestTrue(TEXT("a Remove at r=3 applies"), Backend->ApplyOp(MakeSphere(ETerrainOpKind::Remove, Centre, 3.0), Result));

		TestTrue(TEXT("the voxel at exactly r is inside the write set"), Density(FIntVector(3, 0, 0)) > 0.f);
		TestEqual(TEXT("a voxel one past r is untouched"), Density(FIntVector(4, 0, 0)), OutsideBefore);

		// §4.10.3: Remove preserves material. Digging rock does not repaint it.
		TestEqual(TEXT("Remove preserves the material it removed"), (int32)Material(FIntVector(3, 0, 0)), (int32)BeforeIn);

		// §4.10.2: read bounds are C +/- floor(r), half-open, so 7 per axis at r = 3.
		TestEqual(TEXT("VoxelsScanned is the read-bounds volume"), Result.VoxelsScanned, (int64)(7 * 7 * 7));
		TestTrue(TEXT("the write set is smaller than the read bounds"), Result.VoxelsTouched < Result.VoxelsScanned);
	}

	// --- idempotence ------------------------------------------------------------------
	// The property that makes DEF-3's duplicate application during JIP survivable rather
	// than corrupting, and the second independent reason Smooth is not in the operation set.
	{
		const uint64 Before = StateHash(*Backend);
		FTerrainEditResult Repeat;
		TestTrue(TEXT("repeating the op still succeeds"),
			Backend->ApplyOp(MakeSphere(ETerrainOpKind::Remove, FIntVector(0, 0, 0), 3.0), Repeat));
		TestEqual(TEXT("but it changes nothing"), Repeat.VoxelsTouched, (int64)0);
		TestEqual(TEXT("and the state is bit-identical"), StateHash(*Backend), Before);
	}

	// --- monotonicity, across a chunk boundary ----------------------------------------
	// A Remove may never make a voxel more solid. Validation depends on this: a clearance
	// check that passed before a Remove cannot be invalidated by that Remove.
	//
	// On its OWN backend, deliberately. The ops above already emptied the chunk-0 side of this
	// sphere, and AffectedChunks reports chunks that actually CHANGED — so on the shared
	// fixture this reports seven, correctly, and the assertion would be measuring test order
	// rather than the contract.
	{
		TUniquePtr<FMemoryTerrainBackend> Fresh = StartBackend(*this);
		if (!Fresh)
		{
			return false;
		}
		const auto FreshDensity = [&](const FIntVector& Position)
		{
			FTerrainPointSample Sample;
			Fresh->QueryPoint(Position, Sample);
			return Sample.Density;
		};

		TArray<FIntVector> Probes;
		for (int32 Axis = -4; Axis <= 3; ++Axis)
		{
			Probes.Emplace(Axis, -1, -1);
			Probes.Emplace(-1, Axis, -1);
			Probes.Emplace(-1, -1, Axis);
		}
		TArray<float> Before;
		for (const FIntVector& P : Probes)
		{
			Before.Add(FreshDensity(P));
		}

		FTerrainEditResult Result;
		TestTrue(TEXT("a Remove straddling all eight chunks applies"),
			Fresh->ApplyOp(MakeSphere(ETerrainOpKind::Remove, FIntVector(-1, -1, -1), 4.0), Result));
		TestEqual(TEXT("it reports all eight chunks it changed"), Result.AffectedChunks.Num(), 8);

		int32 Regressions = 0;
		for (int32 Index = 0; Index < Probes.Num(); ++Index)
		{
			if (FreshDensity(Probes[Index]) < Before[Index])
			{
				++Regressions;
			}
		}
		TestEqual(TEXT("Remove never makes a voxel more solid"), Regressions, 0);
	}

	// --- Add sets material where it added; Paint moves material and not density --------
	{
		const FIntVector Centre(-8, 5, -6);
		FTerrainEditResult Result;
		TestTrue(TEXT("Add applies"),
			Backend->ApplyOp(MakeSphere(ETerrainOpKind::Add, Centre, 2.0, ETerrainMaterial::IronOre), Result));
		TestTrue(TEXT("Add left solid material"), Density(Centre) < 0.f);
		TestEqual(TEXT("Add stamped its own material"), (int32)Material(Centre), (int32)ETerrainMaterial::IronOre);

		const float DensityBeforePaint = Density(Centre);
		TestTrue(TEXT("Paint applies"),
			Backend->ApplyOp(MakeSphere(ETerrainOpKind::Paint, Centre, 2.0, ETerrainMaterial::Topsoil), Result));
		TestEqual(TEXT("Paint changed the material"), (int32)Material(Centre), (int32)ETerrainMaterial::Topsoil);
		TestEqual(TEXT("Paint left the density EXACTLY alone"), Density(Centre), DensityBeforePaint);
	}

	// --- the reserved kinds are refused, not approximated ------------------------------
	// §4.10.1. An unimplemented op that is refused is a closed question; an unspecified op
	// that is approximated is an open one, and DEF-5 existed because of the second.
	{
		const uint64 Before = StateHash(*Backend);
		FTerrainEditResult Result;
		TestFalse(TEXT("Flatten is refused"),
			Backend->ApplyOp(MakeSphere(ETerrainOpKind::Flatten, FIntVector(0, 0, 0), 3.0), Result));
		TestFalse(TEXT("Smooth is refused"),
			Backend->ApplyOp(MakeSphere(ETerrainOpKind::Smooth, FIntVector(0, 0, 0), 3.0), Result));
		TestEqual(TEXT("and neither touched the world"), StateHash(*Backend), Before);
	}

	// --- §4.11.6: a false return means NOTHING CHANGED ---------------------------------
	// Not "something may have changed". Each of these fails a different precondition, and
	// every one of them must leave the region hashes bit-identical.
	{
		const uint64 Before = StateHash(*Backend);
		FTerrainEditResult Result;

		TestFalse(TEXT("a zero radius is refused"),
			Backend->ApplyOp(MakeSphere(ETerrainOpKind::Remove, FIntVector(0, 0, 0), 0.0), Result));
		TestFalse(TEXT("an op leaving the world bounds is refused"),
			Backend->ApplyOp(MakeSphere(ETerrainOpKind::Remove, FIntVector(60, 0, 0), 8.0), Result));
		TestFalse(TEXT("an op over the work cap is refused"),
			Backend->ApplyOp(MakeSphere(ETerrainOpKind::Remove, FIntVector(0, 0, 0), 40.0), Result));
		TestFalse(TEXT("an op reaching a non-resident chunk is refused"),
			Backend->ApplyOp(MakeSphere(ETerrainOpKind::Remove, FIntVector(34, 34, 34), 3.0), Result));

		TestEqual(TEXT("no refused op mutated anything"), StateHash(*Backend), Before);
	}

	return true;
}

// ---------------------------------------------------------------------------------------
/**
 * TerrainCore.Op.Semantics.Golden — the committed fixtures of §4.10.7.
 *
 * **A failure here is NEVER fixed by updating the number.** It means one of two things, and
 * both are findings rather than maintenance:
 *
 *   - the kernel changed, which requires a `backendVersion` bump (§4.10.6), or
 *   - the toolchain moved under it, which is §4.10.4(b) — the residual float risk this
 *     project has stated it carries — reporting for duty.
 *
 * The values below were recorded once, at authoring time, from the run that first produced
 * them. That is the only edit to them this file is ever supposed to see.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainOpSemanticsGoldenTest, "TerrainCore.Op.Semantics.Golden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerrainOpSemanticsGoldenTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FMemoryTerrainBackend> Backend = StartBackend(*this);
	if (!Backend)
	{
		return false;
	}

	struct FGoldenStep
	{
		const TCHAR* What;
		FTerrainOp Op;
		bool bExpectApplied;
		uint64 ExpectedStateHash;
	};

	const FGoldenStep Script[] =
	{
		{ TEXT("the fixture, before any op"),
		  FTerrainOp(), true, 0x9A3E01A1AACA67D9ULL },

		{ TEXT("Remove at the origin, r = 3.5"),
		  MakeSphere(ETerrainOpKind::Remove, FIntVector(0, 0, 0), 3.5), true, 0x7B37356B53FDA1DFULL },

		{ TEXT("the same Remove again — must change nothing"),
		  MakeSphere(ETerrainOpKind::Remove, FIntVector(0, 0, 0), 3.5), true, 0x7B37356B53FDA1DFULL },

		{ TEXT("Remove straddling all eight chunks at (-1,-1,-1), r = 5.25"),
		  MakeSphere(ETerrainOpKind::Remove, FIntVector(-1, -1, -1), 5.25), true, 0xD97F46DEB2F83B06ULL },

		{ TEXT("Add iron ore at a negative coordinate, r = 2.75"),
		  MakeSphere(ETerrainOpKind::Add, FIntVector(-9, -4, -7), 2.75, ETerrainMaterial::IronOre), true, 0x9CB8D58821542C0DULL },

		{ TEXT("Paint topsoil over it, r = 2.0"),
		  MakeSphere(ETerrainOpKind::Paint, FIntVector(-9, -4, -7), 2.0, ETerrainMaterial::Topsoil), true, 0x6FACF11F6DB51A1DULL },

		{ TEXT("a refused op — over the work cap"),
		  MakeSphere(ETerrainOpKind::Remove, FIntVector(0, 0, 0), 40.0), false, 0x6FACF11F6DB51A1DULL },
	};

	int32 StepIndex = 0;
	for (const FGoldenStep& Step : Script)
	{
		if (StepIndex > 0)
		{
			FTerrainEditResult Result;
			const bool bApplied = Backend->ApplyOp(Step.Op, Result);
			TestEqual(FString::Printf(TEXT("step %d applied as expected: %s"), StepIndex, Step.What),
				bApplied, Step.bExpectApplied);
		}

		const uint64 Actual = StateHash(*Backend);
		if (Actual != Step.ExpectedStateHash)
		{
			AddError(FString::Printf(
				TEXT("Golden fixture step %d (%s): state hash 0x%016llX, expected 0x%016llX. ")
				TEXT("Do NOT update the expected value to make this pass. Either the kernel changed ")
				TEXT("— which needs a backendVersion bump per ARCHITECTURE 4.10.6 — or the toolchain ")
				TEXT("moved, which is the 4.10.4(b) residual risk. Both are findings."),
				StepIndex, Step.What, Actual, Step.ExpectedStateHash));
		}
		++StepIndex;
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

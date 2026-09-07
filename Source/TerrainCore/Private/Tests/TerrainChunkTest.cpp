// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#if WITH_DEV_AUTOMATION_TESTS

#include "TerrainChunk.h"
#include "MemoryTerrainBackend.h"
#include "TerrainBackendRegistry.h"
#include "Misc/AutomationTest.h"

/**
 * TerrainCore.Chunk.Keys — the voxel-to-chunk mapping, especially below zero.
 *
 * WHY THIS TEST EXISTS. C++ integer division truncates towards zero, so the obvious
 * `Voxel / 32` mirrors around the origin: voxels -31 and +31 would both land in chunk 0, and
 * chunk -1 would hold 33 voxels while chunk 0 held 63. On a world centred on the origin —
 * which ours is — that is wrong for half the map, and it is wrong in a way that only shows up
 * as chunks whose revisions never move, or as an edit near the origin bumping the wrong
 * neighbour. Every assertion below is about the negative side for that reason.
 *
 * It also pins the mapping against FMemoryTerrainBackend's private copy of the same constants
 * (T-112.2), so the two definitions cannot drift apart silently.
 *
 * No engine world, no plugin, per §6.1.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainChunkKeysTest, "TerrainCore.Chunk.Keys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerrainChunkKeysTest::RunTest(const FString& Parameters)
{
	// --- floor division, not truncation ------------------------------------------------
	TestEqual(TEXT("voxel 0 is in chunk 0"), TerrainChunkCoordinate(0), 0);
	TestEqual(TEXT("voxel 31 is in chunk 0"), TerrainChunkCoordinate(31), 0);
	TestEqual(TEXT("voxel 32 is in chunk 1"), TerrainChunkCoordinate(32), 1);
	TestEqual(TEXT("voxel -1 is in chunk -1"), TerrainChunkCoordinate(-1), -1);
	TestEqual(TEXT("voxel -32 is in chunk -1"), TerrainChunkCoordinate(-32), -1);
	TestEqual(TEXT("voxel -33 is in chunk -2"), TerrainChunkCoordinate(-33), -2);

	// Every chunk holds exactly 32 voxels on each axis, on both sides of the origin. This is
	// the property truncation breaks, so assert it as a count rather than as spot checks.
	for (int32 Chunk = -3; Chunk <= 3; ++Chunk)
	{
		int32 Count = 0;
		for (int32 Voxel = Chunk * TerrainChunkSizeVox - 4; Voxel < Chunk * TerrainChunkSizeVox + TerrainChunkSizeVox + 4; ++Voxel)
		{
			if (TerrainChunkCoordinate(Voxel) == Chunk)
			{
				++Count;
			}
		}
		TestEqual(FString::Printf(TEXT("chunk %d holds exactly 32 voxels"), Chunk), Count, TerrainChunkSizeVox);
	}

	// --- bounds round-trip ---------------------------------------------------------------
	// Min inclusive, Max exclusive: the max corner belongs to the NEXT chunk, and a test that
	// accepted either would not notice an off-by-one that puts every edit on a chunk seam.
	for (int32 Z = -2; Z <= 1; ++Z)
	for (int32 Y = -2; Y <= 1; ++Y)
	for (int32 X = -2; X <= 1; ++X)
	{
		const FTerrainChunkKey Key(X, Y, Z);
		const FTerrainBox Bounds = TerrainChunkBounds(Key);
		TestEqual(TEXT("min corner maps back to its own chunk"), TerrainChunkKeyForVoxel(Bounds.Min), Key);
		TestEqual(TEXT("last voxel maps back to its own chunk"),
			TerrainChunkKeyForVoxel(Bounds.Max - FIntVector(1)), Key);
		TestNotEqual(TEXT("max corner belongs to the next chunk"), TerrainChunkKeyForVoxel(Bounds.Max), Key);
	}

	// --- box enumeration ------------------------------------------------------------------
	{
		TArray<FTerrainChunkKey> Keys;
		TestTrue(TEXT("an empty box enumerates nothing and succeeds"),
			TerrainChunkKeysForBox(FTerrainBox(FIntVector(4), FIntVector(4)), Keys));
		TestEqual(TEXT("no keys from an empty box"), Keys.Num(), 0);
	}
	{
		// Wholly inside one chunk.
		TArray<FTerrainChunkKey> Keys;
		TestTrue(TEXT("single-chunk box"), TerrainChunkKeysForBox(FTerrainBox(FIntVector(1), FIntVector(31)), Keys));
		TestEqual(TEXT("one key"), Keys.Num(), 1);
		TestEqual(TEXT("that key is chunk 0"), Keys[0], FTerrainChunkKey(0, 0, 0));
	}
	{
		// Straddling the origin on every axis: the eight chunks around (0,0,0). An edit at the
		// world centre is the single most likely thing anyone tries first.
		TArray<FTerrainChunkKey> Keys;
		TestTrue(TEXT("origin-straddling box"),
			TerrainChunkKeysForBox(FTerrainBox(FIntVector(-1), FIntVector(1)), Keys));
		TestEqual(TEXT("eight chunks meet at the origin"), Keys.Num(), 8);
		for (int32 Z = -1; Z <= 0; ++Z)
		for (int32 Y = -1; Y <= 0; ++Y)
		for (int32 X = -1; X <= 0; ++X)
		{
			TestTrue(FString::Printf(TEXT("contains chunk (%d,%d,%d)"), X, Y, Z),
				Keys.Contains(FTerrainChunkKey(X, Y, Z)));
		}

		// Distinct, not merely the right count — a duplicate key would double-bump a revision
		// and AR-4's "exactly once per affected chunk" would quietly stop holding.
		TSet<FTerrainChunkKey> Distinct(Keys);
		TestEqual(TEXT("keys are distinct"), Distinct.Num(), Keys.Num());
	}
	{
		// The cap refuses without appending. A caller must not be made to allocate without
		// bound by a malformed op, and a half-filled array would be worse than none.
		TArray<FTerrainChunkKey> Keys;
		Keys.Emplace(9, 9, 9);
		TestFalse(TEXT("an oversized box is refused"),
			TerrainChunkKeysForBox(FTerrainBox(FIntVector(-4096), FIntVector(4096)), Keys, 64));
		TestEqual(TEXT("refusal appends nothing"), Keys.Num(), 1);
	}

	return true;
}

/**
 * TerrainCore.Backend.Registry — the indirection that keeps TerrainCore off the adapter.
 *
 * This asserts the mechanism §10 relies on, not any particular backend: register a creator
 * under a name, get a fresh instance back, and get nothing back for a name nobody registered.
 * FMemoryTerrainBackend stands in as the registered backend precisely because it is the one
 * backend that needs no engine world — the test would otherwise have to load the adapter, and
 * a test that loads the adapter to prove TerrainCore does not need the adapter proves nothing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainBackendRegistryTest, "TerrainCore.Backend.Registry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerrainBackendRegistryTest::RunTest(const FString& Parameters)
{
	FTerrainBackendRegistry& Registry = FTerrainBackendRegistry::Get();

	// A name no module would ever ship, so a real registration cannot collide with this test
	// and this test cannot disturb a real one.
	const FName RegistryName = TEXT("TerrainCoreTestOnlyMemoryBackend");
	const bool bWasRegistered = Registry.IsRegistered(RegistryName);
	TestFalse(TEXT("the test name starts unregistered"), bWasRegistered);

	TestNull(TEXT("an unregistered name creates nothing"), Registry.Create(RegistryName).Get());

	int32 CreateCount = 0;
	Registry.Register(RegistryName, [&CreateCount]() -> TUniquePtr<ITerrainBackend>
	{
		++CreateCount;
		return MakeUnique<FMemoryTerrainBackend>();
	});
	TestTrue(TEXT("registered"), Registry.IsRegistered(RegistryName));
	TestTrue(TEXT("the name is listed"), Registry.GetRegisteredNames().Contains(RegistryName));

	// Two calls must give two DIFFERENT instances: a registry that handed out one shared
	// backend would give two worlds the same terrain and nothing would fail until PIE.
	TUniquePtr<ITerrainBackend> First = Registry.Create(RegistryName);
	TUniquePtr<ITerrainBackend> Second = Registry.Create(RegistryName);
	TestNotNull(TEXT("first instance"), First.Get());
	TestNotNull(TEXT("second instance"), Second.Get());
	TestTrue(TEXT("instances are distinct"), First.Get() != Second.Get());
	TestEqual(TEXT("the creator ran once per Create"), CreateCount, 2);

	// The instance really is uninitialised, as FTerrainBackendCreator promises: an already
	// running backend would refuse this call.
	FTerrainBackendInit Init;
	Init.VoxelSizeCm = 50.f;
	Init.WorldBoundsVox = FTerrainBox(FIntVector(-64), FIntVector(64));
	TestTrue(TEXT("a fresh instance initialises"), First->Initialize(Init));
	First->Shutdown();

	Registry.Unregister(RegistryName);
	TestFalse(TEXT("unregistered"), Registry.IsRegistered(RegistryName));
	TestNull(TEXT("nothing is created after unregistering"), Registry.Create(RegistryName).Get());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

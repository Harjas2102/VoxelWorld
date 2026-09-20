// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "Misc/AutomationTest.h"
#include "TerrainPersistenceIndex.h"
#include "TerrainPersistenceRecords.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The chunk-key index tests -- Docs/proposals/P-004 section 13.
 *
 * P-003 section 4 replaced the flat complete manifest with a path-copied radix index, and the
 * whole argument for that change is a bound: a checkpoint that changes D keys writes at most
 * 12*D pages, however much cold history the world holds. This file measures that number rather
 * than believing it, and proves that the untouched subtrees are genuinely shared and still
 * resolve through the new root.
 */

namespace TerrainIndexTest
{
	FTerrainPersistIdentity MakeIdentity()
	{
		FTerrainPersistIdentity Identity;
		for (int32 Index = 0; Index < TerrainPersistIdBytes; ++Index)
		{
			Identity.World.Bytes[Index] = static_cast<uint8>(0x21 + Index);
			Identity.Epoch.Bytes[Index] = static_cast<uint8>(0xB1 + Index);
		}
		for (int32 Index = 0; Index < TerrainPersistDigestSize; ++Index)
		{
			Identity.BaseDigest.Bytes[Index] = static_cast<uint8>(0x51 + Index);
		}
		return Identity;
	}

	FTerrainIndexLeafValue MakeLeafValue(ETerrainRegionEncoding Encoding, uint32 Rev, uint64 LastOpSeq, uint8 Seed)
	{
		FTerrainIndexLeafValue Value;
		Value.Encoding  = Encoding;
		Value.Rev       = Rev;
		Value.LastOpSeq = LastOpSeq;
		if (Encoding != ETerrainRegionEncoding::Empty)
		{
			Value.PayloadLength = 131200;
			for (int32 Index = 0; Index < TerrainPersistDigestSize; ++Index)
			{
				Value.PayloadDigest.Bytes[Index] = static_cast<uint8>(Seed + Index);
			}
		}
		return Value;
	}
}

// ==== Keys ==============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerrainPersistenceIndexKeysTest,
	"TerrainCore.Persistence.Index.Keys",
	EAutomationTestFlags::EditorContext
	| EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext
	| EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

bool FTerrainPersistenceIndexKeysTest::RunTest(const FString& Parameters)
{
	// The whole point of the sign flip and the big-endian write is that unsigned byte-wise
	// comparison of the 12-byte key has to agree with signed comparison of (X, Y, Z). If it
	// does not, sorted pages are sorted by the wrong order and every lookup is a coin flip.
	const int32 Coordinates[] = { MIN_int32, MIN_int32 + 1, -70000, -1024, -33, -1, 0, 1, 33, 1024, 70000, MAX_int32 - 1, MAX_int32 };
	const int32 Count = UE_ARRAY_COUNT(Coordinates);

	int32 Compared = 0;
	for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
	{
		for (int32 A = 0; A < Count; ++A)
		{
			for (int32 B = 0; B < Count; ++B)
			{
				FTerrainChunkKey KeyA(7, 7, 7);
				FTerrainChunkKey KeyB(7, 7, 7);
				int32* TargetA = AxisIndex == 0 ? &KeyA.X : (AxisIndex == 1 ? &KeyA.Y : &KeyA.Z);
				int32* TargetB = AxisIndex == 0 ? &KeyB.X : (AxisIndex == 1 ? &KeyB.Y : &KeyB.Z);
				*TargetA = Coordinates[A];
				*TargetB = Coordinates[B];

				const FTerrainIndexKey IndexA = TerrainIndexKeyFromChunk(KeyA);
				const FTerrainIndexKey IndexB = TerrainIndexKeyFromChunk(KeyB);

				const bool bSignedLess = Coordinates[A] < Coordinates[B];
				const bool bKeyLess    = IndexA < IndexB;
				if (bSignedLess != bKeyLess)
				{
					AddError(FString::Printf(
						TEXT("Key order disagrees with signed order on axis %d: %d vs %d"),
						AxisIndex, Coordinates[A], Coordinates[B]));
					return false;
				}
				++Compared;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("Key order agreed with signed order on %d comparisons"), Compared));

	// X is the most significant component: a smaller X wins whatever Y and Z say.
	{
		const FTerrainIndexKey Low  = TerrainIndexKeyFromChunk(FTerrainChunkKey(-1, MAX_int32, MAX_int32));
		const FTerrainIndexKey High = TerrainIndexKeyFromChunk(FTerrainChunkKey(0, MIN_int32, MIN_int32));
		TestTrue(TEXT("X dominates Y and Z in the key order"), Low < High);
	}

	// Round trip, including the extremes.
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FTerrainChunkKey Key(Coordinates[Index], Coordinates[(Index + 3) % Count], Coordinates[(Index + 7) % Count]);
		const FTerrainIndexKey Encoded = TerrainIndexKeyFromChunk(Key);
		TestTrue(FString::Printf(TEXT("Key (%d,%d,%d) round-trips"), Key.X, Key.Y, Key.Z),
			TerrainChunkKeyFromIndex(Encoded) == Key);
	}

	// The transform is exactly 12 bytes and 12 levels, which is what fixes the traversal depth.
	TestEqual(TEXT("The index key is 12 bytes"), TerrainPersistIndexKeyBytes, 12);
	TestEqual(TEXT("Leaf depth is 11"), TerrainIndexLeafDepth, 11);
	TestEqual(TEXT("Maximum tree depth is 12"), TerrainPersistIndexMaxDepth, 12);

	return true;
}

// ==== PathCopy ==========================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerrainPersistenceIndexPathCopyTest,
	"TerrainCore.Persistence.Index.PathCopy",
	EAutomationTestFlags::EditorContext
	| EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext
	| EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

bool FTerrainPersistenceIndexPathCopyTest::RunTest(const FString& Parameters)
{
	using namespace TerrainIndexTest;

	const FTerrainPersistIdentity Identity = MakeIdentity();
	FTerrainMemoryObjectStore Store;

	// --- an empty index is legal and resolves nothing ------------------------------------
	{
		FTerrainIndexRoot Empty;
		FTerrainIndexValidation Stats;
		TestEqual(TEXT("An empty index validates"),
			TerrainIndexValidate(Identity, Store, Empty, Stats), ETerrainPersistError::None);
		TestEqual(TEXT("It holds no keys"), (int32)Stats.LeafCount, 0);

		FTerrainIndexLeafValue Value;
		bool bFound = true;
		TestEqual(TEXT("A lookup in an empty index succeeds"),
			TerrainIndexLookup(Identity, Store, Empty, FTerrainChunkKey(0, 0, 0), Value, bFound),
			ETerrainPersistError::None);
		TestFalse(TEXT("and finds nothing -- which means never edited, not air"), bFound);
	}

	// --- first checkpoint: 64 keys in a compact neighbourhood -----------------------------
	TArray<FTerrainIndexUpdate> First;
	for (int32 X = -2; X < 2; ++X)
	{
		for (int32 Y = -2; Y < 2; ++Y)
		{
			for (int32 Z = -2; Z < 2; ++Z)
			{
				FTerrainIndexUpdate Update;
				Update.Key   = FTerrainChunkKey(X, Y, Z);
				Update.Value = MakeLeafValue(ETerrainRegionEncoding::Dense, 1, 10,
					static_cast<uint8>(((X + 2) * 16 + (Y + 2) * 4 + (Z + 2)) & 0xFF));
				First.Add(Update);
			}
		}
	}

	FTerrainIndexRoot RootA;
	int32 PagesA = 0;
	TestEqual(TEXT("The first checkpoint applies"),
		TerrainIndexApply(Identity, Store, Store, FTerrainIndexRoot(), First, RootA, PagesA),
		ETerrainPersistError::None);

	AddInfo(FString::Printf(TEXT("First checkpoint: %d keys wrote %d pages (bound 12*D = %d)"),
		First.Num(), PagesA, 12 * First.Num()));
	TestTrue(TEXT("The first checkpoint is inside the 12*D bound"), PagesA <= 12 * First.Num());
	TestTrue(TEXT("Shared prefixes coalesced well below the bound"), PagesA < 12 * First.Num());

	{
		FTerrainIndexValidation Stats;
		TArray<FTerrainChunkKey> Keys;
		TestEqual(TEXT("The first tree validates"),
			TerrainIndexValidate(Identity, Store, RootA, Stats, &Keys), ETerrainPersistError::None);
		TestEqual(TEXT("It holds every key"), (int32)Stats.LeafCount, First.Num());
		TestEqual(TEXT("Its pages are the ones that were written"), (int32)Stats.PageCount, PagesA);
		TestEqual(TEXT("Every traversal reaches the leaf depth"), Stats.MaxDepthSeen, 12);

		// Keys come back in ascending order, which is the ordered-key diagnostic P-003
		// section 8 keeps the X||Y||Z layout for.
		for (int32 Index = 1; Index < Keys.Num(); ++Index)
		{
			const FTerrainIndexKey Previous = TerrainIndexKeyFromChunk(Keys[Index - 1]);
			const FTerrainIndexKey Current  = TerrainIndexKeyFromChunk(Keys[Index]);
			if (!(Previous < Current))
			{
				AddError(TEXT("Validation walked the keys out of order"));
				break;
			}
		}
	}

	for (const FTerrainIndexUpdate& Update : First)
	{
		FTerrainIndexLeafValue Value;
		bool bFound = false;
		TerrainIndexLookup(Identity, Store, RootA, Update.Key, Value, bFound);
		if (!bFound || Value.Rev != Update.Value.Rev || Value.PayloadDigest != Update.Value.PayloadDigest)
		{
			AddError(FString::Printf(TEXT("Key (%d,%d,%d) did not resolve through the first root"),
				Update.Key.X, Update.Key.Y, Update.Key.Z));
			break;
		}
	}

	const int32 ObjectsAfterFirst = Store.Num();

	// --- second checkpoint: one key changes -----------------------------------------------
	TArray<FTerrainIndexUpdate> Second;
	{
		FTerrainIndexUpdate Update;
		Update.Key   = FTerrainChunkKey(-1, 0, 1);
		Update.Value = MakeLeafValue(ETerrainRegionEncoding::SparseDiff, 2, 44, 0xC0);
		Second.Add(Update);
	}

	FTerrainIndexRoot RootB;
	int32 PagesB = 0;
	TestEqual(TEXT("The second checkpoint applies"),
		TerrainIndexApply(Identity, Store, Store, RootA, Second, RootB, PagesB),
		ETerrainPersistError::None);

	AddInfo(FString::Printf(TEXT("Second checkpoint: 1 changed key wrote %d pages; the store grew by %d objects"),
		PagesB, Store.Num() - ObjectsAfterFirst));
	TestEqual(TEXT("One changed key rewrites exactly one path of 12 pages"), PagesB, 12);
	TestTrue(TEXT("The store grew by at most that path"), Store.Num() - ObjectsAfterFirst <= 12);
	TestTrue(TEXT("The previous root is still intact and readable"), Store.Contains(RootA.RootPageDigest));
	TestFalse(TEXT("The new root is a different object"), RootB.RootPageDigest == RootA.RootPageDigest);

	{
		FTerrainIndexValidation StatsB;
		TestEqual(TEXT("The second tree validates"),
			TerrainIndexValidate(Identity, Store, RootB, StatsB), ETerrainPersistError::None);
		TestEqual(TEXT("It still holds every key"), (int32)StatsB.LeafCount, First.Num());
		TestEqual(TEXT("It has the same page count as its predecessor"), (int32)StatsB.PageCount, PagesA);
	}

	// The changed key reads its new value; every other key reads its old one, through the new
	// root. That is the whole claim of path copying: a complete independent tree that shares.
	{
		FTerrainIndexLeafValue Value;
		bool bFound = false;
		TerrainIndexLookup(Identity, Store, RootB, FTerrainChunkKey(-1, 0, 1), Value, bFound);
		TestTrue (TEXT("The changed key resolves through the new root"), bFound);
		TestEqual(TEXT("It carries the new revision"), Value.Rev, (FTerrainRev)2);
		TestTrue (TEXT("It carries the new encoding"), Value.Encoding == ETerrainRegionEncoding::SparseDiff);

		// and through the OLD root it still carries the old value: the old checkpoint is
		// unchanged, which is what makes older-root fallback a real option (P-003 section 5).
		bFound = false;
		TerrainIndexLookup(Identity, Store, RootA, FTerrainChunkKey(-1, 0, 1), Value, bFound);
		TestTrue (TEXT("The old root still resolves the key"), bFound);
		TestEqual(TEXT("and still carries the old revision"), Value.Rev, (FTerrainRev)1);
	}

	int32 Unchanged = 0;
	for (const FTerrainIndexUpdate& Update : First)
	{
		if (Update.Key == FTerrainChunkKey(-1, 0, 1))
		{
			continue;
		}
		FTerrainIndexLeafValue Value;
		bool bFound = false;
		TerrainIndexLookup(Identity, Store, RootB, Update.Key, Value, bFound);
		if (!bFound || Value.Rev != 1 || Value.PayloadDigest != Update.Value.PayloadDigest)
		{
			AddError(FString::Printf(TEXT("Untouched key (%d,%d,%d) did not survive the second checkpoint"),
				Update.Key.X, Update.Key.Y, Update.Key.Z));
			break;
		}
		++Unchanged;
	}
	TestEqual(TEXT("Every untouched key survived"), Unchanged, First.Num() - 1);

	// --- a chunk returning to the base becomes Empty, not absent ---------------------------
	{
		TArray<FTerrainIndexUpdate> ToEmpty;
		FTerrainIndexUpdate Update;
		Update.Key   = FTerrainChunkKey(0, 0, 0);
		Update.Value = MakeLeafValue(ETerrainRegionEncoding::Empty, 3, 91, 0);
		ToEmpty.Add(Update);

		FTerrainIndexRoot RootC;
		int32 PagesC = 0;
		TestEqual(TEXT("An Empty update applies"),
			TerrainIndexApply(Identity, Store, Store, RootB, ToEmpty, RootC, PagesC),
			ETerrainPersistError::None);

		FTerrainIndexLeafValue Value;
		bool bFound = false;
		TerrainIndexLookup(Identity, Store, RootC, FTerrainChunkKey(0, 0, 0), Value, bFound);
		TestTrue (TEXT("An Empty chunk is still IN the index"), bFound);
		TestTrue (TEXT("with Empty encoding"), Value.Encoding == ETerrainRegionEncoding::Empty);
		TestEqual(TEXT("and it keeps its revision metadata"), Value.Rev, (FTerrainRev)3);
		TestEqual(TEXT("and its last-change sequence"), Value.LastOpSeq, (FTerrainOpSeq)91);
		TestEqual(TEXT("and references no payload"), (int32)Value.PayloadLength, 0);

		FTerrainIndexValidation StatsC;
		TerrainIndexValidate(Identity, Store, RootC, StatsC);
		TestEqual(TEXT("The key count is unchanged: Empty is present, not deleted"),
			(int32)StatsC.LeafCount, First.Num());
	}

	// --- distant keys: the deep, sparse case ----------------------------------------------
	{
		FTerrainMemoryObjectStore Sparse;
		TArray<FTerrainIndexUpdate> Far;
		const FTerrainChunkKey Keys[4] = {
			FTerrainChunkKey(MIN_int32, 0, 0),
			FTerrainChunkKey(MAX_int32, 0, 0),
			FTerrainChunkKey(0, MIN_int32, MAX_int32),
			FTerrainChunkKey(-1, -1, -1),
		};
		for (int32 Index = 0; Index < 4; ++Index)
		{
			FTerrainIndexUpdate Update;
			Update.Key   = Keys[Index];
			Update.Value = MakeLeafValue(ETerrainRegionEncoding::Dense, 1, 5, static_cast<uint8>(Index + 1));
			Far.Add(Update);
		}

		FTerrainIndexRoot Root;
		int32 Pages = 0;
		TestEqual(TEXT("Four maximally distant keys apply"),
			TerrainIndexApply(Identity, Sparse, Sparse, FTerrainIndexRoot(), Far, Root, Pages),
			ETerrainPersistError::None);
		AddInfo(FString::Printf(TEXT("Four distant keys wrote %d pages (bound 48)"), Pages));
		TestTrue(TEXT("Still inside the 12*D bound"), Pages <= 48);

		FTerrainIndexValidation Stats;
		TestEqual(TEXT("The sparse tree validates"),
			TerrainIndexValidate(Identity, Sparse, Root, Stats), ETerrainPersistError::None);
		TestEqual(TEXT("It holds four keys"), (int32)Stats.LeafCount, 4);

		for (int32 Index = 0; Index < 4; ++Index)
		{
			FTerrainIndexLeafValue Value;
			bool bFound = false;
			TerrainIndexLookup(Identity, Sparse, Root, Keys[Index], Value, bFound);
			TestTrue(FString::Printf(TEXT("Distant key %d resolves"), Index), bFound);
		}

		// A key that was never written resolves to "not found", not to a neighbour's value.
		FTerrainIndexLeafValue Value;
		bool bFound = true;
		TerrainIndexLookup(Identity, Sparse, Root, FTerrainChunkKey(12345, 6789, -42), Value, bFound);
		TestFalse(TEXT("An unwritten key is not found"), bFound);
	}

	// --- input rules ------------------------------------------------------------------------
	{
		TArray<FTerrainIndexUpdate> Duplicate;
		FTerrainIndexUpdate Update;
		Update.Key   = FTerrainChunkKey(5, 5, 5);
		Update.Value = MakeLeafValue(ETerrainRegionEncoding::Dense, 1, 1, 0x01);
		Duplicate.Add(Update);
		Update.Value = MakeLeafValue(ETerrainRegionEncoding::Dense, 2, 2, 0x02);
		Duplicate.Add(Update);

		FTerrainIndexRoot Root;
		int32 Pages = 0;
		TestEqual(TEXT("A repeated key is refused, not resolved by last-write-wins"),
			TerrainIndexApply(Identity, Store, Store, FTerrainIndexRoot(), Duplicate, Root, Pages),
			ETerrainPersistError::DuplicateKey);

		TArray<FTerrainIndexUpdate> Inconsistent;
		FTerrainIndexUpdate Bad;
		Bad.Key = FTerrainChunkKey(6, 6, 6);
		Bad.Value.Encoding      = ETerrainRegionEncoding::Empty;
		Bad.Value.PayloadLength = 100;   // Empty must reference nothing
		Inconsistent.Add(Bad);
		TestEqual(TEXT("An Empty entry that references a payload is refused"),
			TerrainIndexApply(Identity, Store, Store, FTerrainIndexRoot(), Inconsistent, Root, Pages),
			ETerrainPersistError::FieldOutOfRange);

		TArray<FTerrainIndexUpdate> None;
		FTerrainIndexRoot Carried;
		TestEqual(TEXT("An empty update list succeeds"),
			TerrainIndexApply(Identity, Store, Store, RootB, None, Carried, Pages),
			ETerrainPersistError::None);
		TestTrue (TEXT("and leaves the root exactly as it was"),
			Carried.RootPageDigest == RootB.RootPageDigest);
		TestEqual(TEXT("and writes nothing"), Pages, 0);
	}

	// --- a missing referenced object is corruption, never "nothing there" --------------------
	{
		FTerrainMemoryObjectStore Truncated;
		TArray<FTerrainIndexUpdate> One;
		FTerrainIndexUpdate Update;
		Update.Key   = FTerrainChunkKey(1, 1, 1);
		Update.Value = MakeLeafValue(ETerrainRegionEncoding::Dense, 1, 1, 0x05);
		One.Add(Update);

		FTerrainIndexRoot Root;
		int32 Pages = 0;
		TerrainIndexApply(Identity, Truncated, Truncated, FTerrainIndexRoot(), One, Root, Pages);

		FTerrainMemoryObjectStore Missing;   // deliberately holds nothing
		FTerrainIndexValidation Stats;
		TestEqual(TEXT("A root whose pages are absent fails closed"),
			TerrainIndexValidate(Identity, Missing, Root, Stats), ETerrainPersistError::ShortBuffer);
	}

	// --- a page from another world is refused ------------------------------------------------
	{
		FTerrainPersistIdentity Other = Identity;
		Other.World.Bytes[0] ^= 0xFF;

		FTerrainIndexValidation Stats;
		TestEqual(TEXT("Another world's identity cannot read this index"),
			TerrainIndexValidate(Other, Store, RootB, Stats), ETerrainPersistError::WorldMismatch);
	}

	AddInfo(FString::Printf(TEXT("Store held %d objects totalling %lld bytes at the end of the test"),
		Store.Num(), Store.TotalBytes()));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#if WITH_DEV_AUTOMATION_TESTS

#include "TerrainRevisionIndex.h"
#include "TerrainService.h"
#include "Async/Async.h"
#include "Misc/AutomationTest.h"
#include "Subsystems/SubsystemCollection.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerrainRevisionMonotonicTest,
	"TerrainCore.Revision.Monotonic",
	EAutomationTestFlags::EditorContext
	| EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext
	| EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

bool FTerrainRevisionMonotonicTest::RunTest(const FString& Parameters)
{
	const FTerrainChunkKey A(-2, 3, -4);
	const FTerrainChunkKey B(0, 0, 0);
	const FTerrainChunkKey C(5, -6, 7);
	const FTerrainChunkKey Untouched(MIN_int32, MAX_int32, -1);
	const TArray<FTerrainChunkKey> Empty;
	const TArray<FTerrainChunkKey> First = { A, B, A, B, A };
	const TArray<FTerrainChunkKey> Second = { C, B, C };
	FTerrainRevisionIndex Index;
	TestEqual(TEXT("Unseen key reads as zero"), Index.GetRevision(A), FTerrainRev(0));
	TestEqual(TEXT("Unseen read does not insert"), Index.Revisions.Num(), 0);
	TestTrue(TEXT("Empty fresh update succeeds"), Index.TryBumpRevisions(Empty));
	TestEqual(TEXT("Empty update does not insert"), Index.Revisions.Num(), 0);
	TestTrue(TEXT("First multi-chunk update succeeds"), Index.TryBumpRevisions(First));
	TestEqual(TEXT("Negative key bumped once despite duplicates"), Index.GetRevision(A), FTerrainRev(1));
	TestEqual(TEXT("Origin bumped once despite duplicates"), Index.GetRevision(B), FTerrainRev(1));
	TestTrue(TEXT("Overlapping update succeeds"), Index.TryBumpRevisions(Second));
	TestEqual(TEXT("Unaffected existing key stays at one"), Index.GetRevision(A), FTerrainRev(1));
	TestEqual(TEXT("Overlap advances again"), Index.GetRevision(B), FTerrainRev(2));
	TestEqual(TEXT("New key starts at one"), Index.GetRevision(C), FTerrainRev(1));
	TestTrue(TEXT("Repeated call is another update"), Index.TryBumpRevisions(Second));
	TestTrue(TEXT("Empty populated update succeeds"), Index.TryBumpRevisions(Empty));
	TestEqual(TEXT("Repeated overlap advances once more"), Index.GetRevision(B), FTerrainRev(3));
	TestEqual(TEXT("Repeated new key advances once more"), Index.GetRevision(C), FTerrainRev(2));
	TestEqual(TEXT("Unseen extreme coordinates stay zero"), Index.GetRevision(Untouched), FTerrainRev(0));
	TestEqual(TEXT("Only three distinct entries exist"), Index.Revisions.Num(), 3);

	// Independent fixed-key oracle: count calls containing each key, not input occurrences.
	// Check EVERY key after every call, including keys absent from that call.
	FTerrainRevisionIndex SequenceIndex;
	const TArray<FTerrainChunkKey> Keys = { A, B, C, Untouched };
	TArray<FTerrainRev> Expected = { 0, 0, 0, 0 };
	FRandomStream Random(1123);
	for (int32 Step = 0; Step < 256; ++Step)
	{
		TArray<FTerrainChunkKey> Batch;
		const int32 BatchSize = Random.RandRange(0, 12);
		for (int32 Entry = 0; Entry < BatchSize; ++Entry)
		{
			Batch.Add(Keys[Random.RandRange(0, Keys.Num() - 1)]);
		}
		TestTrue(TEXT("Sequence update succeeds"), SequenceIndex.TryBumpRevisions(Batch));
		for (int32 KeyIndex = 0; KeyIndex < Keys.Num(); ++KeyIndex)
		{
			Expected[KeyIndex] += Batch.Contains(Keys[KeyIndex]) ? 1 : 0;
			TestEqual(FString::Printf(TEXT("Step %d key %d matches per-call oracle"), Step, KeyIndex),
				SequenceIndex.GetRevision(Keys[KeyIndex]), Expected[KeyIndex]);
		}
	}

	// A development-only friend seeds the boundary; no production restore/setter API.
	FTerrainRevisionIndex OverflowIndex;
	OverflowIndex.Revisions.Add(A, MAX_uint32 - 1);
	OverflowIndex.Revisions.Add(B, 9);
	const TArray<FTerrainChunkKey> ReachMaximum = { A, A };
	TestTrue(TEXT("Last representable increment succeeds"), OverflowIndex.TryBumpRevisions(ReachMaximum));
	TestEqual(TEXT("Duplicates reach max without wrapping"), OverflowIndex.GetRevision(A), FTerrainRev(MAX_uint32));
	for (int32 Order = 0; Order < 2; ++Order)
	{
		const TArray<FTerrainChunkKey> Rejected = Order == 0
			? TArray<FTerrainChunkKey>{ C, B, A, B }
			: TArray<FTerrainChunkKey>{ A, B, C, A };
		TestFalse(TEXT("Overflow rejects whole batch"), OverflowIndex.TryBumpRevisions(Rejected));
		TestEqual(TEXT("Overflow preserves saturated revision"), OverflowIndex.GetRevision(A), FTerrainRev(MAX_uint32));
		TestEqual(TEXT("Overflow preserves ordinary revision"), OverflowIndex.GetRevision(B), FTerrainRev(9));
		TestEqual(TEXT("Overflow does not create unseen entry"), OverflowIndex.GetRevision(C), FTerrainRev(0));
		TestEqual(TEXT("Rejected batch leaves map size unchanged"), OverflowIndex.Revisions.Num(), 2);
	}
	TestTrue(TEXT("Empty update succeeds even with saturated entry"), OverflowIndex.TryBumpRevisions(Empty));
	const TArray<FTerrainChunkKey> Independent = { B, C, C };
	TestTrue(TEXT("Saturation does not block other chunks"), OverflowIndex.TryBumpRevisions(Independent));
	TestEqual(TEXT("Independent existing chunk advances"), OverflowIndex.GetRevision(B), FTerrainRev(10));
	TestEqual(TEXT("Independent new chunk advances once"), OverflowIndex.GetRevision(C), FTerrainRev(1));

	// Section 6.1 requires no engine world. Exercise UObject lifecycle and rejection
	// without constructing a UWorld or backend. Live net modes remain later MP tests.
	TStrongObjectPtr<UTerrainService> Service(NewObject<UTerrainService>());
	TestFalse(TEXT("Uninitialized service has no authority"), Service->HasAuthority());
	TestFalse(TEXT("Uninitialized service rejects mutation"), Service->TryAdvanceRevisions(First));
	TestEqual(TEXT("Uninitialized service reads zero"), Service->GetRevision(A), FTerrainRev(0));
	FSubsystemCollection<UWorldSubsystem> Collection;
	Service->Initialize(Collection);
	if (!TestTrue(TEXT("Initialize creates revision ownership"), Service->RevisionIndex.IsValid()))
	{
		Service->Deinitialize();
		return false;
	}
	TestTrue(TEXT("Seed owned index fixture"), Service->RevisionIndex->TryBumpRevisions(First));
	TestEqual(TEXT("Service queries its owned index"), Service->GetRevision(A), FTerrainRev(1));
	Service->Initialize(Collection);
	TestEqual(TEXT("Repeated initialize retains revisions"), Service->GetRevision(A), FTerrainRev(1));
	TestFalse(TEXT("Initialized service without world has no authority"), Service->HasAuthority());
	TestFalse(TEXT("Missing world rejects mutation"), Service->TryAdvanceRevisions(First));
	TestEqual(TEXT("Rejected mutation preserves revision"), Service->GetRevision(A), FTerrainRev(1));
	const bool bWorkerAccepted = Async(EAsyncExecution::Thread, [&Service, &First]()
	{
		return Service->TryAdvanceRevisions(First);
	}).Get();
	TestFalse(TEXT("Worker-thread mutation is rejected"), bWorkerAccepted);
	TestEqual(TEXT("Worker rejection preserves revision"), Service->GetRevision(A), FTerrainRev(1));
	Service->Deinitialize();
	TestFalse(TEXT("Teardown releases revision ownership"), Service->RevisionIndex.IsValid());
	TestFalse(TEXT("Deinitialized service has no authority"), Service->HasAuthority());
	TestFalse(TEXT("Deinitialized service rejects mutation"), Service->TryAdvanceRevisions(First));
	TestEqual(TEXT("Deinitialized query is zero"), Service->GetRevision(A), FTerrainRev(0));
	Service->Deinitialize();
	TestFalse(TEXT("Repeated teardown remains empty"), Service->RevisionIndex.IsValid());

	AddInfo(TEXT("Revision invariants: 256 overlapping batches; duplicates, empty input, extreme keys and atomic overflow checked. Service lifecycle and worldless/thread rejection checked; no live multiplayer claim."));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

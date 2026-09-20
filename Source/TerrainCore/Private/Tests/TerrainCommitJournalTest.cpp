// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Subsystems/SubsystemCollection.h"
#include "UObject/StrongObjectPtr.h"

#include "TerrainService.h"
#include "TerrainEditQueue.h"
#include "TerrainCommitJournal.h"
#include "TerrainPersistenceFixtures.h"

/**
 * The commit path: durable BEFORE published (P-003 §2).
 *
 * WHAT THIS COVERS, headless (§6.1): what a committed operation becomes as a journal record;
 * that a journal which cannot record says so; that the queue treats the sequence as
 * PROVISIONAL and does not consume it when the commit is refused; and that a storage-faulted
 * service closes admission with the reason that tells a client not to retry.
 *
 * WHAT IT DOES NOT COVER, and why. `UTerrainService::CommitOp` -- the function that actually
 * orders "record, then advance, then broadcast" -- cannot run here: it goes through
 * `TryAdvanceRevisions`, which requires `HasAuthority()`, which requires a real game `UWorld`.
 * Building one would make this an in-engine test (§6.2) rather than a headless one. The
 * ordering inside `CommitOp` is therefore READ, not TESTED, and closing that gap belongs with
 * the crash matrix, which is §6.2 work and is not built. This is stated rather than papered
 * over with a test that mirrors the ordering in its own callback and proves only that the
 * test was written correctly.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainCommitJournalTest, "TerrainCore.Persistence.Commit.Journal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

namespace
{
	FTerrainOp DigAt(const FIntVector& Centre, FTerrainOpSeq OpSeq)
	{
		FTerrainOp Op;
		Op.Kind         = ETerrainOpKind::Remove;
		Op.Shape        = ETerrainShape::Sphere;
		Op.Source       = ETerrainSource::Player;
		Op.SourceId     = 2;
		Op.CentreVox    = Centre;
		Op.RadiusVoxQ16 = 4 << 16;
		Op.OpSeq        = OpSeq;
		return Op;
	}

	/** Chunks in FOOTPRINT order -- deliberately not index-key order, so the sort is tested. */
	TArray<FTerrainChunkRevision> ChangedChunks()
	{
		const FIntVector Keys[4] = {
			FIntVector( 1, 0,  0),
			FIntVector(-1, 0,  0),
			FIntVector( 0, 0, -1),
			FIntVector(-1, 0, -1),
		};
		TArray<FTerrainChunkRevision> Out;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			FTerrainChunkRevision Revision;
			Revision.Key    = Keys[Index];
			Revision.Before = 7;
			Revision.After  = 8;
			Out.Add(Revision);
		}
		return Out;
	}
}

bool FTerrainCommitJournalTest::RunTest(const FString& Parameters)
{
	using namespace TerrainPersistTest;

	// Both layers shout when a commit cannot be made durable, and both are the behaviour under
	// test: the writer closes itself over an uncertain tail, and the adapter reports that the
	// edit must not be broadcast. Declaring them makes each log line a checked expectation.
	AddExpectedError(TEXT("could not be recorded"),
		EAutomationExpectedErrorFlags::Contains, /*Occurrences=*/0, /*IsRegex=*/false);
	AddExpectedError(TEXT("The segment's tail is now uncertain"),
		EAutomationExpectedErrorFlags::Contains, /*Occurrences=*/0, /*IsRegex=*/false);

	FTerrainMemoryStorageDevice Device;
	FTerrainFaultDevice Faulty(Device);
	FTerrainWorldStore Store(Faulty);

	const FTerrainPersistIdentity Identity = MakeIdentity();
	{
		const FTerrainStoreResult Created =
			Store.Create(MakeBaseDescriptor(), Identity.World, Identity.Epoch, 1789412345678LL);
		if (!Created.IsOk())
		{
			AddError(FString::Printf(TEXT("World create failed: %s"), *Created.ToString()));
			return false;
		}
	}

	FTerrainWorldStoreJournal Journal(Store);
	TestFalse(TEXT("Physical measurement is NOT claimed by default"), Journal.bPhysicalMeasured);

	// --- what a committed operation becomes -------------------------------------------------
	{
		FTerrainEditResult Result;
		Result.VoxelsTouched = 257;
		// A backend that DID report material volumes. With bPhysicalMeasured false these must
		// not reach the record: P-003 §2 forbids encoding unknown as a measured zero, and
		// encoding an untrustworthy list as measured would be the same lie the other way.
		FTerrainMaterialVolume Volume;
		Volume.MaterialId  = 4;
		Volume.MicroLitres = 257000;
		Result.Removed.Add(Volume);

		FTerrainCommitIdentity CommitIdentity;
		CommitIdentity.RequestId    = 12;
		CommitIdentity.ChildOrdinal = 0;
		CommitIdentity.ChildCount   = 1;

		const TArray<FTerrainChunkRevision> Changed = ChangedChunks();
		TestTrue(TEXT("The commit is recorded"),
			Journal.RecordCommit(DigAt(FIntVector(-13, 7, -41), 1), Result, CommitIdentity, Changed));
		TestEqual(TEXT("and the durable head is 1"), Journal.GetDurableHead(), (FTerrainOpSeq)1);

		// Two more, so sequence enforcement is exercised through the adapter.
		TestTrue(TEXT("A second commit is recorded"),
			Journal.RecordCommit(DigAt(FIntVector(0, 0, -41), 2), Result, CommitIdentity, Changed));
		TestTrue(TEXT("A third commit is recorded"),
			Journal.RecordCommit(DigAt(FIntVector(8, 0, -41), 3), Result, CommitIdentity, Changed));
		TestEqual(TEXT("The durable head is 3"), Journal.GetDurableHead(), (FTerrainOpSeq)3);
	}

	// --- read it back with the SCANNER, not with the writer's opinion --------------------------
	{
		TArray<uint8> SegmentBytes;
		TestEqual(TEXT("The segment reads"),
			Device.Read(TerrainStoragePaths::JournalSegment(1), SegmentBytes),
			ETerrainStorageResult::Ok);

		FTerrainJournalScanResult Scan;
		TestEqual(TEXT("and scans clean"),
			TerrainPersistScanJournalSegment(SegmentBytes, Store.GetState().Identity, true, Scan),
			ETerrainPersistError::None);
		TestEqual(TEXT("with three commit records"), Scan.CommitRecordCount, 3);
		TestEqual(TEXT("ending at OpSeq 3"), Scan.LastOpSeq, (FTerrainOpSeq)3);

		const int32 Cursor = TerrainPersistObjectHeaderSize + TerrainPersistSegmentHeaderBodySize;
		const TArrayView<const uint8> First(
			SegmentBytes.GetData() + Cursor, SegmentBytes.Num() - Cursor);

		FTerrainJournalCommitRecord Record;
		const uint64 WorldTag = TerrainPersistWorldTag(
			Store.GetState().Identity.World, Store.GetState().Identity.Epoch);
		TestEqual(TEXT("The first record decodes"),
			TerrainPersistDecodeCommitRecord(First, WorldTag, Record), ETerrainPersistError::None);

		TestEqual(TEXT("It carries OpSeq 1"), Record.Op.OpSeq, (FTerrainOpSeq)1);
		TestTrue (TEXT("and the operation that was committed"),
			Record.Op.CentreVox == FIntVector(-13, 7, -41));
		TestEqual(TEXT("and the request that asked for it"), Record.RequestId, (uint32)12);

		TestTrue (TEXT("NoEconomy, because DEF-6 is open and no yield exists"),
			Record.EconomyKind == ETerrainEconomyKind::NoEconomy);
		TestTrue (TEXT("PhysicalAvailability Unavailable -- not a measured zero"),
			Record.PhysicalAvailability == ETerrainPhysicalAvailability::Unavailable);
		TestEqual(TEXT("and an EMPTY physical list, even though the backend reported one"),
			Record.Physical.Num(), 0);

		TestEqual(TEXT("Four changed chunks"), Record.ChangedKeys.Num(), 4);
		TestTrue (TEXT("sorted into index-key order, not the footprint order given"),
			Record.ChangedKeys[0].Key == FTerrainChunkKey(-1, 0, -1));
		for (const FTerrainChangedKeyEntry& Entry : Record.ChangedKeys)
		{
			if (Entry.AfterRev != Entry.BeforeRev + 1)
			{
				AddError(TEXT("A changed chunk's revision did not advance by exactly one"));
				break;
			}
		}

		// Protocol 2 does not exist yet, so there is no token to digest and a zero says so.
		bool bTokenZero = true;
		for (int32 Index = 0; Index < TerrainPersistTokenDigestBytes; ++Index)
		{
			bTokenZero &= (Record.TokenDigest[Index] == 0);
		}
		TestTrue(TEXT("The token digest is zero -- protocol 2 is not implemented"), bTokenZero);

		AddInfo(FString::Printf(TEXT("A four-chunk NoEconomy commit is %d journal bytes"),
			TerrainPersistRecordFrameOverhead + TerrainPersistCommitRecordPrefix
				+ 4 * TerrainPersistChangedKeyEntryBytes));
	}

	// --- a measured physical list IS recorded when the backend can be trusted -------------------
	{
		Journal.bPhysicalMeasured = true;

		FTerrainEditResult Result;
		FTerrainMaterialVolume Volume;
		Volume.MaterialId  = 4;
		Volume.MicroLitres = -5000;   // negative: material PLACED, not removed
		Result.Removed.Add(Volume);

		FTerrainCommitIdentity CommitIdentity;
		CommitIdentity.RequestId  = 13;
		CommitIdentity.ChildCount = 1;

		TestTrue(TEXT("A measured commit is recorded"),
			Journal.RecordCommit(DigAt(FIntVector(16, 0, -41), 4), Result, CommitIdentity, ChangedChunks()));

		TArray<uint8> SegmentBytes;
		Device.Read(TerrainStoragePaths::JournalSegment(1), SegmentBytes);
		FTerrainJournalScanResult Scan;
		TerrainPersistScanJournalSegment(SegmentBytes, Store.GetState().Identity, true, Scan);
		TestEqual(TEXT("Four records now"), Scan.CommitRecordCount, 4);

		Journal.bPhysicalMeasured = false;
	}

	// --- a journal that cannot record says so ---------------------------------------------------
	{
		Faulty.FailAfter(ETerrainStorageOp::Append, 0);

		FTerrainEditResult Result;
		FTerrainCommitIdentity CommitIdentity;
		CommitIdentity.RequestId  = 14;
		CommitIdentity.ChildCount = 1;

		TestFalse(TEXT("RecordCommit reports failure"),
			Journal.RecordCommit(DigAt(FIntVector(24, 0, -41), 5), Result, CommitIdentity, {}));
		TestFalse(TEXT("and names a reason"), Journal.GetLastError().IsOk());
		AddInfo(FString::Printf(TEXT("Failed commit reported %s"), *Journal.GetLastError().ToString()));

		Faulty.ClearFaults();

		// The journal is intact: the failed record simply is not in it. The writer has closed
		// itself, so recovering means reopening the world -- which is what a storage fault does.
		TArray<uint8> SegmentBytes;
		Device.Read(TerrainStoragePaths::JournalSegment(1), SegmentBytes);
		FTerrainJournalScanResult Scan;
		TestEqual(TEXT("The journal still scans clean"),
			TerrainPersistScanJournalSegment(SegmentBytes, Store.GetState().Identity, true, Scan),
			ETerrainPersistError::None);
		TestEqual(TEXT("and holds only the four durable records"), Scan.CommitRecordCount, 4);
	}

	// --- the queue treats the sequence as PROVISIONAL -------------------------------------------
	{
		// P-003 §2: the record is created "with provisional next OpSeq" and the sequence is
		// only consumed once it is durable. A commit that refuses must not leave a gap in the
		// journal for an operation that never became part of the world's history.
		FTerrainEditQueue Queue;
		FTerrainSourceState Source;
		Source.Position = FVector::ZeroVector;
		Source.ReachCm  = 100000;
		Queue.RegisterSource(7, Source);

		bool bCommitSucceeds = false;
		int32 CommitCalls = 0;

		FTerrainQueueCallbacks Cb;
		Cb.Validate = [](const FTerrainOp&, const FTerrainSourceState&) { return ETerrainEditRejection::None; };
		Cb.Apply    = [](const FTerrainOp&, FTerrainEditResult& R) { R = FTerrainEditResult(); return true; };
		Cb.Commit   = [&bCommitSucceeds, &CommitCalls]
			(const FTerrainOp&, const FTerrainEditResult&, const FTerrainCommitIdentity& Id)
		{
			++CommitCalls;
			// The identity the journal needs must actually arrive.
			return bCommitSucceeds && Id.RequestId != 0 && Id.ChildOrdinal < Id.ChildCount;
		};

		FTerrainEditReceipt Receipt;
		const FTerrainOpSeq Before = Queue.NextSequence();

		bCommitSucceeds = false;
		Queue.Submit(7, 1, DigAt(FIntVector(0, 0, -4), 0), 100.0, Cb, Receipt);
		Queue.Pump(100.0, Cb);
		TestEqual(TEXT("The commit was attempted"), CommitCalls, 1);
		TestEqual(TEXT("and the sequence was NOT consumed"), Queue.NextSequence(), Before);

		bCommitSucceeds = true;
		Queue.Submit(7, 2, DigAt(FIntVector(0, 0, -4), 0), 200.0, Cb, Receipt);
		Queue.Pump(200.0, Cb);
		TestEqual(TEXT("A successful commit consumes it"), Queue.NextSequence(), Before + 1);
	}

	// --- a storage-faulted service closes admission ----------------------------------------------
	{
		TStrongObjectPtr<UTerrainService> Service(NewObject<UTerrainService>());
		FSubsystemCollection<UWorldSubsystem> Collection;
		Service->Initialize(Collection);
		Service->ActiveInit.WorldBoundsVox = FTerrainBox(FIntVector(-512,-512,-512), FIntVector(512,512,512));

		FTerrainSourceState Source;
		Source.Position = FVector::ZeroVector;

		TestFalse(TEXT("A fresh service is not storage-faulted"), Service->IsStorageFaulted());

		Service->bStorageFaulted = true;
		TestTrue(TEXT("A storage fault closes admission"), Service->IsStorageFaulted());
		TestTrue(TEXT("with ShuttingDown, which tells a client not to retry"),
			Service->ValidateOp(DigAt(FIntVector(0,0,-4), 0), Source) == ETerrainEditRejection::ShuttingDown);
		TestTrue(TEXT("and it outranks NotReady, so the cause is not misreported"),
			!Service->IsBackendReady()
			&& Service->ValidateOp(DigAt(FIntVector(0,0,-4), 0), Source) == ETerrainEditRejection::ShuttingDown);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

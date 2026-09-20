// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "Misc/AutomationTest.h"
#include "TerrainJournalWriter.h"
#include "TerrainWorldStore.h"
#include "TerrainPersistenceFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The journal writer and the world store (P-004 §9, §11; P-003 §3, §4, §5).
 *
 * EVERY CLAIM ABOUT WHAT THE WRITER WROTE IS CHECKED WITH THE SCANNER, never with the
 * writer's own state. The writer and `TerrainPersistScanJournalSegment` were built as two
 * halves of one contract precisely so that neither can mark its own work, and a test that
 * asked the writer whether it had written correctly would throw that away.
 */

namespace TerrainJournalTest
{
	/** A commit record at a chosen sequence, valid for the fixture identity. */
	FTerrainJournalCommitRecord Commit(FTerrainOpSeq OpSeq, uint32 RequestId)
	{
		FTerrainJournalCommitRecord Record = TerrainPersistTest::MakeCommitRecord();
		Record.Op.OpSeq = OpSeq;
		Record.RequestId = RequestId;
		Record.IntentDigest = TerrainPersistComputeIntentDigest(
			Record.Op, Record.TokenDigest, Record.RequestId, Record.ChildOrdinal, Record.ChildCount);
		return Record;
	}

	/** Reads a segment back and validates it the way boot would. */
	ETerrainPersistError ScanSegment(ITerrainStorageDevice& Device,
	                                 const FTerrainPersistIdentity& Identity,
	                                 uint64 SegmentId, bool bActive,
	                                 FTerrainJournalScanResult& Out)
	{
		TArray<uint8> Bytes;
		if (Device.Read(TerrainStoragePaths::JournalSegment(SegmentId), Bytes) != ETerrainStorageResult::Ok)
		{
			return ETerrainPersistError::ShortBuffer;
		}
		return TerrainPersistScanJournalSegment(Bytes, Identity, bActive, Out);
	}

	FTerrainBaseDescriptor Base() { return TerrainPersistTest::MakeBaseDescriptor(); }
}

// ==== Journal.Writer ====================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerrainJournalWriterTest,
	"TerrainCore.Persistence.Journal.Writer",
	EAutomationTestFlags::EditorContext
	| EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext
	| EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

bool FTerrainJournalWriterTest::RunTest(const FString& Parameters)
{
	using namespace TerrainPersistTest;
	using namespace TerrainJournalTest;

	// These errors are the behaviour under test, not noise: the writer is SUPPOSED to shout
	// when it closes itself over an uncertain tail, and when boot refuses an unanchored
	// segment that holds records. Declaring them makes the log line a checked expectation --
	// if the writer ever stopped reporting either, the test fails for that reason alone.
	AddExpectedError(TEXT("append of OpSeq 2 failed"),
		EAutomationExpectedErrorFlags::Contains, /*Occurrences=*/0, /*IsRegex=*/false);
	AddExpectedError(TEXT("is newer than the anchored segment"),
		EAutomationExpectedErrorFlags::Contains, /*Occurrences=*/0, /*IsRegex=*/false);

	const FTerrainPersistIdentity Identity = MakeIdentity();

	// --- create, append, and verify with the SCANNER ---------------------------------------
	FTerrainMemoryStorageDevice Device;
	{
		FTerrainJournalWriter Writer(Device, Identity);
		TestTrue(TEXT("Create succeeds"), Writer.Create(1, 1, 1789412345678LL).IsOk());
		TestEqual(TEXT("The active segment is 1"), Writer.GetState().ActiveSegmentId, (uint64)1);
		TestEqual(TEXT("The next sequence is 1"), Writer.GetNextOpSeq(), (FTerrainOpSeq)1);
		TestEqual(TEXT("A fresh journal has head 0"), Writer.GetHead(), (FTerrainOpSeq)0);

		for (uint32 Index = 1; Index <= 5; ++Index)
		{
			const FTerrainStoreResult Result = Writer.AppendCommit(Commit(Index, Index));
			if (!Result.IsOk())
			{
				AddError(FString::Printf(TEXT("Append %u failed: %s"), Index, *Result.ToString()));
				return false;
			}
		}
		TestEqual(TEXT("Head is 5"), Writer.GetHead(), (FTerrainOpSeq)5);
		TestEqual(TEXT("Five records"), Writer.GetState().CommitRecordCount, 5);

		FTerrainJournalScanResult Scan;
		TestEqual(TEXT("The written segment scans clean"),
			ScanSegment(Device, Identity, 1, true, Scan), ETerrainPersistError::None);
		TestEqual(TEXT("and the scanner agrees about the record count"), Scan.CommitRecordCount, 5);
		TestEqual(TEXT("and about the last sequence"), Scan.LastOpSeq, (FTerrainOpSeq)5);
		TestFalse(TEXT("and sees no torn tail"), Scan.bTornTail);
	}

	// --- the sequence and identity rules ----------------------------------------------------
	{
		FTerrainJournalWriter Writer(Device, Identity);
		TestTrue(TEXT("Reopening succeeds"), Writer.Open().IsOk());
		TestEqual(TEXT("State survives a reopen: head is still 5"), Writer.GetHead(), (FTerrainOpSeq)5);
		TestEqual(TEXT("and the record count was recovered"), Writer.GetState().CommitRecordCount, 5);
		TestEqual(TEXT("and the next sequence is 6"), Writer.GetNextOpSeq(), (FTerrainOpSeq)6);

		const FTerrainStoreResult Gap = Writer.AppendCommit(Commit(8, 8));
		TestEqual(TEXT("A sequence gap is refused"), Gap.Format, ETerrainPersistError::OrderViolation);

		const FTerrainStoreResult Repeat = Writer.AppendCommit(Commit(5, 9));
		TestEqual(TEXT("A repeated sequence is refused"), Repeat.Format, ETerrainPersistError::OrderViolation);

		FTerrainJournalCommitRecord Foreign = Commit(6, 6);
		Foreign.WorldTag ^= 0xFFull;
		const FTerrainStoreResult Spliced = Writer.AppendCommit(Foreign);
		TestEqual(TEXT("A record from another world is refused"),
			Spliced.Format, ETerrainPersistError::WorldMismatch);

		TestTrue(TEXT("and the correct next record is accepted"), Writer.AppendCommit(Commit(6, 6)).IsOk());
		TestEqual(TEXT("Head is 6"), Writer.GetHead(), (FTerrainOpSeq)6);
	}

	// --- seal and rotate, in P-004 9.5's order -----------------------------------------------
	{
		FTerrainJournalWriter Writer(Device, Identity);
		TestTrue(TEXT("Reopen before rotating"), Writer.Open().IsOk());

		TestTrue(TEXT("Rotate succeeds"), Writer.Rotate(2, 1789412399999LL).IsOk());
		TestEqual(TEXT("The active segment is 2"), Writer.GetState().ActiveSegmentId, (uint64)2);
		TestEqual(TEXT("whose first sequence continues the journal"),
			Writer.GetState().FirstOpSeq, (FTerrainOpSeq)7);
		TestEqual(TEXT("and whose head is still the predecessor's"), Writer.GetHead(), (FTerrainOpSeq)6);

		// The predecessor must be sealed, and the seal must describe it truthfully.
		FTerrainJournalScanResult Sealed;
		TestEqual(TEXT("Segment 1 scans as an INACTIVE segment, which tolerates no torn tail"),
			ScanSegment(Device, Identity, 1, false, Sealed), ETerrainPersistError::None);
		TestTrue (TEXT("Segment 1 is sealed"), Sealed.bSealed);
		TestEqual(TEXT("and its seal names the right last sequence"), Sealed.Seal.LastOpSeq, (FTerrainOpSeq)6);
		TestEqual(TEXT("and the right record count"), (int32)Sealed.Seal.CommitRecordCount, 6);

		// Continuity evidence at both ends of the join (P-003 §5).
		TArray<uint8> SegmentTwo;
		Device.Read(TerrainStoragePaths::JournalSegment(2), SegmentTwo);
		FTerrainPersistObjectHeader Header;
		TArrayView<const uint8> Body;
		int32 Consumed = 0;
		TestEqual(TEXT("Segment 2's header object decodes"),
			TerrainPersistDecodeObjectPrefix(SegmentTwo, ETerrainPersistObjectType::JournalSegmentHeader,
				&Identity, Header, Body, Consumed),
			ETerrainPersistError::None);

		FTerrainJournalSegmentHeader Decoded;
		TestEqual(TEXT("and its body decodes"),
			TerrainPersistDecodeSegmentHeaderBody(Body, Decoded), ETerrainPersistError::None);
		TestTrue (TEXT("Segment 2 declares a predecessor"), Decoded.bHasPredecessor);
		TestEqual(TEXT("which is segment 1"), Decoded.PredecessorSegmentId, (uint64)1);
		TestFalse(TEXT("and carries a non-zero seal digest"), Decoded.PredecessorSealDigest.IsZero());

		TestTrue(TEXT("Appending continues into segment 2"), Writer.AppendCommit(Commit(7, 7)).IsOk());
		TestEqual(TEXT("Head is 7"), Writer.GetHead(), (FTerrainOpSeq)7);

		TestEqual(TEXT("Rotating backwards is refused"),
			Writer.Rotate(1, 0).Format, ETerrainPersistError::FieldOutOfRange);
	}

	// --- a reopen after rotation finds segment 2, not segment 1 -------------------------------
	{
		FTerrainJournalWriter Writer(Device, Identity);
		TestTrue (TEXT("Reopen after rotation"), Writer.Open().IsOk());
		TestEqual(TEXT("The anchor named segment 2"), Writer.GetState().ActiveSegmentId, (uint64)2);
		TestEqual(TEXT("and the head is 7"), Writer.GetHead(), (FTerrainOpSeq)7);
		TestEqual(TEXT("with no orphans"), Writer.GetOrphanSegments().Num(), 0);
	}

	// --- a torn append blocks the writer rather than layering over it ---------------------------
	{
		FTerrainMemoryStorageDevice Torn;
		FTerrainFaultDevice Faulty(Torn);
		FTerrainJournalWriter Writer(Faulty, Identity);
		TestTrue(TEXT("Create on the faulty device"), Writer.Create(1, 1, 0).IsOk());
		TestTrue(TEXT("One good record"), Writer.AppendCommit(Commit(1, 1)).IsOk());

		// Tear the next append halfway through its frame.
		Faulty.TearAfter(ETerrainStorageOp::Append, 0, /*TearBytes=*/60);
		const FTerrainStoreResult TornResult = Writer.AppendCommit(Commit(2, 2));
		TestFalse(TEXT("The torn append reports failure"), TornResult.IsOk());
		TestTrue (TEXT("and the writer closes itself -- the tail is uncertain"),
			Writer.GetState().bBlocked);
		TestFalse(TEXT("and refuses further appends"), Writer.AppendCommit(Commit(2, 2)).IsOk());

		Faulty.ClearFaults();

		// Reopening finds the tear, reports it, and still refuses to append.
		FTerrainJournalWriter Reopened(Torn, Identity);
		TestTrue (TEXT("The journal still opens"), Reopened.Open().IsOk());
		TestTrue (TEXT("and reports the torn tail"), Reopened.GetState().bTornTail);
		TestEqual(TEXT("with the last complete record intact"), Reopened.GetHead(), (FTerrainOpSeq)1);
		AddInfo(FString::Printf(TEXT("Torn tail of %d bytes found on reopen"),
			Reopened.GetState().TornTailBytes));
		TestTrue (TEXT("Appending is refused until it is repaired"), Reopened.GetState().bBlocked);
		TestFalse(TEXT("so a good record cannot be layered over an unknown one"),
			Reopened.AppendCommit(Commit(2, 2)).IsOk());
	}

	// --- an interrupted rotation leaves an ignorable orphan ------------------------------------
	{
		FTerrainMemoryStorageDevice Orphaned;
		FTerrainFaultDevice Faulty(Orphaned);
		FTerrainJournalWriter Writer(Faulty, Identity);
		TestTrue(TEXT("Create"), Writer.Create(1, 1, 0).IsOk());
		TestTrue(TEXT("One record"), Writer.AppendCommit(Commit(1, 1)).IsOk());

		// Fail the anchor publication, AFTER the new segment header has been written. That is
		// the exact window P-004 §9.5's ordering exists to make survivable.
		Faulty.FailAfter(ETerrainStorageOp::OverwriteInPlace, 0);
		TestFalse(TEXT("The rotation fails at the anchor"), Writer.Rotate(2, 0).IsOk());
		TestTrue (TEXT("but the new segment file was created"),
			Orphaned.Exists(TerrainStoragePaths::JournalSegment(2)));

		Faulty.ClearFaults();
		FTerrainJournalWriter Reopened(Orphaned, Identity);
		TestTrue (TEXT("Boot still succeeds"), Reopened.Open().IsOk());
		TestEqual(TEXT("and the anchor still names the sealed segment 1"),
			Reopened.GetState().ActiveSegmentId, (uint64)1);
		TestEqual(TEXT("with segment 2 reported as an orphan"), Reopened.GetOrphanSegments().Num(), 1);
		TestEqual(TEXT("which is segment 2"), Reopened.GetOrphanSegments()[0], (uint64)2);
		TestEqual(TEXT("No acknowledged record was lost"), Reopened.GetHead(), (FTerrainOpSeq)1);

		// The orphan's ID is consumed: retrying the rotation with the SAME id is refused,
		// because an immutable object is never overwritten. A caller must pick the next id.
		TestEqual(TEXT("Retrying the rotation with the same segment id is refused"),
			Reopened.Rotate(2, 0).Storage, ETerrainStorageResult::AlreadyExists);
		TestTrue (TEXT("and the next id works"), Reopened.Rotate(3, 0).IsOk());
		TestEqual(TEXT("leaving segment 3 active"), Reopened.GetState().ActiveSegmentId, (uint64)3);
	}

	// --- a newer UNANCHORED NON-EMPTY segment is a protocol violation ----------------------------
	{
		FTerrainMemoryStorageDevice Violating;
		FTerrainJournalWriter Writer(Violating, Identity);
		TestTrue(TEXT("Create"), Writer.Create(1, 1, 0).IsOk());
		TestTrue(TEXT("One record"), Writer.AppendCommit(Commit(1, 1)).IsOk());

		// A segment newer than the anchored one that actually holds records. Ordinary boot must
		// never adopt it; only the offline repair path may consider it, and it does not exist.
		TArray<uint8> Bytes;
		Violating.Read(TerrainStoragePaths::JournalSegment(1), Bytes);
		Violating.WriteNew(TerrainStoragePaths::JournalSegment(7), Bytes);

		FTerrainJournalWriter Reopened(Violating, Identity);
		const FTerrainStoreResult Result = Reopened.Open();
		TestFalse(TEXT("Boot refuses"), Result.IsOk());
		TestEqual(TEXT("as a protocol violation"), Result.Format, ETerrainPersistError::OrderViolation);
	}

	// --- a named-but-missing segment is corruption, never "no more ops" ---------------------------
	{
		FTerrainMemoryStorageDevice Missing;
		FTerrainJournalWriter Writer(Missing, Identity);
		TestTrue(TEXT("Create"), Writer.Create(1, 1, 0).IsOk());
		TestTrue(TEXT("One record"), Writer.AppendCommit(Commit(1, 1)).IsOk());

		Missing.Delete(TerrainStoragePaths::JournalSegment(1));

		FTerrainJournalWriter Reopened(Missing, Identity);
		const FTerrainStoreResult Result = Reopened.Open();
		TestFalse(TEXT("Boot refuses rather than reporting an empty journal"), Result.IsOk());
		AddInfo(FString::Printf(TEXT("Missing anchored segment rejected with %s"), *Result.ToString()));
	}

	// --- another world's journal is not this world's journal ---------------------------------------
	{
		FTerrainPersistIdentity Other = Identity;
		Other.Epoch.Bytes[0] ^= 0xFF;

		FTerrainJournalWriter Foreign(Device, Other);
		const FTerrainStoreResult Result = Foreign.Open();
		TestFalse(TEXT("Opening with another store epoch fails"), Result.IsOk());
		TestEqual(TEXT("at the anchor's identity check"), Result.Format, ETerrainPersistError::EpochMismatch);
	}

	return true;
}

// ==== WorldStore.Lifecycle ==============================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerrainWorldStoreLifecycleTest,
	"TerrainCore.Persistence.WorldStore.Lifecycle",
	EAutomationTestFlags::EditorContext
	| EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext
	| EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

bool FTerrainWorldStoreLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace TerrainPersistTest;
	using namespace TerrainJournalTest;

	// Both are the behaviour under test: a failed publication must say so and leave the
	// previous root current, and a checkpoint beyond the journal head must refuse the world.
	AddExpectedError(TEXT("publishing root generation"),
		EAutomationExpectedErrorFlags::Contains, /*Occurrences=*/0, /*IsRegex=*/false);
	AddExpectedError(TEXT("is beyond journal head"),
		EAutomationExpectedErrorFlags::Contains, /*Occurrences=*/0, /*IsRegex=*/false);

	const FTerrainPersistIdentity Identity = MakeIdentity();
	FTerrainMemoryStorageDevice Device;

	// --- create ------------------------------------------------------------------------------
	{
		FTerrainWorldStore Store(Device);
		const FTerrainStoreResult Result =
			Store.Create(Base(), Identity.World, Identity.Epoch, 1789412345678LL);
		if (!Result.IsOk())
		{
			AddError(FString::Printf(TEXT("Create failed: %s"), *Result.ToString()));
			return false;
		}

		TestTrue (TEXT("The store is open after Create"), Store.IsOpen());
		TestEqual(TEXT("A fresh world is at G=0"), Store.GetState().Checkpoint.G, (FTerrainOpSeq)0);
		TestFalse(TEXT("and its checkpoint has no root page -- created, never edited"),
			Store.GetState().Checkpoint.bHasRootPage);
		TestEqual(TEXT("Root generation is 1"), Store.GetState().Root.Generation, (uint64)1);
		TestTrue (TEXT("Both root slots are valid"), Store.GetState().bRootRedundancyIntact);
		// The identity's BaseDigest must be the digest of the base descriptor BODY that was
		// actually written -- that is the whole self-referential property P-004 4 relies on.
		{
			TArray<uint8> BaseBody;
			TerrainPersistEncodeBaseDescriptorBody(Base(), BaseBody);
			TestTrue(TEXT("The identity's base digest is the digest of the written base body"),
				Store.GetState().Identity.BaseDigest == TerrainPersistDigest(BaseBody));
		}
		TestNotNull(TEXT("The journal is open"), Store.GetJournal());
		TestEqual(TEXT("at segment 1"), Store.GetJournal()->GetState().ActiveSegmentId, (uint64)1);

		AddInfo(FString::Printf(TEXT("A fresh world is %d files"), Device.NumFiles()));
		TestTrue(TEXT("Creating over an existing world is refused"),
			Store.Create(Base(), Identity.World, Identity.Epoch, 0).Storage
				== ETerrainStorageResult::AlreadyExists);
	}

	// --- reopen: the world that was written is the world that is read ---------------------------
	{
		FTerrainWorldStore Store(Device);
		TestTrue (TEXT("Open succeeds"), Store.Open().IsOk());
		TestEqual(TEXT("Seed survives the round trip"), Store.GetState().Base.Seed, Base().Seed);
		TestEqual(TEXT("Generator name survives"), Store.GetState().Base.GeneratorName, Base().GeneratorName);
		TestEqual(TEXT("Voxel size survives exactly"),
			Store.GetState().Base.VoxelSizeMicrometres, Base().VoxelSizeMicrometres);
		TestEqual(TEXT("G is still 0"), Store.GetState().Checkpoint.G, (FTerrainOpSeq)0);
	}

	// --- a journal record, then a checkpoint that names it -----------------------------------------
	{
		FTerrainWorldStore Store(Device);
		TestTrue(TEXT("Open"), Store.Open().IsOk());

		for (uint32 Index = 1; Index <= 3; ++Index)
		{
			TestTrue(FString::Printf(TEXT("Append %u"), Index),
				Store.GetJournal()->AppendCommit(Commit(Index, Index)).IsOk());
		}
		TestEqual(TEXT("H is 3"), Store.GetJournal()->GetHead(), (FTerrainOpSeq)3);

		FTerrainCheckpointDescriptor Cut;
		Cut.G            = 3;
		Cut.bHasRootPage = true;
		Cut.RootPageLength = 149;
		Cut.RootPageDigest = MakeDigest(0x5A);
		Cut.LeafKeyCount   = 12;

		TestTrue (TEXT("Publishing a checkpoint at G=3 succeeds"),
			Store.PublishCheckpoint(Cut, 1789412355555LL).IsOk());
		TestEqual(TEXT("Root generation advanced to 2"), Store.GetState().Root.Generation, (uint64)2);
		TestEqual(TEXT("and G is 3"), Store.GetState().Root.G, (FTerrainOpSeq)3);

		TestTrue(TEXT("A checkpoint that goes backwards is refused"),
			Store.PublishCheckpoint(FTerrainCheckpointDescriptor(), 0).Format
				== ETerrainPersistError::OrderViolation);
	}

	// --- the published checkpoint survives a reopen ------------------------------------------------
	{
		FTerrainWorldStore Store(Device);
		TestTrue (TEXT("Reopen"), Store.Open().IsOk());
		TestEqual(TEXT("G=3 came back"), Store.GetState().Checkpoint.G, (FTerrainOpSeq)3);
		TestEqual(TEXT("at generation 2"), Store.GetState().Root.Generation, (uint64)2);
		TestTrue (TEXT("with a root page"), Store.GetState().Checkpoint.bHasRootPage);
		TestTrue (TEXT("naming the digest that was published"),
			Store.GetState().Checkpoint.RootPageDigest == MakeDigest(0x5A));
		TestEqual(TEXT("and the journal head is still 3"),
			Store.GetJournal()->GetHead(), (FTerrainOpSeq)3);
		TestTrue (TEXT("Root redundancy is intact"), Store.GetState().bRootRedundancyIntact);
	}

	// --- a failed publication leaves the previous checkpoint current ---------------------------------
	{
		FTerrainFaultDevice Faulty(Device);
		FTerrainWorldStore Store(Faulty);
		TestTrue(TEXT("Open on a faulty device"), Store.Open().IsOk());

		FTerrainCheckpointDescriptor Cut;
		Cut.G            = 3;
		Cut.bHasRootPage = true;
		Cut.RootPageLength = 149;
		Cut.RootPageDigest = MakeDigest(0x6B);

		// Tear the root slot overwrite, leaving a slot whose generation says new and whose
		// content is old -- the case the whole-slot checksum exists for.
		Faulty.TearAfter(ETerrainStorageOp::OverwriteInPlace, 0, /*TearBytes=*/104);
		TestFalse(TEXT("The publication fails"), Store.PublishCheckpoint(Cut, 0).IsOk());

		Faulty.ClearFaults();
		FTerrainWorldStore Reopened(Device);
		TestTrue (TEXT("The world still opens"), Reopened.Open().IsOk());
		TestEqual(TEXT("at the PREVIOUS checkpoint -- a lost checkpoint, not a lost world"),
			Reopened.GetState().Root.Generation, (uint64)2);
		TestEqual(TEXT("still at G=3"), Reopened.GetState().Checkpoint.G, (FTerrainOpSeq)3);
		TestFalse(TEXT("and it knows its root redundancy is broken"),
			Reopened.GetState().bRootRedundancyIntact);

		// Republishing repairs the damaged slot.
		TestTrue (TEXT("Republishing succeeds"), Reopened.PublishCheckpoint(Cut, 0).IsOk());
		FTerrainWorldStore Repaired(Device);
		TestTrue (TEXT("and the world reopens"), Repaired.Open().IsOk());
		TestTrue (TEXT("with redundancy restored"), Repaired.GetState().bRootRedundancyIntact);
	}

	// --- a checkpoint beyond the journal head is corruption --------------------------------------------
	{
		FTerrainMemoryStorageDevice Fresh;
		FTerrainWorldStore Store(Fresh);
		TestTrue(TEXT("Create a second world"),
			Store.Create(Base(), Identity.World, Identity.Epoch, 0).IsOk());

		FTerrainCheckpointDescriptor Ahead;
		Ahead.G = 99;   // the journal holds nothing
		TestTrue(TEXT("Publishing a cut beyond H succeeds locally"),
			Store.PublishCheckpoint(Ahead, 0).IsOk());

		FTerrainWorldStore Reopened(Fresh);
		const FTerrainStoreResult Result = Reopened.Open();
		TestFalse(TEXT("but the world refuses to open"), Result.IsOk());
		TestEqual(TEXT("because the terrain cut records edits the journal does not"),
			Result.Format, ETerrainPersistError::OrderViolation);
	}

	// --- another world's directory is not this world's ---------------------------------------------------
	{
		FTerrainMemoryStorageDevice Fresh;
		FTerrainWorldStore Store(Fresh);
		FTerrainWorldId OtherWorld = Identity.World;
		OtherWorld.Bytes[0] ^= 0xFF;
		TestTrue(TEXT("Create a world with a different WorldId"),
			Store.Create(Base(), OtherWorld, Identity.Epoch, 0).IsOk());

		// Cross-wiring: put this world's base descriptor in front of the other world's roots.
		TArray<uint8> ForeignBase;
		Fresh.Read(TerrainStoragePaths::BaseDescriptor, ForeignBase);

		TArray<uint8> OurBase;
		Device.Read(TerrainStoragePaths::BaseDescriptor, OurBase);
		Fresh.Delete(TerrainStoragePaths::BaseDescriptor);
		Fresh.WriteNew(TerrainStoragePaths::BaseDescriptor, OurBase);

		FTerrainWorldStore Crossed(Fresh);
		const FTerrainStoreResult Result = Crossed.Open();
		TestFalse(TEXT("A world whose base does not match its roots refuses to open"), Result.IsOk());
		AddInfo(FString::Printf(TEXT("Cross-wired world rejected with %s"), *Result.ToString()));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

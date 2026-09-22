// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "TerrainCheckpoint.h"
#include "TerrainCommitJournal.h"
#include "TerrainJournalReplay.h"
#include "TerrainPersistenceIndex.h"
#include "MemoryTerrainBackend.h"
#include "ITerrainDensityField.h"
#include "TerrainChunk.h"
#include "TerrainStorage.h"
#include "TerrainPersistenceFixtures.h"

/**
 * TerrainCore.Persistence.CrashMatrix -- crash at every write, recover, prove it landed on a
 * real moment in history (P-003 §8; named evidence for `Restart.CrashMatrix`).
 *
 * WHAT THE OTHER TESTS DO NOT COVER. `Storage.SlotPair` tears a root slot, `Storage.Container`
 * tears a frame, `Persistence.Retention` interrupts a compaction. Each is a case somebody thought of.
 * P-003 §8 asks for something stronger -- *"before/after apply, append/flush, every
 * payload/page/descriptor write/flush, root overwrite/flush, segment discovery/rotation and
 * every deletion"* -- and a handful of hand-picked cases cannot be that, because the write
 * nobody thought to break is exactly the one that breaks.
 *
 * So this runs one scripted session and then runs it again once per mutating write, failing
 * that write and only that write, walking the whole sequence index by index. Both a hard
 * failure and a torn (half-written) failure at each point.
 *
 * WHAT IT ASSERTS, and why this is the only assertion worth making. It is not enough that
 * recovery "works" -- a world that comes back holding a state it was never in is worse than one
 * that refuses to come back at all, because nobody finds out. So the reference run records the
 * exact terrain after **every** committed operation, and after each injected crash the recovered
 * world must equal the reference at an operation sequence inside a window:
 *
 *   - **never below the acknowledged head.** An operation whose record the journal reported
 *     durable was acknowledged; losing it is losing history a player was told happened. (Codex
 *     review F5: this bound was missing, so a recovery that returned an earlier, internally
 *     consistent world would have passed.)
 *   - **at most one above it,** and only when the session's last append FAILED. P-003 §2 makes a
 *     complete valid record recovery authority even when the flush response was lost, so that
 *     one uncertain record may or may not come back. Nothing else may.
 *   - recovery may refuse to open only while the world does not yet exist;
 *   - recovery may **not** land between two operations or on terrain that never existed. The
 *     assertion is an exact chunk-hash comparison rather than "it opened".
 *
 * Three fault modes at every write: refused outright, torn to a 64-byte prefix, and written in
 * full with failure reported -- the last is the lost-acknowledgement case, and the only one in
 * which the uncertain record can survive.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainCrashMatrixTest, "TerrainCore.Persistence.CrashMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

namespace
{
	class FCrashField final : public ITerrainDensityField
	{
	public:
		virtual FTerrainDensitySample Sample(FIntVector Position) const override
		{
			FTerrainDensitySample Out;
			Out.Density    = Position.Z < 0 ? -1.f : 1.f;
			Out.MaterialId = Position.Z < -8 ? 5 : (Position.Z < 0 ? 4 : 1);
			return Out;
		}
	};

	FTerrainOp CrashDig(const FIntVector& Centre, int32 RadiusVox, FTerrainOpSeq OpSeq)
	{
		FTerrainOp Op;
		Op.Kind         = ETerrainOpKind::Remove;
		Op.Shape        = ETerrainShape::Sphere;
		Op.Source       = ETerrainSource::Player;
		Op.SourceId     = 2;
		Op.CentreVox    = Centre;
		Op.RadiusVoxQ16 = RadiusVox << 16;
		Op.OpSeq        = OpSeq;
		return Op;
	}

	/** The scripted session: nine edits with a checkpoint after the third and the seventh. */
	constexpr int32 CrashOpCount = 9;
	constexpr int32 CrashCutAfter[2] = {3, 7};

	FTerrainOp CrashScript(int32 Index)
	{
		static const FIntVector Centres[CrashOpCount] = {
			FIntVector(  0,  0, -4), FIntVector( 34,  0, -4), FIntVector(  0, 34, -4),
			FIntVector(  2,  2, -5), FIntVector( 36,  2, -5), FIntVector(  2, 36, -5),
			FIntVector(-34,  0, -4), FIntVector(  0,-34, -4), FIntVector(  4,  4, -6),
		};
		return CrashDig(Centres[Index], 3, static_cast<FTerrainOpSeq>(Index + 1));
	}
}

bool FTerrainCrashMatrixTest::RunTest(const FString& Parameters)
{
	using namespace TerrainPersistTest;

	// Injected failures log loudly on purpose; they are the subject of this test, not a defect.
	// Declared by their exact text so that an expectation which stops matching is a failure
	// rather than a silent licence to log anything.
	AddExpectedError(TEXT("could not be recorded"),
		EAutomationExpectedErrorFlags::Contains, /*Occurrences=*/0, /*IsRegex=*/false);
	AddExpectedError(TEXT("The segment's tail is now uncertain"),
		EAutomationExpectedErrorFlags::Contains, /*Occurrences=*/0, /*IsRegex=*/false);
	// A crash during root publication. That this is survivable -- "the previous root is intact
	// and remains current" -- is one of the properties the matrix is here to prove.
	AddExpectedError(TEXT("publishing root generation"),
		EAutomationExpectedErrorFlags::Contains, /*Occurrences=*/0, /*IsRegex=*/false);

	const FTerrainPersistIdentity Identity = MakeIdentity();

	TSharedPtr<FCrashField, ESPMode::ThreadSafe> Field =
		MakeShared<FCrashField, ESPMode::ThreadSafe>();

	FTerrainBackendInit Init;
	Init.Seed              = 0;
	Init.GeneratorVersion  = 7;
	Init.VoxelSizeCm       = 50.f;
	Init.WorldBoundsVox    = FTerrainBox(FIntVector(-256,-256,-256), FIntVector(256,256,256));
	Init.DensityField      = Field.Get();
	Init.DensityFieldOwner = Field;
	Init.Role              = ETerrainRole::Server;

	FTerrainBaseDescriptor Base = MakeBaseDescriptor();
	Base.GeneratorVersion          = 7;
	Base.VoxelSizeMicrometres      = 500000;
	Base.WorldBoundsVox            = Init.WorldBoundsVox;
	Base.OriginWorldMicrometres[0] = 0;
	Base.OriginWorldMicrometres[1] = 0;
	Base.OriginWorldMicrometres[2] = 0;
	Base.ValueConfig               = 0;

	auto AddInterest = [](ITerrainBackend& Backend)
	{
		FTerrainStreamingInterest Interest;
		Interest.InterestId    = 1;
		Interest.WorldLocation = FVector::ZeroVector;
		Interest.RadiusCm      = 6000.0;
		Interest.bCollision    = true;
		Backend.SetStreamingInterest(Interest);
	};

	// --- the reference history: terrain after every committed operation ----------------------
	// Indexed by OpSeq, 0 = the untouched generated world. A recovered world must equal one of
	// these exactly; anything else is a state that never existed.
	TSet<FTerrainChunkKey> AllChunks;
	TArray<TMap<FTerrainChunkKey, uint64>> Reference;
	{
		FMemoryTerrainBackend Backend;
		Backend.Initialize(Init);
		AddInterest(Backend);

		FTerrainRevisionIndex Revisions;
		for (int32 Index = 0; Index < CrashOpCount; ++Index)
		{
			FTerrainEditResult Result;
			if (!Backend.ApplyOp(CrashScript(Index), Result))
			{
				AddError(TEXT("Reference run: backend refused an op"));
				return false;
			}
			Revisions.TryBumpRevisions(Result.AffectedChunks);
			for (const FTerrainChunkKey& Key : Result.AffectedChunks) { AllChunks.Add(Key); }
		}

		// Replay from scratch, snapshotting after each op, now that every touched chunk is known.
		FMemoryTerrainBackend Walk;
		Walk.Initialize(Init);
		AddInterest(Walk);

		auto Snapshot = [&]()
		{
			TMap<FTerrainChunkKey, uint64> Hashes;
			for (const FTerrainChunkKey& Key : AllChunks) { Hashes.Add(Key, Walk.HashRegion(Key)); }
			Reference.Add(MoveTemp(Hashes));
		};
		Snapshot();   // OpSeq 0
		for (int32 Index = 0; Index < CrashOpCount; ++Index)
		{
			FTerrainEditResult Result;
			Walk.ApplyOp(CrashScript(Index), Result);
			Snapshot();
		}
	}
	TestEqual(TEXT("The reference history has a state per operation"),
		Reference.Num(), CrashOpCount + 1);

	/** What a session was told: how many commits were acknowledged, and whether one more is in doubt. */
	struct FSessionOutcome
	{
		int32 Acknowledged = 0;
		bool  bUncertainAppend = false;   // the last RecordCommit failed; its record may be complete
	};

	/**
	 * Runs the scripted session against a device, stopping at the first durable failure.
	 *
	 * Mirrors the live commit ordering: apply, then record, and **stop** if the record cannot
	 * be made durable, because the service closes admission there rather than broadcasting an
	 * edit nobody wrote down.
	 */
	auto RunSession = [&](FTerrainFaultDevice& Device, int32* OutCreationWrites = nullptr) -> FSessionOutcome
	{
		FSessionOutcome Outcome;
		FTerrainWorldStore Store(Device);
		const bool bCreated = Store.Create(Base, Identity.World, Identity.Epoch, 1789412345678LL).IsOk();
		if (OutCreationWrites != nullptr) { *OutCreationWrites = Device.MutationCount(); }
		if (!bCreated)
		{
			return Outcome;
		}

		FTerrainWorldStoreJournal Journal(Store);
		FMemoryTerrainBackend Backend;
		Backend.Initialize(Init);
		AddInterest(Backend);

		FTerrainRevisionIndex Revisions;
		TMap<FTerrainChunkKey, FTerrainOpSeq> Dirty;

		for (int32 Index = 0; Index < CrashOpCount; ++Index)
		{
			const FTerrainOp Op = CrashScript(Index);
			FTerrainEditResult Result;
			if (!Backend.ApplyOp(Op, Result)) { break; }

			TArray<FTerrainChunkRevision> Changed;
			for (const FTerrainChunkKey& Key : Result.AffectedChunks)
			{
				FTerrainChunkRevision Revision;
				Revision.Key    = FIntVector(Key.X, Key.Y, Key.Z);
				Revision.Before = Revisions.GetRevision(Key);
				Changed.Add(Revision);
			}
			if (!Revisions.TryBumpRevisions(Result.AffectedChunks)) { break; }
			for (FTerrainChunkRevision& Revision : Changed)
			{
				Revision.After = Revisions.GetRevision(
					FTerrainChunkKey(Revision.Key.X, Revision.Key.Y, Revision.Key.Z));
			}

			FTerrainCommitIdentity CommitIdentity;
			CommitIdentity.RequestId  = uint32(Index + 1);
			CommitIdentity.ChildCount = 1;
			if (!Journal.RecordCommit(Op, Result, CommitIdentity, Changed))
			{
				// Not acknowledged, and nothing after it runs. But "the append failed" is not
				// "the record is absent": its bytes may all be on disk.
				Outcome.bUncertainAppend = true;
				break;
			}
			for (const FTerrainChunkKey& Key : Result.AffectedChunks) { Dirty.Add(Key, Op.OpSeq); }
			++Outcome.Acknowledged;

			for (const int32 Cut : CrashCutAfter)
			{
				if (Index + 1 != Cut) { continue; }
				FTerrainCheckpointStats Stats;
				const FTerrainStoreResult Captured = TerrainCaptureCheckpoint(
					Store, Backend, Revisions, Dirty, static_cast<FTerrainOpSeq>(Index + 1),
					1789412355555LL, Stats);
				if (Captured.IsOk()) { Dirty.Reset(); }
				// A failed capture is survivable and deliberately does NOT stop the session:
				// the previous root and the journal are both intact, so the world simply has
				// more to replay. That is the ruling in D-033 and this exercises it.
			}
		}
		return Outcome;
	};

	/** Reopens, restores, replays, and returns the recovered head -- or -1 if it refused. */
	auto Recover = [&](ITerrainStorageDevice& Device, TMap<FTerrainChunkKey, uint64>& OutHashes) -> int64
	{
		FTerrainWorldStore Store(Device);
		if (!Store.Open().IsOk()) { return -1; }

		FMemoryTerrainBackend Backend;
		Backend.Initialize(Init);
		AddInterest(Backend);

		FTerrainRevisionIndex Revisions;
		FTerrainRestoreStats RestoreStats;
		if (!TerrainRestoreCheckpoint(Store, Backend, Revisions, RestoreStats).IsOk()) { return -1; }

		FTerrainReplayStats ReplayStats;
		if (!TerrainReplayJournal(Store, Backend, Revisions, ReplayStats).IsOk()) { return -1; }

		OutHashes.Reset();
		for (const FTerrainChunkKey& Key : AllChunks) { OutHashes.Add(Key, Backend.HashRegion(Key)); }

		// The recovered head is whichever reaches further: the cut the checkpoint restored, or
		// the last record the journal still held. A checkpoint can be ahead of a truncated
		// journal, and a journal is usually ahead of an older cut.
		return static_cast<int64>(FMath::Max(
			Store.GetState().Checkpoint.G, ReplayStats.LastOpSeq));
	};

	/**
	 * The oracle. Empty when the recovered world is legal for what the session was told; the
	 * reason otherwise. One function, so the matrix and its negative control judge identically.
	 */
	auto Judge = [&](int64 Head, const TMap<FTerrainChunkKey, uint64>& Hashes, const FSessionOutcome& Session) -> FString
	{
		if (Head > CrashOpCount)
		{
			return FString::Printf(TEXT("recovered at OpSeq %lld, past the end of history"), Head);
		}
		if (Head < Session.Acknowledged)
		{
			return FString::Printf(TEXT("recovered at OpSeq %lld but %d operations were acknowledged. ")
				TEXT("Acknowledged history was lost."), Head, Session.Acknowledged);
		}
		const int64 Ceiling = Session.Acknowledged + (Session.bUncertainAppend ? 1 : 0);
		if (Head > Ceiling)
		{
			return FString::Printf(TEXT("recovered at OpSeq %lld but only %d were acknowledged and %s. ")
				TEXT("An edit came back that was never written down."), Head, Session.Acknowledged,
				Session.bUncertainAppend ? TEXT("one more was in doubt") : TEXT("none was in doubt"));
		}
		if (!Hashes.OrderIndependentCompareEqual(Reference[static_cast<int32>(Head)]))
		{
			return FString::Printf(TEXT("recovered at OpSeq %lld, but the terrain does not match the ")
				TEXT("world at OpSeq %lld. Recovery landed on a state that never existed."), Head, Head);
		}
		return FString();
	};

	// --- the clean run, which also counts the writes the matrix will walk --------------------
	int32 MutationCount = 0;
	int32 CreationWrites = 0;   // writes consumed before the world exists at all
	{
		FTerrainMemoryStorageDevice Memory;
		FTerrainFaultDevice Device(Memory);

		const FSessionOutcome Clean = RunSession(Device, &CreationWrites);
		TestEqual(TEXT("With no faults every operation commits"), Clean.Acknowledged, CrashOpCount);
		TestFalse(TEXT("and none is in doubt"), Clean.bUncertainAppend);

		MutationCount = Device.MutationCount();
		// Nine edits, two checkpoints, and the world's own creation. The count is small because
		// packs collapse a capture's payloads, index pages and descriptor into ONE write
		// (P-004 §13) -- before packs this same session would have had well over a hundred
		// crash points. Fewer points, each carrying far more.
		TestTrue(TEXT("and the session performed enough writes to be worth walking"),
			MutationCount >= CrashOpCount + 4);

		TMap<FTerrainChunkKey, uint64> Hashes;
		const int64 Head = Recover(Device, Hashes);
		TestEqual(TEXT("A clean session recovers at the last operation"),
			Head, (int64)CrashOpCount);
		TestTrue(TEXT("and its terrain is the reference history's last state"),
			Hashes.OrderIndependentCompareEqual(Reference[CrashOpCount]));
	}

	// --- the negative control: the oracle must reject lost acknowledged history ---------------
	// A complete session whose journal then loses its last byte: the torn-tail rule drops record
	// 9, leaving a valid, internally consistent world at OpSeq 8. The session was told 9. The old
	// oracle (no lower bound) accepted exactly this; the new one must not.
	{
		FTerrainMemoryStorageDevice Memory;
		FTerrainFaultDevice Device(Memory);
		const FSessionOutcome Session = RunSession(Device);
		const FString Segment = TerrainStoragePaths::JournalSegment(1);
		const int64 Size = Memory.Size(Segment);
		TestTrue(TEXT("Negative control: the journal can be shortened"),
			Size > 1 && Memory.Truncate(Segment, Size - 1) == ETerrainStorageResult::Ok);

		TMap<FTerrainChunkKey, uint64> Hashes;
		const int64 Head = Recover(Device, Hashes);
		TestEqual(TEXT("Negative control: recovery drops the damaged last record"), Head, (int64)CrashOpCount - 1);
		TestTrue(TEXT("and lands on a real, consistent state -- which is why only a lower bound catches it"),
			Head >= 0 && Hashes.OrderIndependentCompareEqual(Reference[static_cast<int32>(Head)]));
		TestFalse(TEXT("The oracle rejects losing acknowledged history"), Judge(Head, Hashes, Session).IsEmpty());
	}

	AddInfo(FString::Printf(
		TEXT("Crash matrix: %d mutating writes over %d edits and 2 checkpoints, walked three times ")
		TEXT("(refused, torn, and written in full with failure reported). Packs keep the count low: ")
		TEXT("one append per capture rather than one write per payload, page and descriptor."),
		MutationCount, CrashOpCount));

	// --- the matrix -------------------------------------------------------------------------
	int32 Refusals = 0, Recovered = 0, LostTail = 0, UncertainKept = 0, UncertainDropped = 0;
	for (int32 Mutation = 0; Mutation < MutationCount; ++Mutation)
	{
		for (int32 Mode = 0; Mode < 3; ++Mode)
		{
			FTerrainMemoryStorageDevice Memory;
			FTerrainFaultDevice Device(Memory);
			// Mode 0 refuses the write outright; mode 1 writes 64 bytes of it and then reports
			// failure, which a "return IoError" fake would never produce; mode 2 writes ALL of it
			// and then reports failure -- a lost acknowledgement, P-003 §2's uncertain record.
			Device.FailAtMutation(Mutation, Mode == 0 ? -1 : (Mode == 1 ? 64 : MAX_int32));

			const FSessionOutcome Session = RunSession(Device);

			TMap<FTerrainChunkKey, uint64> Hashes;
			const int64 Head = Recover(Device, Hashes);

			if (Head < 0)
			{
				// A refusal is valid only while the world does not yet exist. Crashing partway
				// through creation leaves no world to open, which costs nothing because there
				// was nothing there. **Once creation has completed, no single crash may make
				// the world unopenable** -- that is the whole promise of the two-slot root, the
				// torn-tail rule and unreferenced-garbage containment, and if it fails here the
				// protocol is wrong rather than the test.
				if (Mutation >= CreationWrites)
				{
					AddError(FString::Printf(
						TEXT("Mutation %d mode %d: the world refused to open after a crash at ")
						TEXT("write %d, which is past creation (%d writes). An established world ")
						TEXT("must survive any single crash."),
						Mutation, Mode, Mutation, CreationWrites));
					return false;
				}
				++Refusals;
				continue;
			}

			const FString Wrong = Judge(Head, Hashes, Session);
			if (!Wrong.IsEmpty())
			{
				AddError(FString::Printf(TEXT("Mutation %d mode %d: %s"), Mutation, Mode, *Wrong));
				return false;
			}

			++Recovered;
			if (Head < CrashOpCount) { ++LostTail; }
			if (Session.bUncertainAppend)
			{
				++(Head > Session.Acknowledged ? UncertainKept : UncertainDropped);
			}
		}
	}

	AddInfo(FString::Printf(
		TEXT("Crash matrix: %d of %d injections recovered to an exact point in real history, never ")
		TEXT("below the acknowledged head (%d short of the full session). Of the failed appends, %d ")
		TEXT("came back complete and %d did not. The other %d refused to open, and ALL of those were ")
		TEXT("crashes during world creation (the first %d writes), when no world existed yet."),
		Recovered, Recovered + Refusals, LostTail, UncertainKept, UncertainDropped, Refusals, CreationWrites));

	// The matrix proves nothing if every injection happened to be survivable in the same way.
	TestTrue(TEXT("Some crashes were recovered from"), Recovered > 0);
	TestTrue(TEXT("and some cost the unacknowledged tail -- otherwise no write was load-bearing"), LostTail > 0);
	TestTrue(TEXT("A fully written, failed append DID come back -- the lost-acknowledgement case ran"), UncertainKept > 0);
	TestTrue(TEXT("and a torn or refused one did not"), UncertainDropped > 0);
	TestTrue(TEXT("Every refusal happened during world creation, none after it"),
		Refusals <= CreationWrites * 3);

	// --- repeat recovery: opening twice must not change the answer (P-003 §8) ----------------
	{
		FTerrainMemoryStorageDevice Memory;
		FTerrainFaultDevice Device(Memory);
		Device.FailAtMutation(MutationCount / 2);
		RunSession(Device);

		TMap<FTerrainChunkKey, uint64> First, Second;
		const int64 HeadA = Recover(Device, First);
		const int64 HeadB = Recover(Device, Second);
		TestEqual(TEXT("Recovering twice reaches the same head"), HeadB, HeadA);
		if (HeadA >= 0)
		{
			TestTrue(TEXT("and the same terrain -- recovery does not mutate what it recovers"),
				First.OrderIndependentCompareEqual(Second));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

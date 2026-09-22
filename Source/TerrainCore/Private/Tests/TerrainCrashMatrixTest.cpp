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
 * Four fault modes at every write: refused outright, torn to a 64-byte prefix, torn to 300 bytes,
 * and written in full with failure reported -- the last is the lost-acknowledgement case, and the
 * only one in which a whole uncertain append can survive.
 *
 * **Two sessions (P-012).** One records each edit with its own append; the other is group commit,
 * three records per append. In the batched session a failed append puts all three in doubt, and
 * a tear at 300 bytes lands inside the batch, after its first record: recovery must then keep
 * that record and drop the rest, which is the torn multi-record append this matrix must cover.
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

	/**
	 * The scripted session: nine edits, with checkpoints after the third and the seventh. The
	 * batched session (group commit, P-012) flushes three records per append, and cuts at batch
	 * boundaries, after the third and the sixth.
	 */
	constexpr int32 CrashOpCount = 9;
	constexpr int32 CrashCutAfter[2] = {3, 7};
	constexpr int32 CrashCutAfterBatched[2] = {3, 6};
	constexpr int32 CrashBatch = 3;

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
		int32 InDoubt = 0;   // records in the append that failed: any prefix of them may be on disk
	};

	/**
	 * Runs the scripted session against a device, stopping at the first durable failure.
	 *
	 * Mirrors the live commit ordering: apply, then record, and **stop** if the record cannot
	 * be made durable, because the service closes admission there rather than broadcasting an
	 * edit nobody wrote down.
	 */
	auto RunSession = [&](FTerrainFaultDevice& Device, int32 BatchSize, TConstArrayView<int32> Cuts,
	                      int32* OutCreationWrites = nullptr) -> FSessionOutcome
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
		TArray<TPair<FTerrainChunkKey, FTerrainOpSeq>> StagedDirty;
		int32 Staged = 0;

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
			if (BatchSize <= 1)
			{
				if (!Journal.RecordCommit(Op, Result, CommitIdentity, Changed))
				{
					// Not acknowledged, and nothing after it runs. But "the append failed" is not
					// "the record is absent": its bytes may all be on disk.
					Outcome.InDoubt = 1;
					break;
				}
				for (const FTerrainChunkKey& Key : Result.AffectedChunks) { Dirty.Add(Key, Op.OpSeq); }
				++Outcome.Acknowledged;
			}
			else
			{
				// Group commit: stage, and flush once per batch. Nothing staged is acknowledged
				// until its batch's flush succeeds.
				if (!Journal.StageCommit(Op, Result, CommitIdentity, Changed)) { break; }
				for (const FTerrainChunkKey& Key : Result.AffectedChunks) { StagedDirty.Emplace(Key, Op.OpSeq); }
				++Staged;
				if (Staged < BatchSize && Index + 1 < CrashOpCount) { continue; }
				if (!Journal.FlushStaged())
				{
					Outcome.InDoubt = Staged;   // the whole batch: any prefix of it may have landed
					break;
				}
				for (const TPair<FTerrainChunkKey, FTerrainOpSeq>& Entry : StagedDirty) { Dirty.Add(Entry.Key, Entry.Value); }
				StagedDirty.Reset();
				Outcome.Acknowledged += Staged;
				Staged = 0;
			}

			for (const int32 Cut : Cuts)
			{
				if (Outcome.Acknowledged != Cut || Index + 1 != Cut) { continue; }
				FTerrainCheckpointStats Stats;
				const FTerrainStoreResult Captured = TerrainCaptureCheckpoint(
					Store, Backend, Revisions, Dirty, static_cast<FTerrainOpSeq>(Cut),
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
		const int64 Ceiling = Session.Acknowledged + Session.InDoubt;
		if (Head > Ceiling)
		{
			return FString::Printf(TEXT("recovered at OpSeq %lld but only %d were acknowledged and %d more ")
				TEXT("were in doubt. An edit came back that was never written down."), Head,
				Session.Acknowledged, Session.InDoubt);
		}
		if (!Hashes.OrderIndependentCompareEqual(Reference[static_cast<int32>(Head)]))
		{
			return FString::Printf(TEXT("recovered at OpSeq %lld, but the terrain does not match the ")
				TEXT("world at OpSeq %lld. Recovery landed on a state that never existed."), Head, Head);
		}
		return FString();
	};

	// --- the negative control: the oracle must reject lost acknowledged history ---------------
	// A complete session whose journal then loses its last byte: the torn-tail rule drops record
	// 9, leaving a valid, internally consistent world at OpSeq 8. The session was told 9. The old
	// oracle (no lower bound) accepted exactly this; the new one must not.
	{
		FTerrainMemoryStorageDevice Memory;
		FTerrainFaultDevice Device(Memory);
		const FSessionOutcome Session = RunSession(Device, 1, CrashCutAfter);
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

	int32 MutationCount = 0;    // of the unbatched session, for the repeat-recovery check below
	for (const int32 BatchSize : {1, CrashBatch})
	{
		const TConstArrayView<int32> Cuts = BatchSize == 1
			? TConstArrayView<int32>(CrashCutAfter) : TConstArrayView<int32>(CrashCutAfterBatched);
		const TCHAR* Label = BatchSize == 1 ? TEXT("one record per append") : TEXT("group commit, 3 per append");

		// --- the clean run, which also counts the writes the matrix will walk ----------------
		int32 Mutations = 0;
		int32 CreationWrites = 0;   // writes consumed before the world exists at all
		{
			FTerrainMemoryStorageDevice Memory;
			FTerrainFaultDevice Device(Memory);

			const FSessionOutcome Clean = RunSession(Device, BatchSize, Cuts, &CreationWrites);
			TestEqual(FString::Printf(TEXT("%s: with no faults every operation commits"), Label), Clean.Acknowledged, CrashOpCount);
			TestEqual(FString::Printf(TEXT("%s: and none is in doubt"), Label), Clean.InDoubt, 0);

			Mutations = Device.MutationCount();
			// Nine edits, two checkpoints, and the world's own creation. The count is small because
			// packs collapse a capture's payloads, index pages and descriptor into ONE write
			// (P-004 §13), and group commit collapses a batch's records into one append.
			TestTrue(FString::Printf(TEXT("%s: the session performed enough writes to be worth walking"), Label),
				Mutations >= CrashOpCount / BatchSize + 4);

			TMap<FTerrainChunkKey, uint64> Hashes;
			const int64 Head = Recover(Device, Hashes);
			TestEqual(FString::Printf(TEXT("%s: a clean session recovers at the last operation"), Label),
				Head, (int64)CrashOpCount);
			TestTrue(FString::Printf(TEXT("%s: and its terrain is the reference history's last state"), Label),
				Hashes.OrderIndependentCompareEqual(Reference[CrashOpCount]));
		}
		if (BatchSize == 1) { MutationCount = Mutations; }

		// --- the matrix ---------------------------------------------------------------------
		constexpr int32 Modes = 4;
		int32 Refusals = 0, Recovered = 0, LostTail = 0, UncertainKept = 0, UncertainDropped = 0, PartialBatch = 0;
		for (int32 Mutation = 0; Mutation < Mutations; ++Mutation)
		{
			for (int32 Mode = 0; Mode < Modes; ++Mode)
			{
				FTerrainMemoryStorageDevice Memory;
				FTerrainFaultDevice Device(Memory);
				// Mode 0 refuses the write outright; modes 1 and 2 write 64 or 300 bytes of it and
				// then report failure, which a "return IoError" fake would never produce (300 lands
				// inside a batch, after its first record); mode 3 writes ALL of it and then reports
				// failure -- a lost acknowledgement, P-003 §2's uncertain record.
				static constexpr int32 Tear[Modes] = {-1, 64, 300, MAX_int32};
				Device.FailAtMutation(Mutation, Tear[Mode]);

				const FSessionOutcome Session = RunSession(Device, BatchSize, Cuts);

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
							TEXT("%s, mutation %d mode %d: the world refused to open after a crash at ")
							TEXT("write %d, which is past creation (%d writes). An established world ")
							TEXT("must survive any single crash."),
							Label, Mutation, Mode, Mutation, CreationWrites));
						return false;
					}
					++Refusals;
					continue;
				}

				const FString Wrong = Judge(Head, Hashes, Session);
				if (!Wrong.IsEmpty())
				{
					AddError(FString::Printf(TEXT("%s, mutation %d mode %d: %s"), Label, Mutation, Mode, *Wrong));
					return false;
				}

				++Recovered;
				if (Head < CrashOpCount) { ++LostTail; }
				if (Session.InDoubt > 0)
				{
					++(Head > Session.Acknowledged ? UncertainKept : UncertainDropped);
					if (Head > Session.Acknowledged && Head < Session.Acknowledged + Session.InDoubt) { ++PartialBatch; }
				}
			}
		}

		AddInfo(FString::Printf(
			TEXT("Crash matrix, %s: %d mutating writes over %d edits and 2 checkpoints, 4 fault modes. ")
			TEXT("%d of %d injections recovered to an exact point in real history, never below the ")
			TEXT("acknowledged head (%d short of the full session). Of the failed appends, %d came back ")
			TEXT("wholly or partly (%d as a strict prefix of their batch) and %d did not. The other %d ")
			TEXT("refused to open, and ALL of those were crashes during world creation (the first %d ")
			TEXT("writes), when no world existed yet."),
			Label, Mutations, CrashOpCount, Recovered, Recovered + Refusals, LostTail, UncertainKept,
			PartialBatch, UncertainDropped, Refusals, CreationWrites));

		// The matrix proves nothing if every injection happened to be survivable in the same way.
		TestTrue(FString::Printf(TEXT("%s: some crashes were recovered from"), Label), Recovered > 0);
		TestTrue(FString::Printf(TEXT("%s: and some cost the unacknowledged tail -- otherwise no write was load-bearing"), Label), LostTail > 0);
		TestTrue(FString::Printf(TEXT("%s: a fully written, failed append DID come back -- the lost-acknowledgement case ran"), Label), UncertainKept > 0);
		TestTrue(FString::Printf(TEXT("%s: and a torn or refused one did not"), Label), UncertainDropped > 0);
		if (BatchSize > 1)
		{
			TestTrue(TEXT("Group commit: a torn batch recovered as a strict prefix of itself -- the torn multi-record case ran"),
				PartialBatch > 0);
		}
		TestTrue(FString::Printf(TEXT("%s: every refusal happened during world creation, none after it"), Label),
			Refusals <= CreationWrites * Modes);
	}

	// --- repeat recovery: opening twice must not change the answer (P-003 §8) ----------------
	{
		FTerrainMemoryStorageDevice Memory;
		FTerrainFaultDevice Device(Memory);
		Device.FailAtMutation(MutationCount / 2);
		RunSession(Device, 1, CrashCutAfter);

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

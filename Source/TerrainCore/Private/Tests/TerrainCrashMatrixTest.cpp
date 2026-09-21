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
#include "TerrainPersistenceFixtures.h"

/**
 * TerrainCore.Persistence.CrashMatrix -- crash at every write, recover, prove it landed on a
 * real moment in history (P-003 §8; named evidence for `Restart.CrashMatrix`).
 *
 * WHAT THE OTHER TESTS DO NOT COVER. `Storage.SlotPair` tears a root slot, `Storage.Pack` tears
 * a pack, `Persistence.Retention` interrupts a compaction. Each is a case somebody thought of.
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
 * world must equal the reference at **some** operation sequence:
 *
 *   - recovery may lose the tail -- a crash before a record was durable means the edit never
 *     happened, which is correct and is what the commit ordering promises;
 *   - recovery may lose nothing -- the crash landed after the last durable write;
 *   - recovery may refuse to open, when the crash destroyed something no protocol can repair,
 *     and that refusal is a valid outcome provided it is a refusal and not a bad open;
 *   - recovery may **not** land between two operations, ahead of the journal, or on terrain
 *     that never existed. That is the failure this test exists to catch, and the assertion is
 *     an exact chunk-hash comparison rather than "it opened".
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

	/**
	 * Runs the scripted session against a device, stopping at the first durable failure.
	 *
	 * Mirrors the live commit ordering: apply, then record, and **stop** if the record cannot
	 * be made durable, because the service closes admission there rather than broadcasting an
	 * edit nobody wrote down. Returns the number of operations that were durably committed.
	 */
	auto RunSession = [&](FTerrainFaultDevice& Device, int32* OutCreationWrites = nullptr) -> int32
	{
		FTerrainWorldStore Store(Device);
		const bool bCreated = Store.Create(Base, Identity.World, Identity.Epoch, 1789412345678LL).IsOk();
		if (OutCreationWrites != nullptr) { *OutCreationWrites = Device.MutationCount(); }
		if (!bCreated)
		{
			return 0;
		}

		FTerrainWorldStoreJournal Journal(Store);
		FMemoryTerrainBackend Backend;
		Backend.Initialize(Init);
		AddInterest(Backend);

		FTerrainRevisionIndex Revisions;
		TMap<FTerrainChunkKey, FTerrainOpSeq> Dirty;
		int32 Committed = 0;

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
				break;   // not durable: the edit did not happen, and nothing after it does either
			}
			for (const FTerrainChunkKey& Key : Result.AffectedChunks) { Dirty.Add(Key, Op.OpSeq); }
			++Committed;

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
		return Committed;
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

	// --- the clean run, which also counts the writes the matrix will walk --------------------
	int32 MutationCount = 0;
	int32 CreationWrites = 0;   // writes consumed before the world exists at all
	{
		FTerrainMemoryStorageDevice Memory;
		FTerrainFaultDevice Device(Memory);

		const int32 Committed = RunSession(Device, &CreationWrites);
		TestEqual(TEXT("With no faults every operation commits"), Committed, CrashOpCount);

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

	AddInfo(FString::Printf(
		TEXT("Crash matrix: %d mutating writes over %d edits and 2 checkpoints, walked twice ")
		TEXT("(hard failure and torn write). Packs keep the count low: one write per capture ")
		TEXT("rather than one per payload, page and descriptor."),
		MutationCount, CrashOpCount));

	// --- the matrix -------------------------------------------------------------------------
	int32 Refusals = 0, Recovered = 0, LostTail = 0;
	for (int32 Mutation = 0; Mutation < MutationCount; ++Mutation)
	{
		for (int32 Mode = 0; Mode < 2; ++Mode)
		{
			FTerrainMemoryStorageDevice Memory;
			FTerrainFaultDevice Device(Memory);
			// Mode 0 refuses the write outright; mode 1 writes half of it and then reports
			// failure, which is the case a "return IoError" fake would never produce.
			Device.FailAtMutation(Mutation, Mode == 0 ? -1 : 64);

			const int32 Committed = RunSession(Device);

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

			if (Head > CrashOpCount)
			{
				AddError(FString::Printf(
					TEXT("Mutation %d mode %d: recovered at OpSeq %lld, past the end of history."),
					Mutation, Mode, Head));
				return false;
			}
			if (Head > Committed)
			{
				AddError(FString::Printf(
					TEXT("Mutation %d mode %d: recovered at OpSeq %lld but only %d operations were ")
					TEXT("durably committed. An edit came back that was never written down."),
					Mutation, Mode, Head, Committed));
				return false;
			}

			if (!Hashes.OrderIndependentCompareEqual(Reference[static_cast<int32>(Head)]))
			{
				AddError(FString::Printf(
					TEXT("Mutation %d mode %d: recovered at OpSeq %lld, but the terrain does not ")
					TEXT("match the world at OpSeq %lld. Recovery landed on a state that never ")
					TEXT("existed."),
					Mutation, Mode, Head, Head));
				return false;
			}

			++Recovered;
			if (Head < CrashOpCount) { ++LostTail; }
		}
	}

	AddInfo(FString::Printf(
		TEXT("Crash matrix: %d of %d injections recovered to an exact point in real history ")
		TEXT("(%d with a truncated tail); the other %d refused to open, and ALL of those were ")
		TEXT("crashes during world creation (the first %d writes), when no world existed yet. ")
		TEXT("No established world was made unopenable by any single crash."),
		Recovered, Recovered + Refusals, LostTail, Refusals, CreationWrites));

	// The matrix proves nothing if every injection happened to be survivable in the same way.
	TestTrue(TEXT("Some crashes were recovered from"), Recovered > 0);
	TestTrue(TEXT("and some cost the tail -- otherwise no write was load-bearing"), LostTail > 0);
	TestTrue(TEXT("Every refusal happened during world creation, none after it"),
		Refusals <= CreationWrites * 2);

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

// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "TerrainCheckpoint.h"
#include "TerrainSettings.h"
#include "TerrainPersistenceIndex.h"
#include "TerrainCommitJournal.h"
#include "MemoryTerrainBackend.h"
#include "ITerrainDensityField.h"
#include "TerrainChunk.h"
#include "TerrainOpGeometry.h"
#include "TerrainPersistenceFixtures.h"

/**
 * TerrainCore.Persistence.Capture.Pump -- a checkpoint taken while the world keeps changing.
 *
 * `Checkpoint.Equivalence` proves a cut taken at a quiescent moment is correct. That is the
 * easy case: nothing is moving, so the cut is trivially consistent. The pump gives that up on
 * purpose -- it spreads capture across frames so the game does not stall -- and in exchange it
 * has to defend a much harder property:
 *
 *   **every chunk in the checkpoint is its state at G, even though edits kept landing.**
 *
 * The defence is copy-before-write: a chunk the capture still owes is encoded before the edit
 * that is about to change it. This test makes that fire, and would fail loudly if it did not,
 * because a chunk captured AFTER an edit would carry the post-edit hash.
 *
 * **The interleaving is asserted, not assumed.** A test that meant to interleave and did not
 * would pass while proving nothing -- so it checks that copy-before-write actually took chunks,
 * and that the mid-capture edits actually hit chunks the capture still owed.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainCapturePumpTest, "TerrainCore.Persistence.Capture.Pump",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

namespace
{
	class FPumpField final : public ITerrainDensityField
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

	FTerrainOp PumpDig(const FIntVector& Centre, int32 RadiusVox, FTerrainOpSeq OpSeq)
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
}

bool FTerrainCapturePumpTest::RunTest(const FString& Parameters)
{
	using namespace TerrainPersistTest;

	// No AddExpectedError here on purpose: nothing in this test drives a chunk nonresident, and
	// an expectation that never fires is itself a failure.
	FTerrainMemoryStorageDevice Device;
	const FTerrainPersistIdentity Identity = MakeIdentity();

	TSharedPtr<FPumpField, ESPMode::ThreadSafe> Field =
		MakeShared<FPumpField, ESPMode::ThreadSafe>();

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

	// Eight edits spread over eight distinct regions, so the cut has plenty of chunks for the
	// pump to still owe when the later edits arrive.
	const FTerrainOp Cut[8] = {
		PumpDig(FIntVector(   0,  0, -4), 4, 1),
		PumpDig(FIntVector(  34,  0, -4), 4, 2),
		PumpDig(FIntVector(  68,  0, -4), 4, 3),
		PumpDig(FIntVector( -34,  0, -4), 4, 4),
		PumpDig(FIntVector( -68,  0, -4), 4, 5),
		PumpDig(FIntVector(   0, 34, -4), 4, 6),
		PumpDig(FIntVector(   0, 68, -4), 4, 7),
		PumpDig(FIntVector(   0,-34, -4), 4, 8),
	};
	// These land DURING the capture, on chunks the cut already covers.
	const FTerrainOp During[3] = {
		PumpDig(FIntVector(  68,  2, -4), 3,  9),
		PumpDig(FIntVector( -68,  2, -4), 3, 10),
		PumpDig(FIntVector(   0, 70, -4), 3, 11),
	};

	FTerrainWorldStore Store(Device);
	if (!Store.Create(Base, Identity.World, Identity.Epoch, 1789412345678LL).IsOk())
	{
		AddError(TEXT("World create failed"));
		return false;
	}

	FTerrainWorldStoreJournal Journal(Store);
	FMemoryTerrainBackend Backend;
	Backend.Initialize(Init);

	FTerrainStreamingInterest Interest;
	Interest.InterestId    = 1;
	Interest.WorldLocation = FVector::ZeroVector;
	Interest.RadiusCm      = 6000.0;
	Interest.bCollision    = true;
	Backend.SetStreamingInterest(Interest);

	FTerrainRevisionIndex Revisions;
	TMap<FTerrainChunkKey, FTerrainOpSeq> Dirty;
	TSet<FTerrainChunkKey> CutChunks;

	auto Commit = [&](const FTerrainOp& Op, bool bTrack) -> bool
	{
		FTerrainEditResult Result;
		if (!Backend.ApplyOp(Op, Result))
		{
			AddError(FString::Printf(TEXT("Backend refused op %llu"), Op.OpSeq));
			return false;
		}

		TArray<FTerrainChunkRevision> Changed;
		for (const FTerrainChunkKey& Key : Result.AffectedChunks)
		{
			FTerrainChunkRevision Revision;
			Revision.Key    = FIntVector(Key.X, Key.Y, Key.Z);
			Revision.Before = Revisions.GetRevision(Key);
			Changed.Add(Revision);
			if (bTrack) { Dirty.Add(Key, Op.OpSeq); CutChunks.Add(Key); }
		}
		if (!Revisions.TryBumpRevisions(Result.AffectedChunks))
		{
			AddError(TEXT("Revision bump failed"));
			return false;
		}
		for (FTerrainChunkRevision& Revision : Changed)
		{
			Revision.After = Revisions.GetRevision(
				FTerrainChunkKey(Revision.Key.X, Revision.Key.Y, Revision.Key.Z));
		}

		FTerrainCommitIdentity CommitIdentity;
		CommitIdentity.RequestId  = uint32(Op.OpSeq);
		CommitIdentity.ChildCount = 1;
		return Journal.RecordCommit(Op, Result, CommitIdentity, Changed);
	};

	for (const FTerrainOp& Op : Cut)
	{
		if (!Commit(Op, /*bTrack=*/true)) { return false; }
	}

	// The state at G. Every chunk in the published checkpoint must match one of these, and a
	// chunk captured after a mid-capture edit would not.
	TMap<FTerrainChunkKey, uint64> HashesAtG;
	for (const FTerrainChunkKey& Key : CutChunks)
	{
		HashesAtG.Add(Key, Backend.HashRegion(Key));
	}

	// ===== the capture starts, and the world keeps moving ==================================
	FTerrainCapturePump Pump;
	const FTerrainStoreResult Started = Pump.Begin(
		Store, Backend, Revisions, CopyTemp(Dirty), /*G=*/8, /*W=*/MAX_uint64, 1789412355555LL);
	if (!Started.IsOk())
	{
		AddError(FString::Printf(TEXT("Pump refused to start: %s"), *Started.ToString()));
		return false;
	}
	TestTrue(TEXT("The capture is in flight"), Pump.IsActive());
	TestEqual(TEXT("and owes every dirty chunk"), Pump.Remaining(), Dirty.Num());

	// A deliberately tiny budget, so only part of the cut is taken before the edits land.
	Pump.Advance(0.0, MAX_uint64);
	const int32 AfterFirstSlice = Pump.Remaining();
	TestTrue(TEXT("A zero budget still makes progress rather than spinning"),
		AfterFirstSlice < Dirty.Num());
	TestTrue(TEXT("but does NOT finish the capture -- otherwise nothing below is interleaved"),
		AfterFirstSlice > 0);

	// Now the edits land mid-capture, through the same order the live path uses: the footprint
	// is handed to the pump BEFORE the backend is allowed to touch it.
	int32 OwedAndHit = 0;
	for (const FTerrainOp& Op : During)
	{
		FTerrainBox Bounds;
		TArray<FTerrainChunkKey> Keys;
		if (!TerrainOpBounds(Op, Bounds) || !TerrainChunkKeysForBox(Bounds, Keys))
		{
			AddError(TEXT("Could not compute a footprint"));
			return false;
		}

		const int32 Before = Pump.Remaining();
		Pump.NoticeWrite(Keys);
		OwedAndHit += Before - Pump.Remaining();

		if (!Commit(Op, /*bTrack=*/false)) { return false; }
	}

	// Without this the test would pass while proving nothing: if the mid-capture edits never
	// touched a chunk the capture still owed, copy-before-write was never exercised.
	TestTrue(TEXT("The mid-capture edits actually hit chunks the capture still owed"),
		OwedAndHit > 0);
	TestTrue(TEXT("and the pump recorded them as copy-before-write"),
		Pump.Stats().CopiedBeforeWrite > 0);

	while (!Pump.Advance(0.0005, MAX_uint64))
	{
	}
	if (!Pump.Result().IsOk())
	{
		AddError(FString::Printf(TEXT("Capture failed: %s"), *Pump.Result().ToString()));
		return false;
	}

	TestEqual(TEXT("The cut is at G=8"), Pump.Stats().G, (FTerrainOpSeq)8);
	TestEqual(TEXT("and one payload was written per dirty chunk"),
		Pump.Stats().ChunksWritten, Dirty.Num());
	TestEqual(TEXT("The checkpoint published at G=8"),
		Store.GetState().Checkpoint.G, (FTerrainOpSeq)8);
	TestEqual(TEXT("naming every chunk in the cut"),
		(int32)Store.GetState().Checkpoint.LeafKeyCount, Dirty.Num());

	AddInfo(FString::Printf(
		TEXT("Pumped capture: %d chunks, %d taken by copy-before-write, %.4f s of work"),
		Pump.Stats().ChunksWritten, Pump.Stats().CopiedBeforeWrite, Pump.Stats().WorkSeconds()));

	// ===== the checkpoint must be the world as it was at G, not as it is now ================
	// The backend has moved on -- three edits landed after the cut -- so restoring into a fresh
	// backend and comparing against HashesAtG is the whole assertion. A chunk that had been
	// captured after its mid-capture edit would differ here.
	{
		FTerrainWorldStore Reopened(Device);
		if (!Reopened.Open().IsOk())
		{
			AddError(TEXT("World open failed"));
			return false;
		}

		FMemoryTerrainBackend Restored;
		Restored.Initialize(Init);

		FTerrainRevisionIndex RestoredRevisions;
		FTerrainRestoreStats RestoreStats;
		const FTerrainStoreResult Result =
			TerrainRestoreCheckpoint(Reopened, Restored, RestoredRevisions, RestoreStats);
		if (!Result.IsOk())
		{
			AddError(FString::Printf(TEXT("Restore failed: %s"), *Result.ToString()));
			return false;
		}
		TestEqual(TEXT("Every chunk in the cut was restored"),
			RestoreStats.ChunksRestored, HashesAtG.Num());

		int32 Mismatched = 0;
		for (const TPair<FTerrainChunkKey, uint64>& Expected : HashesAtG)
		{
			if (Restored.HashRegion(Expected.Key) != Expected.Value)
			{
				++Mismatched;
			}
		}
		TestEqual(TEXT("and every restored chunk is its state at G, not its state now"),
			Mismatched, 0);

		// The converse, so the comparison above cannot be passing vacuously: the LIVE backend
		// has moved past G, so at least one of those chunks must now hash differently.
		int32 MovedOn = 0;
		for (const TPair<FTerrainChunkKey, uint64>& Expected : HashesAtG)
		{
			if (Backend.HashRegion(Expected.Key) != Expected.Value)
			{
				++MovedOn;
			}
		}
		TestTrue(TEXT("while the live world HAS moved past G -- so the match above is real"),
			MovedOn > 0);
	}

	TestTrue(TEXT("A publication is reported to the owner once"), Pump.ConsumeCompletion());
	TestFalse(TEXT("and only once"), Pump.ConsumeCompletion());

	// ===== a refused cut is an end too, and hands its keys back =============================
	{
		FTerrainCapturePump Refused;
		TMap<FTerrainChunkKey, FTerrainOpSeq> Keys;
		Keys.Add(CutChunks.Array()[0], 11);
		// G=12 is not the journal head (11), so Begin refuses.
		const FTerrainStoreResult Begun = Refused.Begin(
			Store, Backend, Revisions, MoveTemp(Keys), /*G=*/12, /*W=*/MAX_uint64, 1789412366666LL);
		TestFalse(TEXT("A cut ahead of the journal head is refused"), Begun.IsOk());
		TestFalse(TEXT("leaving the pump inactive"), Refused.IsActive());
		TestTrue(TEXT("and the refusal is reported like any other end"), Refused.ConsumeCompletion());
		TestFalse(TEXT("with its result"), Refused.Result().IsOk());
		TestEqual(TEXT("and the cut's keys handed back, not dropped"), Refused.TakeCut().Num(), 1);
	}

	// ===== an abandoned capture leaves nothing behind =======================================
	{
		FTerrainCapturePump Abandoned;
		TMap<FTerrainChunkKey, FTerrainOpSeq> Later;
		Later.Add(CutChunks.Array()[0], 11);

		const FTerrainStoreResult Begun = Abandoned.Begin(
			Store, Backend, Revisions, MoveTemp(Later), /*G=*/11, /*W=*/MAX_uint64, 1789412366666LL);
		TestTrue(TEXT("A second capture starts"), Begun.IsOk());

		const uint64 GenerationBefore = Store.GetState().Root.Generation;
		Abandoned.Abandon();
		TestFalse(TEXT("and is no longer active once abandoned"), Abandoned.IsActive());
		TestEqual(TEXT("and published nothing"),
			Store.GetState().Root.Generation, GenerationBefore);
		TestFalse(TEXT("leaving no open batch on the store"),
			Store.GetObjects().IsBatchOpen());
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

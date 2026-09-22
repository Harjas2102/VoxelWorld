// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "TerrainCheckpoint.h"
#include "TerrainService.h"
#include "TerrainSettings.h"
#include "TerrainSettlement.h"
#include "TerrainCommitJournal.h"
#include "TerrainOpGeometry.h"
#include "MemoryTerrainBackend.h"
#include "ITerrainDensityField.h"
#include "TerrainChunk.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/World.h"
#include "TerrainPersistenceFixtures.h"

/**
 * TerrainCore.Persistence.Capture.Service -- the capture pump as the SERVICE drives it.
 *
 * `Capture.Pump` proves the pump is right when its caller is. Codex's review of CP-016..CP-020
 * found defects in the caller that a component-only test could never see:
 *
 *   F1. The service never told the pump the settlement watermark W, so `G <= W` (P-003 §3) was
 *       dead code in normal play, and an empty cut skipped it even when told. A crash right after
 *       publication booted into W < G and closed terrain access.
 *   F2. A copy-before-write failure, inside an edit, ended the capture with nobody told. The
 *       session kept capturing, and the next checkpoint published past history the failed cut had
 *       taken out of the dirty set.
 *   F3. The 32-record settlement window was checked once per pump call, so one call running many
 *       operations could pass it (measured at 34).
 *
 * All of them run here through the real service entry points -- `MaybeCaptureCheckpoint`, the
 * queue's callbacks and `Pump`, `CommitOp` -- with a real settlement worker whose watermark only moves when the
 * test polls it, so "settlement is slow" is deterministic rather than a sleep.
 *
 * Mutation-checked against the pre-fix source: F1's cases publish at G=2 with W=0 and at G=3
 * with W=2; F2's case publishes a second cut and B restores to its pre-edit state; F3's executes all
 * 48 operations in one pump call.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainCaptureServiceTest, "TerrainCore.Persistence.Capture.Service",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

namespace
{
	class FCaptureServiceField final : public ITerrainDensityField
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

	/** The memory backend, with reads of chosen chunks made to fail on demand. */
	class FFaultingReadBackend final : public ITerrainBackend
	{
	public:
		TSet<FTerrainChunkKey> FailReads;

		virtual bool Initialize(const FTerrainBackendInit& In) override { return Inner.Initialize(In); }
		virtual void Shutdown() override { Inner.Shutdown(); }
		virtual bool ApplyOp(const FTerrainOp& Op, FTerrainEditResult& Out) override { return Inner.ApplyOp(Op, Out); }
		virtual bool ReadRegion(const FTerrainChunkKey& Key, FTerrainRegionData& Out) override
		{
			return !FailReads.Contains(Key) && Inner.ReadRegion(Key, Out);
		}
		virtual bool WriteRegion(const FTerrainRegionData& In) override { return Inner.WriteRegion(In); }
		virtual uint64 HashRegion(const FTerrainChunkKey& Key) const override { return Inner.HashRegion(Key); }
		virtual bool IsRegionResident(const FTerrainChunkKey& Key) const override { return Inner.IsRegionResident(Key); }
		virtual void FlushPendingWork() override { Inner.FlushPendingWork(); }
		virtual bool QueryPoint(const FIntVector& Position, FTerrainPointSample& Out) const override
		{
			return Inner.QueryPoint(Position, Out);
		}
		virtual bool MeasuresPhysicalYield() const override { return true; }
		virtual void SetStreamingInterest(const FTerrainStreamingInterest& In) override { Inner.SetStreamingInterest(In); }
		virtual void ClearStreamingInterest(uint32 InterestId) override { Inner.ClearStreamingInterest(InterestId); }

	private:
		FMemoryTerrainBackend Inner;
	};

	/** A ledger that settles instantly on the worker's pipe; the TEST decides when W is seen. */
	class FInstantLedger final : public ITerrainSettlementLedger
	{
	public:
		virtual bool Exists(const FString&) const override { return true; }
		virtual FTerrainStoreResult Open(const FString&, const FTerrainPersistIdentity&, bool) override
		{
			return FTerrainStoreResult::Ok();
		}
		virtual void Close() override {}
		virtual FTerrainOpSeq GetWatermark() const override { return Watermark; }
		virtual FTerrainStoreResult Settle(TConstArrayView<FTerrainSettlementInput> Inputs) override
		{
			if (Inputs.Num() > 0) Watermark = Inputs.Last().OpSeq;
			Rows += Inputs.Num();
			return FTerrainStoreResult::Ok();
		}
		virtual bool ReadBalances(TArray<FTerrainLedgerBalance>& Out) const override { Out.Reset(); return true; }
		virtual int64 CountSettlements() const override { return Rows; }

	private:
		FTerrainOpSeq Watermark = 0;
		int64 Rows = 0;
	};

	FTerrainOp ServiceDig(const FIntVector& Centre, int32 RadiusVox, FTerrainOpSeq OpSeq)
	{
		FTerrainOp Op;
		Op.Kind         = ETerrainOpKind::Remove;
		Op.Shape        = ETerrainShape::Sphere;
		Op.Source       = ETerrainSource::Admin;
		Op.SourceId     = 1;
		Op.CentreVox    = Centre;
		Op.RadiusVoxQ16 = RadiusVox << 16;
		Op.OpSeq        = OpSeq;
		return Op;
	}

	// Two digs well inside two different chunks, and a third in open air that changes nothing.
	const FIntVector ChunkA(16, 16, -12);
	const FIntVector ChunkB(80, 16, -12);
	const FIntVector InTheAir(16, 16, 40);
}

bool FTerrainCaptureServiceTest::RunTest(const FString& Parameters)
{
	using namespace TerrainPersistTest;

	auto* Settings = GetMutableDefault<UTerrainSettings>();
	TGuardValue<bool>  CaptureOn(Settings->bCheckpointCapture, true);
	TGuardValue<int32> DirtyTrigger(Settings->CheckpointDirtyChunkTrigger, 1);
	TGuardValue<int32> OpTrigger(Settings->CheckpointOpTrigger, 1);
	TGuardValue<double> Budget(Settings->CheckpointPumpMillisPerFrame, 0.05);

	TSharedPtr<FCaptureServiceField, ESPMode::ThreadSafe> Field =
		MakeShared<FCaptureServiceField, ESPMode::ThreadSafe>();

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
	const FTerrainPersistIdentity Identity = MakeIdentity();

	/**
	 * A Ready service on its own store, recording through the real commit path.
	 *
	 * `CommitOp` advances revisions only with authority, which needs an initialised GAME world, so
	 * the service is that world's own subsystem. The world never begins play: BeginPlay is what
	 * creates the configured backend and opens a save on disk, and neither may happen here.
	 */
	auto MakeService = [&](FTerrainMemoryStorageDevice& Device, FFaultingReadBackend*& OutBackend,
	                       TStrongObjectPtr<UWorld>& OutWorld) -> UTerrainService*
	{
		OutWorld.Reset(UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false,
			MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("CaptureServiceTest"))));
		UTerrainService* Service = OutWorld ? OutWorld->GetSubsystem<UTerrainService>() : nullptr;
		if (Service == nullptr || !Service->HasAuthority())
		{
			return nullptr;
		}
		auto Backend = MakeUnique<FFaultingReadBackend>();
		OutBackend = Backend.Get();
		Backend->Initialize(Init);
		FTerrainStreamingInterest Interest;
		Interest.InterestId    = 1;
		Interest.WorldLocation = FVector::ZeroVector;
		Interest.RadiusCm      = 6000.0;
		Backend->SetStreamingInterest(Interest);
		Service->Backend       = MoveTemp(Backend);
		Service->RevisionIndex = MakeUnique<FTerrainRevisionIndex>();
		Service->ActiveInit    = Init;
		Service->State         = ETerrainServiceState::Ready;
		Service->WorldStore    = MakeUnique<FTerrainWorldStore>(Device);
		if (!Service->WorldStore->Create(Base, Identity.World, Identity.Epoch, 1789412345678LL).IsOk())
		{
			return nullptr;
		}
		Service->WorldJournal = MakeUnique<FTerrainWorldStoreJournal>(*Service->WorldStore);
		Service->SetCommitJournal(Service->WorldJournal.Get());
		return Service;
	};

	/** One edit through the live order: Apply (pins, copy-before-write, backend), then Commit. */
	auto Edit = [&](UTerrainService& Service, const FTerrainOp& Op) -> bool
	{
		const FTerrainQueueCallbacks Callbacks = Service.QueueCallbacks();
		FTerrainEditResult Result;
		FTerrainCommitIdentity Commit;
		Commit.RequestId = uint32(Op.OpSeq);
		if (!Callbacks.Apply(Op, Result) || !Callbacks.Commit(Op, Result, Commit))
		{
			AddError(FString::Printf(TEXT("Edit %llu did not commit"), Op.OpSeq));
			return false;
		}
		return true;
	};

	auto PublishedG = [](const UTerrainService& Service) { return Service.WorldStore->GetState().Checkpoint.G; };

	auto KeysOf = [](const FTerrainOp& Op)
	{
		FTerrainBox Bounds;
		TArray<FTerrainChunkKey> Keys;
		TerrainOpBounds(Op, Bounds);
		TerrainChunkKeysForBox(Bounds, Keys);
		return Keys;
	};

	// ===== F1: publication waits for W, on the service's own capture path ====================
	{
		FTerrainMemoryStorageDevice Device;
		FFaultingReadBackend* Backend = nullptr;
		TStrongObjectPtr<UWorld> World;
		UTerrainService* Service = MakeService(Device, Backend, World);
		if (!Service) { AddError(TEXT("Service setup failed")); return false; }

		// A running worker at W=0. It settles on its own pipe, but W only moves when the service
		// polls it -- and nothing below polls until the test says so. That is a settlement that
		// is arbitrarily slow, without a sleep in sight.
		Service->Settlement = MakeUnique<FTerrainSettlementWorker>();
		Service->Settlement->Start(MakeUnique<FInstantLedger>(), 0);

		auto SettleThrough = [&](FTerrainOpSeq Target)
		{
			const double Deadline = FPlatformTime::Seconds() + 10.0;
			while (Service->Settlement->GetWatermark() < Target && FPlatformTime::Seconds() < Deadline)
			{
				FPlatformProcess::Sleep(0.002f);
				Service->Settlement->Poll();
			}
			return Service->Settlement->GetWatermark() >= Target;
		};

		if (!Edit(*Service, ServiceDig(ChunkA, 4, 1)) || !Edit(*Service, ServiceDig(ChunkB, 4, 2))) return false;
		TestTrue(TEXT("F1: the edits dirtied chunks"), Service->DirtyChunks.Num() >= 2);

		// Far more frames than the cut needs: every chunk gets encoded, and still nothing lands.
		bool bEverAhead = false;
		for (int32 Frame = 0; Frame < 64; ++Frame)
		{
			Service->MaybeCaptureCheckpoint();
			bEverAhead |= PublishedG(*Service) > Service->Settlement->GetWatermark();
		}
		TestFalse(TEXT("F1: no root is ever published ahead of W"), bEverAhead);
		TestEqual(TEXT("F1: with W=0 the cut at G=2 does not publish"), PublishedG(*Service), (FTerrainOpSeq)0);
		TestTrue(TEXT("F1: and the capture is waiting, not abandoned"), Service->CapturePump.IsActive());
		TestEqual(TEXT("F1: with every chunk already encoded"), Service->CapturePump.Remaining(), 0);
		TestFalse(TEXT("F1: waiting is not a failure"), Service->bCheckpointDisabled);

		if (!SettleThrough(2)) { AddError(TEXT("F1: settlement did not reach 2")); return false; }
		Service->MaybeCaptureCheckpoint();
		TestEqual(TEXT("F1: once W reaches the cut, it publishes at G=2"), PublishedG(*Service), (FTerrainOpSeq)2);

		// The empty cut. A committed no-change edit moves the head but dirties nothing, so the
		// op trigger takes a cut with no chunks -- the path that used to publish from inside
		// Begin without looking at W at all.
		const int32 DirtyBefore = Service->DirtyChunks.Num();
		if (!Edit(*Service, ServiceDig(InTheAir, 2, 3))) return false;
		TestEqual(TEXT("F1: the edit in the air changed nothing"), Service->DirtyChunks.Num(), DirtyBefore);
		TestEqual(TEXT("F1: but it is committed"), Service->WorldStore->GetJournal()->GetHead(), (FTerrainOpSeq)3);

		bEverAhead = false;
		for (int32 Frame = 0; Frame < 16; ++Frame)
		{
			Service->MaybeCaptureCheckpoint();
			bEverAhead |= PublishedG(*Service) > Service->Settlement->GetWatermark();
		}
		TestFalse(TEXT("F1: an EMPTY cut is not published ahead of W either"), bEverAhead);
		TestEqual(TEXT("F1: so G stays at 2 while W is 2"), PublishedG(*Service), (FTerrainOpSeq)2);

		if (!SettleThrough(3)) { AddError(TEXT("F1: settlement did not reach 3")); return false; }
		Service->MaybeCaptureCheckpoint();
		TestEqual(TEXT("F1: and publishes at G=3 once W does"), PublishedG(*Service), (FTerrainOpSeq)3);

		Service->DestroyBackend();
		World->DestroyWorld(/*bInformEngineOfWorld=*/false);
	}

	// ===== F3: the settlement window holds per operation, inside one big pump call ==========
	//
	// Codex measured 34 unsettled against the 32-record bound: the window was checked once per
	// pump call, and one call may run many operations. Here 48 are queued and ONE pump call with
	// the console's shape (256 ops, a generous budget) is made, with settlement standing still.
	{
		FTerrainMemoryStorageDevice Device;
		FFaultingReadBackend* Backend = nullptr;
		TStrongObjectPtr<UWorld> World;
		UTerrainService* Service = MakeService(Device, Backend, World);
		if (!Service) { AddError(TEXT("Service setup failed")); return false; }
		Service->Settlement = MakeUnique<FTerrainSettlementWorker>();
		Service->Settlement->Start(MakeUnique<FInstantLedger>(), 0);

		const FTerrainQueueCallbacks Cb = Service->QueueCallbacks();
		constexpr int32 Sources = 16, JobsEach = 3;   // the queue's per-source burst is 3
		for (int32 S = 0; S < Sources; ++S)
		{
			Service->EditQueue.RegisterSource(UTerrainService::StressBotBase + S, FTerrainSourceState());
		}
		int32 Queued = 0;
		for (int32 J = 0; J < JobsEach; ++J)
		{
			for (int32 S = 0; S < Sources; ++S)
			{
				FTerrainEditReceipt Receipt;
				const FTerrainOp Op = ServiceDig(ChunkA + FIntVector((S % 4) * 3 - 4, (S / 4) * 3 - 4, -J * 3), 2, 0);
				Queued += Service->EditQueue.Submit(UTerrainService::StressBotBase + S, J + 1, Op, 0.0, Cb, Receipt) ? 1 : 0;
			}
		}
		TestEqual(TEXT("F3: more operations queued than the window allows"), Queued, Sources * JobsEach);

		const int32 Window = FTerrainSettlementWorker::MaxPending;
		Service->EditQueue.Pump(0.0, Cb, 256, 10.0);
		TestEqual(TEXT("F3: one 256-op pump call executes exactly the window's worth"),
			Service->WorldStore->GetJournal()->GetHead(), (FTerrainOpSeq)Window);
		TestEqual(TEXT("F3: so exactly 32 records are unsettled"), Service->Settlement->Pending(), Window);
		TestEqual(TEXT("F3: and the peak, sampled at every Submit, never passed it"), Service->SettlementPendingPeak, Window);
		TestEqual(TEXT("F3: the rest are still queued, not refused"), Service->EditQueue.Depth(), Queued - Window);

		const double Deadline = FPlatformTime::Seconds() + 10.0;
		while (Service->Settlement->Pending() > 0 && FPlatformTime::Seconds() < Deadline)
		{
			FPlatformProcess::Sleep(0.002f);
			Service->Settlement->Poll();
		}
		Service->EditQueue.Pump(0.0, Cb, 256, 10.0);
		TestEqual(TEXT("F3: once settled, the rest execute"),
			Service->WorldStore->GetJournal()->GetHead(), (FTerrainOpSeq)Queued);
		TestTrue(TEXT("F3: and the peak still never passed the window"), Service->SettlementPendingPeak <= Window);

		Service->DestroyBackend();
		World->DestroyWorld(/*bInformEngineOfWorld=*/false);
	}

	// ===== F2: a copy-before-write failure is seen, and loses no dirty history ==============
	{
		AddExpectedError(TEXT("did not read back as a complete Dense region"),
			EAutomationExpectedErrorFlags::Contains, 1, /*IsRegex=*/false);
		AddExpectedError(TEXT("Terrain checkpoint at G=2 failed"),
			EAutomationExpectedErrorFlags::Contains, 1, /*IsRegex=*/false);

		FTerrainMemoryStorageDevice Device;
		FFaultingReadBackend* Backend = nullptr;
		TStrongObjectPtr<UWorld> World;
		UTerrainService* Service = MakeService(Device, Backend, World);
		if (!Service) { AddError(TEXT("Service setup failed")); return false; }
		// No ledger here: W is "everything", so only F2's path decides what publishes.

		const FTerrainOp DigA = ServiceDig(ChunkA, 4, 1);
		const FTerrainOp DigB = ServiceDig(ChunkB, 4, 2);
		const FTerrainOp DigAAgain = ServiceDig(ChunkA + FIntVector(2, 0, 0), 3, 3);
		const TArray<FTerrainChunkKey> KeysA = KeysOf(DigA);
		const TArray<FTerrainChunkKey> KeysB = KeysOf(DigB);
		for (const FTerrainChunkKey& K : KeysB)
		{
			if (KeysA.Contains(K) || KeysOf(DigAAgain).Contains(K))
			{
				AddError(TEXT("Fixture: B's chunks must be disjoint from A's"));
				return false;
			}
		}

		if (!Edit(*Service, DigA) || !Edit(*Service, DigB)) return false;

		// The cut at G=2 over A and B. Begin encodes nothing, so both are still owed.
		Service->MaybeCaptureCheckpoint();
		TestTrue(TEXT("F2: a capture is in flight"), Service->CapturePump.IsActive());
		TestEqual(TEXT("F2: owing every dirty chunk"), Service->CapturePump.Remaining(), Service->CapturePump.Stats().DirtyKeys);
		TestEqual(TEXT("F2: and the service's own dirty set was handed over"), Service->DirtyChunks.Num(), 0);

		TMap<FTerrainChunkKey, uint64> HashesOfB;
		for (const FTerrainChunkKey& K : KeysB) HashesOfB.Add(K, Backend->HashRegion(K));

		// A's chunks cannot be read, so copy-before-write fails inside the next edit of A.
		for (const FTerrainChunkKey& K : KeysA) Backend->FailReads.Add(K);
		if (!Edit(*Service, DigAAgain)) return false;
		Backend->FailReads.Reset();

		TestFalse(TEXT("F2: the failure ended the capture"), Service->CapturePump.IsActive());
		TestTrue(TEXT("F2: and the service SAW it: checkpoints are off for this session"), Service->bCheckpointDisabled);
		int32 BOwed = 0;
		for (const FTerrainChunkKey& K : KeysB)
		{
			const FTerrainOpSeq* Seq = Service->DirtyChunks.Find(K);
			BOwed += (Seq != nullptr && *Seq == 2) ? 1 : 0;
		}
		TestEqual(TEXT("F2: B, never edited again, is dirty again at its own OpSeq"), BOwed, KeysB.Num());
		// (18,16,-12) is in chunk (0,0,-1): its later edit's OpSeq must win over the cut's.
		const FTerrainOpSeq* SeqA = Service->DirtyChunks.Find(FTerrainChunkKey(0, 0, -1));
		TestTrue(TEXT("F2: A keeps the NEWER OpSeq of its later edit"), SeqA != nullptr && *SeqA == 3);

		for (int32 Frame = 0; Frame < 16; ++Frame) Service->MaybeCaptureCheckpoint();
		TestEqual(TEXT("F2: nothing further is published this session"), PublishedG(*Service), (FTerrainOpSeq)0);

		// And the dirty set is honest on its own, not only behind the latch: lift it, as any
		// future retry policy would, and the next checkpoint must still carry B.
		Service->bCheckpointDisabled = false;
		for (int32 Frame = 0; Frame < 256 && PublishedG(*Service) == 0; ++Frame) Service->MaybeCaptureCheckpoint();
		TestEqual(TEXT("F2: a later cut publishes at G=3"), PublishedG(*Service), (FTerrainOpSeq)3);

		FTerrainWorldStore Reopened(Device);
		FMemoryTerrainBackend Restored;
		Restored.Initialize(Init);
		FTerrainRevisionIndex Revisions;
		FTerrainRestoreStats Stats;
		if (!Reopened.Open().IsOk() || !TerrainRestoreCheckpoint(Reopened, Restored, Revisions, Stats).IsOk())
		{
			AddError(TEXT("F2: restore failed"));
			return false;
		}
		int32 BMatches = 0;
		for (const TPair<FTerrainChunkKey, uint64>& Expected : HashesOfB)
		{
			BMatches += Restored.HashRegion(Expected.Key) == Expected.Value ? 1 : 0;
		}
		TestEqual(TEXT("F2: B restores as dug -- its edit is in the checkpoint, not lost below G"),
			BMatches, HashesOfB.Num());

		Service->DestroyBackend();
		World->DestroyWorld(/*bInformEngineOfWorld=*/false);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

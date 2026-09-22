// Copyright VoxelWorld. See Docs/proposals/P-010-settlement-ledger.md.

#include "TerrainSettlement.h"

#include "TerrainCore.h"
#include "TerrainMaterials.h"
#include "TerrainWorldStore.h"
#include "HAL/PlatformProcess.h"
#include "Tasks/Task.h"

// ---- registry ---------------------------------------------------------------------------------

FTerrainSettlementRegistry& FTerrainSettlementRegistry::Get()
{
	static FTerrainSettlementRegistry Registry;
	return Registry;
}

void FTerrainSettlementRegistry::Register(FName Name, FTerrainSettlementCreator Creator)
{
	check(IsInGameThread());
	if (Creators.Contains(Name))
	{
		UE_LOG(LogTerrainCore, Warning, TEXT("Settlement ledger '%s' registered twice; the later one wins."), *Name.ToString());
	}
	Creators.Add(Name, MoveTemp(Creator));
}

void FTerrainSettlementRegistry::Unregister(FName Name)
{
	check(IsInGameThread());
	Creators.Remove(Name);
}

TUniquePtr<ITerrainSettlementLedger> FTerrainSettlementRegistry::Create(FName Name) const
{
	const FTerrainSettlementCreator* Creator = Creators.Find(Name);
	return Creator ? (*Creator)() : nullptr;
}

// ---- reading the journal ------------------------------------------------------------------------

FTerrainStoreResult TerrainReadSettlementInputs(
	FTerrainWorldStore& Store, FTerrainOpSeq AfterExclusive,
	TArray<FTerrainSettlementInput>& OutInputs, bool& bOutAnyEconomy, FTerrainOpSeq& OutFirstOpSeq)
{
	OutInputs.Reset();
	bOutAnyEconomy = false;
	OutFirstOpSeq = 0;
	if (!Store.IsOpen() || !Store.GetJournal())
	{
		return FTerrainStoreResult::Io(ETerrainStorageResult::NotFound);
	}

	const FTerrainPersistIdentity& Identity = Store.GetState().Identity;
	ITerrainStorageDevice& Device = Store.GetDevice();
	const uint64 ActiveSegmentId = Store.GetJournal()->GetState().ActiveSegmentId;

	TArray<FString> Names;
	const ETerrainStorageResult ListResult = Device.ListFiles(TerrainStoragePaths::JournalDirectory, Names);
	if (ListResult != ETerrainStorageResult::Ok)
	{
		return FTerrainStoreResult::Io(ListResult);
	}
	TArray<uint64> Segments;
	for (const FString& Name : Names)
	{
		uint64 SegmentId = 0;
		if (TerrainStoragePaths::ParseJournalSegment(Name, SegmentId) && SegmentId <= ActiveSegmentId)
		{
			Segments.Add(SegmentId);
		}
	}
	Segments.Sort();

	const uint64 WorldTag = TerrainPersistWorldTag(Identity.World, Identity.Epoch);
	FTerrainOpSeq Expected = 0;
	for (const uint64 SegmentId : Segments)
	{
		TArray<uint8> Bytes;
		const ETerrainStorageResult ReadResult = Device.Read(TerrainStoragePaths::JournalSegment(SegmentId), Bytes);
		if (ReadResult != ETerrainStorageResult::Ok)
		{
			return FTerrainStoreResult::Io(ReadResult);
		}
		FTerrainJournalScanResult Scan;
		const ETerrainPersistError ScanError =
			TerrainPersistScanJournalSegment(Bytes, Identity, SegmentId == ActiveSegmentId, Scan);
		if (ScanError != ETerrainPersistError::None)
		{
			return FTerrainStoreResult::Bad(ScanError);
		}

		int32 Cursor = TerrainPersistObjectHeaderSize + TerrainPersistSegmentHeaderBodySize;
		while (Cursor < Scan.GoodBytes)
		{
			int32 Length = 0;
			ETerrainJournalRecordType Type = ETerrainJournalRecordType::Commit;
			const TArrayView<const uint8> Remaining(Bytes.GetData() + Cursor, Scan.GoodBytes - Cursor);
			const ETerrainPersistError FrameError = TerrainPersistPeekRecordFrame(Remaining, Length, Type);
			if (FrameError != ETerrainPersistError::None)
			{
				return FTerrainStoreResult::Bad(FrameError);
			}
			const TArrayView<const uint8> Frame(Bytes.GetData() + Cursor, Length);
			Cursor += Length;
			if (Type == ETerrainJournalRecordType::Seal)
			{
				continue;
			}

			FTerrainJournalCommitRecord Record;
			const ETerrainPersistError RecordError = TerrainPersistDecodeCommitRecord(Frame, WorldTag, Record);
			if (RecordError != ETerrainPersistError::None)
			{
				return FTerrainStoreResult::Bad(RecordError);
			}
			if (Expected != 0 && Record.Op.OpSeq != Expected)
			{
				// P-003 §3: settlement needs the complete contiguous range, like replay does.
				return FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
			}
			Expected = Record.Op.OpSeq + 1;
			if (OutFirstOpSeq == 0)
			{
				OutFirstOpSeq = Record.Op.OpSeq;
			}
			bOutAnyEconomy |= Record.EconomyKind != ETerrainEconomyKind::NoEconomy;
			if (Record.Op.OpSeq <= AfterExclusive)
			{
				continue;
			}

			FTerrainSettlementInput Input;
			Input.OpSeq                = Record.Op.OpSeq;
			Input.EconomyKind          = Record.EconomyKind;
			Input.EconomyPolicyVersion = Record.EconomyPolicyVersion;
			Input.Deltas               = MoveTemp(Record.EconomyDeltas);
			const ETerrainPersistError DigestError = TerrainPersistComputeRecordDigest(Frame, Input.Digest);
			if (DigestError != ETerrainPersistError::None)
			{
				return FTerrainStoreResult::Bad(DigestError);
			}
			OutInputs.Add(MoveTemp(Input));
		}
	}
	return FTerrainStoreResult::Ok();
}

// ---- policy -----------------------------------------------------------------------------------

bool TerrainMaterialYields(FTerrainMatId Id)
{
	switch (Id)
	{
	case ETerrainMaterial::Topsoil:
	case ETerrainMaterial::Dirt:
	case ETerrainMaterial::Stone:
	case ETerrainMaterial::DeepStone:
	case ETerrainMaterial::Bedrock:
	case ETerrainMaterial::IronOre:
		return true;
	default:
		return false;   // Unknown, Air, Fill, and anything this build does not know
	}
}

void TerrainComputeEconomy(uint64 OwnerId, uint32 ToolId,
	TConstArrayView<FTerrainMaterialVolume> Physical, FTerrainCommitEconomy& OutEconomy)
{
	OutEconomy = FTerrainCommitEconomy();
	if (OwnerId == TerrainOwnerNone || ToolId != 0)
	{
		// No stable owner: nobody to pay, so no promise is recorded. Tool 0 is the only tool
		// that exists; any other id was refused at admission and cannot reach here paid.
		return;
	}
	OutEconomy.Kind          = ETerrainEconomyKind::ExactDeltas;
	OutEconomy.PolicyVersion = TerrainEconomyPolicyVersion;
	for (const FTerrainMaterialVolume& Volume : Physical)
	{
		if (Volume.MicroLitres > 0 && TerrainMaterialYields(Volume.MaterialId))
		{
			FTerrainEconomyDelta Delta;
			Delta.OwnerId     = OwnerId;
			Delta.ContainerId = TerrainContainerPersonal;
			Delta.ItemId      = Volume.MaterialId;   // v1: the item IS the material, in microlitres
			Delta.Count       = Volume.MicroLitres;
			OutEconomy.Deltas.Add(Delta);
		}
	}
}

// ---- worker -----------------------------------------------------------------------------------

FTerrainSettlementWorker::FTerrainSettlementWorker() = default;

FTerrainSettlementWorker::~FTerrainSettlementWorker()
{
	Stop();
}

void FTerrainSettlementWorker::Start(TUniquePtr<ITerrainSettlementLedger> InLedger, FTerrainOpSeq InWatermark)
{
	check(IsInGameThread());
	Ledger = TSharedPtr<ITerrainSettlementLedger, ESPMode::ThreadSafe>(InLedger.Release());
	Pipe = MakeUnique<UE::Tasks::FPipe>(TEXT("TerrainSettlement"));
	Watermark = LastSubmitted = InWatermark;
	bStarted = true;
}

void FTerrainSettlementWorker::Submit(FTerrainSettlementInput&& Input)
{
	check(IsInGameThread());
	if (!bStarted || bFailed)
	{
		return;
	}
	if (Input.OpSeq != LastSubmitted + 1)
	{
		// The service submits every committed record exactly once, in order. Anything else is a
		// bug that would leave a hole in W forever; fail closed rather than settle around it.
		bFailed = true;
		Failure = FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
		return;
	}
	LastSubmitted = Input.OpSeq;
	++Submitted;
	SubmitTimes.Add(Input.OpSeq, FPlatformTime::Seconds());
	Queue.Add(MoveTemp(Input));
	Dispatch();
}

void FTerrainSettlementWorker::Dispatch()
{
	// One batch in flight at a time keeps W trivially contiguous: a batch never starts before
	// the one below it has committed.
	if (InFlight > 0 || Queue.IsEmpty() || bFailed)
	{
		return;
	}
	const int32 Count = FMath::Min(MaxBatch, Queue.Num());
	TArray<FTerrainSettlementInput> Batch(Queue.GetData(), Count);
	Queue.RemoveAt(0, Count);
	++InFlight;
	TSharedPtr<ITerrainSettlementLedger, ESPMode::ThreadSafe> Target = Ledger;
	const float Delay = TestDelaySeconds;
	Pipe->Launch(TEXT("TerrainSettle"), [this, Target, Delay, Batch = MoveTemp(Batch)]() mutable
	{
		if (Delay > 0.f) FPlatformProcess::Sleep(Delay);
		FResult Out;
		Out.Result = Target->Settle(Batch);
		Out.Inputs = MoveTemp(Batch);
		Done.Enqueue(MoveTemp(Out));
	});
}

bool FTerrainSettlementWorker::Poll()
{
	check(IsInGameThread());
	FResult Result;
	while (Done.Dequeue(Result))
	{
		--InFlight;
		if (!Result.Result.IsOk())
		{
			bFailed = true;
			Failure = Result.Result;
			continue;
		}
		SettledCount += Result.Inputs.Num();
		Watermark = Result.Inputs.Last().OpSeq;
		const double Now = FPlatformTime::Seconds();
		for (const FTerrainSettlementInput& In : Result.Inputs)
		{
			double SubmittedAt = 0;
			if (SubmitTimes.RemoveAndCopyValue(In.OpSeq, SubmittedAt))
			{
				MaxLatencySeconds = FMath::Max(MaxLatencySeconds, Now - SubmittedAt);
				SumLatencySeconds += Now - SubmittedAt;
				++LatencySamples;
			}
		}
		SettledForNotify.Append(MoveTemp(Result.Inputs));
	}
	Dispatch();
	return !bFailed;
}

TArray<FTerrainSettlementInput> FTerrainSettlementWorker::TakeSettled()
{
	return MoveTemp(SettledForNotify);
}

bool FTerrainSettlementWorker::ReadBalancesBlocking(TArray<FTerrainLedgerBalance>& Out, int64& OutSettlements)
{
	check(IsInGameThread());
	if (!bStarted || !Ledger)
	{
		return false;
	}
	bool bOk = false;
	TSharedPtr<ITerrainSettlementLedger, ESPMode::ThreadSafe> Target = Ledger;
	UE::Tasks::FTask Read = Pipe->Launch(TEXT("TerrainLedgerRead"), [Target, &Out, &OutSettlements, &bOk]()
	{
		bOk = Target->ReadBalances(Out);
		OutSettlements = Target->CountSettlements();
	});
	Read.Wait();
	return bOk;
}

void FTerrainSettlementWorker::Stop()
{
	if (!bStarted)
	{
		return;
	}
	// Everything submitted is journaled; settle what is queued so a clean shutdown leaves W=H.
	// A crash instead leaves it to the boot pass, which is the whole design.
	while (!bFailed && (InFlight > 0 || !Queue.IsEmpty()))
	{
		Pipe->WaitUntilEmpty();
		Poll();
	}
	Pipe->WaitUntilEmpty();
	if (Ledger)
	{
		Ledger->Close();
	}
	Ledger.Reset();
	Pipe.Reset();
	bStarted = false;
}

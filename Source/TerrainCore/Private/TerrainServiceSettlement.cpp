// Copyright VoxelWorld. See Docs/proposals/P-010-settlement-ledger.md.

#include "TerrainService.h"
#include "TerrainSettlement.h"
#include "TerrainSettings.h"
#include "TerrainMaterials.h"
#include "TerrainCore.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "TimerManager.h"

/**
 * Settlement, wired into the running world (P-003 Â§2 steps 4-5, Â§3 settlement pass; P-010).
 *
 * BOOT, in order, before a single edit is admitted:
 *   1. no ledger file and the journal never recorded an economy -> create one at W=0 (a world
 *      from before T-131, or a brand new one); settle its NoEconomy records to reach W=H.
 *   2. no ledger file but the journal HAS recorded an economy -> refuse. Its inventories
 *      existed and are gone; P-003 Â§5 forbids opening a blank one over them.
 *   3. a ledger -> require G <= W <= H, then settle (W, H] from the journal, never recomputing.
 * RUNNING: every commit's recorded economy is submitted to the worker. More than 32 unsettled
 * records stops the edit queue until the ledger catches up; a failed settlement is an uncertain
 * storage fault and closes admission.
 */

namespace
{
	/** A stable owner id from the player's network identity. Zero when there is none. */
	uint64 OwnerFromPlayer(const APlayerController* PC)
	{
		const APlayerState* PS = PC ? PC->PlayerState : nullptr;
		if (!PS || !PS->GetUniqueId().IsValid())
		{
			return TerrainOwnerNone;
		}
		// Through the id's own virtual ToString, so TerrainCore links no online module (its
		// Build.cs stays Core/CoreUObject/Engine).
		const FUniqueNetIdPtr NetId = PS->GetUniqueId().GetUniqueNetId();
		if (!NetId.IsValid())
		{
			return TerrainOwnerNone;
		}
		const FString Id = NetId->ToString();
		const FTCHARToUTF8 Utf8(*Id);
		FBlake3 Hash;
		Hash.Update(Utf8.Get(), Utf8.Length());
		const FBlake3Hash Digest = Hash.Finalize();
		uint64 Owner = 0;
		FMemory::Memcpy(&Owner, Digest.GetBytes(), sizeof(Owner));
		// 0 and 1 are reserved (nobody, the server); remap rather than collide.
		return Owner <= TerrainOwnerServer ? Owner + 2 : Owner;
	}
}

uint64 UTerrainService::OwnerFromController(const APlayerController* PC)
{
	return OwnerFromPlayer(PC);
}

uint64 UTerrainService::OwnerForSource(uint32 SourceId) const
{
	if (SourceId == 1)
	{
		return TerrainOwnerServer;   // console and diagnostic edits
	}
	const uint64* Owner = SourceOwners.Find(SourceId);
	return Owner ? *Owner : TerrainOwnerNone;
}

bool UTerrainService::OpenLedger(const FString& Directory)
{
	check(IsInGameThread());
	const UTerrainSettings* Settings = GetDefault<UTerrainSettings>();
	SettledBalances.Reset();
	if (Settings->SettlementModule.IsNone())
	{
		UE_LOG(LogTerrainCore, Warning, TEXT("Settlement is OFF (SettlementModule=None): edits record no economy."));
		return true;
	}

	FModuleManager::Get().LoadModule(Settings->SettlementModule);
	TUniquePtr<ITerrainSettlementLedger> Ledger = FTerrainSettlementRegistry::Get().Create(Settings->SettlementModule);
	if (!Ledger)
	{
		UE_LOG(LogTerrainCore, Error, TEXT("Settlement module '%s' registered no ledger. TERRAIN ACCESS IS CLOSED."),
			*Settings->SettlementModule.ToString());
		return false;
	}

	const FString Path = FPaths::ConvertRelativePathToFull(Directory / TEXT("ledger.db"));
	const FTerrainPersistIdentity& Identity = WorldStore->GetState().Identity;
	const FTerrainOpSeq G = WorldStore->GetState().Checkpoint.G;
	const FTerrainOpSeq H = WorldStore->GetJournal()->GetHead();

	bool bAnyEconomy = false;
	FTerrainOpSeq FirstOpSeq = 0;
	TArray<FTerrainSettlementInput> Inputs;
	FTerrainStoreResult Result;

	if (!Ledger->Exists(Path))
	{
		Result = TerrainReadSettlementInputs(*WorldStore, 0, Inputs, bAnyEconomy, FirstOpSeq);
		if (!Result.IsOk())
		{
			UE_LOG(LogTerrainCore, Error, TEXT("Settlement: the journal could not be read (%s). TERRAIN ACCESS IS CLOSED."), *Result.ToString());
			return false;
		}
		if (bAnyEconomy)
		{
			UE_LOG(LogTerrainCore, Error,
				TEXT("Settlement: '%s' is missing, but this world's journal records paid edits. Its inventories ")
				TEXT("existed and are gone, and a blank ledger would silently erase them (P-003 Â§5). TERRAIN ACCESS ")
				TEXT("IS CLOSED; restore the whole world from a backup."), *Path);
			return false;
		}
		if (H != 0 && FirstOpSeq != 1)
		{
			UE_LOG(LogTerrainCore, Error, TEXT("Settlement: no ledger, and the journal no longer starts at OpSeq 1. TERRAIN ACCESS IS CLOSED."));
			return false;
		}
		Result = Ledger->Open(Path, Identity, /*bCreate=*/true);
	}
	else
	{
		Result = Ledger->Open(Path, Identity, /*bCreate=*/false);
		if (Result.IsOk())
		{
			const FTerrainOpSeq W = Ledger->GetWatermark();
			if (W > H || W < G)
			{
				// P-003 Â§3: W > H means the journal lost settled history; W < G means this ledger is
				// older than the terrain it would pay for -- an old database copy.
				UE_LOG(LogTerrainCore, Error,
					TEXT("Settlement: ledger watermark W=%llu is outside [G=%llu, H=%llu]. The ledger and the world do ")
					TEXT("not belong to the same moment. TERRAIN ACCESS IS CLOSED."), W, G, H);
				return false;
			}
			Result = TerrainReadSettlementInputs(*WorldStore, W, Inputs, bAnyEconomy, FirstOpSeq);
		}
	}
	if (!Result.IsOk())
	{
		UE_LOG(LogTerrainCore, Error, TEXT("Settlement: the ledger could not be opened (%s). TERRAIN ACCESS IS CLOSED."), *Result.ToString());
		return false;
	}

	// The boot settlement pass (P-003 Â§3): exactly what the journal recorded, in order, in batches.
	const FTerrainOpSeq Before = Ledger->GetWatermark();
	if (Inputs.Num() != int32(H - Before) || (Inputs.Num() > 0 && Inputs[0].OpSeq != Before + 1))
	{
		UE_LOG(LogTerrainCore, Error, TEXT("Settlement: the journal does not hold every record in (W=%llu, H=%llu]. TERRAIN ACCESS IS CLOSED."), Before, H);
		return false;
	}
	for (int32 Start = 0; Start < Inputs.Num(); Start += FTerrainSettlementWorker::MaxBatch)
	{
		const int32 Count = FMath::Min(FTerrainSettlementWorker::MaxBatch, Inputs.Num() - Start);
		Result = Ledger->Settle(TConstArrayView<FTerrainSettlementInput>(Inputs.GetData() + Start, Count));
		if (!Result.IsOk())
		{
			UE_LOG(LogTerrainCore, Error, TEXT("Settlement: the boot pass failed at OpSeq %llu (%s). TERRAIN ACCESS IS CLOSED."),
				Inputs[Start].OpSeq, *Result.ToString());
			return false;
		}
	}
	// The ledger's names -- the database, and the WAL SQLite created at the first write above --
	// exist now and will not change while the world runs. Make them durable before anything is
	// admitted, as bootstrap does for the terrain store's names (P-005 Â§6, P-010 Â§4).
	const ETerrainStorageResult Synced = StorageDevice->SyncDirectory(TEXT(""));
	if (Synced != ETerrainStorageResult::Ok)
	{
		UE_LOG(LogTerrainCore, Error, TEXT("Settlement: the world directory could not be synced (%s). TERRAIN ACCESS IS CLOSED."),
			TerrainStorageResultName(Synced));
		return false;
	}
	UE_LOG(LogTerrainCore, Log, TEXT("Settlement: %d records settled at boot (W %llu -> %llu, G=%llu, H=%llu)."),
		Inputs.Num(), Before, Ledger->GetWatermark(), G, H);

	TArray<FTerrainLedgerBalance> Balances;
	if (!Ledger->ReadBalances(Balances))
	{
		UE_LOG(LogTerrainCore, Error, TEXT("Settlement: the ledger's balances could not be read. TERRAIN ACCESS IS CLOSED."));
		return false;
	}
	for (const FTerrainLedgerBalance& Row : Balances)
	{
		SettledBalances.FindOrAdd(Row.OwnerId).FindOrAdd(Row.ItemId) += Row.Amount;
	}

	Settlement = MakeUnique<FTerrainSettlementWorker>();
#if !UE_BUILD_SHIPPING
	FParse::Value(FCommandLine::Get(), TEXT("TerrainSettleDelay="), Settlement->TestDelaySeconds);
#endif
	Settlement->Start(MoveTemp(Ledger), H);
	return true;
}

void UTerrainService::CloseLedger()
{
	check(IsInGameThread());
	if (Settlement)
	{
		Settlement->Stop();
		TickSettlement();   // fold the last results into the mirror before it goes
		Settlement.Reset();
	}
}

void UTerrainService::TickSettlement()
{
	if (!Settlement)
	{
		return;
	}
	if (!Settlement->Poll() && !bStorageFaulted)
	{
		// P-003 Â§2: after mutation, a settlement failure is an uncertain storage fault.
		bStorageFaulted = true;
		UE_LOG(LogTerrainCore, Error,
			TEXT("Settlement failed (%s). The edits are journaled and will settle at the next boot; admission is ")
			TEXT("CLOSED until then."), *Settlement->GetFailure().ToString());
	}
	TSet<uint64> Changed;
	for (const FTerrainSettlementInput& Input : Settlement->TakeSettled())
	{
		for (const FTerrainEconomyDelta& Delta : Input.Deltas)
		{
			SettledBalances.FindOrAdd(Delta.OwnerId).FindOrAdd(Delta.ItemId) += Delta.Count;
			Changed.Add(Delta.OwnerId);
		}
	}
	if (Changed.IsEmpty())
	{
		return;
	}
	// Owners hear their balance only after it is durable in the ledger, never from a promise.
	for (const auto& Entry : Streams)
	{
		UTerrainStreamComponent* Stream = Entry.Value.Get();
		const uint64 Owner = OwnerForSource(Entry.Key);
		if (!Stream || !Stream->bReady || !Changed.Contains(Owner)) continue;
		TArray<FTerrainYield> Balance;
		if (const TMap<uint32, int64>* Items = SettledBalances.Find(Owner))
		{
			for (const auto& Item : *Items)
			{
				FTerrainYield& Row = Balance.AddDefaulted_GetRef();
				Row.MaterialId = int32(Item.Key);
				Row.MicroLitres = Item.Value;
			}
		}
		Stream->ClientInventory(Balance);
	}
}

void UTerrainService::RunLedgerAudit()
{
	check(IsInGameThread());
	if (!Settlement || !WorldStore)
	{
		UE_LOG(LogTerrainCore, Error, TEXT("**** Terrain.LedgerAudit: FAIL no ledger is open ****"));
		return;
	}
	if (Settlement->Pending() != 0)
	{
		// Audit a quiet ledger: W must equal H for "every journaled credit is in it" to be checkable.
		FTimerHandle Retry; TWeakObjectPtr<UTerrainService> Weak(this);
		GetWorld()->GetTimerManager().SetTimer(Retry, FTimerDelegate::CreateLambda([Weak]() { if (Weak.IsValid()) Weak->RunLedgerAudit(); }), 0.2f, false);
		return;
	}

	// The truth, recomputed from nothing but the journal.
	TArray<FTerrainSettlementInput> All;
	bool bAnyEconomy = false; FTerrainOpSeq First = 0;
	const FTerrainStoreResult Read = TerrainReadSettlementInputs(*WorldStore, 0, All, bAnyEconomy, First);
	TMap<FString, int64> Expected;
	int64 PaidRecords = 0;
	for (const FTerrainSettlementInput& Input : All)
	{
		PaidRecords += Input.Deltas.Num() > 0;
		for (const FTerrainEconomyDelta& D : Input.Deltas)
		{
			Expected.FindOrAdd(FString::Printf(TEXT("%llu/%llu/%u"), D.OwnerId, D.ContainerId, D.ItemId)) += D.Count;
		}
	}

	TArray<FTerrainLedgerBalance> Balances; int64 Settlements = -1;
	const bool bRead = Settlement->ReadBalancesBlocking(Balances, Settlements);
	TMap<FString, int64> Actual;
	for (const FTerrainLedgerBalance& B : Balances)
	{
		Actual.Add(FString::Printf(TEXT("%llu/%llu/%u"), B.OwnerId, B.ContainerId, B.ItemId), B.Amount);
	}
	const FTerrainOpSeq H = WorldStore->GetJournal()->GetHead();
	bool bMatch = Read.IsOk() && bRead && Expected.Num() == Actual.Num() && Settlements == int64(H)
		&& Settlement->GetWatermark() == H && int64(All.Num()) == int64(H);
	for (const auto& E : Expected) bMatch &= Actual.FindRef(E.Key) == E.Value && Actual.Contains(E.Key);

	int64 Total = 0; for (const auto& E : Actual) Total += E.Value;
	for (const auto& E : Actual)
	{
		UE_LOG(LogTerrainCore, Display, TEXT("Terrain.LedgerAudit balance %s = %.3f L (journal says %.3f L)"),
			*E.Key, double(E.Value) / 1.0e6, double(Expected.FindRef(E.Key)) / 1.0e6);
	}
	UE_LOG(LogTerrainCore, Display,
		TEXT("**** Terrain.LedgerAudit: %s H=%llu W=%llu settlements=%lld paid records=%lld balances=%d total=%.3f L ****"),
		bMatch ? TEXT("PASS") : TEXT("FAIL"), H, Settlement->GetWatermark(), Settlements, PaidRecords, Actual.Num(), double(Total) / 1.0e6);
}

void UTerrainService::StartDigStress()
{
#if !UE_BUILD_SHIPPING
	// The kill test's digger: server-attributed edits, forever, down a spiral near the origin, so
	// a hard kill lands at an arbitrary point between journal, settlement and capture.
	// A fresh start point per launch, so repeated runs keep digging new ground and keep paying.
	DigStressCount = FMath::RandRange(0, 1 << 20);
	TWeakObjectPtr<UTerrainService> Weak(this);
	GetWorld()->GetTimerManager().SetTimer(DigStressHandle, FTimerDelegate::CreateLambda([Weak]()
	{
		UTerrainService* Self = Weak.Get();
		if (!Self || !Self->IsBackendReady()) return;
		const int32 N = Self->DigStressCount++;
		FTerrainEditRequest Dig;
		Dig.Kind = (N % 4 == 3) ? ETerrainEditKind::Add : ETerrainEditKind::Remove;
		const double Angle = N * 0.7;
		const double Ring = 300.0 + (N % 97) * 20.0;
		Dig.WorldLocation = FVector(FMath::Cos(Angle) * Ring, FMath::Sin(Angle) * Ring, -200.0 - (N % 40) * 25.0);
		Dig.RadiusCm = 150.0;
		FTerrainEditReceipt Receipt;
		Self->RequestEdit(Dig, Receipt);
	}), 0.05f, true);
	UE_LOG(LogTerrainCore, Display, TEXT("Terrain.DigStress: started"));
#endif
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld GLedgerAudit(TEXT("Terrain.LedgerAudit"),
	TEXT("Recompute every balance from the journal and compare it with the settlement ledger (P-010)."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		FTimerHandle Handle; TWeakObjectPtr<UWorld> Weak(World);
		if (World) World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([Weak]()
		{ if (UWorld* W = Weak.Get()) if (UTerrainService* S = W->GetSubsystem<UTerrainService>()) S->RunLedgerAudit(); }), 2.f, false);
	}));

static FAutoConsoleCommandWithWorld GDigStress(TEXT("Terrain.DigStress"),
	TEXT("Development: dig continuously as the server, for the settlement kill test."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		FTimerHandle Handle; TWeakObjectPtr<UWorld> Weak(World);
		if (World) World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([Weak]()
		{ if (UWorld* W = Weak.Get()) if (UTerrainService* S = W->GetSubsystem<UTerrainService>()) S->StartDigStress(); }), 2.f, false);
	}));
#endif

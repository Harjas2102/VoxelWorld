// Copyright VoxelWorld. See Docs/proposals/P-010-settlement-ledger.md.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "EntityLedger.h"
#include "TerrainSettlement.h"
#include "TerrainWorldStore.h"
#include "TerrainCommitJournal.h"
#include "TerrainMaterials.h"
#include "TerrainChunk.h"
#include "TerrainOpGeometry.h"
#include "MemoryTerrainBackend.h"
#include "ITerrainDensityField.h"
#include "TerrainPersistenceFixtures.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

/**
 * The settlement ledger and its recovery (P-003 §2, §3; P-010). Named in the TerrainCore suite so
 * the one command that runs the persistence tests runs these too.
 *
 * The assertion is always a BALANCE, never "no error": P-003's DEF-1 evidence is that a crash at
 * any point leaves neither missing nor duplicated ore, and only the numbers can say that.
 */

namespace EntityLedgerTest
{
	FString TempDir(const TCHAR* Name)
	{
		const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") / Name);
		IFileManager::Get().DeleteDirectory(*Dir, false, true);
		IFileManager::Get().MakeDirectory(*Dir, true);
		return Dir;
	}

	FTerrainSettlementInput Input(FTerrainOpSeq Seq, uint64 Owner, uint32 Item, int64 Count)
	{
		FTerrainSettlementInput In;
		In.OpSeq = Seq;
		In.EconomyKind = ETerrainEconomyKind::ExactDeltas;
		In.EconomyPolicyVersion = 1;
		In.Digest = TerrainPersistTest::MakeDigest(uint8(Seq));
		FTerrainEconomyDelta D; D.OwnerId = Owner; D.ItemId = Item; D.Count = Count;
		In.Deltas.Add(D);
		return In;
	}

	int64 Balance(const ITerrainSettlementLedger& L, uint64 Owner, uint32 Item)
	{
		TArray<FTerrainLedgerBalance> Rows; L.ReadBalances(Rows);
		for (const FTerrainLedgerBalance& R : Rows) if (R.OwnerId == Owner && R.ItemId == Item) return R.Amount;
		return 0;
	}

	class FFlatField final : public ITerrainDensityField
	{
	public:
		virtual FTerrainDensitySample Sample(FIntVector P) const override
		{
			FTerrainDensitySample Out;
			Out.Density = P.Z < 0 ? -1.f : 1.f;
			Out.MaterialId = P.Z < -6 ? ETerrainMaterial::IronOre : ETerrainMaterial::Stone;
			return Out;
		}
	};
}

// ==== the ledger ==================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEntityLedgerTest, "TerrainCore.Persistence.Settlement.Ledger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

bool FEntityLedgerTest::RunTest(const FString& Parameters)
{
	using namespace EntityLedgerTest;
	AddExpectedError(TEXT("Ledger:"), EAutomationExpectedErrorFlags::Contains, 0);

	const FString Dir = TempDir(TEXT("EntityLedgerTest"));
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Dir, false, true); };
	const FString Path = Dir / TEXT("ledger.db");
	const FTerrainPersistIdentity Identity = TerrainPersistTest::MakeIdentity();

	{
		FEntityLedger L;
		TestFalse(TEXT("Opening a missing ledger without create is refused"), L.Open(Path, Identity, false).IsOk());
		TestTrue (TEXT("Create"), L.Open(Path, Identity, true).IsOk());
		TestEqual(TEXT("A new ledger is at W=0"), L.GetWatermark(), (FTerrainOpSeq)0);
		const FTerrainSettlementInput Batch[] = { Input(1, 7, ETerrainMaterial::Stone, 1000), Input(2, 7, ETerrainMaterial::Stone, 250), Input(3, 9, ETerrainMaterial::IronOre, 40) };
		TestTrue (TEXT("Settle three records"), L.Settle(Batch).IsOk());
		TestEqual(TEXT("W advanced to 3"), L.GetWatermark(), (FTerrainOpSeq)3);
		TestEqual(TEXT("Owner 7 has 1250 stone"), Balance(L, 7, ETerrainMaterial::Stone), (int64)1250);
		TestEqual(TEXT("Owner 9 has 40 ore"), Balance(L, 9, ETerrainMaterial::IronOre), (int64)40);

		TestTrue (TEXT("Replaying settled records is a no-op"), L.Settle(Batch).IsOk());
		TestEqual(TEXT("and pays nothing twice"), Balance(L, 7, ETerrainMaterial::Stone), (int64)1250);

		FTerrainSettlementInput Forged = Input(2, 7, ETerrainMaterial::Stone, 999999);
		Forged.Digest = TerrainPersistTest::MakeDigest(200);
		TestFalse(TEXT("A settled OpSeq with another digest is corruption"), L.Settle(TArray<FTerrainSettlementInput>{Forged}).IsOk());
		TestFalse(TEXT("A gap above W is refused"), L.Settle(TArray<FTerrainSettlementInput>{Input(5, 7, ETerrainMaterial::Stone, 1)}).IsOk());
		TestEqual(TEXT("and neither moved W"), L.GetWatermark(), (FTerrainOpSeq)3);

		// Atomicity, at every statement: a batch of two records with two deltas is ~9 statements.
		int32 Failures = 0, Clean = 0;
		for (int32 At = 0; At < 12; ++At)
		{
			L.TestFailAtStatement = At;
			FTerrainSettlementInput A = Input(4, 7, ETerrainMaterial::Stone, 5);
			FTerrainEconomyDelta Extra; Extra.OwnerId = 9; Extra.ItemId = ETerrainMaterial::IronOre; Extra.Count = 3; A.Deltas.Add(Extra);
			const bool bOk = L.Settle(TArray<FTerrainSettlementInput>{A, Input(5, 9, ETerrainMaterial::Stone, 2)}).IsOk();
			if (bOk) { ++Clean; break; }
			++Failures;
			if (L.GetWatermark() != 3 || Balance(L, 7, ETerrainMaterial::Stone) != 1250 || Balance(L, 9, ETerrainMaterial::IronOre) != 40
				|| Balance(L, 9, ETerrainMaterial::Stone) != 0 || L.CountSettlements() != 3)
			{
				AddError(FString::Printf(TEXT("A failure at statement %d left part of the batch behind"), At));
			}
		}
		L.TestFailAtStatement = -1;
		AddInfo(FString::Printf(TEXT("Atomicity: %d injected failures, each rolled back whole"), Failures));
		TestTrue (TEXT("Every statement of the batch was failed once"), Failures >= 8);
		TestEqual(TEXT("then the batch settled"), Clean, 1);
		TestEqual(TEXT("to W=5"), L.GetWatermark(), (FTerrainOpSeq)5);
		TestEqual(TEXT("with both deltas of record 4"), Balance(L, 9, ETerrainMaterial::IronOre), (int64)43);

		// Fail closed on overflow: a credit is never clamped or dropped.
		TestFalse(TEXT("An overflowing credit fails the batch"), L.Settle(TArray<FTerrainSettlementInput>{Input(6, 7, ETerrainMaterial::Stone, MAX_int64)}).IsOk());
		TestEqual(TEXT("and leaves the balance alone"), Balance(L, 7, ETerrainMaterial::Stone), (int64)1255);
	}

	{
		FEntityLedger L;
		TestTrue (TEXT("Reopen"), L.Open(Path, Identity, false).IsOk());
		TestEqual(TEXT("W survives a close"), L.GetWatermark(), (FTerrainOpSeq)5);
		TestEqual(TEXT("and so do balances"), Balance(L, 7, ETerrainMaterial::Stone), (int64)1255);
		TestFalse(TEXT("Creating over an existing ledger is refused"), FEntityLedger().Open(Path, Identity, true).IsOk());
	}
	{
		FTerrainPersistIdentity Other = Identity; Other.Epoch.Bytes[0] ^= 0xFF;
		FEntityLedger L;
		TestFalse(TEXT("Another lineage's ledger is refused"), L.Open(Path, Other, false).IsOk());
	}
	return true;
}

// ==== the policy ===================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEntityEconomyPolicyTest, "TerrainCore.Persistence.Settlement.Policy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

bool FEntityEconomyPolicyTest::RunTest(const FString& Parameters)
{
	const TArray<FTerrainMaterialVolume> Dug = {
		{ETerrainMaterial::Stone, 5000}, {ETerrainMaterial::IronOre, 700}, {ETerrainMaterial::Fill, 300},
		{ETerrainMaterial::Air, 10}, {ETerrainMaterial::Unknown, 20}, {ETerrainMaterial::Dirt, -400} };
	FTerrainCommitEconomy E;
	TerrainComputeEconomy(42, 0, Dug, E);
	TestTrue(TEXT("A paid dig is ExactDeltas"), E.Kind == ETerrainEconomyKind::ExactDeltas);
	TestTrue(TEXT("at policy 1"), E.PolicyVersion == TerrainEconomyPolicyVersion);
	TestEqual(TEXT("Only stone and ore pay: fill, air, unknown and placements do not"), E.Deltas.Num(), 2);
	int64 Stone = 0, Ore = 0;
	for (const FTerrainEconomyDelta& D : E.Deltas)
	{
		TestEqual(TEXT("to the owner"), D.OwnerId, (uint64)42);
		TestTrue(TEXT("in the personal stock"), D.ContainerId == TerrainContainerPersonal);
		(D.ItemId == ETerrainMaterial::Stone ? Stone : Ore) += D.Count;
	}
	TestEqual(TEXT("Stone is paid exactly"), Stone, (int64)5000);
	TestEqual(TEXT("Ore is paid exactly"), Ore, (int64)700);
	TerrainComputeEconomy(TerrainOwnerNone, 0, Dug, E);
	TestTrue(TEXT("No owner, no economy"), E.Kind == ETerrainEconomyKind::NoEconomy);
	TerrainComputeEconomy(42, 3, Dug, E);
	TestTrue(TEXT("An unknown tool pays nothing"), E.Kind == ETerrainEconomyKind::NoEconomy);
	return true;
}

// ==== crash anywhere, recover from the journal ======================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEntitySettlementReplayTest, "TerrainCore.Persistence.Settlement.CommitCrash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

bool FEntitySettlementReplayTest::RunTest(const FString& Parameters)
{
	using namespace EntityLedgerTest;
	using namespace TerrainPersistTest;

	const FString Dir = TempDir(TEXT("EntitySettlementReplayTest"));
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Dir, false, true); };

	// A real world store (in memory) and a real journal, written by the real commit path with the
	// real policy, from real measured digs on the reference backend.
	FTerrainMemoryStorageDevice Device;
	const FTerrainPersistIdentity Identity = MakeIdentity();
	FTerrainWorldStore Store(Device);
	if (!Store.Create(MakeBaseDescriptor(), Identity.World, Identity.Epoch, 1).IsOk()) { AddError(TEXT("create")); return false; }
	FTerrainWorldStoreJournal Journal(Store);
	Journal.bPhysicalMeasured = true;

	TSharedPtr<FFlatField, ESPMode::ThreadSafe> Field = MakeShared<FFlatField, ESPMode::ThreadSafe>();
	FTerrainBackendInit Init;
	Init.GeneratorVersion = 7; Init.VoxelSizeCm = 50.f; Init.Role = ETerrainRole::Server;
	Init.WorldBoundsVox = FTerrainBox(FIntVector(-256), FIntVector(256));
	Init.DensityField = Field.Get(); Init.DensityFieldOwner = Field;
	FMemoryTerrainBackend Backend; Backend.Initialize(Init);
	FTerrainStreamingInterest Interest; Interest.InterestId = 1; Interest.RadiusCm = 4000; Backend.SetStreamingInterest(Interest);

	TArray<FTerrainSettlementInput> Live;
	TMap<uint32, int64> Truth;   // item -> amount, computed from the measured digs themselves
	const int32 Records = 24;
	for (int32 I = 1; I <= Records; ++I)
	{
		FTerrainOp Op;
		Op.Kind = (I % 5 == 0) ? ETerrainOpKind::Add : ETerrainOpKind::Remove;
		Op.Shape = ETerrainShape::Sphere; Op.Source = ETerrainSource::Player; Op.SourceId = 2;
		Op.CentreVox = FIntVector((I % 6) * 3 - 8, (I % 4) * 3 - 5, -3 - (I % 5));
		Op.RadiusVoxQ16 = 3 << 16; Op.OpSeq = FTerrainOpSeq(I);
		Op.MaterialId = Op.Kind == ETerrainOpKind::Add ? ETerrainMaterial::Fill : 0;
		FTerrainEditResult Result;
		if (!Backend.ApplyOp(Op, Result)) { AddError(TEXT("apply")); return false; }
		FTerrainCommitIdentity Id; Id.RequestId = uint32(I);
		TerrainComputeEconomy(/*Owner=*/77 + (I % 2), 0, Result.Removed, Id.Economy);
		for (const FTerrainEconomyDelta& D : Id.Economy.Deltas) Truth.FindOrAdd(D.ItemId) += D.Count;
		if (!Journal.RecordCommit(Op, Result, Id, {})) { AddError(TEXT("record")); return false; }
		Live.Add(Journal.TakeLastSettlement());
	}
	int64 TruthTotal = 0; for (const auto& T : Truth) TruthTotal += T.Value;
	TestTrue(TEXT("The digs actually paid something"), TruthTotal > 0 && Truth.Contains(ETerrainMaterial::Stone));

	// What the journal says is what was handed to the ledger live, byte for byte.
	{
		TArray<FTerrainSettlementInput> FromJournal; bool bAny = false; FTerrainOpSeq First = 0;
		TestTrue(TEXT("The journal reads back"), TerrainReadSettlementInputs(Store, 0, FromJournal, bAny, First).IsOk());
		TestTrue(TEXT("and records an economy"), bAny);
		TestEqual(TEXT("one input per record"), FromJournal.Num(), Records);
		bool bSame = FromJournal.Num() == Live.Num();
		for (int32 I = 0; bSame && I < Live.Num(); ++I)
			bSame &= FromJournal[I].OpSeq == Live[I].OpSeq && FromJournal[I].Digest == Live[I].Digest && FromJournal[I].Deltas.Num() == Live[I].Deltas.Num();
		TestTrue(TEXT("identical to the live inputs, digest included"), bSame);
	}

	// Crash after K settled records, for every K; recover by settling (W, H] from the journal.
	int32 Recovered = 0;
	for (int32 K = 0; K <= Records; ++K)
	{
		const FString Path = Dir / FString::Printf(TEXT("ledger-%02d.db"), K);
		{
			FEntityLedger L; L.Open(Path, Identity, true);
			if (K > 0) L.Settle(TConstArrayView<FTerrainSettlementInput>(Live.GetData(), K));
		}   // the crash: whatever was committed is all that exists
		FEntityLedger L;
		if (!L.Open(Path, Identity, false).IsOk()) { AddError(TEXT("reopen")); continue; }
		TArray<FTerrainSettlementInput> Tail; bool bAny = false; FTerrainOpSeq First = 0;
		TerrainReadSettlementInputs(Store, L.GetWatermark(), Tail, bAny, First);
		const bool bTailRight = Tail.Num() == Records - K;
		const bool bSettled = L.Settle(Tail).IsOk();
		// And a second boot replaying EVERYTHING must change nothing.
		TArray<FTerrainSettlementInput> All; TerrainReadSettlementInputs(Store, 0, All, bAny, First);
		const bool bReplay = L.Settle(All).IsOk();
		int64 Total = 0; TArray<FTerrainLedgerBalance> Rows; L.ReadBalances(Rows);
		bool bExact = true;
		TMap<uint32, int64> Got; for (const FTerrainLedgerBalance& R : Rows) { Got.FindOrAdd(R.ItemId) += R.Amount; Total += R.Amount; }
		for (const auto& T : Truth) bExact &= Got.FindRef(T.Key) == T.Value;
		if (bTailRight && bSettled && bReplay && bExact && Total == TruthTotal && L.GetWatermark() == Records && L.CountSettlements() == Records)
			++Recovered;
		else
			AddError(FString::Printf(TEXT("Crash after %d settled records did not recover exactly"), K));
	}
	AddInfo(FString::Printf(TEXT("Recovered exactly from %d of %d crash points; %.3f L paid"), Recovered, Records + 1, double(TruthTotal) / 1e6));
	TestEqual(TEXT("Every crash point recovers to the exact balances"), Recovered, Records + 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

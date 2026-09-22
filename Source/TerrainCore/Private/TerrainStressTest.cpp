// Copyright VoxelWorld. Development harness only (T-132): the gate-8 edit stress profile and E-6.
#include "TerrainService.h"
#include "TerrainSettings.h"
#include "TerrainMaterials.h"
#include "TerrainChunk.h"
#include "TerrainCore.h"

#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMemory.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

/**
 * THE PROFILE. A dedicated server runs -TerrainStress:
 *   Phase 1 -- 32 synthetic sources (the design point: 32 players at 3 ops/s = 96 ops/s) dig a
 *              100 m-radius region through the REAL queue, validation, journal, capture,
 *              retention and settlement, until TargetEdits have committed.
 *   Phase 2 -- they keep digging while a fresh client joins (E-6). The joiner's catch-up is
 *              timed: snapshots, bytes, wall time, its connection's total bytes.
 *   Phase 3 -- digging stops; once everything drains, the joiner hashes every edited chunk it
 *              holds (density and material) and the server compares; the ledger is audited.
 * Every second: committed ops, the longest service tick and frame, queue depth, unsettled
 * records and dirty chunks. At the end, one summary line per measurement.
 */

bool UTerrainService::IsStressTest() const
{
#if !UE_BUILD_SHIPPING
	return HasAuthority() && FParse::Param(FCommandLine::Get(), TEXT("TerrainStress"));
#else
	return false;
#endif
}

void UTerrainService::LogStressSummary(const TCHAR* Label)
{
	const FTerrainOpSeq Committed = EditQueue.NextSequence() - 1 - Stress.StartSeq;
	const double Elapsed = GetWorld()->GetTimeSeconds() - Stress.StartTime;
	FString Reject;
	for (const auto& R : Stress.Rejections) Reject += FString::Printf(TEXT(" %d:%d"), R.Key, R.Value);
	UE_LOG(LogTerrainCore, Display,
		TEXT("Stress.Summary %s: ops=%llu in %.1f s (%.1f ops/s); journal flush max %.2f ms avg %.3f ms, %.2f records/flush; apply max %.2f ms; ")
		TEXT("queue age max %.1f ms; service tick max %.1f ms; frame max %.1f ms; settle latency max %.1f ms avg %.1f ms; ")
		TEXT("unsettled max %d, window-full ticks %d; memory %.0f MB; rejections[%s ]"),
		Label, Committed, Elapsed, Elapsed > 0 ? Committed / Elapsed : 0.0,
		CommitStats.Max * 1000.0, CommitStats.Count ? CommitStats.Sum / CommitStats.Count * 1000.0 : 0.0,
		CommitStats.Count ? double(CommitStats.Records) / CommitStats.Count : 0.0,
		EditQueue.MaxApplySeconds() * 1000.0, EditQueue.MaxQueueAgeSeconds() * 1000.0,
		Stress.TickMaxMs, Stress.FrameMaxMs,
		Settlement ? Settlement->MaxLatencySeconds * 1000.0 : 0.0,
		Settlement && Settlement->LatencySamples ? Settlement->SumLatencySeconds / Settlement->LatencySamples * 1000.0 : 0.0,
		Stress.PendingMax, Stress.WindowFullTicks,
		double(FPlatformMemory::GetStats().UsedPhysical) / (1024.0 * 1024.0), *Reject);
}

void UTerrainService::TickStressTest(double TickSeconds)
{
#if !UE_BUILD_SHIPPING
	if (!IsStressTest() || Stress.bFinished || !IsBackendReady()) return;
	const double Now = GetWorld()->GetTimeSeconds();
	const TCHAR* Cmd = FCommandLine::Get();

	if (!Stress.bStarted)
	{
		FParse::Value(Cmd, TEXT("TerrainStressBots="), Stress.Bots);
		FParse::Value(Cmd, TEXT("TerrainStressEdits="), Stress.TargetEdits);
		FParse::Value(Cmd, TEXT("TerrainStressLive="), Stress.LiveSeconds);
		double RegionM = 100; FParse::Value(Cmd, TEXT("TerrainStressRegion="), RegionM);
		Stress.RegionCm = RegionM * 100.0;
		Stress.Bots = FMath::Clamp(Stress.Bots, 1, 64);
		for (int32 B = 0; B < Stress.Bots; ++B)
		{
			FTerrainSourceState Bot;
			Bot.PlacementMaterial = ETerrainMaterial::Fill;
			EditQueue.RegisterSource(StressBotBase + B, Bot);
			SourceOwners.Add(StressBotBase + B, 0xB0700000ull + B);   // each bot is paid as its own owner
			Stress.NextSubmit.Add(Now + B / (3.0 * Stress.Bots));   // stagger the 3/s cadence
			Stress.Rng.Emplace(0x51E55 + B);
		}
		Stress.bStarted = true; Stress.Phase = 1;
		Stress.StartTime = Stress.PhaseTime = Now; Stress.NextSecond = Now + 1.0;
		Stress.StartSeq = Stress.SecondStartSeq = EditQueue.NextSequence() - 1;
		Stress.MemoryStart = FPlatformMemory::GetStats().UsedPhysical;
		UE_LOG(LogTerrainCore, Display, TEXT("Stress: phase A -- %d sources, %d edits, %.0f m region, memory %.0f MB"),
			Stress.Bots, Stress.TargetEdits, Stress.RegionCm / 100.0, double(Stress.MemoryStart) / (1024.0 * 1024.0));
		return;
	}

	// ---- per-tick measurements -------------------------------------------------------------
	const double TickMs = TickSeconds * 1000.0, FrameMs = FApp::GetDeltaTime() * 1000.0;
	Stress.TickMaxMs = FMath::Max(Stress.TickMaxMs, TickMs); Stress.SecondTickMaxMs = FMath::Max(Stress.SecondTickMaxMs, TickMs);
	Stress.FrameMaxMs = FMath::Max(Stress.FrameMaxMs, FrameMs); Stress.SecondFrameMaxMs = FMath::Max(Stress.SecondFrameMaxMs, FrameMs);
	const int32 Unsettled = Settlement ? Settlement->Pending() : 0;
	Stress.PendingMax = FMath::Max(Stress.PendingMax, SettlementPendingPeak);   // sampled at every Submit, not per tick
	Stress.WindowFullTicks += Unsettled >= FTerrainSettlementWorker::MaxPending;
	if (Now >= Stress.NextSecond)
	{
		const FTerrainOpSeq Head = EditQueue.NextSequence() - 1;
		UE_LOG(LogTerrainCore, Display, TEXT("Stress.Second phase=%d ops=%llu tick_max=%.1fms frame_max=%.1fms queue=%d unsettled=%d dirty=%d"),
			Stress.Phase, Head - Stress.SecondStartSeq, Stress.SecondTickMaxMs, Stress.SecondFrameMaxMs,
			EditQueue.Depth(), Unsettled, DirtyChunks.Num());
		Stress.SecondStartSeq = Head; Stress.SecondTickMaxMs = Stress.SecondFrameMaxMs = 0; Stress.NextSecond = Now + 1.0;
	}

	// ---- the diggers ---------------------------------------------------------------------------
	if (Stress.Phase == 1 || Stress.Phase == 2)
	{
		const FVector Centre(-8228.0, 0.0, 0.0);   // the PlayerStart: the joiner spawns in the middle of it
		for (int32 B = 0; B < Stress.Bots; ++B)
		{
			if (Now < Stress.NextSubmit[B]) continue;
			Stress.NextSubmit[B] += 1.0 / 3.0;
			FRandomStream& R = Stress.Rng[B];
			const double Angle = R.FRandRange(0.0, 2.0 * PI), Dist = Stress.RegionCm * FMath::Sqrt(R.FRand());
			FTerrainEditRequest Req;
			Req.Kind = R.FRand() < 0.8f ? ETerrainEditKind::Remove : ETerrainEditKind::Add;
			Req.WorldLocation = Centre + FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, -R.FRandRange(50.0, 1200.0));
			Req.RadiusCm = R.FRandRange(150.0, 250.0);
			FTerrainOp Op; ETerrainEditRejection Failure = QuantiseRequest(Req, Op);
			Op.Source = ETerrainSource::Admin;   // no pawn: reach and clearance are player checks
			FTerrainEditReceipt Receipt;
			EditQueue.Submit(StressBotBase + B, Stress.RequestId++, Op, Now, QueueCallbacks(), Receipt,
				GetDefault<UTerrainSettings>()->MaxVoxelsPerOp, Failure);
			if (!Receipt.bQueued) ++Stress.Rejections.FindOrAdd(int32(Receipt.Rejection));
		}
	}

	const FTerrainOpSeq Committed = EditQueue.NextSequence() - 1 - Stress.StartSeq;
	if (Stress.Phase == 1 && Committed >= FTerrainOpSeq(Stress.TargetEdits))
	{
		Stress.PhaseASeconds = Now - Stress.StartTime; Stress.PhaseAOps = Committed;
		Stress.MemoryAfterA = FPlatformMemory::GetStats().UsedPhysical;
		LogStressSummary(TEXT("A"));
		UE_LOG(LogTerrainCore, Display, TEXT("Stress: phase B -- region ready; a client may join now (dirty=%d)"), DirtyChunks.Num());
		Stress.Phase = 2; Stress.PhaseTime = Now;
	}

	// ---- the joiner (E-6) ---------------------------------------------------------------------
	if (Stress.Phase == 2)
	{
		UTerrainStreamComponent* Joiner = Stress.JoinerSource ? Streams.FindRef(Stress.JoinerSource).Get() : nullptr;
		if (!Joiner)
		{
			for (const auto& S : Streams) if (UTerrainStreamComponent* St = S.Value.Get(); St && St->bReady)
			{ Stress.JoinerSource = S.Key; Stress.JoinTime = Now; Joiner = St;
			  UE_LOG(LogTerrainCore, Display, TEXT("Stress: joiner source %u ready"), S.Key); break; }
			if (!Joiner && Now - Stress.PhaseTime > 180.0)
			{ UE_LOG(LogTerrainCore, Error, TEXT("**** Stress: FAIL no client joined ****")); Stress.bFinished = true; }
			return;
		}
		if (!Stress.SyncedAt)
		{
			const bool bQuiet = Joiner->SnapshotQueue.IsEmpty() && Joiner->Syncing.IsEmpty() && Joiner->SnapshotsSentTo > 0;
			if (!bQuiet) Stress.QuietSince = 0;
			else if (!Stress.QuietSince) Stress.QuietSince = Now;
			else if (Now - Stress.QuietSince >= 1.0)
			{
				Stress.SyncedAt = Stress.QuietSince;
				const APlayerController* PC = Cast<APlayerController>(Joiner->GetOwner());
				const UNetConnection* Conn = PC ? PC->GetNetConnection() : nullptr;
				UE_LOG(LogTerrainCore, Display,
					TEXT("Stress.Join: caught up in %.2f s while editing continued: %d snapshots, %lld snapshot bytes; connection sent %d bytes so far"),
					Stress.SyncedAt - Stress.JoinTime, Joiner->SnapshotsSentTo, Joiner->SnapshotBytesTo, Conn ? Conn->OutTotalBytes : -1);
			}
		}
		if (Stress.SyncedAt && Now - Stress.SyncedAt >= Stress.LiveSeconds)
		{
			Stress.Phase = 3; Stress.PhaseTime = Now;
			UE_LOG(LogTerrainCore, Display, TEXT("Stress: phase C -- editing stops, draining"));
		}
		if (!Stress.SyncedAt && Now - Stress.JoinTime > 300.0)
		{ UE_LOG(LogTerrainCore, Error, TEXT("**** Stress: FAIL the joiner never caught up ****")); Stress.bFinished = true; }
		return;
	}

	// ---- verification ------------------------------------------------------------------------------
	if (Stress.Phase == 3)
	{
		UTerrainStreamComponent* Joiner = Streams.FindRef(Stress.JoinerSource).Get();
		if (!Joiner) { UE_LOG(LogTerrainCore, Error, TEXT("**** Stress: FAIL the joiner left ****")); Stress.bFinished = true; return; }
		if (EditQueue.Depth() != 0 || (Settlement && Settlement->Pending() != 0) || !Joiner->SnapshotQueue.IsEmpty() || !Joiner->Syncing.IsEmpty()) return;
		if (Now - Stress.PhaseTime < 2.0) return;   // let the last ops reach the client
		TArray<FIntVector> Batch;
		for (const FTerrainChunkKey& K : Joiner->Subscribed)
		{
			if (GetRevision(K) == 0) continue;   // only edited chunks: pristine ones prove nothing
			Batch.Add(FIntVector(K.X, K.Y, K.Z));
			if (Batch.Num() == 64) { Joiner->ClientStressVerify(Batch); ++Stress.VerifyOutstanding; Batch.Reset(); }
		}
		if (!Batch.IsEmpty()) { Joiner->ClientStressVerify(Batch); ++Stress.VerifyOutstanding; }
		LogStressSummary(TEXT("total"));
		UE_LOG(LogTerrainCore, Display, TEXT("Stress: verifying %d batches of edited chunks on the joiner"), Stress.VerifyOutstanding);
		Stress.Phase = 4;
	}
#endif
}

void UTerrainService::ReceiveStressHashes(UTerrainStreamComponent& Stream, const TArray<FIntVector>& Keys,
	const TArray<uint64>& Density, const TArray<uint64>& Materials)
{
#if !UE_BUILD_SHIPPING
	if (!IsStressTest() || Stress.Phase != 4 || Stream.SourceId != Stress.JoinerSource || Keys.Num() > 64
		|| Density.Num() != Keys.Num() || Materials.Num() != Keys.Num()) return;
	for (int32 I = 0; I < Keys.Num(); ++I)
	{
		const FTerrainChunkKey K(Keys[I].X, Keys[I].Y, Keys[I].Z);
		++Stress.VerifyChunks;
		if (Density[I] != HashChunk(K) || Materials[I] != HashChunkMaterials(K) || Density[I] == 0)
		{
			++Stress.VerifyMismatches;
			if (Stress.VerifyMismatches <= 8)
				UE_LOG(LogTerrainCore, Warning, TEXT("Stress: chunk (%d,%d,%d) differs on the joiner"), K.X, K.Y, K.Z);
		}
	}
	if (--Stress.VerifyOutstanding > 0) return;

	// The verdict is the WHOLE stack's, so every part has to pass (Codex review F4): the terrain
	// the joiner holds, the ledger against the journal, and the settlement window. A run with any
	// part of the stack switched off is an attribution measurement, not a correctness result. It
	// says PARTIAL and names what was off, and it can never read as PASS.
	const UTerrainSettings* Settings = GetDefault<UTerrainSettings>();
	FString Off;
	if (!Settings->bPersistEdits)                  Off += TEXT(" persistence");
	if (!Settings->bCheckpointCapture)             Off += TEXT(" checkpoints");
	if (Settings->SettlementModule.IsNone())       Off += TEXT(" settlement");
	const bool bLedgerExpected = Settings->bPersistEdits && !Settings->SettlementModule.IsNone();
	const ETerrainLedgerAudit Audit = bLedgerExpected ? RunLedgerAudit() : ETerrainLedgerAudit::Fail;
	const bool bTerrain = Stress.VerifyMismatches == 0 && Stress.VerifyChunks > 0;
	const bool bWindow  = SettlementPendingPeak <= FTerrainSettlementWorker::MaxPending;
	const TCHAR* Verdict = !bTerrain || !bWindow || (bLedgerExpected && Audit != ETerrainLedgerAudit::Pass)
		? TEXT("FAIL") : (Off.IsEmpty() ? TEXT("PASS") : TEXT("PARTIAL"));
	const TCHAR* AuditText = !bLedgerExpected ? TEXT("not run")
		: Audit == ETerrainLedgerAudit::Pass ? TEXT("PASS")
		: Audit == ETerrainLedgerAudit::Deferred ? TEXT("did not complete") : TEXT("FAIL");
	UE_LOG(LogTerrainCore, Display,
		TEXT("**** Stress: %s edited chunks verified=%d mismatches=%d; ledger audit %s; unsettled max %d of %d; ")
		TEXT("off:%s; phase A %llu ops in %.1f s ****"),
		Verdict, Stress.VerifyChunks, Stress.VerifyMismatches, AuditText, SettlementPendingPeak,
		FTerrainSettlementWorker::MaxPending, Off.IsEmpty() ? TEXT(" nothing") : *Off,
		Stress.PhaseAOps, Stress.PhaseASeconds);
	Stress.bFinished = true;
#endif
}

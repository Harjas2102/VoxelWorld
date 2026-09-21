// Copyright VoxelWorld. Real-process development harness; no shipping gameplay behavior.
#include "TerrainService.h"
#include "TerrainCore.h"
#include "TerrainChunk.h"
#include "TerrainMaterials.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

int32& UTerrainService::MultiplayerRoundsCompleted()
{
	static int32 Rounds=0;   // survives server travel, which replaces the subsystem
	return Rounds;
}
bool UTerrainService::IsMultiplayerTest() const
{
#if !UE_BUILD_SHIPPING
	return FParse::Param(FCommandLine::Get(),TEXT("TerrainMPTest"));
#else
	return false;
#endif
}
void UTerrainService::TickMultiplayerTest()
{
#if !UE_BUILD_SHIPPING
	if (!IsMultiplayerTest() || bMPFinished) return;
	TArray<UTerrainStreamComponent*> Players;
	int32 Active=0;
	for (const auto& E:Streams) if (auto* S=E.Value.Get(); S && S->bReady)
	{
		const auto* PC=Cast<APlayerController>(S->GetOwner());
		if (PC && PC->GetPawn()) { Players.Add(S); if (!S->bObserver) ++Active; }
	}
	Players.Sort([](const UTerrainStreamComponent& A,const UTerrainStreamComponent& B) { return A.SourceId<B.SourceId; });
	const bool NeedObserver=FParse::Param(FCommandLine::Get(),TEXT("TerrainMPExpectObserver"));
	if (!bMPStarted && Active==3 && (!NeedObserver || Players.Num()==4))
	{
		// Allow the initial pristine subscription/ack exchanges to finish before issuing edits.
		if (!MPStartTime) { MPStartTime=GetWorld()->GetTimeSeconds()+3.; return; }
		if (GetWorld()->GetTimeSeconds()<MPStartTime) return;
		float Duration=60; FParse::Value(FCommandLine::Get(),TEXT("TerrainMPDuration="),Duration);
		Duration=FMath::Clamp(Duration,5.f,120.f);
		int32 Index=0;
		for (auto* S:Players)
		{
			auto* PC=Cast<APlayerController>(S->GetOwner());
			APawn* Pawn=PC->GetPawn();
			Pawn->SetActorLocation(S->bObserver ? FVector(40000,0,250) : FVector(-8500,-400+Index*400,250),false,nullptr,ETeleportType::TeleportPhysics);
			if (auto* C=Cast<ACharacter>(Pawn)) C->GetCharacterMovement()->DisableMovement();
			RefreshSubscriptions(*S);
			S->ClientBeginTest(Index++,Duration,S->bObserver);
		}
		bMPStarted=true;
		UE_LOG(LogTerrainCore,Display,TEXT("MP.Convergence server begin: %d clients, duration %.1fs"),Players.Num(),Duration);
	}
	if (bMPStarted && !bMPVerifying && Active==3 && EditQueue.Depth()==0)
	{
		for (auto* S:Players) if (!S->bTestDone) return;
		// Whole chunks around the complete 5m test region, including untouched positions.
		TerrainChunkKeysForBox(FTerrainBox(FIntVector(-168,-8,-14),FIntVector(-152,8,0)),MPKeys);
		TArray<FIntVector> WireKeys;
		// Density hashes, then material hashes (P-009): both must converge.
		for (const auto& K:MPKeys) { MPHashes.Add(HashChunk(K)); WireKeys.Add(FIntVector(K.X,K.Y,K.Z)); }
		for (const auto& K:MPKeys) MPHashes.Add(HashChunkMaterials(K));
		int64 Ore=0;
		for (const auto& K:MPKeys)
		{
			FTerrainRegionData Region;
			if (Backend->ReadRegion(K,Region)) for (int32 I=0;I<TerrainChunkSampleCount;++I)
				Ore+=(uint16(Region.Payload[(TerrainChunkSampleCount+I)*2]) | uint16(Region.Payload[(TerrainChunkSampleCount+I)*2+1])<<8)==ETerrainMaterial::IronOre;
		}
		UE_LOG(LogTerrainCore,Display,TEXT("MP.Materials: %lld iron-ore samples in the test chunks (painted in round 1 only)"),Ore);
		bMPVerifying=true;
		for (auto* S:Players) S->ClientVerifyTest(WireKeys);
	}
#endif
}
void UTerrainService::ReceiveTestHashes(UTerrainStreamComponent* Stream,const TArray<uint64>& Hashes,int32 Applied,int32 Failures,int32 Snapshots,int32 Dropped)
{
#if !UE_BUILD_SHIPPING
	if (!bMPVerifying || bMPFinished || !Stream || Stream->bTestReported) return;
	Stream->bTestReported=true; ++MPReports;
	// A client told to lose an op must show the gap AND the repair: failures, then a snapshot,
	// then the same hashes as everyone else. Without the drop, any failure is a failure.
	const bool Repaired=Dropped>0 && Failures>0 && Snapshots>0;
	const bool Match=Stream->bObserver ? Applied==0 && Failures==0
		: Hashes==MPHashes && !Hashes.Contains(uint64(0)) && Applied>0 && (Dropped>0 ? Repaired : Failures==0);
	if (!Match) ++MPFailures;
	UE_LOG(LogTerrainCore,Display,TEXT("MP.Convergence source=%u match=%d applied=%d failures=%d snapshots=%d dropped=%d observer=%d"),
		Stream->SourceId,int32(Match),Applied,Failures,Snapshots,Dropped,int32(Stream->bObserver));
	int32 Expected=0; for (const auto& E:Streams) if (auto* S=E.Value.Get();S && S->bReady) ++Expected;
	if (MPReports==Expected)
	{
        bMPFinished=true;
        UE_LOG(LogTerrainCore,Display,TEXT("MP.Queue max age %.3f ms; max backend apply %.3f ms"),
            EditQueue.MaxQueueAgeSeconds()*1000.,EditQueue.MaxApplySeconds()*1000.);
		UE_LOG(LogTerrainCore,Display,TEXT("MP.Snapshots sent=%d bytes=%lld"),SnapshotsSent,SnapshotBytesSent);
		UE_LOG(LogTerrainCore,Display,TEXT("**** MP.Convergence: %s clients=%d chunks=%d committed=%llu ****"),MPFailures ? TEXT("FAIL") : TEXT("PASS"),MPReports,MPKeys.Num(),EditQueue.NextSequence()-1);
        int32& CompletedRounds=MultiplayerRoundsCompleted();
        int32 Rounds=1; FParse::Value(FCommandLine::Get(),TEXT("TerrainMPRounds="),Rounds);
        if (!MPFailures && ++CompletedRounds<FMath::Clamp(Rounds,1,3))
        {
            UE_LOG(LogTerrainCore,Display,TEXT("MP.Travel: reload after round %d"),CompletedRounds);
            FTimerHandle Handle;
            TWeakObjectPtr<UWorld> LiveWorld(GetWorld());
            GetWorld()->GetTimerManager().SetTimer(Handle,FTimerDelegate::CreateLambda([LiveWorld]()
            { if (auto* World=LiveWorld.Get()) World->ServerTravel(TEXT("/Game/ThirdPerson/Lvl_ThirdPerson"),false); }),1.f,false);
        }

	}
#endif
}

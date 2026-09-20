// Copyright VoxelWorld. Real-process development harness; no shipping gameplay behavior.
#include "TerrainService.h"
#include "TerrainCore.h"
#include "TerrainChunk.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

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
		for (const auto& K:MPKeys) { MPHashes.Add(HashChunk(K)); WireKeys.Add(FIntVector(K.X,K.Y,K.Z)); }
		bMPVerifying=true;
		for (auto* S:Players) S->ClientVerifyTest(WireKeys);
	}
#endif
}
void UTerrainService::ReceiveTestHashes(UTerrainStreamComponent* Stream,const TArray<uint64>& Hashes,int32 Applied,int32 Failures)
{
#if !UE_BUILD_SHIPPING
	if (!bMPVerifying || bMPFinished || !Stream || Stream->bTestReported) return;
	Stream->bTestReported=true; ++MPReports;
	const bool Match=Stream->bObserver ? Applied==0 && Failures==0
		: Hashes==MPHashes && !Hashes.Contains(uint64(0)) && Applied>0 && Failures==0;
	if (!Match) ++MPFailures;
	UE_LOG(LogTerrainCore,Display,TEXT("MP.Convergence source=%u match=%d applied=%d failures=%d observer=%d"),Stream->SourceId,int32(Match),Applied,Failures,int32(Stream->bObserver));
	int32 Expected=0; for (const auto& E:Streams) if (auto* S=E.Value.Get();S && S->bReady) ++Expected;
	if (MPReports==Expected)
	{
        bMPFinished=true;
        UE_LOG(LogTerrainCore,Display,TEXT("MP.Queue max age %.3f ms; max backend apply %.3f ms"),
            EditQueue.MaxQueueAgeSeconds()*1000.,EditQueue.MaxApplySeconds()*1000.);
		UE_LOG(LogTerrainCore,Display,TEXT("**** MP.Convergence: %s clients=%d chunks=%d committed=%llu ****"),MPFailures ? TEXT("FAIL") : TEXT("PASS"),MPReports,MPKeys.Num(),EditQueue.NextSequence()-1);
        static int32 CompletedRounds=0;
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

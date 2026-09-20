// Copyright VoxelWorld.
#include "TerrainStreamComponent.h"
#include "TerrainService.h"
#include "TerrainSettings.h"
#include "TerrainCore.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

UTerrainStreamComponent::UTerrainStreamComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick=true;
	PrimaryComponentTick.TickInterval=.02f;
}
void UTerrainStreamComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner()->HasAuthority()) if (auto* S=GetWorld()->GetSubsystem<UTerrainService>()) S->RegisterStream(this);
}
void UTerrainStreamComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (GetWorld()) if (auto* S=GetWorld()->GetSubsystem<UTerrainService>()) S->UnregisterStream(SourceId);
	Super::EndPlay(Reason);
}
void UTerrainStreamComponent::ClientSession_Implementation(const FTerrainSessionDescriptor& Descriptor)
{ Session=Descriptor; bHasSession=true; }
void UTerrainStreamComponent::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Tick)
{
	Super::TickComponent(Delta,Type,Tick);
	const auto* PC=Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalController()) return;
	auto* S=GetWorld()->GetSubsystem<UTerrainService>();
	if (bHasSession && !bSentReady && S && S->IsBackendReady())
	{
		const auto* Config=GetDefault<UTerrainSettings>();
		bSentReady=true;
		if (Session.Protocol!=1 || Session.Seed!=Config->Seed || Session.Generator!=Config->GeneratorVersion
			|| Session.VoxelSize!=Config->VoxelSizeCm || Session.Origin!=Config->TerrainOriginWorld || Session.Backend!=Config->BackendModule)
		{ UE_LOG(LogTerrainCore,Error,TEXT("Terrain session configuration mismatch; edits refused.")); return; }
		bReady=true;
		ServerReady(FParse::Param(FCommandLine::Get(),TEXT("TerrainMPObserver")));
	}
#if !UE_BUILD_SHIPPING
	if (bRunningTest)
	{
		const double Now=GetWorld()->GetTimeSeconds();
		if (Now>=TestEnd) { bRunningTest=false; ServerTestDone(); }
		else if (!bObserver && Now>=NextTestEdit)
		{
			FTerrainEditRequest R;
			R.Kind=(TestEdits%2) ? ETerrainEditKind::Add : ETerrainEditKind::Remove;
			R.WorldLocation=FVector(-8100+(TestEdits%5)*50,-100+((TestEdits/5)%5)*50,-300-((TestEdits/25)%3)*50);
			R.RadiusCm=200;
			FTerrainEditReceipt Receipt; SubmitEdit(R,Receipt);
			++TestEdits; NextTestEdit=Now+.36;
		}
	}
#endif
}
bool UTerrainStreamComponent::SubmitEdit(FTerrainEditRequest R,FTerrainEditReceipt& Receipt)
{
	Receipt={};
	const auto* PC=Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalController() || !bReady || NextRequestId==MAX_int64)
	{ Receipt.Rejection=ETerrainEditRejection::NotReady; return false; }
	R.RequestId=NextRequestId++; Receipt.RequestId=R.RequestId; Receipt.bQueued=true;
	ServerRequestEdit(R); return true;
}
void UTerrainStreamComponent::ServerRequestEdit_Implementation(const FTerrainEditRequest& R)
{
	FTerrainEditReceipt Receipt;
	if (auto* S=GetWorld()->GetSubsystem<UTerrainService>())
	{
		S->SubmitPlayerEdit(this,R,Receipt);

	}
}
void UTerrainStreamComponent::ClientEditReceipt_Implementation(const FTerrainEditReceipt& Receipt)
{
	LastReceipt=Receipt;
	if (!Receipt.bApplied) UE_LOG(LogTerrainCore,Verbose,TEXT("Terrain request %lld rejected: %d"),Receipt.RequestId,int32(Receipt.Rejection));
}
void UTerrainStreamComponent::ServerReady_Implementation(bool Observer)
{
	bReady=true;
	if (auto* S=GetWorld()->GetSubsystem<UTerrainService>()) bObserver=S->IsMultiplayerTest() && Observer;
}
void UTerrainStreamComponent::ClientPristine_Implementation(const TArray<FIntVector>& Keys)
{
	if (auto* S=GetWorld()->GetSubsystem<UTerrainService>(); S && S->AcceptPristine(Keys)) ServerAckPristine(Keys);
}
void UTerrainStreamComponent::ServerAckPristine_Implementation(const TArray<FIntVector>& Keys)
{
	if (Keys.Num()>64) return;
	for (const auto& P:Keys)
	{
		const FTerrainChunkKey K(P.X,P.Y,P.Z);
		if (PendingPristine.Remove(K)) Subscribed.Add(K);
	}
}
void UTerrainStreamComponent::ClientApplyOp_Implementation(const TArray<uint8>& Bytes,const TArray<FTerrainChunkRevision>& Revisions)
{
	// A listen host already shares the authoritative world; it must never apply twice.
	if (GetOwner()->HasAuthority()) return;
	++ReceivedOps;
	if (auto* S=GetWorld()->GetSubsystem<UTerrainService>(); S && S->ApplyReplicatedOp(Bytes,Revisions)) return;
	++ApplyFailures;
	TArray<FIntVector> Keys; for (const auto& V:Revisions) Keys.Add(V.Key);
	ServerRequestResync(Keys);
	UE_LOG(LogTerrainCore,Warning,TEXT("Terrain revision/contract gap: stopped applying op; resync required."));
}
void UTerrainStreamComponent::ServerRequestResync_Implementation(const TArray<FIntVector>& Keys)
{
	if (Keys.Num()>4096) return;
	for (const auto& P:Keys)
	{
		const FTerrainChunkKey K(P.X,P.Y,P.Z);
		Subscribed.Remove(K); PendingPristine.Remove(K); DeliveredRevisions.Remove(K);
	}
	UE_LOG(LogTerrainCore,Warning,TEXT("Terrain source %u needs modified-chunk resync (step 5); no false sync acknowledgement."),SourceId);
}
void UTerrainStreamComponent::ClientBeginTest_Implementation(int32 Index,float Duration,bool Observer)
{
#if !UE_BUILD_SHIPPING
	TestIndex=Index; bObserver=Observer; TestEdits=0; bRunningTest=true;
	TestEnd=GetWorld()->GetTimeSeconds()+Duration; NextTestEdit=GetWorld()->GetTimeSeconds()+.1*Index;
	if (const auto* PC=Cast<APlayerController>(GetOwner())) if (auto* C=Cast<ACharacter>(PC->GetPawn())) C->GetCharacterMovement()->DisableMovement();
	UE_LOG(LogTerrainCore,Display,TEXT("MP.Convergence client %d begin %.1fs observer=%d"),Index,Duration,int32(Observer));
#endif
}
void UTerrainStreamComponent::ServerTestDone_Implementation()
{
	if (auto* S=GetWorld()->GetSubsystem<UTerrainService>(); S && S->IsMultiplayerTest()) bTestDone=true;
}
void UTerrainStreamComponent::ClientVerifyTest_Implementation(const TArray<FIntVector>& Keys)
{
#if !UE_BUILD_SHIPPING
	if (Keys.Num()>64) return;
	TArray<uint64> Hashes;
	if (auto* S=GetWorld()->GetSubsystem<UTerrainService>()) for (const auto& K:Keys) Hashes.Add(S->HashChunk(FTerrainChunkKey(K.X,K.Y,K.Z)));
	UE_LOG(LogTerrainCore,Display,TEXT("MP.Convergence client %d finished: edits=%d applied=%d failures=%d chunks=%d"),TestIndex,TestEdits,ReceivedOps,ApplyFailures,Hashes.Num());
	ServerTestHashes(Hashes,ReceivedOps,ApplyFailures);
#endif
}
void UTerrainStreamComponent::ServerTestHashes_Implementation(const TArray<uint64>& Hashes,int32 Applied,int32 Failures)
{
	if (auto* S=GetWorld()->GetSubsystem<UTerrainService>(); S && S->IsMultiplayerTest()) S->ReceiveTestHashes(this,Hashes,Applied,Failures);
}

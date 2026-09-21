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
#if !UE_BUILD_SHIPPING
	// Harness only: pretend the Nth op was lost, so the gap detector and the snapshot repair
	// run against a real divergence rather than a hypothetical one (MP.Resync).
	int32 DropAt=0;
	if (FParse::Value(FCommandLine::Get(),TEXT("TerrainMPDropOp="),DropAt) && DropAt>0 && ReceivedOps+DroppedOps+1==DropAt && !DroppedOps)
	{ ++DroppedOps; UE_LOG(LogTerrainCore,Display,TEXT("MP.Resync: dropped op %d on purpose"),DropAt); return; }
#endif
	++ReceivedOps;
	auto* S=GetWorld()->GetSubsystem<UTerrainService>();
	TArray<FIntVector> Resync;
	const bool bUsable=S && S->ApplyReplicatedOp(Bytes,Revisions,Resync);
	if (!bUsable) { Resync.Reset(); for (const auto& V:Revisions) Resync.Add(V.Key); }
	if (Resync.IsEmpty()) return;
	++ApplyFailures;
	ServerRequestResync(Resync);
	UE_LOG(LogTerrainCore,Warning,TEXT("Terrain revision/contract gap in %d chunk(s); requesting resync."),Resync.Num());
}
void UTerrainStreamComponent::ClientChunkSnapshot_Implementation(const FTerrainSnapshotFragment& F)
{
	// The listen host's world IS the authoritative one.
	if (GetOwner()->HasAuthority()) { if (F.Index+1==F.Count) ServerAckSnapshot(F.Key,F.Generation,true); return; }
	auto* S=GetWorld()->GetSubsystem<UTerrainService>();
	FTerrainSnapshotFragmentView View;
	View.Key=FTerrainChunkKey(F.Key.X,F.Key.Y,F.Key.Z); View.Generation=F.Generation; View.Rev=F.Rev;
	View.Index=F.Index; View.Count=F.Count; View.TotalBytes=F.TotalBytes; View.Bytes=F.Bytes;
	TArray<uint8> Compressed; FTerrainChunkKey Discarded;
	switch (Assembler.Add(View,Compressed,Discarded))
	{
	case FTerrainSnapshotAssembler::EResult::Incomplete:
		return;
	case FTerrainSnapshotAssembler::EResult::Complete:
	{
		const double Started=FPlatformTime::Seconds();
		const bool bApplied=S && S->ApplyReplicatedSnapshot(View.Key,View.Rev,Compressed);
		if (bApplied)
		{
			++SnapshotsApplied;
			// The client-side cost of a join, per chunk: E-6 wants it measured, not assumed.
			UE_LOG(LogTerrainCore,Log,TEXT("Terrain snapshot (%d,%d,%d) rev %u installed: %d bytes in %.1f ms"),
				F.Key.X,F.Key.Y,F.Key.Z,F.Rev,Compressed.Num(),(FPlatformTime::Seconds()-Started)*1000.);
		}
		else UE_LOG(LogTerrainCore,Warning,TEXT("Terrain snapshot for chunk (%d,%d,%d) could not be applied."),F.Key.X,F.Key.Y,F.Key.Z);
		ServerAckSnapshot(F.Key,F.Generation,bApplied);
		return;
	}
	case FTerrainSnapshotAssembler::EResult::Rejected:
		if (S) S->MarkReplicaUnsynced(Discarded);
		ServerRequestResync({FIntVector(Discarded.X,Discarded.Y,Discarded.Z)});
		UE_LOG(LogTerrainCore,Warning,TEXT("Terrain snapshot fragment out of sequence; chunk (%d,%d,%d) requested again."),Discarded.X,Discarded.Y,Discarded.Z);
		return;
	}
}
void UTerrainStreamComponent::ServerAckSnapshot_Implementation(FIntVector Key,uint32 Generation,bool bApplied)
{
	if (auto* S=GetWorld()->GetSubsystem<UTerrainService>()) S->ReceiveSnapshotAck(*this,FTerrainChunkKey(Key.X,Key.Y,Key.Z),Generation,bApplied);
}
void UTerrainStreamComponent::ServerRequestResync_Implementation(const TArray<FIntVector>& Keys)
{
	if (Keys.Num()>4096) return;
	for (const auto& P:Keys)
	{
		const FTerrainChunkKey K(P.X,P.Y,P.Z);
		Subscribed.Remove(K); PendingPristine.Remove(K); DeliveredRevisions.Remove(K);
		SnapshotQueue.Remove(K);
		if (const FSnapshotInFlight* Flight=Syncing.Find(K)) { SnapshotBytesInFlight-=Flight->Bytes; Syncing.Remove(K); }
	}
	// The next subscription refresh re-sends each chunk: a snapshot if it has been edited, the
	// 12-byte pristine notice if it has not (P-008 §5).
	UE_LOG(LogTerrainCore,Warning,TEXT("Terrain source %u requested resync of %d chunk(s)."),SourceId,Keys.Num());
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
	UE_LOG(LogTerrainCore,Display,TEXT("MP.Convergence client %d finished: edits=%d applied=%d failures=%d snapshots=%d dropped=%d chunks=%d"),
		TestIndex,TestEdits,ReceivedOps,ApplyFailures,SnapshotsApplied,DroppedOps,Hashes.Num());
	ServerTestHashes(Hashes,ReceivedOps,ApplyFailures,SnapshotsApplied,DroppedOps);
#endif
}
void UTerrainStreamComponent::ServerTestHashes_Implementation(const TArray<uint64>& Hashes,int32 Applied,int32 Failures,int32 Snapshots,int32 Dropped)
{
	if (auto* S=GetWorld()->GetSubsystem<UTerrainService>(); S && S->IsMultiplayerTest()) S->ReceiveTestHashes(this,Hashes,Applied,Failures,Snapshots,Dropped);
}

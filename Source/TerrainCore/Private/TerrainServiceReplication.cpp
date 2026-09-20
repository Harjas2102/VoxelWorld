// Copyright VoxelWorld. Step 3 live transport; snapshot/resync transfer remains step 5.
#include "TerrainService.h"
#include "TerrainCommitJournal.h"
#include "TerrainChunk.h"
#include "TerrainCore.h"
#include "TerrainQuantise.h"
#include "TerrainSettings.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

uint32 UTerrainService::RegisterStream(UTerrainStreamComponent* Stream)
{
	if (!IsBackendReady() || !HasAuthority() || !Stream || !Cast<APlayerController>(Stream->GetOwner())) return 0;
	if (Stream->SourceId) return Stream->SourceId;
	if (NextSourceId == MAX_uint32) return 0;
	const uint32 Id=NextSourceId++;
	Streams.Add(Id,Stream); EditQueue.RegisterSource(Id,FTerrainSourceState()); Stream->SourceId=Id;
	const auto* S=GetDefault<UTerrainSettings>();
	FTerrainSessionDescriptor D; D.Seed=S->Seed; D.Generator=S->GeneratorVersion; D.VoxelSize=S->VoxelSizeCm;
	D.Origin=S->TerrainOriginWorld; D.Backend=S->BackendModule;
	Stream->ClientSession(D);
	return Id;
}
void UTerrainService::UnregisterStream(uint32 Id)
{
	if (Id) { EditQueue.Disconnect(Id); Streams.Remove(Id); }
}
void UTerrainService::TickService()
{
	if (!IsBackendReady() || !HasAuthority()) return;
	for (auto It=GetWorld()->GetPlayerControllerIterator();It;++It)
	{
		APlayerController* PC=It->Get(); if (!PC) continue;
		auto* Stream=PC->FindComponentByClass<UTerrainStreamComponent>();
		if (!Stream)
		{
			Stream=NewObject<UTerrainStreamComponent>(PC,TEXT("TerrainStream"));
			PC->AddInstanceComponent(Stream); Stream->RegisterComponent();
		}
		RegisterStream(Stream);
	}
	const double Now=GetWorld()->GetTimeSeconds();
	if (Now>=NextSubscriptionUpdate)
	{
		for (const auto& Entry:Streams) if (auto* Stream=Entry.Value.Get()) RefreshSubscriptions(*Stream);
		NextSubscriptionUpdate=Now+.5;
	}
	EditQueue.Pump(Now,QueueCallbacks());
	TickMultiplayerTest();
}
ETerrainEditRejection UTerrainService::QuantiseRequest(const FTerrainEditRequest& R, FTerrainOp& Op) const
{
	Op={}; Op.Source=ETerrainSource::Player;
	if (R.WorldLocation.ContainsNaN() || !FMath::IsFinite(R.RadiusCm) || R.RadiusCm<=0 || R.ToolId<0
		|| (R.Kind!=ETerrainEditKind::Remove && R.Kind!=ETerrainEditKind::Add)) return ETerrainEditRejection::BadRequest;
	Op.Kind=R.Kind==ETerrainEditKind::Remove ? ETerrainOpKind::Remove : ETerrainOpKind::Add;
	Op.ToolId=uint32(R.ToolId);
	if (!QuantiseEdit(R.WorldLocation,ActiveInit.OriginTransform,ActiveInit.VoxelSizeCm,Op.CentreVox)) return ETerrainEditRejection::OutOfBounds;
	const double Radius=FMath::Min(R.RadiusCm,GetDefault<UTerrainSettings>()->MaxEditRadiusCm);
	Op.RadiusVoxQ16=QuantiseRadiusQ16(Radius,ActiveInit.VoxelSizeCm);
	return Op.RadiusVoxQ16>0 ? ETerrainEditRejection::None : ETerrainEditRejection::BadRequest;
}
bool UTerrainService::SubmitPlayerEdit(UTerrainStreamComponent* Stream,const FTerrainEditRequest& R,FTerrainEditReceipt& Receipt)
{
	Receipt={}; Receipt.RequestId=R.RequestId;
	if (!IsInGameThread()) { Receipt.Rejection=ETerrainEditRejection::BadRequest; return false; }
    const auto Refuse = [&](ETerrainEditRejection Why)
    { Receipt.Rejection=Why; if (Stream) Stream->ClientEditReceipt(Receipt); return false; };
    if (!IsBackendReady()) return Refuse(State==ETerrainServiceState::Draining ? ETerrainEditRejection::ShuttingDown : ETerrainEditRejection::NotReady);
    if (!HasAuthority() || !Stream || !Stream->SourceId || Streams.FindRef(Stream->SourceId).Get()!=Stream)
        return Refuse(ETerrainEditRejection::NoAuthority);
    if (R.RequestId<=0) return Refuse(ETerrainEditRejection::BadRequest);
	FTerrainOp Op; auto Failure=QuantiseRequest(R,Op);
	if (!Stream->bReady) Failure=ETerrainEditRejection::NotReady;
	return EditQueue.Submit(Stream->SourceId,R.RequestId,Op,GetWorld()->GetTimeSeconds(),QueueCallbacks(),Receipt,
		GetDefault<UTerrainSettings>()->MaxVoxelsPerOp,Failure);
}
ETerrainEditRejection UTerrainService::ValidateOp(const FTerrainOp& Op,const FTerrainSourceState& Source) const
{
	FTerrainBox B; int64 W,Scans;
	// P-003 §2: after an uncertain storage fault, admission closes. The in-memory world can no
	// longer be shown to match what is on disk, so serving edits from it would be serving a
	// world nobody can get back. ShuttingDown tells the client not to retry.
	if (bStorageFaulted) return ETerrainEditRejection::ShuttingDown;
	if (!IsBackendReady()) return ETerrainEditRejection::NotReady;
	if (!TerrainOpBounds(Op,B)) return ETerrainEditRejection::BadRequest;
	for (int32 A=0;A<3;++A) if (B.Min[A]<ActiveInit.WorldBoundsVox.Min[A] || B.Max[A]>ActiveInit.WorldBoundsVox.Max[A])
		return ETerrainEditRejection::OutOfBounds;
	if (!TerrainOpCounts(Op,GetDefault<UTerrainSettings>()->MaxVoxelsPerOp,W,Scans)) return ETerrainEditRejection::TooLarge;
	TArray<FTerrainChunkKey> Keys; if (!TerrainChunkKeysForBox(B,Keys)) return ETerrainEditRejection::TooLarge;
	for (const auto& K:Keys)
	{
		if (!Backend->IsRegionResident(K)) return ETerrainEditRejection::NotResident;
		if (GetRevision(K)==MAX_uint32) return ETerrainEditRejection::RevisionExhausted;
	}
	if (!Source.bPermitted) return ETerrainEditRejection::PermissionDenied;
	if (Op.Source==ETerrainSource::Player)
	{
		const FVector Target=ActiveInit.OriginTransform.TransformPosition(FVector(Op.CentreVox)*ActiveInit.VoxelSizeCm);
		if (FVector::DistSquared(Target,Source.Position)>FMath::Square(Source.ReachCm)) return ETerrainEditRejection::OutOfReach;
		if (double(Op.RadiusVoxQ16)/65536.*ActiveInit.VoxelSizeCm>Source.MaxRadiusCm) return ETerrainEditRejection::ToolUnavailable;
		// Admission does not invent an inventory: tool 0 is the existing nonconsuming prototype
		// tool, all other IDs are refused. Zone policy is currently whole-world permission.
		// Prevent placement inside any pawn; removal/collision readiness remains DEF-8.
		if (Op.Kind==ETerrainOpKind::Add)
		{
			for (TActorIterator<APawn> It(GetWorld());It;++It)
			{
				if (!IsValid(*It)) continue;
				FVector Centre,Extent; It->GetActorBounds(true,Centre,Extent);
				const FVector Low=ActiveInit.OriginTransform.InverseTransformPosition(Centre-Extent)/ActiveInit.VoxelSizeCm;
				const FVector High=ActiveInit.OriginTransform.InverseTransformPosition(Centre+Extent)/ActiveInit.VoxelSizeCm;
				const FBox PawnBox(Low,High);
				for (int32 Z=B.Min.Z;Z<B.Max.Z;++Z) for (int32 Y=B.Min.Y;Y<B.Max.Y;++Y) for (int32 X=B.Min.X;X<B.Max.X;++X)
					if (TerrainOpContains(Op,FIntVector(X,Y,Z)) && PawnBox.Intersect(FBox(FVector(X,Y,Z),FVector(X+1,Y+1,Z+1))))
						return ETerrainEditRejection::UnsafePlacement;
			}
		}
	}
	return ETerrainEditRejection::None;
}
FTerrainQueueCallbacks UTerrainService::QueueCallbacks()
{
	FTerrainQueueCallbacks Cb;
	Cb.Refresh=[this](uint32 Id,FTerrainSourceState& S)
	{
		if (Id==1) return;
		auto* Stream=Streams.FindRef(Id).Get();
		auto* PC=Stream ? Cast<APlayerController>(Stream->GetOwner()) : nullptr;
		APawn* Pawn=PC ? PC->GetPawn() : nullptr;
		S.bConnected=IsValid(PC) && IsValid(Pawn) && Stream->bReady;
		if (Pawn) S.Position=Pawn->GetActorLocation();
		S.MaxRadiusCm=GetDefault<UTerrainSettings>()->MaxEditRadiusCm;
	};
	Cb.Validate=[this](const FTerrainOp& Op,const FTerrainSourceState& S) { return ValidateOp(Op,S); };
	Cb.Apply=[this](const FTerrainOp& Op,FTerrainEditResult& R) { return IsBackendReady() && Backend->ApplyOp(Op,R); };
	Cb.Commit=[this](const FTerrainOp& Op,const FTerrainEditResult& R,const FTerrainCommitIdentity& Id)
	{ return CommitOp(Op,R,Id); };
	Cb.Receipt=[this](uint32 Id,const FTerrainEditReceipt& R)
	{
		if (Id==1) LastAdminReceipt=R;
		else if (auto* Stream=Streams.FindRef(Id).Get()) Stream->ClientEditReceipt(R);
	};
	return Cb;
}
bool UTerrainService::CommitOp(const FTerrainOp& Op,const FTerrainEditResult& R,const FTerrainCommitIdentity& Identity)
{
    FTerrainBox B;
    TArray<FTerrainChunkKey> Keys;
    // Required work must never live inside check(): shipping builds compile checks out.
    if (R.bTruncated || !TerrainOpBounds(Op,B) || !TerrainChunkKeysForBox(B,Keys))
    { UE_LOG(LogTerrainCore,Fatal,TEXT("Committed terrain operation violated its geometry contract.")); return false; }
	TArray<FTerrainChunkRevision> Revisions;
	for (const auto& K:Keys) { FTerrainChunkRevision V; V.Key=FIntVector(K.X,K.Y,K.Z); V.Before=GetRevision(K); Revisions.Add(V); }
    for (const auto& K:R.AffectedChunks) if (!Keys.Contains(K))
    { UE_LOG(LogTerrainCore,Fatal,TEXT("Terrain backend changed a chunk outside the validated footprint.")); return false; }
    if (!TryAdvanceRevisions(R.AffectedChunks))
    { UE_LOG(LogTerrainCore,Fatal,TEXT("Prevalidated terrain revision commit failed.")); return false; }
	for (auto& V:Revisions) V.After=GetRevision(FTerrainChunkKey(V.Key.X,V.Key.Y,V.Key.Z));

	// P-003 §2 step 2, and the ONE ordering this function exists to enforce: the record is
	// durable before anything is told the edit happened.
	//
	// The revision index advances just above rather than just below, because the record has to
	// carry the TRUE after-revisions and the index is the only authority for them. That is a
	// deviation from the literal step order and it is safe for one stated reason: the index is
	// in-memory, and P-003 §2 discards unbroadcast provisional RAM on a storage fault. What
	// must not happen -- publishing before the flush -- cannot happen here.
	if (CommitJournal != nullptr)
	{
		TArray<FTerrainChunkRevision> Changed;
		Changed.Reserve(R.AffectedChunks.Num());
		for (const auto& V:Revisions) if (V.After != V.Before) Changed.Add(V);

		if (!CommitJournal->RecordCommit(Op,R,Identity,Changed))
		{
			bStorageFaulted = true;
			UE_LOG(LogTerrainCore,Error,
				TEXT("Terrain commit could not be made durable at OpSeq %llu. Admission is closed ")
				TEXT("and this world must be restarted from disk; the edit was NOT broadcast."),
				Op.OpSeq);
			return false;
		}
	}

    NextOpSeq=Op.OpSeq+1;
	TArray<uint8> Bytes; SerializeTerrainOp(Op,Bytes);
	for (const auto& Entry:Streams)
	{
		auto* Stream=Entry.Value.Get(); if (!Stream || !Stream->bReady) continue;
		bool Relevant=false;
		for (const auto& K:R.AffectedChunks) if (Stream->Subscribed.Contains(K) || Stream->PendingPristine.Contains(K)) { Relevant=true; break; }
		if (Relevant)
		{
			Stream->ClientApplyOp(Bytes,Revisions);
			for (const auto& V:Revisions) Stream->DeliveredRevisions.Add(FTerrainChunkKey(V.Key.X,V.Key.Y,V.Key.Z),V.After);
		}
	}
	return true;
}
bool UTerrainService::ApplyReplicatedOp(const TArray<uint8>& Bytes,const TArray<FTerrainChunkRevision>& Revisions)
{
	if (!IsBackendReady() || HasAuthority() || Bytes.Num()!=TerrainOpEncodedSize) return false;
	FTerrainOp Op; FTerrainBox B; int64 W,Scans;
	if (!DeserializeTerrainOp(Bytes,Op) || !Op.OpSeq || !TerrainOpBounds(Op,B)
		|| !TerrainOpCounts(Op,GetDefault<UTerrainSettings>()->MaxVoxelsPerOp,W,Scans)) return false;
	TArray<FTerrainChunkKey> Keys; if (!TerrainChunkKeysForBox(B,Keys) || Keys.Num()!=Revisions.Num()) return false;
	const auto NeedResync = [&]() { for (const auto& K:Keys) ResyncRequired.Add(K); return false; };
    TSet<FTerrainChunkKey> Seen; TArray<FTerrainChunkKey> Changed;
	for (const auto& V:Revisions)
	{
		const FTerrainChunkKey K(V.Key.X,V.Key.Y,V.Key.Z);
		if (Seen.Contains(K) || !Keys.Contains(K) || V.After<V.Before || uint64(V.After)>uint64(V.Before)+1) return NeedResync();
		Seen.Add(K);
		if (ResyncRequired.Contains(K) || GetRevision(K)!=V.Before) { return NeedResync(); }
		if (V.After!=V.Before) Changed.Add(K);
	}
	FTerrainEditResult R;
	if (!Backend->ApplyOp(Op,R) || R.bTruncated) return NeedResync();
	if (R.AffectedChunks.Num()!=Changed.Num()) return NeedResync();
	for (const auto& K:R.AffectedChunks) if (!Changed.Contains(K)) return NeedResync();
	return RevisionIndex->TryBumpRevisions(Changed);
}
bool UTerrainService::AcceptPristine(const TArray<FIntVector>& Keys)
{
	if (!IsBackendReady() || Keys.Num()>64) return false;
	for (const auto& K:Keys) if (GetRevision(FTerrainChunkKey(K.X,K.Y,K.Z))!=0 || ResyncRequired.Contains(FTerrainChunkKey(K.X,K.Y,K.Z))) return false;
	return true;
}
uint64 UTerrainService::HashChunk(const FTerrainChunkKey& Key) const
{ return IsBackendReady() ? Backend->HashRegion(Key) : 0; }

void UTerrainService::RefreshSubscriptions(UTerrainStreamComponent& Stream)
{
	auto* PC=Cast<APlayerController>(Stream.GetOwner()); APawn* Pawn=PC ? PC->GetPawn() : nullptr;
	if (!Stream.bReady || !Pawn) return;
	const FVector Position=ActiveInit.OriginTransform.InverseTransformPosition(Pawn->GetActorLocation())/ActiveInit.VoxelSizeCm;
	const double Radius=GetDefault<UTerrainSettings>()->DefaultInterestRadiusCm/ActiveInit.VoxelSizeCm;
	const auto Distance=[&](const FTerrainChunkKey& K)
	{
		const auto B=TerrainChunkBounds(K); return FBox(FVector(B.Min),FVector(B.Max)).ComputeSquaredDistanceToPoint(Position);
	};
	for (auto It=Stream.Subscribed.CreateIterator();It;++It) if (Distance(*It)>FMath::Square(Radius*1.5)) It.RemoveCurrent();
	for (auto It=Stream.PendingPristine.CreateIterator();It;++It) if (Distance(*It)>FMath::Square(Radius*1.5)) It.RemoveCurrent();
	TArray<FIntVector> Batch;
	// The finite prototype world bounds the scan; avoid coordinates derived from client input.
	const auto World=ActiveInit.WorldBoundsVox;
	for (int32 Z=TerrainChunkCoordinate(World.Min.Z);Z<=TerrainChunkCoordinate(World.Max.Z-1);++Z)
	for (int32 Y=TerrainChunkCoordinate(World.Min.Y);Y<=TerrainChunkCoordinate(World.Max.Y-1);++Y)
	for (int32 X=TerrainChunkCoordinate(World.Min.X);X<=TerrainChunkCoordinate(World.Max.X-1);++X)
	{
		FTerrainChunkKey K(X,Y,Z);
		if (Distance(K)>Radius*Radius || Stream.Subscribed.Contains(K) || Stream.PendingPristine.Contains(K)) continue;
		if (const uint32* Delivered=Stream.DeliveredRevisions.Find(K); Delivered && *Delivered==GetRevision(K))
		{ Stream.Subscribed.Add(K); continue; }
		if (GetRevision(K)!=0) continue; // Modified-chunk catch-up needs the step-5 protocol.
		Stream.PendingPristine.Add(K); Batch.Add(FIntVector(X,Y,Z));
		if (Batch.Num()==64) { Stream.ClientPristine(Batch); Batch.Reset(); }
	}
	if (!Batch.IsEmpty()) Stream.ClientPristine(Batch);
}

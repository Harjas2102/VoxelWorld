// Copyright VoxelWorld. Step 3 live transport; step 5 modified-chunk sync and resync (P-008).
#include "TerrainService.h"
#include "TerrainCommitJournal.h"
#include "TerrainChunk.h"
#include "TerrainCore.h"
#include "TerrainQuantise.h"
#include "TerrainSettings.h"
#include "TerrainChunkSnapshot.h"
#include "TerrainMaterials.h"
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
	PumpSnapshots();
	EditQueue.Pump(Now,QueueCallbacks());

	// After the pump, never inside it: a capture taken mid-transaction would not be a cut.
	MaybeCaptureCheckpoint();
	MaybeCollect();

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
    if (!IsBackendReady()) return Refuse((bStorageFaulted || State==ETerrainServiceState::Draining) ? ETerrainEditRejection::ShuttingDown : ETerrainEditRejection::NotReady);
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
	// Count the complete prospective footprint before mutation, not just today's set size.
	// Capture disabled has no dirty-budget gate: otherwise it would eventually deadlock.
	if (CommitJournal && GetDefault<UTerrainSettings>()->bCheckpointCapture && !bCheckpointDisabled)
	{
		// The capture in flight still owes chunks that are NOT in DirtyChunks any more, and
		// they cost memory in the open pack just as dirty ones do. Counting only today's set
		// would let a world hold up to twice the bound it advertises.
		int32 Prospective = DirtyChunks.Num() + CapturePump.Remaining();
		for (const auto& K : Keys) if (!DirtyChunks.Contains(K)) ++Prospective;
		if (Prospective > TerrainCheckpointDirtyHardBound) return ETerrainEditRejection::QueueFull;
	}
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
		// What a player places is server state, never client input (DEF-7), and stays Unknown
		// until inventory decides it (DEF-6, T-131). The multiplayer harness alone places iron
		// ore -- never found at the test depth -- and only in round 1, so the ore later rounds'
		// clients hold can only have arrived by snapshot: the material hashes then test
		// snapshot materials rather than a repeat of the edit script.
		S.PlacementMaterial=(IsMultiplayerTest() && MultiplayerRoundsCompleted()==0) ? FTerrainMatId(ETerrainMaterial::IronOre) : FTerrainMatId(0);
	};
	Cb.Validate=[this](const FTerrainOp& Op,const FTerrainSourceState& S) { return ValidateOp(Op,S); };
	Cb.Apply=[this](const FTerrainOp& Op,FTerrainEditResult& R)
	{
		if (!IsBackendReady()) return false;
		if (WorldStore)
		{
			FTerrainBox Bounds;
			TArray<FTerrainChunkKey> Keys;
			if (!TerrainOpBounds(Op, Bounds) || !TerrainChunkKeysForBox(Bounds, Keys)) return false;
			for (const auto& K : Keys) LivePersistencePins.Pin(*Backend, K, WorldStore->GetState().Base);

			// Copy-before-write (P-003 §4). Any of these chunks an in-progress capture still
			// owes is encoded HERE, from its pre-edit state, before the backend is allowed to
			// change it. Without this the capture would later read a chunk that had moved past
			// its own cut, and the checkpoint would record a world that never existed at G.
			CapturePump.NoticeWrite(Keys);
		}
		return Backend->ApplyOp(Op,R);
	};
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

	// The dirty set is what the next checkpoint will capture. Tracked only when there is
	// somewhere to capture TO: without a journal there is no checkpoint to owe.
	if (CommitJournal != nullptr)
	{
		for (const auto& K:R.AffectedChunks) DirtyChunks.Add(K, Op.OpSeq);
	}

	TArray<uint8> Bytes; SerializeTerrainOp(Op,Bytes);
	for (const auto& Entry:Streams)
	{
		auto* Stream=Entry.Value.Get(); if (!Stream || !Stream->bReady) continue;
		// A chunk with a snapshot in flight is relevant: the client will be synced to the state
		// before this op by the time the op arrives, because both travel on one ordered channel.
		const auto Held=[Stream](const FTerrainChunkKey& K)
		{ return Stream->Subscribed.Contains(K) || Stream->PendingPristine.Contains(K) || Stream->Syncing.Contains(K); };
		bool Relevant=false;
		for (const auto& K:R.AffectedChunks) if (Held(K)) { Relevant=true; break; }
		if (Relevant)
		{
			Stream->ClientApplyOp(Bytes,Revisions);
			// Only for chunks the client holds in sync. Recording a revision for a chunk it merely
			// has in the footprint would later let a resubscription skip a snapshot it needs.
			for (const auto& V:Revisions)
			{
				const FTerrainChunkKey K(V.Key.X,V.Key.Y,V.Key.Z);
				if (Stream->Subscribed.Contains(K) || Stream->Syncing.Contains(K)) Stream->DeliveredRevisions.Add(K,V.After);
			}
		}
	}
	return true;
}
bool UTerrainService::ApplyReplicatedOp(const TArray<uint8>& Bytes,const TArray<FTerrainChunkRevision>& Revisions,
	TArray<FIntVector>& OutResync)
{
	OutResync.Reset();
	if (!IsBackendReady() || HasAuthority() || Bytes.Num()!=TerrainOpEncodedSize) return false;
	FTerrainOp Op; int64 W,Scans;
	if (!DeserializeTerrainOp(Bytes,Op) || !Op.OpSeq
		|| !TerrainOpCounts(Op,GetDefault<UTerrainSettings>()->MaxVoxelsPerOp,W,Scans)) return false;
	TArray<FTerrainChunkKey> Lost;
	if (!Replica.ApplyOp(*Backend,*RevisionIndex,Op,Revisions,Lost)) return false;
	for (const auto& K:Lost) OutResync.Add(FIntVector(K.X,K.Y,K.Z));
	return true;
}
bool UTerrainService::ApplyReplicatedSnapshot(const FTerrainChunkKey& Key,FTerrainRev Rev,TArrayView<const uint8> Compressed)
{
	if (!IsBackendReady() || HasAuthority()) return false;
	return Replica.ApplySnapshot(*Backend,*RevisionIndex,Key,Rev,Compressed);
}
bool UTerrainService::AcceptPristine(const TArray<FIntVector>& Keys)
{
	if (!IsBackendReady() || !RevisionIndex || Keys.Num()>64) return false;
	TArray<FTerrainChunkKey> Chunks;
	for (const auto& K:Keys) Chunks.Add(FTerrainChunkKey(K.X,K.Y,K.Z));
	return Replica.AcceptPristine(*RevisionIndex,Chunks);
}
void UTerrainService::ReceiveSnapshotAck(UTerrainStreamComponent& Stream,const FTerrainChunkKey& Key,uint32 Generation,bool bApplied)
{
	if (!HasAuthority()) return;
	const auto* Flight=Stream.Syncing.Find(Key);
	if (!Flight || Flight->Generation!=Generation) return;   // stale: a resync already superseded it
	Stream.SnapshotBytesInFlight-=Flight->Bytes;
	Stream.Syncing.Remove(Key);
	if (bApplied) Stream.Subscribed.Add(Key);
	else Stream.DeliveredRevisions.Remove(Key);   // the next refresh sends it again
}
bool UTerrainService::SendSnapshot(UTerrainStreamComponent& Stream,const FTerrainChunkKey& Key)
{
	// Read NOW, between commits: this is the cut. Every op committed before it is inside the
	// snapshot; every op committed after it is sent after it, on the same ordered channel.
	FTerrainRegionData Region;
	if (!Backend->IsRegionResident(Key) || !Backend->ReadRegion(Key,Region)
		|| Region.Encoding!=ETerrainRegionEncoding::Dense) return false;
	TArray<uint8> Compressed; TArray<TArray<uint8>> Pieces;
	if (!TerrainEncodeChunkSnapshot(Region.Payload,Compressed) || !TerrainSplitChunkSnapshot(Compressed,Pieces)) return false;
	const FTerrainRev Rev=GetRevision(Key);
	const uint32 Generation=Stream.NextSyncGeneration++;
	FTerrainSnapshotFragment F;
	F.Key=FIntVector(Key.X,Key.Y,Key.Z); F.Generation=Generation; F.Rev=Rev;
	F.Count=uint8(Pieces.Num()); F.TotalBytes=Compressed.Num();
	// Every fragment in this one call: nothing can be committed between them.
	for (int32 I=0;I<Pieces.Num();++I) { F.Index=uint8(I); F.Bytes=MoveTemp(Pieces[I]); Stream.ClientChunkSnapshot(F); }
	UTerrainStreamComponent::FSnapshotInFlight Flight; Flight.Generation=Generation; Flight.Bytes=Compressed.Num();
	Stream.Syncing.Add(Key,Flight);
	Stream.SnapshotBytesInFlight+=Compressed.Num();
	Stream.DeliveredRevisions.Add(Key,Rev);
	++SnapshotsSent; SnapshotBytesSent+=Compressed.Num();
	UE_LOG(LogTerrainCore,Verbose,TEXT("Snapshot (%d,%d,%d) rev %u -> source %u: %d bytes in %d fragment(s)"),
		Key.X,Key.Y,Key.Z,Rev,Stream.SourceId,Compressed.Num(),Pieces.Num());
	return true;
}
void UTerrainService::PumpSnapshots()
{
	// Per connection, a bound on unacknowledged bytes -- not a mean rate -- because UE closes a
	// connection whose reliable buffer overflows (§7.3). One snapshot may always be in flight,
	// so a chunk larger than the budget still goes out, alone.
	constexpr int32 MaxBytesInFlight=64*1024;
	int32 Budget=2;   // chunks per tick across all connections: each read and compress costs ms
	for (const auto& Entry:Streams)
	{
		auto* Stream=Entry.Value.Get(); if (!Stream || !Stream->bReady) continue;
		while (Budget>0 && !Stream->SnapshotQueue.IsEmpty()
			&& (Stream->Syncing.IsEmpty() || Stream->SnapshotBytesInFlight<MaxBytesInFlight))
		{
			const FTerrainChunkKey K=Stream->SnapshotQueue[0];
			Stream->SnapshotQueue.RemoveAt(0);
			// Pristine by now, or already covered: the refresh will choose the right path.
			if (GetRevision(K)==0 || Stream->Subscribed.Contains(K) || Stream->Syncing.Contains(K)) continue;
			--Budget;
			if (!SendSnapshot(*Stream,K)) break;   // not readable yet; the next refresh re-queues it
		}
	}
}
uint64 UTerrainService::HashChunk(const FTerrainChunkKey& Key) const
{ return IsBackendReady() ? Backend->HashRegion(Key) : 0; }
uint64 UTerrainService::HashChunkMaterials(const FTerrainChunkKey& Key) const
{
	FTerrainRegionData Region;
	if (!IsBackendReady() || !Backend->ReadRegion(Key,Region) || Region.Payload.Num()!=TerrainChunkSampleCount*4) return 0;
	uint64 Hash=0x9e3779b97f4a7c15ULL;   // nonzero even for an all-Unknown chunk
	for (int32 I=0;I<TerrainChunkSampleCount;++I)
	{
		const uint64 Id=uint64(Region.Payload[(TerrainChunkSampleCount+I)*2]) | uint64(Region.Payload[(TerrainChunkSampleCount+I)*2+1])<<8;
		uint64 V=(uint64(I)<<32) ^ Id;
		V=(V^(V>>30))*0xbf58476d1ce4e5b9ULL; V=(V^(V>>27))*0x94d049bb133111ebULL; Hash+=V^(V>>31);
	}
	return Hash;
}

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
	// A snapshot already in flight completes on its ack; one still queued is simply dropped.
	Stream.SnapshotQueue.RemoveAll([&](const FTerrainChunkKey& K) { return Distance(K)>FMath::Square(Radius*1.5); });
	// The listen host shares the authoritative world: everything near it is in sync by definition.
	const bool bLocalHost=PC->IsLocalController();
	TArray<FIntVector> Batch;
	// The finite prototype world bounds the scan; avoid coordinates derived from client input.
	const auto World=ActiveInit.WorldBoundsVox;
	for (int32 Z=TerrainChunkCoordinate(World.Min.Z);Z<=TerrainChunkCoordinate(World.Max.Z-1);++Z)
	for (int32 Y=TerrainChunkCoordinate(World.Min.Y);Y<=TerrainChunkCoordinate(World.Max.Y-1);++Y)
	for (int32 X=TerrainChunkCoordinate(World.Min.X);X<=TerrainChunkCoordinate(World.Max.X-1);++X)
	{
		FTerrainChunkKey K(X,Y,Z);
		if (Distance(K)>Radius*Radius || Stream.Subscribed.Contains(K) || Stream.PendingPristine.Contains(K)
			|| Stream.Syncing.Contains(K)) continue;
		if (bLocalHost) { Stream.Subscribed.Add(K); continue; }
		if (const uint32* Delivered=Stream.DeliveredRevisions.Find(K); Delivered && *Delivered==GetRevision(K))
		{ Stream.Subscribed.Add(K); continue; }
		if (GetRevision(K)!=0)
		{
			// P-008 §4: an edited chunk is caught up by snapshot, nearest first.
			if (!Stream.SnapshotQueue.Contains(K)) Stream.SnapshotQueue.Add(K);
			continue;
		}
		Stream.PendingPristine.Add(K); Batch.Add(FIntVector(X,Y,Z));
		if (Batch.Num()==64) { Stream.ClientPristine(Batch); Batch.Reset(); }
	}
	if (!Batch.IsEmpty()) Stream.ClientPristine(Batch);
	Stream.SnapshotQueue.Sort([&](const FTerrainChunkKey& A,const FTerrainChunkKey& B) { return Distance(A)<Distance(B); });
}

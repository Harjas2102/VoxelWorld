// Copyright VoxelWorld.
#include "TerrainEditQueue.h"
#include "HAL/PlatformTime.h"

void FTerrainEditQueue::RegisterSource(uint32 Id, const FTerrainSourceState& State)
{
	check(IsInGameThread());
	if (!Id || Sources.Contains(Id)) return;
	auto S = MakeUnique<FSource>(); S->State = State; S->Tokens = Burst;
	Sources.Add(Id, MoveTemp(S)); RoundRobin.Add(Id);
}
void FTerrainEditQueue::Disconnect(uint32 Id)
{
	check(IsInGameThread());
	if (auto* S = Sources.Find(Id)) (*S)->State.bConnected = false;
}
bool FTerrainEditQueue::SetSourceState(uint32 Id, const FTerrainSourceState& State)
{
	check(IsInGameThread());
	if (auto* S = Sources.Find(Id)) { (*S)->State = State; return true; }
	return false;
}
void FTerrainEditQueue::Refill(FSource& S, double Now)
{
	S.Tokens = FMath::Min(Burst, S.Tokens + FMath::Max(0., Now-S.LastTime)*RatePerSecond);
	S.LastTime = Now;
}
void FTerrainEditQueue::Resolve(uint32 Id, FSource& S, FTerrainEditReceipt R, const FTerrainQueueCallbacks& Cb)
{
	R.bQueued = false;
	if (S.Recent.Num() == 64) S.Recent.RemoveAt(0,1,EAllowShrinking::No);
	S.Recent.Add(R);
	if (Cb.Receipt) Cb.Receipt(Id,R);
}
bool FTerrainEditQueue::SeedSequence(FTerrainOpSeq NextSequenceToAssign)
{
	check(IsInGameThread());
	// Untouched means: nothing committed, nothing queued. Anything else and seeding would
	// renumber operations that already exist or are already in flight.
	if (NextOpSeq != 1 || PendingCount != 0 || NextSequenceToAssign < 1)
	{
		return false;
	}
	NextOpSeq = NextSequenceToAssign;
	return true;
}

bool FTerrainEditQueue::Submit(uint32 Id, int64 RequestId, const FTerrainOp& Intent, double Now,
	const FTerrainQueueCallbacks& Cb, FTerrainEditReceipt& R, int32 MaxWrites, ETerrainEditRejection AdmissionFailure)
{
	check(IsInGameThread()); R = {}; R.RequestId = RequestId;
	auto* Entry = Sources.Find(Id);
	if (!Entry || RequestId <= 0 || !FMath::IsFinite(Now)) { R.Rejection = ETerrainEditRejection::BadRequest; return false; }
	FSource& S = **Entry;
	for (const auto& Old : S.Recent) if (Old.RequestId == RequestId)
	{ R = Old; if (Cb.Receipt) Cb.Receipt(Id,R); return R.bApplied; }
	for (const auto& Job : S.Jobs) if (Job.RequestId == RequestId) { R.bQueued = true; return true; }
	if (RequestId <= S.HighWater) { R.Rejection = ETerrainEditRejection::StaleRequest; if (Cb.Receipt) Cb.Receipt(Id,R); return false; }
	S.HighWater = RequestId;
	const auto Refuse = [&](ETerrainEditRejection Reason) { R.Rejection = Reason; Resolve(Id,S,R,Cb); return false; };
	if (AdmissionFailure != ETerrainEditRejection::None) return Refuse(AdmissionFailure);
	if (Cb.Refresh) Cb.Refresh(Id,S.State);
	if (!S.State.bConnected || !S.State.bOwnsTool || !S.State.bEquipped || S.State.ToolId != Intent.ToolId
		|| S.State.CooldownUntil > Now || S.State.ChargePerOp < 0 || S.State.Charges < -1)
		return Refuse(ETerrainEditRejection::ToolUnavailable);
	if (!S.State.bPermitted) return Refuse(ETerrainEditRejection::PermissionDenied);
	const int32 Capacity = FMath::Min(GlobalLimit-PendingCount,SourceLimit-S.Pending);
	if (Capacity < 1) return Refuse(ETerrainEditRejection::QueueFull);
	TArray<FTerrainOp> Parts;
	if (!SplitTerrainOp(Intent,MaxWrites,SourceLimit,Parts)) return Refuse(ETerrainEditRejection::TooLarge);
	if (Parts.Num()>Capacity) return Refuse(ETerrainEditRejection::QueueFull);
	const int64 Charge = int64(Parts.Num())*S.State.ChargePerOp;
	if (Charge > MAX_int32 || (S.State.Charges >= 0 && Charge + S.ReservedCharge > S.State.Charges))
		return Refuse(ETerrainEditRejection::ToolUnavailable);
	Refill(S,Now);
	// One token per intent, queue/charge reservations per part. Boxes cannot evade bounded capacity.
	if (S.Tokens < 1.) return Refuse(ETerrainEditRejection::RateLimited);
	if (NextTransaction == MAX_uint64) return Refuse(ETerrainEditRejection::RevisionExhausted);
	for (auto& Part : Parts)
	{
		Part.SourceId = Id; Part.OpSeq = 0; Part.TransactionId = 0;
		Part.MaterialId = S.State.PlacementMaterial;
		if (Cb.Validate)
		{
			const auto Reason = Cb.Validate(Part,S.State);
			if (Reason != ETerrainEditRejection::None) return Refuse(Reason);
		}
	}
	for (auto& Part : Parts) Part.TransactionId = NextTransaction;
	++NextTransaction; --S.Tokens; S.ReservedCharge += int32(Charge);
	S.Pending += Parts.Num(); PendingCount += Parts.Num();
	FTransaction Job; Job.RequestId = RequestId; Job.Parts = MoveTemp(Parts); Job.Receipt.RequestId = RequestId;
	Job.ChargePerPart = S.State.ChargePerOp; Job.EnqueuedAt = Now;
	S.Jobs.Add(MoveTemp(Job)); R.bQueued = true; return true;
}

void FTerrainEditQueue::Pump(double Now, const FTerrainQueueCallbacks& Cb, int32 MaxOps, double BudgetSeconds)
{
	check(IsInGameThread());
	if (bPumping) return;
	TGuardValue<bool> Guard(bPumping,true);
	const double Start = FPlatformTime::Seconds();
	for (int32 Done = 0; Done < MaxOps && PendingCount > 0; ++Done)
	{
		if (Done && FPlatformTime::Seconds()-Start >= BudgetSeconds) break;
		uint32 Id = ActiveSource;
		if (!Id)
		{
			for (int32 N = 0; N < RoundRobin.Num(); ++N)
			{
				Cursor %= RoundRobin.Num(); const uint32 Candidate = RoundRobin[Cursor++];
				if (!Sources[Candidate]->Jobs.IsEmpty()) { Id = Candidate; break; }
			}
		}
		if (!Id) break;
		FSource& S = *Sources[Id]; FTransaction& Job = S.Jobs[0];
		FTerrainOp Op = Job.Parts[Job.Next];
		MaxObservedAge = FMath::Max(MaxObservedAge,Now-Job.EnqueuedAt);
		if (Cb.Refresh) Cb.Refresh(Id,S.State);
		bool Valid = S.State.bConnected && S.State.bOwnsTool && S.State.bEquipped && S.State.ToolId == Op.ToolId
			&& S.State.bPermitted && S.State.CooldownUntil <= Now && S.State.ChargePerOp == Job.ChargePerPart
			&& S.State.PlacementMaterial == Op.MaterialId
			&& (S.State.Charges == -1 || S.State.Charges >= S.ReservedCharge);
		if (Valid && Cb.Validate) Valid = Cb.Validate(Op,S.State) == ETerrainEditRejection::None;
		ETerrainEditRejection Rejection = Valid ? ETerrainEditRejection::None : ETerrainEditRejection::Revalidation;
		if (Valid && NextOpSeq > uint64(MAX_int64)) { Valid=false; Rejection=ETerrainEditRejection::RevisionExhausted; }
		FTerrainEditResult Result;
		const double ApplyStart = FPlatformTime::Seconds();
		if (Valid && (!Cb.Apply || !Cb.Apply(Op,Result))) { Valid=false; Rejection=ETerrainEditRejection::BackendFailed; }
		MaxObservedApply = FMath::Max(MaxObservedApply,FPlatformTime::Seconds()-ApplyStart);
		if (Valid)
		{
			// P-003 §2: the sequence is PROVISIONAL until the record is durable. Consuming it
			// before the commit could succeed would leave a gap in the journal for an
			// operation that never became part of the world's history.
			Op.OpSeq = NextOpSeq;
			FTerrainCommitIdentity Identity;
			Identity.RequestId    = uint32(FMath::Clamp<int64>(Job.RequestId,0,MAX_uint32));
			Identity.ChildOrdinal = uint16(Job.Next);
			Identity.ChildCount   = uint16(Job.Parts.Num());
			if (Cb.Commit && !Cb.Commit(Op,Result,Identity))
			{
				// The backend has already mutated and nothing was broadcast. The world is
				// going away; ShuttingDown is the reason that tells a client not to retry.
				Valid = false; Rejection = ETerrainEditRejection::ShuttingDown;
			}
		}
		if (Valid)
		{
			++NextOpSeq;
			Job.Receipt.bApplied = true; Job.Receipt.OpSeq = int64(Op.OpSeq);
			Job.Receipt.VoxelsTouched += Result.VoxelsTouched; Job.Receipt.ChunksAffected += Result.AffectedChunks.Num();
			S.ReservedCharge -= Job.ChargePerPart;
			if (S.State.Charges >= 0) S.State.Charges -= Job.ChargePerPart;
			--S.Pending; --PendingCount; ++Job.Next;
		}
		if (!Valid || Job.Next == Job.Parts.Num())
		{
			const int32 Uncommitted = Job.Parts.Num()-Job.Next;
			S.Pending -= Uncommitted; PendingCount -= Uncommitted;
			S.ReservedCharge -= Uncommitted*Job.ChargePerPart;
			if (!Job.Receipt.bApplied) S.Tokens = FMath::Min(Burst,S.Tokens+1.);
			FTerrainEditReceipt R = Job.Receipt; R.Rejection = Rejection;
			S.Jobs.RemoveAt(0,1,EAllowShrinking::No); ActiveSource=0;
			Resolve(Id,S,R,Cb);
		}
		else ActiveSource = Id;
	}
	// Retain a disconnected source only until its queued requests resolve as rejected.
	for (int32 N=RoundRobin.Num()-1; N>=0; --N)
	{
		const uint32 Id=RoundRobin[N];
		if (!Sources[Id]->State.bConnected && Sources[Id]->Jobs.IsEmpty())
		{ Sources.Remove(Id); RoundRobin.RemoveAt(N); if (Cursor>N) --Cursor; }
	}
}
void FTerrainEditQueue::Cancel(const FTerrainQueueCallbacks& Cb)
{
	check(IsInGameThread());
	for (auto& Entry : Sources)
	{
		FSource& S=*Entry.Value;
		for (auto& Job : S.Jobs)
		{ auto R=Job.Receipt; R.Rejection=ETerrainEditRejection::ShuttingDown; Resolve(Entry.Key,S,R,Cb); }
		S.Jobs.Reset(); S.Pending=0; S.ReservedCharge=0; S.Tokens=Burst;
	}
	PendingCount=0; ActiveSource=0;
}

// Copyright VoxelWorld. Bounded, game-thread-owned admission and commit (§4.11).
#pragma once
#include "TerrainEdit.h"
#include "TerrainOpGeometry.h"

struct FTerrainSourceState
{
	bool bConnected = true, bOwnsTool = true, bEquipped = true, bPermitted = true;
	uint32 ToolId = 0;
	FTerrainMatId PlacementMaterial = 0;
	FVector Position = FVector::ZeroVector;
	double ReachCm = 1150, MaxRadiusCm = 1000, CooldownUntil = 0;
	int32 Charges = -1; // -1: prototype hand tool has no consumable. Other tools can be finite.
	int32 ChargePerOp = 0;
};

struct FTerrainQueueCallbacks
{
	TFunction<void(uint32, FTerrainSourceState&)> Refresh;
	TFunction<ETerrainEditRejection(const FTerrainOp&, const FTerrainSourceState&)> Validate;
	TFunction<bool(const FTerrainOp&, FTerrainEditResult&)> Apply;
	/**
	 * Durably record, then publish, one committed operation.
	 *
	 * Returns false when the operation could not be made durable, in which case it is NOT
	 * committed: the sequence is not consumed and the transaction is refused with
	 * `ShuttingDown`. P-003 §2 requires the durable append to precede the broadcast, and a
	 * callback that could not refuse would make that ordering unenforceable.
	 */
	TFunction<bool(const FTerrainOp&, const FTerrainEditResult&, const FTerrainCommitIdentity&)> Commit;
	TFunction<void(uint32, const FTerrainEditReceipt&)> Receipt;
};

class TERRAINCORE_API FTerrainEditQueue
{
public:
	FTerrainEditQueue() = default;
	FTerrainEditQueue(const FTerrainEditQueue&) = delete;
	FTerrainEditQueue& operator=(const FTerrainEditQueue&) = delete;
	void RegisterSource(uint32 Id, const FTerrainSourceState& State);
	void Disconnect(uint32 Id);
	bool SetSourceState(uint32 Id, const FTerrainSourceState& State);
	bool Submit(uint32 Id, int64 RequestId, const FTerrainOp& Intent, double Now, const FTerrainQueueCallbacks& Cb,
		FTerrainEditReceipt& Receipt, int32 MaxWrites = 65536, ETerrainEditRejection AdmissionFailure = ETerrainEditRejection::None);
	/** Fair between transactions; pin a bounded split transaction to keep its OpSeq contiguous. */
	void Pump(double Now, const FTerrainQueueCallbacks& Cb, int32 MaxOps = 32, double BudgetSeconds = .008);
	void Cancel(const FTerrainQueueCallbacks& Cb);
	int32 Depth() const { return PendingCount; }
	FTerrainOpSeq NextSequence() const { return NextOpSeq; }

	/**
	 * Seeds the committed sequence from a recovered world (P-004 §7's "validated H+1 path").
	 *
	 * **The queue owns sequence assignment**, which is why this exists and why seeding any
	 * other counter does nothing. A restored world continues its history at H+1; a queue left
	 * at 1 would hand out sequences the journal has already used, and the journal would -- and
	 * did, the first time this was wired -- refuse them as an OrderViolation.
	 *
	 * Refuses unless the queue is untouched: seeding one that has already committed, or that
	 * has work in flight, would renumber operations mid-session. Refuses to move backwards for
	 * the same reason. Call it before anything is admitted.
	 */
	bool SeedSequence(FTerrainOpSeq NextSequenceToAssign);
	double MaxQueueAgeSeconds() const { return MaxObservedAge; }
	double MaxApplySeconds() const { return MaxObservedApply; }
	int32 GlobalLimit = 256, SourceLimit = 16;
	double RatePerSecond = 3, Burst = 3;
private:
	struct FTransaction { double EnqueuedAt = 0; int64 RequestId; TArray<FTerrainOp> Parts; int32 Next = 0, ChargePerPart = 0; FTerrainEditReceipt Receipt; };
	struct FSource
	{
		FTerrainSourceState State;
		TArray<FTransaction> Jobs;
		TArray<FTerrainEditReceipt> Recent;
		int64 HighWater = 0;
		int32 Pending = 0, ReservedCharge = 0;
		double Tokens = 0, LastTime = 0;
	};
	TMap<uint32, TUniquePtr<FSource>> Sources;
	TArray<uint32> RoundRobin;
	int32 Cursor = 0, PendingCount = 0;
	uint32 ActiveSource = 0;
	uint64 NextTransaction = 1;
	FTerrainOpSeq NextOpSeq = 1;
	bool bPumping = false;
	double MaxObservedAge = 0, MaxObservedApply = 0;
	void Resolve(uint32 Id, FSource& Source, FTerrainEditReceipt Receipt, const FTerrainQueueCallbacks& Cb);
	void Refill(FSource& Source, double Now);
};

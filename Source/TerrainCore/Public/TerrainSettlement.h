// Copyright VoxelWorld. See Docs/proposals/P-010-settlement-ledger.md.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Templates/UniquePtr.h"
#include "Tasks/Pipe.h"
#include "Containers/Queue.h"
#include "TerrainPersistenceRecords.h"
#include "TerrainJournalWriter.h"
#include "TerrainEdit.h"

class FTerrainWorldStore;

/**
 * TerrainSettlement.h -- how a committed edit pays out (P-003 §2 step 4, §3, P-010).
 *
 * The journal commits terrain. An idempotent ledger then SETTLES each committed record's
 * economic effects, keyed by the record's permanent identity (OpSeq) and digest. The ledger
 * lives in the EntityStore module (SQLite, D-012) and reaches the service through
 * FTerrainSettlementRegistry, exactly as a terrain backend does, so TerrainCore never links
 * SQLite and never includes an EntityStore header.
 *
 * THE RULE THAT MAKES A CRASH HARMLESS. What a record pays is decided before the record is
 * written, and written INTO it (EconomyKind, PolicyVersion, EconomyDeltas). Settlement only ever
 * reads those deltas -- live, or at boot from the journal -- and never recomputes them from
 * current tuning. The ledger stores every settled OpSeq with its digest in the same transaction
 * that applies its deltas and advances the watermark W. So a crash anywhere leaves W at the
 * last settled record, boot settles (W, H] from the journal, and a record can be neither lost
 * nor paid twice.
 */

/** One committed record's settlement input: exactly what the journal recorded, nothing derived. */
struct FTerrainSettlementInput
{
	FTerrainOpSeq                OpSeq = 0;
	FTerrainDigest               Digest;   // P-004 §9.3 record digest, the settlement digest
	ETerrainEconomyKind          EconomyKind = ETerrainEconomyKind::NoEconomy;
	uint16                       EconomyPolicyVersion = 0;
	TArray<FTerrainEconomyDelta> Deltas;
};

/** One balance row: an owner's amount of one item in one container. */
struct FTerrainLedgerBalance
{
	uint64 OwnerId = 0;
	uint64 ContainerId = 0;
	uint32 ItemId = 0;
	int64  Amount = 0;
};

/**
 * The durable ledger. Single-threaded: after Open it is touched only by the settlement worker's
 * pipe (or, before the worker exists, by boot on the game thread -- ownership moves, it is
 * never shared).
 */
class TERRAINCORE_API ITerrainSettlementLedger
{
public:
	virtual ~ITerrainSettlementLedger() = default;

	/** Whether a ledger file exists at the path. Checked BEFORE Open decides to create one. */
	virtual bool Exists(const FString& AbsolutePath) const = 0;

	/**
	 * Opens (or, with bCreate, creates) the ledger and checks it belongs to this world and lineage.
	 * A ledger of another world or epoch is refused, never adopted (P-003 §5).
	 */
	virtual FTerrainStoreResult Open(const FString& AbsolutePath, const FTerrainPersistIdentity& Identity, bool bCreate) = 0;
	virtual void Close() = 0;

	/** The contiguous settlement watermark W: every OpSeq <= W is settled, nothing above is. */
	virtual FTerrainOpSeq GetWatermark() const = 0;

	/**
	 * Settles inputs in ascending OpSeq order, in ONE transaction: all or nothing.
	 *
	 * An input at or below W must match the stored digest and is skipped (a replay after a
	 * crash); a mismatch is corruption. An input above W+1 is a gap. A delta that would overflow
	 * a balance fails the whole batch closed -- a credit is never dropped or clamped (P-003 §2).
	 */
	virtual FTerrainStoreResult Settle(TConstArrayView<FTerrainSettlementInput> Inputs) = 0;

	virtual bool ReadBalances(TArray<FTerrainLedgerBalance>& Out) const = 0;

	/** Rows in the settlement table, for the audit. */
	virtual int64 CountSettlements() const = 0;
};

using FTerrainSettlementCreator = TFunction<TUniquePtr<ITerrainSettlementLedger>()>;

/** Same shape and same reason as FTerrainBackendRegistry: the ledger module registers itself. */
class TERRAINCORE_API FTerrainSettlementRegistry
{
public:
	static FTerrainSettlementRegistry& Get();
	void Register(FName Name, FTerrainSettlementCreator Creator);
	void Unregister(FName Name);
	TUniquePtr<ITerrainSettlementLedger> Create(FName Name) const;
private:
	TMap<FName, FTerrainSettlementCreator> Creators;
};

/**
 * Reads the settlement inputs of every commit record with OpSeq > AfterExclusive, in order,
 * straight from the journal. Also reports whether ANY record in the whole journal carries an
 * economy, which is how boot tells "this world never had a ledger" from "its ledger was lost".
 */
TERRAINCORE_API FTerrainStoreResult TerrainReadSettlementInputs(
	FTerrainWorldStore& Store, FTerrainOpSeq AfterExclusive,
	TArray<FTerrainSettlementInput>& OutInputs, bool& bOutAnyEconomy, FTerrainOpSeq& OutFirstOpSeq);

// ---- the economy policy (P-010 §3, DEF-6's economic half) ------------------------------------

/** Version 1: exact microlitres of each yielding material into the owner's personal stock. */
inline constexpr uint16 TerrainEconomyPolicyVersion = 1;

/** Owner 0 has no stable identity and is never paid; owner 1 is the server's own diagnostics. */
inline constexpr uint64 TerrainOwnerNone   = 0;
inline constexpr uint64 TerrainOwnerServer = 1;

/** The personal stock. Containers beyond it (chests, machines) are later entities. */
inline constexpr uint64 TerrainContainerPersonal = 0;

/** Whether removing this material pays. Air, Unknown and Fill never do (P-010 §3). */
TERRAINCORE_API bool TerrainMaterialYields(FTerrainMatId Id);

/**
 * The economic intent of one committed op, decided BEFORE it is journaled. Removal of a
 * yielding material credits the owner exactly the removed microlitres (tool 0: 100%).
 * Placement debits nothing: a player places Fill, which never yields, so place-then-mine
 * cannot mint. No owner -> NoEconomy.
 */
TERRAINCORE_API void TerrainComputeEconomy(uint64 OwnerId, uint32 ToolId,
	TConstArrayView<FTerrainMaterialVolume> Physical, FTerrainCommitEconomy& OutEconomy);

// ---- the worker (P-003 §2 steps 4-5) ---------------------------------------------------------

/**
 * Owns the ledger once the world is running and settles on a serialized UE::Tasks pipe, so
 * SQLite's fsync never runs on the game thread. Game-thread API only. Batches up to 16 records
 * per transaction; the service stops executing new mutations while 32 are unsettled.
 */
class TERRAINCORE_API FTerrainSettlementWorker
{
public:
	static constexpr int32 MaxPending = 32;
	static constexpr int32 MaxBatch   = 16;

	FTerrainSettlementWorker();
	~FTerrainSettlementWorker();

	void Start(TUniquePtr<ITerrainSettlementLedger> InLedger, FTerrainOpSeq Watermark);

	/** Queues one committed record. Must be exactly the next OpSeq after the last submitted. */
	void Submit(FTerrainSettlementInput&& Input);

	/** Collects finished batches. Returns false once a batch has failed (storage fault). */
	bool Poll();

	int32 Pending() const { return Submitted - SettledCount; }
	FTerrainOpSeq GetWatermark() const { return Watermark; }
	bool HasFailed() const { return bFailed; }
	const FTerrainStoreResult& GetFailure() const { return Failure; }

	/** Waits for every submitted batch, then closes the ledger. Safe to call twice. */
	void Stop();

	/** Game-thread read of the ledger, after waiting for the pipe to go idle. Diagnostics only. */
	bool ReadBalancesBlocking(TArray<FTerrainLedgerBalance>& Out, int64& OutSettlements);

	/**
	 * Development only: the worker sleeps this long before each transaction, widening the
	 * crash window between "journaled" and "settled" for the kill test. Zero in real play.
	 */
	float TestDelaySeconds = 0.f;

	/** Settled OpSeqs since the last call, for the service to notify owners. */
	TArray<FTerrainSettlementInput> TakeSettled();

private:
	void Dispatch();

	struct FResult { FTerrainStoreResult Result; TArray<FTerrainSettlementInput> Inputs; };

	TSharedPtr<ITerrainSettlementLedger, ESPMode::ThreadSafe> Ledger;
	TUniquePtr<UE::Tasks::FPipe> Pipe;
	TArray<FTerrainSettlementInput> Queue;           // submitted, not yet dispatched
	TQueue<FResult, EQueueMode::Mpsc> Done;          // finished on the pipe
	TArray<FTerrainSettlementInput> SettledForNotify;
	int32 Submitted = 0, SettledCount = 0, InFlight = 0;
	FTerrainOpSeq Watermark = 0, LastSubmitted = 0;
	bool bFailed = false, bStarted = false;
	FTerrainStoreResult Failure;
};

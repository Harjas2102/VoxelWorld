// Copyright VoxelWorld. See Docs/proposals/P-010-settlement-ledger.md.

#pragma once

#include "CoreMinimal.h"
#include "TerrainSettlement.h"

class FSQLiteDatabase;

/**
 * FEntityLedger -- the settlement ledger in SQLite (D-012, P-003 §2, P-010 §4).
 *
 * THE SCHEMA (version 1), flat and inspectable with any SQLite tool:
 *   meta(key TEXT PRIMARY KEY, ival INTEGER, bval BLOB)   schema, world id, store epoch, W
 *   settlement(opseq INTEGER PRIMARY KEY, digest BLOB, policy INTEGER, deltas INTEGER)
 *   balance(owner, container, item, amount; PRIMARY KEY(owner, container, item))
 * Unsigned 64-bit ids are stored bit-for-bit in SQLite's signed INTEGER.
 *
 * DURABILITY. journal_mode=WAL and synchronous=FULL, both read back after being set: FULL is
 * what makes a committed transaction survive power loss in WAL mode, and a PRAGMA that silently
 * did not take would leave that claim false (P-003 §2). Every settle is one BEGIN IMMEDIATE ...
 * COMMIT; any failure rolls the whole batch back, including the watermark.
 */
class ENTITYSTORE_API FEntityLedger final : public ITerrainSettlementLedger
{
public:
	static constexpr int64 SchemaVersion = 1;

	FEntityLedger();
	virtual ~FEntityLedger() override;

	virtual bool Exists(const FString& AbsolutePath) const override;
	virtual FTerrainStoreResult Open(const FString& AbsolutePath, const FTerrainPersistIdentity& Identity, bool bCreate) override;
	virtual void Close() override;
	virtual FTerrainOpSeq GetWatermark() const override { return Watermark; }
	virtual FTerrainStoreResult Settle(TConstArrayView<FTerrainSettlementInput> Inputs) override;
	virtual bool ReadBalances(TArray<FTerrainLedgerBalance>& Out) const override;
	virtual int64 CountSettlements() const override;

	/**
	 * Test only: fail the Nth SQL statement inside the next Settle, as if the process died there.
	 * Proves the batch rolls back whole -- no ledger row, no balance, no watermark. Negative = off.
	 */
	int32 TestFailAtStatement = -1;

private:
	bool Exec(const TCHAR* Sql);
	bool ReadMetaInt(const TCHAR* Key, int64& Out) const;
	bool ReadMetaBlob(const TCHAR* Key, TArray<uint8>& Out) const;
	FTerrainStoreResult Fail(const TCHAR* What, FTerrainStoreResult Result);

	TUniquePtr<FSQLiteDatabase> Db;
	FTerrainOpSeq Watermark = 0;
	int32 StatementCounter = 0;
};

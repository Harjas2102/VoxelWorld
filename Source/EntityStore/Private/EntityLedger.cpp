// Copyright VoxelWorld. See Docs/proposals/P-010-settlement-ledger.md.

#include "EntityLedger.h"

#include "SQLiteDatabase.h"
#include "SQLitePreparedStatement.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogEntityStore, Log, All);

namespace
{
	int64 AsSigned(uint64 V) { int64 S; FMemory::Memcpy(&S, &V, sizeof(S)); return S; }
	uint64 AsUnsigned(int64 V) { uint64 U; FMemory::Memcpy(&U, &V, sizeof(U)); return U; }
}

FEntityLedger::FEntityLedger() = default;

FEntityLedger::~FEntityLedger()
{
	Close();
}

bool FEntityLedger::Exists(const FString& AbsolutePath) const
{
	return IFileManager::Get().FileExists(*AbsolutePath);
}

bool FEntityLedger::Exec(const TCHAR* Sql)
{
	if (TestFailAtStatement >= 0 && StatementCounter++ == TestFailAtStatement)
	{
		return false;
	}
	return Db->Execute(Sql);
}

bool FEntityLedger::ReadMetaInt(const TCHAR* Key, int64& Out) const
{
	FSQLitePreparedStatement Stmt(*Db, TEXT("SELECT ival FROM meta WHERE key = ?1;"));
	return Stmt.IsValid() && Stmt.SetBindingValueByIndex(1, Key)
		&& Stmt.Step() == ESQLitePreparedStatementStepResult::Row && Stmt.GetColumnValueByIndex(0, Out);
}

bool FEntityLedger::ReadMetaBlob(const TCHAR* Key, TArray<uint8>& Out) const
{
	FSQLitePreparedStatement Stmt(*Db, TEXT("SELECT bval FROM meta WHERE key = ?1;"));
	return Stmt.IsValid() && Stmt.SetBindingValueByIndex(1, Key)
		&& Stmt.Step() == ESQLitePreparedStatementStepResult::Row && Stmt.GetColumnValueByIndex(0, Out);
}

FTerrainStoreResult FEntityLedger::Fail(const TCHAR* What, FTerrainStoreResult Result)
{
	UE_LOG(LogEntityStore, Error, TEXT("Ledger: %s (%s; sqlite: %s)."), What, *Result.ToString(),
		Db ? *Db->GetLastError() : TEXT("no database"));
	return Result;
}

FTerrainStoreResult FEntityLedger::Open(const FString& AbsolutePath, const FTerrainPersistIdentity& Identity, bool bCreate)
{
	Close();
	const bool bExisted = Exists(AbsolutePath);
	if (!bExisted && !bCreate)
	{
		return FTerrainStoreResult::Io(ETerrainStorageResult::NotFound);
	}
	if (bExisted && bCreate)
	{
		// Creating over a ledger would mint a blank inventory for a world that had one.
		return FTerrainStoreResult::Io(ETerrainStorageResult::AlreadyExists);
	}

	Db = MakeUnique<FSQLiteDatabase>();
	if (!Db->Open(*AbsolutePath, bCreate ? ESQLiteDatabaseOpenMode::ReadWriteCreate : ESQLiteDatabaseOpenMode::ReadWrite))
	{
		const FTerrainStoreResult R = Fail(TEXT("cannot open the database"), FTerrainStoreResult::Io(ETerrainStorageResult::IoError));
		Close();
		return R;
	}

	// Durability first, and read back: a PRAGMA that did not take would make every claim below
	// false. Each statement is finalized (scope ends) BEFORE any Close -- SQLite refuses to close a
	// connection with a live statement.
	FString Mode;
	bool bModeRead = false;
	int64 Sync = -1;
	bool bSyncSet = false, bSyncRead = false;
	// UE compiles SQLite on its own file layer (SQLITE_OS_OTHER), which has no shared memory, so
	// WAL is only available with EXCLUSIVE locking, set BEFORE the first WAL access. Without it
	// SQLite silently stays in DELETE mode, which creates and removes a journal file per
	// transaction -- the name churn P-005 removed from the terrain store (P-010 §4). One connection
	// is all there ever is: the world's writer lease (T-128) already guarantees it.
	FString Locking;
	{
		FSQLitePreparedStatement Stmt(*Db, TEXT("PRAGMA locking_mode=EXCLUSIVE;"));
		if (Stmt.IsValid() && Stmt.Step() == ESQLitePreparedStatementStepResult::Row) Stmt.GetColumnValueByIndex(0, Locking);
	}
	{
		FSQLitePreparedStatement Stmt(*Db, TEXT("PRAGMA journal_mode=WAL;"));
		bModeRead = Stmt.IsValid() && Stmt.Step() == ESQLitePreparedStatementStepResult::Row && Stmt.GetColumnValueByIndex(0, Mode);
	}
	bSyncSet = Db->Execute(TEXT("PRAGMA synchronous=FULL;"));
	{
		FSQLitePreparedStatement Stmt(*Db, TEXT("PRAGMA synchronous;"));
		bSyncRead = Stmt.IsValid() && Stmt.Step() == ESQLitePreparedStatementStepResult::Row && Stmt.GetColumnValueByIndex(0, Sync);
	}
	if (!Locking.Equals(TEXT("exclusive"), ESearchCase::IgnoreCase)
		|| !bModeRead || !Mode.Equals(TEXT("wal"), ESearchCase::IgnoreCase) || !bSyncSet || !bSyncRead || Sync != 2)
	{
		UE_LOG(LogEntityStore, Error, TEXT("Ledger durability settings: locking_mode='%s', journal_mode read=%d value='%s', synchronous set=%d read=%d value=%lld."),
			*Locking, bModeRead ? 1 : 0, *Mode, bSyncSet ? 1 : 0, bSyncRead ? 1 : 0, Sync);
		const FTerrainStoreResult R = Fail(TEXT("WAL/FULL durability did not take"), FTerrainStoreResult::Io(ETerrainStorageResult::IoError));
		Close();
		return R;
	}

	if (bCreate)
	{
		const bool bSchema = Db->Execute(TEXT("BEGIN IMMEDIATE;"))
			&& Db->Execute(TEXT("CREATE TABLE meta(key TEXT PRIMARY KEY, ival INTEGER, bval BLOB);"))
			&& Db->Execute(TEXT("CREATE TABLE settlement(opseq INTEGER PRIMARY KEY, digest BLOB NOT NULL, policy INTEGER NOT NULL, deltas INTEGER NOT NULL);"))
			&& Db->Execute(TEXT("CREATE TABLE balance(owner INTEGER NOT NULL, container INTEGER NOT NULL, item INTEGER NOT NULL, amount INTEGER NOT NULL, PRIMARY KEY(owner, container, item)) WITHOUT ROWID;"));
		bool bMeta = bSchema;
		const auto PutInt = [&](const TCHAR* Key, int64 V)
		{
			FSQLitePreparedStatement Stmt(*Db, TEXT("INSERT INTO meta(key, ival) VALUES(?1, ?2);"));
			bMeta &= Stmt.IsValid() && Stmt.SetBindingValueByIndex(1, Key) && Stmt.SetBindingValueByIndex(2, V) && Stmt.Execute();
		};
		const auto PutBlob = [&](const TCHAR* Key, const uint8* Bytes, int32 Num)
		{
			FSQLitePreparedStatement Stmt(*Db, TEXT("INSERT INTO meta(key, bval) VALUES(?1, ?2);"));
			bMeta &= Stmt.IsValid() && Stmt.SetBindingValueByIndex(1, Key)
				&& Stmt.SetBindingValueByIndex(2, TArrayView<const uint8>(Bytes, Num)) && Stmt.Execute();
		};
		PutInt(TEXT("schema"), SchemaVersion);
		PutInt(TEXT("watermark"), 0);
		PutBlob(TEXT("world"), Identity.World.Bytes, TerrainPersistIdBytes);
		PutBlob(TEXT("epoch"), Identity.Epoch.Bytes, TerrainPersistIdBytes);
		if (!bMeta || !Db->Execute(TEXT("COMMIT;")))
		{
			Db->Execute(TEXT("ROLLBACK;"));
			const FTerrainStoreResult R = Fail(TEXT("cannot create the schema"), FTerrainStoreResult::Io(ETerrainStorageResult::IoError));
			Close();
			IFileManager::Get().Delete(*AbsolutePath);   // never leave a half-made ledger that looks real
			return R;
		}
	}

	// Whoever made it, it must be THIS world's, in THIS lineage, at a schema we understand.
	int64 Schema = 0, W = -1;
	TArray<uint8> World, Epoch;
	if (!ReadMetaInt(TEXT("schema"), Schema) || !ReadMetaInt(TEXT("watermark"), W)
		|| !ReadMetaBlob(TEXT("world"), World) || !ReadMetaBlob(TEXT("epoch"), Epoch))
	{
		const FTerrainStoreResult R = Fail(TEXT("the ledger's identity rows are missing"), FTerrainStoreResult::Bad(ETerrainPersistError::FieldOutOfRange));
		Close();
		return R;
	}
	if (Schema != SchemaVersion)
	{
		const FTerrainStoreResult R = Fail(TEXT("unsupported ledger schema"), FTerrainStoreResult::Bad(ETerrainPersistError::UnsupportedSchema));
		Close();
		return R;
	}
	if (World.Num() != TerrainPersistIdBytes || Epoch.Num() != TerrainPersistIdBytes
		|| FMemory::Memcmp(World.GetData(), Identity.World.Bytes, TerrainPersistIdBytes) != 0
		|| FMemory::Memcmp(Epoch.GetData(), Identity.Epoch.Bytes, TerrainPersistIdBytes) != 0)
	{
		const FTerrainStoreResult R = Fail(TEXT("the ledger belongs to another world or lineage"), FTerrainStoreResult::Bad(ETerrainPersistError::WorldMismatch));
		Close();
		return R;
	}
	if (W < 0 || !Db->PerformQuickIntegrityCheck())
	{
		const FTerrainStoreResult R = Fail(TEXT("the ledger failed its integrity check"), FTerrainStoreResult::Bad(ETerrainPersistError::FieldOutOfRange));
		Close();
		return R;
	}
	Watermark = FTerrainOpSeq(W);
	UE_LOG(LogEntityStore, Log, TEXT("Ledger %s at '%s': W=%llu, %lld settlements."),
		bCreate ? TEXT("created") : TEXT("opened"), *AbsolutePath, Watermark, CountSettlements());
	return FTerrainStoreResult::Ok();
}

void FEntityLedger::Close()
{
	if (Db)
	{
		Db->Close();
		Db.Reset();
	}
	Watermark = 0;
}

FTerrainStoreResult FEntityLedger::Settle(TConstArrayView<FTerrainSettlementInput> Inputs)
{
	if (!Db)
	{
		return FTerrainStoreResult::Io(ETerrainStorageResult::NotFound);
	}
	StatementCounter = 0;
	if (!Exec(TEXT("BEGIN IMMEDIATE;")))
	{
		return Fail(TEXT("cannot begin a settlement transaction"), FTerrainStoreResult::Io(ETerrainStorageResult::IoError));
	}

	FTerrainOpSeq W = Watermark;
	FTerrainStoreResult Error;
	const auto Counted = [&]() { return !(TestFailAtStatement >= 0 && StatementCounter++ == TestFailAtStatement); };

	for (const FTerrainSettlementInput& Input : Inputs)
	{
		if (Input.OpSeq <= W)
		{
			// Already settled: a replay after a crash. Same record, same digest -- or corruption.
			FSQLitePreparedStatement Stmt(*Db, TEXT("SELECT digest FROM settlement WHERE opseq = ?1;"));
			TArray<uint8> Stored;
			if (!Counted() || !Stmt.IsValid() || !Stmt.SetBindingValueByIndex(1, AsSigned(Input.OpSeq))
				|| Stmt.Step() != ESQLitePreparedStatementStepResult::Row || !Stmt.GetColumnValueByIndex(0, Stored))
			{
				Error = FTerrainStoreResult::Io(ETerrainStorageResult::IoError);
				break;
			}
			if (Stored.Num() != TerrainPersistDigestSize || FMemory::Memcmp(Stored.GetData(), Input.Digest.Bytes, TerrainPersistDigestSize) != 0)
			{
				Error = FTerrainStoreResult::Bad(ETerrainPersistError::DuplicateKey);
				break;
			}
			continue;
		}
		if (Input.OpSeq != W + 1)
		{
			Error = FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);   // a gap in W
			break;
		}

		{
			FSQLitePreparedStatement Stmt(*Db, TEXT("INSERT INTO settlement(opseq, digest, policy, deltas) VALUES(?1, ?2, ?3, ?4);"));
			if (!Counted() || !Stmt.IsValid() || !Stmt.SetBindingValueByIndex(1, AsSigned(Input.OpSeq))
				|| !Stmt.SetBindingValueByIndex(2, TArrayView<const uint8>(Input.Digest.Bytes, TerrainPersistDigestSize))
				|| !Stmt.SetBindingValueByIndex(3, int64(Input.EconomyPolicyVersion))
				|| !Stmt.SetBindingValueByIndex(4, int64(Input.Deltas.Num())) || !Stmt.Execute())
			{
				Error = FTerrainStoreResult::Io(ETerrainStorageResult::IoError);
				break;
			}
		}

		for (const FTerrainEconomyDelta& Delta : Input.Deltas)
		{
			int64 Current = 0;
			{
				FSQLitePreparedStatement Stmt(*Db, TEXT("SELECT amount FROM balance WHERE owner = ?1 AND container = ?2 AND item = ?3;"));
				if (!Counted() || !Stmt.IsValid() || !Stmt.SetBindingValueByIndex(1, AsSigned(Delta.OwnerId))
					|| !Stmt.SetBindingValueByIndex(2, AsSigned(Delta.ContainerId)) || !Stmt.SetBindingValueByIndex(3, int64(Delta.ItemId)))
				{
					Error = FTerrainStoreResult::Io(ETerrainStorageResult::IoError);
					break;
				}
				const ESQLitePreparedStatementStepResult Step = Stmt.Step();
				if (Step == ESQLitePreparedStatementStepResult::Row) Stmt.GetColumnValueByIndex(0, Current);
				else if (Step != ESQLitePreparedStatementStepResult::Done) { Error = FTerrainStoreResult::Io(ETerrainStorageResult::IoError); break; }
			}
			// SQLite silently turns an overflowing integer into a REAL; check here, fail closed.
			int64 Next = 0;
			if (FMath::Abs(double(Current) + double(Delta.Count)) > 9.0e18 || Current + Delta.Count < 0)
			{
				Error = FTerrainStoreResult::Bad(ETerrainPersistError::FieldOutOfRange);
				break;
			}
			Next = Current + Delta.Count;
			FSQLitePreparedStatement Stmt(*Db, TEXT("INSERT INTO balance(owner, container, item, amount) VALUES(?1, ?2, ?3, ?4) ")
				TEXT("ON CONFLICT(owner, container, item) DO UPDATE SET amount = excluded.amount;"));
			if (!Counted() || !Stmt.IsValid() || !Stmt.SetBindingValueByIndex(1, AsSigned(Delta.OwnerId))
				|| !Stmt.SetBindingValueByIndex(2, AsSigned(Delta.ContainerId)) || !Stmt.SetBindingValueByIndex(3, int64(Delta.ItemId))
				|| !Stmt.SetBindingValueByIndex(4, Next) || !Stmt.Execute())
			{
				Error = FTerrainStoreResult::Io(ETerrainStorageResult::IoError);
				break;
			}
		}
		if (!Error.IsOk())
		{
			break;
		}
		W = Input.OpSeq;
	}

	if (Error.IsOk())
	{
		FSQLitePreparedStatement Stmt(*Db, TEXT("UPDATE meta SET ival = ?1 WHERE key = 'watermark';"));
		if (!Counted() || !Stmt.IsValid() || !Stmt.SetBindingValueByIndex(1, AsSigned(W)) || !Stmt.Execute())
		{
			Error = FTerrainStoreResult::Io(ETerrainStorageResult::IoError);
		}
	}
	if (Error.IsOk() && !Exec(TEXT("COMMIT;")))
	{
		Error = FTerrainStoreResult::Io(ETerrainStorageResult::IoError);
	}
	if (!Error.IsOk())
	{
		Db->Execute(TEXT("ROLLBACK;"));
		return Fail(TEXT("settlement rolled back; W is unchanged"), Error);
	}
	Watermark = W;
	return FTerrainStoreResult::Ok();
}

bool FEntityLedger::ReadBalances(TArray<FTerrainLedgerBalance>& Out) const
{
	Out.Reset();
	if (!Db)
	{
		return false;
	}
	FSQLitePreparedStatement Stmt(*Db, TEXT("SELECT owner, container, item, amount FROM balance ORDER BY owner, container, item;"));
	if (!Stmt.IsValid())
	{
		return false;
	}
	for (;;)
	{
		const ESQLitePreparedStatementStepResult Step = Stmt.Step();
		if (Step == ESQLitePreparedStatementStepResult::Done) return true;
		if (Step != ESQLitePreparedStatementStepResult::Row) return false;
		int64 Owner = 0, Container = 0, Item = 0, Amount = 0;
		Stmt.GetColumnValueByIndex(0, Owner); Stmt.GetColumnValueByIndex(1, Container);
		Stmt.GetColumnValueByIndex(2, Item);  Stmt.GetColumnValueByIndex(3, Amount);
		FTerrainLedgerBalance& Row = Out.AddDefaulted_GetRef();
		Row.OwnerId = AsUnsigned(Owner); Row.ContainerId = AsUnsigned(Container);
		Row.ItemId = uint32(Item); Row.Amount = Amount;
	}
}

int64 FEntityLedger::CountSettlements() const
{
	if (!Db)
	{
		return -1;
	}
	FSQLitePreparedStatement Stmt(*Db, TEXT("SELECT COUNT(*) FROM settlement;"));
	int64 Count = -1;
	if (Stmt.IsValid() && Stmt.Step() == ESQLitePreparedStatementStepResult::Row) Stmt.GetColumnValueByIndex(0, Count);
	return Count;
}

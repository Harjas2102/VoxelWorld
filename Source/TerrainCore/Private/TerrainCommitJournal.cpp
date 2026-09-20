// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainCommitJournal.h"

#include "TerrainCore.h"
#include "TerrainPersistenceIndex.h"

#include "Misc/DateTime.h"

/**
 * Turning a committed operation into a durable journal record (P-003 §2, P-004 §9.3).
 */

FTerrainWorldStoreJournal::FTerrainWorldStoreJournal(FTerrainWorldStore& InStore)
	: Store(InStore)
{
}

FTerrainOpSeq FTerrainWorldStoreJournal::GetDurableHead() const
{
	const FTerrainJournalWriter* Journal = Store.GetJournal();
	return Journal != nullptr ? Journal->GetHead() : 0;
}

bool FTerrainWorldStoreJournal::RecordCommit(
	const FTerrainOp& Op,
	const FTerrainEditResult& Result,
	const FTerrainCommitIdentity& Identity,
	TConstArrayView<FTerrainChunkRevision> ChangedRevisions)
{
	LastError = FTerrainStoreResult::Ok();

	FTerrainJournalWriter* Journal = Store.GetJournal();
	if (!Store.IsOpen() || Journal == nullptr)
	{
		LastError = FTerrainStoreResult::Io(ETerrainStorageResult::NotFound);
		return false;
	}

	// P-004 §9.3 fixes RequestId at uint32. The queue counts in int64, so a value that does
	// not fit cannot be recorded faithfully -- and a silently truncated request identity is
	// worse than a refused commit, because it would make two different requests indexable as
	// one. Per connection at the admission rate this is tens of years of continuous editing,
	// so the check is a guard rather than a live concern, but it fails closed either way.
	if (Identity.RequestId == 0)
	{
		LastError = FTerrainStoreResult::Bad(ETerrainPersistError::FieldOutOfRange);
		return false;
	}

	FTerrainJournalCommitRecord Record;
	Record.WorldTag = TerrainPersistWorldTag(
		Store.GetState().Identity.World, Store.GetState().Identity.Epoch);
	Record.ServerUtcMillis = FDateTime::UtcNow().ToUnixTimestamp() * 1000;
	Record.Op              = Op;

	// Protocol 2's admission token does not exist yet (P-003 §1 proposes it; the live code
	// still has connection-local IDs). There is therefore no token to digest, and a zero
	// digest says exactly that. It is NOT a stand-in for one: when protocol 2 lands this
	// becomes the digest of a real token and old records remain readable and honestly marked.
	FMemory::Memzero(Record.TokenDigest, TerrainPersistTokenDigestBytes);

	Record.RequestId    = Identity.RequestId;
	Record.ChildOrdinal = Identity.ChildOrdinal;
	Record.ChildCount   = Identity.ChildCount;
	Record.IntentDigest = TerrainPersistComputeIntentDigest(
		Op, Record.TokenDigest, Record.RequestId, Record.ChildOrdinal, Record.ChildCount);

	// DEF-6 is open and no yield exists. NoEconomy is the only intent this prototype may write
	// (P-003 §2), and the format refuses deltas or a policy version alongside it.
	Record.EconomyKind          = ETerrainEconomyKind::NoEconomy;
	Record.EconomyPolicyVersion = 0;

	if (bPhysicalMeasured)
	{
		Record.PhysicalAvailability = ETerrainPhysicalAvailability::Measured;
		Record.Physical             = Result.Removed;
	}
	else
	{
		// Unavailable with an EMPTY list, never a measured zero. See the flag's comment.
		Record.PhysicalAvailability = ETerrainPhysicalAvailability::Unavailable;
	}

	// P-004 §9.3 requires the changed-key list sorted strictly ascending by the §6.1 index key
	// and unique. The service supplies footprint order, which is not that order.
	Record.ChangedKeys.Reserve(ChangedRevisions.Num());
	for (const FTerrainChunkRevision& Revision : ChangedRevisions)
	{
		FTerrainChangedKeyEntry Entry;
		Entry.Key       = FTerrainChunkKey(Revision.Key.X, Revision.Key.Y, Revision.Key.Z);
		Entry.BeforeRev = Revision.Before;
		Entry.AfterRev  = Revision.After;
		Record.ChangedKeys.Add(Entry);
	}
	Record.ChangedKeys.Sort([](const FTerrainChangedKeyEntry& A, const FTerrainChangedKeyEntry& B)
	{
		return TerrainIndexKeyFromChunk(A.Key) < TerrainIndexKeyFromChunk(B.Key);
	});

	const FTerrainStoreResult Result2 = Journal->AppendCommit(Record);
	if (!Result2.IsOk())
	{
		LastError = Result2;
		UE_LOG(LogTerrainCore, Error,
			TEXT("Commit journal: OpSeq %llu could not be recorded (%s). The edit must not be ")
			TEXT("broadcast, and this world's RAM now holds a change that is not durable."),
			Op.OpSeq, *Result2.ToString());
		return false;
	}
	return true;
}

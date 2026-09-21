// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "TerrainStorage.h"
#include "TerrainPersistenceRecords.h"

#include "Hash/Blake3.h"

/**
 * TerrainJournalWriter.h -- the journal's writer, and segment rotation (P-004 §9, P-003 §3).
 *
 * `TerrainPersistScanJournalSegment` already reads a segment and says what is in it, and it
 * is tested. This is the other half of that contract: the thing that produces the bytes the
 * scanner validates. The two are deliberately separate, because a writer that also defined
 * what "valid" means could never be wrong.
 *
 * WHAT IT OWNS: the active segment file, the dual anchor slots, and the running state the
 * next append depends on -- the last sequence, the record count, and the incremental digest a
 * seal record has to carry.
 *
 * WHAT IT DOES NOT OWN: the checkpoint, the object store, the commit path, recovery, replay
 * and retention. It appends records and rotates segments. Deciding *what* to append is the
 * commit path's job and it does not exist yet.
 */

/**
 * Two taxonomies, kept apart on purpose.
 *
 * `Storage` is what the device did; `Format` is what the bytes mean. A caller that conflates
 * "this disk is full" with "this journal is corrupt" will eventually delete the wrong thing,
 * so this type refuses to collapse them into one code.
 */
struct FTerrainStoreResult
{
	ETerrainStorageResult Storage = ETerrainStorageResult::Ok;
	ETerrainPersistError  Format  = ETerrainPersistError::None;

	bool IsOk() const
	{
		return Storage == ETerrainStorageResult::Ok && Format == ETerrainPersistError::None;
	}

	static FTerrainStoreResult Ok() { return FTerrainStoreResult(); }
	static FTerrainStoreResult Io(ETerrainStorageResult In)
	{
		FTerrainStoreResult Out;
		Out.Storage = In;
		return Out;
	}
	static FTerrainStoreResult Bad(ETerrainPersistError In)
	{
		FTerrainStoreResult Out;
		Out.Format = In;
		return Out;
	}

	TERRAINCORE_API FString ToString() const;
};

/** What the writer knows about the journal right now. */
struct FTerrainJournalState
{
	uint64        ActiveSegmentId = 0;
	FTerrainOpSeq FirstOpSeq = 0;          // the active segment's first sequence
	FTerrainOpSeq LastOpSeq = 0;           // 0 when the active segment holds no records
	int32         CommitRecordCount = 0;   // in the active segment only
	bool          bSealed = false;

	/** Set when Open() found an incomplete final record. Appending is refused until repaired. */
	bool  bTornTail = false;
	int32 TornTailBytes = 0;

	/**
	 * Set when a write failed after the journal had been mutated, or when a torn tail was
	 * found. P-003 §2 calls this an uncertain storage fault: close admission, drain, and
	 * recover from disk. The writer refuses to append while it is set rather than layering a
	 * good record on top of an unknown one.
	 */
	bool bBlocked = false;
};

/**
 * Appends to the journal and rotates its segments.
 *
 * THE ORDERING THIS CLASS EXISTS TO ENFORCE (P-004 §9.5, normative):
 *   create and flush the new segment's header and namespace
 *   → publish and flush the **inactive** anchor slot
 *   → append records.
 * The preceding segment is sealed before rotation. Nothing here reorders those steps, and
 * the whole point of routing them through one object is that nothing else can either.
 */
class TERRAINCORE_API FTerrainJournalWriter
{
public:
	FTerrainJournalWriter(ITerrainStorageDevice& InDevice, const FTerrainPersistIdentity& InIdentity);

	/**
	 * Creates the journal directory, the first segment and both anchor slots.
	 *
	 * The first segment has no predecessor, so its header carries a zero predecessor ID and a
	 * zero seal digest, and P-004 §9.1 makes that legal only for the first segment.
	 */
	FTerrainStoreResult Create(uint64 SegmentId, FTerrainOpSeq FirstOpSeq, int64 CreatedUtcMillis);

	/**
	 * Reads the anchors, finds the active segment, validates it and restores the append state.
	 *
	 * Refuses, rather than guessing, when: no anchor validates; the anchored segment file is
	 * missing (P-003 §3 -- a named-but-absent segment is corruption, never "no more ops"); the
	 * segment's own header disagrees with the anchor; or a newer **unanchored non-empty**
	 * segment exists, which is a protocol violation only the offline repair path may consider.
	 * A newer unanchored **empty** segment is an orphan and is ignored.
	 */
	FTerrainStoreResult Open();

	/**
	 * Appends one commit record.
	 *
	 * The record's `Op.OpSeq` must be exactly the next sequence and its `WorldTag` must match,
	 * because a journal whose sequence can skip is a journal whose replay can silently omit an
	 * edit. Both are checked here rather than trusted from above.
	 */
	/** OutDigest, when given, receives the P-004 §9.3 record digest of exactly the bytes appended. */
	FTerrainStoreResult AppendCommit(const FTerrainJournalCommitRecord& Record, FTerrainDigest* OutDigest = nullptr);

	/** Appends the seal record. The segment accepts nothing afterwards. */
	FTerrainStoreResult Seal(int64 SealedUtcMillis);

	/**
	 * Seals the active segment if needed, creates the next one, publishes the anchor, switches.
	 *
	 * The new segment's `FirstOpSeq` continues the sequence and its `PredecessorSealDigest` is
	 * the BLAKE3 of the seal record frame just written -- the continuity evidence P-003 §5
	 * requires at both ends of the join, which is why sealing and rotating are one operation
	 * and not two a caller could get out of order.
	 *
	 * **A FAILED ROTATION MUST BE RETRIED WITH A DIFFERENT SEGMENT ID.** If the new segment's
	 * header was written and the anchor publication then failed, the file exists and is an
	 * orphan; writing it again is `AlreadyExists`, because immutable objects are never
	 * overwritten. That is the safe failure -- the alternative would be a writer that
	 * overwrites a file it cannot prove is its own -- but it does mean segment IDs are
	 * consumed by failed attempts, and a caller that keeps retrying one ID makes no progress.
	 * Reclaiming an orphan's ID is the retention path's job and is not built.
	 *
	 * **Not for a production path as written (P-005 §8).** Rotation creates a segment file, and
	 * an open world must create no names: whether a new directory entry survives a power cut is
	 * exactly what R-015 could not prove. Production never rotates today. Journal trimming, when
	 * built, must rotate within a pre-created ring of segment files reset by truncation -- the
	 * container pattern -- rather than call this.
	 */
	FTerrainStoreResult Rotate(uint64 NewSegmentId, int64 UtcMillis);

	const FTerrainJournalState& GetState() const { return State; }

	/** The highest committed sequence in the active segment, or its predecessor's when empty. */
	FTerrainOpSeq GetHead() const;

	/** The sequence the next appended record must carry. */
	FTerrainOpSeq GetNextOpSeq() const;

	/** Segment IDs found on disk that the anchor does not name, in ascending order. */
	const TArray<uint64>& GetOrphanSegments() const { return OrphanSegments; }

private:
	FTerrainStoreResult WriteSegmentHeader(uint64 SegmentId, FTerrainOpSeq FirstOpSeq,
	                                       uint64 PredecessorId, const FTerrainDigest& PredecessorSeal,
	                                       int64 CreatedUtcMillis);
	FTerrainStoreResult PublishAnchor(uint64 ActiveSegmentId, FTerrainOpSeq ActiveFirstOpSeq, int64 UtcMillis);
	FTerrainStoreResult ListSegments(TArray<uint64>& OutSegmentIds) const;

	ITerrainStorageDevice&  Device;
	FTerrainPersistIdentity Identity;
	uint64                  WorldTag = 0;
	FTerrainSlotPair        Anchors;

	FTerrainJournalState State;
	FString              ActiveSegmentPath;
	uint64               AnchorGeneration = 0;

	/** The predecessor identity the NEXT segment header will carry. */
	uint64         PredecessorSegmentId = 0;
	FTerrainOpSeq  PredecessorLastOpSeq = 0;
	FTerrainDigest PredecessorSealDigest;

	/**
	 * BLAKE3 over every commit frame in the active segment, in append order.
	 *
	 * An accumulator, not a buffer: a segment can hold a very large number of records and
	 * keeping their bytes to hash later would make the writer's memory grow with the journal.
	 * Sealing therefore costs no re-read. The state cannot be restored from a digest, so
	 * Open() rebuilds it by walking the segment's frames -- which is also a second,
	 * independent pass over bytes the scanner has already validated.
	 */
	FBlake3 RecordsHash;

	TArray<uint64> OrphanSegments;
	bool           bOpen = false;
};

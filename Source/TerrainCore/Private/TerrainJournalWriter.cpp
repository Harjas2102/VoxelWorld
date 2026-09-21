// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainJournalWriter.h"

#include "TerrainCore.h"

/**
 * The journal writer (P-004 §9, P-003 §3, §5).
 *
 * The scanner in TerrainPersistenceRecords.cpp decides what a valid segment is; this file only
 * produces bytes and is checked by it. Every test below opens what it wrote with the scanner
 * rather than with a private reader, so "the writer thinks this is fine" is never the evidence.
 */

namespace
{

	/** A segment file holding only its header object -- created, never appended to. */
	constexpr int64 EmptySegmentSize = TerrainPersistObjectHeaderSize + TerrainPersistSegmentHeaderBodySize;
}

FString FTerrainStoreResult::ToString() const
{
	if (Format != ETerrainPersistError::None)
	{
		return FString::Printf(TEXT("format=%s"), TerrainPersistErrorName(Format));
	}
	return FString::Printf(TEXT("storage=%s"), TerrainStorageResultName(Storage));
}

FTerrainJournalWriter::FTerrainJournalWriter(
	ITerrainStorageDevice& InDevice, const FTerrainPersistIdentity& InIdentity)
	: Device(InDevice)
	, Identity(InIdentity)
	, WorldTag(TerrainPersistWorldTag(InIdentity.World, InIdentity.Epoch))
	, Anchors(InDevice, ETerrainPersistObjectType::JournalAnchorSlot,
	          TerrainStoragePaths::AnchorSlot(0), TerrainStoragePaths::AnchorSlot(1))
{
}

FTerrainOpSeq FTerrainJournalWriter::GetHead() const
{
	// An empty active segment means the head is whatever its predecessor ended at. Returning
	// 0 there would say "no history", which is a different and much more dangerous claim.
	return State.LastOpSeq != 0 ? State.LastOpSeq : PredecessorLastOpSeq;
}

FTerrainOpSeq FTerrainJournalWriter::GetNextOpSeq() const
{
	return State.LastOpSeq != 0 ? State.LastOpSeq + 1 : State.FirstOpSeq;
}

FTerrainStoreResult FTerrainJournalWriter::WriteSegmentHeader(
	uint64 SegmentId, FTerrainOpSeq FirstOpSeq, uint64 PredecessorId,
	const FTerrainDigest& PredecessorSeal, int64 CreatedUtcMillis)
{
	FTerrainJournalSegmentHeader Header;
	Header.SegmentId             = SegmentId;
	Header.FirstOpSeq            = FirstOpSeq;
	Header.PredecessorSegmentId  = PredecessorId;
	Header.PredecessorSealDigest = PredecessorSeal;
	Header.CreatedUtcMillis      = CreatedUtcMillis;
	Header.bHasPredecessor       = PredecessorId != 0;

	TArray<uint8> Body;
	const ETerrainPersistError BodyError = TerrainPersistEncodeSegmentHeaderBody(Header, Body);
	if (BodyError != ETerrainPersistError::None)
	{
		return FTerrainStoreResult::Bad(BodyError);
	}

	TArray<uint8> Object;
	const ETerrainPersistError ObjectError = TerrainPersistEncodeObject(
		ETerrainPersistObjectType::JournalSegmentHeader, Identity, Body, Object);
	if (ObjectError != ETerrainPersistError::None)
	{
		return FTerrainStoreResult::Bad(ObjectError);
	}

	const ETerrainStorageResult Result =
		Device.WriteNew(TerrainStoragePaths::JournalSegment(SegmentId), Object);
	return Result == ETerrainStorageResult::Ok
		? FTerrainStoreResult::Ok() : FTerrainStoreResult::Io(Result);
}

FTerrainStoreResult FTerrainJournalWriter::PublishAnchor(
	uint64 InActiveSegmentId, FTerrainOpSeq ActiveFirstOpSeq, int64 UtcMillis)
{
	FTerrainJournalAnchor Anchor;
	// Advanced before the write is known to have succeeded, so a failed publication consumes a
	// generation. That is deliberate: generations only have to INCREASE for boot to order the
	// two slots, and reusing one after a failure would risk two different bodies claiming the
	// same generation -- which is the one thing that ordering cannot survive.
	Anchor.AnchorGeneration        = ++AnchorGeneration;
	Anchor.ActiveSegmentId         = InActiveSegmentId;
	Anchor.ActiveSegmentFirstOpSeq = ActiveFirstOpSeq;
	Anchor.PredecessorSegmentId    = PredecessorSegmentId;
	Anchor.PredecessorLastOpSeq    = PredecessorLastOpSeq;
	Anchor.PredecessorSealDigest   = PredecessorSealDigest;
	Anchor.PublishedUtcMillis      = UtcMillis;
	Anchor.bHasPredecessor         = PredecessorSegmentId != 0;

	TArray<uint8> Body;
	const ETerrainPersistError BodyError = TerrainPersistEncodeAnchorBody(Anchor, Body);
	if (BodyError != ETerrainPersistError::None)
	{
		return FTerrainStoreResult::Bad(BodyError);
	}

	const ETerrainStorageResult Result = Anchors.Publish(Identity, Body);
	return Result == ETerrainStorageResult::Ok
		? FTerrainStoreResult::Ok() : FTerrainStoreResult::Io(Result);
}

FTerrainStoreResult FTerrainJournalWriter::ListSegments(TArray<uint64>& OutSegmentIds) const
{
	TArray<FString> Names;
	const ETerrainStorageResult Result =
		Device.ListFiles(TerrainStoragePaths::JournalDirectory, Names);
	if (Result != ETerrainStorageResult::Ok)
	{
		return FTerrainStoreResult::Io(Result);
	}

	OutSegmentIds.Reset();
	for (const FString& Name : Names)
	{
		uint64 SegmentId = 0;
		if (TerrainStoragePaths::ParseJournalSegment(Name, SegmentId))
		{
			OutSegmentIds.Add(SegmentId);
		}
		// Anything else in journal/ is an anchor slot or something this protocol did not
		// write. Neither is a segment, and neither is grounds to refuse to boot.
	}
	OutSegmentIds.Sort();
	return FTerrainStoreResult::Ok();
}

FTerrainStoreResult FTerrainJournalWriter::Create(
	uint64 SegmentId, FTerrainOpSeq FirstOpSeq, int64 CreatedUtcMillis)
{
	if (SegmentId == 0 || FirstOpSeq == 0)
	{
		// P-004 §9.1: both are 1-based, and segment 0 is reserved to mean "no predecessor".
		return FTerrainStoreResult::Bad(ETerrainPersistError::FieldOutOfRange);
	}

	const ETerrainStorageResult DirectoryResult =
		Device.EnsureDirectory(TerrainStoragePaths::JournalDirectory);
	if (DirectoryResult != ETerrainStorageResult::Ok)
	{
		return FTerrainStoreResult::Io(DirectoryResult);
	}

	// Step one of P-004 §9.5's order: the segment header and its namespace exist and are
	// flushed BEFORE anything names them.
	const FTerrainStoreResult HeaderResult =
		WriteSegmentHeader(SegmentId, FirstOpSeq, 0, FTerrainDigest(), CreatedUtcMillis);
	if (!HeaderResult.IsOk())
	{
		return HeaderResult;
	}

	// Step two: publish the anchor. Bootstrapping creates both slots with the same content,
	// because there is no previous state for the alternation to protect.
	FTerrainJournalAnchor Anchor;
	Anchor.AnchorGeneration        = 1;
	Anchor.ActiveSegmentId         = SegmentId;
	Anchor.ActiveSegmentFirstOpSeq = FirstOpSeq;
	Anchor.PublishedUtcMillis      = CreatedUtcMillis;
	Anchor.bHasPredecessor         = false;

	TArray<uint8> AnchorBody;
	const ETerrainPersistError AnchorError = TerrainPersistEncodeAnchorBody(Anchor, AnchorBody);
	if (AnchorError != ETerrainPersistError::None)
	{
		return FTerrainStoreResult::Bad(AnchorError);
	}

	const ETerrainStorageResult AnchorResult = Anchors.Create(Identity, AnchorBody);
	if (AnchorResult != ETerrainStorageResult::Ok)
	{
		return FTerrainStoreResult::Io(AnchorResult);
	}

	AnchorGeneration = 1;
	return Open();
}

FTerrainStoreResult FTerrainJournalWriter::Open()
{
	bOpen = false;
	State = FTerrainJournalState();
	OrphanSegments.Reset();
	RecordsHash.Reset();

	// --- the anchor decides which segment is active -------------------------------------
	FTerrainSlotState SlotZero, SlotOne;
	int32 BestIndex = INDEX_NONE;
	const ETerrainStorageResult AnchorRead = Anchors.Read(Identity, SlotZero, SlotOne, BestIndex);
	if (AnchorRead != ETerrainStorageResult::Ok)
	{
		return FTerrainStoreResult::Io(AnchorRead);
	}
	if (BestIndex == INDEX_NONE)
	{
		if (!SlotZero.bPresent && !SlotOne.bPresent)
		{
			return FTerrainStoreResult::Io(ETerrainStorageResult::NotFound);
		}
		// Present but neither validates: the journal has no head it is allowed to trust.
		return FTerrainStoreResult::Bad(
			SlotZero.bPresent ? SlotZero.Error : SlotOne.Error);
	}

	const FTerrainSlotState& Best = (BestIndex == 0) ? SlotZero : SlotOne;

	FTerrainJournalAnchor Anchor;
	const ETerrainPersistError AnchorError = TerrainPersistDecodeAnchorBody(Best.Body, Anchor);
	if (AnchorError != ETerrainPersistError::None)
	{
		return FTerrainStoreResult::Bad(AnchorError);
	}

	AnchorGeneration      = Anchor.AnchorGeneration;
	PredecessorSegmentId  = Anchor.PredecessorSegmentId;
	PredecessorLastOpSeq  = Anchor.PredecessorLastOpSeq;
	PredecessorSealDigest = Anchor.PredecessorSealDigest;

	// --- what is actually on disk ---------------------------------------------------------
	TArray<uint64> SegmentIds;
	const FTerrainStoreResult ListResult = ListSegments(SegmentIds);
	if (!ListResult.IsOk())
	{
		return ListResult;
	}

	if (!SegmentIds.Contains(Anchor.ActiveSegmentId))
	{
		// P-003 §3, stated exactly: a named segment that is missing is corruption, and never
		// "there are no more ops". Treating it as the latter would silently drop history.
		return FTerrainStoreResult::Bad(ETerrainPersistError::ShortBuffer);
	}

	for (const uint64 SegmentId : SegmentIds)
	{
		if (SegmentId <= Anchor.ActiveSegmentId)
		{
			continue;
		}

		const int64 SegmentSize = Device.Size(TerrainStoragePaths::JournalSegment(SegmentId));
		if (SegmentSize > EmptySegmentSize)
		{
			// A newer segment that holds records and that no anchor names. Ordinary boot never
			// adopts it: only the explicit offline repair path may consider it, and that path
			// does not exist yet (P-003 §3).
			UE_LOG(LogTerrainCore, Error,
				TEXT("Journal: segment %llu is newer than the anchored segment %llu and holds records. ")
				TEXT("Ordinary boot refuses this; it needs the offline repair path."),
				SegmentId, Anchor.ActiveSegmentId);
			return FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
		}

		// Created, never appended to: an orphan of an interrupted rotation. Ignored, reported.
		OrphanSegments.Add(SegmentId);
	}

	// --- the active segment ----------------------------------------------------------------
	ActiveSegmentPath = TerrainStoragePaths::JournalSegment(Anchor.ActiveSegmentId);

	TArray<uint8> SegmentBytes;
	const ETerrainStorageResult SegmentRead = Device.Read(ActiveSegmentPath, SegmentBytes);
	if (SegmentRead != ETerrainStorageResult::Ok)
	{
		return FTerrainStoreResult::Io(SegmentRead);
	}

	FTerrainJournalScanResult Scan;
	const ETerrainPersistError ScanError =
		TerrainPersistScanJournalSegment(SegmentBytes, Identity, /*bActiveSegment=*/true, Scan);
	if (ScanError != ETerrainPersistError::None)
	{
		return FTerrainStoreResult::Bad(ScanError);
	}

	if (Scan.Header.SegmentId != Anchor.ActiveSegmentId
		|| Scan.Header.FirstOpSeq != Anchor.ActiveSegmentFirstOpSeq)
	{
		// The anchor and the segment's own header disagree about what this segment is. One of
		// them is wrong and nothing here can tell which.
		return FTerrainStoreResult::Bad(ETerrainPersistError::FieldOutOfRange);
	}
	if (Anchor.bHasPredecessor
		&& Scan.Header.PredecessorSealDigest != Anchor.PredecessorSealDigest)
	{
		// Continuity evidence must agree at both ends of the join (P-003 §5).
		return FTerrainStoreResult::Bad(ETerrainPersistError::BaseMismatch);
	}

	State.ActiveSegmentId   = Scan.Header.SegmentId;
	State.FirstOpSeq        = Scan.Header.FirstOpSeq;
	State.LastOpSeq         = Scan.LastOpSeq;
	State.CommitRecordCount = Scan.CommitRecordCount;
	State.bSealed           = Scan.bSealed;
	State.bTornTail         = Scan.bTornTail;
	State.TornTailBytes     = Scan.TornTailBytes;

	// A torn tail is not repaired here. P-003 §3 requires the torn bytes to be preserved
	// diagnostically before any repair, and repair is the recovery increment's job with its
	// own policy. Until then the journal is readable and refuses to be written.
	State.bBlocked = Scan.bTornTail;
	if (Scan.bTornTail)
	{
		UE_LOG(LogTerrainCore, Warning,
			TEXT("Journal: segment %llu has a %d-byte torn tail after OpSeq %llu. ")
			TEXT("Appending is refused until it is repaired; this writer does not truncate."),
			State.ActiveSegmentId, State.TornTailBytes, State.LastOpSeq);
	}

	// Rebuild the running digest by walking the frames the scanner just validated. It cannot
	// be restored from a digest, and this second pass is also an independent check that the
	// frames are where the scanner said they were.
	{
		int32 Cursor = TerrainPersistObjectHeaderSize + TerrainPersistSegmentHeaderBodySize;
		int32 Walked = 0;
		while (Cursor < Scan.GoodBytes)
		{
			const TArrayView<const uint8> Remaining(
				SegmentBytes.GetData() + Cursor, Scan.GoodBytes - Cursor);

			int32 Length = 0;
			ETerrainJournalRecordType Type = ETerrainJournalRecordType::Commit;
			if (TerrainPersistPeekRecordFrame(Remaining, Length, Type) != ETerrainPersistError::None)
			{
				return FTerrainStoreResult::Bad(ETerrainPersistError::BodyChecksumMismatch);
			}
			if (Type == ETerrainJournalRecordType::Commit)
			{
				RecordsHash.Update(SegmentBytes.GetData() + Cursor, static_cast<uint64>(Length));
				++Walked;
			}
			Cursor += Length;
		}
		if (Walked != Scan.CommitRecordCount)
		{
			return FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
		}
	}

	bOpen = true;
	return FTerrainStoreResult::Ok();
}

FTerrainStoreResult FTerrainJournalWriter::AppendCommit(const FTerrainJournalCommitRecord& Record, FTerrainDigest* OutDigest)
{
	if (!bOpen)
	{
		return FTerrainStoreResult::Io(ETerrainStorageResult::NotFound);
	}
	if (State.bBlocked)
	{
		// An uncertain storage fault, or a torn tail. Layering a good record on top of an
		// unknown one is how a journal stops being a journal.
		return FTerrainStoreResult::Io(ETerrainStorageResult::IoError);
	}
	if (State.bSealed)
	{
		return FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
	}
	if (Record.WorldTag != WorldTag)
	{
		return FTerrainStoreResult::Bad(ETerrainPersistError::WorldMismatch);
	}
	if (Record.Op.OpSeq != GetNextOpSeq())
	{
		// Checked here rather than trusted from above: a journal whose sequence can skip is a
		// journal whose replay can silently omit an edit.
		return FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
	}

	TArray<uint8> Frame;
	const ETerrainPersistError EncodeError = TerrainPersistEncodeCommitRecord(Record, Frame);
	if (EncodeError != ETerrainPersistError::None)
	{
		return FTerrainStoreResult::Bad(EncodeError);
	}

	const ETerrainStorageResult Result = Device.Append(ActiveSegmentPath, Frame);
	if (Result != ETerrainStorageResult::Ok)
	{
		// After the mutation was attempted, the journal's physical state is unknown: the write
		// may have landed whole, partly, or not at all. P-003 §2 calls that an uncertain
		// storage fault and requires closing admission rather than carrying on.
		State.bBlocked = true;
		UE_LOG(LogTerrainCore, Error,
			TEXT("Journal: append of OpSeq %llu failed (%s). The segment's tail is now uncertain ")
			TEXT("and this writer is closed until the world is reopened."),
			Record.Op.OpSeq, TerrainStorageResultName(Result));
		return FTerrainStoreResult::Io(Result);
	}

	RecordsHash.Update(Frame.GetData(), static_cast<uint64>(Frame.Num()));
	if (OutDigest != nullptr)
	{
		TerrainPersistComputeRecordDigest(Frame, *OutDigest);
	}
	State.LastOpSeq = Record.Op.OpSeq;
	++State.CommitRecordCount;
	return FTerrainStoreResult::Ok();
}

FTerrainStoreResult FTerrainJournalWriter::Seal(int64 SealedUtcMillis)
{
	if (!bOpen || State.bBlocked)
	{
		return FTerrainStoreResult::Io(ETerrainStorageResult::IoError);
	}
	if (State.bSealed)
	{
		return FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
	}

	FTerrainJournalSealRecord Seal;
	Seal.WorldTag          = WorldTag;
	Seal.LastOpSeq         = State.LastOpSeq;
	Seal.CommitRecordCount = static_cast<uint64>(State.CommitRecordCount);
	Seal.SealedUtcMillis   = SealedUtcMillis;
	{
		const FBlake3Hash Hash = RecordsHash.Finalize();
		FMemory::Memcpy(Seal.RecordsDigest.Bytes, Hash.GetBytes(), TerrainPersistDigestSize);
	}

	TArray<uint8> Frame;
	const ETerrainPersistError EncodeError = TerrainPersistEncodeSealRecord(Seal, Frame);
	if (EncodeError != ETerrainPersistError::None)
	{
		return FTerrainStoreResult::Bad(EncodeError);
	}

	const ETerrainStorageResult Result = Device.Append(ActiveSegmentPath, Frame);
	if (Result != ETerrainStorageResult::Ok)
	{
		State.bBlocked = true;
		return FTerrainStoreResult::Io(Result);
	}

	// The NEXT segment's continuity evidence is the digest of this seal FRAME, not of the
	// records it summarises (P-004 §9.4).
	PredecessorSegmentId  = State.ActiveSegmentId;
	PredecessorLastOpSeq  = State.LastOpSeq;
	PredecessorSealDigest = TerrainPersistDigest(Frame);

	State.bSealed = true;
	return FTerrainStoreResult::Ok();
}

FTerrainStoreResult FTerrainJournalWriter::Rotate(uint64 NewSegmentId, int64 UtcMillis)
{
	if (!bOpen || State.bBlocked)
	{
		return FTerrainStoreResult::Io(ETerrainStorageResult::IoError);
	}
	if (NewSegmentId <= State.ActiveSegmentId)
	{
		// Segment IDs increase, because boot uses the ordering to tell an orphan from history.
		return FTerrainStoreResult::Bad(ETerrainPersistError::FieldOutOfRange);
	}

	// P-003 §5: seal the preceding segment BEFORE rotating. Doing it here rather than leaving
	// it to the caller is the reason the two cannot be performed out of order.
	if (!State.bSealed)
	{
		const FTerrainStoreResult SealResult = Seal(UtcMillis);
		if (!SealResult.IsOk())
		{
			return SealResult;
		}
	}

	const FTerrainOpSeq NewFirstOpSeq = PredecessorLastOpSeq + 1;

	// P-004 §9.5, in order: header and namespace first...
	const FTerrainStoreResult HeaderResult = WriteSegmentHeader(
		NewSegmentId, NewFirstOpSeq, PredecessorSegmentId, PredecessorSealDigest, UtcMillis);
	if (!HeaderResult.IsOk())
	{
		// The anchor still names the old segment, which is sealed and complete. An orphan
		// segment file may exist; boot ignores an empty one and refuses a non-empty one.
		return HeaderResult;
	}

	// ...then the inactive anchor slot...
	const FTerrainStoreResult AnchorResult = PublishAnchor(NewSegmentId, NewFirstOpSeq, UtcMillis);
	if (!AnchorResult.IsOk())
	{
		// Same containment: the old anchor is intact and still names the sealed segment, so
		// boot recovers to a complete state and the new segment is an ignorable orphan.
		return AnchorResult;
	}

	// ...and only then does anything append to the new segment.
	ActiveSegmentPath       = TerrainStoragePaths::JournalSegment(NewSegmentId);
	State.ActiveSegmentId   = NewSegmentId;
	State.FirstOpSeq        = NewFirstOpSeq;
	State.LastOpSeq         = 0;
	State.CommitRecordCount = 0;
	State.bSealed           = false;
	RecordsHash.Reset();

	return FTerrainStoreResult::Ok();
}

// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainJournalReplay.h"

#include "TerrainCore.h"
#include "TerrainChunk.h"
#include "TerrainOpGeometry.h"
#include "TerrainCheckpoint.h"

/**
 * Rebuilding a world from its journal (P-003 §3, terrain pass).
 */

namespace
{

	/**
	 * Pins every chunk one operation's footprint touches.
	 *
	 * A conforming backend refuses an edit it does not have loaded, so without this every
	 * replayed op would fail. Each chunk gets its own RETAINED pin: a single interest moved
	 * from op to op would leave every earlier footprint unresident, and on a backend that
	 * evicts that means the replayed world is discarded as it is being built. An earlier
	 * version of this file did exactly that and passed only because the ops happened to be
	 * close enough together that the last interest still covered the first.
	 */
	bool PinFootprint(ITerrainBackend& Backend, const FTerrainOp& Op,
	                  const FTerrainBaseDescriptor& Base, FTerrainResidencyPins& Pins)
	{
		FTerrainBox Bounds;
		if (!TerrainOpBounds(Op, Bounds))
		{
			return false;
		}
		if (!(Base.VoxelSizeMicrometres > 0))
		{
			return false;
		}

		TArray<FTerrainChunkKey> Keys;
		if (!TerrainChunkKeysForBox(Bounds, Keys))
		{
			return false;
		}
		for (const FTerrainChunkKey& Key : Keys)
		{
			Pins.Pin(Backend, Key, Base);
		}
		return true;
	}
}

FTerrainStoreResult TerrainReplayJournal(
	FTerrainWorldStore& Store,
	ITerrainBackend& Backend,
	FTerrainRevisionIndex& Revisions,
	FTerrainReplayStats& OutStats)
{
	OutStats = FTerrainReplayStats();
	const double Started = FPlatformTime::Seconds();

	if (!Store.IsOpen() || Store.GetJournal() == nullptr)
	{
		return FTerrainStoreResult::Io(ETerrainStorageResult::NotFound);
	}

	const FTerrainPersistIdentity& Identity = Store.GetState().Identity;
	const FTerrainBaseDescriptor&  Base     = Store.GetState().Base;
	ITerrainStorageDevice&         Device   = Store.GetDevice();

	// The caller has restored the checkpoint; apply only the committed tail after its cut.
	const FTerrainOpSeq G = Store.GetState().Checkpoint.G;
	const uint64 ActiveSegmentId = Store.GetJournal()->GetState().ActiveSegmentId;

	// --- the segment chain, ascending -------------------------------------------------------
	TArray<FString> Names;
	const ETerrainStorageResult ListResult =
		Device.ListFiles(TerrainStoragePaths::JournalDirectory, Names);
	if (ListResult != ETerrainStorageResult::Ok)
	{
		return FTerrainStoreResult::Io(ListResult);
	}

	for (const FString& Name : Names)
	{
		uint64 SegmentId = 0;
		if (TerrainStoragePaths::ParseJournalSegment(Name, SegmentId) && SegmentId <= ActiveSegmentId)
		{
			OutStats.Segments.Add(SegmentId);
		}
	}
	OutStats.Segments.Sort();
	if (OutStats.Segments.IsEmpty())
	{
		return FTerrainStoreResult::Bad(ETerrainPersistError::ShortBuffer);
	}

	const uint64 WorldTag = TerrainPersistWorldTag(Identity.World, Identity.Epoch);
	FTerrainOpSeq TailExpected = G + 1;
	FTerrainOpSeq Expected = 0;   // 0 until the first record fixes the start of the chain
	FTerrainResidencyPins Pins(TerrainReplayPinBaseId);

	for (const uint64 SegmentId : OutStats.Segments)
	{
		TArray<uint8> SegmentBytes;
		const ETerrainStorageResult ReadResult =
			Device.Read(TerrainStoragePaths::JournalSegment(SegmentId), SegmentBytes);
		if (ReadResult != ETerrainStorageResult::Ok)
		{
			return FTerrainStoreResult::Io(ReadResult);
		}

		const bool bActive = (SegmentId == ActiveSegmentId);

		FTerrainJournalScanResult Scan;
		const ETerrainPersistError ScanError =
			TerrainPersistScanJournalSegment(SegmentBytes, Identity, bActive, Scan);
		if (ScanError != ETerrainPersistError::None)
		{
			return FTerrainStoreResult::Bad(ScanError);
		}

		// Walk this segment's frames. The scanner has already validated framing, contiguity
		// within the segment and identity; this pass is what turns records into operations.
		int32 Cursor = TerrainPersistObjectHeaderSize + TerrainPersistSegmentHeaderBodySize;
		while (Cursor < Scan.GoodBytes)
		{
			const TArrayView<const uint8> Remaining(
				SegmentBytes.GetData() + Cursor, Scan.GoodBytes - Cursor);

			int32 Length = 0;
			ETerrainJournalRecordType Type = ETerrainJournalRecordType::Commit;
			const ETerrainPersistError FrameError =
				TerrainPersistPeekRecordFrame(Remaining, Length, Type);
			if (FrameError != ETerrainPersistError::None)
			{
				return FTerrainStoreResult::Bad(FrameError);
			}

			if (Type == ETerrainJournalRecordType::Seal)
			{
				Cursor += Length;
				continue;
			}

			FTerrainJournalCommitRecord Record;
			const ETerrainPersistError RecordError = TerrainPersistDecodeCommitRecord(
				TArrayView<const uint8>(SegmentBytes.GetData() + Cursor, Length), WorldTag, Record);
			if (RecordError != ETerrainPersistError::None)
			{
				return FTerrainStoreResult::Bad(RecordError);
			}

			++OutStats.RecordsRead;

			// Contiguity ACROSS segments, which no single segment's scan can check. P-003 §3
			// requires the complete contiguous range; a gap here means a segment is missing
			// and the world cannot be rebuilt, however healthy each surviving file looks.
			if (Expected != 0 && Record.Op.OpSeq != Expected)
			{
				UE_LOG(LogTerrainCore, Error,
					TEXT("Replay: expected OpSeq %llu and found %llu. The journal chain is not ")
					TEXT("contiguous, so this world cannot be rebuilt from it."),
					Expected, Record.Op.OpSeq);
				return FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
			}
			Expected = Record.Op.OpSeq + 1;

			if (OutStats.FirstOpSeq == 0)
			{
				OutStats.FirstOpSeq = Record.Op.OpSeq;
			}
			OutStats.LastOpSeq = Record.Op.OpSeq;

			// Everything at or below the checkpoint cut is already in the restored state.
			if (Record.Op.OpSeq <= G)
			{
				Cursor += Length;
				continue;
			}

			if (Record.Op.OpSeq != TailExpected || Record.Op.OpSeq > Store.GetJournal()->GetHead())
			{
				return FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
			}
			++TailExpected;

			// --- re-apply, whole, exactly once ------------------------------------------------
			if (!PinFootprint(Backend, Record.Op, Base, Pins))
			{
				return FTerrainStoreResult::Bad(ETerrainPersistError::FieldOutOfRange);
			}

			FTerrainEditResult Applied;
			if (!Backend.ApplyOp(Record.Op, Applied))
			{
				UE_LOG(LogTerrainCore, Error,
					TEXT("Replay: OpSeq %llu was recorded as committed and the backend now refuses ")
					TEXT("it. The journal and the base do not describe the same world."),
					Record.Op.OpSeq);
				return FTerrainStoreResult::Bad(ETerrainPersistError::BaseMismatch);
			}

			// P-003 §3: validate results and revisions rather than trusting them. A record that
			// says it changed chunks the backend did not is a record describing another world.
			if (Applied.AffectedChunks.Num() != Record.ChangedKeys.Num())
			{
				UE_LOG(LogTerrainCore, Error,
					TEXT("Replay: OpSeq %llu changed %d chunks on replay and %d when it was ")
					TEXT("committed."),
					Record.Op.OpSeq, Applied.AffectedChunks.Num(), Record.ChangedKeys.Num());
				return FTerrainStoreResult::Bad(ETerrainPersistError::BaseMismatch);
			}

			for (const FTerrainChangedKeyEntry& Entry : Record.ChangedKeys)
			{
				if (!Applied.AffectedChunks.Contains(Entry.Key))
				{
					return FTerrainStoreResult::Bad(ETerrainPersistError::BaseMismatch);
				}
				if (Revisions.GetRevision(Entry.Key) != Entry.BeforeRev)
				{
					UE_LOG(LogTerrainCore, Error,
						TEXT("Replay: OpSeq %llu expected chunk revision %u and found %u."),
						Record.Op.OpSeq, Entry.BeforeRev, Revisions.GetRevision(Entry.Key));
					return FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
				}
			}

			if (!Revisions.TryBumpRevisions(Applied.AffectedChunks))
			{
				return FTerrainStoreResult::Bad(ETerrainPersistError::FieldOutOfRange);
			}
			for (const FTerrainChangedKeyEntry& Entry : Record.ChangedKeys)
			{
				if (Revisions.GetRevision(Entry.Key) != Entry.AfterRev)
				{
					return FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
				}
			}

			++OutStats.OpsApplied;
			for (const FTerrainChangedKeyEntry& Entry : Record.ChangedKeys)
			{
				OutStats.DirtyChunks.Add(Entry.Key, Record.Op.OpSeq);
			}
			Cursor += Length;
		}
	}

	// Pins remain held until backend shutdown: on-demand reload from a checkpoint is not built.
	if (TailExpected != Store.GetJournal()->GetHead() + 1)
	{
		return FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
	}

	OutStats.Seconds = FPlatformTime::Seconds() - Started;
	UE_LOG(LogTerrainCore, Log,
		TEXT("Replay: %d of %d records applied from G=%llu to H=%llu across %d segment(s) in %.3f s"),
		OutStats.OpsApplied, OutStats.RecordsRead, G, OutStats.LastOpSeq,
		OutStats.Segments.Num(), OutStats.Seconds);
	return FTerrainStoreResult::Ok();
}

// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainJournalReplay.h"

#include "TerrainCore.h"
#include "TerrainChunk.h"
#include "TerrainOpGeometry.h"

/**
 * Rebuilding a world from its journal (P-003 §3, terrain pass).
 */

namespace
{

	/** The interest id replay takes. Deliberately NOT released here -- see below. */
	constexpr uint32 ReplayInterestId = TerrainReplayInterestId;

	/**
	 * Makes one operation's whole footprint resident.
	 *
	 * A conforming backend refuses an edit it does not have loaded, so without this every
	 * replayed op would fail. The interest is centred on the op and sized to its footprint
	 * plus a chunk of margin, because residency is granted per chunk and a footprint that
	 * merely touches a chunk still needs all of it.
	 */
	bool MakeFootprintResident(ITerrainBackend& Backend, const FTerrainOp& Op,
	                           const FTerrainBaseDescriptor& Base)
	{
		FTerrainBox Bounds;
		if (!TerrainOpBounds(Op, Bounds))
		{
			return false;
		}

		const double VoxelCm = double(Base.VoxelSizeMicrometres) / 10000.0;
		if (!(VoxelCm > 0.0))
		{
			return false;
		}

		// Half-extent of the footprint in voxels, plus one chunk so a partially touched chunk
		// is fully covered.
		double HalfExtentVox = 0.0;
		FVector CentreVox = FVector::ZeroVector;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const double Low  = double(Bounds.Min[Axis]);
			const double High = double(Bounds.Max[Axis]);
			CentreVox[Axis]   = (Low + High) * 0.5;
			HalfExtentVox     = FMath::Max(HalfExtentVox, (High - Low) * 0.5);
		}
		HalfExtentVox += double(TerrainChunkSizeVox);

		FTerrainStreamingInterest Interest;
		Interest.InterestId = ReplayInterestId;
		Interest.WorldLocation = FVector(
			double(Base.OriginWorldMicrometres[0]) / 10000.0 + CentreVox.X * VoxelCm,
			double(Base.OriginWorldMicrometres[1]) / 10000.0 + CentreVox.Y * VoxelCm,
			double(Base.OriginWorldMicrometres[2]) / 10000.0 + CentreVox.Z * VoxelCm);
		Interest.RadiusCm   = HalfExtentVox * VoxelCm * 1.74;   // sphere over a cube's diagonal
		Interest.bCollision = true;
		Interest.bRender    = false;

		Backend.SetStreamingInterest(Interest);
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

	// Checkpoint capture does not exist, so G is 0 and the whole journal is replayed onto the
	// freshly generated base. When capture lands this becomes the checkpoint's cut.
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
	FTerrainOpSeq Expected = 0;   // 0 until the first record fixes the start of the chain

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

			// --- re-apply, whole, exactly once ------------------------------------------------
			if (!MakeFootprintResident(Backend, Record.Op, Base))
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
			Cursor += Length;
		}
	}

	// P-003 §3 says replay's residency interests are "released afterwards", and that is right
	// in the world P-003 describes -- one where a checkpoint holds the restored state, so an
	// evicted chunk simply reloads from its payload. THAT WORLD DOES NOT EXIST YET. With no
	// capture pump, G is 0 and everything replay rebuilt lives only in backend RAM, so
	// releasing the interest on a backend that evicts would silently discard the entire
	// restored world. The interest is therefore RETAINED and handed to the caller, which must
	// release it only once the world's own streaming interests cover those chunks.
	//
	// This is one of the concrete reasons the capture pump is required rather than an
	// optimisation, and it is left as a visible seam rather than hidden behind a clear() that
	// happens to be harmless on the memory backend and destructive on the real one.
	if (OutStats.LastOpSeq < G)
	{
		// The checkpoint claims a cut the journal cannot reach. P-003 §3 calls this corruption.
		return FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation);
	}

	OutStats.Seconds = FPlatformTime::Seconds() - Started;
	UE_LOG(LogTerrainCore, Log,
		TEXT("Replay: %d of %d records applied from G=%llu to H=%llu across %d segment(s) in %.3f s"),
		OutStats.OpsApplied, OutStats.RecordsRead, G, OutStats.LastOpSeq,
		OutStats.Segments.Num(), OutStats.Seconds);
	return FTerrainStoreResult::Ok();
}

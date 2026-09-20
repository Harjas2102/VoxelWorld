// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "TerrainJournalReplay.h"
#include "TerrainCommitJournal.h"
#include "MemoryTerrainBackend.h"
#include "ITerrainDensityField.h"
#include "TerrainChunk.h"
#include "TerrainPersistenceFixtures.h"

/**
 * TerrainCore.Persistence.Replay.Equivalence -- the central persistence invariant.
 *
 * ARCHITECTURE.md §6.1 states it as "snapshot@R + ops after R == apply all ops from base".
 * Checkpoint capture does not exist, so R is 0 and this is its degenerate, complete form:
 * **a fresh backend plus the whole journal reproduces the world that was shut down.**
 *
 * This is the first test in the project that demonstrates Pillar 1 -- "the world permanently
 * records what the players did to it" -- rather than building towards it. Equality is by
 * `HashRegion`, which is position-sensitive and includes materials (§4.10), so it is a real
 * comparison of two worlds and not a count of operations that happened to succeed.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainJournalReplayTest, "TerrainCore.Persistence.Replay.Equivalence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

namespace
{
	/** Solid below Z=0 with a couple of material bands, so a hash means something. */
	class FReplayField final : public ITerrainDensityField
	{
	public:
		virtual FTerrainDensitySample Sample(FIntVector Position) const override
		{
			FTerrainDensitySample Out;
			Out.Density    = Position.Z < 0 ? -1.f : 1.f;
			Out.MaterialId = Position.Z < -8 ? 5 : (Position.Z < 0 ? 4 : 1);
			return Out;
		}
	};

	FTerrainBackendInit MakeInit(const TSharedPtr<FReplayField, ESPMode::ThreadSafe>& Field)
	{
		FTerrainBackendInit Init;
		Init.Seed             = 0;
		Init.GeneratorVersion = 7;
		Init.VoxelSizeCm      = 50.f;
		Init.WorldBoundsVox   = FTerrainBox(FIntVector(-256,-256,-256), FIntVector(256,256,256));
		Init.DensityField     = Field.Get();
		Init.DensityFieldOwner = Field;
		Init.Role             = ETerrainRole::Server;
		return Init;
	}

	FTerrainOp Dig(const FIntVector& Centre, int32 RadiusVox, FTerrainOpSeq OpSeq)
	{
		FTerrainOp Op;
		Op.Kind         = ETerrainOpKind::Remove;
		Op.Shape        = ETerrainShape::Sphere;
		Op.Source       = ETerrainSource::Player;
		Op.SourceId     = 2;
		Op.CentreVox    = Centre;
		Op.RadiusVoxQ16 = RadiusVox << 16;
		Op.OpSeq        = OpSeq;
		return Op;
	}
}

bool FTerrainJournalReplayTest::RunTest(const FString& Parameters)
{
	using namespace TerrainPersistTest;

	// No AddExpectedError here, deliberately: every failure this test provokes is caught by a
	// returned code rather than an error log, and declaring an expected error that never fires
	// is itself a test failure. That is the right behaviour -- it stopped exactly one stale
	// declaration in this file from silently outliving the case it was written for.

	FTerrainMemoryStorageDevice Device;
	const FTerrainPersistIdentity Identity = MakeIdentity();

	TSharedPtr<FReplayField, ESPMode::ThreadSafe> Field =
		MakeShared<FReplayField, ESPMode::ThreadSafe>();
	const FTerrainBackendInit Init = MakeInit(Field);

	// The base descriptor the store records must describe the backend replay will run against.
	FTerrainBaseDescriptor Base = MakeBaseDescriptor();
	Base.GeneratorVersion     = 7;
	Base.VoxelSizeMicrometres = 500000;          // 50 cm, matching Init.VoxelSizeCm
	Base.WorldBoundsVox       = Init.WorldBoundsVox;
	Base.OriginWorldMicrometres[0] = 0;
	Base.OriginWorldMicrometres[1] = 0;
	Base.OriginWorldMicrometres[2] = 0;

	// The edits this world will remember. Overlapping on purpose: a later op that touches a
	// chunk an earlier one already changed is where replay ORDER starts to matter.
	const FTerrainOp Ops[6] = {
		Dig(FIntVector(  0, 0, -4), 4, 1),
		Dig(FIntVector(  6, 0, -4), 3, 2),
		Dig(FIntVector(  0, 6, -6), 5, 3),
		Dig(FIntVector(  2, 2, -4), 4, 4),   // overlaps the first
		Dig(FIntVector(-34, 0, -4), 4, 5),   // a different chunk column
		Dig(FIntVector(  0, 0, -4), 2, 6),   // same place again, smaller
	};

	TSet<FTerrainChunkKey> TouchedChunks;
	TMap<FTerrainChunkKey, uint64> OriginalHashes;

	// ===== the world as it was played =====================================================
	{
		FTerrainWorldStore Store(Device);
		const FTerrainStoreResult Created =
			Store.Create(Base, Identity.World, Identity.Epoch, 1789412345678LL);
		if (!Created.IsOk())
		{
			AddError(FString::Printf(TEXT("World create failed: %s"), *Created.ToString()));
			return false;
		}

		FTerrainWorldStoreJournal Journal(Store);
		FMemoryTerrainBackend Backend;
		if (!Backend.Initialize(Init))
		{
			AddError(TEXT("Backend failed to initialise"));
			return false;
		}

		FTerrainStreamingInterest Interest;
		Interest.InterestId    = 1;
		Interest.WorldLocation = FVector::ZeroVector;
		Interest.RadiusCm      = 3000.0;   // 60 voxels, covering every op above
		Interest.bCollision    = true;
		Backend.SetStreamingInterest(Interest);

		FTerrainRevisionIndex Revisions;

		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Ops); ++Index)
		{
			FTerrainEditResult Result;
			if (!Backend.ApplyOp(Ops[Index], Result))
			{
				AddError(FString::Printf(TEXT("Backend refused op %d"), Index + 1));
				return false;
			}

			// Mirror exactly what the live commit path records: before-revisions, then the
			// bump, then the record.
			TArray<FTerrainChunkRevision> Changed;
			for (const FTerrainChunkKey& Key : Result.AffectedChunks)
			{
				FTerrainChunkRevision Revision;
				Revision.Key    = FIntVector(Key.X, Key.Y, Key.Z);
				Revision.Before = Revisions.GetRevision(Key);
				Changed.Add(Revision);
				TouchedChunks.Add(Key);
			}
			if (!Revisions.TryBumpRevisions(Result.AffectedChunks))
			{
				AddError(TEXT("Revision bump failed"));
				return false;
			}
			for (FTerrainChunkRevision& Revision : Changed)
			{
				Revision.After = Revisions.GetRevision(
					FTerrainChunkKey(Revision.Key.X, Revision.Key.Y, Revision.Key.Z));
			}

			FTerrainCommitIdentity CommitIdentity;
			CommitIdentity.RequestId  = uint32(Index + 1);
			CommitIdentity.ChildCount = 1;

			if (!Journal.RecordCommit(Ops[Index], Result, CommitIdentity, Changed))
			{
				AddError(FString::Printf(TEXT("Commit %d was not recorded"), Index + 1));
				return false;
			}
		}

		TestEqual(TEXT("Six operations are durable"), Journal.GetDurableHead(), (FTerrainOpSeq)6);
		TestTrue (TEXT("and they touched several chunks"), TouchedChunks.Num() >= 2);

		for (const FTerrainChunkKey& Key : TouchedChunks)
		{
			OriginalHashes.Add(Key, Backend.HashRegion(Key));
		}
		AddInfo(FString::Printf(TEXT("The played world touched %d chunks over 6 edits"),
			TouchedChunks.Num()));
	}

	// ===== the world as it comes back =====================================================
	{
		// A brand-new store object over the same bytes, and a brand-new backend that has never
		// seen an edit. Nothing is carried over in memory: this is a restart.
		FTerrainWorldStore Reopened(Device);
		const FTerrainStoreResult Opened = Reopened.Open();
		if (!Opened.IsOk())
		{
			AddError(FString::Printf(TEXT("World open failed: %s"), *Opened.ToString()));
			return false;
		}
		TestEqual(TEXT("The journal head survived the restart"),
			Reopened.GetJournal()->GetHead(), (FTerrainOpSeq)6);
		TestEqual(TEXT("and the checkpoint is still G=0, because capture does not exist"),
			Reopened.GetState().Checkpoint.G, (FTerrainOpSeq)0);

		FMemoryTerrainBackend Restored;
		if (!Restored.Initialize(Init))
		{
			AddError(TEXT("Restored backend failed to initialise"));
			return false;
		}

		FTerrainRevisionIndex Revisions;
		FTerrainReplayStats Stats;
		const FTerrainStoreResult Replayed =
			TerrainReplayJournal(Reopened, Restored, Revisions, Stats);
		if (!Replayed.IsOk())
		{
			AddError(FString::Printf(TEXT("Replay failed: %s"), *Replayed.ToString()));
			return false;
		}

		TestEqual(TEXT("Every record was read"), Stats.RecordsRead, 6);
		TestEqual(TEXT("and every operation re-applied"), Stats.OpsApplied, 6);
		TestEqual(TEXT("from OpSeq 1"), Stats.FirstOpSeq, (FTerrainOpSeq)1);
		TestEqual(TEXT("to OpSeq 6"), Stats.LastOpSeq, (FTerrainOpSeq)6);
		AddInfo(FString::Printf(TEXT("Replay rebuilt %d edits across %d segment(s) in %.4f s"),
			Stats.OpsApplied, Stats.Segments.Num(), Stats.Seconds));

		// ---- THE INVARIANT --------------------------------------------------------------
		int32 Compared = 0;
		for (const FTerrainChunkKey& Key : TouchedChunks)
		{
			const uint64 Before = OriginalHashes[Key];
			const uint64 After  = Restored.HashRegion(Key);
			if (Before != After)
			{
				AddError(FString::Printf(
					TEXT("Chunk (%d,%d,%d) did not come back: hash was 0x%016llx, is 0x%016llx"),
					Key.X, Key.Y, Key.Z, Before, After));
				break;
			}
			++Compared;
		}
		TestEqual(TEXT("EVERY edited chunk came back byte-for-byte"), Compared, TouchedChunks.Num());

		// Revisions are history too: a client that reconnects must not be told a chunk is older
		// than the edits it has already seen.
		bool bRevisionsMatch = true;
		for (const FTerrainChunkKey& Key : TouchedChunks)
		{
			bRevisionsMatch &= (Revisions.GetRevision(Key) > 0);
		}
		TestTrue(TEXT("and every edited chunk's revision was reconstructed"), bRevisionsMatch);

		// An untouched chunk is regenerated from the base, not restored -- and must match the
		// base exactly, or replay has written somewhere it was never asked to.
		{
			FMemoryTerrainBackend Pristine;
			Pristine.Initialize(Init);
			FTerrainStreamingInterest Interest;
			Interest.InterestId    = 2;
			Interest.WorldLocation = FVector(0, 0, 0);
			Interest.RadiusCm      = 3000.0;
			Interest.bCollision    = true;
			Pristine.SetStreamingInterest(Interest);
			Restored.SetStreamingInterest(Interest);

			const FTerrainChunkKey Untouched(3, 3, 0);
			if (!TouchedChunks.Contains(Untouched))
			{
				TestEqual(TEXT("An untouched chunk is identical to the pristine base"),
					Restored.HashRegion(Untouched), Pristine.HashRegion(Untouched));
			}
		}
	}

	// ===== damaged journals: what is refused, and what is silently lost ====================
	//
	// These two cases look identical to a reader of one record and are NOT the same thing, and
	// the difference is a real property of an append-only journal rather than a defect to fix.
	{
		// Build a world with TWO records so "interior" and "final" are distinguishable.
		auto BuildTwoRecordWorld = [&](FTerrainMemoryStorageDevice& Target)
		{
			FTerrainWorldStore Store(Target);
			Store.Create(Base, Identity.World, Identity.Epoch, 0);
			FTerrainWorldStoreJournal Journal(Store);

			FMemoryTerrainBackend Backend;
			Backend.Initialize(Init);
			FTerrainStreamingInterest Interest;
			Interest.InterestId    = 1;
			Interest.WorldLocation = FVector::ZeroVector;
			Interest.RadiusCm      = 3000.0;
			Interest.bCollision    = true;
			Backend.SetStreamingInterest(Interest);

			FTerrainRevisionIndex Revisions;
			for (int32 Index = 0; Index < 2; ++Index)
			{
				FTerrainEditResult Result;
				Backend.ApplyOp(Ops[Index], Result);

				TArray<FTerrainChunkRevision> Changed;
				for (const FTerrainChunkKey& Key : Result.AffectedChunks)
				{
					FTerrainChunkRevision Revision;
					Revision.Key    = FIntVector(Key.X, Key.Y, Key.Z);
					Revision.Before = Revisions.GetRevision(Key);
					Changed.Add(Revision);
				}
				Revisions.TryBumpRevisions(Result.AffectedChunks);
				for (FTerrainChunkRevision& Revision : Changed)
				{
					Revision.After = Revisions.GetRevision(
						FTerrainChunkKey(Revision.Key.X, Revision.Key.Y, Revision.Key.Z));
				}

				FTerrainCommitIdentity CommitIdentity;
				CommitIdentity.RequestId  = uint32(Index + 1);
				CommitIdentity.ChildCount = 1;
				Journal.RecordCommit(Ops[Index], Result, CommitIdentity, Changed);
			}
		};

		const int32 FirstRecordOffset =
			TerrainPersistObjectHeaderSize + TerrainPersistSegmentHeaderBodySize;

		// --- an INTERIOR record is damaged: refuse, never rebuild part of a world -------------
		{
			FTerrainMemoryStorageDevice Broken;
			BuildTwoRecordWorld(Broken);

			TArray<uint8>* SegmentBytes = Broken.Find(TerrainStoragePaths::JournalSegment(1));
			TestNotNull(TEXT("The segment is there to damage"), SegmentBytes);
			(*SegmentBytes)[FirstRecordOffset + 40] ^= 0xFF;

			FTerrainWorldStore Damaged(Broken);
			const FTerrainStoreResult Opened = Damaged.Open();
			TestFalse(TEXT("A damaged interior record refuses the world outright"), Opened.IsOk());
			AddInfo(FString::Printf(TEXT("Damaged interior record refused with %s"),
				*Opened.ToString()));
		}

		// --- the FINAL record is damaged: indistinguishable from a torn append -----------------
		{
			FTerrainMemoryStorageDevice Torn;
			BuildTwoRecordWorld(Torn);

			TArray<uint8>* SegmentBytes = Torn.Find(TerrainStoragePaths::JournalSegment(1));
			// Damage a byte inside the LAST record. Its frame reaches exactly end of file, which
			// is P-004 §9.2 form 3, so it is a torn tail by definition.
			(*SegmentBytes)[SegmentBytes->Num() - 30] ^= 0xFF;

			FTerrainWorldStore Damaged(Torn);
			const FTerrainStoreResult Opened = Damaged.Open();
			TestTrue(TEXT("The world still opens -- a torn tail is a survivable state"), Opened.IsOk());
			if (!Opened.IsOk())
			{
				return false;
			}

			TestTrue (TEXT("and the journal reports the tear"), Damaged.GetJournal()->GetState().bTornTail);
			TestEqual(TEXT("with the head rolled back to the last intact record"),
				Damaged.GetJournal()->GetHead(), (FTerrainOpSeq)1);

			FMemoryTerrainBackend Restored;
			Restored.Initialize(Init);
			FTerrainRevisionIndex Revisions;
			FTerrainReplayStats Stats;
			TestTrue(TEXT("Replay succeeds"),
				TerrainReplayJournal(Damaged, Restored, Revisions, Stats).IsOk());
			TestEqual(TEXT("rebuilding only the intact edit"), Stats.OpsApplied, 1);

			// THE HONEST PART. A bit-rotted final record and a torn append are the same bytes,
			// so the world comes back MISSING THAT EDIT and nothing can tell the player which
			// of the two happened. That is a property of an append-only journal, not a defect
			// here, and the mitigation is checkpoints plus the crash matrix -- neither of which
			// exists. It is asserted rather than left for someone to discover in a saved world.
			AddInfo(TEXT("A damaged FINAL record is indistinguishable from a torn append: the ")
				TEXT("world returns without that edit, and no reader can tell which occurred."));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "TerrainCheckpoint.h"
#include "TerrainService.h"
#include "TerrainSettings.h"
#include "TerrainPersistenceIndex.h"
#include "UObject/StrongObjectPtr.h"
#include "TerrainJournalReplay.h"
#include "TerrainCommitJournal.h"
#include "MemoryTerrainBackend.h"
#include "ITerrainDensityField.h"
#include "TerrainChunk.h"
#include "TerrainPersistenceFixtures.h"

/**
 * TerrainCore.Persistence.Checkpoint.Equivalence -- the cut that bounds replay.
 *
 * `Persistence.Replay.Equivalence` proved a world comes back. It comes back by replaying
 * EVERY edit ever made, which is the cost a checkpoint exists to remove. This proves the
 * removal: after a capture, a restart restores the cut and replays **only what came after
 * it**, and the world is still identical chunk-for-chunk.
 *
 * Both halves matter. A checkpoint that bounded replay but lost terrain would be worse than
 * no checkpoint at all, so every case here ends in a `HashRegion` comparison.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainCheckpointTest, "TerrainCore.Persistence.Checkpoint.Equivalence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

namespace
{
	class FCheckpointField final : public ITerrainDensityField
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

	FTerrainOp CheckpointDig(const FIntVector& Centre, int32 RadiusVox, FTerrainOpSeq OpSeq)
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

bool FTerrainCheckpointTest::RunTest(const FString& Parameters)
{
	using namespace TerrainPersistTest;

	AddExpectedError(TEXT("is dirty but not resident"),
		EAutomationExpectedErrorFlags::Contains, /*Occurrences=*/0, /*IsRegex=*/false);

	FTerrainMemoryStorageDevice Device;
	const FTerrainPersistIdentity Identity = MakeIdentity();

	TSharedPtr<FCheckpointField, ESPMode::ThreadSafe> Field =
		MakeShared<FCheckpointField, ESPMode::ThreadSafe>();

	FTerrainBackendInit Init;
	Init.Seed              = 0;
	Init.GeneratorVersion  = 7;
	Init.VoxelSizeCm       = 50.f;
	Init.WorldBoundsVox    = FTerrainBox(FIntVector(-256,-256,-256), FIntVector(256,256,256));
	Init.DensityField      = Field.Get();
	Init.DensityFieldOwner = Field;
	Init.Role              = ETerrainRole::Server;

	FTerrainBaseDescriptor Base = MakeBaseDescriptor();
	Base.GeneratorVersion          = 7;
	Base.VoxelSizeMicrometres      = 500000;
	Base.WorldBoundsVox            = Init.WorldBoundsVox;
	Base.OriginWorldMicrometres[0] = 0;
	Base.OriginWorldMicrometres[1] = 0;
	Base.OriginWorldMicrometres[2] = 0;
	Base.ValueConfig               = 0;

	const FTerrainOp Ops[10] = {
		CheckpointDig(FIntVector(  0, 0, -4), 4, 1),
		CheckpointDig(FIntVector(  6, 0, -4), 3, 2),
		CheckpointDig(FIntVector(  0, 6, -6), 5, 3),
		CheckpointDig(FIntVector(  2, 2, -4), 4, 4),
		CheckpointDig(FIntVector(-34, 0, -4), 4, 5),
		CheckpointDig(FIntVector(  0, 0, -4), 2, 6),
		// after the cut
		CheckpointDig(FIntVector( 10, 4, -5), 3, 7),
		CheckpointDig(FIntVector(-34, 4, -4), 3, 8),
		CheckpointDig(FIntVector(  4, 0, -8), 4, 9),
		CheckpointDig(FIntVector( 48, 8,-16), 3, 10), // disjoint from all replayed chunks
	};

	TSet<FTerrainChunkKey> TouchedChunks;
	TMap<FTerrainChunkKey, uint64> HashesAfterSix;
	TMap<FTerrainChunkKey, uint64> HashesAfterNine;
	TMap<FTerrainChunkKey, uint64> HashesAfterTen;

	auto AddInterest = [](ITerrainBackend& Backend, uint32 Id)
	{
		FTerrainStreamingInterest Interest;
		Interest.InterestId    = Id;
		Interest.WorldLocation = FVector::ZeroVector;
		Interest.RadiusCm      = 3000.0;
		Interest.bCollision    = true;
		Backend.SetStreamingInterest(Interest);
	};

	/** Applies ops [First,Last] and records each, exactly as the live commit path would. */
	auto PlayAndRecord = [&](FTerrainWorldStoreJournal& Journal, ITerrainBackend& Backend,
	                         FTerrainRevisionIndex& Revisions, int32 First, int32 Last,
	                         TMap<FTerrainChunkKey, FTerrainOpSeq>* Dirty) -> bool
	{
		for (int32 Index = First; Index <= Last; ++Index)
		{
			FTerrainEditResult Result;
			if (!Backend.ApplyOp(Ops[Index], Result))
			{
				AddError(FString::Printf(TEXT("Backend refused op %d"), Index + 1));
				return false;
			}

			TArray<FTerrainChunkRevision> Changed;
			for (const FTerrainChunkKey& Key : Result.AffectedChunks)
			{
				FTerrainChunkRevision Revision;
				Revision.Key    = FIntVector(Key.X, Key.Y, Key.Z);
				Revision.Before = Revisions.GetRevision(Key);
				Changed.Add(Revision);
				TouchedChunks.Add(Key);
				if (Dirty != nullptr) { Dirty->Add(Key, Ops[Index].OpSeq); }
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
		return true;
	};

	// ===== session A: six edits, then a checkpoint =========================================
	{
		FTerrainWorldStore Store(Device);
		if (!Store.Create(Base, Identity.World, Identity.Epoch, 1789412345678LL).IsOk())
		{
			AddError(TEXT("World create failed"));
			return false;
		}

		FTerrainWorldStoreJournal Journal(Store);
		FMemoryTerrainBackend Backend;
		Backend.Initialize(Init);
		AddInterest(Backend, 1);

		FTerrainRevisionIndex Revisions;
		TMap<FTerrainChunkKey, FTerrainOpSeq> Dirty;
		if (!PlayAndRecord(Journal, Backend, Revisions, 0, 5, &Dirty))
		{
			return false;
		}

		for (const FTerrainChunkKey& Key : TouchedChunks)
		{
			HashesAfterSix.Add(Key, Backend.HashRegion(Key));
		}

		FTerrainCheckpointStats Stats;
		const FTerrainStoreResult Captured = TerrainCaptureCheckpoint(
			Store, Backend, Revisions, Dirty, /*G=*/6, 1789412355555LL, Stats);
		if (!Captured.IsOk())
		{
			AddError(FString::Printf(TEXT("Capture failed: %s"), *Captured.ToString()));
			return false;
		}

		TestEqual(TEXT("The cut is at G=6"), Stats.G, (FTerrainOpSeq)6);
		TestEqual(TEXT("and it wrote one payload per dirty chunk"), Stats.ChunksWritten, Dirty.Num());
		TestTrue (TEXT("and some index pages"), Stats.IndexPagesWritten > 0);
		TestTrue (TEXT("The published checkpoint has a root page"),
			Store.GetState().Checkpoint.bHasRootPage);
		TestEqual(TEXT("at G=6"), Store.GetState().Checkpoint.G, (FTerrainOpSeq)6);
		TestEqual(TEXT("naming every edited chunk"),
			(int32)Store.GetState().Checkpoint.LeafKeyCount, Dirty.Num());

		AddInfo(FString::Printf(
			TEXT("Checkpoint at G=6: %d chunks, %lld payload bytes, %d index pages, %.4f s"),
			Stats.ChunksWritten, Stats.PayloadBytes, Stats.IndexPagesWritten, Stats.Seconds));
	}

	// ===== session B: restart, restore the cut, replay NOTHING =============================
	{
		FTerrainWorldStore Store(Device);
		if (!Store.Open().IsOk())
		{
			AddError(TEXT("World open failed"));
			return false;
		}
		TestEqual(TEXT("The checkpoint came back at G=6"),
			Store.GetState().Checkpoint.G, (FTerrainOpSeq)6);

		FMemoryTerrainBackend Backend;
		Backend.Initialize(Init);

		FTerrainRevisionIndex Revisions;
		FTerrainRestoreStats RestoreStats;
		const FTerrainStoreResult Restored =
			TerrainRestoreCheckpoint(Store, Backend, Revisions, RestoreStats);
		if (!Restored.IsOk())
		{
			AddError(FString::Printf(TEXT("Restore failed: %s"), *Restored.ToString()));
			return false;
		}
		TestEqual(TEXT("Every checkpointed chunk was restored"),
			RestoreStats.ChunksRestored, HashesAfterSix.Num());

		FTerrainReplayStats ReplayStats;
		const FTerrainStoreResult Replayed =
			TerrainReplayJournal(Store, Backend, Revisions, ReplayStats);
		if (!Replayed.IsOk())
		{
			AddError(FString::Printf(TEXT("Replay failed: %s"), *Replayed.ToString()));
			return false;
		}

		// THE POINT OF THE WHOLE INCREMENT: the journal still holds six records, and replay
		// re-applied NONE of them, because the cut already contains them.
		TestEqual(TEXT("All six records were read"), ReplayStats.RecordsRead, 6);
		TestEqual(TEXT("and NONE were re-applied -- the cut already holds them"),
			ReplayStats.OpsApplied, 0);

		int32 Compared = 0;
		for (const TPair<FTerrainChunkKey, uint64>& Entry : HashesAfterSix)
		{
			if (Backend.HashRegion(Entry.Key) != Entry.Value)
			{
				AddError(FString::Printf(TEXT("Chunk (%d,%d,%d) did not survive the checkpoint"),
					Entry.Key.X, Entry.Key.Y, Entry.Key.Z));
				break;
			}
			++Compared;
		}
		TestEqual(TEXT("EVERY chunk is identical after restore alone"), Compared, HashesAfterSix.Num());

		// Revisions came back with the terrain, so a returning client is not told a chunk it
		// has already seen edits for is at revision 0.
		bool bRevisions = true;
		for (const TPair<FTerrainChunkKey, uint64>& Entry : HashesAfterSix)
		{
			bRevisions &= (Revisions.GetRevision(Entry.Key) > 0);
		}
		TestTrue(TEXT("and every restored chunk carries its revision"), bRevisions);

		// ---- three more edits, on top of the restored world -------------------------------
		FTerrainWorldStoreJournal Journal(Store);
		AddInterest(Backend, 1);
		if (!PlayAndRecord(Journal, Backend, Revisions, 6, 8, nullptr))
		{
			return false;
		}
		TestEqual(TEXT("The journal head is 9"), Journal.GetDurableHead(), (FTerrainOpSeq)9);

		for (const FTerrainChunkKey& Key : TouchedChunks)
		{
			HashesAfterNine.Add(Key, Backend.HashRegion(Key));
		}
	}

	// ===== session C: restart again, restore the cut, replay ONLY (6,9] ====================
	{
		FTerrainWorldStore Store(Device);
		if (!Store.Open().IsOk())
		{
			AddError(TEXT("World open failed"));
			return false;
		}

		FMemoryTerrainBackend Backend;
		Backend.Initialize(Init);

		FTerrainRevisionIndex Revisions;
		FTerrainRestoreStats RestoreStats;
		TerrainRestoreCheckpoint(Store, Backend, Revisions, RestoreStats);

		FTerrainReplayStats ReplayStats;
		const FTerrainStoreResult Replayed =
			TerrainReplayJournal(Store, Backend, Revisions, ReplayStats);
		if (!Replayed.IsOk())
		{
			AddError(FString::Printf(TEXT("Replay failed: %s"), *Replayed.ToString()));
			return false;
		}

		TestEqual(TEXT("Nine records were read"), ReplayStats.RecordsRead, 9);
		TestEqual(TEXT("and exactly THREE were re-applied -- the ones after the cut"),
			ReplayStats.OpsApplied, 3);
		AddInfo(FString::Printf(
			TEXT("Bounded replay: 9 edits in the journal, %d re-applied after the G=6 cut"),
			ReplayStats.OpsApplied));

		int32 Compared = 0;
		for (const TPair<FTerrainChunkKey, uint64>& Entry : HashesAfterNine)
		{
			if (Backend.HashRegion(Entry.Key) != Entry.Value)
			{
				AddError(FString::Printf(
					TEXT("Chunk (%d,%d,%d) is wrong after checkpoint + partial replay"),
					Entry.Key.X, Entry.Key.Y, Entry.Key.Z));
				break;
			}
			++Compared;
		}
		TestEqual(TEXT("EVERY chunk is identical after restore + bounded replay"),
			Compared, HashesAfterNine.Num());

		// The next cut must include the replayed tail AND a disjoint new live edit.
		FTerrainWorldStoreJournal Journal(Store);
		AddInterest(Backend, 1);
		if (!PlayAndRecord(Journal, Backend, Revisions, 9, 9, &ReplayStats.DirtyChunks)) return false;
		for (const auto& Key : TouchedChunks) HashesAfterTen.Add(Key, Backend.HashRegion(Key));
		FTerrainCheckpointStats Captured;
		TestTrue(TEXT("A second cut carries replayed dirty chunks forward"), TerrainCaptureCheckpoint(
			Store, Backend, Revisions, ReplayStats.DirtyChunks, 10, 0, Captured).IsOk());

	}

	// ===== session D: the second cut alone preserves both old and new chunks ===============
	{
		FTerrainWorldStore Store(Device);
		if (!Store.Open().IsOk()) return false;
		FMemoryTerrainBackend Backend;
		Backend.Initialize(Init);
		FTerrainRevisionIndex Revisions;
		FTerrainRestoreStats Restored;
		FTerrainReplayStats Replayed;
		TestTrue(TEXT("Restore second cut"), TerrainRestoreCheckpoint(Store, Backend, Revisions, Restored).IsOk());
		TestTrue(TEXT("Replay after second cut"), TerrainReplayJournal(Store, Backend, Revisions, Replayed).IsOk());
		TestEqual(TEXT("Second cut requires no operation replay"), Replayed.OpsApplied, 0);
		for (const auto& Entry : HashesAfterTen)
			TestEqual(TEXT("Second checkpoint preserves every chunk hash"), Backend.HashRegion(Entry.Key), Entry.Value);

		FTerrainCheckpointStats Invalid;
		TestFalse(TEXT("Cannot capture ahead of journal head"), TerrainCaptureCheckpoint(
			Store, Backend, Revisions, {}, 11, 0, Invalid).IsOk());
		TestEqual(TEXT("Invalid cut does not advance G"), Store.GetState().Checkpoint.G, FTerrainOpSeq(10));
	}

	// Invalid revision seeds are atomic: duplicate entries may not overwrite history.
	{
		FTerrainRevisionIndex Revisions;
		const FTerrainChunkKey Key(1, 2, 3);
		TestFalse(TEXT("Duplicate revision seeds refused"), Revisions.SeedRevisions({{Key, 1}, {Key, 2}}));
		TestEqual(TEXT("Refused seed leaves index empty"), Revisions.GetRevision(Key), FTerrainRev(0));
		TestTrue(TEXT("Valid seed after failed seed"), Revisions.SeedRevisions({{Key, 3}}));
	}

	// Service admission counts the whole footprint, but journal-only mode cannot deadlock.
	{
		FTerrainWorldStore Store(Device);
		if (!Store.Open().IsOk()) return false;
		FTerrainWorldStoreJournal Journal(Store);
		TStrongObjectPtr<UTerrainService> Service(NewObject<UTerrainService>());
		Service->Backend = MakeUnique<FMemoryTerrainBackend>();
		Service->Backend->Initialize(Init);
		AddInterest(*Service->Backend, 1);
		Service->RevisionIndex = MakeUnique<FTerrainRevisionIndex>();
		Service->ActiveInit = Init;
		Service->State = ETerrainServiceState::Ready;
		Service->SetCommitJournal(&Journal);
		auto* Settings = GetMutableDefault<UTerrainSettings>();
		TGuardValue<bool> CaptureSetting(Settings->bCheckpointCapture, true);
		for (int32 I = 0; I < TerrainCheckpointDirtyHardBound - 1; ++I)
			Service->DirtyChunks.Add(FTerrainChunkKey(I, 10, 10), 1);
		FTerrainOp Op = Ops[0];
		Op.Source = ETerrainSource::Admin;
		TestEqual(TEXT("Multi-chunk op cannot overshoot dirty cap"), Service->ValidateOp(Op, {}), ETerrainEditRejection::QueueFull);
		Settings->bCheckpointCapture = false;
		TestEqual(TEXT("Capture-off ignores dirty budget"), Service->ValidateOp(Op, {}), ETerrainEditRejection::None);
		Settings->bCheckpointCapture = true;
		Service->DirtyChunks.Reset();
		FTerrainBox Bounds;
		TArray<FTerrainChunkKey> Keys;
		TerrainOpBounds(Op, Bounds);
		TerrainChunkKeysForBox(Bounds, Keys);
		for (const auto& K : Keys) Service->DirtyChunks.Add(K, 1);
		for (int32 I = 0; Service->DirtyChunks.Num() < TerrainCheckpointDirtyHardBound; ++I)
			Service->DirtyChunks.Add(FTerrainChunkKey(I, 10, 10), 1);
		TestEqual(TEXT("Already-dirty footprint is legal at cap"), Service->ValidateOp(Op, {}), ETerrainEditRejection::None);
		Service->bStorageFaulted = true;
		TestFalse(TEXT("Storage fault hides partial backend"), Service->IsBackendReady());
		FTerrainPointSample Sample;
		TestFalse(TEXT("Storage fault refuses terrain reads"), Service->QueryPoint(FIntVector::ZeroValue, Sample));
		Service->DestroyBackend();
	}

	// A valid index object can still lie about the valid payload it references.
	{
		FTerrainWorldStore Store(Device);
		if (!Store.Open().IsOk()) return false;
		const auto& Cut = Store.GetState().Checkpoint;
		FTerrainIndexRoot Root;
		Root.bHasRootPage = Cut.bHasRootPage;
		Root.RootPageDigest = Cut.RootPageDigest;
		Root.RootPageLength = Cut.RootPageLength;
		FTerrainIndexUpdate Bad;
		TerrainIndexEnumerate(Store.GetState().Identity, Store.GetObjects(), Root,
			[&](const FTerrainChunkKey& Key, const FTerrainIndexLeafValue& Value)
			{ Bad.Key = Key; Bad.Value = Value; return false; });
		++Bad.Value.Rev;
		FTerrainIndexRoot WrongRoot;
		int32 Pages = 0;
		TestEqual(TEXT("Construct inconsistent index fixture"), TerrainIndexApply(Store.GetState().Identity,
			Store.GetObjects(), Store.GetObjects(), Root, {Bad}, WrongRoot, Pages), ETerrainPersistError::None);
		FTerrainCheckpointDescriptor Wrong = Cut;
		Wrong.RootPageDigest = WrongRoot.RootPageDigest;
		Wrong.RootPageLength = WrongRoot.RootPageLength;
		TestTrue(TEXT("Publish inconsistent fixture"), Store.PublishCheckpoint(Wrong, 0).IsOk());
		FMemoryTerrainBackend Backend;
		Backend.Initialize(Init);
		FTerrainRevisionIndex Revisions;
		FTerrainRestoreStats Stats;
		TestFalse(TEXT("Leaf/payload revision mismatch refuses restore"), TerrainRestoreCheckpoint(Store, Backend, Revisions, Stats).IsOk());
		Wrong.bHasRootPage = false;
		Wrong.RootPageDigest = {};
		Wrong.RootPageLength = 0;
		TestTrue(TEXT("Publish no-root/nonzero-count fixture"), Store.PublishCheckpoint(Wrong, 0).IsOk());
		TestFalse(TEXT("Missing root cannot hide nonzero descriptor totals"), TerrainRestoreCheckpoint(Store, Backend, Revisions, Stats).IsOk());
	}

	// Failure publishing a cut leaves the earlier root + journal recoverable.
	{
		FTerrainMemoryStorageDevice Memory;
		FTerrainFaultDevice Faults(Memory);
		FTerrainWorldStore Store(Faults);
		if (!Store.Create(Base, Identity.World, Identity.Epoch, 0).IsOk()) return false;
		FTerrainWorldStoreJournal Journal(Store);
		FMemoryTerrainBackend Backend;
		Backend.Initialize(Init);
		AddInterest(Backend, 1);
		FTerrainRevisionIndex Revisions;
		TMap<FTerrainChunkKey, FTerrainOpSeq> Dirty;
		if (!PlayAndRecord(Journal, Backend, Revisions, 0, 0, &Dirty)) return false;
		FTerrainResidencyPins Pins(0x53000000);
		TMap<FTerrainChunkKey, uint64> ExpectedHashes;
		for (const auto& Entry : Dirty)
		{
			Pins.Pin(Backend, Entry.Key, Base);
			ExpectedHashes.Add(Entry.Key, Backend.HashRegion(Entry.Key));
		}
		Backend.ClearStreamingInterest(1);
		for (const auto& Entry : Dirty) TestTrue(TEXT("Dirty pin survives player departure"), Backend.IsRegionResident(Entry.Key));

		Faults.FailAfter(ETerrainStorageOp::WriteNew, 0);
		FTerrainCheckpointStats Stats;
		TestFalse(TEXT("Payload write fault refuses capture"), TerrainCaptureCheckpoint(Store, Backend, Revisions, Dirty, 1, 0, Stats).IsOk());
		TestEqual(TEXT("Failed capture does not advance cut"), Store.GetState().Checkpoint.G, FTerrainOpSeq(0));
		Faults.ClearFaults();
		FTerrainWorldStore Reopened(Memory);
		TestTrue(TEXT("Old cut still opens"), Reopened.Open().IsOk());
		FMemoryTerrainBackend Fresh;
		Fresh.Initialize(Init);
		FTerrainRevisionIndex FreshRevisions;
		FTerrainReplayStats Replay;
		TestTrue(TEXT("Journal recovers failed capture"), TerrainReplayJournal(Reopened, Fresh, FreshRevisions, Replay).IsOk());
		for (const auto& Entry : ExpectedHashes)
			TestEqual(TEXT("Failed capture loses no terrain"), Fresh.HashRegion(Entry.Key), Entry.Value);
	}

	// A missing required prefix must fail even if the remaining edit touches pristine chunks.
	{
		FTerrainMemoryStorageDevice Memory;
		FTerrainWorldStore Store(Memory);
		if (!Store.Create(Base, Identity.World, Identity.Epoch, 0).IsOk()) return false;
		FTerrainWorldStoreJournal Journal(Store);
		FMemoryTerrainBackend Backend;
		Backend.Initialize(Init);
		AddInterest(Backend, 1);
		FTerrainRevisionIndex Revisions;
		if (!PlayAndRecord(Journal, Backend, Revisions, 0, 0, nullptr)) return false;
		if (!Store.GetJournal()->Rotate(2, 0).IsOk()) return false;
		FTerrainOp Disjoint = CheckpointDig(FIntVector(48, 8, -16), 3, 2);
		FTerrainEditResult Result;
		if (!Backend.ApplyOp(Disjoint, Result)) return false;
		TArray<FTerrainChunkRevision> Changed;
		for (const auto& Key : Result.AffectedChunks)
		{
			FTerrainChunkRevision Revision;
			Revision.Key = FIntVector(Key.X, Key.Y, Key.Z);
			Revision.Before = 0;
			Revision.After = 1;
			Changed.Add(Revision);
		}
		FTerrainCommitIdentity Commit;
		Commit.RequestId = 2;
		Commit.ChildCount = 1;
		if (!Journal.RecordCommit(Disjoint, Result, Commit, Changed)) return false;
		Memory.Delete(TerrainStoragePaths::JournalSegment(1));
		FTerrainWorldStore Reopened(Memory);
		TestTrue(TEXT("Active segment alone still opens at storage layer"), Reopened.Open().IsOk());
		FMemoryTerrainBackend Fresh;
		Fresh.Initialize(Init);
		FTerrainRevisionIndex FreshRevisions;
		FTerrainReplayStats Replay;
		TestFalse(TEXT("Replay refuses missing G+1 even for disjoint later op"), TerrainReplayJournal(Reopened, Fresh, FreshRevisions, Replay).IsOk());
		TestEqual(TEXT("Missing-prefix refusal occurs before mutation"), Replay.OpsApplied, 0);
	}

	// ===== the sentinel trap: a dirty chunk that cannot be read is never "unchanged" =======
	{
		FTerrainMemoryStorageDevice Fresh;
		FTerrainWorldStore Store(Fresh);
		Store.Create(Base, Identity.World, Identity.Epoch, 0);

		FMemoryTerrainBackend Backend;
		Backend.Initialize(Init);

		// A chunk far outside any streaming interest: never generated, so not resident. A
		// backend that returns a successful default Empty for it must not let the capture
		// publish it as pristine -- P-003 §4's named trap.
		const FTerrainChunkKey Nonresident(5, 5, 5);
		TestFalse(TEXT("The chunk is genuinely not resident"), Backend.IsRegionResident(Nonresident));

		FTerrainRevisionIndex Revisions;
		FTerrainCheckpointStats Stats;
		const FTerrainStoreResult Captured = TerrainCaptureCheckpoint(
			Store, Backend, Revisions, {{ Nonresident, 1 }}, /*G=*/0, 0, Stats);

		TestFalse(TEXT("The capture is refused"), Captured.IsOk());
		TestEqual(TEXT("because an unreadable chunk is not evidence of an unchanged one"),
			Captured.Format, ETerrainPersistError::EncodingNotPermitted);
		TestFalse(TEXT("and no checkpoint was published"),
			Store.GetState().Checkpoint.bHasRootPage);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

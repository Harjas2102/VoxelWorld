// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "TerrainRetention.h"
#include "TerrainCheckpoint.h"
#include "TerrainCommitJournal.h"
#include "TerrainPersistenceIndex.h"
#include "MemoryTerrainBackend.h"
#include "ITerrainDensityField.h"
#include "TerrainChunk.h"
#include "TerrainPersistenceFixtures.h"

/**
 * TerrainCore.Persistence.Retention -- deleting what nothing refers to, and nothing else.
 *
 * The store only ever grew: every checkpoint writes a fresh payload per captured chunk and a
 * fresh page for every index node on the path to a changed leaf, and the previous versions
 * became unreachable without ever being removed.
 *
 * The danger in fixing that is obvious and total. A sweep that deletes one live object destroys
 * the world silently -- the checkpoint still publishes, the root still resolves, and the loss is
 * only discovered on a restart that cannot restore. So every case here ends by **restoring from
 * disk and comparing chunk hashes**, because "retention returned Ok" is not evidence.
 *
 * The case that would be easy to get wrong, and that this exists for: **the previous generation
 * must survive.** The root slot pair is the recovery mechanism, so the checkpoint the OTHER
 * slot names must still be restorable after a sweep. A mark that walked only the current root
 * would pass every other test in this file and quietly destroy the fallback.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainRetentionTest, "TerrainCore.Persistence.Retention",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

namespace
{
	class FRetentionField final : public ITerrainDensityField
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

	FTerrainOp RetentionDig(const FIntVector& Centre, int32 RadiusVox, FTerrainOpSeq OpSeq)
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

bool FTerrainRetentionTest::RunTest(const FString& Parameters)
{
	using namespace TerrainPersistTest;

	FTerrainMemoryStorageDevice Device;
	const FTerrainPersistIdentity Identity = MakeIdentity();

	TSharedPtr<FRetentionField, ESPMode::ThreadSafe> Field =
		MakeShared<FRetentionField, ESPMode::ThreadSafe>();

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

	// Every operation the live store performs is counted, so the test can say what P-005 claims:
	// after bootstrap, an open world creates no name and removes none.
	FTerrainFaultDevice Counted(Device);
	FTerrainWorldStore Store(Counted);
	if (!Store.Create(Base, Identity.World, Identity.Epoch, 1789412345678LL).IsOk())
	{
		AddError(TEXT("World create failed"));
		return false;
	}
	const int32 BootstrapWriteNew = Counted.OpCount(ETerrainStorageOp::WriteNew);

	FTerrainWorldStoreJournal Journal(Store);
	FMemoryTerrainBackend Backend;
	Backend.Initialize(Init);

	FTerrainStreamingInterest Interest;
	Interest.InterestId    = 1;
	Interest.WorldLocation = FVector::ZeroVector;
	Interest.RadiusCm      = 6000.0;
	Interest.bCollision    = true;
	Backend.SetStreamingInterest(Interest);

	FTerrainRevisionIndex Revisions;

	auto Commit = [&](const FTerrainOp& Op, TMap<FTerrainChunkKey, FTerrainOpSeq>& Dirty) -> bool
	{
		FTerrainEditResult Result;
		if (!Backend.ApplyOp(Op, Result))
		{
			AddError(FString::Printf(TEXT("Backend refused op %llu"), Op.OpSeq));
			return false;
		}
		TArray<FTerrainChunkRevision> Changed;
		for (const FTerrainChunkKey& Key : Result.AffectedChunks)
		{
			FTerrainChunkRevision Revision;
			Revision.Key    = FIntVector(Key.X, Key.Y, Key.Z);
			Revision.Before = Revisions.GetRevision(Key);
			Changed.Add(Revision);
			Dirty.Add(Key, Op.OpSeq);
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
		CommitIdentity.RequestId  = uint32(Op.OpSeq);
		CommitIdentity.ChildCount = 1;
		return Journal.RecordCommit(Op, Result, CommitIdentity, Changed);
	};

	auto Capture = [&](const TMap<FTerrainChunkKey, FTerrainOpSeq>& Dirty, FTerrainOpSeq G) -> bool
	{
		FTerrainCheckpointStats Stats;
		const FTerrainStoreResult Result = TerrainCaptureCheckpoint(
			Store, Backend, Revisions, Dirty, G, 1789412355555LL, Stats);
		if (!Result.IsOk())
		{
			AddError(FString::Printf(TEXT("Capture at G=%llu failed: %s"), G, *Result.ToString()));
			return false;
		}
		return true;
	};

	// Three generations over the SAME chunks, so each checkpoint supersedes the last and leaves
	// a full set of payloads and pages unreachable behind it.
	TMap<FTerrainChunkKey, FTerrainOpSeq> Dirty1;
	for (int32 i = 0; i < 4; ++i)
	{
		if (!Commit(RetentionDig(FIntVector(i * 34, 0, -4), 4, i + 1), Dirty1)) { return false; }
	}
	if (!Capture(Dirty1, 4)) { return false; }

	TMap<FTerrainChunkKey, FTerrainOpSeq> Dirty2;
	for (int32 i = 0; i < 4; ++i)
	{
		if (!Commit(RetentionDig(FIntVector(i * 34, 2, -4), 3, i + 5), Dirty2)) { return false; }
	}
	if (!Capture(Dirty2, 8)) { return false; }

	// The state the SECOND (previous) generation holds, which must survive the sweep.
	TMap<FTerrainChunkKey, uint64> HashesAtG8;
	for (const TPair<FTerrainChunkKey, FTerrainOpSeq>& Entry : Dirty2)
	{
		HashesAtG8.Add(Entry.Key, Backend.HashRegion(Entry.Key));
	}

	TMap<FTerrainChunkKey, FTerrainOpSeq> Dirty3;
	for (int32 i = 0; i < 4; ++i)
	{
		if (!Commit(RetentionDig(FIntVector(i * 34, 4, -4), 3, i + 9), Dirty3)) { return false; }
	}
	if (!Capture(Dirty3, 12)) { return false; }

	TMap<FTerrainChunkKey, uint64> HashesAtG12;
	for (const TPair<FTerrainChunkKey, FTerrainOpSeq>& Entry : Dirty3)
	{
		HashesAtG12.Add(Entry.Key, Backend.HashRegion(Entry.Key));
	}

	// Compare actual restored values, including the older slot selected by ordinary Open().
	auto VerifyRestore = [&](FTerrainMemoryStorageDevice& Disk, FTerrainOpSeq ExpectedG,
		const TMap<FTerrainChunkKey, uint64>& Hashes) -> bool
	{
		FTerrainWorldStore Opened(Disk);
		if (!Opened.Open().IsOk()) { AddError(TEXT("Reopen failed")); return false; }
		TestEqual(TEXT("Recovered checkpoint cut"), Opened.GetState().Checkpoint.G, ExpectedG);
		FMemoryTerrainBackend Restored;
		Restored.Initialize(Init);
		FTerrainRevisionIndex RestoredRevisions;
		FTerrainRestoreStats RestoreStats;
		if (!TerrainRestoreCheckpoint(Opened, Restored, RestoredRevisions, RestoreStats).IsOk())
		{
			AddError(TEXT("Recovery dependencies did not restore"));
			return false;
		}
		TestTrue(TEXT("Hash evidence is nonempty"), Hashes.Num() > 0);
		for (const auto& Expected : Hashes)
		{
			TestEqual(TEXT("Recovered chunk hash"), Restored.HashRegion(Expected.Key), Expected.Value);
		}
		return true;
	};
	auto BreakNewestSlot = [&](FTerrainMemoryStorageDevice& Disk) -> bool
	{
		FTerrainWorldStore Opened(Disk);
		if (!Opened.Open().IsOk()) { return false; }
		for (int32 Index = 0; Index < 2; ++Index)
		{
			if (Opened.GetState().RootSlots[Index].Generation == Opened.GetState().Root.Generation)
			{
				TArray<uint8>* Bytes = Disk.Find(TerrainStoragePaths::RootSlot(Index));
				if (!Bytes || Bytes->IsEmpty()) { return false; }
				(*Bytes)[0] ^= 1;
				return true;
			}
		}
		return false;
	};
	auto Fingerprint = [](FTerrainMemoryStorageDevice& Disk)
	{
		TArray<FString> Paths;
		Disk.GetPaths(Paths);
		Paths.Sort();
		FString Signature;
		for (const FString& Path : Paths)
		{
			Signature += Path + TerrainPersistDigestToHex(TerrainPersistDigest(*Disk.Find(Path)));
		}
		return Signature;
	};

	// Slot damage after Open() must be observed even if the cached pair was healthy.
	{
		FTerrainMemoryStorageDevice Copy = Device;
		FTerrainWorldStore Opened(Copy);
		if (!Opened.Open().IsOk() || !BreakNewestSlot(Copy)) { return false; }
		const FString Before = Fingerprint(Copy);
		FTerrainRetentionStats Refused;
		TestFalse(TEXT("A damaged disk slot overrides healthy cached roots"),
			TerrainReclaimStore(Opened, Refused).IsOk());
		TestEqual(TEXT("Failed validation changes no file"), Fingerprint(Copy), Before);
	}

	// A valid index naming a missing payload is not a validated recovery closure.
	// A wrong root-page length is likewise invalid even though its bytes decode alone.
	for (int32 Case = 0; Case < 2; ++Case)
	{
		FTerrainMemoryStorageDevice Copy = Device;
		FTerrainWorldStore Opened(Copy);
		if (!Opened.Open().IsOk()) { return false; }
		FTerrainCheckpointDescriptor Descriptor = Opened.GetState().Checkpoint;
		if (Case == 0)
		{
			FTerrainIndexRoot OldRoot;
			OldRoot.bHasRootPage = true;
			OldRoot.RootPageDigest = Descriptor.RootPageDigest;
			OldRoot.RootPageLength = Descriptor.RootPageLength;
			FTerrainIndexUpdate Update;
			Update.Key = Dirty3.CreateConstIterator().Key();
			bool Found = false;
			if (TerrainIndexLookup(Opened.GetState().Identity, Opened.GetObjects(), OldRoot,
				Update.Key, Update.Value, Found) != ETerrainPersistError::None || !Found)
			{
				AddError(TEXT("Missing-payload fixture could not find its leaf")); return false;
			}
			Update.Value.PayloadDigest = MakeDigest(0xAD);
			TArray<FTerrainIndexUpdate> Updates;
			Updates.Add(Update);
			FTerrainIndexRoot NewRoot;
			int32 Pages = 0;
			if (TerrainIndexApply(Opened.GetState().Identity, Opened.GetObjects(),
				Opened.GetObjects(), OldRoot, Updates, NewRoot, Pages) != ETerrainPersistError::None)
			{
				AddError(TEXT("Missing-payload fixture could not write index")); return false;
			}
			Descriptor.RootPageDigest = NewRoot.RootPageDigest;
			Descriptor.RootPageLength = NewRoot.RootPageLength;
		}
		else { ++Descriptor.RootPageLength; }
		if (!Opened.PublishCheckpoint(Descriptor, 1789412360000LL).IsOk()) { return false; }
		const FString Before = Fingerprint(Copy);
		FTerrainRetentionStats Refused;
		AddExpectedError(TEXT("Retention refused: the live set could not be walked"),
			EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("Invalid recovery closure stops the sweep"),
			TerrainReclaimStore(Opened, Refused).IsOk());
		TestEqual(TEXT("Invalid closure preserves every file"), Fingerprint(Copy), Before);
	}

	// Interrupt replacement before writing, during writing, and after writing before deletion.
	// Reopen from bytes each time; both checkpoint generations must survive every case.
	for (int32 Case = 0; Case < 3; ++Case)
	{
		FTerrainMemoryStorageDevice Copy = Device;
		FTerrainFaultDevice Faults(Copy);
		FTerrainWorldStore Opened(Faults);
		if (!Opened.Open().IsOk()) { return false; }
		// Every capture went to c.0, so the sweep moves writing to c.1 and compacts c.0 into it:
		// fail the copy, tear the copy, or fail the cut after the copy is durable.
		if (Case == 0) { Faults.FailAfter(ETerrainStorageOp::Append, 0, TerrainStoragePaths::Container(1)); }
		if (Case == 1) { Faults.TearAfter(ETerrainStorageOp::Append, 0, 100, TerrainStoragePaths::Container(1)); }
		if (Case == 2) { Faults.FailAfter(ETerrainStorageOp::Truncate, 0, TerrainStoragePaths::Container(0)); }
		FTerrainRetentionStats Interrupted;
		TestFalse(TEXT("Injected compaction interruption is reported"),
			TerrainReclaimStore(Opened, Interrupted).IsOk());
		Faults.ClearFaults();
		if (!VerifyRestore(Copy, 12, HashesAtG12)) { return false; }
		FTerrainMemoryStorageDevice Fallback = Copy;
		if (!BreakNewestSlot(Fallback) || !VerifyRestore(Fallback, 8, HashesAtG8)) { return false; }
		// Retry in the same process -- after a torn copy, the retry must cut the tail first.
		FTerrainRetentionStats Retried;
		TestTrue(TEXT("Retry after an interrupted sweep succeeds without restarting"),
			TerrainReclaimStore(Opened, Retried).IsOk());
		if (!VerifyRestore(Copy, 12, HashesAtG12)) { return false; }
	}

	// ===== a capture in flight blocks reclamation ==========================================
	{
		Store.GetObjects().BeginBatch();   // stands in for a capture holding an open batch

		FTerrainRetentionStats Blocked;
		const FTerrainStoreResult Refused = TerrainReclaimStore(Store, Blocked);
		TestFalse(TEXT("Reclamation refuses while a capture holds an open batch"), Refused.IsOk());
		TestEqual(TEXT("and touches nothing"), Blocked.ContainersCompacted + Blocked.LegacyFilesDeleted, 0);

		Store.GetObjects().AbandonBatch();
	}

	// ===== the sweep =======================================================================
	const int64 BeforeBytes = Device.TotalBytes();

	FTerrainRetentionStats Stats;
	const FTerrainStoreResult Reclaimed = TerrainReclaimStore(Store, Stats);
	if (!Reclaimed.IsOk())
	{
		AddError(FString::Printf(TEXT("Reclaim failed: %s"), *Reclaimed.ToString()));
		return false;
	}

	TestTrue(TEXT("Something was actually live"), Stats.LiveObjects > 0);
	TestTrue(TEXT("and something was actually reclaimed -- otherwise this proves nothing"),
		Stats.ObjectsDropped > 0);
	TestTrue(TEXT("so the store got smaller"), Device.TotalBytes() < BeforeBytes);

	// Specifically: compaction had to do the work. Path-copying shares pages between
	// generations, so nothing ever becomes entirely dead on its own -- measured at 176 bytes
	// freed before compaction existed.
	TestTrue(TEXT("Writing moved to an empty container"), Stats.bRotated);
	TestTrue(TEXT("so the one holding garbage could be compacted"), Stats.ContainersCompacted > 0);
	TestTrue(TEXT("The reclaim is worth having, not a rounding error"),
		BeforeBytes - Device.TotalBytes() > BeforeBytes / 10);

	// P-005's claim, as a count: three captures and a full compaction, and not one name.
	TestEqual(TEXT("After bootstrap the open world created no name"),
		Counted.OpCount(ETerrainStorageOp::WriteNew), BootstrapWriteNew);
	TestEqual(TEXT("and removed none"), Counted.OpCount(ETerrainStorageOp::Delete), 0);
	TestTrue(TEXT("while compaction really cut a container"), Counted.OpCount(ETerrainStorageOp::Truncate) > 0);

	AddInfo(FString::Printf(
		TEXT("Retention: %d live; %d containers compacted (%d dead objects dropped), rotated=%d; ")
		TEXT("%lld bytes reclaimed, %lld -> %lld total"),
		Stats.LiveObjects, Stats.ContainersCompacted, Stats.ObjectsDropped, Stats.bRotated ? 1 : 0,
		Stats.BytesReclaimed, BeforeBytes, Device.TotalBytes()));

	// ===== the current generation still restores, chunk for chunk ==========================
	{
		FTerrainWorldStore Reopened(Device);
		if (!Reopened.Open().IsOk())
		{
			AddError(TEXT("Open after reclaim failed"));
			return false;
		}
		TestEqual(TEXT("The current checkpoint is still at G=12"),
			Reopened.GetState().Checkpoint.G, (FTerrainOpSeq)12);

		FMemoryTerrainBackend Restored;
		Restored.Initialize(Init);
		FTerrainRevisionIndex RestoredRevisions;
		FTerrainRestoreStats RestoreStats;
		const FTerrainStoreResult Result =
			TerrainRestoreCheckpoint(Reopened, Restored, RestoredRevisions, RestoreStats);
		if (!Result.IsOk())
		{
			AddError(FString::Printf(TEXT("Restore after reclaim failed: %s"), *Result.ToString()));
			return false;
		}

		int32 Mismatched = 0;
		for (const TPair<FTerrainChunkKey, uint64>& Expected : HashesAtG12)
		{
			if (Restored.HashRegion(Expected.Key) != Expected.Value) { ++Mismatched; }
		}
		TestEqual(TEXT("and every chunk it names survived the sweep intact"), Mismatched, 0);
	}

	// Corrupt only the newer root after reclamation: ordinary recovery must select G=8
	// and reconstruct its actual terrain, not merely find its payload filenames.
	{
		FTerrainMemoryStorageDevice Fallback = Device;
		if (!BreakNewestSlot(Fallback) || !VerifyRestore(Fallback, 8, HashesAtG8)) { return false; }
	}

	// ===== a second sweep finds nothing left to do =========================================
	{
		FTerrainWorldStore Reopened(Device);
		if (!Reopened.Open().IsOk())
		{
			AddError(TEXT("Open failed"));
			return false;
		}
		FTerrainRetentionStats Again;
		const FTerrainStoreResult Result = TerrainReclaimStore(Reopened, Again);
		TestTrue (TEXT("A second sweep succeeds"), Result.IsOk());
		TestEqual(TEXT("and has nothing left to compact -- the first one was complete"),
			Again.ContainersCompacted, 0);
		TestFalse(TEXT("nor any reason to move writing"), Again.bRotated);
		TestEqual(TEXT("because every surviving object is live"), Again.ObjectsDropped, 0);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

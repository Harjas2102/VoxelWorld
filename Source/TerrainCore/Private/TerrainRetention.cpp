// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainRetention.h"

#include "TerrainCore.h"
#include "TerrainPersistenceIndex.h"
#include "TerrainPersistenceRecords.h"

namespace
{
	// Use the same structural traversal as restore, recording every successfully loaded
	// dependency. A second, looser page walker must not authorize deletion of recovery data.
	class FMarkingObjectStore final : public ITerrainObjectStore
	{
	public:
		FMarkingObjectStore(const ITerrainObjectStore& InSource, TSet<FTerrainDigest>& InLive)
			: Source(InSource), Live(InLive) {}

		virtual bool LoadObject(const FTerrainDigest& Digest, TArray<uint8>& Out) const override
		{
			if (!Source.LoadObject(Digest, Out)) { return false; }
			Live.Add(Digest);
			return true;
		}
		virtual bool StoreObject(const FTerrainDigest&, TArrayView<const uint8>) override
		{
			return false;
		}
	private:
		const ITerrainObjectStore& Source;
		TSet<FTerrainDigest>& Live;
	};

	ETerrainPersistError MarkCheckpoint(
		const FTerrainPersistIdentity& Identity,
		const FTerrainBaseDescriptor& Base,
		const ITerrainObjectStore& Objects,
		const FTerrainRootSlot& Slot,
		TSet<FTerrainDigest>& Live)
	{
		FMarkingObjectStore Marked(Objects, Live);
		TArray<uint8> Object;
		if (!Marked.LoadObject(Slot.DescriptorDigest, Object))
		{
			return ETerrainPersistError::ShortBuffer;
		}
		if (uint32(Object.Num()) != Slot.DescriptorLength)
		{
			return ETerrainPersistError::BodyLengthOutOfRange;
		}
		FTerrainPersistObjectHeader Header;
		TArrayView<const uint8> Body;
		ETerrainPersistError Error = TerrainPersistDecodeObject(
			Object, ETerrainPersistObjectType::CheckpointDescriptor, &Identity, Header, Body);
		if (Error != ETerrainPersistError::None) { return Error; }
		FTerrainCheckpointDescriptor Descriptor;
		Error = TerrainPersistDecodeCheckpointBody(Body, Descriptor);
		if (Error != ETerrainPersistError::None) { return Error; }
		if (Descriptor.G != Slot.G || Descriptor.Generation != Slot.Generation)
		{
			return ETerrainPersistError::FieldOutOfRange;
		}

		FTerrainIndexRoot Root;
		Root.bHasRootPage = Descriptor.bHasRootPage;
		Root.RootPageDigest = Descriptor.RootPageDigest;
		Root.RootPageLength = Descriptor.RootPageLength;
		ETerrainPersistError PayloadError = ETerrainPersistError::None;
		const ETerrainPersistError WalkError = TerrainIndexEnumerate(
			Identity, Marked, Root,
			[&](const FTerrainChunkKey& Key, const FTerrainIndexLeafValue& Value)
			{
				if (Value.Rev == 0 || Value.LastOpSeq == 0 || Value.LastOpSeq > Descriptor.G)
				{
					PayloadError = ETerrainPersistError::OrderViolation;
					return false;
				}
				const int32 Coordinates[] = {Key.X, Key.Y, Key.Z};
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					const int64 Min = int64(Coordinates[Axis]) * TerrainChunkSizeVox;
					if (Min >= Base.WorldBoundsVox.Max[Axis]
						|| Min + TerrainChunkSizeVox <= Base.WorldBoundsVox.Min[Axis])
					{
						PayloadError = ETerrainPersistError::FieldOutOfRange;
						return false;
					}
				}
				if (Value.Encoding == ETerrainRegionEncoding::Empty) { return true; }
				TArray<uint8> Payload;
				if (!Marked.LoadObject(Value.PayloadDigest, Payload))
				{
					PayloadError = ETerrainPersistError::ShortBuffer;
					return false;
				}
				if (uint32(Payload.Num()) != Value.PayloadLength)
				{
					PayloadError = ETerrainPersistError::BodyLengthOutOfRange;
					return false;
				}
				FTerrainPersistObjectHeader PayloadHeader;
				TArrayView<const uint8> PayloadBody;
				PayloadError = TerrainPersistDecodeObject(Payload,
					ETerrainPersistObjectType::ChunkPayload, &Identity, PayloadHeader, PayloadBody);
				if (PayloadError != ETerrainPersistError::None) { return false; }
				FTerrainChunkPayloadRecord Record;
				PayloadError = TerrainPersistDecodeChunkPayloadBody(
					PayloadBody, Base.GeneratorVersion, Base.ValueConfig, Record);
				if (PayloadError != ETerrainPersistError::None) { return false; }
				if (!(Record.Key == Key) || Record.Rev != Value.Rev
					|| Record.LastOpSeq != Value.LastOpSeq || Record.Encoding != Value.Encoding)
				{
					PayloadError = ETerrainPersistError::FieldOutOfRange;
					return false;
				}
				return true;
			});
		return WalkError != ETerrainPersistError::None ? WalkError : PayloadError;
	}
}

// ---- the collector (DEF-9, P-003 §5) ------------------------------------

void FTerrainRetentionCollector::Launch(TUniqueFunction<void(FWork&)> WorkFunction)
{
	Work = MakeShared<FWork>();
	if (Settings.bInline)
	{
		WorkFunction(*Work);
		return;
	}
	// The worker holds its own references to what it reads and writes; the collector reads the
	// result only after IsCompleted(), and Abandon() waits for it.
	TSharedPtr<FWork> Target = Work;
	Task = UE::Tasks::Launch(UE_SOURCE_LOCATION,
		[Target, Function = MoveTemp(WorkFunction)]() mutable { Function(*Target); });
	bTaskInFlight = true;
}

bool FTerrainRetentionCollector::EpochUnchanged() const
{
	return Store != nullptr && Store->GetObjects().GetReferenceEpoch() == Epoch;
}

FTerrainStoreResult FTerrainRetentionCollector::Begin(
	FTerrainWorldStore& InStore, const FTerrainRetentionSettings& InSettings)
{
	check(IsInGameThread() || InSettings.bInline);
	Abandon();

	Store = &InStore;
	Settings = InSettings;
	CycleStats = FTerrainRetentionStats();
	CycleResult = FTerrainStoreResult::Ok();
	Jobs.Reset();
	JobIndex = 0;
	Live.Reset();
	StartedSeconds = FPlatformTime::Seconds();

	auto Refuse = [this](const FTerrainStoreResult& Result)
	{
		CycleResult = Result;
		CycleStats.Seconds = FPlatformTime::Seconds() - StartedSeconds;
		Store = nullptr;
		return Result;
	};

	if (!InStore.IsOpen())
	{
		return Refuse(FTerrainStoreResult::Io(ETerrainStorageResult::NotFound));
	}

	// Rule 3: a capture in flight has objects buffered and pages written that no root names yet.
	if (InStore.GetObjects().IsBatchOpen())
	{
		UE_LOG(LogTerrainCore, Warning,
			TEXT("Retention refused: a checkpoint capture is in flight. Its objects are not yet ")
			TEXT("named by any root, so a sweep would be entitled to delete them."));
		return Refuse(FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation));
	}

	// Rule 1: read the actual pair; cached redundancy cannot authorize a destructive operation.
	const FTerrainPersistIdentity Identity = InStore.GetState().Identity;
	FTerrainSlotPair DiskRoots(InStore.GetDevice(), ETerrainPersistObjectType::RootSlot,
		TerrainStoragePaths::RootSlot(0), TerrainStoragePaths::RootSlot(1));
	FTerrainSlotState Slots[2];
	int32 Best = INDEX_NONE;
	const ETerrainStorageResult Read = DiskRoots.Read(Identity, Slots[0], Slots[1], Best);
	if (Read != ETerrainStorageResult::Ok)
	{
		return Refuse(FTerrainStoreResult::Io(Read));
	}
	if (!Slots[0].bValid || !Slots[1].bValid)
	{
		UE_LOG(LogTerrainCore, Warning, TEXT("Retention refused: both root slots must validate."));
		return Refuse(FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation));
	}

	FTerrainRootSlot Roots[2];
	for (int32 SlotIndex = 0; SlotIndex < 2; ++SlotIndex)
	{
		const ETerrainPersistError RootError = TerrainPersistDecodeRootSlotBody(Slots[SlotIndex].Body, Roots[SlotIndex]);
		if (RootError != ETerrainPersistError::None)
		{
			return Refuse(FTerrainStoreResult::Bad(RootError));
		}
		if (!InStore.GetJournal() || Roots[SlotIndex].G > InStore.GetJournal()->GetHead())
		{
			return Refuse(FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation));
		}
	}

	// The epoch, and the map as it stands at that epoch. Everything the worker reads comes from
	// this snapshot; everything destructive later is checked against this epoch.
	Epoch = InStore.GetObjects().GetReferenceEpoch();
	Snapshot = InStore.GetObjects().MakeReadSnapshot();
	Phase = EPhase::Marking;

	// --- mark, from BOTH slots (rule 2), off the game thread ----------------------------------------
	TSharedPtr<FTerrainObjectSnapshot> Snap = Snapshot;
	const FTerrainBaseDescriptor Base = InStore.GetState().Base;
	Launch([Snap, Identity, Base, Root0 = Roots[0], Root1 = Roots[1]](FWork& Out)
	{
		const FTerrainRootSlot* Pair[2] = { &Root0, &Root1 };
		for (int32 SlotIndex = 0; SlotIndex < 2; ++SlotIndex)
		{
			const ETerrainPersistError MarkError = MarkCheckpoint(Identity, Base, *Snap, *Pair[SlotIndex], Out.Live);
			if (MarkError != ETerrainPersistError::None)
			{
				Out.Error = MarkError;
				Out.Consumed = SlotIndex;   // which slot failed, for the log
				return;
			}
		}

		// The pre-P-005 files, listed here because listing 256 fan-out directories is I/O the
		// game thread has no business doing. A read-only store over the same device is enough.
		ITerrainStorageDevice& Device = Snap->GetDevice();
		const FTerrainFileObjectStore Lister(Device);
		TArray<FTerrainDigest> Loose;
		TArray<uint64> Packs;
		if (Lister.ListLooseObjects(Loose) != ETerrainStorageResult::Ok
			|| Lister.ListPacks(Packs) != ETerrainStorageResult::Ok)
		{
			Out.bIoError = true;
			return;
		}
		for (const FTerrainDigest& Digest : Loose)
		{
			Out.LegacyFiles.Add(TerrainStoragePaths::Object(Digest));
		}
		for (const uint64 PackId : Packs)
		{
			Out.LegacyFiles.Add(TerrainStoragePaths::Pack(PackId));
		}
		for (const FString& Path : Out.LegacyFiles)
		{
			Out.LegacyBytes += FMath::Max<int64>(Device.Size(Path), 0);
		}
	});
	return FTerrainStoreResult::Ok();
}

bool FTerrainRetentionCollector::Plan()
{
	FTerrainFileObjectStore& Objects = Store->GetObjects();

	// --- 1. pre-P-005 files: what is live in them moves into a container first ---------------------
	if (Work->LegacyFiles.Num() > 0)
	{
		FJob Job;
		Objects.GetSurvivors(INDEX_NONE, Live, Job.Survivors, Job.Dropped);
		Job.LegacyFiles = MoveTemp(Work->LegacyFiles);
		Job.SourceBytes = Work->LegacyBytes;
		Jobs.Add(MoveTemp(Job));
	}

	// Worth compacting: enough of it is dead that copying the rest out is a real saving.
	auto WorthCompacting = [&](int32 Index)
	{
		const int64 Dead = Objects.ContainerDeadBytes(Index, Live);
		return Dead > 0 && Dead >= Settings.MinDeadFraction * Objects.ContainerObjectBytes(Index);
	};

	// --- 2. move writing off a container that holds garbage, so it can be compacted -------------
	// Compaction never touches the active container: it would copy into itself and then cut
	// itself. With no empty container to move to, the active one waits for a later cycle.
	if (WorthCompacting(Objects.GetActiveContainer()))
	{
		CycleStats.bRotated = Objects.RotateActiveToEmpty();
	}

	// --- 3. every other container holding dead bytes ---------------------------------------------
	const int32 Active = Objects.GetActiveContainer();
	for (int32 Index = 0; Index < TerrainStoragePaths::ContainerCount; ++Index)
	{
		if (Index == Active || Objects.ContainerValidEnd(Index) <= 0)
		{
			continue;
		}
		if (!WorthCompacting(Index))
		{
			++CycleStats.ContainersKept;
			continue;
		}
		FJob Job;
		Job.Container = Index;
		Job.SourceBytes = Objects.ContainerValidEnd(Index);
		Objects.GetSurvivors(Index, Live, Job.Survivors, Job.Dropped);
		Jobs.Add(MoveTemp(Job));
	}

	Work.Reset();
	if (Jobs.Num() == 0)
	{
		return false;
	}
	JobIndex = 0;
	Jobs[0].TargetBytesBefore = Objects.ContainerValidEnd(Active);
	return true;
}

bool FTerrainRetentionCollector::Tick()
{
	if (Phase == EPhase::Idle)
	{
		return true;
	}
	if (bTaskInFlight)
	{
		if (!Task.IsCompleted())
		{
			return false;   // waiting on the worker is not a game-thread step
		}
		bTaskInFlight = false;
	}

	const double StepStarted = FPlatformTime::Seconds();
	const bool bDone = Step();
	++CycleStats.GameThreadSteps;
	CycleStats.LongestGameThreadStepSeconds = FMath::Max(
		CycleStats.LongestGameThreadStepSeconds, FPlatformTime::Seconds() - StepStarted);
	if (bDone)
	{
		LogCycle();
	}
	return bDone;
}

bool FTerrainRetentionCollector::Step()
{
	FTerrainFileObjectStore& Objects = Store->GetObjects();

	switch (Phase)
	{
	case EPhase::Marking:
	{
		if (Work->Error != ETerrainPersistError::None)
		{
			// Nothing has been deleted. An incomplete mark looks exactly like a small live set,
			// and sweeping on one would delete live data -- so a failed mark fails the cycle.
			UE_LOG(LogTerrainCore, Error,
				TEXT("Retention refused: the live set could not be walked from root slot %d ")
				TEXT("(%s). Nothing was deleted."),
				Work->Consumed, TerrainPersistErrorName(Work->Error));
			Finish(FTerrainStoreResult::Bad(Work->Error));
			return true;
		}
		if (Work->bIoError)
		{
			Finish(FTerrainStoreResult::Io(ETerrainStorageResult::IoError));
			return true;
		}
		if (!EpochUnchanged())
		{
			AbandonForEpoch();
			return true;
		}
		Live = MoveTemp(Work->Live);
		CycleStats.LiveObjects = Live.Num();

		// The mark must contain the store's own current checkpoint: cheap, and it turns a silent
		// catastrophe (marking from stale state) into a refusal.
		if (!Live.Contains(Store->GetState().Root.DescriptorDigest))
		{
			UE_LOG(LogTerrainCore, Error,
				TEXT("Retention refused: the live set walked from the root slots does not contain the ")
				TEXT("current checkpoint (generation %llu, G=%llu). The slots and the open store ")
				TEXT("disagree, so nothing here can be trusted to be garbage. Nothing was deleted."),
				Store->GetState().Root.Generation, Store->GetState().Root.G);
			Finish(FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation));
			return true;
		}

		if (!Plan())
		{
			Finish(FTerrainStoreResult::Ok());
			return true;
		}
		Phase = EPhase::Copying;
		return false;
	}

	case EPhase::Copying:
	{
		FJob& Job = Jobs[JobIndex];

		// A frame the worker finished building: one bounded append.
		if (Work.IsValid())
		{
			if (Work->bIoError)
			{
				Finish(FTerrainStoreResult::Io(ETerrainStorageResult::IoError));
				return true;
			}
			if (!EpochUnchanged())
			{
				AbandonForEpoch();
				return true;
			}
			const ETerrainStorageResult Appended = Objects.AppendPreparedImage(Work->Image);
			if (Appended != ETerrainStorageResult::Ok)
			{
				Finish(FTerrainStoreResult::Io(Appended));
				return true;
			}
			Job.Copied += Work->Consumed;
			++CycleStats.FramesAppended;
			Work.Reset();
			return false;
		}

		// The next frame, built off the game thread from the snapshot.
		if (Job.Copied < Job.Survivors.Num())
		{
			TArray<FTerrainDigest> Remaining(Job.Survivors.GetData() + Job.Copied, Job.Survivors.Num() - Job.Copied);
			Launch([Snap = Snapshot, Remaining = MoveTemp(Remaining), Max = Settings.MaxFrameBytes](FWork& Out)
			{
				FTerrainPackImageBuilder Builder;
				for (const FTerrainDigest& Digest : Remaining)
				{
					TArray<uint8> Bytes;
					if (!Snap->LoadObject(Digest, Bytes))   // resolved and digest-verified
					{
						Out.bIoError = true;
						return;
					}
					if (Builder.Num() > 0 && Builder.BodyBytes() + Bytes.Num() > Max)
					{
						break;   // bounded buffer: the rest go in the next frame
					}
					Builder.Add(Digest, Bytes);
				}
				Builder.Finish(Out.Image);
				Out.Consumed = Builder.Num();
			});
			return false;
		}

		// Everything is copied. Read every copy back from where it now is, off the game thread,
		// before anything is cut. LoadObject would be satisfied by the source copy, which is
		// exactly the copy about to go, so the reads name the active container explicitly.
		const int32 Active = Objects.GetActiveContainer();
		TArray<TPair<FTerrainDigest, FTerrainObjectLocation>> Copies;
		Copies.Reserve(Job.Survivors.Num());
		for (const FTerrainDigest& Digest : Job.Survivors)
		{
			FTerrainObjectLocation Where;
			if (!Objects.FindInContainer(Active, Digest, Where))
			{
				Finish(FTerrainStoreResult::Io(ETerrainStorageResult::IoError));
				return true;
			}
			Copies.Emplace(Digest, Where);
		}
		Phase = EPhase::Verifying;
		Launch([Snap = Snapshot, Copies = MoveTemp(Copies), Path = TerrainStoragePaths::Container(Active)](FWork& Out)
		{
			for (const TPair<FTerrainDigest, FTerrainObjectLocation>& Copy : Copies)
			{
				TArray<uint8> Bytes;
				if (!FTerrainObjectSnapshot::LoadVerified(Snap->GetDevice(), Path, Copy.Value, Copy.Key, Bytes))
				{
					Out.bIoError = true;
					return;
				}
			}
		});
		return false;
	}

	case EPhase::Verifying:
	{
		if (Work->bIoError)
		{
			Finish(FTerrainStoreResult::Io(ETerrainStorageResult::IoError));
			return true;
		}
		Work.Reset();
		Phase = EPhase::Cutting;
		return false;
	}

	case EPhase::Cutting:
	{
		// THE RULE. Nothing referenced since the mark, and no capture holding references that
		// no root names yet. Otherwise the mark may not describe what a root can reach.
		if (!EpochUnchanged())
		{
			AbandonForEpoch();
			return true;
		}
		if (Objects.IsBatchOpen())
		{
			if (Settings.bInline)
			{
				Finish(FTerrainStoreResult::Bad(ETerrainPersistError::OrderViolation));
				return true;
			}
			return false;   // a capture is under way; its first store will end this cycle
		}

		FJob& Job = Jobs[JobIndex];
		const int32 Active = Objects.GetActiveContainer();
		if (Job.Container != INDEX_NONE)
		{
			const ETerrainStorageResult Cut = Objects.TruncateContainer(Job.Container);
			if (Cut != ETerrainStorageResult::Ok)
			{
				Finish(FTerrainStoreResult::Io(Cut));
				return true;
			}
			++CycleStats.ContainersCompacted;
			CycleStats.ObjectsDropped += Job.Dropped;
		}
		else
		{
			const int32 Stop = FMath::Min(Job.LegacyFiles.Num(), Job.Deleted + FMath::Max(1, Settings.MaxDeletesPerStep));
			for (; Job.Deleted < Stop; ++Job.Deleted)
			{
				const ETerrainStorageResult Deleted = Objects.DeleteLegacyFile(Job.LegacyFiles[Job.Deleted]);
				if (Deleted != ETerrainStorageResult::Ok)
				{
					Finish(FTerrainStoreResult::Io(Deleted));
					return true;
				}
				++CycleStats.LegacyFilesDeleted;
			}
			if (Job.Deleted < Job.LegacyFiles.Num())
			{
				return false;   // bounded: more next step
			}
			Objects.ForgetLegacyPacks();
			CycleStats.LegacyObjectsMigrated += Job.Survivors.Num();
		}

		// Net: the copies grew the active container by some of what the source held.
		const int64 Grown = Objects.ContainerValidEnd(Active) - Job.TargetBytesBefore;
		CycleStats.BytesReclaimed += FMath::Max<int64>(Job.SourceBytes - Grown, 0);

		if (++JobIndex >= Jobs.Num())
		{
			Finish(FTerrainStoreResult::Ok());
			return true;
		}
		Jobs[JobIndex].TargetBytesBefore = Objects.ContainerValidEnd(Active);
		Phase = EPhase::Copying;
		return false;
	}

	default:
		return true;
	}
}

void FTerrainRetentionCollector::AbandonForEpoch()
{
	CycleStats.bAbandonedForEpoch = true;
	UE_LOG(LogTerrainCore, Log,
		TEXT("Retention: the reference epoch moved (a capture stored an object or a root was ")
		TEXT("published), so this cycle stops before deleting anything further. Copies already ")
		TEXT("made are harmless duplicates; the next cycle marks afresh."));
	Finish(FTerrainStoreResult::Ok());
}

void FTerrainRetentionCollector::Finish(const FTerrainStoreResult& Result)
{
	CycleResult = Result;
	CycleStats.Seconds = FPlatformTime::Seconds() - StartedSeconds;
	Phase = EPhase::Idle;
	Snapshot.Reset();
	Work.Reset();
	Live.Reset();
	Jobs.Reset();
}

void FTerrainRetentionCollector::LogCycle() const
{
	UE_LOG(LogTerrainCore, Log,
		TEXT("Retention: %d live objects; %d migrated from %d pre-P-005 files; rotated=%s; ")
		TEXT("%d containers compacted (%d dead objects dropped, %d frames), %d untouched; %lld bytes ")
		TEXT("reclaimed in %.3f s; %d game-thread steps, longest %.4f s%s; %s."),
		CycleStats.LiveObjects, CycleStats.LegacyObjectsMigrated, CycleStats.LegacyFilesDeleted,
		CycleStats.bRotated ? TEXT("yes") : TEXT("no"), CycleStats.ContainersCompacted,
		CycleStats.ObjectsDropped, CycleStats.FramesAppended, CycleStats.ContainersKept,
		CycleStats.BytesReclaimed, CycleStats.Seconds, CycleStats.GameThreadSteps,
		CycleStats.LongestGameThreadStepSeconds,
		Settings.bInline ? TEXT(" (inline: includes worker time)") : TEXT(""),
		CycleStats.bAbandonedForEpoch ? TEXT("abandoned for epoch") : *CycleResult.ToString());
}

void FTerrainRetentionCollector::Abandon()
{
	if (bTaskInFlight)
	{
		Task.Wait();
		bTaskInFlight = false;
	}
	if (Phase != EPhase::Idle)
	{
		Phase = EPhase::Idle;
		Snapshot.Reset();
		Work.Reset();
		Live.Reset();
		Jobs.Reset();
	}
}

FTerrainStoreResult TerrainReclaimStore(
	FTerrainWorldStore& Store, FTerrainRetentionStats& OutStats, const FTerrainRetentionSettings& InSettings)
{
	FTerrainRetentionSettings Settings = InSettings;
	Settings.bInline = true;

	FTerrainRetentionCollector Collector;
	const FTerrainStoreResult Started = Collector.Begin(Store, Settings);
	if (!Started.IsOk())
	{
		OutStats = Collector.Stats();
		return Started;
	}
	while (!Collector.Tick())
	{
	}
	OutStats = Collector.Stats();
	return Collector.Result();
}

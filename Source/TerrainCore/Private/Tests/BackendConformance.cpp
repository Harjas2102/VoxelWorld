// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#if WITH_DEV_AUTOMATION_TESTS

#include "BackendConformance.h"
#include "MemoryTerrainBackend.h"
#include "Misc/AutomationTest.h"

namespace
{
	constexpr int32 ConformanceChunkSize = 32;
	constexpr int32 ConformanceSampleCount = 32768;
	constexpr uint32 GeneratorVersion = 7;

	FTerrainBackendInit MakeInit(ETerrainRole Role = ETerrainRole::Server)
	{
		FTerrainBackendInit Init;
		Init.Seed = 12345;
		Init.GeneratorVersion = GeneratorVersion;
		Init.VoxelSizeCm = 50.f;
		Init.WorldBoundsVox = FTerrainBox(FIntVector(-64), FIntVector(64));
		Init.Role = Role;
		return Init;
	}

	FTerrainStreamingInterest MakeInterest(uint32 Id = 1)
	{
		FTerrainStreamingInterest Interest;
		Interest.InterestId = Id;
		Interest.RadiusCm = 6000.0; // includes our entire small fixture world
		return Interest;
	}

	void Store16(TArray<uint8>& Payload, int32 Offset, uint16 Value)
	{
		Payload[Offset] = static_cast<uint8>(Value);
		Payload[Offset + 1] = static_cast<uint8>(Value >> 8);
	}

	FTerrainRegionData MakeRegion(const FTerrainChunkKey& Key, int16 Density = -32767, FTerrainMatId Material = 17)
	{
		FTerrainRegionData Data;
		Data.Key = Key;
		Data.GeneratorVersion = GeneratorVersion;
		Data.Encoding = ETerrainRegionEncoding::Dense;
		Data.Payload.SetNumUninitialized(ConformanceSampleCount * 4);
		for (int32 Index = 0; Index < ConformanceSampleCount; ++Index)
		{
			Store16(Data.Payload, Index * 2, static_cast<uint16>(Density));
			Store16(Data.Payload, ConformanceSampleCount * 2 + Index * 2, Material);
		}
		return Data;
	}

	FIntVector PositionAt(const FTerrainChunkKey& Key, int32 Index)
	{
		return FIntVector(Key.X * ConformanceChunkSize + Index % ConformanceChunkSize, Key.Y * ConformanceChunkSize + (Index / ConformanceChunkSize) % ConformanceChunkSize,
			Key.Z * ConformanceChunkSize + Index / (ConformanceChunkSize * ConformanceChunkSize));
	}

	TArray<FTerrainChunkKey> EightKeys()
	{
		TArray<FTerrainChunkKey> Keys;
		for (int32 Z = -1; Z <= 0; ++Z)
		for (int32 Y = -1; Y <= 0; ++Y)
		for (int32 X = -1; X <= 0; ++X)
		{
			Keys.Emplace(X, Y, Z);
		}
		return Keys;
	}

	TUniquePtr<ITerrainBackend> Start(FAutomationTestBase& Test, const FTerrainBackendFactory& Factory,
		ETerrainRole Role = ETerrainRole::Server)
	{
		TUniquePtr<ITerrainBackend> Backend = Factory();
		if (!Test.TestTrue(TEXT("Factory supplies a backend"), Backend.IsValid()))
		{
			return nullptr;
		}
		if (!Test.TestTrue(TEXT("Initialize succeeds"), Backend->Initialize(MakeInit(Role))))
		{
			return nullptr;
		}
		return Backend;
	}

	void CheckUnavailable(FAutomationTestBase& Test, ITerrainBackend& Backend, const FIntVector& Position)
	{
		FTerrainPointSample Sample;
		Sample.bResident = true; // detect a stale out parameter
		Backend.QueryPoint(Position, Sample);
		Test.TestFalse(TEXT("Unavailable point reports bResident false"), Sample.bResident);
	}

	bool CheckPoint(FAutomationTestBase& Test, ITerrainBackend& Backend, const FIntVector& Position,
		bool bSolid, FTerrainMatId Material)
	{
		FTerrainPointSample Sample;
		if (!Test.TestTrue(TEXT("Resident QueryPoint succeeds"), Backend.QueryPoint(Position, Sample)))
		{
			return false;
		}
		const bool bMatches = Sample.bResident && Sample.MaterialId == Material
			&& FMath::IsFinite(Sample.Density) && (bSolid ? Sample.Density < 0.f : Sample.Density > 0.f);
		if (!bMatches)
		{
			Test.AddError(FString::Printf(TEXT("Query (%d,%d,%d): density=%f material=%u resident=%d; expected solid=%d material=%u"),
				Position.X, Position.Y, Position.Z, Sample.Density, Sample.MaterialId,
				Sample.bResident, bSolid, Material));
		}
		return bMatches;
	}

	bool RunLifecycle(FAutomationTestBase& Test, const FTerrainBackendFactory& Factory)
	{
		TUniquePtr<ITerrainBackend> Backend = Factory();
		if (!Test.TestTrue(TEXT("Lifecycle factory succeeds"), Backend.IsValid()))
		{
			return false;
		}
		const FTerrainChunkKey Key(0, 0, 0);
		const FTerrainRegionData Data = MakeRegion(Key);
		// Exercise the same dead-state contract before initialization and after teardown.
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			FTerrainEditResult Result;
			Result.VoxelsTouched = 123;
			FTerrainOp Op;
			Op.RadiusVoxQ16 = 65536;
			Test.TestFalse(TEXT("ApplyOp is safe when not initialized"), Backend->ApplyOp(Op, Result));
			Test.TestEqual(TEXT("Dead ApplyOp clears its result"), Result.VoxelsTouched, int64(0));
			FTerrainRegionData Read = Data;
			Test.TestFalse(TEXT("ReadRegion is safe when not initialized"), Backend->ReadRegion(Key, Read));
			Test.TestTrue(TEXT("Dead ReadRegion resets output"), Read.Encoding == ETerrainRegionEncoding::Empty && Read.Payload.IsEmpty());
			Test.TestFalse(TEXT("WriteRegion is safe when not initialized"), Backend->WriteRegion(Data));
			Test.TestFalse(TEXT("Dead backend is not resident"), Backend->IsRegionResident(Key));
			Test.TestTrue(TEXT("Dead hash is zero"), Backend->HashRegion(Key) == 0);
			CheckUnavailable(Test, *Backend, FIntVector(16));
			Backend->SetStreamingInterest(MakeInterest());
			Backend->ClearStreamingInterest(9999);
			Backend->FlushPendingWork();
			Backend->FlushPendingWork();
			Test.TestFalse(TEXT("Interest cannot revive a shut down backend"), Backend->IsRegionResident(Key));
			Backend->Shutdown();
			Backend->Shutdown();
			if (Pass == 0)
			{
				if (!Test.TestTrue(TEXT("Initialize after pre-init calls succeeds"), Backend->Initialize(MakeInit())))
				{
					return false;
				}
				Backend->SetStreamingInterest(MakeInterest());
				Test.TestFalse(TEXT("Null field and interest do not fabricate unwritten air"), Backend->IsRegionResident(Key));
				CheckUnavailable(Test, *Backend, FIntVector(16));
				Test.TestTrue(TEXT("Explicit fixture write succeeds"), Backend->WriteRegion(Data));
				const uint64 Hash = Backend->HashRegion(Key);
				Test.TestFalse(TEXT("Double Initialize rejects without destroying data"), Backend->Initialize(MakeInit()));
				Test.TestTrue(TEXT("Rejected Initialize preserves hash"), Hash == Backend->HashRegion(Key));
				Backend->Shutdown();
			}
		}
		Test.TestTrue(TEXT("Reinitialize after double Shutdown succeeds"), Backend->Initialize(MakeInit()));
		Backend->SetStreamingInterest(MakeInterest());
		Test.TestFalse(TEXT("Shutdown discarded old data"), Backend->IsRegionResident(Key));
		Backend->Shutdown();
		return !Test.HasAnyErrors();
	}

	bool RunStreamingAndTransfer(FAutomationTestBase& Test, const FTerrainBackendFactory& Factory)
	{
		auto Backend = Start(Test, Factory);
		if (!Backend) return false;
		const FTerrainChunkKey West(-1, 0, 0), East(0, 0, 0);
		Test.TestTrue(TEXT("WriteRegion stages west data"), Backend->WriteRegion(MakeRegion(West)));
		Test.TestTrue(TEXT("WriteRegion stages east data"), Backend->WriteRegion(MakeRegion(East)));
		Test.TestFalse(TEXT("WriteRegion alone does not create residency"), Backend->IsRegionResident(West));
		FTerrainStreamingInterest Interest;
		Interest.InterestId = 10;
		Interest.WorldLocation = FVector(-1.0, 1.0, 1.0); // west of the zero boundary
		Backend->SetStreamingInterest(Interest);
		Test.TestTrue(TEXT("Zero-radius interest loads its negative chunk"), Backend->IsRegionResident(West));
		Test.TestFalse(TEXT("Zero-radius interest excludes neighbouring chunk"), Backend->IsRegionResident(East));
		CheckPoint(Test, *Backend, FIntVector(-1, 0, 0), true, 17);
		CheckUnavailable(Test, *Backend, FIntVector(0));
		const uint64 WestHash = Backend->HashRegion(West);
		Interest.InterestId = 11;
		Backend->SetStreamingInterest(Interest);
		Backend->ClearStreamingInterest(10);
		Test.TestTrue(TEXT("Overlapping interest retains residency"), Backend->IsRegionResident(West));
		Backend->ClearStreamingInterest(987654);
		Test.TestTrue(TEXT("Clearing unknown interest changes nothing"), WestHash == Backend->HashRegion(West));
		Interest.WorldLocation = FVector(0.0, 1.0, 1.0);
		Backend->SetStreamingInterest(Interest); // replace same handle, do not append
		Test.TestFalse(TEXT("Moving interest unloads old chunk"), Backend->IsRegionResident(West));
		Test.TestTrue(TEXT("Zero boundary belongs to east chunk"), Backend->IsRegionResident(East));
		Backend->ClearStreamingInterest(11);
		Test.TestFalse(TEXT("Clearing final interest unloads chunk"), Backend->IsRegionResident(East));
		Backend->SetStreamingInterest(MakeInterest());
		Test.TestTrue(TEXT("Re-entering interest restores stored edits"), WestHash == Backend->HashRegion(West));

		FTerrainRegionData Snapshot;
		Test.TestTrue(TEXT("ReadRegion captures a resident chunk"), Backend->ReadRegion(West, Snapshot));
		Test.TestTrue(TEXT("Resident read is Dense"), Snapshot.Encoding == ETerrainRegionEncoding::Dense);
		Test.TestEqual(TEXT("Dense payload size is values plus materials"), Snapshot.Payload.Num(), ConformanceSampleCount * 4);
		Test.TestTrue(TEXT("WriteRegion accepts captured data"), Backend->WriteRegion(Snapshot));
		Test.TestTrue(TEXT("Read/Write round trip preserves hash"), WestHash == Backend->HashRegion(West));
		for (int32 Repeat = 0; Repeat < 5; ++Repeat)
		{
			Backend->FlushPendingWork();
			Test.TestTrue(TEXT("Hash stable across repeated calls and empty flushes"), WestHash == Backend->HashRegion(West));
		}
		Snapshot.Rev = 123;
		Snapshot.LastOpSeq = 456;
		Test.TestTrue(TEXT("Metadata-only write accepted"), Backend->WriteRegion(Snapshot));
		Test.TestTrue(TEXT("Convergence hash excludes revisions"), WestHash == Backend->HashRegion(West));

		for (int32 BadSize : { 0, 1, ConformanceSampleCount * 2 - 1, ConformanceSampleCount * 4 - 1, ConformanceSampleCount * 4 + 1 })
		{
			FTerrainRegionData Bad = Snapshot;
			Bad.Payload.SetNumZeroed(BadSize);
			Test.TestFalse(TEXT("Malformed Dense size rejected"), Backend->WriteRegion(Bad));
			Test.TestTrue(TEXT("Malformed write leaves data unchanged"), WestHash == Backend->HashRegion(West));
		}
		FTerrainRegionData Bad = Snapshot;
		Bad.ValueConfig = 255;
		Test.TestFalse(TEXT("Incompatible value config rejected"), Backend->WriteRegion(Bad));
		Bad = Snapshot;
		++Bad.GeneratorVersion;
		Test.TestFalse(TEXT("Generator version mismatch rejected"), Backend->WriteRegion(Bad));
		Test.TestTrue(TEXT("Rejected metadata leaves data unchanged"), WestHash == Backend->HashRegion(West));

		// No data in this covered chunk. Empty must not be treated as a Dense air region.
		const FTerrainChunkKey Missing(1, 1, 1);
		Test.TestTrue(TEXT("Nonresident ReadRegion succeeds with Empty"), Backend->ReadRegion(Missing, Snapshot));
		Test.TestTrue(TEXT("Nonresident read resets key/encoding/payload"), Snapshot.Key == Missing
			&& Snapshot.Encoding == ETerrainRegionEncoding::Empty && Snapshot.Payload.IsEmpty());
		Test.TestFalse(TEXT("Nonresident IsRegionResident is false"), Backend->IsRegionResident(Missing));
		CheckUnavailable(Test, *Backend, FIntVector(40));
		Backend->Shutdown();
		return !Test.HasAnyErrors();
	}

	bool RunHashPositions(FAutomationTestBase& Test, const FTerrainBackendFactory& Factory)
	{
		auto A = Start(Test, Factory);
		auto B = Start(Test, Factory);
		if (!A || !B) return false;
		A->SetStreamingInterest(MakeInterest());
		B->SetStreamingInterest(MakeInterest());
		const TArray<FTerrainChunkKey> Keys = EightKeys();
		for (int32 I = 0; I < Keys.Num(); ++I)
		{
			Test.TestTrue(TEXT("Forward-order write"), A->WriteRegion(MakeRegion(Keys[I])));
			Test.TestTrue(TEXT("Reverse-order write"), B->WriteRegion(MakeRegion(Keys[Keys.Num() - 1 - I])));
		}
		for (const FTerrainChunkKey& Key : Keys)
		{
			Test.TestTrue(TEXT("Chunk insertion order does not affect hash"), A->HashRegion(Key) == B->HashRegion(Key));
		}
		const FTerrainChunkKey Key(0, 0, 0);
		FTerrainRegionData Data = MakeRegion(Key);
		// Same multiset, distinct positions. Test density independently from material.
		Store16(Data.Payload, 13 * 2, static_cast<uint16>(12345));
		Test.TestTrue(TEXT("Write asymmetric density fixture"), A->WriteRegion(Data));
		const uint64 DensityHash = A->HashRegion(Key);
		Store16(Data.Payload, 13 * 2, static_cast<uint16>(-32767));
		Store16(Data.Payload, 117 * 2, static_cast<uint16>(12345));
		Test.TestTrue(TEXT("Move same density multiset"), A->WriteRegion(Data));
		Test.TestTrue(TEXT("Hash is position-sensitive for density"), DensityHash != A->HashRegion(Key));
		Data = MakeRegion(Key);
		Store16(Data.Payload, ConformanceSampleCount * 2 + 31 * 2, 201);
		Test.TestTrue(TEXT("Write asymmetric material fixture"), A->WriteRegion(Data));
		const uint64 MaterialHash = A->HashRegion(Key);
		Store16(Data.Payload, ConformanceSampleCount * 2 + 31 * 2, 17);
		Store16(Data.Payload, ConformanceSampleCount * 2 + 1024 * 2, 201);
		Test.TestTrue(TEXT("Move same material multiset"), A->WriteRegion(Data));
		Test.TestTrue(TEXT("Hash is position-sensitive for material"), MaterialHash != A->HashRegion(Key));
		A->Shutdown();
		B->Shutdown();
		return !Test.HasAnyErrors();
	}

	bool RunEdits(FAutomationTestBase& Test, const FTerrainBackendFactory& Factory)
	{
		const TArray<FTerrainChunkKey> Keys = EightKeys();
		for (ETerrainRole Role : { ETerrainRole::Server, ETerrainRole::Client })
		for (ETerrainShape Shape : { ETerrainShape::Sphere, ETerrainShape::Box })
		for (ETerrainOpKind Kind : { ETerrainOpKind::Remove, ETerrainOpKind::Add, ETerrainOpKind::Paint })
		{
			auto Backend = Start(Test, Factory, Role);
			if (!Backend) return false;
			Backend->SetStreamingInterest(MakeInterest());
			const int16 InitialDensity = Kind == ETerrainOpKind::Add ? 32767 : -32767;
			TMap<FTerrainChunkKey, uint64> BeforeHashes;
			for (const FTerrainChunkKey& Key : Keys)
			{
				if (!Test.TestTrue(TEXT("Edit fixture write succeeds"), Backend->WriteRegion(MakeRegion(Key, InitialDensity)))) return false;
				BeforeHashes.Add(Key, Backend->HashRegion(Key));
			}
			FTerrainPointSample Baseline;
			if (!Test.TestTrue(TEXT("Read initial uniform fixture density"), Backend->QueryPoint(FIntVector(0), Baseline))) return false;
			FTerrainOp Op;
			Op.Kind = Kind;
			Op.Shape = Shape;
			Op.RadiusVoxQ16 = 2 * 65536 + 16384;
			Op.ExtentVox = FIntVector(2);
			Op.MaterialId = 29;
			FTerrainEditResult Result;
			if (!Test.TestTrue(TEXT("Sphere/Box Remove/Add/Paint succeeds"), Backend->ApplyOp(Op, Result))) return false;
			if (Role == ETerrainRole::Server)
			{
				Test.TestTrue(TEXT("Read footprint >= write footprint"), Result.VoxelsScanned >= Result.VoxelsTouched);
				Test.TestFalse(TEXT("Small complete edit is not truncated"), Result.bTruncated);
			}

			// Independently observe every sample, including the far outside of the op.
			// This validates exact reporting without copying the backend's geometry kernel.
			int64 Changed = 0;
			TSet<FTerrainChunkKey> ObservedKeys;
			FTerrainBox ObservedBounds;
			for (const FTerrainChunkKey& Key : Keys)
			{
				for (int32 Index = 0; Index < ConformanceSampleCount; ++Index)
				{
					const FIntVector Position = PositionAt(Key, Index);
					FTerrainPointSample Sample;
					if (!Backend->QueryPoint(Position, Sample) || !Sample.bResident)
					{
						Test.AddError(TEXT("Edit unexpectedly made an explicit fixture sample unavailable"));
						return false;
					}
					if (Sample.Density == Baseline.Density && Sample.MaterialId == 17) continue;
					ObservedKeys.Add(Key);
					if (Role == ETerrainRole::Server)
					{
						Test.TestTrue(TEXT("EditedBounds encloses every changed sample"), Result.EditedBounds.Contains(Position));
					}
					if (Changed++ == 0) ObservedBounds = FTerrainBox(Position, Position + FIntVector(1));
					else for (int32 Axis = 0; Axis < 3; ++Axis)
					{
						ObservedBounds.Min[Axis] = FMath::Min(ObservedBounds.Min[Axis], Position[Axis]);
						ObservedBounds.Max[Axis] = FMath::Max(ObservedBounds.Max[Axis], Position[Axis] + 1);
					}
					Test.TestTrue(TEXT("Paint preserves density"), Kind != ETerrainOpKind::Paint || Sample.Density == Baseline.Density);
					Test.TestTrue(TEXT("Remove keeps material; Add/Paint set material"), Sample.MaterialId == (Kind == ETerrainOpKind::Remove ? 17 : 29));
				}
			}
			Test.TestTrue(TEXT("Operation actually changes data"), Changed > 0);
			CheckPoint(Test, *Backend, FIntVector(0), Kind != ETerrainOpKind::Remove, Kind == ETerrainOpKind::Remove ? 17 : 29);
			Test.TestEqual(TEXT("Operation spans all eight neighbouring chunks"), ObservedKeys.Num(), 8);
			if (Role == ETerrainRole::Server)
			{
				Test.TestEqual(TEXT("Touched count matches observed changes"), Result.VoxelsTouched, Changed);
				Test.TestTrue(TEXT("EditedBounds is tight and half-open"), Result.EditedBounds == ObservedBounds);
				Test.TestFalse(TEXT("Max is exclusive"), Result.EditedBounds.Contains(Result.EditedBounds.Max));
				TSet<FTerrainChunkKey> ReportedKeys;
				for (const FTerrainChunkKey& Key : Result.AffectedChunks)
				{
					Test.TestFalse(TEXT("AffectedChunks contains no duplicate"), ReportedKeys.Contains(Key));
					ReportedKeys.Add(Key);
					Test.TestTrue(TEXT("Every reported chunk actually changed"), ObservedKeys.Contains(Key));
				}
				Test.TestEqual(TEXT("No changed chunk omitted"), ReportedKeys.Num(), ObservedKeys.Num());
			}
			for (const FTerrainChunkKey& Key : ObservedKeys)
			{
				Test.TestTrue(TEXT("ApplyOp changes every affected chunk hash"), BeforeHashes.FindChecked(Key) != Backend->HashRegion(Key));
			}
			if (Role == ETerrainRole::Server)
			{
				if (Kind == ETerrainOpKind::Paint) Test.TestTrue(TEXT("Paint carries no physical volume"), Result.Removed.IsEmpty());
				else
				{
					Test.TestFalse(TEXT("Density edit reports volume"), Result.Removed.IsEmpty());
					for (const FTerrainMaterialVolume& Volume : Result.Removed)
					{
						Test.TestTrue(TEXT("Removed volume sign matches Remove/Add"), Kind == ETerrainOpKind::Remove ? Volume.MicroLitres > 0 : Volume.MicroLitres < 0);
						Test.TestTrue(TEXT("Volume uses removed/placed material"), Volume.MaterialId == (Kind == ETerrainOpKind::Remove ? 17 : 29));
					}
				}
			}
			for (ETerrainOpKind Unsupported : { ETerrainOpKind::Flatten, ETerrainOpKind::Smooth })
			{
				Op.Kind = Unsupported;
				const uint64 Before = Backend->HashRegion(Keys[0]);
				Test.TestFalse(TEXT("DEF-5 operation remains unsupported"), Backend->ApplyOp(Op, Result));
				if (Role == ETerrainRole::Server)
				{
					Test.TestEqual(TEXT("Rejected op resets result"), Result.VoxelsTouched, int64(0));
				}
				Test.TestTrue(TEXT("Rejected operation leaves data unchanged"), Before == Backend->HashRegion(Keys[0]));
			}
			Backend->Shutdown();
		}
		return !Test.HasAnyErrors();
	}

	bool RunRejectedEdit(FAutomationTestBase& Test, const FTerrainBackendFactory& Factory)
	{
		auto Backend = Start(Test, Factory);
		if (!Backend) return false;
		Backend->SetStreamingInterest(MakeInterest());
		const FTerrainChunkKey Key(-1, -1, -1);
		Test.TestTrue(TEXT("Partial-residency fixture supplied"), Backend->WriteRegion(MakeRegion(Key)));
		const uint64 Before = Backend->HashRegion(Key);
		FTerrainOp Op;
		Op.Shape = ETerrainShape::Box;
		Op.ExtentVox = FIntVector(2);
		FTerrainEditResult Result;
		Test.TestFalse(TEXT("Edit crossing missing chunk rejects"), Backend->ApplyOp(Op, Result));
		Test.TestTrue(TEXT("Missing-chunk failure did not partially mutate first chunk"), Before == Backend->HashRegion(Key));
		Test.TestTrue(TEXT("Rejected edit reports no changes"), Result.AffectedChunks.IsEmpty()
			&& Result.Removed.IsEmpty() && Result.EditedBounds.IsEmpty() && Result.VoxelsTouched == 0);
		Op.CentreVox = FIntVector(MIN_int32);
		Op.ExtentVox = FIntVector(MAX_int32);
		Test.TestFalse(TEXT("Extreme out-of-world edit fails without integer overflow"), Backend->ApplyOp(Op, Result));
		Test.TestTrue(TEXT("Extreme edit leaves data unchanged"), Before == Backend->HashRegion(Key));
		Backend->Shutdown();
		return !Test.HasAnyErrors();
	}
}

bool RunTerrainPointConformance(FAutomationTestBase& Test, FTerrainBackendFactory Factory)
{
	auto Backend = Start(Test, Factory);
	if (!Backend) return false;
	Backend->SetStreamingInterest(MakeInterest());
	const TArray<FTerrainChunkKey> Keys = EightKeys();
	for (const FTerrainChunkKey& Key : Keys)
	{
		FTerrainRegionData Data = MakeRegion(Key);
		// Distinct signs and material ids on every corner, plus interior data. No
		// uniform fixture can catch an off-by-one or an axis/index transposition.
		TArray<int32> ProbeIndices = { 3 + 32 * 7 + 1024 * 19, 16 + 32 * 16 + 1024 * 16 };
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			ProbeIndices.Add(((Corner & 1) ? 31 : 0) + ((Corner & 2) ? 31 * 32 : 0)
				+ ((Corner & 4) ? 31 * 1024 : 0));
		}
		for (int32 Probe = 0; Probe < ProbeIndices.Num(); ++Probe)
		{
			Store16(Data.Payload, ProbeIndices[Probe] * 2, static_cast<uint16>((Probe & 1) ? 32767 : -32767));
			Store16(Data.Payload, ConformanceSampleCount * 2 + ProbeIndices[Probe] * 2, static_cast<uint16>(100 + Probe));
		}
		if (!Test.TestTrue(TEXT("Write distinct corner/interior fixture"), Backend->WriteRegion(Data))) return false;
		Test.TestTrue(TEXT("Region residency agrees with resident point samples"), Backend->IsRegionResident(Key));
		for (int32 Probe = 0; Probe < ProbeIndices.Num(); ++Probe)
		{
			CheckPoint(Test, *Backend, PositionAt(Key, ProbeIndices[Probe]), (Probe & 1) == 0, static_cast<FTerrainMatId>(100 + Probe));
		}
		// Flip every probe via WriteRegion and query immediately, without a flush.
		for (int32 Probe = 0; Probe < ProbeIndices.Num(); ++Probe)
		{
			Store16(Data.Payload, ProbeIndices[Probe] * 2, static_cast<uint16>((Probe & 1) ? -32767 : 32767));
			Store16(Data.Payload, ConformanceSampleCount * 2 + ProbeIndices[Probe] * 2, static_cast<uint16>(200 + Probe));
		}
		Test.TestTrue(TEXT("Rewrite corner/interior fixture"), Backend->WriteRegion(Data));
		for (int32 Probe = 0; Probe < ProbeIndices.Num(); ++Probe)
		{
			CheckPoint(Test, *Backend, PositionAt(Key, ProbeIndices[Probe]), (Probe & 1) != 0, static_cast<FTerrainMatId>(200 + Probe));
		}
	}
	// All adjacent chunks are now loaded, so edit probes exactly on their corners.
	for (const FTerrainChunkKey& Key : Keys)
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const int32 Index = ((Corner & 1) ? 31 : 0) + ((Corner & 2) ? 31 * 32 : 0) + ((Corner & 4) ? 31 * 1024 : 0);
		FTerrainOp Op;
		Op.CentreVox = PositionAt(Key, Index);
		Op.RadiusVoxQ16 = 16384; // positive sub-voxel radius, centre sample included
		FTerrainEditResult Result;
		Op.Kind = ETerrainOpKind::Add;
		Op.MaterialId = 41;
		Test.TestTrue(TEXT("Corner Add succeeds"), Backend->ApplyOp(Op, Result));
		CheckPoint(Test, *Backend, Op.CentreVox, true, 41);
		Op.Kind = ETerrainOpKind::Paint;
		Op.MaterialId = 42;
		Test.TestTrue(TEXT("Corner Paint succeeds"), Backend->ApplyOp(Op, Result));
		CheckPoint(Test, *Backend, Op.CentreVox, true, 42);
		Op.Kind = ETerrainOpKind::Remove;
		Test.TestTrue(TEXT("Corner Remove succeeds"), Backend->ApplyOp(Op, Result));
		CheckPoint(Test, *Backend, Op.CentreVox, false, 42);
	}
	CheckUnavailable(Test, *Backend, FIntVector(32, 0, 0));
	CheckUnavailable(Test, *Backend, FIntVector(-33, 0, 0));
	CheckUnavailable(Test, *Backend, FIntVector(MAX_int32));
	CheckUnavailable(Test, *Backend, FIntVector(MIN_int32));
	Backend->ClearStreamingInterest(1);
	for (const FTerrainChunkKey& Key : Keys)
	{
		Test.TestFalse(TEXT("Region becomes nonresident after final interest clears"), Backend->IsRegionResident(Key));
		CheckUnavailable(Test, *Backend, PositionAt(Key, 0));
	}
	Backend->Shutdown();
	return !Test.HasAnyErrors();
}

bool RunTerrainBackendConformance(FAutomationTestBase& Test, FTerrainBackendFactory Factory)
{
	RunLifecycle(Test, Factory);
	RunStreamingAndTransfer(Test, Factory);
	RunHashPositions(Test, Factory);
	RunEdits(Test, Factory);
	RunRejectedEdit(Test, Factory);
	RunTerrainPointConformance(Test, Factory); // QueryPoint is one of the eleven methods
	return !Test.HasAnyErrors();
}

// Flag spelling verified against UE 5.7 Core/Private/Tests/HAL/PlatformTest.cpp:41.
// ProductFilter matches the two existing TerrainCore registrations.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainBackendConformanceTest, "TerrainCore.Backend.Conformance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

bool FTerrainBackendConformanceTest::RunTest(const FString& Parameters)
{
	return RunTerrainBackendConformance(*this, []() -> TUniquePtr<ITerrainBackend> { return MakeUnique<FMemoryTerrainBackend>(); });
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainPointConformanceTest, "TerrainCore.Query.Point",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

bool FTerrainPointConformanceTest::RunTest(const FString& Parameters)
{
	return RunTerrainPointConformance(*this, []() -> TUniquePtr<ITerrainBackend> { return MakeUnique<FMemoryTerrainBackend>(); });
}

#endif // WITH_DEV_AUTOMATION_TESTS

// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "MemoryTerrainBackend.h"
#include "ITerrainDensityField.h"

namespace
{
	constexpr int32 MemoryChunkSize = 32;
	constexpr int32 MemorySampleCount = MemoryChunkSize * MemoryChunkSize * MemoryChunkSize;
	constexpr int64 MaxWrites = 65536; // ARCHITECTURE.md 7.1
	constexpr int64 MaxScans = 262144; // bounded reference-backend scratch/work

	int32 ChunkCoordinate(int32 Position)
	{
		return Position / MemoryChunkSize - (Position % MemoryChunkSize < 0 ? 1 : 0);
	}

	FTerrainChunkKey KeyAt(const FIntVector& P)
	{
		return FTerrainChunkKey(ChunkCoordinate(P.X), ChunkCoordinate(P.Y), ChunkCoordinate(P.Z));
	}

	int32 IndexAt(const FIntVector& P)
	{
		const auto Local = [](int32 V) { return (V % MemoryChunkSize + MemoryChunkSize) % MemoryChunkSize; };
		return Local(P.X) + MemoryChunkSize * Local(P.Y) + MemoryChunkSize * MemoryChunkSize * Local(P.Z);
	}

	float Normalise(int16 Value)
	{
		return FMath::Max(-1.f, static_cast<float>(Value) / 32767.f);
	}

	double Occupancy(int16 Value)
	{
		return FMath::Clamp((1.0 - static_cast<double>(Value) / 32767.0) * 0.5, 0.0, 1.0);
	}

	void Append16(TArray<uint8>& Bytes, uint16 Value)
	{
		Bytes.Add(static_cast<uint8>(Value));
		Bytes.Add(static_cast<uint8>(Value >> 8));
	}

	uint16 Read16(const TArray<uint8>& Bytes, int32 Offset)
	{
		return static_cast<uint16>(Bytes[Offset]) | (static_cast<uint16>(Bytes[Offset + 1]) << 8);
	}

	// An avalanched (index, raw value, material) tuple, then a commutative sum.
	// Permuting traversal order cannot change the sum; moving a value changes its tuple.
	uint64 Mix(uint64 V)
	{
		V = (V ^ (V >> 30)) * 0xbf58476d1ce4e5b9ULL;
		V = (V ^ (V >> 27)) * 0x94d049bb133111ebULL;
		return V ^ (V >> 31);
	}

	struct FTerrainPendingChange
	{
		FIntVector Position;
		int16 Value;
		FTerrainMatId Material;
	};
}

bool FMemoryTerrainBackend::Initialize(const FTerrainBackendInit& InInit)
{
	const double Size = InInit.VoxelSizeCm;
	const double Volume = Size * Size * Size * 1000.0; // cm3 -> microlitres
	if (bInitialized || !(Size > 0.0) || !FMath::IsFinite(Size)
		|| !FMath::IsFinite(Volume) || Volume >= static_cast<double>(MAX_int64) / MaxWrites
		|| InInit.WorldBoundsVox.IsEmpty()
		|| (InInit.Role != ETerrainRole::Server && InInit.Role != ETerrainRole::Client))
	{
		return false;
	}
	Init = InInit;
	bInitialized = true;
	return true;
}

void FMemoryTerrainBackend::Shutdown()
{
	Interests.Empty();
	Values.Empty();
	Materials.Empty();
	Metadata.Empty();
	Init = FTerrainBackendInit();
	bInitialized = false;
}

bool FMemoryTerrainBackend::IsKeyInWorld(const FTerrainChunkKey& Key) const
{
	const int64 Low[3] = { int64(Key.X) * MemoryChunkSize, int64(Key.Y) * MemoryChunkSize, int64(Key.Z) * MemoryChunkSize };
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (Low[Axis] >= Init.WorldBoundsVox.Max[Axis]
			|| Low[Axis] + MemoryChunkSize <= Init.WorldBoundsVox.Min[Axis])
		{
			return false;
		}
	}
	return true;
}

bool FMemoryTerrainBackend::HasInterest(const FTerrainChunkKey& Key) const
{
	const int64 Low[3] = { int64(Key.X) * MemoryChunkSize, int64(Key.Y) * MemoryChunkSize, int64(Key.Z) * MemoryChunkSize };
	for (const auto& Entry : Interests)
	{
		const FTerrainStreamingInterest& Interest = Entry.Value;
		const FVector Centre = Interest.WorldLocation / static_cast<double>(Init.VoxelSizeCm);
		const double Radius = Interest.RadiusCm / static_cast<double>(Init.VoxelSizeCm);
		double DistanceSquared = 0.0;
		bool bInside = true;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const double Min = FMath::Max<int64>(Low[Axis], Init.WorldBoundsVox.Min[Axis]);
			const double Max = FMath::Min<int64>(Low[Axis] + MemoryChunkSize, Init.WorldBoundsVox.Max[Axis]);
			bInside &= Centre[Axis] >= Min && Centre[Axis] < Max;
			const double Distance = FMath::Max(FMath::Max(Min - Centre[Axis], Centre[Axis] - Max), 0.0);
			DistanceSquared += Distance * Distance;
		}
		// A radius-zero interest selects exactly one half-open cell; tangent-only
		// contact at positive radius does not load the adjacent chunk.
		if (bInside || (Radius > 0.0 && DistanceSquared < Radius * Radius))
		{
			return true;
		}
	}
	return false;
}

bool FMemoryTerrainBackend::IsRegionResident(const FTerrainChunkKey& Key) const
{
	return bInitialized && IsKeyInWorld(Key) && Values.Contains(Key) && HasInterest(Key);
}

void FMemoryTerrainBackend::GenerateInterestedChunks()
{
	if (!Init.DensityField)
	{
		return; // AR-2: an interest is not a source of natural terrain.
	}
	for (const auto& Entry : Interests)
	{
		const FTerrainStreamingInterest& Interest = Entry.Value;
		const FVector Centre = Interest.WorldLocation / static_cast<double>(Init.VoxelSizeCm);
		const double Radius = Interest.RadiusCm / static_cast<double>(Init.VoxelSizeCm);
		FIntVector First, Last;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			// Clamp in double before narrowing; remote/extreme interests cannot overflow.
			First[Axis] = ChunkCoordinate(FMath::FloorToInt32(FMath::Clamp(Centre[Axis] - Radius,
				double(Init.WorldBoundsVox.Min[Axis]), double(Init.WorldBoundsVox.Max[Axis] - 1))));
			Last[Axis] = ChunkCoordinate(FMath::FloorToInt32(FMath::Clamp(Centre[Axis] + Radius,
				double(Init.WorldBoundsVox.Min[Axis]), double(Init.WorldBoundsVox.Max[Axis] - 1))));
		}
		for (int32 Z = First.Z; Z <= Last.Z; ++Z)
		for (int32 Y = First.Y; Y <= Last.Y; ++Y)
		for (int32 X = First.X; X <= Last.X; ++X)
		{
			const FTerrainChunkKey Key(X, Y, Z);
			if (Values.Contains(Key) || !HasInterest(Key))
			{
				continue;
			}
			TArray<int16> NewValues;
			TArray<FTerrainMatId> NewMaterials;
			NewValues.SetNumZeroed(MemorySampleCount);
			NewMaterials.SetNumZeroed(MemorySampleCount);
			bool bValid = true;
			for (int32 Index = 0; Index < MemorySampleCount; ++Index)
			{
				const FIntVector Position(X * MemoryChunkSize + Index % MemoryChunkSize,
					Y * MemoryChunkSize + (Index / MemoryChunkSize) % MemoryChunkSize, Z * MemoryChunkSize + Index / (MemoryChunkSize * MemoryChunkSize));
				if (!Init.WorldBoundsVox.Contains(Position))
				{
					continue; // out-of-world padding is never queryable or editable
				}
				const FTerrainDensitySample Sample = Init.DensityField->Sample(Position);
				if (!FMath::IsFinite(Sample.Density))
				{
					bValid = false;
					break;
				}
				NewValues[Index] = static_cast<int16>(FMath::RoundToInt32(
					FMath::Clamp(Sample.Density, -1.f, 1.f) * 32767.f));
				NewMaterials[Index] = Sample.MaterialId;
			}
			if (bValid)
			{
				Values.Add(Key, MoveTemp(NewValues));
				Materials.Add(Key, MoveTemp(NewMaterials));
			}
		}
	}
}

void FMemoryTerrainBackend::SetStreamingInterest(const FTerrainStreamingInterest& In)
{
	if (!bInitialized || In.WorldLocation.ContainsNaN() || !FMath::IsFinite(In.RadiusCm)
		|| In.RadiusCm < 0.0
		|| (In.WorldLocation / double(Init.VoxelSizeCm)).ContainsNaN()
		|| !FMath::IsFinite(In.RadiusCm / double(Init.VoxelSizeCm)))
	{
		return;
	}
	Interests.Add(In.InterestId, In);
	GenerateInterestedChunks();
}

void FMemoryTerrainBackend::ClearStreamingInterest(uint32 InterestId)
{
	Interests.Remove(InterestId);
}

void FMemoryTerrainBackend::FlushPendingWork()
{
	// All data operations complete before returning. No world or async work exists.
}

bool FMemoryTerrainBackend::QueryPoint(const FIntVector& Position, FTerrainPointSample& Out) const
{
	Out = FTerrainPointSample();
	const FTerrainChunkKey Key = KeyAt(Position);
	if (!bInitialized || !Init.WorldBoundsVox.Contains(Position) || !IsRegionResident(Key))
	{
		return false;
	}
	const int32 Index = IndexAt(Position);
	Out.Density = Normalise(Values.FindChecked(Key)[Index]);
	Out.MaterialId = Materials.FindChecked(Key)[Index];
	Out.bResident = true;
	return true;
}

bool FMemoryTerrainBackend::ReadRegion(const FTerrainChunkKey& Key, FTerrainRegionData& Out)
{
	Out = FTerrainRegionData();
	Out.Key = Key;
	if (!bInitialized)
	{
		return false;
	}
	Out.GeneratorVersion = Init.GeneratorVersion;
	if (!IsRegionResident(Key))
	{
		return true;
	}
	if (const FTerrainRegionData* Stored = Metadata.Find(Key))
	{
		Out.Rev = Stored->Rev;
		Out.LastOpSeq = Stored->LastOpSeq;
	}
	Out.Encoding = ETerrainRegionEncoding::Dense;
	Out.Payload.Reserve(MemorySampleCount * 4);
	for (int16 Value : Values.FindChecked(Key))
	{
		Append16(Out.Payload, static_cast<uint16>(Value));
	}
	for (FTerrainMatId Material : Materials.FindChecked(Key))
	{
		Append16(Out.Payload, Material);
	}
	return true;
}

bool FMemoryTerrainBackend::WriteRegion(const FTerrainRegionData& In)
{
	if (!bInitialized || !IsKeyInWorld(In.Key) || In.Encoding != ETerrainRegionEncoding::Dense
		|| In.Payload.Num() != MemorySampleCount * 4 || In.ValueConfig != 0
		|| In.GeneratorVersion != Init.GeneratorVersion)
	{
		return false;
	}
	TArray<int16> NewValues;
	TArray<FTerrainMatId> NewMaterials;
	NewValues.SetNumUninitialized(MemorySampleCount);
	NewMaterials.SetNumUninitialized(MemorySampleCount);
	for (int32 Index = 0; Index < MemorySampleCount; ++Index)
	{
		NewValues[Index] = static_cast<int16>(Read16(In.Payload, Index * 2));
		NewMaterials[Index] = Read16(In.Payload, MemorySampleCount * 2 + Index * 2);
	}
	Values.Add(In.Key, MoveTemp(NewValues));
	Materials.Add(In.Key, MoveTemp(NewMaterials));
	FTerrainRegionData& Stored = Metadata.FindOrAdd(In.Key);
	Stored.Key = In.Key;
	Stored.Rev = In.Rev;
	Stored.LastOpSeq = In.LastOpSeq;
	return true;
}

uint64 FMemoryTerrainBackend::HashRegion(const FTerrainChunkKey& Key) const
{
	if (!IsRegionResident(Key))
	{
		return 0;
	}
	const TArray<int16>& ChunkValues = Values.FindChecked(Key);
	const TArray<FTerrainMatId>& ChunkMaterials = Materials.FindChecked(Key);
	uint64 Hash = 0;
	for (int32 Index = 0; Index < MemorySampleCount; ++Index)
	{
		const uint64 Tuple = (uint64(Index) << 32)
			| (uint64(static_cast<uint16>(ChunkValues[Index])) << 16) | ChunkMaterials[Index];
		Hash += Mix(Tuple + 0x9e3779b97f4a7c15ULL);
	}
	return Hash;
}

bool FMemoryTerrainBackend::ApplyOp(const FTerrainOp& Op, FTerrainEditResult& Out)
{
	Out = FTerrainEditResult();
	if (!bInitialized || (Op.Kind != ETerrainOpKind::Remove && Op.Kind != ETerrainOpKind::Add
		&& Op.Kind != ETerrainOpKind::Paint)
		|| (Op.Shape != ETerrainShape::Sphere && Op.Shape != ETerrainShape::Box))
	{
		return false;
	}
	const double Radius = double(Op.RadiusVoxQ16) / 65536.0;
	if (Op.Shape == ETerrainShape::Sphere && Op.RadiusVoxQ16 <= 0)
	{
		return false;
	}
	FTerrainBox Bounds;
	int64 ScanCount = 1;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int64 Extent = Op.Shape == ETerrainShape::Sphere
			? int64(Op.RadiusVoxQ16 / 65536) : int64(Op.ExtentVox[Axis]);
		if (Op.Shape == ETerrainShape::Box && Extent <= 0)
		{
			return false;
		}
		const int64 Min = int64(Op.CentreVox[Axis]) - Extent;
		const int64 Max = int64(Op.CentreVox[Axis]) + Extent + (Op.Shape == ETerrainShape::Sphere ? 1 : 0);
		if (Min < Init.WorldBoundsVox.Min[Axis] || Max > Init.WorldBoundsVox.Max[Axis]
			|| Max - Min > MaxScans || ScanCount > MaxScans / (Max - Min))
		{
			return false;
		}
		ScanCount *= Max - Min;
		Bounds.Min[Axis] = static_cast<int32>(Min);
		Bounds.Max[Axis] = static_cast<int32>(Max);
	}

	// Validate and stage every change before writing any data: false is never partial.
	TArray<FTerrainPendingChange> Changes;
	TMap<FTerrainMatId, double> Volumes;
	const double Size = Init.VoxelSizeCm;
	const double UnitVolume = Size * Size * Size * 1000.0;
	for (int32 Z = Bounds.Min.Z; Z < Bounds.Max.Z; ++Z)
	for (int32 Y = Bounds.Min.Y; Y < Bounds.Max.Y; ++Y)
	for (int32 X = Bounds.Min.X; X < Bounds.Max.X; ++X)
	{
		const FIntVector Position(X, Y, Z);
		if (Op.Shape == ETerrainShape::Sphere)
		{
			const double DX = double(X) - Op.CentreVox.X;
			const double DY = double(Y) - Op.CentreVox.Y;
			const double DZ = double(Z) - Op.CentreVox.Z;
			if (DX * DX + DY * DY + DZ * DZ > Radius * Radius)
			{
				continue;
			}
		}
		const FTerrainChunkKey Key = KeyAt(Position);
		if (!IsRegionResident(Key))
		{
			return false;
		}
		const int32 Index = IndexAt(Position);
		const int16 OldValue = Values.FindChecked(Key)[Index];
		const FTerrainMatId OldMaterial = Materials.FindChecked(Key)[Index];
		const int16 NewValue = Op.Kind == ETerrainOpKind::Remove ? 32767
			: (Op.Kind == ETerrainOpKind::Add ? -32767 : OldValue);
		const FTerrainMatId NewMaterial = Op.Kind == ETerrainOpKind::Remove ? OldMaterial : Op.MaterialId;
		if (OldValue == NewValue && OldMaterial == NewMaterial)
		{
			continue;
		}
		if (Changes.Num() == MaxWrites)
		{
			return false;
		}
		Changes.Add({Position, NewValue, NewMaterial});
		const double Delta = Occupancy(OldValue) - Occupancy(NewValue);
		if (Delta != 0.0)
		{
			Volumes.FindOrAdd(Delta > 0.0 ? OldMaterial : NewMaterial) += Delta * UnitVolume;
		}
	}

	TSet<FTerrainChunkKey> Affected;
	for (const FTerrainPendingChange& Change : Changes)
	{
		const FTerrainChunkKey Key = KeyAt(Change.Position);
		const int32 Index = IndexAt(Change.Position);
		Values.FindChecked(Key)[Index] = Change.Value;
		Materials.FindChecked(Key)[Index] = Change.Material;
		Affected.Add(Key);
		if (Out.VoxelsTouched++ == 0)
		{
			Out.EditedBounds = FTerrainBox(Change.Position, Change.Position + FIntVector(1));
		}
		else
		{
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				Out.EditedBounds.Min[Axis] = FMath::Min(Out.EditedBounds.Min[Axis], Change.Position[Axis]);
				Out.EditedBounds.Max[Axis] = FMath::Max(Out.EditedBounds.Max[Axis], Change.Position[Axis] + 1);
			}
		}
	}
	Out.VoxelsScanned = ScanCount;
	Out.AffectedChunks = Affected.Array();
	Out.AffectedChunks.Sort([](const FTerrainChunkKey& A, const FTerrainChunkKey& B)
	{
		return A.X != B.X ? A.X < B.X : (A.Y != B.Y ? A.Y < B.Y : A.Z < B.Z);
	});
	for (const auto& Entry : Volumes)
	{
		const int64 Rounded = FMath::RoundToInt64(Entry.Value);
		if (Rounded != 0)
		{
			Out.Removed.Add({Entry.Key, Rounded});
		}
	}
	Out.Removed.Sort([](const FTerrainMaterialVolume& A, const FTerrainMaterialVolume& B)
	{
		return A.MaterialId < B.MaterialId;
	});
	return true;
}

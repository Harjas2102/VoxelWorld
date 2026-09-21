// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "VPLegacyBackend.h"
#include "TerrainBackendVPLegacy.h"
#include "TerrainChunk.h"
#include "TerrainOpGeometry.h"
#include "VoxelTools/Impl/VoxelSphereToolsImpl.inl"
#include "VoxelTools/VoxelToolHelpers.h"
#include "VPLegacyDensityGenerator.h"

// The plugin. These four includes are the entire reason this module exists as a module.
#include "VoxelWorld.h"
#include "VoxelComponents/VoxelInvokerComponent.h"
#include "VoxelTools/Gen/VoxelSphereTools.h"
#include "VoxelTools/VoxelDataTools.h"
#include "VoxelData/VoxelDataAccelerator.h"
#include "VoxelData/VoxelDataLock.h"
#include "VoxelTools/VoxelBlueprintLibrary.h"

#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	/** ARCHITECTURE.md §7.1. The service bounds requests too; this is the backend's own floor. */
	constexpr int64 VPLegacyMaxWrites = 65536;

	/** §2.3: FVoxelValue is int16 normalised density, EIGHT_BITS_VOXEL_VALUE = 0. */
	constexpr int16 VPLegacyValueScale = 32767;

	/** §4.7 dense payload: LE int16[N] values, then LE uint16[N] materials. ValueConfig 0 = int16. */
	constexpr uint8 VPLegacyValueConfig = 0;

	void Append16(TArray<uint8>& Bytes, uint16 Value)
	{
		Bytes.Add(static_cast<uint8>(Value));
		Bytes.Add(static_cast<uint8>(Value >> 8));
	}

	uint16 Read16(const TArray<uint8>& Bytes, int32 Offset)
	{
		return static_cast<uint16>(Bytes[Offset]) | (static_cast<uint16>(Bytes[Offset + 1]) << 8);
	}

	int16 QuantiseDensity(float Value)
	{
		if (!FMath::IsFinite(Value))
		{
			return VPLegacyValueScale;
		}
		return static_cast<int16>(FMath::Clamp(FMath::RoundToInt(Value * VPLegacyValueScale),
			-static_cast<int32>(VPLegacyValueScale), static_cast<int32>(VPLegacyValueScale)));
	}

	/** Local sample index inside a chunk: x + 32*y + 1024*z, matching FMemoryTerrainBackend. */
	int32 LocalIndex(int32 X, int32 Y, int32 Z)
	{
		return X + TerrainChunkSizeVox * Y + TerrainChunkSizeVox * TerrainChunkSizeVox * Z;
	}

	/** Avalanche then commutative sum: order-independent, position-sensitive (DEF-5). */
	uint64 Mix(uint64 V)
	{
		V = (V ^ (V >> 30)) * 0xbf58476d1ce4e5b9ULL;
		V = (V ^ (V >> 27)) * 0x94d049bb133111ebULL;
		return V ^ (V >> 31);
	}
}

FVPLegacyBackend::~FVPLegacyBackend()
{
	// Shutdown is idempotent, and a backend destroyed without one would leave invoker
	// components alive on an actor the game still owns.
	Shutdown();
}

bool FVPLegacyBackend::Initialize(const FTerrainBackendInit& InInit)
{
	if (!IsInGameThread())
	{
		UE_LOG(LogTerrainBackendVPLegacy, Error, TEXT("Initialize called off the game thread. Refused (DEF-4)."));
		return false;
	}
	if (bInitialized)
	{
		// Matching FMemoryTerrainBackend: initialising a running backend fails without
		// resetting anything, rather than silently rebuilding the world under the service.
		UE_LOG(LogTerrainBackendVPLegacy, Error, TEXT("Initialize called on a running backend. Refused."));
		return false;
	}
	if (!InInit.World
		|| (InInit.DensityField && InInit.DensityFieldOwner.Get() != InInit.DensityField)
		|| !(InInit.VoxelSizeCm > 0.f) || !FMath::IsFinite(InInit.VoxelSizeCm)
		|| InInit.WorldBoundsVox.IsEmpty()
		|| (InInit.Role != ETerrainRole::Server && InInit.Role != ETerrainRole::Client))
	{
		UE_LOG(LogTerrainBackendVPLegacy, Error,
			TEXT("Initialize rejected: needs a UWorld, a positive finite voxel size, a non-empty "
				 "world box and a valid role."));
		return false;
	}

	Init = InInit;
	World = InInit.World;

	AVoxelWorld* Actor = AcquireVoxelWorld();
	if (!Actor)
	{
		World.Reset();
		Init = FTerrainBackendInit();
		return false;
	}
	if (!ConformVoxelWorld(*Actor))
	{
		VoxelWorld.Reset();
		World.Reset();
		Init = FTerrainBackendInit();
		return false;
	}

	bInitialized = true;

	UE_LOG(LogTerrainBackendVPLegacy, Log,
		TEXT("FVPLegacyBackend ready on '%s': voxel %.1f cm, world %d voxels, created=%s, role=%s."),
		*Actor->GetName(), Actor->VoxelSize, Actor->WorldSizeInVoxel,
		Actor->IsCreated() ? TEXT("yes") : TEXT("no"),
		Init.Role == ETerrainRole::Server ? TEXT("Server") : TEXT("Client"));

	// The density field is the game's (§4.6, T-108). Until an implementer exists the actor
	// keeps whatever generator it was authored with — VoxelFlatGenerator, per T-101A — which
	// is why the test world is a plane and not the hill. Say so once, at startup, rather than
	// letting someone rediscover it from an empty-looking world.
	if (Init.DensityField)
	{
		// The material config is logged rather than forced: the strata colours below only
		// become visible in an RGB config with a vertex-colour material, and which of those
		// the level's actor carries is an authored asset choice, not the backend's call.
		UE_LOG(LogTerrainBackendVPLegacy, Log,
			TEXT("World shape comes from the game density field (T-108). Material config: %d "
				 "(strata colours are visible in RGB only)."),
			static_cast<int32>(Actor->MaterialConfig));
	}
	else
	{
		UE_LOG(LogTerrainBackendVPLegacy, Warning,
			TEXT("No ITerrainDensityField supplied; the actor's own generator decides the world's shape. "
				 "Since T-108 the service always supplies one, so this means it failed to."));
	}

	return true;
}

AVoxelWorld* FVPLegacyBackend::AcquireVoxelWorld()
{
	UWorld* LiveWorld = World.Get();
	if (!LiveWorld)
	{
		return nullptr;
	}

	AVoxelWorld* Found = nullptr;
	int32 Count = 0;
	for (TActorIterator<AVoxelWorld> It(LiveWorld); It; ++It)
	{
		if (IsValid(*It))
		{
			++Count;
			if (!Found)
			{
				Found = *It;
			}
		}
	}

	if (Count > 1)
	{
		// place_voxel_world.py deletes duplicates for the same reason: overlapping voxel
		// worlds make every result ambiguous. Refuse rather than pick one silently.
		UE_LOG(LogTerrainBackendVPLegacy, Error,
			TEXT("%d AVoxelWorld actors in this level. Overlapping voxel worlds make terrain results "
				 "ambiguous; leave exactly one."), Count);
		return nullptr;
	}

	if (Found)
	{
		VoxelWorld = Found;
		bSpawnedVoxelWorld = false;
		return Found;
	}

	FActorSpawnParameters Params;
	Params.Name = TEXT("TerrainBackendVoxelWorld");
	Params.ObjectFlags |= RF_Transient;
	AVoxelWorld* Spawned = LiveWorld->SpawnActor<AVoxelWorld>(
		AVoxelWorld::StaticClass(), Init.OriginTransform, Params);
	if (!Spawned)
	{
		UE_LOG(LogTerrainBackendVPLegacy, Error, TEXT("No AVoxelWorld in the level and spawning one failed."));
		return nullptr;
	}

	UE_LOG(LogTerrainBackendVPLegacy, Warning,
		TEXT("No AVoxelWorld in the level; spawned a transient one at the terrain origin. It carries no "
			 "authored generator or material, so the world will be whatever the plugin defaults to."));
	VoxelWorld = Spawned;
	bSpawnedVoxelWorld = true;
	return Spawned;
}

bool FVPLegacyBackend::ConformVoxelWorld(AVoxelWorld& Actor)
{
	// THE GAME OWNS THE GRID (§8.1, AR-5). Everything below forces the actor onto the origin,
	// voxel size and extent the service quantises against. A mismatch here would not look like
	// a bug: it would look like terrain that is subtly in the wrong place, one voxel at a time.
	bool bNeedsRecreate = false;

	const FTransform Current = Actor.GetActorTransform();
	if (!Current.Equals(Init.OriginTransform, UE_KINDA_SMALL_NUMBER))
	{
		UE_LOG(LogTerrainBackendVPLegacy, Warning,
			TEXT("Voxel world transform did not match the configured terrain origin; moving it. "
				 "Was %s, now %s."), *Current.ToString(), *Init.OriginTransform.ToString());
		Actor.SetActorTransform(Init.OriginTransform);
	}

	if (!FMath::IsNearlyEqual(Actor.VoxelSize, Init.VoxelSizeCm))
	{
		UE_LOG(LogTerrainBackendVPLegacy, Warning,
			TEXT("Voxel size mismatch: actor %.3f cm, TerrainSettings %.3f cm. Using the configured value. "
				 "Changing voxel size on a world that already holds edits is a resample migration (FM-9), "
				 "not a tuning change."), Actor.VoxelSize, Init.VoxelSizeCm);
		Actor.VoxelSize = Init.VoxelSizeCm;
		bNeedsRecreate = true;
	}

	const FIntVector Extent = Init.WorldBoundsVox.Max - Init.WorldBoundsVox.Min;
	const int32 DesiredSize = FMath::Max3(Extent.X, Extent.Y, Extent.Z);
	if (static_cast<int32>(Actor.WorldSizeInVoxel) < DesiredSize)
	{
		UE_LOG(LogTerrainBackendVPLegacy, Warning,
			TEXT("Voxel world is %u voxels across but TerrainSettings asks for %d; enlarging it."),
			Actor.WorldSizeInVoxel, DesiredSize);
		Actor.SetWorldSize(DesiredSize);
		bNeedsRecreate = true;
	}

	// THE GAME OWNS THE WORLD'S SHAPE TOO (T-108, §4.6). Installing the generator has to
	// happen BEFORE CreateWorld, and a change to it has to force a recreate, because the
	// generator is the baseline every unedited voxel is read from: swapping it under a live
	// world would leave already-meshed chunks showing the old shape next to new ones showing
	// the new, with no error anywhere.
	if (Init.DensityField)
	{
		if (!Generator.IsValid())
		{
			Generator.Reset(NewObject<UVPLegacyDensityGenerator>(GetTransientPackage()));
		}
		Generator->SetField(Init.DensityField, Init.DensityFieldOwner);

		if (Actor.Generator.GetObject() != Generator.Get())
		{
			UE_LOG(LogTerrainBackendVPLegacy, Log,
				TEXT("Installing the game density field as the voxel world generator (was '%s')."),
				Actor.Generator.GetObject() ? *Actor.Generator.GetObject()->GetName() : TEXT("none"));
			Actor.SetGeneratorObject(Generator.Get());
			bNeedsRecreate = true;
		}
	}

	if (!Actor.IsCreated())
	{
		Actor.CreateWorld();
	}
	else if (bNeedsRecreate)
	{
		// bSaveData true: recreating is a conform step, not a reset. Discarding edits here
		// would silently destroy a player's excavation on a config typo.
		UVoxelBlueprintLibrary::Recreate(&Actor, /*bSaveData*/ true);
	}

	if (!Actor.IsCreated())
	{
		UE_LOG(LogTerrainBackendVPLegacy, Error, TEXT("Voxel world '%s' would not create."), *Actor.GetName());
		return false;
	}
	return true;
}

void FVPLegacyBackend::Shutdown()
{
	if (!IsInGameThread())
	{
		// Refusing is the safe answer: tearing down invoker components and an actor from a
		// worker thread is exactly the lifetime hazard DEF-4 names.
		UE_LOG(LogTerrainBackendVPLegacy, Error, TEXT("Shutdown called off the game thread. Refused (DEF-4)."));
		return;
	}

	for (TPair<uint32, TWeakObjectPtr<UVoxelSimpleInvokerComponent>>& Pair : Invokers)
	{
		if (UVoxelSimpleInvokerComponent* Invoker = Pair.Value.Get())
		{
			Invoker->DestroyComponent();
		}
	}
	Invokers.Empty();

	if (bSpawnedVoxelWorld)
	{
		// Only ever destroy an actor this backend created. A level-authored voxel world
		// outlives the backend; a backend swap must not delete the designer's actor.
		if (AVoxelWorld* Actor = VoxelWorld.Get())
		{
			Actor->Destroy();
		}
	}
	bSpawnedVoxelWorld = false;

	ScratchModified.Empty();
	// Released before the borrowed field goes away with the service. The generator holds a
	// raw pointer to that field, so an instance still meshing after this point would be
	// reading freed memory (AR-2: the field is borrowed until Shutdown).
	if (Generator.IsValid()) Generator->SetField(nullptr);
	Generator.Reset();
	VoxelWorld.Reset();
	World.Reset();
	Init = FTerrainBackendInit();
	bInitialized = false;
}

AVoxelWorld* FVPLegacyBackend::GetLiveVoxelWorld() const
{
	if (!bInitialized)
	{
		return nullptr;
	}
	AVoxelWorld* Actor = VoxelWorld.Get();
	return (Actor && IsValid(Actor) && Actor->IsCreated()) ? Actor : nullptr;
}

bool FVPLegacyBackend::IsBoxInWorld(const FTerrainBox& Box) const
{
	const FTerrainBox& Bounds = Init.WorldBoundsVox;
	return !Box.IsEmpty()
		&& Box.Min.X >= Bounds.Min.X && Box.Max.X <= Bounds.Max.X
		&& Box.Min.Y >= Bounds.Min.Y && Box.Max.Y <= Bounds.Max.Y
		&& Box.Min.Z >= Bounds.Min.Z && Box.Max.Z <= Bounds.Max.Z;
}

namespace
{
	// The plugin supplies density values; this view enforces the game's exact W/B.
	// The only lock is the plugin's own documented data lock, matching its generated tools.
	struct FCanonicalSphereData
	{
		TVoxelDataImpl<FModifiedVoxelValue>& Data;
		const FTerrainOp& Op;
		FVoxelIntBox Bounds;
		template<typename T, typename F> void Set(const FVoxelIntBox&, F Edit)
		{
			Data.template Set<T>(Bounds, [&](int32 X, int32 Y, int32 Z, T& Value)
			{
				if (!TerrainOpContains(Op,FIntVector(X,Y,Z))) return;
                // Fully contained half-open cells must reach empty/full; the plugin's
                // two-voxel edge ramp alone does not satisfy that contract.
                const double Radius = double(Op.RadiusVoxQ16)/65536.;
                double FarthestSquared = 0;
                const FIntVector P(X,Y,Z);
                for (int32 A=0; A<3; ++A)
                {
                    const double D = double(P[A])-Op.CentreVox[A];
                    FarthestSquared += FMath::Max(D*D,(D+1)*(D+1));
                }
                if (FarthestSquared<=Radius*Radius)
                    Value = Op.Kind==ETerrainOpKind::Remove ? T::Empty() : T::Full();
                else Edit(X,Y,Z,Value);
			});
		}
	};
}

bool FVPLegacyBackend::ApplyOp(const FTerrainOp& Op, FTerrainEditResult& Out)
{
	Out = {};
	if (!IsInGameThread()) return false;
	AVoxelWorld* Actor = GetLiveVoxelWorld();
	FTerrainBox Footprint;
	int64 Writes=0, Scans=0;
	if (!Actor || (Op.Kind != ETerrainOpKind::Remove && Op.Kind != ETerrainOpKind::Add)
		|| !TerrainOpBounds(Op,Footprint) || !IsBoxInWorld(Footprint)
		|| !TerrainOpCounts(Op,VPLegacyMaxWrites,Writes,Scans)) return false;
	TArray<FTerrainChunkKey> Keys;
	if (!TerrainChunkKeysForBox(Footprint,Keys)) return false;
	for (const auto& Key : Keys) if (!IsRegionResident(Key)) return false;
	const FVoxelIntBox Bounds(Footprint.Min,Footprint.Max);
	ScratchModified.Reset();
	{
		auto& WorldData = Actor->GetData();
		FVoxelWriteScopeLock Lock(WorldData,Bounds,FUNCTION_FNAME);
		auto Data = TVoxelDataImpl<FModifiedVoxelValue>(WorldData,false,true);
		if (Op.Shape == ETerrainShape::Sphere)
		{
			FCanonicalSphereData Clipped{Data,Op,Bounds};
			const FVoxelVector Position(Op.CentreVox);
			const float Radius = float(double(Op.RadiusVoxQ16)/65536.0);
			if (Op.Kind == ETerrainOpKind::Remove) FVoxelSphereToolsImpl::RemoveSphere(Clipped,Position,Radius);
			else FVoxelSphereToolsImpl::AddSphere(Clipped,Position,Radius);
		}
		else
		{
			Data.Set<FVoxelValue>(Bounds,[&](int32,int32,int32,FVoxelValue& Value)
			{ Value = Op.Kind == ETerrainOpKind::Remove ? FVoxelValue::Empty() : FVoxelValue::Full(); });
		}
		ScratchModified = MoveTemp(Data.ModifiedValues);
	}
	TSet<FTerrainChunkKey> Affected;
	for (const FModifiedVoxelValue& Modified : ScratchModified)
	{
		const FIntVector P = Modified.Position;
		Affected.Add(TerrainChunkKeyForVoxel(P));
		if (Out.VoxelsTouched++ == 0) Out.EditedBounds = FTerrainBox(P,P+FIntVector(1));
		else for (int32 A=0;A<3;++A)
		{
			Out.EditedBounds.Min[A]=FMath::Min(Out.EditedBounds.Min[A],P[A]);
			Out.EditedBounds.Max[A]=FMath::Max(Out.EditedBounds.Max[A],P[A]+1);
		}
	}
	Out.VoxelsScanned=Scans;
	Out.AffectedChunks=Affected.Array();
	Out.AffectedChunks.Sort([](const FTerrainChunkKey& A,const FTerrainChunkKey& B)
	{ return A.X!=B.X ? A.X<B.X : A.Y!=B.Y ? A.Y<B.Y : A.Z<B.Z; });
	if (Out.VoxelsTouched) FVoxelToolHelpers::UpdateWorld(Actor,Bounds);
	return true;
}

bool FVPLegacyBackend::IsRegionResident(const FTerrainChunkKey& Key) const
{
	if (!IsInGameThread() || !GetLiveVoxelWorld())
	{
		return false;
	}
	// The plugin generates on demand inside its own bounds, so "resident" here means the
	// chunk is addressable and will answer a read — not that anything is cached for it.
	return IsBoxInWorld(TerrainChunkBounds(Key));
}

void FVPLegacyBackend::FlushPendingWork()
{
	if (!IsInGameThread()) return;
	// Intentionally empty. §4.5: "Rendering and collision updates remain the plugin's own
	// async work and are explicitly not serialised by us. This is the seam where the
	// fall-through-the-floor bug lives: the data edit completes before the collision cook
	// does." The plugin exposes no synchronous drain, and a busy-wait here would convert an
	// honest asynchronous seam into a hidden hitch. DEF-8 owns the real answer.
}

bool FVPLegacyBackend::QueryPoint(const FIntVector& VoxelPos, FTerrainPointSample& Out) const
{
	Out = FTerrainPointSample();

	if (!IsInGameThread())
	{
		return false;
	}
	AVoxelWorld* Actor = GetLiveVoxelWorld();
	if (!Actor)
	{
		return false;
	}

	if (!Init.WorldBoundsVox.Contains(VoxelPos))
	{
		// bResident false, and Density/MaterialId meaningless — §4.3: "a caller that ignores
		// bResident reads a solid world as empty at streaming boundaries."
		return true;
	}

	float Value = 0.f;
	UVoxelDataTools::GetValue(Value, Actor, VoxelPos);

	Out.Density = FMath::Clamp(Value, -1.f, 1.f);
	Out.MaterialId = 0;   // K9, build step 6. Zero is "unknown", not "air".
	Out.bResident = true;
	return true;
}

bool FVPLegacyBackend::ReadRegion(const FTerrainChunkKey& Key, FTerrainRegionData& Out)
{
	Out = FTerrainRegionData();
	Out.Key = Key;
	if (!IsInGameThread()) return false;
	Out.GeneratorVersion = Init.GeneratorVersion;
	Out.ValueConfig = VPLegacyValueConfig;

	AVoxelWorld* Actor = IsInGameThread() ? GetLiveVoxelWorld() : nullptr;
	if (!Actor || !IsRegionResident(Key))
	{
		Out.Encoding = ETerrainRegionEncoding::Empty;
		return false;
	}

	const FTerrainBox ChunkBox = TerrainChunkBounds(Key);
	Out.Encoding = ETerrainRegionEncoding::Dense;

	// Written in place rather than appended. Two Add() calls per sample is 65,536 of them per
	// chunk, and the bounds check and capacity test on each are pure overhead when the exact
	// size is known before the first byte.
	Out.Payload.SetNumUninitialized(TerrainChunkSampleCount * 4);
	uint8* const Density  = Out.Payload.GetData();
	uint8* const Material = Density + TerrainChunkSampleCount * 2;

	// Materials are K9 / build step 6 and are not read back yet, so the upper half is zero. It
	// is zeroed once here instead of through 32,768 append calls.
	FMemory::Memzero(Material, TerrainChunkSampleCount * 2);

	{
		// ONE lock and ONE accelerator for the whole chunk.
		//
		// This used to call UVoxelDataTools::GetValue per voxel: 32,768 separate lock
		// acquisitions, each followed by a full octree traversal from the root. P-003 §4 named
		// that path as an acceptance gate before it was ever measured -- "the current
		// 32,768-per-voxel-call adapter path must gain a measured bulk-read implementation
		// before production integration" -- and measuring it is what made checkpoint capture
		// unaffordable: 42 ms per chunk solo and 86 ms under three-client load.
		//
		// FVoxelConstDataAccelerator is what the plugin's own bulk reader uses. Built over the
		// chunk's bounds, it caches the octree node between lookups, so walking a chunk in
		// index order costs one traversal and then near-constant-time neighbours.
		auto& WorldData = Actor->GetData();
		const FVoxelIntBox Bounds(ChunkBox.Min, ChunkBox.Max);
		FVoxelReadScopeLock Lock(WorldData, Bounds, FUNCTION_FNAME);
		const FVoxelConstDataAccelerator Accelerator(WorldData, Bounds);

		// Local index x + 32y + 1024z (§4.2), so X is the innermost loop and consecutive
		// samples are adjacent in voxel space -- which is what the accelerator's cache rewards.
		int32 Index = 0;
		for (int32 Z = 0; Z < TerrainChunkSizeVox; ++Z)
		for (int32 Y = 0; Y < TerrainChunkSizeVox; ++Y)
		for (int32 X = 0; X < TerrainChunkSizeVox; ++X, ++Index)
		{
			const float Value = Accelerator.GetValue(
				ChunkBox.Min.X + X, ChunkBox.Min.Y + Y, ChunkBox.Min.Z + Z, 0).ToFloat();
			const uint16 Quantised = static_cast<uint16>(QuantiseDensity(Value));
			Density[Index * 2]     = static_cast<uint8>( Quantised       & 0xFFu);
			Density[Index * 2 + 1] = static_cast<uint8>((Quantised >> 8) & 0xFFu);
		}
	}

	return true;
}

bool FVPLegacyBackend::WriteRegion(const FTerrainRegionData& In)
{
	if (!IsInGameThread())
	{
		return false;
	}
	AVoxelWorld* Actor = GetLiveVoxelWorld();
	if (!Actor || !IsRegionResident(In.Key))
	{
		return false;
	}
	if (In.Encoding != ETerrainRegionEncoding::Dense)
	{
		// SparseDiff needs a generator baseline to diff against and Empty needs one to restore
		// from, and no ITerrainDensityField exists until T-108. FMemoryTerrainBackend refuses
		// them at step 1 for the same reason.
		UE_LOG(LogTerrainBackendVPLegacy, Warning, TEXT("Only Dense regions can be written at build step 2."));
		return false;
	}
	if (In.ValueConfig != VPLegacyValueConfig)
	{
		// §2.3: "our snapshot header records it too, or an 8-bit rebuild silently corrupts
		// old saves." Refusing an unknown config is the whole point of recording it.
		UE_LOG(LogTerrainBackendVPLegacy, Error, TEXT("Region value config %u is not int16; refusing to write it."),
			In.ValueConfig);
		return false;
	}
	if (In.Payload.Num() != TerrainChunkSampleCount * 4)
	{
		return false;
	}

	// ONE write lock, ONE accelerator and ONE render update for the whole chunk -- the write
	// twin of ReadRegion's T-120 fix. UVoxelDataTools::SetValue per voxel took a lock, walked
	// the octree from the root and queued a remesh of the voxel's bounds, 32,768 times: a
	// measured 65-165 ms per chunk on a client installing a join-in-progress snapshot (P-008).
	// Same value conversion as before (FVoxelValue from the same float), so the stored samples
	// are unchanged; only the number of locks and remesh requests is.
	const FTerrainBox Bounds = TerrainChunkBounds(In.Key);
	const FVoxelIntBox VoxelBounds(Bounds.Min, Bounds.Max);
	{
		auto& WorldData = Actor->GetData();
		FVoxelWriteScopeLock Lock(WorldData, VoxelBounds, FUNCTION_FNAME);
		FVoxelMutableDataAccelerator Accelerator(WorldData, VoxelBounds);
		for (int32 Z = 0; Z < TerrainChunkSizeVox; ++Z)
		for (int32 Y = 0; Y < TerrainChunkSizeVox; ++Y)
		for (int32 X = 0; X < TerrainChunkSizeVox; ++X)
		{
			const int16 Stored = static_cast<int16>(Read16(In.Payload, LocalIndex(X, Y, Z) * 2));
			Accelerator.SetValue(Bounds.Min.X + X, Bounds.Min.Y + Y, Bounds.Min.Z + Z,
				FVoxelValue(static_cast<float>(Stored) / VPLegacyValueScale));
		}
	}
	FVoxelToolHelpers::UpdateWorld(Actor, VoxelBounds);

	// Materials in the payload are ignored: nothing writes non-zero ones yet (K9, step 6).
	return true;
}

uint64 FVPLegacyBackend::HashRegion(const FTerrainChunkKey& Key) const
{
	AVoxelWorld* Actor = IsInGameThread() ? GetLiveVoxelWorld() : nullptr;
	if (!Actor || !IsRegionResident(Key))
	{
		return 0;
	}

	// Position-sensitive, traversal-order independent (DEF-5): the local index is mixed INTO
	// each sample's hash, so rearranged terrain cannot hash the same, and the combine is a
	// plain sum, so the answer cannot depend on the order the samples were visited.
	const FTerrainBox Bounds = TerrainChunkBounds(Key);
	uint64 Hash = 0;
	for (int32 Z = 0; Z < TerrainChunkSizeVox; ++Z)
	for (int32 Y = 0; Y < TerrainChunkSizeVox; ++Y)
	for (int32 X = 0; X < TerrainChunkSizeVox; ++X)
	{
		float Value = 0.f;
		UVoxelDataTools::GetValue(Value, Actor, FIntVector(Bounds.Min.X + X, Bounds.Min.Y + Y, Bounds.Min.Z + Z));
		const uint64 Sample = (static_cast<uint64>(LocalIndex(X, Y, Z)) << 32)
			^ static_cast<uint64>(static_cast<uint16>(QuantiseDensity(Value)));
		Hash += Mix(Sample);
	}
	return Hash;
}

void FVPLegacyBackend::SetStreamingInterest(const FTerrainStreamingInterest& In)
{
	if (!IsInGameThread() || In.InterestId == 0)
	{
		return;
	}
	AVoxelWorld* Actor = VoxelWorld.Get();
	if (!bInitialized || !Actor || !IsValid(Actor))
	{
		return;
	}

	// §7.4: "FVPLegacyBackend creates, moves and destroys the plugin invoker internally."
	// The component is owned by the VOXEL WORLD actor, not by the gameplay actor that asked
	// for the interest — which is what keeps the plugin type out of every character, in code
	// and in every asset (DEF-10). UVoxelInvokerComponentBase::IsLocalInvoker defaults to
	// true for a non-pawn owner, which is what we want here: the service already decided
	// whose interest this is.
	TWeakObjectPtr<UVoxelSimpleInvokerComponent>& Slot = Invokers.FindOrAdd(In.InterestId);
	UVoxelSimpleInvokerComponent* Invoker = Slot.Get();
	if (!Invoker)
	{
		Invoker = NewObject<UVoxelSimpleInvokerComponent>(Actor, NAME_None, RF_Transient);
		if (!Invoker)
		{
			Invokers.Remove(In.InterestId);
			return;
		}
		Invoker->SetupAttachment(Actor->GetRootComponent());
		Invoker->RegisterComponent();
		Slot = Invoker;
	}

	const float Range = static_cast<float>(FMath::Max(0.0, In.RadiusCm));

	// bRender maps onto LOD: a dedicated server asks for collision without paying for the
	// mesh resolution nobody looks at (§4.2, §7.4). bCollision maps onto collisions and
	// navmesh, which are the two things a server actually needs terrain resident for.
	Invoker->bUseForLOD = In.bRender;
	Invoker->LODToSet = 0;
	Invoker->LODRange = Range;
	Invoker->bUseForCollisions = In.bCollision;
	Invoker->CollisionsRange = Range;
	Invoker->bUseForNavmesh = In.bCollision;
	Invoker->NavmeshRange = Range;

	Invoker->SetWorldLocation(In.WorldLocation);

	if (!Invoker->IsInvokerEnabled())
	{
		Invoker->EnableInvoker();
	}
	// The plugin caches invoker settings; a changed range or flag needs an explicit refresh.
	UVoxelInvokerComponentBase::RefreshAllVoxelInvokers();
}

void FVPLegacyBackend::ClearStreamingInterest(uint32 InterestId)
{
	if (!IsInGameThread() || InterestId == 0)
	{
		return;
	}
	TWeakObjectPtr<UVoxelSimpleInvokerComponent> Slot;
	if (!Invokers.RemoveAndCopyValue(InterestId, Slot))
	{
		return;
	}
	if (UVoxelSimpleInvokerComponent* Invoker = Slot.Get())
	{
		Invoker->DisableInvoker();
		Invoker->DestroyComponent();
	}
	UVoxelInvokerComponentBase::RefreshAllVoxelInvokers();
}

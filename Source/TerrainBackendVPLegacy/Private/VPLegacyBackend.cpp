// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "VPLegacyBackend.h"
#include "TerrainBackendVPLegacy.h"
#include "TerrainChunk.h"

// The plugin. These four includes are the entire reason this module exists as a module.
#include "VoxelWorld.h"
#include "VoxelComponents/VoxelInvokerComponent.h"
#include "VoxelTools/Gen/VoxelSphereTools.h"
#include "VoxelTools/VoxelDataTools.h"
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
	if (!Init.DensityField)
	{
		UE_LOG(LogTerrainBackendVPLegacy, Log,
			TEXT("No ITerrainDensityField supplied; the actor's own generator decides the world's shape "
				 "until T-108 (build step 8)."));
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

bool FVPLegacyBackend::ApplyOp(const FTerrainOp& Op, FTerrainEditResult& Out)
{
	Out = FTerrainEditResult();

	if (!IsInGameThread())
	{
		UE_LOG(LogTerrainBackendVPLegacy, Error, TEXT("ApplyOp called off the game thread. Refused (DEF-4)."));
		return false;
	}
	AVoxelWorld* Actor = GetLiveVoxelWorld();
	if (!Actor)
	{
		return false;
	}

	// Shape and kind. An operation this backend cannot perform exactly is refused, never
	// approximated with a nearby one: DEF-5 is open precisely because "close enough" terrain
	// semantics are how server and client stop agreeing.
	if (Op.Shape != ETerrainShape::Sphere)
	{
		UE_LOG(LogTerrainBackendVPLegacy, Warning, TEXT("Box operations are not implemented at build step 2."));
		return false;
	}
	if (Op.Kind != ETerrainOpKind::Remove && Op.Kind != ETerrainOpKind::Add)
	{
		UE_LOG(LogTerrainBackendVPLegacy, Warning,
			TEXT("Operation kind %u is unsupported: Flatten and Smooth have no ruled semantics (DEF-5) and "
				 "Paint has no game-id-to-plugin-index table until K9 at build step 6."),
			static_cast<uint32>(Op.Kind));
		return false;
	}
	if (Op.RadiusVoxQ16 <= 0)
	{
		return false;
	}

	const double RadiusVox = static_cast<double>(Op.RadiusVoxQ16) / 65536.0;
	const int32 RadiusCeil = FMath::CeilToInt(RadiusVox);
	const FTerrainBox Footprint(Op.CentreVox - FIntVector(RadiusCeil), Op.CentreVox + FIntVector(RadiusCeil + 1));
	if (!IsBoxInWorld(Footprint))
	{
		return false;
	}

	// Own bound, independent of the service's. §7.1 caps a single op at 65,536 written voxels
	// and splitting belongs to the service (step 3, DEF-7) — never to this backend.
	const double Estimate = (4.0 / 3.0) * PI * FMath::Pow(RadiusVox + 0.5, 3.0);
	if (Estimate > static_cast<double>(VPLegacyMaxWrites))
	{
		UE_LOG(LogTerrainBackendVPLegacy, Warning,
			TEXT("Operation of radius %.2f voxels is above MaxVoxelsPerOp; the service splits, this does not."),
			RadiusVox);
		return false;
	}

	// §4.3: the C++ overload APPENDS. Reset explicitly, every op.
	ScratchModified.Reset();

	FVoxelIntBox EditedBounds;
	// bConvertToVoxelSpace = FALSE. The server already quantised, once (§4.3): handing the
	// plugin a world-space float here would reintroduce exactly the per-machine rounding
	// divergence the integer wire op exists to remove.
	const FVector PositionVox(Op.CentreVox.X, Op.CentreVox.Y, Op.CentreVox.Z);
	const float RadiusVoxFloat = static_cast<float>(RadiusVox);

	if (Op.Kind == ETerrainOpKind::Remove)
	{
		UVoxelSphereTools::RemoveSphere(Actor, PositionVox, RadiusVoxFloat,
			&ScratchModified, &EditedBounds,
			/*bMultiThreaded*/ false, /*bConvertToVoxelSpace*/ false, /*bUpdateRender*/ true);
	}
	else
	{
		UVoxelSphereTools::AddSphere(Actor, PositionVox, RadiusVoxFloat,
			&ScratchModified, &EditedBounds,
			/*bMultiThreaded*/ false, /*bConvertToVoxelSpace*/ false, /*bUpdateRender*/ true);
	}

	const int64 Touched = ScratchModified.Num();

	FTerrainBox Edited(EditedBounds.Min, EditedBounds.Max);
	if (Touched > 0 && Edited.IsEmpty())
	{
		// A degenerate bounds with real modifications would leave the chunks that changed
		// without a revision bump. Fall back to the op's own footprint, which is a superset.
		Edited = Footprint;
	}

	Out.EditedBounds = Edited;
	Out.VoxelsTouched = Touched;
	Out.VoxelsScanned = static_cast<int64>(Footprint.Max.X - Footprint.Min.X)
		* (Footprint.Max.Y - Footprint.Min.Y)
		* (Footprint.Max.Z - Footprint.Min.Z);
	Out.bTruncated = false;

	if (!Edited.IsEmpty() && !TerrainChunkKeysForBox(Edited, Out.AffectedChunks))
	{
		UE_LOG(LogTerrainBackendVPLegacy, Error, TEXT("Edited bounds enumerate too many chunks; refusing to report them."));
		return false;
	}

	// Out.Removed stays empty. See the class comment: yield needs a separate material read
	// (§2.3 — FModifiedVoxelValue carries no material) and the K9 id table, both build step 6.

	// Free the scratch when an op was unusually large, so one big edit does not hold a
	// multi-megabyte array for the rest of the session.
	if (ScratchModified.Num() > 4096)
	{
		ScratchModified.Empty();
	}

	return true;
}

bool FVPLegacyBackend::IsRegionResident(const FTerrainChunkKey& Key) const
{
	if (!GetLiveVoxelWorld())
	{
		return false;
	}
	// The plugin generates on demand inside its own bounds, so "resident" here means the
	// chunk is addressable and will answer a read — not that anything is cached for it.
	return IsBoxInWorld(TerrainChunkBounds(Key));
}

void FVPLegacyBackend::FlushPendingWork()
{
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
	Out.GeneratorVersion = Init.GeneratorVersion;
	Out.ValueConfig = VPLegacyValueConfig;

	AVoxelWorld* Actor = IsInGameThread() ? GetLiveVoxelWorld() : nullptr;
	if (!Actor || !IsRegionResident(Key))
	{
		Out.Encoding = ETerrainRegionEncoding::Empty;
		return false;
	}

	const FTerrainBox Bounds = TerrainChunkBounds(Key);
	Out.Encoding = ETerrainRegionEncoding::Dense;
	Out.Payload.Reserve(TerrainChunkSampleCount * 4);

	// Density, then materials, both little-endian, local index x + 32y + 1024z (§4.7).
	for (int32 Z = 0; Z < TerrainChunkSizeVox; ++Z)
	for (int32 Y = 0; Y < TerrainChunkSizeVox; ++Y)
	for (int32 X = 0; X < TerrainChunkSizeVox; ++X)
	{
		float Value = 0.f;
		UVoxelDataTools::GetValue(Value, Actor, FIntVector(Bounds.Min.X + X, Bounds.Min.Y + Y, Bounds.Min.Z + Z));
		Append16(Out.Payload, static_cast<uint16>(QuantiseDensity(Value)));
	}
	for (int32 Index = 0; Index < TerrainChunkSampleCount; ++Index)
	{
		Append16(Out.Payload, 0);   // materials: K9, build step 6
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

	const FTerrainBox Bounds = TerrainChunkBounds(In.Key);
	for (int32 Z = 0; Z < TerrainChunkSizeVox; ++Z)
	for (int32 Y = 0; Y < TerrainChunkSizeVox; ++Y)
	for (int32 X = 0; X < TerrainChunkSizeVox; ++X)
	{
		const int16 Stored = static_cast<int16>(Read16(In.Payload, LocalIndex(X, Y, Z) * 2));
		UVoxelDataTools::SetValue(Actor,
			FIntVector(Bounds.Min.X + X, Bounds.Min.Y + Y, Bounds.Min.Z + Z),
			static_cast<float>(Stored) / VPLegacyValueScale);
	}

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

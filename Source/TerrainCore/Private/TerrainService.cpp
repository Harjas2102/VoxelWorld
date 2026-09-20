// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainService.h"
#include "TerrainBackendRegistry.h"
#include "TerrainChunk.h"
#include "TerrainCore.h"
#include "TerrainQuantise.h"
#include "TerrainSettings.h"
#include "TerrainWorldField.h"
#include "Engine/World.h"
#include "Modules/ModuleManager.h"
#include "TimerManager.h"

namespace
{
	const TCHAR* RejectionName(ETerrainEditRejection Rejection)
	{
		switch (Rejection)
		{
		case ETerrainEditRejection::None:              return TEXT("None");
		case ETerrainEditRejection::NoAuthority:       return TEXT("NoAuthority");
		case ETerrainEditRejection::NotReady:          return TEXT("NotReady");
		case ETerrainEditRejection::BadRequest:        return TEXT("BadRequest");
		case ETerrainEditRejection::RadiusTooLarge:    return TEXT("RadiusTooLarge");
		case ETerrainEditRejection::OutOfBounds:       return TEXT("OutOfBounds");
		case ETerrainEditRejection::TooLarge:          return TEXT("TooLarge");
		case ETerrainEditRejection::RevisionExhausted: return TEXT("RevisionExhausted");
		case ETerrainEditRejection::BackendFailed:     return TEXT("BackendFailed");
		case ETerrainEditRejection::ShuttingDown:      return TEXT("ShuttingDown");
		case ETerrainEditRejection::QueueFull:         return TEXT("QueueFull");
		case ETerrainEditRejection::StaleRequest:      return TEXT("StaleRequest");
		case ETerrainEditRejection::Revalidation:      return TEXT("Revalidation");
		default:                                       return TEXT("Unknown");
		}
	}

	FTerrainEditReceipt Reject(ETerrainEditRejection Rejection)
	{
		// Verbose, not Warning: a refused request is the system working. It is logged at all
		// because "digging silently does nothing" is otherwise indistinguishable from a
		// broken input binding, and that is the first thing anyone will hit.
		UE_LOG(LogTerrainCore, Verbose, TEXT("Terrain edit request rejected: %s"), RejectionName(Rejection));

		FTerrainEditReceipt Receipt;
		Receipt.bApplied = false;
		Receipt.Rejection = Rejection;
		return Receipt;
	}
}

void UTerrainService::Initialize(FSubsystemCollectionBase& Collection)
{
	check(IsInGameThread());
	// A repeated initialize must not erase this lifetime's revision history.
	if (RevisionIndex)
	{
		return;
	}
	RevisionIndex = MakeUnique<FTerrainRevisionIndex>();
	State = ETerrainServiceState::Uninitialised;
	WorldTearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(
		this, &UTerrainService::OnWorldBeginTearDown);
	Super::Initialize(Collection);
}

void UTerrainService::Deinitialize()
{
	check(IsInGameThread());
	DestroyBackend();
	FWorldDelegates::OnWorldBeginTearDown.Remove(WorldTearDownHandle);
	WorldTearDownHandle.Reset();
	RevisionIndex.Reset();
	Super::Deinitialize();
}

void UTerrainService::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	check(IsInGameThread());

	// The backend is created at BeginPlay and not at Initialize because a backend may need to
	// find or spawn actors, and subsystem initialization runs before the level's actors are
	// ready. Editor preview and other non-game worlds get no backend at all.
	if (InWorld.IsGameWorld())
	{
		CreateBackend(InWorld);
	}
}

void UTerrainService::CreateBackend(UWorld& InWorld)
{
	check(IsInGameThread());
	if (State != ETerrainServiceState::Uninitialised || Backend)
	{
		return;
	}

	const UTerrainSettings* Settings = GetDefault<UTerrainSettings>();
	if (!Settings->IsValid())
	{
		UE_LOG(LogTerrainCore, Error,
			TEXT("[/Script/TerrainCore.TerrainSettings] is not usable (BackendModule='%s', VoxelSizeCm=%f, "
				 "WorldSizeVoxels=%d). No terrain backend will be created."),
			*Settings->BackendModule.ToString(), Settings->VoxelSizeCm, Settings->WorldSizeVoxels);
		return;
	}

	BackendName = Settings->BackendModule;

	// Load by NAME. TerrainCore has no build dependency on any backend module and must not
	// acquire one (§4.1, §4.1.0): that build-system fact is the enforcement mechanism for
	// D-011, and §10 makes this config line the whole of a backend swap.
	if (!FTerrainBackendRegistry::Get().IsRegistered(BackendName))
	{
		FModuleManager::Get().LoadModule(BackendName);
	}

	Backend = FTerrainBackendRegistry::Get().Create(BackendName);
	if (!Backend)
	{
		const TArray<FName> Registered = FTerrainBackendRegistry::Get().GetRegisteredNames();
		FString Names;
		for (const FName& Name : Registered)
		{
			Names += (Names.IsEmpty() ? TEXT("") : TEXT(", ")) + Name.ToString();
		}
		UE_LOG(LogTerrainCore, Error,
			TEXT("Terrain backend '%s' is not registered (registered: [%s]). Terrain edits will be refused."),
			*BackendName.ToString(), Names.IsEmpty() ? TEXT("none") : *Names);
		return;
	}

	ActiveInit = FTerrainBackendInit();
	ActiveInit.Seed = Settings->Seed;
	ActiveInit.GeneratorVersion = static_cast<uint32>(FMath::Max(0, Settings->GeneratorVersion));
	ActiveInit.VoxelSizeCm = Settings->VoxelSizeCm;
	ActiveInit.WorldBoundsVox = Settings->GetWorldBoundsVox();
	// T-108 / build step 8: the game states the world's shape and the backend renders it.
	// Until this existed the actor kept whatever generator it was authored with — a
	// VoxelFlatGenerator — which is why the test world was a plane and why the T-101A hill
	// had to be sculpted by script and did not survive a map load (finding 2e, R-003).
	// The subsystem creates the immutable field; async generator instances retain shared
	// lifetime so releasing this owner after Shutdown cannot invalidate plugin workers.
	{
		FTerrainWorldFieldParams FieldParams;
		FieldParams.Seed = Settings->Seed;
		DensityField = MakeShared<FTerrainWorldField, ESPMode::ThreadSafe>(FieldParams);
	}
	ActiveInit.DensityField = DensityField.Get();
	ActiveInit.DensityFieldOwner = DensityField;
	ActiveInit.Role = InWorld.GetNetMode() == NM_Client ? ETerrainRole::Client : ETerrainRole::Server;
	ActiveInit.World = &InWorld;
	ActiveInit.OriginTransform = Settings->GetTerrainOrigin();

	if (!Backend->Initialize(ActiveInit))
	{
		UE_LOG(LogTerrainCore, Error, TEXT("Terrain backend '%s' failed to initialize. Terrain edits will be refused."),
			*BackendName.ToString());
		Backend.Reset();
		ActiveInit = {};
		DensityField.Reset();
		return;
	}

	State = ETerrainServiceState::Ready;

	// Before the queue can admit anything and before the tick starts: replay needs a backend
	// that nothing has edited yet, and a client that connected mid-replay would be told about
	// a world that was still being rebuilt.
	OpenWorldStore(InWorld);

	FTerrainSourceState Admin;
	EditQueue.RegisterSource(1,Admin);
	InWorld.GetTimerManager().SetTimer(ServiceTickHandle,this,&UTerrainService::TickService,0.01f,true);
	UE_LOG(LogTerrainCore, Log,
		TEXT("Terrain backend '%s' ready: role=%s, voxel=%.1f cm, bounds=[%d,%d,%d)-[%d,%d,%d), generator version %u."),
		*BackendName.ToString(),
		ActiveInit.Role == ETerrainRole::Server ? TEXT("Server") : TEXT("Client"),
		ActiveInit.VoxelSizeCm,
		ActiveInit.WorldBoundsVox.Min.X, ActiveInit.WorldBoundsVox.Min.Y, ActiveInit.WorldBoundsVox.Min.Z,
		ActiveInit.WorldBoundsVox.Max.X, ActiveInit.WorldBoundsVox.Max.Y, ActiveInit.WorldBoundsVox.Max.Z,
		ActiveInit.GeneratorVersion);
}

void UTerrainService::OnWorldBeginTearDown(UWorld* World)
{
	if (World && World == GetWorld())
	{
		// Close admission immediately. Release the backend at Deinitialize, after actor
		// EndPlay: a level-authored voxel actor still uses the borrowed field before then.
		if (State != ETerrainServiceState::TornDown)
		{
			State = ETerrainServiceState::Draining;
			EditQueue.Cancel(QueueCallbacks());
		}
	}
}

void UTerrainService::DestroyBackend()
{
	check(IsInGameThread());
	if (bDestroyingBackend || State == ETerrainServiceState::TornDown)
	{
		return;
	}
	State = ETerrainServiceState::Draining;
	TGuardValue<bool> DestroyGuard(bDestroyingBackend, true);
	EditQueue.Cancel(QueueCallbacks());

	// Detach before anything else is released: the commit path must not be able to reach a
	// store that is going away. Ordinary teardown needs no final checkpoint -- the committed
	// journal is already durable, which is the whole point of writing it before broadcasting
	// (P-003 §2).
	CloseWorldStore();

	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(ServiceTickHandle);
	Streams.Reset();

	// Clear interests through the backend before shutting it down: the backend owns whatever
	// it created for each one, and a shutdown that leaves those alive is the teardown half of
	// the DEF-10 arrangement failing silently.
	if (Backend)
	{
		for (const TPair<uint32, FTerrainStreamingInterest>& Pair : Interests)
		{
			Backend->ClearStreamingInterest(Pair.Key);
		}
	}
	Interests.Empty();

	if (Backend)
	{
		Backend->Shutdown();
	}
	Backend.Reset();

	// STRICTLY AFTER Shutdown. The backend holds a borrowed pointer to this field and its
	// teardown may still sample it; releasing it first would be a use-after-free that only
	// shows up under a mesher thread still in flight, which is the hardest kind to see.
	ActiveInit = FTerrainBackendInit();
	DensityField.Reset();
	State = ETerrainServiceState::TornDown;
}

bool UTerrainService::HasAuthority() const
{
	check(IsInGameThread());
	const UWorld* World = GetWorld();
	return RevisionIndex && World && World->bIsWorldInitialized
		&& World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

FTerrainRev UTerrainService::GetRevision(const FTerrainChunkKey& Key) const
{
	check(IsInGameThread());
	return RevisionIndex ? RevisionIndex->GetRevision(Key) : 0;
}

float UTerrainService::GetVoxelSizeCm() const
{
	return Backend ? ActiveInit.VoxelSizeCm : GetDefault<UTerrainSettings>()->VoxelSizeCm;
}

FTransform UTerrainService::GetTerrainOrigin() const
{
	return Backend ? ActiveInit.OriginTransform : GetDefault<UTerrainSettings>()->GetTerrainOrigin();
}

bool UTerrainService::QueryPoint(const FIntVector& VoxelPos, FTerrainPointSample& OutSample) const
{
	check(IsInGameThread());
	OutSample = FTerrainPointSample();
	return IsBackendReady() && Backend->QueryPoint(VoxelPos, OutSample);
}

bool UTerrainService::RequestEdit(const FTerrainEditRequest& Request, FTerrainEditReceipt& OutReceipt)
{
	check(IsInGameThread());
	OutReceipt = {};
	if (!IsBackendReady())
	{
		OutReceipt = Reject(State == ETerrainServiceState::Draining ? ETerrainEditRejection::ShuttingDown : ETerrainEditRejection::NotReady);
		return false;
	}
	if (!HasAuthority()) { OutReceipt = Reject(ETerrainEditRejection::NoAuthority); return false; }
#if !UE_BUILD_SHIPPING
	// Legacy console diagnostics only. Gameplay uses the owning controller's transport.
	// There is no network RPC into this admin path, and it is unavailable in multiplayer.
	if (GetWorld()->GetNetMode() == NM_Standalone)
	{
		FTerrainOp Op;
		auto Failure = QuantiseRequest(Request,Op);
		if (Request.RadiusCm > GetDefault<UTerrainSettings>()->MaxEditRadiusCm) Failure = ETerrainEditRejection::RadiusTooLarge;
		Op.Source = ETerrainSource::Admin;
		const int64 Id = NextAdminRequest++;
		const auto Cb = QueueCallbacks();
		EditQueue.Submit(1,Id,Op,GetWorld()->GetTimeSeconds(),Cb,OutReceipt,
			GetDefault<UTerrainSettings>()->MaxVoxelsPerOp,Failure);
		EditQueue.Pump(GetWorld()->GetTimeSeconds(),Cb,256,1.);
		if (LastAdminReceipt.RequestId == Id) OutReceipt=LastAdminReceipt;
		return OutReceipt.bApplied;
	}
#endif
	OutReceipt = Reject(ETerrainEditRejection::NoAuthority);
	return false;
}

uint32 UTerrainService::AcquireStreamingInterest(const FVector& WorldLocation, double RadiusCm, bool bCollision, bool bRender)
{
	check(IsInGameThread());
	if (!IsBackendReady() || WorldLocation.ContainsNaN() || !FMath::IsFinite(RadiusCm) || RadiusCm < 0.0)
	{
		return 0;
	}

	FTerrainStreamingInterest Interest;
	Interest.InterestId = NextInterestId++;
	Interest.WorldLocation = WorldLocation;
	Interest.RadiusCm = RadiusCm;
	Interest.bCollision = bCollision;
	Interest.bRender = bRender;

	Interests.Add(Interest.InterestId, Interest);
	Backend->SetStreamingInterest(Interest);
	return Interest.InterestId;
}

void UTerrainService::UpdateStreamingInterest(uint32 InterestId, const FVector& WorldLocation)
{
	check(IsInGameThread());
	if (!IsBackendReady() || InterestId == 0 || WorldLocation.ContainsNaN())
	{
		return;
	}
	FTerrainStreamingInterest* Interest = Interests.Find(InterestId);
	if (!Interest)
	{
		return;
	}
	Interest->WorldLocation = WorldLocation;
	Backend->SetStreamingInterest(*Interest);
}

void UTerrainService::ReleaseStreamingInterest(uint32 InterestId)
{
	check(IsInGameThread());
	if (!IsBackendReady() || InterestId == 0 || Interests.Remove(InterestId) == 0)
	{
		return;
	}
	if (Backend)
	{
		Backend->ClearStreamingInterest(InterestId);
	}
}

bool UTerrainService::TryAdvanceRevisions(TConstArrayView<FTerrainChunkKey> AffectedChunks)
{
	// Short circuit before touching UObject/world state from a worker thread.
	return IsInGameThread() && HasAuthority()
		&& RevisionIndex->TryBumpRevisions(AffectedChunks);
}

// ---------------------------------------------------------------------------------------
// Development console commands.
//
// These exist because build step 2's result would otherwise be checkable only by a human
// with a mouse. `Terrain.Edit` drives the exact same RequestEdit path the rewired Blueprint
// drives, so a headless run can prove the whole chain — quantisation, backend, plugin kernel,
// revision bump — rather than proving only that the backend initialised.
//
// They are debug affordances, not gameplay: they carry no tool, no permission and no reach
// check, and they run on the authority only. When build step 3 adds real validation these
// stop being a shortcut past it, because RequestEdit is where that validation will live.
// ---------------------------------------------------------------------------------------

static UTerrainService* FindTerrainServiceForCommand(UWorld* World)
{
	if (!World || !World->IsGameWorld())
	{
		UE_LOG(LogTerrainCore, Warning, TEXT("No game world; terrain commands need a running game."));
		return nullptr;
	}
	UTerrainService* Service = World->GetSubsystem<UTerrainService>();
	if (!Service)
	{
		UE_LOG(LogTerrainCore, Warning, TEXT("This world has no UTerrainService."));
	}
	return Service;
}

static FAutoConsoleCommandWithWorldAndArgs GTerrainEditCommand(
	TEXT("Terrain.Edit"),
	TEXT("Terrain.Edit <Remove|Add> <X> <Y> <Z> [RadiusCm]  — issue one edit request through UTerrainService, in world space."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UTerrainService* Service = FindTerrainServiceForCommand(World);
		if (!Service)
		{
			return;
		}
		if (Args.Num() < 4)
		{
			UE_LOG(LogTerrainCore, Warning, TEXT("Usage: Terrain.Edit <Remove|Add> <X> <Y> <Z> [RadiusCm]"));
			return;
		}

		FTerrainEditRequest Request;
		Request.Kind = Args[0].Equals(TEXT("Add"), ESearchCase::IgnoreCase)
			? ETerrainEditKind::Add : ETerrainEditKind::Remove;
		Request.WorldLocation = FVector(FCString::Atod(*Args[1]), FCString::Atod(*Args[2]), FCString::Atod(*Args[3]));
		Request.RadiusCm = Args.Num() > 4 ? FCString::Atod(*Args[4]) : 200.0;

		FTerrainEditReceipt Receipt;
		Service->RequestEdit(Request, Receipt);

		UE_LOG(LogTerrainCore, Display,
			TEXT("Terrain.Edit %s at (%.1f, %.1f, %.1f) r=%.1f cm -> applied=%s reason=%s "
				 "opseq=%lld chunks=%d voxels=%lld"),
			Request.Kind == ETerrainEditKind::Add ? TEXT("Add") : TEXT("Remove"),
			Request.WorldLocation.X, Request.WorldLocation.Y, Request.WorldLocation.Z, Request.RadiusCm,
			Receipt.bApplied ? TEXT("YES") : TEXT("no"),
			RejectionName(Receipt.Rejection),
			Receipt.OpSeq, Receipt.ChunksAffected, Receipt.VoxelsTouched);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTerrainStatusCommand(
	TEXT("Terrain.Status"),
	TEXT("Terrain.Status [X Y Z]  — report the terrain service, and optionally sample one world position."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UTerrainService* Service = FindTerrainServiceForCommand(World);
		if (!Service)
		{
			return;
		}

		UE_LOG(LogTerrainCore, Display,
			TEXT("Terrain.Status: backend='%s' ready=%s authority=%s voxel=%.1fcm interests=%d"),
			*Service->GetBackendName().ToString(),
			Service->IsBackendReady() ? TEXT("yes") : TEXT("NO"),
			Service->HasAuthority() ? TEXT("yes") : TEXT("no"),
			Service->GetVoxelSizeCm(),
			Service->GetStreamingInterestCount());

		if (Args.Num() < 3)
		{
			return;
		}

		const FVector WorldPos(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]), FCString::Atod(*Args[2]));
		FIntVector Voxel;
		if (!QuantiseEdit(WorldPos, Service->GetTerrainOrigin(), Service->GetVoxelSizeCm(), Voxel))
		{
			UE_LOG(LogTerrainCore, Warning, TEXT("Terrain.Status: (%s) does not quantise."), *WorldPos.ToString());
			return;
		}

		const FTerrainChunkKey Key = TerrainChunkKeyForVoxel(Voxel);
		FTerrainPointSample Sample;
		const bool bQueried = Service->QueryPoint(Voxel, Sample);
		UE_LOG(LogTerrainCore, Display,
			TEXT("Terrain.Status: world (%.1f, %.1f, %.1f) = voxel (%d, %d, %d) in chunk (%d, %d, %d) rev %u; "
				 "query=%s resident=%s density=%.4f material=%u"),
			WorldPos.X, WorldPos.Y, WorldPos.Z, Voxel.X, Voxel.Y, Voxel.Z, Key.X, Key.Y, Key.Z,
			Service->GetRevision(Key),
			bQueried ? TEXT("ok") : TEXT("FAILED"),
			Sample.bResident ? TEXT("yes") : TEXT("no"),
			Sample.Density, Sample.MaterialId);
	}));

/**
 * Terrain.SelfTest — drive the whole build-step-2 path and say plainly whether it worked.
 *
 * One command with no arguments, deliberately: `-ExecCmds` splits on commas and does not
 * reliably deliver argument lists to a deferred console command, so anything that has to run
 * from a command line has to be argument-free. It also self-delays, because the voxel world
 * finishes generating after the first map-load commands fire, and an edit issued into a world
 * that has not generated yet would report zero touched voxels and look like a failure.
 *
 * This is a smoke test, not a §6.2 automation test. It proves the chain is connected —
 * request, quantisation, admission limits, backend, plugin kernel, revision bump — in a real
 * game world. It proves nothing about determinism, concurrency, persistence or yield.
 */
static void RunTerrainSelfTest(UWorld* World)
{
	UTerrainService* Service = FindTerrainServiceForCommand(World);
	if (!Service)
	{
		UE_LOG(LogTerrainCore, Error, TEXT("Terrain.SelfTest: FAIL — no service."));
		return;
	}

	int32 Failures = 0;
	const auto Check = [&Failures](bool bCondition, const TCHAR* What)
	{
		if (bCondition)
		{
			UE_LOG(LogTerrainCore, Display, TEXT("Terrain.SelfTest:   ok   %s"), What);
		}
		else
		{
			++Failures;
			UE_LOG(LogTerrainCore, Error, TEXT("Terrain.SelfTest:   FAIL %s"), What);
		}
	};

	UE_LOG(LogTerrainCore, Display, TEXT("Terrain.SelfTest: backend='%s' ready=%d authority=%d interests=%d"),
		*Service->GetBackendName().ToString(), Service->IsBackendReady() ? 1 : 0,
		Service->HasAuthority() ? 1 : 0, Service->GetStreamingInterestCount());

	Check(Service->IsBackendReady(), TEXT("a backend is ready"));
	Check(Service->HasAuthority(), TEXT("this process holds terrain authority"));
	Check(Service->GetStreamingInterestCount() > 0,
		TEXT("something registered streaming interest (UTerrainStreamingComponent)"));

	// The world origin: inside the world, away from the player start, and — since T-108 put
	// the hill east of here — still on the lowland plain, so this is ground rather than sky.
	const FVector Target(0.0, 0.0, 0.0);
	FIntVector Voxel;
	if (!QuantiseEdit(Target, Service->GetTerrainOrigin(), Service->GetVoxelSizeCm(), Voxel))
	{
		UE_LOG(LogTerrainCore, Error, TEXT("Terrain.SelfTest: FAIL — the target does not quantise."));
		return;
	}
	const FTerrainChunkKey Key = TerrainChunkKeyForVoxel(Voxel);
	const FTerrainRev RevBefore = Service->GetRevision(Key);

	FTerrainPointSample Before;
	Check(Service->QueryPoint(Voxel, Before), TEXT("QueryPoint answers before the edit"));
	UE_LOG(LogTerrainCore, Display, TEXT("Terrain.SelfTest: voxel (%d,%d,%d) chunk (%d,%d,%d) rev %u, density %.4f, resident %d"),
		Voxel.X, Voxel.Y, Voxel.Z, Key.X, Key.Y, Key.Z, RevBefore, Before.Density, Before.bResident ? 1 : 0);

	// --- the edit that matters -----------------------------------------------------------
	FTerrainEditRequest Dig;
	Dig.Kind = ETerrainEditKind::Remove;
	Dig.WorldLocation = Target;
	Dig.RadiusCm = 200.0;
	FTerrainEditReceipt DigReceipt;
	Service->RequestEdit(Dig, DigReceipt);
	UE_LOG(LogTerrainCore, Display,
		TEXT("Terrain.SelfTest: Remove r=200cm -> applied=%d reason=%s opseq=%lld chunks=%d voxels=%lld"),
		DigReceipt.bApplied ? 1 : 0, RejectionName(DigReceipt.Rejection),
		DigReceipt.OpSeq, DigReceipt.ChunksAffected, DigReceipt.VoxelsTouched);

	Check(DigReceipt.bApplied, TEXT("the dig was applied"));
	Check(DigReceipt.OpSeq > 0, TEXT("an OpSeq was assigned at commit"));
	Check(DigReceipt.ChunksAffected > 0, TEXT("at least one chunk was reported affected"));
	Check(DigReceipt.VoxelsTouched > 0, TEXT("the plugin kernel actually changed voxels"));
	Check(Service->GetRevision(Key) > RevBefore, TEXT("the chunk revision advanced"));

	FTerrainPointSample After;
	Service->QueryPoint(Voxel, After);
	Check(After.Density > Before.Density, TEXT("density at the centre moved towards empty"));
	UE_LOG(LogTerrainCore, Display, TEXT("Terrain.SelfTest: density %.4f -> %.4f, rev %u -> %u"),
		Before.Density, After.Density, RevBefore, Service->GetRevision(Key));

	// --- a placement, so Add is exercised too ---------------------------------------------
	// Back into the hole the dig just made, at a smaller radius so it lands strictly inside
	// the void. Before T-108 this added at a fixed offset on a flat plane; a generated world
	// has no such guaranteed-empty address, and an Add into solid rock touches nothing and
	// would read as a failure of Add rather than of the assumption. The freshly dug void is
	// the one place that is empty whatever shape the world has.
	FTerrainEditRequest Place;
	Place.Kind = ETerrainEditKind::Add;
	Place.WorldLocation = Target;
	Place.RadiusCm = 100.0;
	FTerrainEditReceipt PlaceReceipt;
	Service->RequestEdit(Place, PlaceReceipt);
	Check(PlaceReceipt.bApplied && PlaceReceipt.VoxelsTouched > 0, TEXT("Add placed material"));
	Check(PlaceReceipt.OpSeq > DigReceipt.OpSeq, TEXT("OpSeq is monotonic across operations"));

	// --- the admission limits, which are the half that must refuse ------------------------
	FTerrainEditRequest TooBig = Dig;
	TooBig.RadiusCm = 99999.0;
	FTerrainEditReceipt R;
	Service->RequestEdit(TooBig, R);
	Check(!R.bApplied && R.Rejection == ETerrainEditRejection::RadiusTooLarge,
		TEXT("an over-large radius is refused as RadiusTooLarge"));

	FTerrainEditRequest FarAway = Dig;
	FarAway.WorldLocation = FVector(1.0e7, 0.0, 0.0);
	Service->RequestEdit(FarAway, R);
	Check(!R.bApplied && R.Rejection == ETerrainEditRejection::OutOfBounds,
		TEXT("an edit outside the world is refused as OutOfBounds"));

	FTerrainEditRequest Nonsense = Dig;
	Nonsense.RadiusCm = -1.0;
	Service->RequestEdit(Nonsense, R);
	Check(!R.bApplied && R.Rejection == ETerrainEditRejection::BadRequest,
		TEXT("a negative radius is refused as BadRequest"));

	if (Failures == 0)
	{
		UE_LOG(LogTerrainCore, Display, TEXT("**** Terrain.SelfTest: PASS ****"));
	}
	else
	{
		UE_LOG(LogTerrainCore, Error, TEXT("**** Terrain.SelfTest: FAILED %d check(s) ****"), Failures);
	}
}

static FAutoConsoleCommandWithWorld GTerrainSelfTestCommand(
	TEXT("Terrain.SelfTest"),
	TEXT("Terrain.SelfTest — drive one dig and one placement through UTerrainService and report PASS/FAIL. Runs two seconds after invocation, so the voxel world has generated."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (!World)
		{
			return;
		}
		UE_LOG(LogTerrainCore, Display, TEXT("Terrain.SelfTest: scheduled in 2s (waiting for the world to generate)."));
		FTimerHandle Handle;
		TWeakObjectPtr<UWorld> WeakWorld(World);
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			if (UWorld* Live = WeakWorld.Get())
			{
				RunTerrainSelfTest(Live);
			}
		}), 2.0f, false);
	}));

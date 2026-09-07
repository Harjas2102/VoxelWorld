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
	/**
	 * The number of written voxels a sphere of RadiusVox covers, as a bound rather than a
	 * count. The backend decides which samples its kernel actually touches; the service only
	 * needs to know whether the request can possibly exceed MaxVoxelsPerOp (§7.1), and it has
	 * to know that BEFORE calling the backend. Half a voxel of slack on the radius keeps the
	 * estimate on the safe side of the integer lattice.
	 */
	double EstimateSphereVoxels(double RadiusVox)
	{
		const double R = FMath::Max(0.0, RadiusVox) + 0.5;
		return (4.0 / 3.0) * PI * R * R * R;
	}

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
	Super::Initialize(Collection);
}

void UTerrainService::Deinitialize()
{
	check(IsInGameThread());
	DestroyBackend();
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
	if (Backend)
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
	// The field is owned by this subsystem and only LENT to the backend (AR-2): it must
	// outlive Shutdown, so DestroyBackend releases it strictly afterwards.
	{
		FTerrainWorldFieldParams FieldParams;
		FieldParams.Seed = Settings->Seed;
		DensityField = MakeUnique<FTerrainWorldField>(FieldParams);
	}
	ActiveInit.DensityField = DensityField.Get();
	ActiveInit.Role = InWorld.GetNetMode() == NM_Client ? ETerrainRole::Client : ETerrainRole::Server;
	ActiveInit.World = &InWorld;
	ActiveInit.OriginTransform = Settings->GetTerrainOrigin();

	if (!Backend->Initialize(ActiveInit))
	{
		UE_LOG(LogTerrainCore, Error, TEXT("Terrain backend '%s' failed to initialize. Terrain edits will be refused."),
			*BackendName.ToString());
		Backend.Reset();
		DensityField.Reset();
		return;
	}

	UE_LOG(LogTerrainCore, Log,
		TEXT("Terrain backend '%s' ready: role=%s, voxel=%.1f cm, bounds=[%d,%d,%d)-[%d,%d,%d), generator version %u."),
		*BackendName.ToString(),
		ActiveInit.Role == ETerrainRole::Server ? TEXT("Server") : TEXT("Client"),
		ActiveInit.VoxelSizeCm,
		ActiveInit.WorldBoundsVox.Min.X, ActiveInit.WorldBoundsVox.Min.Y, ActiveInit.WorldBoundsVox.Min.Z,
		ActiveInit.WorldBoundsVox.Max.X, ActiveInit.WorldBoundsVox.Max.Y, ActiveInit.WorldBoundsVox.Max.Z,
		ActiveInit.GeneratorVersion);
}

void UTerrainService::DestroyBackend()
{
	check(IsInGameThread());
	if (!Backend)
	{
		Interests.Empty();
		DensityField.Reset();
		return;
	}

	// Clear interests through the backend before shutting it down: the backend owns whatever
	// it created for each one, and a shutdown that leaves those alive is the teardown half of
	// the DEF-10 arrangement failing silently.
	for (const TPair<uint32, FTerrainStreamingInterest>& Pair : Interests)
	{
		Backend->ClearStreamingInterest(Pair.Key);
	}
	Interests.Empty();

	Backend->Shutdown();
	Backend.Reset();

	// STRICTLY AFTER Shutdown. The backend holds a borrowed pointer to this field and its
	// teardown may still sample it; releasing it first would be a use-after-free that only
	// shows up under a mesher thread still in flight, which is the hardest kind to see.
	DensityField.Reset();
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
	return Backend && Backend->QueryPoint(VoxelPos, OutSample);
}

bool UTerrainService::RequestEdit(const FTerrainEditRequest& Request, FTerrainEditReceipt& OutReceipt)
{
	check(IsInGameThread());
	OutReceipt = FTerrainEditReceipt();

	// --- admission: who, and is there anything to talk to ---------------------------------
	// Step 3 replaces this refusal on the client with a ServerRequestEdit RPC. Until then a
	// client asking to dig is told no rather than being allowed to edit its own copy, which
	// is the client-authoritative shortcut AGENTS.md §4 forbids outright.
	if (!HasAuthority())
	{
		OutReceipt = Reject(ETerrainEditRejection::NoAuthority);
		return false;
	}
	if (!Backend)
	{
		OutReceipt = Reject(ETerrainEditRejection::NotReady);
		return false;
	}

	// --- admission: is the request even well formed ---------------------------------------
	if (Request.WorldLocation.ContainsNaN()
		|| !FMath::IsFinite(Request.RadiusCm)
		|| Request.RadiusCm <= 0.0
		|| Request.MaterialId < 0 || Request.MaterialId > MAX_uint16
		|| (Request.Kind != ETerrainEditKind::Remove && Request.Kind != ETerrainEditKind::Add))
	{
		OutReceipt = Reject(ETerrainEditRejection::BadRequest);
		return false;
	}

	const UTerrainSettings* Settings = GetDefault<UTerrainSettings>();
	if (Request.RadiusCm > Settings->MaxEditRadiusCm)
	{
		OutReceipt = Reject(ETerrainEditRejection::RadiusTooLarge);
		return false;
	}

	// --- quantise ONCE, here (§4.3) --------------------------------------------------------
	// The integers below are the operation: for the wire, for the journal, and for this
	// server's own application of it. Nothing downstream re-derives them from a float.
	FIntVector CentreVox;
	if (!QuantiseEdit(Request.WorldLocation, ActiveInit.OriginTransform, ActiveInit.VoxelSizeCm, CentreVox))
	{
		OutReceipt = Reject(ETerrainEditRejection::OutOfBounds);
		return false;
	}

	const int32 RadiusVoxQ16 = QuantiseRadiusQ16(Request.RadiusCm, ActiveInit.VoxelSizeCm);
	if (RadiusVoxQ16 <= 0)
	{
		OutReceipt = Reject(ETerrainEditRejection::BadRequest);
		return false;
	}
	const double RadiusVox = static_cast<double>(RadiusVoxQ16) / 65536.0;

	// --- bound the work (§7.1) -------------------------------------------------------------
	// Over the cap the correct answer is to split into sub-ops sharing a TransactionId. That
	// is step 3 work under DEF-7, which also owns whether a split is geometrically equivalent
	// to the unsplit op — so until then, refuse rather than invent the semantics.
	if (EstimateSphereVoxels(RadiusVox) > static_cast<double>(Settings->MaxVoxelsPerOp))
	{
		OutReceipt = Reject(ETerrainEditRejection::TooLarge);
		return false;
	}

	// --- footprint, and whether it is inside the world -------------------------------------
	const int32 RadiusCeil = FMath::CeilToInt(RadiusVox);
	const FTerrainBox Footprint(CentreVox - FIntVector(RadiusCeil), CentreVox + FIntVector(RadiusCeil + 1));
	const FTerrainBox& Bounds = ActiveInit.WorldBoundsVox;
	if (Footprint.Min.X < Bounds.Min.X || Footprint.Max.X > Bounds.Max.X
		|| Footprint.Min.Y < Bounds.Min.Y || Footprint.Max.Y > Bounds.Max.Y
		|| Footprint.Min.Z < Bounds.Min.Z || Footprint.Max.Z > Bounds.Max.Z)
	{
		OutReceipt = Reject(ETerrainEditRejection::OutOfBounds);
		return false;
	}

	TArray<FTerrainChunkKey> PredictedChunks;
	if (!TerrainChunkKeysForBox(Footprint, PredictedChunks))
	{
		OutReceipt = Reject(ETerrainEditRejection::TooLarge);
		return false;
	}

	// Revision exhaustion is checked BEFORE the backend is called. AR-4's helper refuses an
	// overflowing batch without mutating anything, but by then the terrain would already have
	// moved — leaving changed terrain that no revision records, which is the one failure this
	// step can actually prevent. The predicted footprint is a superset of what the kernel
	// touches, so this is conservative in the safe direction.
	for (const FTerrainChunkKey& Key : PredictedChunks)
	{
		if (GetRevision(Key) == MAX_uint32)
		{
			OutReceipt = Reject(ETerrainEditRejection::RevisionExhausted);
			return false;
		}
	}

	// --- build the operation ---------------------------------------------------------------
	FTerrainOp Op;
	Op.OpSeq = 0;               // assigned at commit, below, and only if the edit lands
	Op.TransactionId = 0;       // splitting is step 3; a single op is its own transaction
	Op.Kind = Request.Kind == ETerrainEditKind::Add ? ETerrainOpKind::Add : ETerrainOpKind::Remove;
	Op.Shape = ETerrainShape::Sphere;
	Op.Source = ETerrainSource::Player;
	Op.SourceId = static_cast<uint32>(FMath::Max(0, Request.SourceId));
	Op.ToolId = static_cast<uint32>(FMath::Max(0, Request.ToolId));
	Op.CentreVox = CentreVox;
	Op.RadiusVoxQ16 = RadiusVoxQ16;
	Op.ExtentVox = FIntVector::ZeroValue;
	Op.MaterialId = static_cast<FTerrainMatId>(Request.MaterialId);
	Op.Flags = 0;

	// --- execute ----------------------------------------------------------------------------
	FTerrainEditResult Result;
	if (!Backend->ApplyOp(Op, Result))
	{
		OutReceipt = Reject(ETerrainEditRejection::BackendFailed);
		return false;
	}

	// --- commit: sequence, then revisions ---------------------------------------------------
	// Journal append, yield settlement and client acknowledgement all belong here too, and
	// their relative ordering is DEF-1, bound to build step 4 with K5 ruled but unimplemented.
	// Nothing about this step's ordering should be read as settling that.
	const FTerrainOpSeq AssignedSeq = NextOpSeq++;
	Op.OpSeq = AssignedSeq;

	if (!Result.AffectedChunks.IsEmpty() && !TryAdvanceRevisions(Result.AffectedChunks))
	{
		// The pre-check above makes overflow unreachable for the predicted footprint; a
		// backend reporting chunks OUTSIDE that footprint can still land here. Say so loudly
		// rather than pretending the metadata is consistent.
		UE_LOG(LogTerrainCore, Error,
			TEXT("Terrain op %llu mutated %d chunk(s) but its revisions could not be advanced. "
				 "Chunk metadata is now behind the terrain."),
			AssignedSeq, Result.AffectedChunks.Num());
	}

	OutReceipt.bApplied = true;
	OutReceipt.Rejection = ETerrainEditRejection::None;
	OutReceipt.OpSeq = static_cast<int64>(AssignedSeq);
	OutReceipt.ChunksAffected = Result.AffectedChunks.Num();
	OutReceipt.VoxelsTouched = Result.VoxelsTouched;

	UE_LOG(LogTerrainCore, Verbose,
		TEXT("Terrain op %llu %s at (%d,%d,%d) r=%.2f vox: %lld voxels over %d chunk(s)."),
		AssignedSeq, Op.Kind == ETerrainOpKind::Add ? TEXT("Add") : TEXT("Remove"),
		CentreVox.X, CentreVox.Y, CentreVox.Z, RadiusVox,
		Result.VoxelsTouched, Result.AffectedChunks.Num());

	return true;
}

uint32 UTerrainService::AcquireStreamingInterest(const FVector& WorldLocation, double RadiusCm, bool bCollision, bool bRender)
{
	check(IsInGameThread());
	if (!Backend || WorldLocation.ContainsNaN() || !FMath::IsFinite(RadiusCm) || RadiusCm < 0.0)
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
	if (!Backend || InterestId == 0 || WorldLocation.ContainsNaN())
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
	if (InterestId == 0 || Interests.Remove(InterestId) == 0)
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

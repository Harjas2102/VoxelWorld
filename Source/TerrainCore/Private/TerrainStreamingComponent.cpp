// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainStreamingComponent.h"
#include "TerrainCore.h"
#include "TerrainService.h"
#include "TerrainSettings.h"
#include "Engine/World.h"

UTerrainStreamingComponent::UTerrainStreamingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// Interest follows the owner; it never drives it.
	bWantsInitializeComponent = false;
	SetIsReplicatedByDefault(false);
}

void UTerrainStreamingComponent::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	UTerrainService* Service = World->GetSubsystem<UTerrainService>();
	if (!Service)
	{
		UE_LOG(LogTerrainCore, Warning, TEXT("%s: no UTerrainService in this world; terrain will not stream around it."),
			*GetReadableName());
		return;
	}

	const UTerrainSettings* Settings = GetDefault<UTerrainSettings>();
	const double EffectiveRadius = RadiusCm > 0.0 ? RadiusCm : Settings->DefaultInterestRadiusCm;

	// §4.2: bRender is false on a dedicated server. A server that asked for render would pay
	// for meshing nobody looks at; it still needs collision, which is the other flag.
	const bool bEffectiveRender = bRender && World->GetNetMode() != NM_DedicatedServer;

	LastReportedLocation = GetComponentLocation();
	InterestId = Service->AcquireStreamingInterest(LastReportedLocation, EffectiveRadius, bCollision, bEffectiveRender);

	if (InterestId == 0)
	{
		UE_LOG(LogTerrainCore, Verbose, TEXT("%s: streaming interest not acquired (no backend yet)."), *GetReadableName());
	}
}

void UTerrainStreamingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Release before Super: after Super the component is on its way out and the service must
	// not be left holding an interest for an actor that no longer exists. §7.4 requires
	// release on EndPlay, travel and connection loss, and all three arrive here.
	if (InterestId != 0)
	{
		if (const UWorld* World = GetWorld())
		{
			if (UTerrainService* Service = World->GetSubsystem<UTerrainService>())
			{
				Service->ReleaseStreamingInterest(InterestId);
			}
		}
		InterestId = 0;
	}
	Super::EndPlay(EndPlayReason);
}

void UTerrainStreamingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	UTerrainService* Service = World->GetSubsystem<UTerrainService>();
	if (!Service)
	{
		return;
	}

	// A component that begins play before the backend exists gets no handle. Retry rather
	// than leaving the owner permanently uninteresting to the terrain.
	if (InterestId == 0)
	{
		const UTerrainSettings* Settings = GetDefault<UTerrainSettings>();
		const double EffectiveRadius = RadiusCm > 0.0 ? RadiusCm : Settings->DefaultInterestRadiusCm;
		const bool bEffectiveRender = bRender && World->GetNetMode() != NM_DedicatedServer;
		LastReportedLocation = GetComponentLocation();
		InterestId = Service->AcquireStreamingInterest(LastReportedLocation, EffectiveRadius, bCollision, bEffectiveRender);
		return;
	}

	const FVector Current = GetComponentLocation();
	if (FVector::DistSquared(Current, LastReportedLocation) >= UpdateThresholdCm * UpdateThresholdCm)
	{
		LastReportedLocation = Current;
		Service->UpdateStreamingInterest(InterestId, Current);
	}
}

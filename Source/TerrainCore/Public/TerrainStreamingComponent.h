// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "TerrainStreamingComponent.generated.h"

/**
 * UTerrainStreamingComponent — "terrain must be resident here, and why" (§7.4, DEF-10).
 *
 * THIS CLASS EXISTS BECAUSE OF WHAT IT IS NOT. The backend's plugin requires an invoker
 * component on every character, on client and server both. Putting that component on
 * BP_ThirdPersonCharacter is exactly the leak D-011 forbids, and putting it there in an
 * ASSET is worse than in code, because a backend swap would then need an asset edit that no
 * compiler can force. So gameplay attaches this — a TerrainCore class, no plugin dependency,
 * no plugin type in its interface — and the backend receives the intent through
 * ITerrainBackend::SetStreamingInterest. DEF-10 is closed by this arrangement, not by a
 * comment; §10's swap test exercises attachment, respawn and teardown for that reason.
 *
 * Interest handles are owned by UTerrainService and released on EndPlay, travel and
 * connection loss (§7.4). This component holds a handle, never a backend pointer.
 *
 * Cost at 32 dispersed players is UNKNOWN and is experiment E-8. Nothing here claims it is
 * cheap; it claims only that it is measurable in one place.
 */
UCLASS(ClassGroup = (Terrain), meta = (BlueprintSpawnableComponent), HideCategories = (Trigger, PhysicsVolume))
class TERRAINCORE_API UTerrainStreamingComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UTerrainStreamingComponent();

	/** Interest radius in centimetres. Zero or negative falls back to the project default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Streaming", meta = (ClampMin = "0"))
	double RadiusCm = 0.0;

	/** Terrain must be collidable within the radius. Servers care about this and not render. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Streaming")
	bool bCollision = true;

	/**
	 * Ask for rendered terrain as well. Forced false on a dedicated server (§4.2: bRender is
	 * "false on a dedicated server"), so this is a request, not a guarantee.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Streaming")
	bool bRender = true;

	/**
	 * How far this component may move before the service is told, in centimetres. Movement is
	 * continuous and interest is not: forwarding every frame would push a backend refresh per
	 * tick per player for a change smaller than one voxel.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Streaming", meta = (ClampMin = "0"))
	double UpdateThresholdCm = 100.0;

	/** Zero when this component holds no interest — before BeginPlay, or after EndPlay. */
	UFUNCTION(BlueprintCallable, Category = "Terrain|Streaming")
	int64 GetInterestHandle() const { return static_cast<int64>(InterestId); }

	//~ Begin UActorComponent Interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

private:
	/** Service-owned handle. 0 is "no interest" and is never a valid handle. */
	uint32 InterestId = 0;

	/** Where the service last thinks this interest is. Only meaningful while InterestId != 0. */
	FVector LastReportedLocation = FVector::ZeroVector;
};

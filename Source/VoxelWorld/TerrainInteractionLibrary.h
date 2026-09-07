// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TerrainService.h"
#include "TerrainInteractionLibrary.generated.h"

/**
 * UTerrainInteractionLibrary — the gameplay side of the build-step-2 rewire (§4.3, §4.4).
 *
 * WHAT THIS REPLACES. Until T-113, BP_ThirdPersonCharacter did a camera line trace and then
 * called UVoxelSphereTools::RemoveSphere / AddSphere on an AVoxelWorld reference it held as a
 * variable. That was a direct plugin call from gameplay (D-011, AGENTS.md §4 and §9) and it
 * was client-authoritative (AGENTS.md §4), and STATE.md has carried both as flagged drift
 * checks since T-101A on the explicit understanding that the first thing built afterwards
 * would delete them.
 *
 * WHAT STAYS LEGAL. The trace itself. §4.3: "Aiming does not use this interface. World-space
 * aiming is a standard UE collision trace against the rendered mesh. It returns an FHitResult,
 * involves no plugin type, and needs no backend method — so the T-101A trace path is legal
 * gameplay code and stays legal after the build-step-2 rewire. What changes at step 2 is what
 * happens AFTER the trace: the hit location becomes an edit request to the service instead of
 * a direct RemoveSphere call."
 *
 * This library is that sentence, in code. It holds no plugin type, no AVoxelWorld reference,
 * and no terrain state; it turns a look direction into an FTerrainEditRequest and hands it to
 * UTerrainService, which decides everything else.
 *
 * A function library rather than a component so that the Blueprint change is one node per
 * input event: an asset that references nothing but game types is far easier to keep honest
 * than one that must be checked for a plugin reference at every review.
 */
UCLASS()
class VOXELWORLD_API UTerrainInteractionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Traces from the pawn's view and asks the terrain service to edit at whatever it hit.
	 *
	 * Defaults reproduce the T-101A dig exactly — a camera-forward line trace at 1000 uu with
	 * a 200 uu brush — so the rewire is testable as "the same dig, through the service", with
	 * the routing as the only variable.
	 *
	 * Returns false, with a reason in the receipt, when the trace misses or the service
	 * refuses. On a client it returns false with NoAuthority: build step 3 replaces that with
	 * a ServerRequestEdit RPC. Refusing is deliberate — letting a client edit its own copy is
	 * the client-authoritative shortcut AGENTS.md §4 forbids.
	 */
	UFUNCTION(BlueprintCallable, Category = "Terrain", meta = (DefaultToSelf = "Pawn", AdvancedDisplay = "TraceDistanceCm,ToolId"))
	static bool RequestTerrainEditFromView(
		APawn* Pawn,
		ETerrainEditKind Kind,
		FTerrainEditReceipt& OutReceipt,
		double RadiusCm = 200.0,
		double TraceDistanceCm = 1000.0,
		int32 ToolId = 0);

	/**
	 * Asks the service to edit at an explicit world location. The aiming half is the caller's.
	 * Same authority rules as above.
	 */
	UFUNCTION(BlueprintCallable, Category = "Terrain", meta = (WorldContext = "WorldContextObject"))
	static bool RequestTerrainEditAt(
		const UObject* WorldContextObject,
		ETerrainEditKind Kind,
		const FVector& WorldLocation,
		FTerrainEditReceipt& OutReceipt,
		double RadiusCm = 200.0,
		int32 SourceId = 0,
		int32 ToolId = 0);

	/** The world's terrain service, or null. Exposed so Blueprints never construct one. */
	UFUNCTION(BlueprintCallable, Category = "Terrain", meta = (WorldContext = "WorldContextObject"))
	static UTerrainService* GetTerrainService(const UObject* WorldContextObject);
};

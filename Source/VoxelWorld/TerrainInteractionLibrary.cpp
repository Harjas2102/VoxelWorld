// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainInteractionLibrary.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "WorldCollision.h"

UTerrainService* UTerrainInteractionLibrary::GetTerrainService(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UTerrainService>() : nullptr;
}

bool UTerrainInteractionLibrary::RequestTerrainEditAt(
	const UObject* WorldContextObject,
	ETerrainEditKind Kind,
	const FVector& WorldLocation,
	FTerrainEditReceipt& OutReceipt,
	double RadiusCm,
	int32 SourceId,
	int32 ToolId)
{
	OutReceipt = FTerrainEditReceipt();

	UTerrainService* Service = GetTerrainService(WorldContextObject);
	if (!Service)
	{
		OutReceipt.Rejection = ETerrainEditRejection::NotReady;
		return false;
	}

	FTerrainEditRequest Request;
	Request.Kind = Kind;
	Request.WorldLocation = WorldLocation;
	Request.RadiusCm = RadiusCm;
	Request.SourceId = SourceId;
	Request.ToolId = ToolId;
	Request.MaterialId = 0;   // material identity is K9 / build step 6

	// Every decision from here on is the service's: quantisation, bounds, work limits,
	// sequencing and revisions. Gameplay states an intent and reads a receipt.
	return Service->RequestEdit(Request, OutReceipt);
}

bool UTerrainInteractionLibrary::RequestTerrainEditFromView(
	APawn* Pawn,
	ETerrainEditKind Kind,
	FTerrainEditReceipt& OutReceipt,
	double RadiusCm,
	double TraceDistanceCm,
	int32 ToolId)
{
	OutReceipt = FTerrainEditReceipt();

	if (!Pawn)
	{
		OutReceipt.Rejection = ETerrainEditRejection::BadRequest;
		return false;
	}
	UWorld* World = Pawn->GetWorld();
	if (!World)
	{
		OutReceipt.Rejection = ETerrainEditRejection::NotReady;
		return false;
	}

	// View point: the player camera when there is one, the pawn's own view otherwise, so this
	// works for an AI or a headless test as well as for a local player.
	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	if (const APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
	{
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	else
	{
		Pawn->GetActorEyesViewPoint(ViewLocation, ViewRotation);
	}

	const FVector TraceEnd = ViewLocation + ViewRotation.Vector() * FMath::Max(1.0, TraceDistanceCm);

	// A plain Visibility trace against the rendered mesh. T-101A found simple collision is
	// enough — "Trace Complex was never needed" — and this whole path is deliberately
	// unchanged by the rewire so that a failed dig cannot be blamed on new aiming code.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TerrainEditTrace), /*bTraceComplex*/ false, Pawn);
	Params.AddIgnoredActor(Pawn);

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, ViewLocation, TraceEnd, ECC_Visibility, Params))
	{
		// Nothing under the crosshair. Not a rejection by the service — there was no request.
		OutReceipt.Rejection = ETerrainEditRejection::BadRequest;
		return false;
	}

	return RequestTerrainEditAt(Pawn, Kind, Hit.ImpactPoint, OutReceipt, RadiusCm,
		/*SourceId*/ 0, ToolId);
}

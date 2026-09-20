// Copyright VoxelWorld. Owner-only PlayerController transport (§4.4/4.8).
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TerrainEdit.h"
#include "TerrainTypes.h"
#include "TerrainStreamComponent.generated.h"

USTRUCT()
struct FTerrainChunkRevision
{
	GENERATED_BODY()
	UPROPERTY() FIntVector Key = FIntVector::ZeroValue;
	UPROPERTY() uint32 Before = 0;
	UPROPERTY() uint32 After = 0;
};

USTRUCT()
struct FTerrainSessionDescriptor
{
	GENERATED_BODY()
	UPROPERTY() int32 Protocol = 1;
	UPROPERTY() int32 Seed = 0;
	UPROPERTY() int32 Generator = 0;
	UPROPERTY() float VoxelSize = 0;
	UPROPERTY() FVector Origin = FVector::ZeroVector;
	UPROPERTY() FName Backend;
};

UCLASS()
class TERRAINCORE_API UTerrainStreamComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UTerrainStreamComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;
	bool SubmitEdit(FTerrainEditRequest Request, FTerrainEditReceipt& Receipt);
	UFUNCTION(Server,Reliable) void ServerRequestEdit(const FTerrainEditRequest& Request);
	UFUNCTION(Client,Reliable) void ClientEditReceipt(const FTerrainEditReceipt& Receipt);
	UFUNCTION(Client,Reliable) void ClientSession(const FTerrainSessionDescriptor& Descriptor);
	UFUNCTION(Server,Reliable) void ServerReady(bool Observer);
	UFUNCTION(Client,Reliable) void ClientPristine(const TArray<FIntVector>& Keys);
	UFUNCTION(Server,Reliable) void ServerAckPristine(const TArray<FIntVector>& Keys);
	UFUNCTION(Client,Reliable) void ClientApplyOp(const TArray<uint8>& Bytes, const TArray<FTerrainChunkRevision>& Revisions);
	UFUNCTION(Server,Reliable) void ServerRequestResync(const TArray<FIntVector>& Keys);

	// Development harness RPCs are inert unless the server explicitly enables TerrainMPTest.
	UFUNCTION(Client,Reliable) void ClientBeginTest(int32 Index, float Duration, bool Observer);
	UFUNCTION(Server,Reliable) void ServerTestDone();
	UFUNCTION(Client,Reliable) void ClientVerifyTest(const TArray<FIntVector>& Keys);
	UFUNCTION(Server,Reliable) void ServerTestHashes(const TArray<uint64>& Hashes, int32 Applied, int32 Failures);

	uint32 SourceId = 0; // Assigned on server, never taken from a client.
	bool bReady = false, bObserver = false, bTestDone = false, bTestReported = false;
	TSet<FTerrainChunkKey> Subscribed, PendingPristine;
	TMap<FTerrainChunkKey,uint32> DeliveredRevisions;
	FTerrainEditReceipt LastReceipt;
	int32 ReceivedOps = 0, ApplyFailures = 0;
private:
	FTerrainSessionDescriptor Session;
	bool bHasSession = false, bSentReady = false, bRunningTest = false;
	int64 NextRequestId = 1;
	int32 TestIndex = 0, TestEdits = 0;
	double TestEnd = 0, NextTestEdit = 0;
};

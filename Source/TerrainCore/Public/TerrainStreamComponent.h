// Copyright VoxelWorld. Owner-only PlayerController transport (§4.4/4.8).
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TerrainEdit.h"
#include "TerrainTypes.h"
#include "TerrainChunkSnapshot.h"
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

/** One fragment of a modified chunk's snapshot (P-008). See TerrainChunkSnapshot.h. */
USTRUCT()
struct FTerrainSnapshotFragment
{
	GENERATED_BODY()
	UPROPERTY() FIntVector Key = FIntVector::ZeroValue;
	UPROPERTY() uint32 Generation = 0;
	UPROPERTY() uint32 Rev = 0;
	UPROPERTY() uint8 Index = 0;
	UPROPERTY() uint8 Count = 0;
	UPROPERTY() int32 TotalBytes = 0;
	UPROPERTY() TArray<uint8> Bytes;
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
	// P-008: modified-chunk sync. Travels on the same reliable, ordered channel as ClientApplyOp,
	// which is the whole of the DEF-3 argument -- a snapshot taken between two commits arrives
	// before every op committed after it.
	UFUNCTION(Client,Reliable) void ClientChunkSnapshot(const FTerrainSnapshotFragment& Fragment);
	UFUNCTION(Server,Reliable) void ServerAckSnapshot(FIntVector Key, uint32 Generation, bool bApplied);
	/** P-010: the owner's settled balances, sent only after the ledger committed them. */
	UFUNCTION(Client,Reliable) void ClientInventory(const TArray<FTerrainYield>& Balances);
	TArray<FTerrainYield> LastInventory;

	// Development harness RPCs are inert unless the server explicitly enables TerrainMPTest.
	UFUNCTION(Client,Reliable) void ClientBeginTest(int32 Index, float Duration, bool Observer);
	UFUNCTION(Server,Reliable) void ServerTestDone();
	UFUNCTION(Client,Reliable) void ClientVerifyTest(const TArray<FIntVector>& Keys);
	UFUNCTION(Server,Reliable) void ServerTestHashes(const TArray<uint64>& Hashes, int32 Applied, int32 Failures,
		int32 Snapshots, int32 Dropped);

	uint32 SourceId = 0; // Assigned on server, never taken from a client.
	bool bReady = false, bObserver = false, bTestDone = false, bTestReported = false;
	TSet<FTerrainChunkKey> Subscribed, PendingPristine;
	TMap<FTerrainChunkKey,uint32> DeliveredRevisions;

	// Server side, P-008. A snapshot in flight has been sent and not yet acknowledged; it counts
	// as subscribed for op relevance, because the client will be synced by the time any later op
	// arrives. Queued keys are waiting for byte budget, nearest first.
	struct FSnapshotInFlight { uint32 Generation = 0; int32 Bytes = 0; };
	TMap<FTerrainChunkKey,FSnapshotInFlight> Syncing;
	TArray<FTerrainChunkKey> SnapshotQueue;
	int32 SnapshotBytesInFlight = 0;
	uint32 NextSyncGeneration = 1;

	FTerrainEditReceipt LastReceipt;
	int32 ReceivedOps = 0, ApplyFailures = 0, SnapshotsApplied = 0, DroppedOps = 0;
private:
	FTerrainSnapshotAssembler Assembler;
	FTerrainSessionDescriptor Session;
	bool bHasSession = false, bSentReady = false, bRunningTest = false;
	int64 NextRequestId = 1;
	int32 TestIndex = 0, TestEdits = 0;
	double TestEnd = 0, NextTestEdit = 0;
};

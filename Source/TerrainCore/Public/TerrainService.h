// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ITerrainBackend.h"
#include "ITerrainDensityField.h"
#include "TerrainRevisionIndex.h"
#include "TerrainEdit.h"
#include "TerrainEditQueue.h"
#include "TerrainStreamComponent.h"
#include "TerrainService.generated.h"

class UTerrainSettings;

/** World-lifetime states from ARCHITECTURE §4.5.1; transitions are game-thread only. */
enum class ETerrainServiceState : uint8
{
	Uninitialised,
	Ready,
	Draining,
	TornDown,
};

/**
 * UTerrainService — the authoritative terrain service (ARCHITECTURE.md §4.1, §4.4).
 *
 * A UWorldSubsystem, not an actor: fork K7, ruled at CP-005 by D-024 — "the service is
 * authority, not a thing in the world". It exists on both server and client; HasAuthority
 * gates the authoritative half.
 *
 * Step 3 owns server admission, a bounded serial queue, connection-owned transport,
 * revision-checked client replay and distance subscriptions. Gameplay submits through
 * UTerrainStreamComponent. Persistence, modified-chunk catch-up, material/yield and
 * collision readiness remain later build steps.
 *
 * Public queries and lifecycle calls are game-thread-only.
 */
UCLASS()
class TERRAINCORE_API UTerrainService : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** False outside an initialized game world, or on a client. */
	bool HasAuthority() const;

	/** Zero for unseen keys or outside this subsystem's initialized lifetime. */
	FTerrainRev GetRevision(const FTerrainChunkKey& Key) const;

	/** True once a backend has been created and initialized for this world. */
	UFUNCTION(BlueprintCallable, Category = "Terrain")
	bool IsBackendReady() const { return IsInGameThread() && State == ETerrainServiceState::Ready && Backend.IsValid(); }

	/** The configured backend module name, whether or not it loaded. Diagnostics only. */
	UFUNCTION(BlueprintCallable, Category = "Terrain")
	FName GetBackendName() const { return BackendName; }

	/** Standalone development diagnostics only. Gameplay uses the owning stream's SubmitEdit. */
	UFUNCTION(BlueprintCallable, Category = "Terrain")
	bool RequestEdit(const FTerrainEditRequest& Request, FTerrainEditReceipt& OutReceipt);
	uint32 RegisterStream(UTerrainStreamComponent* Stream);
	void UnregisterStream(uint32 Id);
	bool SubmitPlayerEdit(UTerrainStreamComponent* Stream, const FTerrainEditRequest& Request, FTerrainEditReceipt& Receipt);
	bool ApplyReplicatedOp(const TArray<uint8>& Bytes, const TArray<FTerrainChunkRevision>& Revisions);
	bool AcceptPristine(const TArray<FIntVector>& Keys);
	uint64 HashChunk(const FTerrainChunkKey& Key) const;
	bool IsMultiplayerTest() const;
#if !UE_BUILD_SHIPPING
	void RunAdapterChecks();
#endif
	void ReceiveTestHashes(UTerrainStreamComponent* Stream, const TArray<uint64>& Hashes, int32 Applied, int32 Failures);

	/**
	 * Streaming interest (§7.4). Handles are service-owned; 0 means "not acquired" and is
	 * never a valid handle. Acquire returns 0 while there is no backend — callers retry.
	 */
	uint32 AcquireStreamingInterest(const FVector& WorldLocation, double RadiusCm, bool bCollision, bool bRender);
	void UpdateStreamingInterest(uint32 InterestId, const FVector& WorldLocation);
	void ReleaseStreamingInterest(uint32 InterestId);
	int32 GetStreamingInterestCount() const { return Interests.Num(); }

	/** Coordinate policy, owned by the game (§8.1) and read from UTerrainSettings. */
	float GetVoxelSizeCm() const;
	FTransform GetTerrainOrigin() const;

	/** Read-only access for gameplay questions a collision trace cannot answer (§4.3). */
	bool QueryPoint(const FIntVector& VoxelPos, FTerrainPointSample& OutSample) const;

private:
	/** Creates and initializes the configured backend. Idempotent; logs every failure path. */
	void CreateBackend(UWorld& InWorld);

	/** Shuts the backend down and drops every interest. Safe to call twice. */
	void DestroyBackend();
	void OnWorldBeginTearDown(UWorld* World);

	ETerrainServiceState State = ETerrainServiceState::Uninitialised;
	bool bDestroyingBackend = false;
	FDelegateHandle WorldTearDownHandle;
	FTimerHandle ServiceTickHandle;
	void TickService();
	void RefreshSubscriptions(UTerrainStreamComponent& Stream);
	FTerrainQueueCallbacks QueueCallbacks();
	ETerrainEditRejection ValidateOp(const FTerrainOp& Op, const FTerrainSourceState& Source) const;
	ETerrainEditRejection QuantiseRequest(const FTerrainEditRequest& Request, FTerrainOp& Op) const;
	void BroadcastCommit(const FTerrainOp& Op, const FTerrainEditResult& Result);
	FTerrainEditQueue EditQueue;
	TMap<uint32,TWeakObjectPtr<UTerrainStreamComponent>> Streams;
	uint32 NextSourceId = 2;
	int64 NextAdminRequest = 1;
	FTerrainEditReceipt LastAdminReceipt;
	double NextSubscriptionUpdate = 0;
	TSet<FTerrainChunkKey> ResyncRequired;
	void TickMultiplayerTest();
	bool bMPStarted = false, bMPVerifying = false, bMPFinished = false;
	double MPStartTime = 0;
	TArray<FTerrainChunkKey> MPKeys;
	TArray<uint64> MPHashes;
	int32 MPReports = 0, MPFailures = 0;

	/**
	 * Metadata-only helper for the authoritative commit path. Does not apply terrain edits.
	 * Returns false off the game thread or without authority, without mutation.
	 */
	bool TryAdvanceRevisions(TConstArrayView<FTerrainChunkKey> AffectedChunks);

	TUniquePtr<FTerrainRevisionIndex> RevisionIndex;

	/** The one backend instance for this world. Owned here; released in Deinitialize. */
	TUniquePtr<ITerrainBackend> Backend;

	/** Immutable world field; async plugin generator instances retain shared lifetime
     * after service/backend teardown, without waiting on meshing or collision workers. */
	TSharedPtr<const ITerrainDensityField, ESPMode::ThreadSafe> DensityField;

	/**
	 * Copied from settings at backend creation, so a mid-session config edit cannot make the
	 * service quantise against a grid the backend was not built on.
	 */
	FTerrainBackendInit ActiveInit;

	FName BackendName;

	/** Live interests by handle, so Release reaches the backend with the right id. */
	TMap<uint32, FTerrainStreamingInterest> Interests;

	/** Monotonic, never reused within a session. Starts at 1; 0 means "no interest". */
	uint32 NextInterestId = 1;

	/** Global sequence, assigned at commit (§4.4). Starts at 1; 0 means "no operation". */
	FTerrainOpSeq NextOpSeq = 1;

#if WITH_DEV_AUTOMATION_TESTS
	friend class FTerrainRevisionMonotonicTest;
	friend class FTerrainServiceLifecycleTest;
	friend class FTerrainReplayValidationTest;
#endif
};

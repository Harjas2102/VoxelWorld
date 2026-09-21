// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ITerrainBackend.h"
#include "ITerrainDensityField.h"
#include "TerrainRevisionIndex.h"
#include "TerrainEdit.h"
#include "TerrainEditQueue.h"
#include "TerrainCommitJournal.h"
#include "TerrainCheckpoint.h"
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
	bool IsBackendReady() const { return IsInGameThread() && State == ETerrainServiceState::Ready && Backend.IsValid() && !bStorageFaulted; }

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

	/**
	 * Points the commit path at a journal, or clears it with null.
	 *
	 * The service does not own it and does not open it; the caller's object must outlive the
	 * service's use of it. Set before any edit is admitted -- attaching a journal to a world
	 * that has already been edited would produce a journal that does not describe its world.
	 */
	void SetCommitJournal(ITerrainCommitJournal* InJournal) { CommitJournal = InJournal; }

	/** True once a commit could not be made durable. Admission is closed; restart to clear. */
	bool IsStorageFaulted() const { return bStorageFaulted; }

	/** The world store this session is recording into, or null when nothing is being saved. */
	const FTerrainWorldStore* GetWorldStore() const { return WorldStore.Get(); }
	ETerrainEditRejection QuantiseRequest(const FTerrainEditRequest& Request, FTerrainOp& Op) const;
	/**
	 * P-003 §2 steps 2 and 3: durably record the operation, THEN advance and broadcast.
	 *
	 * Returns false when the record could not be made durable. The backend has already
	 * mutated at that point, so the world holds a change that is neither durable nor
	 * published; the service marks itself storage-faulted and stops admitting work, and
	 * recovery is a restart from disk.
	 */
	bool CommitOp(const FTerrainOp& Op, const FTerrainEditResult& Result,
	              const FTerrainCommitIdentity& Identity);
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

	/**
	 * Where committed operations are made durable, or null.
	 *
	 * Null means NOTHING IS PERSISTED and every edit is lost at shutdown -- which is the
	 * state the game shipped in through build step 3, and which every existing test relies
	 * on. Wiring a journal is what turns this service from "authoritative" into
	 * "authoritative and remembered"; not owning one is a valid configuration, not a bug.
	 */
	ITerrainCommitJournal* CommitJournal = nullptr;

	/**
	 * Opens or creates this world's durable history and replays it onto the fresh backend.
	 *
	 * Called once, immediately after the backend becomes Ready and before the queue can admit
	 * anything, because replay requires a backend nothing has edited yet. Server only.
	 *
	 * Recovery failure closes terrain access until restart, as required by P-003. A partial
	 * restore must never be served as a healthy fresh world or accept unsaved edits.
	 */
	void OpenWorldStore(UWorld& InWorld);

	/** Detaches the journal and releases the store. Safe to call when nothing was opened. */
	void CloseWorldStore();

	/**
	 * Captures a checkpoint when enough has changed and nothing is executing.
	 *
	 * Called from the tick, never from inside the pump: capture reads the world as it stands
	 * and is only a consistent cut if nothing is part-way through an operation. P-003 section 4
	 * also requires a cut to start between transactions, which "the queue is empty" satisfies
	 * more strictly than it needs to.
	 */
	void MaybeCaptureCheckpoint();

	/** Handles a capture that has just ended, successfully or not. */
	void FinishCapture();

	/**
	 * Chunks changed since the last checkpoint.
	 *
	 * P-003 section 4's dirty-key set, without its banks: with a synchronous capture there is
	 * no frozen bank to keep separate from a current one, because no edit happens during a
	 * capture. The hard bound still applies -- admission closes before the set can grow past
	 * it when capture is enabled. Journal-only mode has no capture budget gate; its
	 * history and residency remain unbounded prototype costs.
	 */
	TMap<FTerrainChunkKey, FTerrainOpSeq> DirtyChunks;
	FTerrainResidencyPins LivePersistencePins{0x53000000};

	/**
	 * Set when a capture failed, to stop this session attempting another.
	 *
	 * A checkpoint failure is the LEAST severe storage failure there is: the previous root is
	 * untouched, the journal is intact, and the world remains fully recoverable -- it simply
	 * has more to replay next time. Closing terrain access over it would cost the player their
	 * session for a fault that cost nothing. But retrying is not an option either, because the
	 * trigger would still be satisfied and a synchronous capture would run EVERY TICK.
	 *
	 * So: try once, say so once, and keep playing without checkpoints.
	 */
	bool bCheckpointDisabled = false;

	/**
	 * The in-progress checkpoint, if any (P-003 §4, DEF-2).
	 *
	 * Capture is no longer one synchronous call. The cut is taken once, the chunks are read and
	 * encoded a few per frame, and edits keep flowing throughout -- with any chunk an edit is
	 * about to change captured first, from its pre-edit state, by `NoticeWrite`.
	 */
	FTerrainCapturePump CapturePump;

	TUniquePtr<FTerrainPlatformStorageDevice> StorageDevice;
	TUniquePtr<FTerrainWorldStore>            WorldStore;
	TUniquePtr<FTerrainWorldStoreJournal>     WorldJournal;

	/**
	 * Set when a commit could not be made durable after the backend had already mutated.
	 *
	 * P-003 §2 calls this an uncertain storage fault and requires closing admission. Once
	 * set, every request is refused with `ShuttingDown` and only a restart clears it: the
	 * in-memory world can no longer be shown to match what is on disk, and continuing to
	 * serve edits from it would be serving a world nobody can get back.
	 */
	bool bStorageFaulted = false;

#if WITH_DEV_AUTOMATION_TESTS
	friend class FTerrainRevisionMonotonicTest;
	friend class FTerrainServiceLifecycleTest;
	friend class FTerrainCheckpointTest;
	friend class FTerrainReplayValidationTest;
	friend class FTerrainCommitJournalTest;
#endif
};

// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ITerrainBackend.h"
#include "ITerrainDensityField.h"
#include "TerrainRevisionIndex.h"
#include "TerrainService.generated.h"

class UTerrainSettings;

/** What a request asks the terrain to become. Sphere shapes only until DEF-5 is closed. */
UENUM(BlueprintType)
enum class ETerrainEditKind : uint8
{
	/** Dig. FTerrainOp Kind Remove. */
	Remove,
	/** Place material. FTerrainOp Kind Add. */
	Add,
};

/**
 * Why a request did not become an operation.
 *
 * An enum and not a string because ARCHITECTURE.md §4.4 sends this to the client as
 * "ClientEditRejected (ReqId + reason enum)" at build step 3. Naming the reasons now means
 * step 3 adds a transport, not a vocabulary.
 */
UENUM(BlueprintType)
enum class ETerrainEditRejection : uint8
{
	None,
	/** Not the authority. Build step 3 gives the client a ServerRequestEdit RPC instead. */
	NoAuthority,
	/** No backend yet, or the world has not begun play. */
	NotReady,
	/** Non-finite input, or an unsupported kind. */
	BadRequest,
	/** Radius above UTerrainSettings::MaxEditRadiusCm. */
	RadiusTooLarge,
	/** The request does not quantise, or its footprint leaves the world bounds. */
	OutOfBounds,
	/** Footprint above MaxVoxelsPerOp. Splitting into sub-ops is step 3 work (DEF-7). */
	TooLarge,
	/** A chunk in the footprint is at the maximum revision. Checked BEFORE mutating. */
	RevisionExhausted,
	/** The backend refused or failed the operation. No mutation is claimed either way. */
	BackendFailed,
};

/** What the caller learns about a request. Metadata only: terrain state is the backend's. */
USTRUCT(BlueprintType)
struct FTerrainEditReceipt
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	bool bApplied = false;

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	ETerrainEditRejection Rejection = ETerrainEditRejection::None;

	/**
	 * Global sequence assigned AT COMMIT (§4.4). Zero on a rejected request, which is what
	 * keeps the journal free of holes: a rejection never consumes a sequence number.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	int64 OpSeq = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	int32 ChunksAffected = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	int64 VoxelsTouched = 0;
};

/** A world-space ask, before quantisation. The op is what the SERVER makes of it (§4.3). */
USTRUCT(BlueprintType)
struct FTerrainEditRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	ETerrainEditKind Kind = ETerrainEditKind::Remove;

	/** World space, in centimetres. Quantised once, by the server, at validation time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "0"))
	double RadiusCm = 200.0;

	/**
	 * Tool/tier. Affects yield, not geometry — and it is client input, so step 3 validates
	 * ownership, equip state, cooldown and charge before trusting it (§4.4, DEF-7).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	int32 ToolId = 0;

	/** PlayerId or MachineId. Zero until those identities exist. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	int32 SourceId = 0;

	/** Game material id for Add. Ignored by Remove. Bounded to a uint16 on the wire. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "0", ClampMax = "65535"))
	int32 MaterialId = 0;
};

/**
 * UTerrainService — the authoritative terrain service (ARCHITECTURE.md §4.1, §4.4).
 *
 * A UWorldSubsystem, not an actor: fork K7, ruled at CP-005 by D-024 — "the service is
 * authority, not a thing in the world". It exists on both server and client; HasAuthority
 * gates the authoritative half.
 *
 * WHAT T-113 (build step 2) ADDS, AND WHAT IT DELIBERATELY DOES NOT.
 *
 * Adds: backend ownership by config name, the streaming-interest ledger, and RequestEdit —
 * the single gameplay entry point through which every terrain change now passes. Step 2's
 * stated end state is "digging works as today, through the service, server-authoritative in
 * standalone", and that is the whole of the claim.
 *
 * Does NOT add, because §9 binds these to later steps and to unresolved defects:
 *   - ServerRequestEdit / ClientApplyOp and the subscription set (step 3; DEF-4, DEF-5, DEF-7)
 *   - commit-time revalidation, request dedup, split operations   (step 3; DEF-7)
 *   - the journal, snapshots, and any durability at all           (step 4; DEF-1, DEF-2, K5)
 *   - yield settlement and tool efficiency                        (step 6; DEF-6, K9)
 *   - reach, permission, rate limit and self-clearance validation (step 3; DEF-7, DEF-8)
 *
 * The §4.4 sequence diagram is therefore only partly implemented here, and RequestEdit's
 * comment says which parts. Server authority is NOT proven by this class until step 3
 * exercises the multiplayer route; standalone is the narrower result step 2 may record.
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
	bool IsBackendReady() const { return Backend.IsValid(); }

	/** The configured backend module name, whether or not it loaded. Diagnostics only. */
	UFUNCTION(BlueprintCallable, Category = "Terrain")
	FName GetBackendName() const { return BackendName; }

	/**
	 * The one gameplay entry point for changing terrain (§4.4, D-011).
	 *
	 * Validates what step 2 is allowed to validate, quantises ONCE into integer voxel space,
	 * builds the FTerrainOp, executes it through the backend, and only then assigns OpSeq and
	 * bumps the revision of every affected chunk.
	 *
	 * Ordering note: revision exhaustion is checked over the op's PREDICTED footprint before
	 * the backend is called, so "mutated terrain that no revision records" cannot arise from
	 * overflow. That is narrower than DEF-7's no-change-or-committed-result invariant, which
	 * stays open and bound to step 3: a backend that fails halfway still reports one boolean,
	 * and nothing here can tell that apart from a clean refusal.
	 *
	 * Returns bApplied. Never asserts on bad input; every refusal names a reason.
	 */
	UFUNCTION(BlueprintCallable, Category = "Terrain")
	bool RequestEdit(const FTerrainEditRequest& Request, FTerrainEditReceipt& OutReceipt);

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

	/**
	 * Metadata-only helper for the authoritative commit path. Does not apply terrain edits.
	 * Returns false off the game thread or without authority, without mutation.
	 */
	bool TryAdvanceRevisions(TConstArrayView<FTerrainChunkKey> AffectedChunks);

	TUniquePtr<FTerrainRevisionIndex> RevisionIndex;

	/** The one backend instance for this world. Owned here; released in Deinitialize. */
	TUniquePtr<ITerrainBackend> Backend;

	/**
	 * The world's shape (T-108, §4.6). OWNED HERE and only LENT to the backend: AR-2 records
	 * that FTerrainBackendInit borrows the field until Shutdown, so this must outlive the
	 * backend and is destroyed only after Shutdown has returned. The plugin samples it from
	 * mesher worker threads, which is why ITerrainDensityField forbids mutable state.
	 */
	TUniquePtr<ITerrainDensityField> DensityField;

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
#endif
};

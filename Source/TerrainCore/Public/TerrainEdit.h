// Copyright VoxelWorld.
#pragma once
#include "CoreMinimal.h"
#include "TerrainEdit.generated.h"

/**
 * What a request asks the terrain to become.
 *
 * DEF-5 is resolved (ARCHITECTURE.md §4.10.1) and the operation set is CLOSED at Remove, Add and
 * Paint. `Flatten` and `Smooth` are permanently reserved and refused — they were never given
 * plane, strength, iteration or falloff semantics, and inventing some is what the defect
 * existed to prevent. They keep their `ETerrainOpKind` enumerators because the 58-byte wire
 * encoding is permanent, but nothing may issue them.
 *
 * `Paint` is in the ruled set and is deliberately absent HERE: a game material id has no plugin
 * material index to land on until fork K9 at build step 6, so a Paint request would be a
 * request no backend could honour. It joins this enum with K9, not before.
 *
 * Sphere shapes only. Box shapes exist in `FTerrainOp` for the split rule (§4.11.7) and arrive
 * with the machine excavation that needs them.
 */
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
	/**
	 * Footprint above MaxVoxelsPerOp.
	 *
	 * §4.11.7 rules that only BOX ops split; an over-cap sphere is rejected here and is never
	 * approximated by smaller spheres, because a sphere has no exact partition and a request
	 * that produced different terrain depending on whether it crossed a cap would be a
	 * determinism bug wearing a performance feature's clothes.
	 */
	TooLarge,
	/** A chunk in the footprint is at the maximum revision. Checked BEFORE mutating. */
	RevisionExhausted,
	/**
	 * The backend refused or failed the operation.
	 *
	 * §4.11.6: a false return from ApplyOp means NOTHING CHANGED — not "something may have
	 * changed". Backends reach that by staging, or by pre-validating the whole footprint so the
	 * kernel call has no precondition left to fail on.
	 */
	BackendFailed,

	// ---- DEF-7 / §4.11.8. Each exists because a client that cannot tell it from its
	// neighbour will do the wrong thing with it. ----

	/** The world is going away (§4.5.1 Draining). Stop; do not retry. Distinct from NotReady. */
	ShuttingDown,

	/** Global or per-source queue depth cap hit. Back off and retry later. */
	QueueFull,

	/** Request identity older than the per-connection dedup ring. Retry with a NEW RequestId. */
	StaleRequest,

	/** Passed admission, failed the commit-time recheck. Re-check local state, then retry. */
	Revalidation,
	OutOfReach,
	ToolUnavailable,
	PermissionDenied,
	NotResident,
	RateLimited,
	UnsafePlacement,
};

/** What the caller learns about a request. Metadata only: terrain state is the backend's. */
USTRUCT(BlueprintType)
struct FTerrainEditReceipt
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	bool bApplied = false;

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	bool bQueued = false;

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	int64 RequestId = 0;

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

	/** Unique and increasing within the owning connection. Zero is never a network request. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	int64 RequestId = 0;
};

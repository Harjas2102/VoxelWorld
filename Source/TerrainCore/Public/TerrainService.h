// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TerrainRevisionIndex.h"
#include "TerrainService.generated.h"

/**
 * UTerrainService — the authoritative terrain service (ARCHITECTURE.md §4.1, §4.4).
 *
 * A UWorldSubsystem, not an actor: fork K7, ruled at CP-005 by D-024 — "the service is
 * authority, not a thing in the world". It exists on both server and client; HasAuthority
 * gates the authoritative half.
 *
 * T-112.3 owns in-memory revisions only. Backend execution, validation and global
 * sequencing arrive in later steps; journalling and yield remain later work.
 * Public queries and lifecycle calls are game-thread-only.
 */
UCLASS()
class TERRAINCORE_API UTerrainService : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** False outside an initialized game world, or on a client. */
	bool HasAuthority() const;

	/** Zero for unseen keys or outside this subsystem's initialized lifetime. */
	FTerrainRev GetRevision(const FTerrainChunkKey& Key) const;

private:
	/**
	 * Metadata-only helper for the later authoritative commit path. Does not apply
	 * terrain edits or assign OpSeq. No public gameplay mutation entry point yet.
	 * Returns false off the game thread or without authority, without mutation.
	 */
	bool TryAdvanceRevisions(TConstArrayView<FTerrainChunkKey> AffectedChunks);

	TUniquePtr<FTerrainRevisionIndex> RevisionIndex;

#if WITH_DEV_AUTOMATION_TESTS
	friend class FTerrainRevisionMonotonicTest;
#endif
};

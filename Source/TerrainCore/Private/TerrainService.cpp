// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainService.h"
#include "Engine/World.h"

void UTerrainService::Initialize(FSubsystemCollectionBase& Collection)
{
	check(IsInGameThread());
	// A repeated initialize must not erase this lifetime's revision history.
	if (RevisionIndex)
	{
		return;
	}
	RevisionIndex = MakeUnique<FTerrainRevisionIndex>();
	Super::Initialize(Collection);
}

void UTerrainService::Deinitialize()
{
	check(IsInGameThread());
	RevisionIndex.Reset();
	Super::Deinitialize();
}

bool UTerrainService::HasAuthority() const
{
	check(IsInGameThread());
	const UWorld* World = GetWorld();
	return RevisionIndex && World && World->bIsWorldInitialized
		&& World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

FTerrainRev UTerrainService::GetRevision(const FTerrainChunkKey& Key) const
{
	check(IsInGameThread());
	return RevisionIndex ? RevisionIndex->GetRevision(Key) : 0;
}

bool UTerrainService::TryAdvanceRevisions(TConstArrayView<FTerrainChunkKey> AffectedChunks)
{
	// Short circuit before touching UObject/world state from a worker thread.
	return IsInGameThread() && HasAuthority()
		&& RevisionIndex->TryBumpRevisions(AffectedChunks);
}

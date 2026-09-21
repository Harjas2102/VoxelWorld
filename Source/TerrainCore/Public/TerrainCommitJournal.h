// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "TerrainWorldStore.h"
#include "TerrainStreamComponent.h"
#include "TerrainSettlement.h"

/**
 * TerrainCommitJournal.h -- the seam between the live edit path and the journal (P-003 §2).
 *
 * P-003 §2 fixes the commit protocol and its order:
 *
 *   1. apply the bounded backend op;
 *   2. create the complete journal record and **append and durably flush it**;
 *   3. advance the committed sequence and **broadcast**.
 *
 * The load-bearing part is that step 3 follows step 2. A client that has been told an edit
 * happened must never be able to observe a world where it did not, and the only way to
 * guarantee that is to make the broadcast wait for the flush. Everything in this header
 * exists to put that ordering in one place where it can be read, tested and not reordered.
 *
 * WHY A SEAM. `UTerrainService` is a `UWorldSubsystem`; the store underneath is plain C++ over
 * a device that can be in-memory. Putting the journal behind an interface means the commit
 * ordering is testable headlessly, with injected storage faults, without a world, a backend or
 * a file system -- and that the service holds no file handle.
 */

/**
 * Durably records committed operations.
 *
 * `RecordCommit` returning false means the commit **must not be broadcast**. The caller has
 * already mutated the backend at that point, so the honest state is "this world's RAM holds a
 * change that is not durable and that nobody was told about" -- which P-003 §2 calls an
 * uncertain storage fault, and which is resolved by tearing the world down and recovering
 * from disk, never by pretending the edit happened.
 */
class TERRAINCORE_API ITerrainCommitJournal
{
public:
	virtual ~ITerrainCommitJournal() = default;

	/**
	 * Appends and durably flushes one committed operation.
	 *
	 * `ChangedRevisions` carries only the chunks the operation actually changed, with the
	 * revisions they held before and after. A zero-change commit passes an empty view and is
	 * legal: P-003 §2 keeps "a successful zero-change op may commit and consumes a sequence".
	 */
	virtual bool RecordCommit(const FTerrainOp& Op,
	                          const FTerrainEditResult& Result,
	                          const FTerrainCommitIdentity& Identity,
	                          TConstArrayView<FTerrainChunkRevision> ChangedRevisions) = 0;

	/** The highest sequence this journal has durably recorded. */
	virtual FTerrainOpSeq GetDurableHead() const = 0;
};

/**
 * Records commits into an `FTerrainWorldStore`'s journal.
 *
 * The store must already be open. This object does not own it, does not open it and does not
 * close it: the world store's lifetime is the caller's, which keeps "who owns the files"
 * answerable without reading this class.
 */
class TERRAINCORE_API FTerrainWorldStoreJournal final : public ITerrainCommitJournal
{
public:
	explicit FTerrainWorldStoreJournal(FTerrainWorldStore& InStore);

	virtual bool RecordCommit(const FTerrainOp& Op,
	                          const FTerrainEditResult& Result,
	                          const FTerrainCommitIdentity& Identity,
	                          TConstArrayView<FTerrainChunkRevision> ChangedRevisions) override;

	virtual FTerrainOpSeq GetDurableHead() const override;

	/**
	 * Whether the backend's physical material measurement can be trusted.
	 *
	 * **Defaults to false, and that is the honest value today.** The production adapter's
	 * transfer and hashes are density-only and its materials are zero (step-3 evidence), so a
	 * `Removed` list from it is not a measurement. P-003 §2 is explicit that the prototype
	 * "must not encode unknown as a measured zero", so with this false the record carries
	 * `PhysicalAvailability = Unavailable` and an **empty** list rather than a zero one, and a
	 * later reader can tell the difference. Set it true only when DEF-6 has defined what a
	 * measurement means and a backend actually produces one.
	 */
	bool bPhysicalMeasured = false;

	/**
	 * The settlement input of the record the last successful RecordCommit wrote: its OpSeq, the
	 * digest of the bytes actually appended, and the economy as recorded (P-010).
	 */
	FTerrainSettlementInput TakeLastSettlement() { return MoveTemp(LastSettlement); }

	/** The reason the last RecordCommit failed, for a caller that wants to log it. */
	const FTerrainStoreResult& GetLastError() const { return LastError; }

private:
	FTerrainWorldStore& Store;
	FTerrainStoreResult LastError;
	FTerrainSettlementInput LastSettlement;
};

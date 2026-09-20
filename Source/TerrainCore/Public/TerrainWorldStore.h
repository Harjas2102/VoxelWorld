// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "TerrainStorage.h"
#include "TerrainJournalWriter.h"
#include "TerrainPersistenceRecords.h"

/**
 * TerrainWorldStore.h -- a world directory: create it, open it, publish a checkpoint.
 *
 * The thing that finally composes the format, the device, the object store, the slot pairs
 * and the journal into something that can be pointed at a directory. Everything below it has
 * been tested in isolation since T-117; this is where those pieces first have to agree with
 * each other about one world.
 *
 * ORDER IS THE WHOLE JOB. Creating a world writes the base descriptor before anything that
 * binds to its digest, the checkpoint object before the root slot that names it, and the
 * segment header before the anchor that names it. Publishing a checkpoint writes the
 * descriptor object before the root slot, for the same reason: a root that names an object
 * which is not there yet is a root that cannot be used, and P-004 §12's containment argument
 * is precisely that such a root is never published in the first place.
 *
 * WHAT THIS IS NOT. There is no commit path, no capture pump, no settlement, no replay and no
 * retention. Opening a world validates its roots, its base and its journal head; it does NOT
 * perform P-003 §4's eager validation of the whole checkpoint closure, and it does not replay
 * anything. A world that opens here is a world whose *entry points* are intact, which is a
 * smaller claim than a world that is ready to play.
 */

/** What a world store knows once it is open. */
struct FTerrainWorldStoreState
{
	FTerrainPersistIdentity      Identity;
	FTerrainBaseDescriptor       Base;
	FTerrainRootSlot             Root;         // the selected root slot
	FTerrainCheckpointDescriptor Checkpoint;   // the descriptor that root names

	/**
	 * False when one root slot is damaged or missing.
	 *
	 * P-004 §8 requires reclamation to stop until redundancy is repaired: with one root left,
	 * deleting anything the surviving root does not reference removes the only copy of the
	 * evidence that would let the world be recovered if that root is next.
	 */
	bool bRootRedundancyIntact = false;

	/** Both root slots' states, so a caller can report which one is damaged and how. */
	FTerrainSlotState RootSlots[2];
};

/**
 * One world directory.
 *
 * Holds the journal writer, so a caller that has a store has exactly one journal and cannot
 * accidentally open a second one over the same files. Exclusive writer ownership is P-003 §5's
 * requirement; this class is where it will be enforced when the store gains a lease, and the
 * fact that it does not have one yet is stated in its Limits rather than implied away.
 */
class TERRAINCORE_API FTerrainWorldStore
{
public:
	explicit FTerrainWorldStore(ITerrainStorageDevice& InDevice);

	/**
	 * Creates a new world: directories, base descriptor, the empty G=0 checkpoint, both root
	 * slots, the first journal segment and both anchor slots -- then opens it.
	 *
	 * P-004 §8: "Initial world creation durably creates both slots and an empty G=0 checkpoint
	 * before any edit admission." The empty checkpoint is not a placeholder; it is the legal
	 * state of a world that has been created and never edited, and having it means the boot
	 * path has exactly one shape rather than two.
	 *
	 * Refuses if the directory already holds a base descriptor.
	 */
	FTerrainStoreResult Create(const FTerrainBaseDescriptor& Base,
	                           const FTerrainWorldId& World,
	                           const FTerrainStoreEpoch& Epoch,
	                           int64 UtcMillis);

	/**
	 * Opens an existing world.
	 *
	 * Reads the base descriptor (which verifies its own digest), then both root slots, then
	 * the checkpoint descriptor the selected root names, then the journal. Every step is
	 * identity-checked against the base: a file from another world or another store lineage is
	 * refused rather than read (P-003 §5).
	 */
	FTerrainStoreResult Open();

	/**
	 * Publishes a new checkpoint: the descriptor object first, then the root slot.
	 *
	 * The caller has already stored the index pages and chunk payloads the descriptor
	 * references -- this does not walk them. `Generation` is assigned here, one above the
	 * current root, because two writers choosing their own generations is how a store ends up
	 * unable to say which root is newer.
	 */
	FTerrainStoreResult PublishCheckpoint(const FTerrainCheckpointDescriptor& Checkpoint, int64 UtcMillis);

	bool IsOpen() const { return bOpen; }

	/**
	 * The device this world lives on.
	 *
	 * Exposed for replay, which has to enumerate the journal's segment chain. It is a
	 * deliberate widening of what the store shares and it is read-only in practice: anything
	 * that MUTATES this world goes through the store or the journal writer, so that the
	 * ordering rules stay in one place.
	 */
	ITerrainStorageDevice& GetDevice() const { return Device; }
	const FTerrainWorldStoreState& GetState() const { return State; }
	FTerrainFileObjectStore&       GetObjects() { return Objects; }
	const FTerrainFileObjectStore& GetObjects() const { return Objects; }

	/** Null until the world is open. */
	FTerrainJournalWriter*       GetJournal()       { return Journal.Get(); }
	const FTerrainJournalWriter* GetJournal() const { return Journal.Get(); }

private:
	FTerrainStoreResult StoreCheckpointObject(const FTerrainCheckpointDescriptor& Checkpoint,
	                                          FTerrainDigest& OutDigest, uint32& OutLength);

	ITerrainStorageDevice&  Device;
	FTerrainFileObjectStore Objects;
	FTerrainSlotPair        RootSlots;

	TUniquePtr<FTerrainJournalWriter> Journal;
	FTerrainWorldStoreState           State;
	bool                              bOpen = false;
};

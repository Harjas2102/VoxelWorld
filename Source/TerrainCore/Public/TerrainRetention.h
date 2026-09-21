// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "TerrainWorldStore.h"

/**
 * TerrainRetention.h -- reclaiming what no live checkpoint refers to (P-004 §8, DEF-9).
 *
 * THE PROBLEM THIS SOLVES. Every checkpoint writes a fresh payload object for each chunk it
 * captures, and path-copies a fresh page for every index node on the way to a changed leaf.
 * The previous checkpoint's versions of both become unreachable the moment a new root slot
 * names the new ones -- and nothing has ever deleted them. Measured at the 256-chunk trigger
 * that is **33 MB of payloads per checkpoint**, almost all of it superseded by the next one.
 * A world played for a week would be mostly garbage.
 *
 * WHAT IS RECLAIMED, and what is deliberately not. This is a mark-and-sweep over the object
 * store. It never touches `base.tobj`, the root slots or the journal: journal deletion requires
 * coverage by the older retained cut, live W and all active pins (P-003 section 5), and
 * trimming it is a separate job, worth roughly a thousandth of this one -- about 49 KB per
 * checkpoint interval against 33 MB.
 *
 * THE THREE RULES THAT MAKE IT SAFE. Each exists because breaking it destroys a world.
 *
 * 1. **Both root slots must be valid.** P-004 §8: *"with one root left, reclamation must stop
 *    until redundancy is repaired"*. Deleting what the surviving root does not reference
 *    removes the only evidence that would rebuild the world if that root is next to fail.
 *
 * 2. **Both root slots are roots of the mark.** Not just the current one. The slot pair *is*
 *    the recovery mechanism: if the newest checkpoint is later found damaged, the other slot's
 *    checkpoint is what the world falls back to, and a fallback whose payloads have been
 *    deleted is not a fallback. So the live set is the union of two checkpoints, and a
 *    generation survives exactly as long as a root slot still names it.
 *
 * 3. **No capture may be in flight.** A running capture has objects buffered in an open pack
 *    batch and index pages written but not yet named by any root. They are unreachable by
 *    construction and a sweep would be right to delete them and wrong to have run at all.
 *
 * WHAT PACKS COST HERE, and why compaction is not optional (P-004 §13.6). A packed object
 * cannot be deleted on its own, and index path-copying **shares** pages between generations by
 * design, so old packs almost never become entirely dead. The first working sweep over a
 * three-generation world reclaimed 176 bytes and stranded 757 KB. So partly dead storage is
 * **rewritten without its garbage** rather than waited on.
 *
 * HOW IT IS REWRITTEN, since P-005. Objects live in a fixed pool of pre-created containers. A
 * container holding dead bytes has its live objects copied into one frame in the active
 * container, flushed and read back, and only then is it truncated to zero. No name is created
 * or removed, so a power cut can at most undo the truncation and leave a duplicate -- which is
 * what lifts the R-015 gate. Before P-005, compaction wrote a replacement pack under a new name
 * and deleted the original; losing that new name could take objects both roots share.
 *
 * Objects still in pre-P-005 loose files or packs are migrated the same way: copied into a
 * container first, and the old files removed only after the copy is durable.
 *
 * STILL A DIAGNOSTIC (DEF-9). This is synchronous, on the game thread and scheduled by hand; it is
 * not the incremental off-thread collector with pins and an epoch protocol that P-003 §5
 * specifies. No backup, migration or sync consumer may rely on it. The service therefore still
 * requires an explicit experiment flag -- for that reason, no longer for durability.
 */

struct FTerrainRetentionStats
{
	/** Objects reachable from either root slot's checkpoint. */
	int32 LiveObjects = 0;

	/** Live objects copied out of pre-P-005 loose files and packs into a container. */
	int32 LegacyObjectsMigrated = 0;
	/** Pre-P-005 files removed after their live contents were durable in a container. */
	int32 LegacyFilesDeleted = 0;

	/** Whether the pass moved writing to an empty container so the old one could be compacted. */
	bool bRotated = false;

	int32 ContainersCompacted = 0;   // live objects copied out, then truncated to zero
	int32 ContainersKept = 0;        // non-empty and entirely live: nothing to do

	int32 ObjectsDropped = 0;

	int64 BytesReclaimed = 0;

	double Seconds = 0.0;
};

/**
 * Deletes every object no live checkpoint refers to.
 *
 * Refuses rather than guessing when the store is not in a state where deletion is provably
 * safe: a store that is not open, root redundancy that is not intact, or a capture in flight.
 * Refusing costs disk space; guessing costs the world.
 *
 * The mark walks each live checkpoint's descriptor, its whole index page tree, and every leaf's
 * payload digest. A page that cannot be read fails the whole pass **without deleting anything**
 * -- an incomplete mark is indistinguishable from a small live set, and sweeping on one would
 * delete live data.
 */
TERRAINCORE_API FTerrainStoreResult TerrainReclaimStore(
	FTerrainWorldStore& Store,
	FTerrainRetentionStats& OutStats);

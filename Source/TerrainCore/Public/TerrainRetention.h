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
 * WHAT PACKS COST HERE, and why compaction is not optional (P-004 §13.6). A loose object is
 * deleted individually. **A packed object cannot be.** A pack is removable only when nothing
 * live refers to *any* object inside it -- and that essentially never happens, because index
 * path-copying **shares** pages between generations by design, so an old pack keeps at least one
 * page the current checkpoint still references.
 *
 * That is not a theory. The first working sweep over a three-generation world deleted **0 of 3
 * packs** and reclaimed 176 bytes while leaving 757 KB of garbage in a 2.68 MB store. Packs made
 * capture sixty-five times faster (D-036) and made reclamation ineffective in the same stroke.
 *
 * So a partly dead pack is **rewritten without its dead objects** rather than waiting for it to
 * become entirely dead. The new pack is durable before the old is removed, and because objects
 * are content-addressed a crash in between leaves a byte-identical duplicate rather than a
 * contradiction, provided the device's namespace durability contract holds (R-015 remains open).
 * This is a synchronous diagnostic, not the incremental off-thread collector specified in
 * P-003 section 5. Production use also requires storage ownership and retention pins before
 * enabling backup, migration or sync consumers. The service requires an explicit experiment flag.
 */

struct FTerrainRetentionStats
{
	/** Objects reachable from either root slot's checkpoint. */
	int32 LiveObjects = 0;

	int32 LooseScanned = 0;
	int32 LooseDeleted = 0;

	int32 PacksScanned = 0;
	int32 PacksDeleted = 0;     // nothing in them was live
	int32 PacksCompacted = 0;   // rewritten without their dead objects
	int32 PacksKept = 0;        // entirely live, nothing to do

	int32 ObjectsDroppedFromPacks = 0;

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

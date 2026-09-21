// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "TerrainWorldStore.h"
#include "TerrainRevisionIndex.h"
#include "ITerrainBackend.h"

/**
 * TerrainJournalReplay.h -- rebuilding a world from its journal (P-003 §3, terrain pass).
 *
 * THIS IS THE FUNCTION PILLAR 1 RESTS ON. "The server remembers everything at next login" is
 * true exactly when a fresh backend, plus the journal, reproduces the world that was shut
 * down. Everything built before this increment was the machinery to record; this is the part
 * that reads it back and makes the record mean something.
 *
 * P-003 §3's terrain pass, in order: restore the checkpoint at G, materialize absent chunks
 * touched by replay from the exact recorded base, and apply whole ops `G < OpSeq <= H` once,
 * in global order, validating before/after revisions and results as it goes.
 *
 * Capture can advance G, limiting operations re-applied on restart. The current reader
 * still scans all retained journal files; startup I/O is not bounded until retention and
 * checkpoint-aware segment selection are implemented.
 *
 * Never replays an op per chunk (P-003 §3 forbids it): a multi-chunk operation is applied
 * once, whole, exactly as it was applied the first time.
 */

struct FTerrainReplayStats
{
	/** Records seen across every segment in the chain. */
	int32 RecordsRead = 0;
	/** Operations actually re-applied, i.e. those with `OpSeq > G`. */
	int32 OpsApplied = 0;
	/** Changed chunks after G, with their actual last committed sequence. Capture must inherit these. */
	TMap<FTerrainChunkKey, FTerrainOpSeq> DirtyChunks;

	FTerrainOpSeq FirstOpSeq = 0;
	FTerrainOpSeq LastOpSeq = 0;

	/** Segment IDs walked, ascending. */
	TArray<uint64> Segments;

	double Seconds = 0.0;
};

/**
 * Applies the journal to a freshly initialised backend.
 *
 * `Backend` must be initialised against the **same base** the store records; replaying onto a
 * different generator produces a world that is neither the old one nor a new one. The store's
 * base descriptor is what that identity is checked against by every object it reads, but this
 * function cannot check the backend itself -- ITerrainBackend exposes no base identity -- so
 * the caller is responsible for that and the limitation is stated rather than implied.
 *
 * `Revisions` must describe the restored checkpoint (empty only when G=0). Replay bumps them as the
 * live path did, and validates each against the `BeforeRev`/`AfterRev` the record carries:
 * a mismatch means the journal and the backend disagree about history, which is corruption,
 * not a difference to reconcile.
 *
 * Residency: each touched chunk receives a retained backend interest. Restore and replay
 * use separate reserved ID ranges. These remain until backend shutdown: checkpoint-backed
 * on-demand reload is not implemented, so releasing them on player departure would lose
 * authoritative in-memory terrain. Invoke replay once on a fresh, checkpoint-restored backend.
 *
 * On success the caller must inherit OutStats.DirtyChunks for the next capture. These are
 * precisely the changed keys after G, with their last-changing sequences.
 */
TERRAINCORE_API FTerrainStoreResult TerrainReplayJournal(
	FTerrainWorldStore& Store,
	ITerrainBackend& Backend,
	FTerrainRevisionIndex& Revisions,
	FTerrainReplayStats& OutStats);

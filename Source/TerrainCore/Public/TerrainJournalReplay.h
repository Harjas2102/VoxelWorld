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
 * **Checkpoint capture does not exist yet, so G is always 0** and the whole journal is
 * replayed onto a freshly generated base. That is correct and complete for a young world and
 * it is *unbounded*: replay cost grows with every edit ever made, which is precisely the
 * problem checkpoints exist to solve. Measuring when that becomes unacceptable is a gate, not
 * an assumption -- see the limits in the handoff.
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

	FTerrainOpSeq FirstOpSeq = 0;
	FTerrainOpSeq LastOpSeq = 0;

	/** Segment IDs walked, ascending. */
	TArray<uint64> Segments;

	double Seconds = 0.0;
};

/** The streaming interest replay takes and deliberately does NOT release. See below. */
inline constexpr uint32 TerrainReplayInterestId = 0x52504C59;   // 'RPLY'

/**
 * Applies the journal to a freshly initialised backend.
 *
 * `Backend` must be initialised against the **same base** the store records; replaying onto a
 * different generator produces a world that is neither the old one nor a new one. The store's
 * base descriptor is what that identity is checked against by every object it reads, but this
 * function cannot check the backend itself -- ITerrainBackend exposes no base identity -- so
 * the caller is responsible for that and the limitation is stated rather than implied.
 *
 * `Revisions` must be empty. Replay reconstructs revisions by bumping them exactly as the
 * live path did, and validates each against the `BeforeRev`/`AfterRev` the record carries:
 * a mismatch means the journal and the backend disagree about history, which is corruption,
 * not a difference to reconcile.
 *
 * Residency: each operation's footprint is made resident through a service-owned interest
 * before it is applied, because a conforming backend refuses an edit it does not have loaded.
 *
 * **That interest is NOT released when this returns, and the difference matters.** P-003 §3
 * says replay's interests are "released afterwards", which is right in the world P-003
 * describes -- one where a checkpoint holds the restored state, so an evicted chunk simply
 * reloads from its payload. That world does not exist yet: with no capture pump, G is 0 and
 * everything replay rebuilds lives only in backend RAM. Releasing the interest on a backend
 * that evicts would silently discard the whole restored world. The caller must therefore
 * release `TerrainReplayInterestId` only once the world's own streaming interests cover those
 * chunks -- and the fact that this is even a question is one of the concrete reasons the
 * capture pump is required rather than an optimisation.
 */
TERRAINCORE_API FTerrainStoreResult TerrainReplayJournal(
	FTerrainWorldStore& Store,
	ITerrainBackend& Backend,
	FTerrainRevisionIndex& Revisions,
	FTerrainReplayStats& OutStats);

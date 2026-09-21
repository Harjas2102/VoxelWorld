# HANDOFF

**Last session:** CP-015 · T-120 · 2026-09-20 · Claude (Opus 5)
**Branch:** `main` · **Tests:** 33/33 TerrainCore automation, `Terrain.SelfTest` PASS,
`Adapter.DensityContract` 20/20, `Test-TerrainCheckpoint.py` PASS, `MP.Convergence` PASS
(3 clients, 243 commits).

---

## What shipped: the bulk adapter `ReadRegion`

`FVPLegacyBackend::ReadRegion` previously called `UVoxelDataTools::GetValue` once per voxel:
**32,768 calls per chunk**, each taking its own read lock and walking the octree from the root,
each result appended with `TArray::Add`.

It now takes **one `FVoxelReadScopeLock` and one `FVoxelConstDataAccelerator` for the whole
chunk** — the plugin's own node-caching bulk path, used directly rather than through
`UVoxelDataTools`, which would have churned a `TArray<FIntVector>` of positions and a
`TArray<FVoxelValueMaterial>` of results for no benefit. The payload is sized once with
`SetNumUninitialized` and written by index; the material half is zeroed once with `Memzero`.

**It reads exactly what the old path read.** `Adapter.DensityContract` passes 20/20 with all
four fixture hashes unchanged.

## What it revealed: the stall was never the read

P-003 §4 predicted this fix and the reason for it: *"the current 32,768-per-voxel-call adapter
path must gain a measured bulk-read implementation before production integration."* The fix was
worth making. The reason was wrong.

Capture only moved from ~42 to ~25 ms/chunk, so the phases were instrumented permanently in
`FTerrainCheckpointStats` and are now logged on every capture. Warm solo, 8 chunks, 1,049,600
bytes, total 0.197 s:

| Phase | Time | Share |
|---|---|---|
| `ReadRegion` × 8 | 0.003 s | 1.5% |
| encode + BLAKE3 | 0.000 s | ~0% |
| store 8 payload objects | 0.015 s | 8% |
| **index path-copy, 49 pages** | **0.168 s** | **85%** |
| descriptor + root slot | 0.005 s | 2.5% |

The load run says the same thing independently: under three clients, **4 chunks cost 0.14–0.25 s
— the same wall-clock as 8 chunks solo.** Doubling the chunks did not change the time.

**The stall is index write amplification.** 8 changed keys produce 49 durably written pages,
each an object write ending in `Flush(true)` per D-034.

## What this changes for whoever is next

**Do the index write path before the incremental pump.** This reverses the order the Director
gave and the reason is in the table: the pump spreads chunk payload work, which is 9.5% of
capture. Building it next would spread a tenth of the stall, leave 85% synchronous on the game
thread, and let us claim a fix a player would still feel. Ruled in **D-035**.

The candidates, in the order I'd try them:

1. **One fsync barrier per capture instead of one per page.** P-004 §12's publication order
   already makes every page unreferenced garbage until the root slot lands, so a crash
   mid-batch is already safe — the durability barrier only has to precede the descriptor. This
   is the cheapest change and should take most of the 0.168 s.
2. **Batch the pages of one capture into fewer object writes.**
3. **Reduce the page count** — 6:1 amplification is a property of path-copying a 12-level trie
   for scattered keys.

Then re-measure, and only then size the pump against what is left. It may be nothing.

`bCheckpointCapture` stays **false**: the projection to the 256-chunk trigger is still
multi-second, so P-003 §4's gate is still failed.

## Remaining queue (Director's order, amended by D-035)

1. ~~Bulk adapter `ReadRegion`~~ — done, this session.
2. **Index write amplification** — inserted ahead of the pump on the evidence above.
3. Incremental capture pump — scope to be re-sized after (2).
4. Retention / GC — the store still only grows.
5. Crash matrix.

## Standing note

A prediction in an adopted proposal is not evidence. P-003 §4 named a real inefficiency and was
still wrong about the cost by a factor of nine. The phase breakdown is permanent so the next
claim about where capture time goes can be checked instead of inherited.

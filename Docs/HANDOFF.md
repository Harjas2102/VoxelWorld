# HANDOFF

**Last session:** CP-015 · T-123 · 2026-09-20 · Claude (Opus 5)
**Branch:** `main` · **Tests:** 35/35 TerrainCore automation, `Test-TerrainCheckpoint.py` PASS,
`MP.Convergence` PASS (3 clients, 244 commits, 15 captures, 0 stalls), `Terrain.StressCapture`
at the real trigger.

---

## What shipped: the incremental capture pump (DEF-2 closed)

Capture was one synchronous call — every dirty chunk read, encoded and buffered while the game
thread waited, 0.162 s at the real trigger. The pump takes the cut once at G, then reads and
encodes a few chunks per frame under a budget (`CheckpointPumpMillisPerFrame`, default 2 ms).
Edits keep being admitted, committed and broadcast throughout.

| | synchronous | pumped |
|---|---|---|
| 256-chunk capture, game thread | **0.162 s in one frame** | 0.135 s over 0.444 s wall |
| longest unbroken step | 0.162 s | **0.035 s** (index 0.014 + publish 0.021) |
| stall warnings | fired | none |

### Copy-before-write, without the copy

P-003 §4 specifies stashing a copy of a chunk before an edit touches it — dirty banks, a fence,
a reconciliation step. **None of it is needed.** When an edit is about to modify a chunk the
capture still owes, the pump captures *that chunk immediately, out of order*, and drops it from
the pending set. The pre-edit state is encoded before the backend can change it — the same
property the banks existed to provide, with no second copy of a 131 KB payload and nothing to
reconcile. The cost is one chunk read inside that edit, bounded by its own validated footprint.

The hook is in `Cb.Apply`, immediately before `Backend->ApplyOp`.

**Why the cut is still consistent:** a chunk the pump reaches on its own cannot have been edited
since G (any edit would have taken it first); a chunk an edit touches is encoded before
`ApplyOp`; and revisions agree, because the hook runs before the revision index advances. Edits
after G go into a fresh dirty set, so a chunk edited mid-capture is in both — this checkpoint
records its state at G, the next records what the edit made of it.

### Two things worth knowing before touching it

- **The synchronous entry point is now the pump with an unlimited budget.** One code path, so
  they cannot drift, and every existing capture test exercises the pump. That refactor landed
  green at 34/34 *before* any new behaviour was added, which is what made it safe.
- **`Advance` always captures at least one chunk, whatever the budget.** A pump that can make
  zero progress can never finish — a budget below one chunk's cost would starve the capture
  forever. Found by the test asserting a zero budget still progresses; the first implementation
  checked the deadline before doing any work.

### Evidence

`Persistence.Capture.Pump` interleaves edits with a part-finished capture and asserts the
restored checkpoint is the world **at G**, not as it is now — *and* the converse, that the live
world really has moved on, so the match cannot pass vacuously. It also asserts copy-before-write
actually fired, because a test that meant to interleave and did not would pass while proving
nothing.

In production: a 30-second three-client round with captures every 16 ops took 15 checkpoints and
**2 chunks by copy-before-write**, with no stalls.

## What is still synchronous

Publication — index path-copy, descriptor, the single pack write, root slot — is one step at the
end, 0.035 s at the trigger. Spreading it means interleaving a path-copy with edits changing the
very set being indexed: a much harder problem, not worth it until the number says so. The stall
warning now measures exactly that unspread part, at a 0.5 s threshold.

The 4,096-chunk tail is **reduced, not gone**. Chunk work is spread, so what remains at that
size is publication — roughly sixteen times 0.035 s. Admission now also counts the capture's
outstanding set toward the hard bound, which it previously did not; without that a world could
hold up to twice the bound it advertises.

## Remaining queue

1. ~~Bulk adapter `ReadRegion`~~ (T-120, D-035) · ~~index write amplification / packs~~ (T-121,
   D-036) · ~~measure at the real trigger, default on~~ (T-122, D-037) · ~~incremental pump~~
   (T-123, D-038).
2. **Retention / GC (DEF-9)** — next. The store only grows, and packs make it harder in a
   specific way: reclamation can delete a loose object individually but **cannot delete one
   object out of a pack** (P-004 §13.6). A pack is reclaimable only when nothing live refers to
   anything in it; reclaiming partially dead packs needs a compaction pass that does not exist.
3. Crash matrix.

## Standing note

Four increments, four measurements, three projections proved wrong before they could be acted
on. The phase times are logged on every capture and `Terrain.StressCapture` reproduces the
trigger run in ~80 seconds. Measure before building, and measure again after.

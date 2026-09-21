# HANDOFF

**Last session:** CP-015 · T-122 · 2026-09-20 · Claude (Opus 5)
**Branch:** `main` · **Tests:** 34/34 TerrainCore automation, `Test-TerrainCheckpoint.py` PASS,
`MP.Convergence` PASS (3 clients, 243 commits), `Terrain.StressCapture` at the real trigger
twice.

---

## What shipped: the measurement, and the default flips

D-036 left `bCheckpointCapture` false for a stated reason — *"the measurement has not been
taken."* The trigger is 256 dirty chunks; every harness dirtied four or eight. So the harness
now exists.

`Terrain.StressCapture` dirties exactly `CheckpointDirtyChunkTrigger` chunks through the real
`RequestEdit` path and lets the ordinary pump fire the capture. **Two things it taught by
failing first**, both worth knowing before touching it:

- **It runs over ~80 seconds, not one frame.** The queue rate limits each source to three
  intents a second; a first attempt to issue 256 edits in a frame was refused 253 times with
  `RateLimited`. That limit is anti-griefing and correct. Spreading them is also the truthful
  shape — 256 dirty chunks accumulate from many players over minutes on a real server.
- **It adds rather than removes.** The first working run dirtied nothing: a `Remove` in empty
  air modifies no voxels, so no chunk is affected, so nothing is dirty and no capture fires. It
  looked like a silent failure and was really a test placing edits in the sky.

### The number

**256 chunks, 33,587,200 bytes, 1,155 index pages, in 0.162 s** — 0.6 ms/chunk.

| Phase | Time |
|---|---|
| read 256 chunks | 0.089 s (55%) |
| encode + BLAKE3 | 0.005 s |
| store payloads | 0.026 s |
| **1,155 index pages** | **0.016 s** |
| pack write + root slot | 0.025 s |

Reproduced at 0.165 s on a second run with no settings overrides, which also confirms the new
default takes effect. The index line is the packs result in miniature: 1,155 durable pages in
0.016 s, where before D-036 that alone would have been ~3.5 s of `fsync`.

**D-036 extrapolated 1.5 s. The truth is nine times better** — the third wrong projection in
this checkpoint, after P-003 §4 (reading was the cost: it was 1.5%) and D-035 (a deferred fsync
barrier was the fix: it was 4 ms slower).

### The ruling

**`bCheckpointCapture` defaults to true** (D-037). 0.162 s is an order of magnitude inside
P-003 §4's multi-second gate, and the trade inverted: off means startup replay grows without
bound forever; on costs an occasional sixth of a second.

Also: the stall warning moved from 0.1 s to 0.5 s — at 0.1 s it fired on every healthy capture
while announcing a gate failure that had not happened. And `RejectionName` gained the six enum
values it was missing (`OutOfReach`, `ToolUnavailable`, `PermissionDenied`, `NotResident`,
`RateLimited`, `UnsafePlacement`), all of which printed as `Unknown` — which is what hid the
rate limiter for two runs.

## The tail this does not fix

Capture runs only when the queue is empty, and admission closes at 4,096 dirty chunks. A server
busy enough that the queue never drains would accumulate toward that bound and then take a
capture roughly sixteen times this one — about **2.6 s**, back inside what P-003 §4 fails —
while refusing edits until it drained.

Nothing observed goes near it: a 30-second three-client round reaches 243 ops and 4 dirty
chunks and takes **no checkpoint at all**. But it is a real shape, and it is exactly what the
pump exists to prevent.

## Remaining queue

1. ~~Bulk adapter `ReadRegion`~~ — done (T-120, D-035).
2. ~~Index write amplification~~ — done (T-121, D-036); it was a file-count problem.
3. ~~Measure capture at the real trigger~~ — done (T-122, D-037); default now on.
4. **Incremental copy-before-write pump (DEF-2)** — next. For the first time it is aimed at the
   phase that actually dominates (reading, 55%), and it closes the 4,096-chunk tail above.
5. Retention / GC — **the store still only grows, and a pack cannot be reclaimed object by
   object** (P-004 §13.6).
6. Crash matrix.

## Standing note

Three projections this checkpoint, all reasonable, all wrong, each cheaper to measure than to
argue about. The phase times are logged on every capture and `Terrain.StressCapture` reproduces
the trigger run in 80 seconds. Measure before building, and measure again after.

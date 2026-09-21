→ No action. For your reading only.

# HANDOFF

**Last session:** CP-015 · T-124 · 2026-09-21 · Claude (Opus 5) implementing, Codex reviewing
and repairing (**D-028**)
**Branch:** `main` · **Tests:** 36/36 TerrainCore automation (verified independently after the
transfer, not inherited), production retention harness PASS.

---

## What shipped: object retention, gated off (D-039)

A mark-and-sweep over the object store behind `Terrain.Reclaim`. It marks from **both on-disk
root slots, re-read every pass**, walking each checkpoint with the same `TerrainIndexEnumerate`
traversal restore uses — so the live set is by construction *what a restore would need* — and
loading and decoding every payload rather than merely naming it. Unreachable loose objects are
deleted; partly dead packs are rewritten without their garbage.

### Measured, on a real world

`Tools/Test-TerrainRetention.py` builds three genuinely distinct generations (Add / Remove /
Add, 539,904 / 538,368 / 538,368 voxels changed, cuts at G=256, G=512, G=768), verifying 256
chunk hashes across a restart after each.

| | |
|---|---|
| store before / after | **101,658,408 → 67,823,838 bytes** |
| reclaimed | **33,834,570 bytes** — one full dead generation |
| packs | 1 of 3 deleted, 1 loose object deleted |
| terrain | **all 256 hashes survive reclamation and restart** |
| second sweep | 0 bytes — correctly idempotent |

Two costs this exposes, both worth carrying:

- **The pass takes ~16.8 s, synchronous on the game thread, whether or not it reclaims
  anything** (the idempotent second sweep cost the same as the first). The expense is the
  *mark*, not the sweep — so making retention production-grade means making the walk
  incremental, not just the deletion.
- **Restore is 13.1 s for 256 chunks.** Replay is zero, which is what checkpointing bought, but
  the restore that replaced it is not free and has never been optimised.

### Why the unit test needs compaction and the production run did not

In the harness every generation re-edits the *same* 256 chunks, so each one supersedes the last
completely and the oldest pack becomes entirely dead — a whole-pack delete. In the unit test the
generations touch *different* chunks, so index path-copying shares subtrees and no pack ever
becomes fully dead: before compaction that world freed **176 bytes and stranded 757 KB**; with
compaction, **666 KB, nothing stranded**. Both shapes are real and both are covered.

## Why it is gated off, and it should stay that way

`Terrain.Reclaim` does nothing unless the process carries `-TerrainRetentionExperiment`.

Compaction removes the original pack **after** writing a replacement, and a replacement can hold
objects shared by *both* roots. If the replacement's directory entry is not durable across power
loss — **R-015**, which P-004 §12 leaves unproven on Windows — that one loss defeats both
retained generations at once. Every other failure in this system leaves a fallback; this is the
only one that would not. Content addressing makes a *process* crash harmless here, which is a
different and weaker claim than the one originally written down.

**Lifting the gate means discharging R-015 first.** That is the next thing worth doing if
retention matters; until then the store grows and that is the safer trade.

## The review, and what it says about how we work

The first pass was mine; Codex reviewed and repaired it
(`Docs/reviews/P-004-review-codex-retention.md`). Six defects, all real. Two are worth
remembering for their shape rather than their content:

- the mark **added** payload digests to the live set instead of **loading** them, so a
  checkpoint with a missing payload marked clean and reclamation proceeded;
- the fallback test **computed** `HashesAtG8` and never **compared** it.

Both looked like checks. Neither was. A seventh finding invalidated a result I had already
reported: repeating `Add` on solid terrain is idempotent, so my earlier production runs changed
`voxels=0` and never built the generations they claimed to measure. This is exactly the failure
mode **R-016** names, and the reason **D-028** alternates implementation between agents.

## Remaining queue

1. ~~Bulk `ReadRegion`~~ (D-035) · ~~packs~~ (D-036) · ~~measure at the real trigger, default
   on~~ (D-037) · ~~incremental capture pump, DEF-2~~ (D-038) · ~~object retention, gated~~
   (D-039).
2. **The crash matrix** — next, and the last structural piece of build step 4.
3. **R-015 namespace durability** — the gate on letting retention run for real.
4. Production retention: incremental, off-thread, with P-003 §5's pins and epoch protocol.
   **DEF-9 is not closed.**
5. Journal trimming — correctly deprioritised at ~49 KB per checkpoint interval against 33 MB
   of payloads.

## Standing note

Five increments, five measurements. Three projections were proved wrong before they could be
acted on, and this increment added a fourth kind of error: a measurement that ran, passed, and
measured nothing, because the workload was idempotent. A number is only evidence once you have
checked that the thing you were varying actually varied.

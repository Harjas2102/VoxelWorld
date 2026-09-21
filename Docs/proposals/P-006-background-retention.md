→ No action. For your reading only.

# P-006 — Background retention: the incremental collector (DEF-9)

**Status: specification and Architect ruling, 2026-09-21. Implemented as T-127.**
**Author:** Claude Opus. **Risk:** R3. **Base:** `374d11f` (CP-017).
**Authority:** P-003 §5 (payload/index GC), P-005 (containers), D-023 (technical ruling, logged).

Writer and reviewer are the same agent (Director's standing instruction). §6 is the self-review.

---

## 1. What P-003 §5 requires, and where each requirement lands

| P-003 §5 requirement | Implementation |
|---|---|
| Trace the union of both root closures | The mark walks both **on-disk** slots, re-read at cycle start, with restore's own traversal |
| Bounded I/O buffers off the game thread | Mark, frame building and read-back verification run on a `UE::Tasks` worker against an `FTerrainObjectSnapshot`; copy frames are capped at `RetentionFrameMegabytes` (4 MB) |
| One storage owner; reference selection and deletion serialized | Every mutation — capture commits, collector appends, cuts, deletes — happens on the game thread. The worker only reads |
| Capture a root/pin epoch; stop deletion if it changes | `FTerrainFileObjectStore::ReferenceEpoch`, moved by **every** `StoreObject` and every root publication. Checked before planning, before every append and before every cut or delete; a moved epoch abandons the cycle |
| Never delete an object created after the captured epoch | An object stored since the epoch moved it, so no deletion follows; new objects also only ever go to the active container, which is never a source |
| A publication cannot resurrect an unpinned orphan by content hash | A dedup hit in `StoreObject` writes nothing but **still moves the epoch** — that hit is exactly the resurrection |
| Deletion only after publication success and validation of both roots | The mark fails the whole cycle on any unreadable or invalid object; both slots must validate |
| Full mark may grow with history but runs incrementally | Off-thread; not required for any checkpoint to succeed |
| Failure leaves extra files | Every failure leaves duplicates at most; nothing is cut until copies are durable and verified |

## 2. The cycle

1. **Begin (game thread).** Refuse if a capture batch is open or either on-disk root slot fails
   to validate. Record the epoch; snapshot the location map.
2. **Mark (worker).** Walk both roots through the snapshot; list any pre-P-005 files.
3. **Plan (game thread).** Epoch unchanged, and the live set must contain the current root's
   descriptor. The plan has up to three kinds of job:
   - migrate pre-P-005 files;
   - move writing to an empty container if the active one is worth compacting;
   - one job per other container that is worth compacting.
4. **Copy.** The worker builds a frame of at most 4 MB from the snapshot. The game thread
   appends it, one frame per step, checking the epoch first.
5. **Verify (worker).** Read every copy back from the active container by explicit location.
6. **Cut (game thread).** Continue only if the epoch is unchanged and no capture batch is open.
   Truncate the source, or delete pre-P-005 files, 64 per step.

**Worth compacting:** at least `MinDeadFraction` (25%) of a container's object bytes are dead.
Without this threshold, the cycle after the second checkpoint copied 67 MB of live data to
reclaim almost nothing.

The service owes a cycle after each successful publication. It runs one when no capture is in
flight, advances it one step per service tick, and re-owes the cycle if it was abandoned for the
epoch. `Terrain.Reclaim` runs a whole cycle inline, as a diagnostic. `-TerrainRetentionExperiment`
is gone: `bBackgroundRetention` defaults to true.

## 3. Deviations from P-003, stated

- **No explicit pin objects.** No backup, migration or sync consumer exists yet. When one does,
  its retention handle must move the epoch on acquisition and be included in the mark. The
  epoch rule is the hook for it.
- **Abandon rather than reconcile.** When the epoch moves, the cycle ends; it does not merge the
  new references into its live set. Captures run minutes apart, and a cycle takes about 0.55 s,
  so abandonment is rare (0 of 30 in the multiplayer run). The simpler rule is easier to prove.

## 4. Evidence

- **Automation:** 37/37 TerrainCore tests pass.
  - The **epoch matrix** interrupts a 40-step cycle after every step, two ways: a dedup store,
    and an open capture batch. In all 80 cases nothing is cut or deleted after the
    interruption, and both generations restore exactly.
  - **The matrix is sensitive:** with the pre-cut epoch check disabled, it fails at steps 37
    and 38.
  - A background cycle on a worker thread over a real disk ran in 18 bounded frames, with a
    longest game-thread step of 2.7 ms.
- **Background retention in the real game** (`Test-TerrainRetention.py --background`):
  - after capture 2 the cycle correctly declines to compact (one step, 0.1 ms);
  - after capture 3 it reclaims 33.8 MB, with a longest game-thread step of 7.9 ms;
  - the save stays at 67.8 MB across three generations, and all 256 hashes survive a restart.
- **Other harnesses:**
  - `Test-TerrainRetention.py` in explicit mode passes (101.7 MB → 67.8 MB).
  - `Test-TerrainCheckpoint.py` passes.
  - Multiplayer with a capture every 16 ops: 486 commits, 30 checkpoints and 30 completed
    background cycles, none abandoned, no terrain warnings.
  - A real pre-P-005 world was migrated through the collector with identical hashes.

## 5. Not done

- **Exclusive-writer lease** (P-003 §5): a separate increment.
- **Journal trimming:** needs a pre-created segment ring (P-005 §8).
- **Longest game-thread step.** The 7.9 ms step is one 4 MB append plus its flush. It is
  controlled by `RetentionFrameMegabytes`; halving that halves the step.

## 6. Self-review — assume it is wrong

1. **Collection was run on every checkpoint regardless of how much garbage existed.** A full
   copy was spent to save nothing. Found in the real-game log and fixed with the dead-fraction
   threshold.
2. **Sharing violations.** The worker opens containers for reading while the game thread appends
   to them. Without write-sharing on `ReadRange`, Windows could refuse the **capture's** open.
   Found in design; `ReadRange` now opens with write sharing.
3. **The epoch matrix could have been vacuous.** It was mutation-tested (§4) and caught the
   removed check.
4. **Teardown with a worker in flight.** `CloseWorldStore` abandons the collector first, and
   abandoning waits for the task before the device and store are released.
5. **Still trusted, not proved.** The Linux build of the worker path has not been compiled
   (`UE::Tasks` is portable; the unproved part is P-005's `fsync(dir)`).

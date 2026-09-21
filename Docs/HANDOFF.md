→ No action. For your reading only.

# HANDOFF

**Checkpoint:** CP-016 · **Date:** 2026-09-21 · **Branch:** `main`
**Agents:** Claude (Opus 5) implementing T-120…T-123 and T-125; Codex reviewing and repairing
T-124 (**D-028**).
**Expected next agent:** either.

---

## Where the project is

**Build step 4 is done for terrain. The server remembers.** An edit is journalled durably
before it is broadcast; a checkpoint bounds replay; capture is incremental and on by default;
the store can be reclaimed; and the recovery protocol has been attacked at every write point.

CP-015 recorded that VISION's Pillar 1 — *"the server remembers everything at next login"* —
was still false, and said two checkpoints with that sentence false was acceptable only while
the work was real. It was. **That flag is now clear.**

## Evidence (all re-run on the committed source)

- **37/37** TerrainCore automation.
- `Terrain.SelfTest` PASS · `Adapter.DensityContract` **20/20**, fixture hashes unchanged.
- `Tools/Test-TerrainCheckpoint.py` PASS — four real game launches, all eight affected chunk
  hashes identical across every launch, run 4 restores the cut and replays **zero** edits, and
  a wrong-base boot closes terrain access leaving every save file byte-identical.
- `Tools/Test-TerrainMultiplayer.ps1` PASS — 3 clients, 244 commits, 15 checkpoints, 2 chunks
  taken by copy-before-write, **0 stalls**.
- `Tools/Test-TerrainRetention.py` PASS — three genuinely distinct generations,
  **101,658,408 → 67,823,838 bytes**, all 256 hashes surviving reclamation *and* restart.
- `Terrain.StressCapture` at the real 256-chunk trigger: **0.162 s**, reproduced twice.

## The six increments

| | | |
|---|---|---|
| T-120 | D-035 | Bulk adapter `ReadRegion`. P-003 §4 named it as the cause of the capture stall; **it was 1.5% of it.** |
| T-121 | D-036 | **Packs** — one file, one flush per capture instead of 59. The obvious fix (a deferred flush barrier) was measured first and is *slower*. 0.197 s → 0.010 s. |
| T-122 | D-037 | Measured capture at its **real** trigger: 0.162 s, not the extrapolated 1.5 s. Default flipped on. |
| T-123 | D-038 | **Incremental capture pump — DEF-2 closed.** Copy-before-write needs no copy. Longest unbroken step 0.162 s → 0.035 s. |
| T-124 | D-039 | **Object retention + pack compaction.** 33.8 MB reclaimed on a real world. **Gated off** pending R-015. |
| T-125 | D-040 | **The crash matrix.** Every mutating write failed in turn; every recovery landed on an exact point in real history. |

## What is NOT done, stated plainly

- **R-015 — durable name publication on Windows.** Unproved, and now the gate on **two**
  things: letting retention run for real, and extending the crash matrix's claims from *lost
  writes* to *power loss*. Nothing in-process can settle it.
- **DEF-1** — needs settlement and its SQLite ledger (build step 6). This is also the missing
  half of `Restart.CrashMatrix`: *"no duplicated or missing payout, no durable ore without
  durable removal."*
- **DEF-9** — retention exists but is a synchronous, hand-scheduled diagnostic, not P-003 §5's
  incremental off-thread collector with pins and an epoch protocol.
- Journal trimming (correctly deprioritised: ~49 KB per checkpoint interval against 33 MB of
  payloads), spread publication (one step, 0.035 s), Empty/SparseDiff compaction (P-003 §6
  blocks it until a backend can state its own base), and the exclusive-writer lease.

## Next safe action

**R-015.** It is the highest-value open item in the persistence stack and it unblocks two
things at once. It needs an *external* experiment — power-loss-class testing of name creation
(a VM with host-level power control, or equivalent) — or an explicit decision to adopt P-003's
preallocated-container mode instead. **The container fallback costs no stored bytes**, because
no schema-2 field contains a path, which is why it remains the cheap escape and why choosing
it is not a rewrite.

This is a judgement about acceptable risk on a player's save data, so it is worth the
Director's attention rather than being ruled silently.

## What this checkpoint learned about its own process

Three predictions were acted on and wrong before being measured (P-003 §4 on where capture time
went; D-035 on the fsync barrier; D-036 on the trigger cost). Then a measurement **ran, passed,
and measured nothing**, because repeating `Add` on solid terrain is idempotent — caught by
Codex, not by me. Then the crash matrix showed the last form: a test asserting only that
recovery *succeeds* would have passed on a world that came back wrong.

**Assert the state, not the absence of an error, and check that the thing you varied actually
varied.**

R-016 also got its first real evidence: T-124 is the first R3 increment to go through a genuine
second reader, and it found **six defects**, two of them recovery-critical — a mark that
*added* payload digests instead of *loading* them, and a fallback test that *computed* its
comparison hashes and never *compared* them. Both looked like checks. Neither would have been
caught by their author, because their author already believed what they were supposed to prove.

# HANDOFF

**Checkpoint:** CP-022 · **Date:** 2026-09-21 · **Branch:** `main`
**Agents:** Claude (Opus 5) wrote T-128 to T-132.2. Codex independently reviewed CP-016 to CP-020
(`Docs/reviews/2026-09-21-checkpoints-review-codex.md`). Claude corrected F1 to F5 (T-132.1 D-048,
T-132.2 D-049) and self-reviewed the fixes; the fixes themselves have not had a cross-vendor read.
**Expected next agent:** either (D-028).

---

## Where the project is

**Phase 1's gate items 1B through 1E are done (E-6 measured at CP-020). 1F's stress profile is
done; throughput at the design point (R-018, T-133) and 1F's observations remain.** Since CP-018:
- **T-128, P-007:** one writer per world, enforced by an OS lock.
- **T-129, P-008:** players who join or rejoin see the saved, edited world; DEF-3 resolved.
- **T-130, P-009:** the ground knows its material exactly, and each edit measures what it moved.
- **T-131, P-010:** digging credits the player durably, in a SQLite ledger. DEF-1 is resolved
  for terrain settlement.

The specs carry the evidence tables and self-reviews; STATE's CP-019 section is the summary.

## How to verify, from scratch

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' VoxelWorldEditor Win64 Development '-Project=C:\Dev\VoxelWorld\VoxelWorld.uproject' -WaitMutex
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'C:\Dev\VoxelWorld\VoxelWorld.uproject' '-ExecCmds=Automation RunTests TerrainCore; Quit' -unattended -nopause -nosplash -nullrhi -log   # 43/43
python Tools\Test-TerrainSettlement.py            # hard kills + journal audit + two refusals
.\Tools\Test-TerrainMultiplayer.ps1 -Rounds 3 -IncludeObserver -CheckpointCapture   # joins, materials, ledger audit per round
.\Tools\Test-TerrainMultiplayer.ps1 -Rounds 2 -DurationSeconds 30 -CheckpointCapture -DropOp 20
python Tools\Test-TerrainLease.py ; python Tools\Test-TerrainCheckpoint.py ; python Tools\Test-TerrainRetention.py
.\Tools\Test-TerrainStress.ps1 -Edits 5000 -LiveSeconds 20   # gate 8 + E-6 (about 3 min)
.\Tools\Test-TerrainStress.ps1 -Edits 100 -LiveSeconds 2 -ServerIni 'SettlementModule=None'   # must THROW (PARTIAL)
```
In a standalone game, `Terrain.AdapterChecks` runs the plugin's density, material and E-1
checks, and `Terrain.LedgerAudit` recomputes every balance from the journal.

## T-132 (recorded at CP-020, D-047)

**T-132 is recorded.** The
report, rulings and self-review are in `Docs/proposals/P-011-stress-profile.md`, and the driver is
`Tools/Test-TerrainStress.ps1`.
- **Five defects found and fixed:**
  - the service ran about 4 times per 30 Hz frame (a 10 ms looping timer);
  - **checkpoints starved under sustained load** (0 in 7,871 edits);
  - the console pump bypassed the settlement window;
  - residency pins built collision and navmesh for every edited chunk;
  - clients installed snapshot bursts in one frame (now an ordered 8 ms/frame inbox).
- **Gate 8: correctness PASS, performance FAIL at 96 edits/s on this machine.** Sustained
  throughput is 71–78/s, and server frames are p95 65–80 ms. The named costs are one journal flush
  per edit (3.5 ms, on the game thread) and checkpoint re-reads (about 3 ms per chunk).
- **E-6:** a joiner caught up in 6.1 s mid-edit (243 snapshots, 1.6 MB), installing one chunk per
  frame at about 23 ms each.
- Writer and reviewer were the same agent.

## What is NOT done, stated plainly

- **Throughput at the design point (T-133, next):** 96 edits/s is not sustained on this machine
  (71–78/s). The fix is journal group commit plus a checkpoint trigger priced in chunks (P-011 §4).
- **Player identity** (R-017): owners come from the Null online subsystem, which is per machine at
  best. A real login is a Director decision in Phase 4.
- **No inventory UI, no spending or crafting.** Placement is free and places Fill.
  Inventory-based building is a GAME decision.
- **Placing and re-digging recovers about 79%** (P-009 §6). It is a leak, not a mint, and a
  policy question for when building costs materials.
- **Linux has never been built** (R-007). The first build must run `WriterLease`, `Settlement.*`
  and `Storage.PlatformDevice`; UE's SQLite WAL behaviour there is unverified.
- **Journal trimming, retention pins and backups** are not built.
- **Resync has no rate limit** (P-008 §7).
- **K9's SingleIndex switch** is deferred, because it changes how terrain looks.
- **Codex review F6 is open** (Linux parent-directory sync, R-007). F1 to F5 are fixed. The specs
  P-005 to P-010 have still not had a cross-vendor design read; the review covered the code of
  CP-016 to CP-020. The fixes are worth a short Codex re-read when it is next available.

## Next safe action

### Independent review completed (Codex, 2026-09-21)

- Director requested review of all Claude changes recorded in the September 21 checkpoints.
- Role: independent reviewer; R3 review, no implementation or decision changes authorized by this request.
- Base: `6a10a84` on clean, synchronized `main` before this note. Scope: CP-016 through CP-020, including the September 20 implementation commits recorded by CP-016 (`555261c^..6a10a84`).
- Report: `Docs/reviews/2026-09-21-checkpoints-review-codex.md`. Verdict: changes requested, two P1 and four P2 findings. P1: normal checkpoint publication lacks the settlement watermark (reproduced restart refusal at G=3/W=0); copy-before-write capture failure can discard dirty history without disabling later captures (source-traced).
- P2: settlement window exceeded 32 (reproduced 34); stress gate accepts failed ledger audit (reproduced); crash matrix lacks an acknowledged-history lower bound; Linux bootstrap omits parent-directory durability for the world root (last two source-traced).
- Verification: both Win64 build targets succeeded (up to date), TerrainCore 42/42 passed, checkpoint/restart and writer-lease harnesses passed, and two-round observer/drop-op multiplayer passed with ledger audits. Exact commands, evidence paths, qualifications and unrerun tests are in the report.
- No production code, tests, configuration, assets or checkpoint documents changed. Only this handoff and the review report are tracked changes; diagnostics are under ignored `Saved/`. No checkpoint, commit, or push requested or performed.
- Next safe action: Director selects the correction scope. Reviewer recommends resolving F1/F2 and their regression coverage before T-133, then F3-F5 and the full stress gate; F6 needs Linux durability correction/validation. The previously planned T-133 work remains below for context and is not started.

### F1 and F2 corrected (Claude, recorded at CP-021 as T-132.1, D-048)

- **Task:** correct the review's two P1 findings before T-133. R3 (persistence). The Director
  ruled the correction scope by forwarding the review. Writer and self-reviewer were the same
  agent (Claude); Codex's review is the independent half. Base `6a10a84`. Codex's report and
  handoff note above are preserved unchanged.
- **F1, fixed.** The pump takes the watermark W as a required argument to `Begin` and `Advance`.
  The setter and its "everything settled" default are gone. Publication has one boundary,
  `FTerrainCapturePump::TryPublish`, which the empty cut also goes through. The service supplies
  `Settlement->GetWatermark()` on every call (`MAX_uint64` with no ledger). The synchronous
  `TerrainCaptureCheckpoint` passes MAX and is documented as for ledger-less worlds only. It is
  used by tests only.
- **F2, fixed.** Every capture end (a `Begin` refusal, an immediate empty publish, `NoticeWrite`
  or `Advance`) sets a completion flag that `ConsumeCompletion()` reports exactly once. The service
  consumes it right after `NoticeWrite` in Apply, after `Begin` and `Advance`, and as a backstop at
  the top of `MaybeCaptureCheckpoint`. A failed or refused cut hands back all its keys
  (`TakeCut`), and `FinishCapture` merges them into `DirtyChunks`, where the newer OpSeq wins. That
  is in addition to the existing session latch, so the dirty set is honest even without the latch.
- **Found in self-review, same class as F2:** a `Begin` refusal returned its error without storing
  it in `Result()`. `FinishCapture` therefore read it as success and dropped the cut's dirty set.
  The fix above covers it, and the pump test asserts it.
- **New test** `TerrainCore.Persistence.Capture.Service`: a real `UTerrainService` inside an
  initialised game `UWorld` that never begins play, so `CommitOp` really runs. It uses a live
  settlement worker whose W only moves when the test polls it, and a backend that can fail reads
  on demand. It covers F1's non-empty and empty cuts, and F2 per Codex's trace (B restored from
  the checkpoint and hash-compared). **Mutation-checked:** removing the W wiring, bypassing
  `TryPublish` on the empty cut, removing both completion consumers, or removing the merge each
  fails it. Removing only the backstop consumer passes, as designed, because Apply already
  consumes. The pump test gained completion-once and refusal-returns-keys checks.
- **Evidence (final source):** both Win64 targets build. **43/43 TerrainCore automation**
  (`Saved/Logs/F1F2-Automation.log`). Codex's `python Saved/CodexReview-Cut.py`: the first cut
  waited 20.02 s for W, then published at G=1; the kill and restart booted, settled 34 records and
  **the ledger audit PASSED** (`Saved/Logs/CodexReview-Cut-5b91273b14`; before the fix: G=3, W=0,
  refused). Settlement kill test, checkpoint and lease harnesses PASS. MP 2 rounds with observer,
  capture and `-DropOp 5` PASS, with both ledger audits. **Stress 5,000:** PASS, 254 chunks
  verified, ledger audit exact over 7,722 edits, 31 checkpoints (T-132 had 29), unsettled max 6,
  settle latency max 132 ms. That is the most the W wait adds to a publication. Throughput
  unchanged at 76.7/s (R-018) (`Saved/Logs/StressTest-4b41bdc47a51`).
- **Not done:** F3 to F6. P-011's claim that G <= W is enforced at publication is now true, but
  P-011, ARCHITECTURE and STATE are not yet annotated. That is checkpoint text.
- **Recorded at CP-021** with the review report. STATE, D-048, BACKLOG, RISKS (R-016, R-007) and
  P-011's correction note are updated.

### T-132.2: F3, F4 and F5 corrected (Claude, recorded at CP-022, D-049)

- **Task:** the review's P2 findings F3 to F5. R3 (persistence tests and gates), under the standing
  instruction. Base `af88a30`. Writer and self-reviewer were the same agent.
- **F3, fixed.** `FTerrainQueueCallbacks::CanExecute` is asked before every operation `Pump` runs,
  including each child of a split transaction. The service's callback is
  `Pending() < MaxPending`, which replaces the two per-call gates (the frame pump and the console's
  256-op pump). `SettlementPendingPeak` is sampled at every `Submit`, the only moment the count
  rises. The stress verdict now fails if the peak ever passes 32.
- **F4, fixed.** `RunLedgerAudit` returns `ETerrainLedgerAudit` (Pass, Fail or Deferred).
  - The stress verdict requires a clean terrain comparison, the window, and a passing audit.
  - With persistence, checkpoints or settlement off, the verdict is **PARTIAL** and names what
    was off.
  - `Test-TerrainStress.ps1` also requires the audit's own PASS line. It throws on PARTIAL unless
    `-Measurement` is given.
- **F5, fixed.** The crash matrix judges every recovery with one `Judge` function:
  - never below the acknowledged head;
  - at most one above it, and only when the last append failed (P-003 §2's uncertain record);
  - exact hashes.
  - A third fault mode writes the whole record and then reports failure: 9 such appends came back
    and 18 torn or refused ones did not.
  - A negative control drops the final acknowledged record, leaving a valid, consistent prefix at
    OpSeq 8. The old oracle accepted this; `Judge` rejects it.
- **Mutation-checked:**
  - M5 (no per-op gate): the service test runs 48 in one pump call against a limit of 32;
  - M6 (no lower bound): the negative control fails;
  - M7 (the old ceiling, Head ≤ acknowledged): the matrix fails at mutation 11, mode 2. This
    confirms Codex's point that the old upper bound was wrong.
- **Evidence (final source):** both targets build. **43/43 automation**
  (`Saved/Logs/T1322-Automation-final.log`). Codex's controls:
  - `-ServerIni 'SettlementModule=None'` is now refused as PARTIAL (before: PASS);
  - `-ExtraArgs '-TerrainSettleDelay=2' -ServerIni 'bCheckpointCapture=False' -Measurement` gives
    **unsettled max 32 of 32** (before: 34), with the audit PASS.
  - Short full-stack stress: PASS.
  - Stress 5,000 (before the PARTIAL wording change, same gates): PASS, 259 chunks, ledger exact
    over 7,334 edits, 28 checkpoints, 68.3/s (R-018 unchanged).
  - Settlement kill test PASS; MP 3 rounds with observer and capture PASS, audits every round.
- **Not exercised in a real process:** a stress run with settlement on whose audit FAILS. It goes
  through the same `Audit != Pass` branch as the unit-level logic, but no real run drove it.
- **Also still open:** the review's reporting notes (percentiles are of per-second maxima; stale
  capture comments in ARCHITECTURE and settings). F6 stays with R-007.

### Next safe actions, in order

1. **T-133: journal group commit** (below). Measure it with several stress runs (the spread is
   about ±5/s), then run the full default stress run.
2. **F6** with the first Linux build (R-007).
3. When Codex is next available: a short re-read of T-132.1 and T-132.2.

**T-133: journal group commit and checkpoint trigger policy** (P-011 §4). Batch every commit in
one pump call behind a single flush, and broadcast and settle only after that flush. It needs a
short spec, and the crash matrix must cover a torn multi-record append. Gate it on
`Tools\Test-TerrainStress.ps1`: 96 edits/s sustained with server frames near 33 ms. After that,
1F's observations and the backend decision.

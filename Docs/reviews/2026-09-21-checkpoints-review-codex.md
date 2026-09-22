→ No action. For your reading only.

# Independent review: September 21 checkpoints CP-016–CP-020

**Reviewer:** Codex, independent reviewer. **Risk:** R3 review. **Date:** 2026-09-21 (America/New_York; evidence logs use September 22 UTC).
**Reviewed HEAD:** `6a10a84`. **Range:** `555261c^..6a10a84`.
**Request:** evaluate all Claude code and changes recorded in the September 21 checkpoints.
**Verdict:** changes requested. Two persistence findings require attention before treating the checkpointed stack as crash-safe or building T-133 on its current behavior. Four additional findings concern the settlement bound, test verdicts, crash assertions, and Linux bootstrap durability.

This is a review, not a Director ruling or an implementation. No production source, tests, configuration, assets, numbered decisions, STATE, BACKLOG, or RISKS were changed. Test processes used unique generated worlds. This report and HANDOFF are the only tracked changes.

## Scope and provenance

The date includes more than CP-019 and CP-020. CP-016 records work committed on September 20 as well, so those implementation commits are included. The range contains 78 changed files, including documentation, in three runtime modules and the test tools.

| Checkpoint | Tasks reviewed | Implementation commits |
|---|---|---|
| CP-016 | T-120 bulk read, T-121 batching, T-122 measured capture/default, T-123 capture pump, T-124 retention, T-125 crash matrix | `555261c`, `3d30c97`, `1c7c91a`, `ebdf8ae`, `9969128`, `52fde65` |
| CP-017 | T-126 containers and namespace durability | `d20ab87` |
| CP-018 | T-127 background retention | `3da2b8c` |
| CP-019 | T-128 lease, T-129 join/resync, T-130 materials/yield, T-131 settlement | `1a53645`, `152e6f4`, `7ce51a2`, `1639f42` |
| CP-020 | T-132 stress profile and five fixes | `baeb54a` |

D-039 explicitly attributes T-124 to Claude plus an earlier Codex review/repair. It is included as inherited code and evidence, not represented as exclusively Claude-authored. I compared the production changes, supporting interfaces, tests and harnesses against P-003/P-004, P-005 through P-011, and the checkpoint claims. No sub-agents were used.

## Findings

### F1 — P1: normal checkpoint publication never receives the settlement watermark

**Locations:** [TerrainServicePersistence.cpp:467](C:/Dev/VoxelWorld/Source/TerrainCore/Private/TerrainServicePersistence.cpp:467), [TerrainCheckpoint.cpp:105](C:/Dev/VoxelWorld/Source/TerrainCore/Private/TerrainCheckpoint.cpp:105), [TerrainCheckpoint.h:259](C:/Dev/VoxelWorld/Source/TerrainCore/Public/TerrainCheckpoint.h:259). Introduced by T-132.

`SettledThrough` defaults to `MAX_uint64`. The only call to `SetSettledThrough` is in the manual `ReclaimStore()` diagnostic at line 362. `MaybeCaptureCheckpoint()` never supplies W before `Begin` or `Advance`, so the new `SettledThrough < G` condition does not enforce the invariant during normal play. In addition, `Begin()` calls `Finish()` directly for an empty dirty set, bypassing the guard even if its caller supplies W. Committed no-change edits make that case legitimate.

**Reproduced on the unchanged production build:** in a unique world, set both checkpoint triggers to 1, run `Terrain.DigStress` with `-TerrainSettleDelay=20`, and kill the process as soon as a checkpoint is logged. Restart without the delay. The restart logged `G=3`, `H=3`, `W=0`, followed by `TERRAIN ACCESS IS CLOSED` and `Terrain.LedgerAudit: FAIL no ledger is open`. All the journal records were available; this was an ordinary delayed-worker/crash case, not an old database deliberately restored by the operator.

The reduced trigger makes the race deterministic; the missing guard also exists with default triggers. P-011's claim that G <= W moved to publication is therefore false in the actual service integration.

**Required correction:** supply the current watermark on every capture path and enforce it at the single publication boundary, including empty captures. Preserve the boot refusal for genuinely stale ledgers. Add a service-level delayed-settlement/kill regression and an empty-cut regression; testing the pump's setter alone would miss the wiring defect.

**Evidence:** `Saved/Logs/CodexReview-Cut-77ddf814c3/dig.log` and `restart.log`; standalone diagnostic driver `Saved/CodexReview-Cut.py` (run with `python Saved/CodexReview-Cut.py`).

### F2 — P1: copy-before-write capture failure can discard dirty history and then publish past it

**Locations:** [TerrainServiceReplication.cpp:192](C:/Dev/VoxelWorld/Source/TerrainCore/Private/TerrainServiceReplication.cpp:192), [TerrainCheckpoint.cpp:368](C:/Dev/VoxelWorld/Source/TerrainCore/Private/TerrainCheckpoint.cpp:368), [TerrainServicePersistence.cpp:467](C:/Dev/VoxelWorld/Source/TerrainCore/Private/TerrainServicePersistence.cpp:467). Introduced by T-123.

`NoticeWrite()` can fail a capture before applying the new edit. `Fail()` sets the pump inactive, discards its pending keys and updates, and abandons the batch. The caller neither checks nor reports that completion. `MaybeCaptureCheckpoint()` only calls `FinishCapture()` for an *active* pump that completes through `Advance`, or a just-started capture. A failure in `NoticeWrite` therefore bypasses the intended `bCheckpointDisabled` latch.

**Failure trace:** checkpoint G0 exists; chunks A and B are subsequently edited; a capture moves their dirty entries out of the service at G1. An edit of A invokes `NoticeWrite`, where a failed region read/encode/store aborts that capture. The edit can still commit, putting A into the new dirty set. B is now absent from both the service dirty set and the abandoned capture. A later successful capture at G2 writes only newly dirtied chunks and advances the root beyond B's uncaptured edit. Restart restores B from G0 and skips its journal record because it precedes G2.

This is a **source-traced failure path**, not observed loss in the real-plugin runs. It requires one of `CaptureOne`'s handled failures during `NoticeWrite`; no claim is made that the current plugin spontaneously produced such a failure. The error-handling contract nevertheless permits losing previously committed terrain if it does occur.

**Required correction:** consume capture completion/failure regardless of which entry point produced it. Under the current approved policy, any such failure must disable subsequent captures for that session; alternatively, a separately approved retry must retain the entire failed cut's dirty history. Test a transient read failure during `NoticeWrite` with another dirty chunk that is never edited again, then restart and compare that chunk.

### F3 — P2: the settlement window is checked per pump, allowing more than 32 unsettled records

**Locations:** [TerrainServiceReplication.cpp:63](C:/Dev/VoxelWorld/Source/TerrainCore/Private/TerrainServiceReplication.cpp:63), [TerrainService.cpp:319](C:/Dev/VoxelWorld/Source/TerrainCore/Private/TerrainService.cpp:319). Introduced by T-131 and incompletely addressed by T-132.

With 31 records pending, the service still enters a pump that may execute multiple edits. Nothing inside that pump limits execution to the one remaining slot. The normal call permits up to 32 operations, and the console call permits 256 subject to its time budget. P-003 §2 bounds H-W, not the number pending at entry to a frame.

**Reproduced:** `Tools/Test-TerrainStress.ps1 -Edits 100 -LiveSeconds 2 -Port 17891 -ExtraArgs '-TerrainSettleDelay=2' -ServerIni 'bCheckpointCapture=False'` reported **unsettled max 34**. Disabling capture isolates this from F1. The run subsequently drained correctly and passed density/material comparisons for 155 chunks plus the ledger audit.

**Required correction:** stop before the next mutation when the remaining window is zero, including console pumping and split children. Assert the maximum throughout a slow-worker integration test, not just at the end after draining.

**Evidence:** `Saved/Logs/StressTest-267263e524d3/Server.log` and `Saved/Logs/CodexReview-Window-stdout.log`. Artificial worker delay was used to test the bound; these timings are not a throughput benchmark.

### F4 — P2: the stress driver reports PASS even when its ledger audit fails

**Locations:** [TerrainStressTest.cpp:218](C:/Dev/VoxelWorld/Source/TerrainCore/Private/TerrainStressTest.cpp:218), [Test-TerrainStress.ps1:76](C:/Dev/VoxelWorld/Tools/Test-TerrainStress.ps1:76). Introduced by T-132.

`RunLedgerAudit()` returns no result to the stress verdict. The C++ PASS condition only checks terrain hashes and a nonempty sample. The PowerShell driver prints the audit line but gates its exit status only on `Stress: PASS`. It does not require a successful audit or reject an explicit failed one.

**Reproduced negative control:** `Tools/Test-TerrainStress.ps1 -Edits 100 -LiveSeconds 2 -Port 17893 -ServerIni 'SettlementModule=None'` printed `Terrain.LedgerAudit: FAIL no ledger is open`, then `Stress: PASS`, and exited successfully. This deliberately missing ledger demonstrates that the gate accepts absent settlement verification; the same unchecked branch applies to a failed balance comparison.

**Required correction:** for the full-stack gate, require a completed successful ledger audit and reject missing/failed audits. If attribution modes intentionally omit a subsystem, report a distinct partial/measurement result rather than full correctness PASS. Performance thresholds are a separate T-133 acceptance matter; this finding is about the currently claimed correctness gate.

**Evidence:** `Saved/Logs/StressTest-425d128b70f2/Server.log` and `Saved/Logs/CodexReview-StressNoLedger-stdout.log`.

### F5 — P2: the crash matrix does not assert survival of acknowledged history

**Location:** [TerrainCrashMatrixTest.cpp:365](C:/Dev/VoxelWorld/Source/TerrainCore/Private/Tests/TerrainCrashMatrixTest.cpp:365). Introduced by T-125.

The matrix rejects `Head > Committed` but never rejects `Head < Committed`. It then checks recovered hashes against `Reference[Head]`. A recovery regression that loses already successful commits and returns an earlier internally consistent world can therefore satisfy the per-injection assertions. The separate clean-run equality check does not test this condition after an injected fault.

The upper-bound explanation is also incorrect for the general protocol: P-003 §2 explicitly makes a complete valid record recovery authority even when the flush response or acknowledgement was lost. Such a record may legitimately recover beyond the caller's count of successful returns. The current fault matrix uses either no bytes or a fixed 64-byte prefix, so it does not exercise that case.

**Required correction:** track the last positively durable commit as a lower bound and model the uncertain final append separately. Add a full-record-write/failed-response case and a negative control that drops an acknowledged record while leaving a valid shorter prefix. The recovered state must match the permitted head and must never fall below the acknowledged head.

This is a source-confirmed test-oracle defect. The existing crash matrix itself passed in this review; no production loss from this particular omission is asserted.

### F6 — P2: Linux bootstrap does not sync the parent containing a newly created world directory

**Locations:** [TerrainStorage.cpp:577](C:/Dev/VoxelWorld/Source/TerrainCore/Private/TerrainStorage.cpp:577), [TerrainWorldStore.cpp:83](C:/Dev/VoxelWorld/Source/TerrainCore/Private/TerrainWorldStore.cpp:83). Introduced by the T-126/T-128 bootstrap path.

Lease acquisition can create `Saved/Worlds/<name>` with `CreateDirectoryTree`. Bootstrap syncs that directory and its `roots`, `journal`, and `containers` subdirectories, but never the parent `Saved/Worlds` containing the newly created world's name (nor newly created ancestor entries). Flushing the world's contents does not establish durability of the world's own name in its parent. The directory-entry requirement is documented by the [Linux fsync manual](https://man7.org/linux/man-pages/man2/fsync.2.html).

Consequently P-005 §6 overstates that the Unix bootstrap window is closed. A power loss after a newly created world starts acknowledging edits is not covered by the stated namespace proof at its top-level directory entry.

**Required correction:** durably publish newly created directory entries through their containing parents before admission, or explicitly require the world directory hierarchy to have been durably provisioned beforehand. Add platform-level coverage of the directory publication sequence. This finding is source analysis against the documented contract; Linux compilation and a power-loss experiment were not available/performed, and no actual directory loss was reproduced.

## Verification performed on the reviewed source

| Check | Result / evidence |
|---|---|
| VoxelWorldEditor Win64 Development | Build succeeded; target already up to date |
| VoxelWorld Win64 Development | Build succeeded; target already up to date |
| TerrainCore automation | **42/42 Success**, exit 0; `Saved/Logs/CodexReview-20260921-Automation.log` |
| Delayed settlement + hard kill + restart | **Reproduced F1**, W=0 below G=3 |
| Delayed settlement stress | **Reproduced F3**, maximum 34; later terrain and ledger convergence passed |
| Stress with absent ledger | **Reproduced F4**, audit FAIL followed by successful driver exit |
| `python Tools/Test-TerrainCheckpoint.py` | **PASS**, four launches, restart hashes, checkpoint restore, wrong-base refusal with every save byte unchanged; `Saved/Logs/CheckpointTest-e56c6011ae34` |
| `python Tools/Test-TerrainLease.py` | **PASS**, second process refused with unchanged bytes; hard-killed holder followed by successful successor; `Saved/Logs/LeaseTest-8c04eed42863` |
| Multiplayer: 2 rounds, 12 seconds, observer, checkpoint capture, drop op 5 | **PASS**, four clients each round, both ledger audits; `Saved/Logs/T101B-MP-20260921-212331` |

Baseline automation also runs the container, background-retention/epoch, writer-lease, journal, capture, join/resync, and ledger tests. The startup log contains GameFeatureData asset-manager errors outside the terrain test results; these were not counted as terrain test failures or investigated as part of this change scope.

The real 256-chunk retention measurement, the full 5,000-edit default stress profile, real-plugin `Terrain.AdapterChecks`, and the stock randomized settlement kill matrix were not rerun here. Their historical results remain historical evidence. The additional tests here targeted the contracts the existing green suite missed. Concurrent regression runs and intentional ledger delays make this session unsuitable for revising CP-020 performance numbers.

## Assessment and remaining limits

The modular boundaries remain intact in this diff: no gameplay plugin calls were introduced, TerrainCore still avoids SQLite dependencies, the new EntityStore dependency is recorded by D-046, and both MCP plugins retain their editor-only target allowlists. The lease, ordinary restart, and live resync paths have fresh passing evidence. The material/yield implementation has a coherent shared occupancy definition and server-selected Fill policy; this review found no additional demonstrated material mint.

The most consequential misses are **integration and failure-path coverage**. Correct isolated pump/ledger tests did not establish the actual service's G/W wiring. Correct capture abort behavior in the pump did not establish that its owner observes every abort. Positive stress hashes did not establish settlement success.

Known limitations are not new findings: the measured 96 edits/s failure (R-018), unbuilt Linux target (R-007), identity lifetime (R-017), journal trimming, retention pins, resync rate limiting, and the untested listen-server path remain as documented. Nothing here closes those risks or authorizes a change to their scope.

Two reporting qualifications also deserve cleanup at an authorized checkpoint: the stress script calculates percentiles of **per-second frame maxima**, not a percentile over all frames; and several older ARCHITECTURE/settings comments still describe capture as off/synchronous or requiring an empty queue. Follow the current status notes and code rather than those historical paragraphs.

**Recommended order:** correct and regression-test F1 and F2 first; enforce the window and harden the test verdict/oracle (F3–F5); correct the Linux bootstrap contract before claiming its namespace guarantee (F6). Then re-run the full default stress profile before proceeding with T-133's batching change. These are recommendations for the Director; no implementation or decision entry was made.

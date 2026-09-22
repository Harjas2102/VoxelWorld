→ No action. For your reading only.

# P-011 — The edit stress profile and E-6: what 96 edits a second costs

**Status: measurement report and Architect rulings, 2026-09-21. Implemented as T-132.**
**Author:** Claude Opus. **Risk:** R3 (it changed tick cadence, checkpoint gating and client apply
timing). **Base:** `d43504b` (CP-019).
**Authority:** gate-critical item 8 (BACKLOG), E-6, ARCHITECTURE §7.1–7.3, P-003 §2's
"measure … sustained commits/s versus 96 ops/s", D-023.

Writer and reviewer are the same agent. §6 is the self-review.

---

## 1. The profile

`Tools/Test-TerrainStress.ps1` drives a dedicated server with **32 synthetic sources at the queue's
own limit of 3 edits/s each: 96/s, ARCHITECTURE §7.1's design point.** They dig a 100 m-radius
region around the PlayerStart through the **real** queue, validation, journal, checkpoint capture,
retention and settlement.

- **Phase A:** until 5,000 edits have committed.
- **Phase B:** a fresh client joins while the digging continues. This is E-6.
- **Phase C:** digging stops, everything drains, the joiner hashes every edited chunk it holds
  (density **and** material), the server compares, and the ledger is audited.

Every second the server logs committed edits, its longest service tick and frame, queue depth,
unsettled records and dirty chunks. Unreal Insights tracing can be switched on with
`-ExtraArgs '-trace=cpu,frame','-tracefile=…'`, and `UnrealInsights.exe
-ExecOnAnalysisCompleteCmd="TimingInsights.ExportTimerStatistics …"` exports the timers headlessly.

## 2. Five defects the profile found, all fixed

| # | Defect | How it showed | Fix |
|---|---|---|---|
| 1 | **The service ran about 4 times per server frame.** It was a 10 ms looping timer, and Unreal fires a looping timer once per *elapsed interval*, so on a 30 Hz server every per-frame budget (edit pump, capture slice, retention step) was really four times its size | Server frames reached 164 ms at p95 and 310 ms at worst. The Insights export showed 3,355 service calls in 847 frames, and 16.4 s of a 37 s game-thread trace inside the service | The service ticks exactly **once per world frame** (`FWorldDelegates::OnWorldTickStart`) |
| 2 | **Checkpoints starved under sustained load.** The start gate required an empty edit queue and zero unsettled records, and under load neither is ever true. It was hidden by defect 1, whose repeated ticks drained the queue | **0 checkpoints in 7,871 edits**, noticed because the save was implausibly small (6.1 MB). Replay and the journal would have grown without bound | A cut needs only **no half-executed transaction** (`IsBetweenTransactions`); waiting jobs commit at later sequence numbers. P-003's G ≤ W invariant moves to **publication**: all the chunk work proceeds, and the root waits until settlement reaches G. Result: 29 checkpoints and 29 retention cycles in the run |
| 3 | The console edit path pumped the queue inline and **bypassed the 32-record settlement window** (T-131) | Found by reading while building the harness | That pump respects the window too |
| 4 | Residency pins asked the plugin for **collision and navmesh for every chunk ever edited**, on the server, forever | 285 plugin invokers in a 1,500-edit run | Pins are residency only. A pin exists so capture, restore and replay can read a chunk, not for physics. p95 fell from 164 to 136 ms before defect 1 was found |
| 5 | A client installed every snapshot that arrived in one frame, **all in that frame** | A worst frame of 401–416 ms on the joiner | An **ordered inbox**: ops and completed snapshots are applied in exact channel order under an 8 ms per-frame budget, with at least one item per frame. P-008's argument is unchanged, since order is preserved and only *when* moves. Verification requests drain the inbox first |

**Attribution runs** (1,500 edits each, before defect 1 was found):

| Variant | Frame p95 (worst) |
|---|---|
| Everything on | 164 ms (310) |
| Saving off | 35 ms (78) |
| Checkpoints off | 35 ms (76) |
| Retention off | 202 ms (332) |
| Pins without collision | 136 ms (226) |

That pointed at checkpoints, and the trace then showed the real cause was defect 1.

## 3. The result on this machine (final source, 5,000 + live edits)

| Measure | Result | Against the design |
|---|---|---|
| Correctness under load | **PASS.** 254 edited chunks identical on the joiner in density and material; ledger audit exact over 7,659 edits (5,736 paid, 96 balances) | — |
| **Sustained throughput** | **71–78 edits/s median** (phase A 71.4/s overall); 1,463–2,055 `QueueFull` refusals; worst queue age 8.0 s | **Below 96/s** |
| **Server frame** | **p95 65 ms (phase A), 80 ms (phase B); worst 88 ms**; the service's own tick p95 is 63–76 ms | **Above the 33 ms frame** |
| Journal append+flush, per edit | 3.5 ms average, 37 ms max, on the game thread | At 96/s this alone is about 34% of every second |
| Backend apply | max 23 ms, including copy-before-write chunk captures | The §7.1 per-op budget is 8 ms |
| Settlement | latency 60 ms average, 112 ms max; at most 6 unsettled; **the 32-record window never filled** | Fine |
| Checkpoints | 29 captures and 29 retention cycles; each capture re-reads about 250 chunks at roughly 3 ms per chunk | The cost scales with the number of chunks dirtied, and this workload spreads edits widely |
| **E-6: the joiner** | **caught up in 6.1 s while editing continued**: 243 snapshots, **1.6 MB**, 1.74 MB total over the connection | — |
| Joiner install cost | 22.7 ms median, 31 ms p95, 36 ms max per chunk, **one per frame**; 5.4 s of total install work spread across frames | Budgeted |
| Joiner frame | only its start-up frame is long (403 ms). A control run joining an almost untouched world (32 snapshots) shows the same 410 ms, so it is engine and plugin warm-up, not snapshots | — |
| Save | journal about 235 B/edit (1.8 MB over 7,659 edits); 55.8 MB in total, mostly two retained checkpoint generations of about 250 Dense chunks; ledger 0.35 MB plus a 4 MB WAL | — |
| Server memory | 1,650 MB → 2,331 MB over the run | Not investigated |

**Gate 8's verdict: correctness PASS, performance FAIL at the design point, on this machine.**
There are two named costs, both on the game thread:
1. **One journal flush per edit.** P-003 §2 anticipated this: "If either game-thread latency or
   sustained throughput fails, return with evidence before adopting journal batching." This is
   that evidence.
2. **Checkpoint capture reads**, about one chunk read (3 ms) per edit when edits are spread over a
   wide region. The trigger (every 256 edits) re-captures the whole dirty set each time.

## 4. Rulings (D-047)

- The five fixes in §2 stand.
- **T-133 is journal group commit**, plus a measured capture-trigger policy, with this profile as
  the gate:
  - Every commit in one pump call is appended, then **one** flush covers them all, and only then
    is any of them broadcast or submitted for settlement. That is P-003 §2's order ("durable
    before anyone is told"), per batch instead of per edit.
  - Capture triggers get evaluated against the *chunk* cost, not only the edit count.
  - It needs a short spec, because it changes the commit protocol's granularity and the crash
    matrix must cover a torn multi-record append.

## 5. Evidence that nothing else moved

After the fixes: TerrainCore automation **42/42**; `MP.Convergence` passes 3 rounds with an observer
(ledger audit PASS each round); `-DropOp` passes both rounds; `Test-TerrainCheckpoint.py`,
`Test-TerrainLease.py`, `Test-TerrainSettlement.py`, `Test-TerrainRetention.py` and
`Terrain.AdapterChecks` all pass. The last four are recorded in HANDOFF from the final run.

## 6. Self-review: what could still be wrong

- **One machine, Windows, one SSD.** The Linux server's fsync cost is unknown (R-007). The numbers
  describe this box, not the shipping target.
- **The workload is adversarial on purpose:** 32 diggers spread over 100 m, 80% digging. Friends
  digging near each other dirty fewer chunks, so checkpoints cost less. The throughput verdict
  holds regardless, because the journal flush cost is per edit.
- **The synthetic sources skip reach and pawn-clearance checks** (admin source), so real players
  cost slightly more at validation.
- **Frame time comes from `FApp::GetDeltaTime`** on a server capped at 30 Hz, so 33 ms is the
  floor, not headroom.
- **Server memory grew 680 MB over the run.** That is uninvestigated, and could be plugin data for
  a dug region, the capture batch, or the ledger WAL.
- **The client inbox adds up to one frame of latency to every edit** a client sees. That is not
  measured as felt; 30 Hz server updates dominate it anyway.
- **A checkpoint's publication can now wait on settlement.** The wait was not observed as
  significant (60 ms average latency), but a stalled ledger would now stall publication, not the
  cut. That is safe (the journal still covers it), just slower.

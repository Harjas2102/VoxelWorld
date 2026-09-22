→ No action. For your reading only.

# P-012 — Journal group commit, and a checkpoint trigger priced in chunks

**Status: spec, Architect rulings and result, 2026-09-21. Implemented as T-133; recorded at CP-023 (D-050).**
**Author:** Claude Opus. **Risk:** R3 (it changes the commit protocol's granularity).
**Base:** `cd1e462` (CP-022). **Authority:** P-003 §2 ("if either game-thread latency or sustained
throughput fails, return with evidence before adopting journal batching"), P-011 §4 (D-047), D-023.

Writer and reviewer are the same agent. §7 is the self-review. The Codex review of CP-016 to CP-020
found six defects in integration and failure paths that the author's component tests had passed,
so every rule here has a service-level test and a mutation check.

---

## 1. The evidence that asks for it

P-011 measured the server at 67–78 edits/s against the 96/s design point, and named two costs:
- **one journal flush per edit**, 3.5 ms on average, on the game thread;
- **checkpoint re-reads**, about 2.8 ms per chunk, at a trigger of every 256 edits.

The queue pumps under an 8 ms per-frame budget, so a 3.5 ms flush per edit spends most of that
budget on the disk. That, not the terrain work, is what caps edits per frame.

The trigger has a measured price as well. Replay at boot costs 0.3–0.4 ms per edit (the
settlement kill test's restarts: 17 edits in 6 ms, 37 in 10–15 ms), while capture costs about
2.8 ms per chunk read. **One chunk re-read costs about eight edits' worth of replay.** At 256
edits and about 140 dirty chunks, a capture costs roughly 50 times the replay it saves.

## 2. The rule: durable before anyone is told, per batch

Today, per edit: apply → append and flush → broadcast, settle, receipt.

**Group commit, per pump call:**
1. each edit is validated and applied as now, and its record is **staged** (encoded in memory; no
   I/O). The revision index and the dirty set advance at stage time. Both are server RAM and are
   discarded on a storage fault, exactly as today (P-003 §2, "unbroadcast provisional RAM");
2. before `Pump` returns, if anything was staged, **one append writes every staged record, then
   one flush covers them all**;
3. only after that flush succeeds is anything published: broadcasts, settlement submits, receipts.
   Records are published in OpSeq order.

Nothing outside the server learns of an edit before its record is durable. That is the same
promise P-003 §2 makes, at the granularity of a pump call instead of a single edit.

**The save format does not change.** The records are byte-identical and appended in the same
order; only how many share a write changes. Old worlds open unchanged.

## 3. Failure

- **The flush fails:** the journal's tail is uncertain, the writer blocks itself (as today), and the
  service marks the storage fault and closes admission. **Nothing from the batch is published**, and
  every job that staged in it is refused with `ShuttingDown`. The queue's sequence rolls back to the
  first staged OpSeq, so its RAM view matches the durable head.
- **Recovery after a crash mid-batch:** the torn-tail rule keeps any complete leading records of
  the batch and drops the rest. **Any prefix of the batch is a legal recovery**, because no record in
  it was acknowledged. The crash-matrix oracle (D-049) becomes: acknowledged head ≤ recovered head
  ≤ acknowledged head + records in doubt, where the whole failed batch is in doubt.
- **Staging fails** (a record cannot be encoded): that edit is refused with `ShuttingDown` as today,
  and the rest of the batch is flushed and published normally.

## 4. The settlement window, and the cut

- The 32-record window (P-003 §2, D-049) counts **unsettled plus staged** records. Staged records
  are submitted the moment their flush succeeds, so counting only the unsettled ones would let one
  batch overshoot the window by its own size.
- Checkpoints, snapshots and retention only ever see flushed state. The flush happens before `Pump`
  returns, and all three run outside the pump. A cut's G is still the journal writer's head, which
  only moves on a successful flush.
- Copy-before-write is unchanged. It runs inside Apply, before the backend changes the chunk,
  whether or not earlier records in the same batch are durable yet. The cut is at G, the durable
  head, and every staged edit is above it.

## 5. The checkpoint trigger, priced in chunks

A capture is taken when any of these holds:
- **the dirty set reaches `CheckpointDirtyChunkTrigger` (256):** it bounds the capture's own size
  and its pack in memory. Unchanged;
- **the replay tail reaches `CheckpointMaxReplayOps` (4,096 edits):** it bounds boot time, at
  roughly 1.6 s of replay at the measured cost. New;
- **the replay it saves is worth the chunks it reads:** at least `CheckpointOpTrigger` (256) edits
  since the last cut, **and** at least `CheckpointOpsPerDirtyChunk` (8) edits per dirty chunk. New.
  8 is the measured ratio of capture cost per chunk to replay cost per edit.

For a quiet world that edits a few chunks, this is the old behaviour: 256 edits over 32 chunks
still captures. For the stress workload, 140 dirty chunks now wait for 1,120 edits instead of 256.

## 6. Tests

- **Crash matrix:** a second, batched session writes three records per append. Four fault modes:
  refused, torn at 64 bytes, torn at 300 bytes (inside the batch, after its first record), and
  written in full with failure reported. The oracle is §3's window. The test asserts that a
  mid-batch prefix was actually recovered, so the torn-batch case is known to have run.
- **`Capture.Service`:**
  - one pump call over many edits appends to the journal **once**;
  - every receipt arrives after its record is durable;
  - a failed flush publishes nothing, refuses the batch's receipts, rolls the sequence back and
    faults storage;
  - the settlement window counts staged records.
- **Trigger:** the three triggers, unit-tested against the service's decision.
- **Mutation checks:** per-edit flushing restored; receipts released before the flush; the window
  counting only unsettled records; publishing on a failed flush.
- **Measurement:** several full stress runs (the spread is about ±5 edits/s), plus the settlement kill
  test, checkpoint, lease and retention harnesses, 3 MP rounds, and the automation suite.

## 7. Self-review: what could still be wrong

- **A batch widens the uncertainty window from one record to a batch** (at most 32, usually 3–4
  at the design rate). It stays invisible to players, because none of those records was
  acknowledged, but a crash now loses up to a frame's worth of edits instead of one. That is P-003
  §2's documented trade for batching.
- **Latency:** an edit is now acknowledged at the end of its pump call rather than right after its
  own flush. That is milliseconds, on a queue whose measured wait was seconds.
- **The trigger's ratio is one machine's.** Replay cost on the Linux server, and for larger edits,
  is unmeasured (R-007). The 4,096 cap is what keeps a wrong ratio from making boot unbounded.
- **Frame time may not reach 33 ms even if throughput does.** P-011 names plugin apply at up to
  23–43 ms per edit. If frames stay long, the result says so; it does not claim the gate.

## 8. Result (final source, this machine)

**Gate 8's performance half passes on this machine.** Three consecutive full stress runs, 5,000
edits plus 20 s live with a joiner, all PASS:

| | Before (CP-022) | After, runs 1 / 2 / 3 |
|---|---|---|
| Sustained edits/s (median per second) | 67–78 | **99 / 99 / 99** |
| Refused as queue-full | 1,600–2,700 per run | **0 / 0 / 0** |
| Longest wait in the queue | 6.7–9.6 s | **0.27 s** |
| Server frame (p95 of per-second maxima) | 72–97 ms | **34–38 ms**; one phase at 49 |
| Checkpoints per run | 28–31 | 4–5 |
| Records per flush | 1 | 3.2 |
| Ledger audit | exact | exact over 8,053–8,184 edits |
| Unsettled max (window 32) | 6 | 21–25 |

Correctness is unchanged: 262 edited chunks identical on the joiner in density and material, and
the ledger exact, in every run.

**Attribution:** one run with group commit but the old trigger (`CheckpointOpsPerDirtyChunk=0`)
reached 99/s with no refusals, but frames stayed at p95 70–84 ms over 31 checkpoints.
- **Group commit bought the throughput.**
- **The chunk-priced trigger bought the frame time.**
- Both halves are needed.

**Boot replay** of a stress world: 29 edits in 10 ms (0.34 ms each), which confirms §1's price.

**What is still not good:**
- **The worst frame each run is still 88–96 ms.** It occurs a handful of times per run, at
  checkpoint or retention moments.
- **The joiner's one 406 ms start-up frame** is engine and plugin warm-up, as before.
- The frame figure is a p95 of per-second *maxima*, which overstates a typical frame. It is not a
  percentile over all frames.

**Regression:** 43/43 automation. The crash matrix covers two sessions and four fault modes; torn
batches recovered as a strict prefix. Mutations M8 to M11 each fail a test. The settlement kill
test, checkpoint, lease and retention (256 hashes) harnesses PASS; MP 3 rounds with observer and
capture PASS, and `-DropOp 20` PASS. Both targets build.

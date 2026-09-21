→ No action. For your reading only.

# HANDOFF

**Last session:** CP-015 · T-125 · 2026-09-21 · Claude (Opus 5)
**Branch:** `main` · **Tests:** 37/37 TerrainCore automation.

---

## What shipped: the crash matrix (D-040)

P-003 §8 asks for injection at *"every payload/page/descriptor write/flush, root overwrite/flush,
segment discovery/rotation and every deletion"*. The tests that existed each broke **one case
somebody had thought of** — a torn root slot, a torn pack, an interrupted compaction. That is not
the list, because the write nobody thought to break is the one that breaks.

So `Persistence.CrashMatrix` runs a scripted session (nine edits, two checkpoints) and then runs
it again **once per mutating write**, failing that write and only that write, in two modes: a
hard refusal, and a torn write that lands half on disk and then reports failure.

`FTerrainFaultDevice` gained `FailAtMutation`, counting `WriteNew`, `OverwriteInPlace`, `Append`
and `Delete` as one ordered sequence — because a crash is a **moment**, and the per-op-type
faults could only express a **kind**.

### The assertion, which is the actual work

"Recovery works" is not worth proving. A world that comes back holding a state it was never in is
**worse** than one that refuses to come back, because nobody finds out.

The reference run therefore records the exact terrain after **every** committed operation, and
each recovered world must equal the reference **at its own OpSeq** by chunk hash. Recovery may
lose the tail — a crash before a record was durable means the edit did not happen, which is what
the commit ordering promises. It may not land between two operations, ahead of the journal, or on
terrain that never existed.

| Outcome over 40 injections | |
|---|---|
| recovered to an exact point in real history | **28** (20 losing the tail) |
| refused to open | 12 — **all of them crashes during world creation** |
| landed on a state that never existed | **0** |

The bounded refusal count is the stronger claim: **once a world exists, no single crash made it
unopenable.** Crashing partway through creation leaves nothing to open, which costs nothing.
Everything after that was survived by the two-slot root, the torn-tail rule, or
unreferenced-garbage containment. Recovering twice reaches the same head and the same terrain,
which is §8's repeat-recovery requirement.

### Worth knowing: packs made this affordable

The session has only **20** mutating writes, because a capture writes one pack instead of one
file per payload, page and descriptor (D-036). Before packs it would have had well over a
hundred crash points. Fewer, larger, better-understood steps — the same property that made the
six-operation device surface worth having.

## What this does not close

- **`Restart.CrashMatrix`'s terrain half is satisfied.** Its other half — *"no duplicated or
  missing payout, no durable ore without durable removal"* — needs settlement and SQLite, which
  are build step 6. **DEF-1 stays open.**
- **It models lost writes, not power loss.** The matrix runs against a memory device behind a
  fault decorator. Whether a file's *name* survives a power cut is **R-015**, and no in-process
  injection can speak to it.

## Remaining queue

1. ~~Bulk `ReadRegion`~~ (D-035) · ~~packs~~ (D-036) · ~~trigger measurement, capture on~~
   (D-037) · ~~incremental pump, DEF-2~~ (D-038) · ~~object retention, gated~~ (D-039) ·
   ~~crash matrix~~ (D-040).
2. **R-015 namespace durability** — next, and now the single gate holding back *two* things:
   letting retention run for real, and extending the crash matrix's claims from lost writes to
   power loss.
3. Production retention: incremental, off-thread, P-003 §5's pins and epoch protocol. **DEF-9
   is not closed.**
4. Journal trimming — correctly deprioritised at ~49 KB per checkpoint interval against 33 MB.
5. Build step 6 (settlement) is what closes DEF-1 and the payout half of `Restart.CrashMatrix`.

## Standing note

Six increments, six measurements. The recurring lesson has shifted: it started as "measure before
you optimise" (three wrong projections), became "check that what you varied actually varied" (an
idempotent workload that measured nothing), and is now "assert the state, not the absence of an
error" — a test that only checks recovery *succeeds* would have passed on a world that came back
wrong.

# HANDOFF

**Last session:** CP-015 · T-121 · 2026-09-20 · Claude (Opus 5)
**Branch:** `main` · **Tests:** 34/34 TerrainCore automation, `Terrain.SelfTest` PASS,
`Adapter.DensityContract` 20/20, `Test-TerrainCheckpoint.py` PASS, `MP.Convergence` PASS
(3 clients, 243 commits, **zero stall warnings**).

---

## What shipped: objects are written in packs

The checkpoint stall was 85% index path-copy: 49 durably written pages holding about **7 KB
between them**, costing 0.168 s. A durable write is one `fsync`, and an `fsync` costs **~3 ms
regardless of size** — a 160-byte index page costs what a megabyte costs.

**I measured the three candidates before building any of them**, which mattered, because the
one D-035 ruled for first does not work:

| Strategy over 49 small files | Time |
|---|---|
| write each and flush it (what existed) | 151.6 ms |
| **hold the handles, flush them all at the end** | **155.8 ms** |
| write and close each, then reopen and flush | 55.6 ms |
| **one file, one flush** | **2.3 ms** |

A deferred barrier buys nothing — `FlushFileBuffers` is per file whenever you call it. The cost
was never *when* the store syncs; it is **how many files it syncs**, and a capture synced 59.

So a capture now buffers its payloads, index pages and descriptor into **one pack** (P-004 §13),
written and flushed once, strictly before the root slot. Two `fsync`s per capture instead of 59.
Content addressing is untouched — objects are still named by and verified against their BLAKE3
digest, loose or packed — so no other section's byte tables moved.

| | before | after |
|---|---|---|
| warm solo capture, 8 chunks | 0.197 s | **0.010 s** |
| three-client load, 4 chunks | 0.14–0.25 s | **0.026–0.035 s** |
| stall warnings per 30 s MP round | 15, ~0.34 s each | **0** |

Restore works across process restarts: the four-launch harness restores 8 chunks from a pack
written by an earlier process, replays zero edits, and all eight chunk hashes are identical.

**Crash containment is the same argument §12 always made.** The pack is flushed before the root
slot that names anything in it, so a pack failing its trailer checks belongs to a capture that
never published: ignored in full, explicitly not an error, because refusing to open over it
would turn collectable garbage into a dead world. Tested — torn trailer, flipped body bit, id
reuse, abandoned batch, loose/packed coexistence.

## Next: measure a capture at its real trigger

`bCheckpointCapture` is still **false**, but the reason changed from *"the measurement failed
the gate"* to **"the measurement has not been taken."**

Reading is the dominant phase again (~75% of a capture under load). Extrapolating 4 chunks to
the 256-chunk trigger gives roughly 1.5 s — not multi-second, and not evidence. **This
checkpoint has now twice acted on a plausible projection and been wrong**: P-003 §4 predicted
reading was the cost (it was 1.5%), and D-035 predicted the fsync barrier was the fix (it was
4 ms slower). So build a harness that actually dirties 256 chunks, measure, and *then* decide
the default.

## Remaining queue

1. ~~Bulk adapter `ReadRegion`~~ — done (T-120, D-035).
2. ~~Index write amplification~~ — done (T-121, D-036), and it was a file-count problem.
3. **Measure capture at the 256-chunk trigger** — the gate on `bCheckpointCapture`.
4. Incremental capture pump — now genuinely the right lever, since reading is dominant again.
5. Retention / GC — **the store still only grows, and a pack cannot be reclaimed object by
   object** (P-004 §13.6). Retention now has to reason about packs, not just loose objects.
6. Crash matrix.

## Standing note

Both increments this session were cheap to price and expensive to guess at. The fsync
comparison took three minutes and saved building the wrong fix. Measure the obvious thing
before you build it.

# HANDOFF

**Checkpoint:** CP-018 · **Date:** 2026-09-21 · **Branch:** `main`
**Agents:** Claude (Opus 5) wrote and self-reviewed T-126 (CP-017) and T-127 (CP-018) under
the Director's standing instruction. Writer and reviewer were the same agent, which is weaker
evidence than a cross-vendor review.
**Expected next agent:** either (D-028).

---

## Where the project is

**The server remembers, and its save no longer grows without bound.**
- **CP-017 (D-041, P-005):** an open world creates and removes no file names. R-015 is closed
  by construction.
- **CP-018 (D-042, P-006):** a background collector reclaims superseded checkpoint data after
  every checkpoint. It works on a worker thread, and the game thread takes only bounded steps.
  P-003 §5's epoch rule means it deletes nothing once a capture has referenced anything since
  its mark.

## Evidence for CP-018 (re-run on the final source)

- **37/37** TerrainCore automation. It includes an **epoch matrix**: a 40-step cycle interrupted
  after every step, by a dedup store or an open capture batch — 80 cases, no cut or delete after
  any interruption, and both generations restored every time.
  - **The matrix is sensitive.** With the pre-cut epoch check disabled, it fails at steps 37
    and 38.
- **Threaded cycle on a real disk:** 18 frames, with a longest game-thread step of 2.7 ms.
- **`Tools/Test-TerrainRetention.py --background`:** the save holds at 67.8 MB over three
  generations, reclaim takes a longest step of 7.9 ms, and all 256 hashes survive a restart.
- **Explicit mode:** PASS.
- **`Tools/Test-TerrainMultiplayer.ps1 -CheckpointCapture`:** 486 commits, 30 checkpoints and
  30 completed background cycles; none abandoned or failed.
- **`Tools/Test-TerrainCheckpoint.py`:** PASS.
- **Migration:** a real pre-P-005 world migrated through the collector with identical hashes.
- **Not re-run:** `Terrain.SelfTest` and `Adapter.DensityContract`. Neither touches this code.

## T-128 breadcrumb (post-CP-018, not yet checkpointed)

**T-128 is implemented, tested and committed. The next `checkpoint` records it.** The spec, its
evidence and the self-review are in `Docs/proposals/P-007-exclusive-writer-lease.md`.
- The lease is an OS lock on `writer.lock` (Windows `LockFileEx`, Unix `flock`). It lives in the
  device seam, the store holds it for its whole life, and the service takes it before it checks
  whether the world exists. If the lease is held elsewhere, the service logs `StoreBusy` and
  closes terrain access.
- Evidence: **38/38** automation. The new `WriterLease` case was mutation-tested: with the lock
  removed, it fails. `Tools/Test-TerrainLease.py` passes with real processes: a second server is
  refused and changes no byte of the save, and after the first server is killed hard a third
  opens the world and passes SelfTest. `Test-TerrainCheckpoint.py` and one-round
  `MP.Convergence -CheckpointCapture` also pass.
- **New defect found, predating T-128:** after a server travel on a persisted world, clients
  apply none of the server's edits. The multi-round `MP.Convergence` fails in round 2. I
  reproduced it on `614201d` with the T-128 work stashed. This is **T-129**, next.
- Writer and reviewer were the same agent.

## What is NOT done, stated plainly

- **T-129, terrain after server travel.** Round 2 of a multi-round `MP.Convergence` fails on a
  persisted world: the server commits, and the clients apply 0. Reproduce with
  `Tools/Test-TerrainMultiplayer.ps1 -Rounds 2 -DurationSeconds 20 -CheckpointCapture`.
- **Journal trimming.** The journal grows forever: about 49 KB per checkpoint interval. It needs
  a pre-created segment ring, because `Rotate` creates a name (P-005 §8).
- **Retention pins.** No backup, migration or sync consumer exists. When one does, its pin must
  move the reference epoch and join the mark (D-042 §4).
- **Linux has never been built.** The first build must compile P-005's `fsync(dir)` and run
  `Storage.PlatformDevice`.
- **Windows bootstrap window** after world creation (P-005 §6): best-effort directory sync only.
- **DEF-1:** settlement and SQLite (build step 6).
- **Cross-agent review of P-005 and P-006** is worth doing when Codex is next available.

## Next safe action

**T-129: clients receive no terrain after a server travel on a persisted world.** Start with the
round-2 server and client logs from the reproduction above. The server reopens the store at
G>0 with nonzero chunk revisions. Suspect the client subscription and pristine acknowledgement
path, which was last proved across travel at CP-014, before persistence existed. After that,
Phase 1's gate items 1C (material yield) and 1E (join-in-progress) remain before the backend
decision.

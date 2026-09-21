# HANDOFF

**Checkpoint:** CP-017 · **Date:** 2026-09-21 · **Branch:** `main`
**Agents:** Claude (Opus 5) wrote and self-reviewed T-126 under the Director's standing
instruction (writer = reviewer; weaker evidence than a cross-vendor review — see below).
**Expected next agent:** either (D-028).

---

## Where the project is

**R-015 is closed by construction.** After bootstrap, an open world creates and removes no file
names, so its crash safety no longer rests on whether a new directory entry survives a power cut.
Spec: `Docs/proposals/P-005-namespace-durability-containers.md`. Ruling: **D-041**.

- Objects are **frames** in four pre-created files `containers/c.0`–`c.3`. A frame is a 40-byte
  header that names its own offset, followed by an **unchanged** P-004 §13.3 pack image.
- A capture is one append. **An append first truncates any torn tail** back to the last valid
  frame. Without that rule, a frame written after garbage would be named by a root but
  invisible to the next boot's scan.
- The boot scan stops at the first bad header. A frame with a good header but a bad body is
  skipped.
- Retention copies live objects into the active container, flushes, verifies, and then
  truncates the source. Pre-P-005 `objects/` and `packs/` are read, migrated, and never
  written.

## Evidence (re-run on the final source)

- **37/37** TerrainCore automation.
- Retention test asserts **0 `WriteNew` and 0 `Delete`** after bootstrap across three captures
  and a full compaction.
- `Persistence.CrashMatrix`: 48 injections.
  - 28 recovered exactly.
  - 20 refused, all inside world creation (the first 11 writes, 4 of which pre-create the pool).
  - 0 landed on a state that never existed.
- `Tools/Test-TerrainRetention.py` PASS: 101,658,644 → 67,823,846 bytes.
  - All 256 hashes survive reclaim and restart.
  - **The file-name set is identical before and after reclaim on a real disk.**
- Migration of copies of two real pre-P-005 worlds (`RetentionTest-781145c776e9`,
  `MPTest-34819678…`): 2,824 and 78 live objects moved, every legacy file removed, hashes
  identical across open, migration and restart, and a second sweep reclaims 0 bytes.
- `Tools/Test-TerrainCheckpoint.py` PASS, including the wrong-base boot leaving every save file
  byte-unchanged.
- `Tools/Test-TerrainMultiplayer.ps1` PASS, both plain and with `-CheckpointCapture`: 485
  commits, 30 checkpoints, 3 of them via copy-before-write, no terrain warnings.
- Publish at the 256-chunk trigger: 0.032–0.033 s (0.035 s at CP-016).
- **Not re-run:** `Terrain.SelfTest` and `Adapter.DensityContract`. Neither touches the storage
  code that changed.

## Self-review (same agent wrote it — say so to any reviewer)

Three defects were found before any test ran, all fixed:

1. An append after a torn tail would have published an unscannable frame.
2. A corrupt body in the middle of a container would have hidden every later frame.
3. Migration reported gross bytes rather than net.

**A cross-agent review of P-005 and `TerrainStorage.cpp`'s container section is worth doing** when
Codex is next available. It is the same exposure R-016 described for P-004.

## What is NOT done, stated plainly

- **The Linux `fsync(dir)` branch in `FTerrainPlatformStorageDevice::SyncDirectory` has never
  been compiled.** The first Linux build must compile it and run `Storage.PlatformDevice`.
- **Windows bootstrap window.** Directory sync after world creation is `FlushFileBuffers` on a
  directory handle. NTFS accepted it here, but Microsoft does not document it. A loss makes the
  world refuse to open; it never opens wrong.
- **DEF-9: production retention.** `Terrain.Reclaim` is still behind
  `-TerrainRetentionExperiment`, now for that reason alone. It is a 0.3 s synchronous pass on the
  game thread at 256 chunks, run by hand, with no pins or epochs.
- **Journal `Rotate` still creates a name.** Production never calls it. Trimming must rotate
  within a pre-created segment ring instead (commented in `TerrainJournalWriter.h`).
- `Contains()` still checks the old loose-object path on disk for each new object, the same cost
  as CP-016. Not yet optimised.
- **DEF-1:** settlement and SQLite (build step 6).

## Next safe action

**T-127: production retention (DEF-9).** P-003 §5 specifies it: an incremental, off-thread
collector with retention pins and an epoch protocol, so reclaim can run without a game-thread
stall and without the experiment flag. The container layer already gives it a cheap unit of work:
compact one container per step.

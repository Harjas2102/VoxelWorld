# HANDOFF

**Checkpoint:** CP-020 · **Date:** 2026-09-21 · **Branch:** `main`
**Agents:** Claude (Opus 5) wrote and self-reviewed T-128 to T-132 under the Director's standing
instruction. Writer and reviewer were the same agent, which is weaker evidence than a
cross-vendor review (R-016).
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
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'C:\Dev\VoxelWorld\VoxelWorld.uproject' '-ExecCmds=Automation RunTests TerrainCore; Quit' -unattended -nopause -nosplash -nullrhi -log   # 42/42
python Tools\Test-TerrainSettlement.py            # hard kills + journal audit + two refusals
.\Tools\Test-TerrainMultiplayer.ps1 -Rounds 3 -IncludeObserver -CheckpointCapture   # joins, materials, ledger audit per round
.\Tools\Test-TerrainMultiplayer.ps1 -Rounds 2 -DurationSeconds 30 -CheckpointCapture -DropOp 20
python Tools\Test-TerrainLease.py ; python Tools\Test-TerrainCheckpoint.py ; python Tools\Test-TerrainRetention.py
.\Tools\Test-TerrainStress.ps1 -Edits 5000 -LiveSeconds 20   # gate 8 + E-6 (about 3 min)
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
- **Cross-agent review of P-005 to P-010** is worth doing when Codex is next available.

## Next safe action

**T-133: journal group commit and checkpoint trigger policy** (P-011 §4). Batch every commit in
one pump call behind a single flush, and broadcast and settle only after that flush. It needs a
short spec, and the crash matrix must cover a torn multi-record append. Gate it on
`Tools\Test-TerrainStress.ps1`: 96 edits/s sustained with server frames near 33 ms. After that,
1F's observations and the backend decision.

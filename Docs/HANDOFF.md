# HANDOFF

**Checkpoint:** CP-019 · **Date:** 2026-09-21 · **Branch:** `main`
**Agents:** Claude (Opus 5) wrote and self-reviewed T-128 to T-131 under the Director's standing
instruction. Writer and reviewer were the same agent, which is weaker evidence than a
cross-vendor review (R-016).
**Expected next agent:** either (D-028).

---

## Where the project is

**Phase 1's gate items 1B, 1C and 1D are done, 1E is done except for the E-6 measurement, and
1F remains.** Since CP-018:
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
```
In a standalone game, `Terrain.AdapterChecks` runs the plugin's density, material and E-1
checks, and `Terrain.LedgerAudit` recomputes every balance from the journal.

## What is NOT done, stated plainly

- **E-6 and the gate-8 stress profile (T-132, next).** Nobody has measured a heavy region with a
  joiner arriving mid-edit, or throughput against 96 ops/s with FULL-sync settlement.
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

**T-132: the edit stress profile, with E-6.** Script thousands of edits across a region (for
example with a longer `Terrain.DigStress`), then join a fresh client mid-edit. Measure server and
client frame time, snapshot bytes and install stalls, save growth, bandwidth, memory, settlement
latency and window saturation. Gate on measurements. After that comes 1F: collision, foliage, nav
and streaming observations, then the backend decision.

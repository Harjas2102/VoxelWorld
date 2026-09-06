# VoxelWorld

> **`VoxelWorld` is a development codename, not the title.** It names an implementation
> detail — the terrain representation — rather than the game. The final title should not
> mention voxels (D-015). Naming is open as D-008.

A realistic, persistent, multiplayer survival and industrial sandbox built in Unreal
Engine 5.7 for a self-hosted server of 16–32 friends. The world is made of material
rather than being an immutable stage: players dig, tunnel, quarry, and terraform, and
the server remembers all of it. Technology progression is framed as increasing scale of
control over the environment — hand labour, then a workshop, then power, then logistics,
then landscape-scale infrastructure. The factory does not merely grow on top of the map;
**the map physically records the factory's growth.**

Not commercial. Not space. Not blocky. Not Rust-scale.

## Start here

**→ [`Docs/OPERATIONS.md`](Docs/OPERATIONS.md)** — how to run a session, day to day.

Then, in order of authority:

| File | What it is |
|---|---|
| [`Docs/VISION.md`](Docs/VISION.md) | The founding document. Pillars, scope guards, drift checks. Outranks everything. |
| [`AGENTS.md`](AGENTS.md) | The constitution for any AI agent, any vendor. Roles, risk classes, engineering rules. |
| [`Docs/STATE.md`](Docs/STATE.md) | Where the project actually is right now. Read at every session start. |
| [`Docs/HANDOFF.md`](Docs/HANDOFF.md) | Current work, decision summaries, verification and the next safe action for either agent. |
| [`Docs/DECISIONS.md`](Docs/DECISIONS.md) | Every numbered ruling. Never deleted, only superseded. |
| [`Docs/BACKLOG.md`](Docs/BACKLOG.md) | Ordered task list. |
| [`Docs/RISKS.md`](Docs/RISKS.md) | Architectural unknowns, each with the experiment that resolves it. |
| [`Docs/ARCHITECTURE.md`](Docs/ARCHITECTURE.md) | System shape. **v1** — the implementation spec for terrain authority, persistence and replication. |
| [`Docs/archive/ARCHITECTURE_v0.md`](Docs/archive/ARCHITECTURE_v0.md) | Historical v0 stub. Its carried requirements now live in v1; do not implement against the archive. |
| [`Docs/GDD.md`](Docs/GDD.md) | Game design detail. |

`Docs/reviews/` holds external architecture reviews; `Docs/SETUP.md` is the record of how
the environment was built.

## Status

**CP-009 (2026-09-06) — Phase 1: Terrain Feasibility.** Milestone: *one hill is trustworthy.*

- Smooth digging and a through-tunnel were demonstrated in T-101A. The terrain backend
  remains provisional; multiplayer edits, persistence, join-in-progress and yield still
  need the T-101B gate.
- UE **5.7** builds from C++. T-112.1 and T-112.2 are complete: op codec, quantiser,
  backend interfaces, a memory backend and a reusable conformance suite.
- Last code validation, **CP-008**: build succeeded; **four TerrainCore tests passed,
  zero failures, exit 0**. CP-009 changes documentation only.
- **Next: T-112.3**, revision index and service skeleton. Claude is the expected next
  Implementer; either agent can pick it up from the handoff. Then T-112.5 (the D-025
  engine/tooling upgrade) and T-113 (production adapter and Blueprint service rewire).

## Build and headless tests

From `C:\Dev\VoxelWorld`, with the installed UE 5.7 and VS C++ toolchain:

```powershell
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat' VoxelWorldEditor Win64 Development '-Project=C:\Dev\VoxelWorld\VoxelWorld.uproject' -WaitMutex
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'C:\Dev\VoxelWorld\VoxelWorld.uproject' '-ExecCmds=Automation RunTests TerrainCore; Quit' -unattended -nopause -nosplash -nullrhi -log
```

Expected now: `Result: Succeeded`, four successful TerrainCore tests and automation
exit 0. `Revision.Monotonic` becomes the fifth test at T-112.3. Test output is in
`Saved/Logs/VoxelWorld.log`. See [SETUP](Docs/SETUP.md) for the environment record and
[STATE](Docs/STATE.md) for installed dependencies.

## Working across agents

Claude and Codex alternate to use the Director's available usage windows (D-028).
There is one active Implementer at a time. Each reads the same project docs, leaves
brief decision/evidence breadcrumbs, and ends with a handoff addressed to whoever
continues. The stepwise protocol is [OPERATIONS §5.1](Docs/OPERATIONS.md#51-breadcrumbs-and-formal-handoff).
R3 work still requires an independent reviewer; switching agents does not waive it.

The docs are the project's memory — models are replaceable staff, and `/Docs` is the only
source of truth.

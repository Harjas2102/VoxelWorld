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
| [`Docs/DECISIONS.md`](Docs/DECISIONS.md) | Every numbered ruling. Never deleted, only superseded. |
| [`Docs/BACKLOG.md`](Docs/BACKLOG.md) | Ordered task list. |
| [`Docs/RISKS.md`](Docs/RISKS.md) | Architectural unknowns, each with the experiment that resolves it. |
| [`Docs/ARCHITECTURE.md`](Docs/ARCHITECTURE.md) | System shape. **v1** — the implementation spec for terrain authority, persistence and replication. |
| [`Docs/archive/ARCHITECTURE_v0.md`](Docs/archive/ARCHITECTURE_v0.md) | The v0 stub, archived. Its header lists what v0 still covers that v1 does not. |
| [`Docs/GDD.md`](Docs/GDD.md) | Game design detail. |

`Docs/reviews/` holds external architecture reviews; `Docs/SETUP.md` is the record of how
the environment was built.

## Status

**CP-002 (2026-09-05) — Phase 1: Terrain Feasibility.** The current question is whether a
volumetric terrain backend can survive concurrent multiplayer edits, save/restart,
join-in-progress, and material-yield accounting. Milestone: *one hill is trustworthy.*

The docs are the project's memory — models are replaceable staff, and `/Docs` is the only
source of truth.

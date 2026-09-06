# BACKLOG.md — Ordered Task List (v2)

> Tasks are pulled top-down. New ideas are **appended** to their phase, never inserted,
> unless the Director explicitly reorders. Completed tasks move to the Done log at the
> bottom with their checkpoint number.
>
> **v2 (CP-002)** — restructured around the terrain feasibility gate per **D-013**. The
> June phasing that deferred terrain multiplayer to Phase 3 is superseded. Old
> T-102…T-106 are mapped into the new phases below; the Done log is unchanged.

---

## Phase 1 — Terrain Feasibility (NOW)

**Milestone: one hill is trustworthy.**

Ordinary survival content does not start until the backend is ruled on. A successful
"I dug a smooth hole in PIE" demo proves almost nothing about whether the plugin can be
the permanent world substrate.

### T-100 — Visual Studio 2022 + C++ workload *(parallel, does not block T-101A)*

Community edition with the **"Game development with C++"** workload (`NativeGame`).
Director runs the winget command and approves the UAC prompt; manual installer is the
fallback. Needed from the first C++ increment (Phase 1B), not before.

### T-101A — Terrain smoke test *(one evening, solo, NOW)*

Free Legacy is already installed (v432, see STATE). Remaining: a configured voxel world
in `VoxelSandbox`, dig and add in PIE, a tunnel or overhang, no editor errors,
screenshots, and findings written to `Docs/T-101A_FINDINGS.md`.
Runbook: `Docs/T-101A_RUNBOOK.md`. **Solo is acceptable for 1A only.**

### Blind benchmark *(between 1A and 1B)*

`Docs/DUAL_AGENT_SETUP.md` section 6. Identical context and an identical terrain-authority
challenge to two vendors, blind, then cross-reviewed. Produces **ARCHITECTURE.md v1**,
**D-017** (terrain architecture) and **D-018** (primary implementer). Runs *after* T-101A
so the proposals are written against the plugin's real API, not against a guess.

### T-101B — Terrain Feasibility Gate *(tiered)*

**Gate-Critical — must pass:**

1. **Smooth volumetric terrain** — no cube aesthetic, real tunnel, overhang, varied slopes.
2. **Material queries** — soil / generic stone / ore-bearing material, queryable by
   gameplay.
3. **Authoritative edit path** — client requests, server validates, server applies,
   clients receive authoritative state.
4. **Concurrent edits** — 2–3 clients altering the same region, deterministic ordering,
   no permanent divergence.
5. **Save / restart** — heavily modified terrain saved, server closed and reloaded,
   meaningful state restored exactly.
6. **Join-in-progress** — a fresh client reconstructs modified chunks without replaying
   full server history and without visible divergence.
7. **Material yield** — removed material measured deterministically; the server awards
   resources from actual excavation (D-011).
8. **Edit stress** — hundreds to thousands of operations, measuring server and client
   frame time, terrain rebuild latency, save-file growth, network bandwidth, memory, and
   collision rebuild cost.

**Gate-Observe — documented, not solved:**

collision edge cases · foliage / PCG response to removed terrain · nav dirtying and
rebuild · unsupported and floating terrain behaviour when undermined · world streaming of
modified chunks.

**Exit:** **PASS** (backend approved) · **CONDITIONAL** (retained behind our own
authoritative wrapper, missing systems built in C++) · **FAIL** (test hybrid or
alternative backend before ordinary feature work) · **VISION CHANGE** (Director only —
deliberately removing arbitrary terrain manipulation from Pillar 1).

**Sub-steps:**

- **1B** — `TerrainEditOp`, authoritative request path, revision IDs, 2–3 client
  same-region test.
- **1C** — Material field; soil/stone/ore query; **resource yield from removed material**.
- **1D** — Persistence journal, snapshot + compaction prototype, restart test.
- **1E** — Join-in-progress, chunk relevancy, compression/batching only as needed.
- **1F** — Stress profile, collision, foliage, nav, streaming → **decide the backend**.

### T-006 — Editor fluency *(standing, parallel, ongoing)*

Viewport navigation, Content Browser, placing/transforming actors, Blueprint editor,
Epic's "first hour" learning path. Carried over from Phase 0 — not a blocker, not
"done," just an ongoing habit. Status unverified at CP-002.

### T-107 — First-person camera rig *(D-009; cheap, slots in any time after 1A)*

Head-socket camera, FOV tuning, near-clip + owner-only head hiding, third-person toggle.

---

## Phase 2 — First complete physical loop

Only after the terrain decision.

- Interact / tool framework
- Tree harvesting *(was T-102)*
- Terrain mining *(was T-103)*
- Authoritative inventory: C++ base + Blueprint UI child *(was T-104)*
- One craftable tool
- One campfire or workbench
- Save / restart all of the above

**Milestone:** a player joins, gathers wood and stone, digs an ore vein, crafts something,
logs out; the server restarts; they return to the same altered hill, inventory, and
structure.

## Phase 3 — Building

- Socket set: foundation, wall, ceiling, doorframe, door *(was T-201)*
- Placement validation, server authority, persistence
- Workbench + recipe/crafting system *(was T-202)*
- **Terrain-under-structure policy** — first version: block edits beneath critical
  structure bounds, **or** allow the building to float until a support system exists.
  **Decide it; do not simulate it by implication.**

## Phase 4 — Multiplayer vertical slice

All systems are already multiplayer by this point; this phase proves it outside the
building.

- Engine-from-source build; Linux dedicated server target compiles *(was T-302)*
- Server on a LAN box; port forwarding; friends direct-connect from outside *(was T-303)*
- Remote friends playtest, 2–4 players; reconnect; save/restart *(was T-305)*
- Bandwidth and performance telemetry

**Milestone:** harvest → mine → craft → build → persist, with friends.

## Phase 5 — Environment / survival

- Day/night cycle *(was T-105)*
- One meaningful hazard
- One mitigation / recovery mechanic
- Basic passive wildlife (no creature framework)
- Survival meter set decided **by testing** (D-016)

## Phase 6 — First electricity (Stage 2)

- Fuel generator *(was T-401)*
- Cable placement; **server-side power graph simulation, no per-machine tick**
  *(was T-402)*
- Powered light, powered furnace *(was T-403)*
- Machine and grid state persistence, delta replication *(was T-404)*

## Phase 7 — First automation (Stage 3)

One material path end to end: **ore → powered miner or quarry → transport → powered
processor → storage.** This is where the project's identity begins to show.

---

## Later / parked (do not start)

Biome expansion + World Partition · weather · larger continent · industrial excavation
(Stage 4) · roads · deeper logistics · creatures, taming, breeding (Year 2) · PvP zones
*(needs the integrity model)* · structural integrity & decay · 16–32 player load tuning ·
**MCP editor control — evaluate after T-101B** · **GLM worker tier — parked until at
least 10 bounded tasks exist** (D-014).

*Superseded:* old **T-106** (single-player voxel delta save/load) is absorbed into
T-101B sub-step 1D, which requires the multiplayer-capable version instead.

---

## Done

- **T-001** *(CP-001)* Install Epic Games Launcher + UE 5.7.x to NVMe.
- **T-002** *(CP-001)* Third Person template project created (Blueprint, Desktop, Max
  quality, Starter Content **OFF** — amended from ON to stay inside GitHub's free LFS
  quota). Shader compile survived.
- **T-003** *(CP-001)* Multiplayer PIE test: 3 players, Listen Server — movement
  replicated across all three windows.
- **T-004** *(CP-001)* Git + LFS init, doc pack committed, pushed to
  https://github.com/Harjas2102/VoxelWorld.
- **T-005** *(CP-001)* Claude Code installed, opened in repo, verified it reads CLAUDE.md
  and summarizes the docs correctly.

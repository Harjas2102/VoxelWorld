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

### T-101A — Terrain smoke test — ✅ **DONE (CP-004)**

Passed with caveats on 2026-09-06. See the Done log and `Docs/T-101A_FINDINGS.md`.

### Blind benchmark *(between 1A and 1B — NOW)*

`Docs/DUAL_AGENT_SETUP.md` section 6. Identical context and an identical terrain-authority
challenge to two vendors, blind, then cross-reviewed. Produces **ARCHITECTURE.md v1**,
**D-017** (terrain architecture) and **D-018** (primary implementer). Runs *after* T-101A
so the proposals are written against the plugin's real API, not against a guess.

### T-112 — TerrainCore build step 1 — in progress (CP-008)

Build order follows `ARCHITECTURE.md` §9. T-112.1 and T-112.2 are in the Done log.
**Next: T-112.3** — `FTerrainRevisionIndex` and `UTerrainService` skeleton;
`TerrainCore.Revision.Monotonic` green headless. AR-4 limits this to in-memory
monotonicity and one bump per affected chunk; compaction coverage stays at step 4.
Completion means all five TerrainCore tests pass; four pass at CP-008.

Then **T-112.5** — UE 5.7 → 5.8 and Unreal MCP, per D-025 — followed by **T-113**,
the production backend adapter, streaming component and Blueprint service rewire.
**T-110 onboarding was brought forward and completed with T-112.2** (D-027).

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

### T-006 — Editor fluency *(rescoped at CP-002; not started)*

Confirmed not started. Rather than carry an open-ended "learn Unreal" task that has sat
untouched since June, it is cut down to exactly the skills T-101A actually needs, done
**inside** T-101A rather than before it:

1. **Viewport navigation** — right-mouse-drag to look, WASD while held, mouse wheel to
   zoom, **F** to focus the selected actor. (Needed the moment the terrain appears.)
2. **World Outliner** — find and select an actor by name. (Needed to confirm the script
   worked.)
3. **Output Log** — open it, switch the input dropdown from Cmd to Python. (Needed to
   run any script the agent writes; this is the highest-leverage skill on the list.)
4. **Blueprint Event Graph** — open a Blueprint, right-click to add a node, drag a wire,
   Compile, Save. (Needed for runbook step 6.)

Everything else — materials, PCG graphs, Sequencer, the wider "first hour" learning path
— is **parked**. The governance now routes editor work through config, C++, and Python
(AGENTS.md section 11), so broad editor fluency is no longer on the project's critical
path. Learn it when a task demands it.

### T-108 — C++ voxel generator *(NEW at CP-003; blocks 1C)*

Forced by the T-101A discovery that **Voxel Graphs are Pro-gated** (R-008): the only
runnable generators on Free are `VoxelFlatGenerator` and `VoxelEmptyGenerator`, so the
authored test world cannot come from a graph asset.

A `UVoxelGenerator` subclass in C++ producing the GDD's first test world: a 256–512 m
hill with **several material strata** and **an underground ore body**, plus a cliff and a
lowland. This is the prerequisite for sub-step **1C** (material field, soil/stone/ore
query, yield from removed material) — there is nothing to query without it.

Requires the project to gain a C++ module — **done at CP-006** (T-111, build step 0): the
project compiles from source and `TerrainCore` exists. T-108 is build step 8 and the
generator forwards through `ITerrainDensityField` (`ARCHITECTURE.md` §4.6). **R2/R3 boundary — propose before implementing.**

Not a detour: D-011 always put generation semantics in game-owned code. The Pro gate only
removed the option of postponing it.

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
**GLM worker tier — parked until at
least 10 bounded tasks exist** (D-014).

*Superseded:* old **T-106** (single-player voxel delta save/load) is absorbed into
T-101B sub-step 1D, which requires the multiplayer-capable version instead.

---

## Done

- **T-110** *(CP-008)* Astra onboarded as Implementer against the existing repo;
  read the constitution/docs and T-112.1, extended the code, built with UE 5.7 and ran
  four headless tests successfully. Brought forward by the Director (D-027).
- **T-112.2** *(CP-008)* Eleven-method `ITerrainBackend`, single-Sample density-field
  declaration, dense memory backend and reusable factory conformance suite, including
  `Query.Point`. Six source files. Build succeeded; four tests passed, zero failures,
  exit 0. Position-sensitive density/material rearrangements tested; Build.cs unchanged.
- **T-112.1** *(CP-007; carried into this log at CP-008)* Value types, 58-byte op codec,
  quantiser; `Op.Codec.RoundTrip` and `Op.Quantisation.Stable` green headless.
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
- **T-100** *(CP-003)* Visual Studio Community 2022 17.14.37614.0 + "Game development
  with C++" workload, via winget. MSVC 14.44.35207, Windows SDK 10.0.26100.0.
  Verified with `vswhere -requires Microsoft.VisualStudio.Workload.NativeGame` and by
  finding `cl.exe` on disk — not on the installer's exit code.
- **T-111** *(CP-006)* **Build step 0 — the project compiles from source for the first
  time.** `VoxelWorld` (primary game module, depends on `TerrainCore` only) and
  `TerrainCore` (Core/CoreUObject/Engine, no plugin) with one empty
  `UTerrainService : UWorldSubsystem` (K7), plus the `Target.cs` pair and the `.uproject`
  `Modules` array. `VoxelWorldEditor Win64 Development` → `Result: Succeeded`.
  **The D-011 boundary was tested, not asserted:** a `#include` of a plugin header added to
  `TerrainCore/Private/` fails with `fatal error C1083 ... No such file or directory`, and
  the build succeeds again once removed. Editor headless boot exit 0; standalone runs clean
  with the invoker enabled; `RemoveSphere`/`AddSphere` still return populated modified-voxel
  arrays. No gameplay change — both drift-check flags remain until build step 2.
- **T-101A** *(CP-004)* Terrain smoke test — **PASS, with caveats.** A mound built with
  `AddSphere`, tunnelled through with `RemoveSphere` until it broke out the far side:
  rock spanning open air with sky visible through it, which no heightfield can represent
  (`Docs/images/T-101A_tunnel.png`). Smooth, not blocky. Clean log. Run standalone, not
  PIE (**D-021**), in `Lvl_ThirdPerson`, not `VoxelSandbox` (**D-020**).
  Four costs discovered and carried into T-101B rather than treated as surprises:
  Voxel Graphs are Pro-gated (2b, → **T-108**); the mesh cannot be a replicated movement
  base and an invoker is mandatory (2d, **R-010**); **voxel edits do not persist** — the
  world regenerates from the generator on every load (2e, **R-003**); and editing near
  your own feet drops the player through the floor (2f).
  **The representation is proven; everything that makes it a persistent multiplayer
  world is not.** Passing 1A does not adopt the backend (**D-013**).
  Tooling produced: `Tools/Editor/place_voxel_world.py`,
  `Tools/Editor/fix_player_spawn.py`, `Tools/Editor/clean_duplicate_skysphere.py`,
  `Tools/Play-Solo.ps1`.

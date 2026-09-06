# STATE.md — Current Project State

> **Read this file + VISION.md at the start of every session** (Claude Code reads them
> from the repo; see `AGENTS.md` section 1). Updated at every checkpoint.

---

**Checkpoint:** CP-004 · **Date:** 2026-09-06
**Phase:** 1 — Terrain Feasibility

## What happened at CP-004

**T-101A is done. The first hole is dug, and the tunnel goes all the way through.**

A mound built entirely from `AddSphere`, then tunnelled with `RemoveSphere` until it
broke out the far side — **rock spanning open air with sky visible through the opening**
(`Docs/images/T-101A_tunnel.png`). No heightfield can represent that geometry. The
terrain reads smooth and organic, never blocky (Pillar 2, D-015). Log clean.

**Verdict: PASS, with caveats.** Per **D-013** this does *not* adopt the backend; it says
the backend is worth testing properly at T-101B.

> **The honest one-line summary: the representation is proven; everything that makes it a
> persistent multiplayer world is not.**

Four costs were discovered and are now carried into T-101B as known entry costs rather
than surprises. All four are written up in `Docs/T-101A_FINDINGS.md`:

| § | Finding | Consequence |
|---|---|---|
| 2d | `VoxelProceduralMeshComponent` is `NOT Supported` by `FNetGUIDCache`, so a character standing on terrain has an unresolvable movement base; and the plugin refuses camera-as-invoker outside standalone | A `VoxelInvokerComponent` on the character is a **hard requirement** for any multiplayer terrain test. → **R-010** |
| 2e | **Voxel edits do not persist.** `SaveObject` defaults to null, so the world regenerates from the generator on every load | Pillar 1's core promise is an unmeasured opt-in step. `SaveData()` is editor-only; the runtime path is `UVoxelDataTools`. → **R-003** |
| 2f | Editing near your own feet drops the player through the floor into an endless fall | Edit/collision atomicity is a gameplay-facing bug, not cosmetic. → **R-010** |
| 2b | *(from CP-003)* Voxel Graphs are Pro-gated | Procedural generation must be C++. → **T-108** |

Also this session:

- **D-020** — `Lvl_ThirdPerson` is the T-101A map of record; `VoxelSandbox` reverted and
  abandoned for this task. The template map already had the PlayerStart, GameMode and
  character the dig test needed.
- **D-021** — solo terrain work runs **standalone** (`Tools\Play-Solo.ps1`), not PIE.
  PIE stays on the three-player settings because T-101B needs them.
- **R-009 first test passed.** The 7-day deadline was 2026-09-12; the hole and the tunnel
  landed on the **6th**, six days early. The rule stays in force.
- **A One File Per Actor trap cost three failed test launches**, and is worth never
  repeating: `Lvl_ThirdPerson` stores each actor in its own package, so saving the level
  does **not** save its actors. Scripts must save `actor.get_package()` — *not*
  `actor.get_outer().get_outermost()`, which silently saves the map instead. Symptom:
  PIE looks correct (it duplicates the in-memory world) while standalone, which loads
  from disk, does not.

## What happened at CP-003

T-100 done (VS 2022 Community 17.14.37614.0, MSVC 14.44.35207, Win SDK 10.0.26100.0).
D-019 ruled the repo stays public. `CHAT_OPENER.md` made the single canonical opener and
T-006 rescoped. **R-008 found: Voxel Graphs are Pro-gated**, making T-108 a Phase 1
requirement. The D-011 yield hook was proven live — 861,781 voxels across five spheres.

## What happened at CP-002

External architecture review (GPT-5.6) plus a response review (Claude Fable 5.1),
accepted in full as **D-010 … D-016**. Governance became vendor-neutral (`AGENTS.md`);
the roadmap was reordered around a terrain feasibility gate. Reviews archived in
`Docs/reviews/`.

## What exists right now

**In-engine:**

- UE 5.7 Third Person template project **VoxelWorld** (Blueprint, Desktop, Max quality,
  Starter Content OFF), shaders compiled, runs clean.
- **`Content/ThirdPerson/Lvl_ThirdPerson` — the T-101A map of record (D-020).** Contains:
  - `VoxelWorld_T101A` — 50 cm voxels, 1024 voxels (512 m), `VoxelFlatGenerator`,
    collisions on, `WorldGridMaterial` (the engine checker grid — projected from world
    position, so it reads correctly on UV-less procedural meshes and makes holes and
    overhangs legible; plain `BasicShapeMaterial` rendered white-on-white).
  - PlayerStart moved to **(-8228.66, 0, 150)**, ~82 m west of the hill centre, facing it.
  - `BP_ThirdPersonCharacter` wired for digging: LMB → line trace → `RemoveSphere`,
    RMB → `AddSphere`, radius 200, 1000 uu reach, both behind a hit `Branch`.
- **Voxel Plugin Free Legacy** at `Plugins/VoxelFree/` — **v432 / `e9648b302` / 5.7.0**,
  prebuilt Win64 binaries. **Not committed** (gitignored); reinstall via
  `Tools/Install-VoxelFreeLegacy.ps1`.
- `Content/Maps/VoxelSandbox.umap` — at its CP-002 committed state, abandoned (D-020).
  Would need a PlayerStart and GameMode before it is usable.
- Known non-fatal issue: an `ensure` on `AVoxelWorld::DestroyWorldInternal`
  (GeneratorCache) at editor shutdown. Cosmetic; logged, not chased.
- 3-player PIE replication verified at CP-001 (movement only, no terrain).

**Tooling (all scripted; no menu navigation required):**

| Tool | Does |
|---|---|
| `Tools/Editor/place_voxel_world.py` | Places and configures the voxel world, sculpts the test hill, saves the actor package |
| `Tools/Editor/fix_player_spawn.py` | Measures the hill's real reach and moves every PlayerStart clear of it, saving the actor's own package |
| `Tools/Editor/clean_duplicate_skysphere.py` | Removes an accidental duplicate sky sphere |
| `Tools/Play-Solo.ps1` | Launches standalone single-player (D-021); separate process, own log |
| `Tools/Install-VoxelFreeLegacy.ps1` | Idempotent plugin reinstall |

**Repository:**

- Git + LFS, pushed to **https://github.com/Harjas2102/VoxelWorld** — **PUBLIC** (D-019).
- Governance: `AGENTS.md` (constitution) + `CLAUDE.md` (adapter) + `Docs/` (truth).

No gameplay systems yet. No authoritative terrain layer yet. No C++ module yet.

## Current task

**T-111 — build step 0.** Create the `VoxelWorld` and `TerrainCore` C++ modules with
one empty `UWorldSubsystem`. **The project builds from source for the first time.**
Implemented by Claude Code (D-018 amendment). No gameplay change.

Queued:

- **T-112** — build step 1: `FTerrainOp`, chunk keys, `ITerrainBackend`,
  `FMemoryTerrainBackend`, `UTerrainService` skeleton. Headless tests pass.
- **T-113** — build step 2: `FVPLegacyBackend` and `UTerrainStreamingComponent`;
  **rewire the T-101A dig Blueprint through the service and delete the direct plugin
  calls.** Both flagged drift checks clear here, standalone only.
- **T-110** — onboard Astra (Codex CLI) against a repo that builds. After T-113.
- **T-101B** — Terrain Feasibility Gate. Build steps 3–7.
- **T-108** — C++ density field, strata and ore. Build step 8.

**Drift checks:** both flags remain until T-113, and then only for standalone.
Server authority is not proven until build step 3.

**Process:** R-012 in force. The next three tasks all end in something that runs.

## Drift checks (VISION.md, run at CP-004)

- [x] Terrain is smooth-voxel and player-deformable — **proven today**
- [ ] **Every gameplay system is server-authoritative — FLAGGED**
- [x] The tech path still leads to electricity and machines
- [x] Scope is still one planet, 16–32 players
- [x] Development is still incremental, Minecraft-alpha style
- [x] The five inspiration games are still the reference set
- [ ] **The terrain backend remains replaceable (D-010, D-011) — FLAGGED**
- [x] Voxels are still invisible to the player (D-015) — grid material is placeholder

**Both flags have the same cause: the T-101A dig wiring.**
`BP_ThirdPersonCharacter` calls `UVoxelSphereTools::RemoveSphere` / `AddSphere`
**directly, on the client, from gameplay code.** That is simultaneously:

- a violation of **D-011** and `AGENTS.md` section 4 — *gameplay never calls the terrain
  plugin directly; all terrain access goes through the game-owned terrain service* — and
  it is named explicitly in the section 9 drift guard as "direct plugin calls from
  gameplay code"; and
- client-authoritative, which `AGENTS.md` section 4 forbids outright.

**This is accepted for T-101A only, and must not survive it.** A smoke test whose entire
purpose is to find out whether the plugin can dig at all has nothing to route through
yet — the adapter's shape is what the blind benchmark and D-017 exist to decide. Writing
one first would have been inventing the architecture the gate is supposed to produce.

The obligation this creates: **the first thing built after D-017 is the terrain adapter,
and this Blueprint is rewired through it or deleted.** It is test scaffolding with a
gameplay-shaped silhouette, which is exactly the kind of thing that quietly becomes
permanent. Feature work does not start on top of it.

## Blockers

None.

## Open decisions

- **D-008** — working title
- Survival meter set — resolved by testing, not convention (D-016)
- Stage 3 transport method (conveyors / pipes / vehicles / drones)
- Structural integrity & decay model
- Terrain-under-structure policy

## Role assignments today (D-014 — operational, not constitutional)

| Role | Holder |
|---|---|
| Director | Harjas |
| Implementer | Claude Code on Opus (Claude Pro) |
| Architect | Opus in the Claude app; Astra when verified available |
| Independent reviewer | Whichever vendor did not author (R3 only) |

## Toolchain status

| Tool | Status |
|---|---|
| UE 5.7 | ✅ `C:\Program Files\Epic Games\UE_5.7` — installed, shaders compiled |
| Git + LFS | ✅ git-lfs 3.7.1, push credentials verified |
| Claude Code | ✅ Installed, verified in-repo |
| Codex CLI (Astra, D-018) | ⏳ Not installed — deferred to T-110, after the repo builds |
| **Voxel Plugin Free Legacy** | ✅ **v432 / e9648b302 / 5.7 — mounts clean** (gitignored) |
| Voxel Plugin 2 | ❌ Paid, gated on owning Pro Legacy — upgrade candidate only (R-008) |
| Python Editor Script Plugin | ✅ Enabled at CP-002 — the primary way work gets done here |
| Editor Scripting Utilities | ✅ Enabled at CP-002 |
| Visual Studio 2022 (C++ workload) | ✅ 17.14.37614.0, MSVC 14.44.35207, Win SDK 10.0.26100 — **not yet exercised; no C++ module has been built** |
| Editor revision control | ⚠️ Enabled and failing checkout on every scripted save, popping a modal each time. Saves succeed anyway. Set Provider to None when it gets in the way. |

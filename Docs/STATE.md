# STATE.md — Current Project State

> **Read this file + VISION.md at the start of every session** (Claude Code reads them
> from the repo; see `AGENTS.md` section 1). Updated at every checkpoint.

---

**Checkpoint:** CP-008 · **Date:** 2026-09-06
**Phase:** 1 — Terrain Feasibility

## What happened at CP-008

**T-110 onboarding and T-112.2 complete. Four TerrainCore tests are green headless.**

- Astra worked as Implementer, read the constitution, project docs and T-112.1 code,
  and built and tested against the installed UE 5.7 toolchain. The Director brought
  onboarding forward from after T-113 and authorised the increment's technical
  determinations when the density-field interface conflict was reported (**D-027**).
- Six source files added: `ITerrainBackend.h` (eleven methods and AR-2 init),
  `ITerrainDensityField.h` (declaration only), `MemoryTerrainBackend.h/.cpp`, and
  `Private/Tests/BackendConformance.h/.cpp`. No field implementer exists yet.
- The reusable suite accepts `TFunction<TUniquePtr<ITerrainBackend>()>` and includes
  point-query coverage. T-113 supplies its adapter factory without editing the suite.
  Tests inspect all samples in eight chunks to check actual changes against reported
  bounds, touched count and exact, deduplicated affected keys. Separate density-only
  and material-only rearrangements prove the hash is position-sensitive.
- Queries exercise distinct signs/materials at interiors and all eight corners of
  positive and negative chunks, immediately after writes and edits. Lifecycle,
  streaming overlap/movement/clear, transfer, rejected-edit atomicity and flushes are
  covered. Client terrain is checked; full edit-result reporting is asserted on the
  server, as §4.3 specifies.
- **Verification:** `VoxelWorldEditor Win64 Development` → `Result: Succeeded`.
  `Automation RunTests TerrainCore; Quit` → four `Result={Success}` entries:
  `Backend.Conformance`, `Op.Codec.RoundTrip`, `Op.Quantisation.Stable`, `Query.Point`;
  zero failures; `**** TEST COMPLETE. EXIT CODE: 0 ****`; process exit 0.
  Final run: 2026-09-06 16:53 UTC, `Saved/Logs/VoxelWorld.log` (local, gitignored).
- `TerrainCore.Build.cs` unchanged (`git diff --quiet`, exit 0): only Core,
  CoreUObject, Engine. No plugin includes or new game-owned type/file names containing
  "Voxel". Tests are guarded by `WITH_DEV_AUTOMATION_TESTS`; flag spelling verified
  from UE 5.7's `Core/Private/Tests/HAL/PlatformTest.cpp`.

**Determinations and limits.** Recorded initially in the new headers, now reconciled
in `ARCHITECTURE.md`'s CP-008 header block and §4.6. `Sample(FIntVector)` returns
density/material together; generator version comes from init. With a null field,
residency requires explicit data plus interest: interest never fabricates air.
Clearing interest retains data for re-entry. Dense transfer only at this step;
SparseDiff/Empty restoration remains later work. Reference geometry, occupancy,
rounding and work limits are documented; they do not settle production DEF-5/DEF-6.
`Flatten` and `Smooth` return false. No gameplay or asset change; no PIE result claimed.

## What happened at CP-007

**D-025 ruled, and the first third of build step 1 is green.**

- **D-025** — stay on **UE 5.7** through T-112; upgrade to **UE 5.8** and adopt Epic's
  first-party **Unreal MCP** plugin as **T-112.5**, between T-112 and T-113. Unreal MCP
  shipped with 5.8 and does not exist in 5.7, so this is an engine upgrade, not a plugin
  toggle. 5.8 is Epic's last planned major UE5 release. The expected blocker did not
  survive contact: **VoxelPluginFreeLegacy publishes prebuilt binaries for both 5.7 and
  5.8**, so `Tools/Install-VoxelFreeLegacy.ps1` only repoints to a different release
  asset. Sequenced after T-112 because build step 1 is engine-agnostic
  (Core/CoreUObject/Engine, headless, no plugin, no world) and before T-113 because
  `FVPLegacyBackend` binds to plugin headers and should be written once, against the
  plugin build we keep.
- **Four Architect rulings (AR-1 … AR-4)** closed the v1 gaps build step 1 walks into:
  `FTerrainBox`, `FTerrainBackendInit`/`ETerrainRole`, `Op.Quantisation.Stable`
  semantics, and `Revision.Monotonic`'s step-1 scope. Recorded in `ARCHITECTURE.md`'s
  header ruling block and §6.1; technical, so no `DECISIONS.md` entry (D-023).
- **T-112.1 complete.** `TerrainTypes.h`, `TerrainOp.h/.cpp`, `TerrainQuantise.h/.cpp`
  and two headless tests. Six new files; nothing else modified.
  `Result: Succeeded` · `**** TEST COMPLETE. EXIT CODE: 0 ****` · 2 tests, 0 failures.

### The 58-byte encoding is measured, not asserted by comment

`ARCHITECTURE.md` §4.2 fixes the encoded `FTerrainOp` body at 58 bytes. That number is
now an artifact rather than a claim, at three levels: a `checkf` inside
`SerializeTerrainOp`, a `TestEqual` in `Op.Codec.RoundTrip`, and a logged measurement —
`Encoded FTerrainOp body measured 58 bytes`. Layout: OpSeq 8 · TransactionId 8 ·
Kind/Shape/Source 1+1+1 · SourceId 4 · ToolId 4 · CentreVox 12 · RadiusVoxQ16 4 ·
ExtentVox 12 · MaterialId 2 · Flags 1.

The codec test asserts all eight octants, `MAX_int32`/`MIN_int32` radius, every
Kind × Shape × Source combination, all 58 truncation lengths — each into a fresh
exactly-sized allocation, so an OOB read hits real heap rather than the tail of a live
buffer — and out-of-range enum bytes at all three offsets, with the highest defined value
at each offset also asserted to still decode, making it a range check rather than an
off-by-one.

### Two floating-point limits found by the quantiser test, and written down rather than worked around

Both are properties of IEEE754, not defects, and both live in the test file:

| Limit | Consequence |
|---|---|
| A world-space nudge smaller than one ULP of the **terrain-local** coordinate is unrepresentable after the subtraction and collapses onto the boundary exactly. First run failed here: `Boundary voxel 64 axis 1 at 1 ULP: on=64 up=64 down=64` (world 1152.0, local 3200.0) | The test nudges by ULPs of the coarser of the two magnitudes so the perturbation provably survives, and separately asserts the strict single-raw-ULP form under an exactly-invertible origin, where the question is well posed |
| One ULP below 0.0 is the smallest denormal; divided by the voxel size it underflows to −0.0, whose floor is 0, not −1 | Voxel 0 under an identity origin is excluded from the raw-ULP form. No divide-then-floor rule can do otherwise. Voxel 0 stays covered by the scaled-nudge loop, whose reference magnitude is floored at the voxel size so its nudge stays normal |

The test also pins Floor against Round explicitly: 45 cm → voxel 0; −1 cm → voxel −1.

### Boundary constraints re-verified

- `TerrainCore.Build.cs` — `git diff --quiet` confirms **unchanged**: `Core`,
  `CoreUObject`, `Engine`.
- No plugin header anywhere in `TerrainCore`; the module's complete include set is nine
  engine headers plus our own.
- No "Voxel" in any game-owned file or type name (D-015).
- Tests in `Private/Tests/`, `WITH_DEV_AUTOMATION_TESTS`-guarded, flags spelled from
  UE 5.7's own `PlatformTest.cpp` rather than from memory.

## What happened at CP-005 / CP-006

**The project compiles from source for the first time.** Build step 0 is done.

Governance and architecture settled first, then the first line of C++ was written against
them:

- **D-022** — `ARCHITECTURE.md` **v1 adopted**, on the basis of `P-001-terrain-claude.md`
  per D-017, amended by blockers B1–B10 (carried as §14) and carrying the evidence table
  and experiment discipline of `P-001-terrain-astra.md`. v0 is archived at
  `Docs/archive/ARCHITECTURE_v0.md`. **v1 is the implementation spec.**
- **D-023** — **decision classes split GAME / TECHNICAL.** Technical rulings move to the
  Architect; GAME decisions stay with the Director. This is what lets architecture move at
  the speed of the work without spending Director attention on it.
- **D-024** — **forks K1–K10 ruled by the Architect** under D-023. `§15 Open forks: None.`
  Step 0's two bindings are among them: **K7** (the service is a `UWorldSubsystem` — "the
  service is authority, not a thing in the world") and **K8** (`TerrainCore`,
  `TerrainBackendVPLegacy` — no "Voxel" in game-owned names, per D-015).
- **R-011** and **R-012** in force. R-011: an increment is never returned unstarted; block
  with a named ambiguous line of `ARCHITECTURE.md` or proceed. R-012: process weight is
  checked at every checkpoint.
- **Four v0 items carried into v1** by Architect ruling (technical, per D-023): the
  boundary rationale → §4.1.0; surface queries → §4.3 as `QueryPoint` /
  `FTerrainPointSample`, taking `ITerrainBackend` from ten methods to eleven; tool
  ownership, cooldown and fuel → §4.4, at admission and revalidated at commit under DEF-7;
  power grids → §4.7, per D-012 and VISION pillar 3. The archive header now records them as
  carried, with each item's original wording preserved as the record of what was missing at
  adoption.
- **T-111 — build step 0 complete.** `VoxelWorld` and `TerrainCore` C++ modules, one empty
  `UTerrainService : UWorldSubsystem`, the `Target.cs` pair, and the `.uproject` `Modules`
  array. `Result: Succeeded`. No gameplay change.

### The boundary claim is tested, not asserted

`ARCHITECTURE.md` §4.1 claims the module boundary is a **build-system** boundary — that a
plugin include in gameplay code is "a compile error rather than a code-review finding".
That claim was tested rather than trusted:

| Probe | Result |
|---|---|
| `#include "VoxelTools/VoxelDataTools.h"` added to `Source/TerrainCore/Private/` | `fatal error C1083: Cannot open include file: 'VoxelTools/VoxelDataTools.h': No such file or directory` — `Result: Failed` |
| Probe removed, rebuilt | `Result: Succeeded` |

`TerrainCore` cannot see the plugin's headers because its `Build.cs` does not list the
plugin. D-011 and the `AGENTS.md` §9 drift guard are now enforced by the compiler. This is
evidence, not narrative: the probe is re-runnable and the error code is the artifact.

Other verification at CP-006:

- Headless editor boot, **exit 0**: `/Script/TerrainCore.TerrainService` resolves as a
  `UClass`; `Lvl_ThirdPerson` loads with `VoxelWorld_T101A` and PlayerStart intact at
  (-8228.66, 0, 150).
- Standalone (`Tools/Play-Solo.ps1`): `World NetMode = Standalone`, `Voxel Invoker enabled;
  Name: VoxelInvokerAutoCameraComponent_0`, world generated in 0.193s, **no errors** — the
  T-101A success signature from the runbook.
- T-101A dig path: `RemoveSphere` and `AddSphere` at radius 200 — the exact calls
  `BP_ThirdPersonCharacter` makes — both return populated modified-voxel arrays over
  `VoxelIntBox (-7,-7,-7)..(8,8,8)`. Transient session, not saved.
- **Both input bindings confirmed by hand on the from-source build** — RMB built a mound,
  LMB dug it. **This is the T-101A dig path reproduced after the C++ conversion**, through
  the same `BP_ThirdPersonCharacter` wiring, and it closes the last open item from build
  step 0.

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

**C++ (builds from source; memory backend and conformance suite added at CP-008):**

```text
Source/
  VoxelWorld.Target.cs             Game target      | BuildSettingsVersion.V6
  VoxelWorldEditor.Target.cs       Editor target    | EngineIncludeOrderVersion.Unreal5_7
  VoxelWorld/                      primary game module — depends on TerrainCore ONLY
  TerrainCore/                     Core, CoreUObject, Engine. NO plugin dependency.
    Public/TerrainService.h        UTerrainService : UWorldSubsystem — still empty (K7)
    Public/TerrainTypes.h          §4.2 value types + §4.3 FTerrainPointSample
    Public/TerrainOp.h             FTerrainOp + the 58-byte codec's declarations
    Public/TerrainQuantise.h       QuantiseEdit / DequantiseVoxel / QuantiseRadiusQ16
    Public/ITerrainBackend.h       eleven-method interface + FTerrainBackendInit
    Public/ITerrainDensityField.h  Sample declaration only — implementers at T-108
    Public/MemoryTerrainBackend.h dense reference backend + documented determinations
    Private/TerrainOp.cpp          the codec — PERMANENT FORMAT
    Private/TerrainQuantise.cpp    world→voxel, Floor, fixed by §4.3
    Private/MemoryTerrainBackend.cpp synchronous data, edits, queries, interest, transfer
    Private/Tests/                 TerrainOpCodecTest.cpp, TerrainQuantiseTest.cpp
                                   BackendConformance.h/.cpp (factory suite + Query.Point)
```

- **`VoxelWorld`** — the primary game module (`IMPLEMENT_PRIMARY_GAME_MODULE`). Gameplay,
  characters, tools, UI hooks. `ARCHITECTURE.md` §4.1: it depends on `TerrainCore` only, and
  never on a backend module.
- **`TerrainCore`** — game-owned, compiles headless, holds no plugin type. **Its `Build.cs`
  is the boundary** (§4.1, §4.1.0). Adding a plugin to that dependency list breaks D-011 and
  the `AGENTS.md` §9 drift guard and requires a numbered decision, not an edit.
- **`UTerrainService : UWorldSubsystem`** — **still empty at CP-008.** A subsystem, not an
  actor: K7, ruled by D-024. Validation, sequencing, revisions, journalling and yield arrive
  at build steps 1 and 3.
- **The §4.2 value types, the `FTerrainOp` codec and the quantiser** — new at CP-007, and the
  first thing in this repo with a **permanent format** in it. The encoded op body is 58 bytes
  and is simultaneously the wire format and the journal record body, so changing it changes
  what old saves mean: it is a persistence format under `AGENTS.md` §4 and moves by numbered
  decision, not by edit. The memory backend and tests now consume these types;
  gameplay does not call them yet.
- **`VoxelWorld.uproject`** now carries a `Modules` array (`TerrainCore` first, then
  `VoxelWorld`). Both DLLs build into `Binaries/Win64/` (gitignored).
- `TerrainBackendVPLegacy` — the only module that may ever include plugin headers — **does
  not exist yet**. It arrives at build step 2 (T-113).

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
  - ⚠️ **The T-101A hill is not persistent** (finding 2e / **R-003**).
    `Tools/Editor/place_voxel_world.py` sculpts it into the **running editor session
    only**; any process that loads the level from disk regenerates a **flat plane** from
    `VoxelFlatGenerator`. **Do not re-run the script expecting a hill in standalone** —
    build the mound in-game with **RMB**, which is what the CP-004 tunnel test actually
    did. Terrain first survives a restart at **build step 4**.
  - ⚠️ **The PlayerStart offset assumes that hill.** (-8228.66, 0, 150) was placed relative
    to terrain that does not exist at runtime, so standalone spawns on bare ground facing
    nothing. **Second symptom of the same cause, not a separate issue.** Revisit at
    **build step 4**.
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

No gameplay systems yet. No authoritative terrain layer yet — `UTerrainService` exists as
an empty subsystem and does nothing. No backend adapter yet.

## Current task

**T-112 — build step 1** (`ARCHITECTURE.md` §9), **in progress**. Split into three
sub-increments under R-009.

| # | Scope | Status |
|---|---|---|
| **T-112.1** | `FTerrainOp` + 58-byte codec, chunk keys, quantiser. Tests `Op.Codec.RoundTrip`, `Op.Quantisation.Stable` | ✅ **Done at CP-007** |
| **T-112.2** | `ITerrainBackend` (eleven methods), `ITerrainDensityField` declaration, `FMemoryTerrainBackend`, `Backend.Conformance` + `Query.Point` | ✅ **Done at CP-008** |
| **T-112.3** | `FTerrainRevisionIndex`, `UTerrainService` skeleton. Test `Revision.Monotonic` | ⏭ **Next** |

T-112 is complete when all five TerrainCore tests are green headless. Four now pass;
`Revision.Monotonic` remains. T-112.3 is in-memory only per AR-4: revisions never
decrease, and each affected chunk bumps exactly once. Payload deletion/compaction
coverage stays at build step 4. The reusable backend suite and position-sensitive hash
checks are implemented; `Flatten` and `Smooth` remain unsupported under DEF-5.

Queued:

- **T-112.5** — UE 5.7 → 5.8 upgrade + Unreal MCP adoption (D-025). Bump engine, bump
  `.uplugin` `EngineVersion`, repoint `Tools/Install-VoxelFreeLegacy.ps1` at the 5.8
  binaries, re-run the CP-006 verification set green (headless boot, `Play-Solo.ps1`,
  LMB/RMB dig, the `#include` boundary probe), then enable `ModelContextProtocol` +
  `AllToolsets` and generate `.mcp.json`. **Adds to the AGENTS §9 drift guard:** Unreal
  MCP is editor-only; `IModelContextProtocolModule::StartServer()` is never called from a
  game target.
- **T-113** — build step 2: `FVPLegacyBackend` and `UTerrainStreamingComponent`;
  **rewire the T-101A dig Blueprint through the service and delete the direct plugin
  calls.** Both flagged drift checks clear here, standalone only.
- **T-101B** — Terrain Feasibility Gate. Build steps 3–7.
- **T-108** — C++ density field, strata and ore. Build step 8.

**Drift checks:** both flags remain until T-113, and then only for standalone.
Server authority is not proven until build step 3.

**Process:** R-012 in force. The next three tasks all end in something that runs.

## Drift checks (VISION.md, run at CP-008)

**Both flags REMAIN.** CP-008 added a memory backend and headless contract tests.
Gameplay still does not route through the service, so
`BP_ThirdPersonCharacter` still calls `UVoxelSphereTools::RemoveSphere` and `AddSphere`
directly, on the client. **They clear at build step 2 (T-113)**, when the Blueprint is rewired
through `RequestEdit` and the direct calls are deleted — and then for standalone only. Server
authority is not proven until build step 3.

- [x] Terrain is smooth-voxel and player-deformable — proven at T-101A
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

**Boundary evidence at CP-008.** The dependency list remains byte-identical and no
plugin type/header crosses it. Backend replacement now has an executable factory suite,
including queries and position-sensitive hashes. The production adapter has not run
that suite yet; replacement is not proved by the memory backend passing alone.

## R-012 check (process weight, run at CP-008)

**PASS for this bounded step; the lack of a playable change remains visible.**

T-112.2 supplies executable contract tests and a headless backend, as specified. The
initial stop named the density-field conflict at §4.6 lines 536–538; the Director
authorised a determination and implementation completed in the same session. No
checkpoint text or project-doc edits were produced before the Director said
"checkpoint". **The next playable change is T-113**, one remaining sub-increment and
the scheduled engine upgrade away. No new process task was inserted before it.

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
| Implementer | Astra (Codex), onboarded at CP-008 alongside T-112.2 (D-027) |
| Architect | Opus in the Claude app; T-112.2 determinations explicitly delegated by the Director (D-027) |
| Independent reviewer | Whichever vendor did not author (R3 only) |

## Toolchain status

| Tool | Status |
|---|---|
| UE 5.7 | ✅ `C:\Program Files\Epic Games\UE_5.7` — installed, shaders compiled |
| Git + LFS | ✅ git-lfs 3.7.1, push credentials verified |
| Claude Code | ✅ Installed, verified in-repo |
| Codex (Astra, D-018 / D-027) | ✅ Onboarded in-repo at CP-008: docs, source edits, UE 5.7 build and four headless tests exercised |
| **Voxel Plugin Free Legacy** | ✅ **v432 / e9648b302 / 5.7 — mounts clean** (gitignored) |
| Voxel Plugin 2 | ❌ Paid, gated on owning Pro Legacy — upgrade candidate only (R-008) |
| Python Editor Script Plugin | ✅ Enabled at CP-002 — the primary way work gets done here |
| Editor Scripting Utilities | ✅ Enabled at CP-002 |
| Visual Studio 2022 (C++ workload) | ✅ 17.14.37614.0, MSVC 14.44.35207, Win SDK 10.0.26100 — **exercised and verified at CP-006.** `VoxelWorldEditor Win64 Development` → `Result: Succeeded` (63 actions, 85s cold). Toolchain reported by UBT: MSVC 14.44.35228 / Windows 10.0.26100.0 SDK |
| Editor revision control | ⚠️ Enabled and failing checkout on every scripted save, popping a modal each time. Saves succeed anyway. Set Provider to None when it gets in the way. |

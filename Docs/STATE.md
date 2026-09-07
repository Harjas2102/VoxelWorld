# STATE.md — Current Project State

> **Read this file + VISION.md at the start of every session** (Claude Code reads them
> from the repo; see `AGENTS.md` section 1). Updated at every checkpoint.

---

**Checkpoint:** CP-013 · **Date:** 2026-09-07
**Phase:** 1 — Terrain Feasibility

## What happened at CP-013

**The world is generated instead of flat, and the three defects blocking multiplayer
terrain are closed.** Two increments: T-108 (build step 8) and T-114 (the DEF-4/5/7
resolutions). Build step 3 is now the next task and it is no longer blocked.

### T-108 — there is a hill, and it survives a restart

- **The Director closed CP-012's last item first:** *"Play solo worked... I can place and
  dig."* Build step 2 is finished. He then said *"Still dont see a hill"*, which is what
  T-108 answers.
- **Why there was never a hill.** T-101A finding 2b: Voxel Graphs are Pro-gated and fail
  **silently**, so the only runnable generators on Free are `VoxelFlatGenerator` and
  `VoxelEmptyGenerator` (R-008). The T-101A hill was therefore *sculpted by a Python
  script into a running editor session*, and finding 2e / R-003 records that it did not
  survive a map load. Every standalone process regenerated a plane. **That is now closed
  at the root**, and closed earlier than the plan expected: it was scheduled for build
  step 4.
- **`FTerrainWorldField` (TerrainCore) is the world's shape.** A 280 m hill east of the
  origin with 65 m of relief; a **west-facing escarpment** cutting it to a shelf, which
  exposes ~18 m of rock at the player; topsoil / dirt / stone / deep stone / bedrock by
  depth below the local surface; an iron ore body under the hill that **never breaks the
  surface**; a lowland basin. No plugin, no `UObject`, no `UWorld` — it unit-tests
  headless, which is the whole argument §4.6 makes for game-owned generation.
- **It is a pure function of position and seed**, so the world comes back identically on
  every load with no save file. Roughness is integer-hashed value noise: no RNG and no
  float bit tricks, for the §4.10.4(b) reason.
- `UVPLegacyDensityGenerator` in the adapter forwards the plugin's value and material
  queries to the field and **decides nothing**. `GeneratorVersion` moved **0 → 1**.
- **Step 8 was taken out of order, legitimately.** §14's rule is about *defects*, not
  sequence. Step 8 was bound only to **R-008, a risk**, while steps 3–7 were blocked by
  three open defects — and step 8 was the one that moved the Phase 1 milestone, which
  BACKLOG states as *"one hill is trustworthy."* §9 now says this explicitly.
- **AR-6** (new): `ITerrainDensityField` gains `SampleRange`, defaulting to the full
  `[-1, 1]`. `Sample` alone can fill a chunk but cannot let the octree **skip** one.
  **AR-5 confirmed** as written at T-113.
- The most direct evidence the world changed is in `Terrain.SelfTest`: the probe at the
  world origin read `density 0.0010` and `voxels=438` at CP-012 — the surface of a flat
  plane — and now reads **`density -1.0000` and `voxels=895`**, buried in solid rock under
  the shelf. Nothing else in the project could have done that.

### T-114 — DEF-4, DEF-5 and DEF-7 resolved; build step 3 unblocked

Technical rulings, made and logged by the Architect per **D-023**. The Director's
instruction was one word.

- **DEF-4 → §4.5.1.** Affinity and ownership table, five rules, a four-state shutdown
  machine with a fixed eight-step teardown. The whole `ITerrainBackend` surface is
  game-thread only; `ITerrainDensityField` is the single any-thread exception and is safe
  by construction. **The defect's lock hazard is answered by holding no lock across the
  plugin boundary at all** — not by a better lock order. Cancellation is a *queue*
  operation: `ApplyOp` is synchronous on the game thread and cannot be pre-empted by
  `EndPlay`, travel or PIE exit, so no op is ever partially applied at teardown.
- **DEF-5 → §4.10.** The operation set is **closed at Remove, Add, Paint**; `Flatten` and
  `Smooth` are **removed from it** and permanently refused rather than given invented
  semantics. Canonical write set, read bounds and rounding, with no epsilon anywhere.
  **Monotonicity and idempotence required.** Determinism split into three claims, of which
  **cross-backend value identity is explicitly out of scope** — §8.1 gives the kernel to
  the plugin and the meaning to the game, and those two are consistent only if the game
  specifies properties and the backend supplies values.
- **That has a consequence, and it is written down rather than hidden: a backend swap is a
  resample migration for every EDITED chunk, not a format-compatible reload.** FM-9
  previously flagged only voxel size and grid alignment. Pristine chunks regenerate and
  are unaffected. It also fixes what `Backend.Conformance` means: the **contract**, never
  density equality between two backends.
- **DEF-7 → §4.11.** Trusted-input table; validation on the **quantised** footprint, not
  the float request; `(SourceId, RequestId)` identity with a 64-entry per-connection dedup
  ring; two-phase reserve-then-revalidate; bounded queue, round-robin across sources, no
  priority classes. **`bTruncated` is removed as a success signal** and a false return from
  `ApplyOp` now means *nothing changed*. **Only `Box` ops split** — an over-cap `Sphere` is
  rejected, because a sphere has no exact partition and the permanent 58-byte wire has
  nowhere to put a clip box.
- **This is the specification and its headless evidence. It does not implement step 3.**

### Verification, all executed

| Check | Result |
|---|---|
| `VoxelWorldEditor` and `VoxelWorld` builds | Both `Result: Succeeded` at both increments |
| TerrainCore automation | **Thirteen** tests, all `Result={Success}`, `EXIT CODE: 0`. The seven from CP-012 plus `Field.Shape`, `Field.Strata`, `Field.Range`, `Field.Determinism`, `Op.Semantics.Contract`, `Op.Semantics.Golden` |
| D-011 `#include` boundary probe | `C1083` / `Result: Failed` in `TerrainCore`; file restored byte-identical, md5 verified. **No `.Build.cs` and no `.uproject` change in either increment**, so the D-025 guard is structurally untouched |
| Standalone boot | `Installing the game density field as the voxel world generator (was 'VoxelFlatGenerator')`; `Material config: 0` (RGB); `generator version 1`; **zero `LogVoxel: Error`, zero fatals** |
| `Terrain.SelfTest` | **PASS**, 13 checks |
| The Director's by-hand check | **Play solo works; dig and place both confirmed.** CP-012's outstanding item |

**`Op.Semantics.Golden` is worth one line of its own.** Its seven expected hashes were
recorded once and the test's own error text says a failure is never fixed by updating the
number. Two of them are load-bearing: a repeated op equals the one before it, and a refused
op equals the state before it — so idempotence and no-change-on-failure are pinned by the
fixture as well as by the contract test.

**One test assertion was wrong and the backend was right.** A straddle check expected 8
affected chunks and got 7, because an earlier op in the same fixture had already emptied
that side and `AffectedChunks` reports chunks that actually **changed**. The test now runs
on its own backend, so it measures the contract rather than test ordering.

## What happened at CP-012

**T-113 / build step 2: terrain edits now go through the service, and the two drift
checks flagged since T-101A are cleared for standalone.** This is the first playable
change since T-101A.

- **Authorisation (D-031).** The Director gave blanket execution authority for the
  increment: *"I trust you on all accounts to execute anything as needed for the
  implementation. Begin everything necessary."* No numbered decision changed. §14 binds
  **no** defect to step 2 — DEF-10, the only one that was, is Resolved — so the step was
  clear to start.
- **What the Blueprint used to do, and no longer does.** LMB/RMB ran a camera line trace
  and then called `UVoxelSphereTools::RemoveSphere`/`AddSphere` directly, on the client,
  against an `AVoxelWorld` held in a `TargetVoxelWorld` variable. All three of those are
  gone. The graph went **38 nodes → 19**, the variable is deleted, and `grep` over the
  `.uasset` finds **no `/Script/Voxel` reference at all** — only `/Script/VoxelWorld`,
  our own module. §7.4 forbids a plugin reference in an *asset* separately from the code
  rule, and that is the half no compiler could ever have caught.
- **What replaced it.** New module **`TerrainBackendVPLegacy`** (`FVPLegacyBackend`) — the
  only module permitted to include a plugin header. New in `TerrainCore`: `TerrainChunk`,
  `TerrainSettings`, `FTerrainBackendRegistry`, `UTerrainStreamingComponent`, and
  `UTerrainService::RequestEdit`. New in `VoxelWorld`: `UTerrainInteractionLibrary` —
  trace, then request. The trace stays gameplay code, which §4.3 explicitly permits.
- **Backend selection is a config line.** `BackendModule=TerrainBackendVPLegacy` in
  `DefaultEngine.ini`; the adapter registers its own factory at `StartupModule`; the
  service loads the module **by name**. `TerrainCore` links no backend, in either
  direction — which is what makes §10's swap procedure true rather than aspirational.
- **The streaming component is the DEF-10 answer working in practice.** Gameplay attaches
  a `TerrainCore` class; the backend creates, moves and destroys the plugin invoker
  internally. Confirmed in the log: `Voxel Invoker enabled; Name:
  VoxelSimpleInvokerComponent_0`, owned by the voxel world actor, not the character.
- **AR-5, one determination, routed for cheap overrule (D-031 §2).**
  `FTerrainBackendInit` gains `UWorld* World` and `FTransform OriginTransform`. §4.3 never
  gave a backend a world, which a backend that owns an actor cannot do without; and §8.1
  gives coordinate policy to the *game*, so the service must state where the grid starts
  rather than let the adapter read it off an actor someone may have dragged. Both are
  **engine** types, not plugin types. The memory backend ignores both and conformance
  still passes.
- **Verification, all executed:** both targets `Result: Succeeded`; **seven** TerrainCore
  tests `Result={Success}`, zero failures, exit 0 (the five from CP-010 plus new
  `Chunk.Keys` and `Backend.Registry`); the `#include` boundary probe fails to compile in
  **both** `TerrainCore` *and* `VoxelWorld`, both files restored byte-identical (md5); the
  D-025 game-target guard unchanged (0 MCP plugins, 0 matching products); standalone boots
  with the backend ready and **zero `LogVoxel: Error`**; and `Terrain.SelfTest` **PASS** on
  13 checks — a real dig of **438 voxels across 8 chunks**, chunk revision advanced,
  density inverted, OpSeq monotonic, and three rejection paths correct.
- **The tooling finding (D-031 §4): Unreal MCP was not used, and did not need to be.** The
  server binds loopback on demand and auto-start is off, so it was not listening. **UE 5.8
  turns out to expose a full Blueprint graph API to plain Python** — enumerate, delete,
  create call-function nodes, connect pins, set defaults, remove variables, attach
  components, compile, save. That is AGENTS §11's *third* rung and beats the fourth. The
  rewire is committed as `Tools/Editor/rewire_dig_through_service.py`, idempotent. D-025
  is unchanged; MCP stays adopted, editor-only.
- **Deliberately not built, so nobody reads more into this than is there:** no
  replication, no journal, no yield, no reach/permission/rate-limit validation — steps 3+
  behind DEF-4, DEF-5, DEF-7. `FTerrainEditResult::Removed` is left **empty** and region
  transfer moves **density only** (K9 at step 6, K3/DEF-9 at step 4), so
  **`FVPLegacyBackend` does not yet pass `Backend.Conformance`** — which §10 makes the
  operational meaning of "replaceable". New risk **R-013** tracks exactly that, including
  the fact that the §6.2 in-engine harness to run it does not exist and has no owner.
- **Outstanding: the Director's by-hand LMB/RMB dig**, standalone via
  `Tools\Play-Solo.ps1`. Everything else is verified automatically; until that is run,
  "digging works as today" rests on `Terrain.SelfTest` rather than on the game as played.
- **Next:** **T-101B / build step 3** — but it **cannot start yet**: DEF-4, DEF-5 and
  DEF-7 are all open and bound to it. Closing them is R3 work. Expected incoming
  Implementer: **either** agent.

## What happened at CP-011

**T-112.5 complete: the project is on UE 5.8.2 and Unreal MCP is in, editor-only.**

- **Director rulings (D-030):** install 5.8 **side-by-side, keeping 5.7.4** as the
  rollback; **split T-112.5** into a bump half and an MCP half, one commit each.
- **T-112.5a.** `EngineAssociation` 5.8; VoxelFree **432 → 434 / engine 5.8.0**;
  `Install-VoxelFreeLegacy.ps1` parametrised `-EngineVersion` with a per-version
  known-good fallback table. **The whole CP-006 verification set was re-run green
  under 5.8**, including the `#include` boundary probe — D-011 is still enforced by
  the compiler, not by review — and the Director's by-hand LMB/RMB dig.
- **T-112.5b.** `ModelContextProtocol` + `AllToolsets` enabled with
  `"TargetAllowList": ["Editor"]`; `.mcp.json` generated; the D-025 guard added to
  **AGENTS §9**. The guard is load-bearing: the plugin ships **Runtime** modules, so
  the allow-list is the only thing keeping it out of a game target. Proven from the
  build receipts — the game target lists none of those plugins and zero matching build
  products, against 278 plugins and 70 products for the editor target.
- **Three defects found and fixed, none of them in the approved plan:** the 5.8 plugin
  archive has no wrapping folder (a latent `Substring` bug in the installer, after a
  successful 1.56 GB download); `-Force` parked the old install *inside* `Plugins\`,
  where UBT scans recursively and saw every `Build.cs` twice (`CS0101`); and both
  `Target.cs` pinned `BuildSettingsVersion.V6` / `Unreal5_7`, which 5.8 rejects as a
  conflict with the installed engine's shared build environment. Bumped to **V7 /
  `Unreal5_8`** — recorded as an Implementer determination in **D-030 §3**, cheap to
  overrule while it is one line in each of two files.
- **`Source/**` needed no logic change at all.** The two `Target.cs` lines were the
  only source-tree edit across the whole engine upgrade — evidence for the §4.1 claim
  that `TerrainCore` is engine-agnostic, rather than a restatement of it.
- **Verification:** build `Result: Succeeded` exit 0; five TerrainCore tests
  `Result={Success}`, zero failures, `**** TEST COMPLETE. EXIT CODE: 0 ****` — once
  after the bump (**2026-09-07 01:24:44 UTC**) and again with MCP enabled
  (**02:18:39 UTC**); headless boot with `TerrainService` resolving and PlayerStart
  unchanged at (-8228.66, 0, 150); standalone invoker signature clean with zero
  `LogVoxel` errors; live MCP `initialize` handshake returning **HTTP 200**,
  protocol 2025-06-18, 52 toolsets discoverable.
- **Two things the register now carries (R-008, R-010).** MCP is Epic-**Experimental**,
  accepted deliberately because it is never shipped. And the first cold-cache 5.8 launch
  dropped the player into an endless void: 150 PSO hitches and 18–55 s RTPSO compiles at
  spawn, above a plane whose collision did not exist yet. A warm relaunch generated in
  0.130s and played correctly. Not an upgrade defect — but it is now the *second* route
  into an unrecoverable fall, and the first needs no player action. **A KillZ or respawn
  volume is a prerequisite for real play, not polish.**
- **Limits:** no gameplay, terrain architecture, dependency or save-format change. No
  multiplayer, persistence or production-backend claim. UE 5.8's **Mesh Terrain was not
  evaluated** — T-112.5 was bounded away from it, so it stays a D-025 watch item with no
  evidence either way. All existing drift flags and terrain risks remain open.
- **Next:** **T-113**, build step 2. Expected incoming Implementer: **either** agent.

## What happened at CP-010

**T-112.3 and T-112 complete. Five TerrainCore tests pass headless.**

- Director approved the bounded R2 revision-index/service plan with "go" (D-029).
  Codex implemented three new source files and updated TerrainService.h/.cpp.
- `FTerrainRevisionIndex` returns zero for unseen keys without insertion, bumps
  each distinct affected key once per call, and rejects an entire update before
  mutation if any revision would overflow. Empty input succeeds without changes.
- `UTerrainService` privately owns the index. Its metadata update helper requires
  the game thread, initialized subsystem/world, a game world and non-client mode.
  Repeated initialization preserves history; teardown releases ownership. No
  backend execution, global sequencing or public gameplay edit API exists yet.
- `TerrainCore.Revision.Monotonic` checks 256 overlapping batches against an
  independent per-key oracle, duplicates, negative/extreme keys, empty input,
  unaffected chunks and atomic overflow. Worldless service ownership, lifecycle
  and missing-world/worker-thread rejection are checked without creating UWorld.
- **Verification:** UE 5.7 `VoxelWorldEditor Win64 Development` succeeded, exit 0.
  All five TerrainCore tests passed at **2026-09-06 17:25:08 UTC**; zero failures,
  automation and process exit 0. Evidence: `Saved/Logs/VoxelWorld.log` (local).
  Exact commands and limits are in HANDOFF and README. Author diff review and
  whitespace checks passed; Build.cs and existing interfaces remain unchanged.
- **Limits:** no live server/client authority, multiplayer PIE, persistence,
  compaction or production-backend result. Existing drift flags and terrain risks
  remain open. Future edit integration must handle revision exhaustion before
  terrain mutation; DEF-7 is not resolved by metadata atomicity.
- **Next:** T-112.5 under D-025. Expected incoming Implementer: **either** agent.
  This checkpoint saves the completed increment; it does not start the upgrade.

## What happened at CP-009

**Documentation-only session wrap-up, explicitly requested by the Director.**

- README now reflects the built code, four-test baseline, exact UE 5.7 commands and
  remaining terrain gate. Skimmed all 26 tracked project Markdown files, including
  archived reviews/benchmark material; historical evidence was left intact.
- **D-028:** alternate Claude and Codex according to available usage/time, with one
  active Implementer. **Claude is expected next for T-112.3**; either agent may receive
  the same handoff. This supersedes a fixed primary vendor, not the role boundaries.
- `OPERATIONS.md` §5.1 defines receive → scope → brief decision/evidence breadcrumbs
  → pre-limit save → formal handoff → authorized checkpoint/push. `HANDOFF.md` is the
  single rolling artifact, linked by AGENTS, README, CLAUDE and the universal opener.
  It includes the T-112.2 evidence and the next safe steps for T-112.3, without choosing
  its unspecified API or extending D-027's bounded technical delegation.
- Updated active workflow docs to remove obsolete vendor gates and reconcile risk
  rules with AGENTS. The earlier benchmark remains historical, not another startup task.
- **Validation:** documentation diff/relative-link checks only; no source or assets
  changed. The last code validation remains CP-008 (`306348a`): build succeeded,
  four tests passed, 0 failures, exit 0. No new build/PIE result or risk closure claimed.

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

**C++ (builds from source; T-112 complete at CP-010):**

```text
Source/
  VoxelWorld.Target.cs             Game target      | BuildSettingsVersion.V6
  VoxelWorldEditor.Target.cs       Editor target    | EngineIncludeOrderVersion.Unreal5_7
  VoxelWorld/                      primary game module — depends on TerrainCore ONLY
  TerrainCore/                     Core, CoreUObject, Engine. NO plugin dependency.
    Public/TerrainService.h        UTerrainService : UWorldSubsystem — revision skeleton
    Public/TerrainRevisionIndex.h in-memory, monotonic per-chunk revisions
    Public/TerrainTypes.h          §4.2 value types + §4.3 FTerrainPointSample
    Public/TerrainOp.h             FTerrainOp + the 58-byte codec's declarations
    Public/TerrainQuantise.h       QuantiseEdit / DequantiseVoxel / QuantiseRadiusQ16
    Public/ITerrainBackend.h       eleven-method interface + FTerrainBackendInit
    Public/ITerrainDensityField.h  Sample declaration only — implementers at T-108
    Public/MemoryTerrainBackend.h dense reference backend + documented determinations
    Private/TerrainOp.cpp          the codec — PERMANENT FORMAT
    Private/TerrainQuantise.cpp    world→voxel, Floor, fixed by §4.3
    Private/MemoryTerrainBackend.cpp synchronous data, edits, queries, interest, transfer
    Private/TerrainRevisionIndex.cpp distinct-key updates with atomic overflow rejection
    Private/TerrainService.cpp    index ownership, lifecycle and authority gates
    Private/Tests/                 TerrainOpCodecTest.cpp, TerrainQuantiseTest.cpp
                                   BackendConformance.h/.cpp (factory suite + Query.Point)
                                   TerrainRevisionTest.cpp (Revision.Monotonic)
```

- **`VoxelWorld`** — the primary game module (`IMPLEMENT_PRIMARY_GAME_MODULE`). Gameplay,
  characters, tools, UI hooks. `ARCHITECTURE.md` §4.1: it depends on `TerrainCore` only, and
  never on a backend module.
- **`TerrainCore`** — game-owned, compiles headless, holds no plugin type. **Its `Build.cs`
  is the boundary** (§4.1, §4.1.0). Adding a plugin to that dependency list breaks D-011 and
  the `AGENTS.md` §9 drift guard and requires a numbered decision, not an edit.
- **`UTerrainService : UWorldSubsystem`** — **revision skeleton at CP-010.** K7/D-024.
  Owns a private in-memory revision index and gates its internal metadata helper.
  Backend execution, validation, global sequencing, journalling and yield remain
  later build-step work; gameplay still does not route through this service.
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
  - `VoxelWorld_T101A` — 50 cm voxels, 1024 voxels (512 m), collisions on,
    `WorldGridMaterial` (the engine checker grid — projected from world position, so it
    reads correctly on UV-less procedural meshes and makes holes and overhangs legible;
    plain `BasicShapeMaterial` rendered white-on-white).
    **Its authored generator no longer decides anything**: since T-108 the backend installs
    the game's density field over whatever the actor carries, and logs that it did.
  - PlayerStart at **(-8228.66, 0, 150)**, ~82 m west of the hill, facing it. **As of T-108
    that offset is correct again** — there is a hill there, the generated plain is held
    below world Z = 0 so the spawn is always in open air, and `Field.Shape` asserts both.
  - `BP_ThirdPersonCharacter` wired for digging: LMB → line trace → `RemoveSphere`,
    RMB → `AddSphere`, radius 200, 1000 uu reach, both behind a hit `Branch`.
  - ✅ **Both of the warnings that stood here from T-101A to CP-012 are gone, closed by
    T-108.** They were one cause with two symptoms: the hill was *sculpted* by
    `Tools/Editor/place_voxel_world.py` into a running editor session only, so any process
    loading the level from disk regenerated a flat plane, and the PlayerStart offset
    therefore pointed at nothing. The world is now **generated** by `FTerrainWorldField`
    from position and seed, so it is identical in every process with no save file — which
    is why this arrived at step 8 rather than waiting for persistence at step 4.
    **`place_voxel_world.py` must not be re-run to sculpt terrain**; it is kept only for
    placing and configuring the actor. Player *edits* still do not survive a restart —
    that is build step 4 and is a different question from the world's shape.
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

**One gameplay system now exists end to end: digging.** `UTerrainService::RequestEdit` is
the sole entry point, `FVPLegacyBackend` executes it, and the rewired Blueprint is the only
caller. Authoritative in **standalone only** — there is no replication, no journal and no
yield, and server authority is not proven until build step 3.

## Current task

**Nothing is outstanding from CP-013.** Build steps 0, 1, 2 and 8 are complete; the
Director's by-hand dig closed step 2, and the by-eye look at the generated hill is the only
thing left from T-108 and it is optional — `Field.Shape` asserts the shape headlessly.

**Next: T-101B / build step 3 — and it MAY now start.** §9 bound it to K1, K4, DEF-4, DEF-5
and DEF-7. K1 and K4 were ruled at CP-005 (D-024); the three defects were **resolved at
T-114** (§4.5.1, §4.10, §4.11), so §14's rule no longer reaches this step. It is the largest
single piece of work in the phase and should be its own increment:

- **Server validation** to the §4.11 specification: trusted inputs, quantised-footprint
  checks, `(SourceId, RequestId)` dedup ring, two-phase reserve-then-revalidate, bounded
  fair queue, the four new rejection reasons.
- **`ServerRequestEdit` / `ClientApplyOp`** and the subscription set (§4.4).
- **The serialised execution path and the shutdown state machine** to §4.5.1.
- **Split operations** for box ops only (§4.11.7), with `Split.Equivalence`.
- **New `Backend.Conformance` clauses**: off-game-thread refusal, state-machine rejection
  reasons, and a failed `ApplyOp` leaving the region hash unchanged.
- **Ends with** 3-client PIE convergence (`MP.Convergence`), which is what turns the
  server-authority drift check from "standalone only" into a real result.

**Incoming Implementer: either** Claude or Codex according to availability (D-028). Read
`HANDOFF.md` first.

**Also open, and unassigned:** **R-013** — the production adapter has not passed
`Backend.Conformance`, and the §6.2 in-engine harness that would run it does not exist;
§4.10.4(c) now defines what that pass can and cannot mean. **R-010's KillZ** remains a
prerequisite for anyone actually playing, and the plain now sits 3 m lower than the old flat
plane so the spawn drop is longer. **R-014** is the new cross-platform kernel-determinism
watch item. None is build step 3's job unless the Director says so.

## Drift checks (VISION.md, run at CP-013)

**BOTH FLAGS STILL CLEAR, and still for standalone only.** Nothing at CP-013 touched the
edit path: T-108 changed what the world is made of, not who is allowed to change it, and
T-114 wrote specification. The `#include` probe still fails to compile in `TerrainCore` and
no `.Build.cs` or `.uproject` changed in either increment.

- [x] **Every gameplay system is server-authoritative — CLEARED for standalone.** Unchanged
      from CP-012 and it will stay unchanged until **build step 3** exercises a real client.
- [x] **The terrain backend remains replaceable — CLEARED, and the limit is now defined.**
      §4.10.4(c) rules that `Backend.Conformance` asserts the **contract** and never density
      equality between backends, so R-013's "has not passed conformance" now has an exact
      meaning. The D-011 boundary itself is untouched and compiler-enforced.
- [x] **Voxels are still invisible to the player (D-015).** Worth re-checking deliberately
      this checkpoint, because T-108 added a strata colour palette. It is **cosmetic and
      explicitly not the K9 catalog** — it exists so the bands are visible in the cliff
      face. Nothing reads a colour back and no yield is computed from one. The player sees
      rock, soil and ore, which is D-015's intent, not voxels.

The CP-012 record below is retained as the fuller statement of why the flags cleared.

## Drift checks (VISION.md, run at CP-012)

**BOTH FLAGS CLEAR — for standalone only.** They were flagged from T-101A to CP-011 with a
single cause: `BP_ThirdPersonCharacter` called `UVoxelSphereTools::RemoveSphere`/`AddSphere`
directly, on the client, from gameplay. T-113 deleted that. The graph went 38 nodes to 19,
the `TargetVoxelWorld` variable is gone, and the asset contains **no `/Script/Voxel`
reference at all**. Every terrain change now enters through `UTerrainService::RequestEdit`.

- [x] Terrain is smooth-voxel and player-deformable — proven at T-101A
- [x] **Every gameplay system is server-authoritative — CLEARED for standalone at CP-012.**
      `RequestEdit` refuses on a client with `NoAuthority` rather than editing a local copy.
      **Server authority is NOT proven**: there is no `ServerRequestEdit` RPC and no
      `ClientApplyOp` until build step 3, so "authoritative" today means "there is exactly
      one authority and it is this process". Re-check at step 3 against a real client.
- [x] The tech path still leads to electricity and machines
- [x] Scope is still one planet, 16–32 players
- [x] Development is still incremental, Minecraft-alpha style
- [x] The five inspiration games above are still the reference set
- [x] **The terrain backend remains replaceable (D-010, D-011) — CLEARED at CP-012**, with
      a named limit. The D-011 violation is gone and the boundary is compiler-enforced on
      **both** game modules (`#include` probe → `C1083` in each). Backend selection is a
      config line through a name→factory registry, so `TerrainCore` links no backend.
      **The limit:** §10 defines replaceability operationally as passing
      `Backend.Conformance`, and `FVPLegacyBackend` does not yet — materials, region
      transfer and three operations are bound to later steps. That is **R-013**, tracked as
      a risk rather than as drift, because it is a known incompleteness on a planned path
      and not a violation of the boundary.
- [x] Voxels are still invisible to the player (D-015) — grid material is placeholder

**What would re-flag these.** Any gameplay code or asset that calls the plugin again; any
edit path that bypasses `RequestEdit`; a `Build.cs` gaining a plugin dependency; or a client
being allowed to apply an edit it was not told about by the server.

## R-012 check (process weight, run at CP-012)

**PASS — and the run of headless-only steps has ended, as CP-008 and CP-010 both said to
watch for.** T-113 produced a playable change: digging works, through the service, and the
two drift checks flagged since T-101A are cleared for standalone.

Cost to the Director: **one sentence of authorisation**, and no new process task, document
or gate was created. Checkpoint text was held until "checkpoint" was typed.

**One process cost, recorded honestly:** the session hit the usage limit mid-task. The
handoff breadcrumb written *before* the risky editor work is what made the pickup cost
about one message instead of a re-derivation — which is exactly what D-028 was written for.
The lesson worth keeping: write the breadcrumb before the risky half, not after it.

**CP-010 check, kept as history:** PASS for that bounded step; the shared handoff enabled
pickup without reconstructing the previous chat, one R2 plan approval preceded code, and
T-112 completed with five green tests. No playable change was claimed there.

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
| Implementer | Alternating Claude/Codex (D-028); outgoing Claude, either agent next. **The next task is R3**, so whoever implements must not also review it |
| Architect | Opus in the Claude app. D-027's delegation was limited to the completed T-112.2; D-031's authority was limited to T-113. **AR-5 is awaiting an Architect/Director confirmation** and is cheap to overrule |
| Independent reviewer | Whichever vendor did not author (R3 only) |

## Toolchain status

| Tool | Status |
|---|---|
| **UE 5.8** | ✅ **5.8.2** at `C:\Program Files\Epic Games\UE_5.8` — the build/test engine since T-112.5 (D-025). Editor and game targets both build; **seven** TerrainCore tests green |
| UE 5.7 | ✅ 5.7.4 at `C:\Program Files\Epic Games\UE_5.7` — **kept deliberately** as the T-112.5 rollback path (D-030). Not the build engine |
| Git + LFS | ✅ git-lfs 3.7.1, push credentials verified |
| Claude Code | ✅ Installed, verified in-repo |
| Codex (Astra, D-018 / D-027) | ✅ Onboarded in-repo at CP-008: docs, source edits, UE 5.7 build and four headless tests exercised |
| **Voxel Plugin Free Legacy** | ✅ **v434 / 159fd19a0 / 5.8 — mounts clean** (gitignored). Bumped from v432 at T-112.5; the 5.7 build is kept at `Tools\downloads\VoxelFree.bak-*-engine5.7.0` |
| **Unreal MCP** (`ModelContextProtocol` + `AllToolsets`) | ✅ Enabled **editor-only** at T-112.5 via `TargetAllowList` (D-025 guard, AGENTS §9). Epic-**Experimental**. `.mcp.json` at the repo root points at `http://127.0.0.1:8000/mcp`. **Auto-start is OFF** — start it with `ModelContextProtocol.StartServer`; it binds loopback with no authentication. **CP-012: not used for the T-113 Blueprint rewire** — it was not listening, and scripted Python did the job (D-031 §4). Still adopted; the §9 guard stands |
| Voxel Plugin 2 | ❌ Paid, gated on owning Pro Legacy — upgrade candidate only (R-008) |
| Python Editor Script Plugin | ✅ Enabled at CP-002 — the primary way work gets done here. **CP-012: UE 5.8 exposes a full Blueprint graph API to it** (`unreal.BlueprintGraphEditor`, `BlueprintGraphPinLibrary`, `BlueprintEditorLibrary`, `SubobjectDataSubsystem`): enumerate/delete nodes, create call-function nodes, connect pins, set defaults, remove variables, attach components, compile, save. **Try this before starting the MCP server.** Run a script headlessly with `UnrealEditor-Cmd.exe <uproject> -unattended -nopause -nosplash -nullrhi -ExecutePythonScript="<path>"` |
| Editor Scripting Utilities | ✅ Enabled at CP-002 |
| Visual Studio 2022 (C++ workload) | ✅ 17.14.37614.0, MSVC 14.44.35207, Win SDK 10.0.26100 — **exercised and verified at CP-006.** `VoxelWorldEditor Win64 Development` → `Result: Succeeded` (63 actions, 85s cold). Toolchain reported by UBT: MSVC 14.44.35228 / Windows 10.0.26100.0 SDK |
| Editor revision control | ⚠️ Enabled and failing checkout on every scripted save, popping a modal each time. Saves succeed anyway. Set Provider to None when it gets in the way. |

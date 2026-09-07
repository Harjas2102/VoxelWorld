# HANDOFF.md — Current agent handoff

> Rolling work log and formal handoff for either agent. Follow OPERATIONS §5.1.
> STATE owns checkpoint status; approved architecture remains in ARCHITECTURE and
> DECISIONS. This file records actionable evidence, not a new ruling.

## Identity and status

- Updated: 2026-09-06, **CP-011** checkpoint; T-112.5 verified 2026-09-07 01:24 and
  02:18 UTC. The CP-010 record below is retained as history.
- Outgoing: **Claude**, Implementer. Incoming: either agent; one active Implementer.
- Task: **T-112.5 complete at CP-011.** The project is on UE 5.8.2 with VoxelFree 434,
  and Unreal MCP is enabled editor-only. **T-113 has not started.**
- Risk/authorization: R2. Director typed "Begin T-112.5", approved the plan in plan
  mode, ruled the side-by-side install and the a/b split, confirmed the hand dig with
  "It worked perfectly", typed "go" for T-112.5b, then "checkpoint". Approval is
  bounded to this increment and its two commits, this checkpoint and its push.
- Base: `3aedeac` (CP-010), branch `main`. Clean on receipt; `git pull --ff-only`
  reported already up to date. Work landed as `26c6eb4` (T-112.5a) and `2e2181a`
  (T-112.5b), both pushed `main -> main`. This handoff belongs to the enclosing CP-011
  commit; verify its final commit/push and a clean worktree from Git on receipt.
- Checkpoint scope: STATE, BACKLOG, DECISIONS, RISKS and this handoff. **No active
  build, test or editor process remains** — every UnrealEditor process this session
  started has been stopped. No unfinished implementation. Preserve any unexpected
  dirty work on pickup.

## Next safe actions (CP-011)

1. Read the required docs and this handoff. Verify the CP-011 commit, main/origin
   state and worktree; sync with `git pull --ff-only`.
2. Recite CP-011 and T-113. **T-112.5 is complete — do not repeat its installs,
   planning or verification** unless new changes or failures justify it.
3. **T-113 is next**: `FVPLegacyBackend`, `UTerrainStreamingComponent`, and rewiring
   the T-101A dig Blueprint through the service so the direct plugin calls are deleted.
   Both flagged drift checks clear there, standalone only. Confirm its bounded
   task/risk plan with the Director before implementation. Unreal MCP is now available
   for the Blueprint rewiring — that was D-025's whole reason for sequencing it here.
4. Build and test with **5.8** paths (`C:\Program Files\Epic Games\UE_5.8`); README
   carries the exact two commands. Use `-ExecCmds=...; Quit` with a **semicolon** for
   automation runs — see the console-command gotcha in the T-112.5b section below.
5. **Two open items this session surfaced but did not fix**, both recorded in RISKS:
   a KillZ or respawn volume (R-010) and the un-evaluated Mesh Terrain watch item
   (R-008). Neither is T-113's job unless the Director says so.

---

## T-113 working breadcrumb (D-028) — build step 2, IMPLEMENTATION COMPLETE

- 2026-09-06/07, Claude, Implementer. Base `cbcacf4` (CP-011), clean on receipt;
  `git pull --ff-only` reported already up to date. Director typed "I trust you on all
  accounts to execute anything as needed for the implementation. Begin everything
  necessary", which is the authorisation for this increment. **Nothing is committed yet.**
- Risk class: the task is build step 2 in ARCHITECTURE §9. **No defect is bound to step 2**
  (DEF-10, the only one that was, is Resolved), so the step is clear to start under §14's
  own rule. Work stays inside the approved architecture; no numbered decision is changed.

### One architecture gap found and closed as AR-5 — read this first

§4.3 lists `Initialize`'s inputs as "seed, gen version, voxel size, bounds, density field,
role" and stops there. That is complete for a backend that owns only memory. It is NOT
complete for `FVPLegacyBackend`, which must find or spawn an `AVoxelWorld`, attach invoker
components to it and destroy them at teardown — every one of which needs a `UWorld`. The
alternative was for the adapter to reach for `GWorld` and guess.

**AR-5 adds two fields to `FTerrainBackendInit`: `UWorld* World` and
`FTransform OriginTransform`.** Both are ENGINE types, not plugin types, so this widens what
the game tells a backend without widening what a backend may tell the game. `OriginTransform`
exists because §8.1 assigns coordinate policy to the GAME: the service states where the grid
starts and the adapter conforms its actor to it, rather than the adapter reading the origin
off an actor someone may have dragged. `FMemoryTerrainBackend` ignores both and still runs
headless; the conformance suite leaves them defaulted and still passes.

This follows the AR-1..AR-4 precedent (an Architect determination recorded in the docs, not a
new numbered decision). **It wants a DECISIONS entry at checkpoint and a Director ruling.**

### What is written and building

New in `TerrainCore`: `TerrainChunk.h/.cpp` (the one definition of voxel-to-chunk keying),
`TerrainSettings.h/.cpp` (the `[/Script/TerrainCore.TerrainSettings]` section §4.1 names),
`TerrainBackendRegistry.h/.cpp` (name-to-factory, so TerrainCore never links an adapter),
`TerrainStreamingComponent.h/.cpp` (§7.4 / DEF-10), and `UTerrainService::RequestEdit` plus
backend and interest lifecycle. New module `TerrainBackendVPLegacy` holds `FVPLegacyBackend`
and is **the only module that may include a plugin header**. New in `VoxelWorld`:
`UTerrainInteractionLibrary`, the trace-then-request node the Blueprint will call.

Green as of this breadcrumb: `VoxelWorldEditor` and `VoxelWorld` both `Result: Succeeded`;
**seven** TerrainCore tests all `Result={Success}`, `EXIT CODE: 0` — the five from CP-010
plus new `Chunk.Keys` and `Backend.Registry`.

### Deliberate step-2 limits, recorded so nobody reads more into this than is there

- `FTerrainEditResult::Removed` is left EMPTY. Yield is step 6, DEF-6 is open, and §2.3
  records that `FModifiedVoxelValue` carries no material — so a number here would be invented.
- `ReadRegion`/`WriteRegion`/`HashRegion` move DENSITY ONLY, materials zero (K9, step 6).
  Useful as a convergence oracle; **not** the snapshot format, which is step 4 under K3/DEF-9.
- Therefore `FVPLegacyBackend` does **not** pass the full `Backend.Conformance` suite yet and
  no such claim is made. §10 requires that pass before "replaceable" is proven.
- `FlushPendingWork` is a deliberate no-op (§4.5: mesh and collision work is explicitly not
  serialised by us). Flatten, Smooth, Paint and box ops are refused, not approximated.
- Still absent by design: `ServerRequestEdit`/`ClientApplyOp`, journal, yield, reach and
  permission validation, split ops. All bound to steps 3+ behind DEF-4/5/7.

### Verification — everything below was executed, not reasoned about

Engine `C:\Program Files\Epic Games\UE_5.8`, run from `C:/Dev/VoxelWorld`.

| Check | Result |
|---|---|
| `VoxelWorldEditor Win64 Development` build | `Result: Succeeded`. The only warnings are two pre-existing `C4305` in the plugin's own headers |
| `VoxelWorld Win64 Development` (game target) build | `Result: Succeeded` |
| TerrainCore automation | **Seven** tests, all `Result={Success}`, zero failures, `**** TEST COMPLETE. EXIT CODE: 0 ****`. The five from CP-010 plus new `Chunk.Keys` and `Backend.Registry` |
| **D-011 `#include` boundary probe, both game modules** | With `#include "VoxelTools/VoxelDataTools.h"` in `TerrainCore/Private/TerrainCore.cpp`: `fatal error C1083` / `Result: Failed`. With `#include "VoxelTools/Gen/VoxelSphereTools.h"` in `VoxelWorld/VoxelWorld.cpp`: `fatal error C1083` / `Result: Failed`. **Both files then restored byte-identical, md5 verified.** The boundary is compiler-enforced on *both* game modules, not only TerrainCore |
| D-025 guard (AGENTS §9) still holding | Editor target: 278 plugins including `ModelContextProtocol` and `AllToolsets`, 71 matching build products. **Game target: 234 plugins, neither present, 0 matching products.** Unchanged by this increment |
| Blueprint asset scrubbed | `grep -oE "/Script/Voxel[A-Za-z]*"` over `BP_ThirdPersonCharacter.uasset` returns **only `/Script/VoxelWorld`**, our own game module. **No `/Script/Voxel` plugin reference remains in the asset** |
| Standalone boot | `LogTerrainCore: Terrain backend registered: TerrainBackendVPLegacy`; `FVPLegacyBackend ready ... voxel 50.0 cm, world 1024 voxels, created=yes, role=Server`; `bounds=[-512,-512,-512)-[512,512,512)`. `LogVoxel: Voxel Invoker enabled; Name: VoxelSimpleInvokerComponent_0` — our streaming interest, created through the backend. **Zero `LogVoxel: Error`, zero fatals** |
| `Terrain.SelfTest` in standalone | **PASS**, 13 checks, zero failures |

`Terrain.SelfTest` is the evidence that matters, because it drives the same `RequestEdit`
path the rewired Blueprint drives:

```
backend='TerrainBackendVPLegacy' ready=1 authority=1 interests=1
voxel (0,0,0) chunk (0,0,0) rev 0, density 0.0010, resident 1
Remove r=200cm -> applied=1 reason=None opseq=1 chunks=8 voxels=438
density 0.0010 -> 1.0000, rev 0 -> 1
ok  Add placed material            ok  OpSeq is monotonic across operations
ok  an over-large radius is refused as RadiusTooLarge
ok  an edit outside the world is refused as OutOfBounds
ok  a negative radius is refused as BadRequest
**** Terrain.SelfTest: PASS ****
```

438 voxels over **8** chunks is correct rather than surprising: the test digs at the world
origin, which is exactly where eight chunks meet, and `Chunk.Keys` asserts that case
separately. Evidence logs `Saved/Logs/VoxelWorld.log`, `Standalone_T113.log` and
`Standalone_T113edit.log` are local, gitignored, and overwritten by later runs.

The game target is **monolithic**, so it produces no separate adapter DLL and no
`Terrain`-named build product; its clean build with the module in the `.uproject` is what
shows the adapter is in it. The editor target does produce
`UnrealEditor-TerrainBackendVPLegacy.dll`.

### The Blueprint rewire, and how it was done

`Tools/Editor/rewire_dig_through_service.py` (committed, idempotent, re-runnable) did it.

**Unreal MCP was not used and could not be.** The MCP server binds loopback on demand and
Auto Start Server is off, by D-025's own reasoning — so it was not listening when this
session started and its tools were unavailable for the whole session. That turned out not to
matter: UE 5.8 ships a full Blueprint graph API to plain Python
(`unreal.BlueprintGraphEditor`, `unreal.BlueprintGraphPinLibrary`,
`unreal.BlueprintEditorLibrary` — `list_all_nodes`, `remove_nodes`,
`add_call_function_node`, `try_create_connection`, `set_pin_value`, `remove_member_variable`,
`compile_blueprint`). That is AGENTS §11's third rung, and it is better than the fourth.
**Whoever automates editor work next should reach for that before starting the MCP server.**

The graph went from **38 nodes to 19**. Deleted: both `LineTraceByChannel` chains, both
`BreakHitResult`, both `Branch`, `RemoveSphere`, `AddSphere`, both camera-manager chains, and
the `Event BeginPlay -> GetActorOfClass(VoxelWorld) -> Set TargetVoxelWorld` chain. Deleted
variable: `TargetVoxelWorld`, an `AVoxelWorld` reference **held in the asset** — §7.4 forbids
that separately from the code rule, and no compiler could ever have caught it. Kept: the Left
and Right Mouse Button events, each now feeding one `RequestTerrainEditFromView` node with
`Kind=Remove` / `Kind=Add`, `RadiusCm=200`, `TraceDistanceCm=1000` — the T-101A numbers
exactly, so the hand check compares like with like. Also attached
`UTerrainStreamingComponent`. Blueprint compiled clean and saved.

### What is outstanding

1. **The Director's hand check. It is the only thing between here and "step 2 done".**
   Run `Tools\Play-Solo.ps1`, standalone and not PIE. **LMB should dig and RMB should place,
   exactly as they did at T-112.5a** — that is the whole test. It is the CP-006 by-hand check
   reproduced once more, now through the service. In the console, `Terrain.SelfTest` gives
   the same answer without a mouse, and `Terrain.Status` / `Terrain.Edit <Remove|Add> X Y Z
   [RadiusCm]` are there for poking at it by hand.
   *One note for that run:* the plugin still logs `No Voxel Invoker found, using camera as
   invoker` a millisecond before ours registers, so both invokers end up live in standalone.
   Harmless here, but it means the camera invoker is still doing some of the LOD work, and
   that stops being true on a dedicated server. Worth a look at build step 3, not now.
2. **AR-5 wants a Director ruling and a DECISIONS entry** — see the top of this section.
3. **STATE / BACKLOG / DECISIONS / RISKS are deliberately NOT updated.** AGENTS §1 reserves
   checkpoint text for the word "checkpoint", which has not been typed for this increment.
   The **two flagged drift checks in STATE therefore still read FLAGGED**, and should flip to
   passing **for standalone only** at that checkpoint — server authority is not proven until
   build step 3, and §9 says to record the narrower result.

### Next safe action

Hand check, then `checkpoint`. **Do not start build step 3**: it is bound to DEF-4, DEF-5 and
DEF-7, all open, and §14's rule is that a step may not start while an unresolved defect is
bound to it.

## Completed files and decisions — CP-010 (history)

All source paths are relative to `C:/Dev/VoxelWorld/Source/TerrainCore/`.

| File | Result |
|---|---|
| Public/TerrainRevisionIndex.h (new) | Plain index API, no setter/reset/assignment or persistence API |
| Private/TerrainRevisionIndex.cpp (new) | Unseen reads return zero without insertion; distinct affected keys bump once per call |
| Public/TerrainService.h (modified) | Private owned index, public const queries and private metadata update helper |
| Private/TerrainService.cpp (modified) | Lifecycle ownership and game-thread/initialized-game-world/non-client gate |
| Private/Tests/TerrainRevisionTest.cpp (new) | TerrainCore.Revision.Monotonic, guarded by WITH_DEV_AUTOMATION_TESTS |

- AR-4 fixes in-memory monotonicity and exactly one bump per affected chunk.
  D-029 and ARCHITECTURE's CP-010 block record the approved bounded API plan.
- TryBumpRevisions deduplicates within one call. Repeated calls remain separate
  updates; retry deduplication/global OpSeq assignment remain later service work.
- Before any mutation, all affected revisions are checked against MAX_uint32.
  Overflow returns false without insertion or increments. Empty input succeeds.
  A saturated chunk does not block updates that affect only other chunks.
- Service Initialize creates ownership; repeated Initialize preserves history.
  Deinitialize releases the index; double teardown is safe. Public access is
  game-thread-only. Private TryAdvanceRevisions rejects off-thread calls before
  touching UObject/world state and otherwise requires HasAuthority.
- This helper updates metadata only; it does not execute backend edits. Future
  integration must handle revision exhaustion before terrain mutation; this
  increment does not resolve DEF-7's mutation/commit atomicity.
- ARCHITECTURE §6.1 explicitly requires no engine world. Tests therefore exercise
  the pure index and worldless service lifecycle/rejection. No UWorld is created.
  A development-only friend seeds overflow and owned-index fixtures; there is no
  shipping setter, role switch or public mutation bypass.

## Verification and limits — CP-010 (history; 5.7 commands, superseded by 5.8)

Executed from `C:/Dev/VoxelWorld`:

```powershell
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat' VoxelWorldEditor Win64 Development '-Project=C:\Dev\VoxelWorld\VoxelWorld.uproject' -WaitMutex
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'C:\Dev\VoxelWorld\VoxelWorld.uproject' '-ExecCmds=Automation RunTests TerrainCore; Quit' -unattended -nopause -nosplash -nullrhi -log
```

- Build: Result: Succeeded, exit 0; UHT passed, seven build actions, 4.85 seconds.
- Automation: 2026-09-06 17:25:08 UTC, five Result={Success} entries:
  Backend.Conformance, Op.Codec.RoundTrip, Op.Quantisation.Stable, Query.Point,
  Revision.Monotonic (all prefixed TerrainCore). Zero failures; automation and
  process exit 0. Evidence: Saved/Logs/VoxelWorld.log, lines 1113–1145, local and
  gitignored; may be overwritten by later runs.
- Revision coverage: 256 deterministic overlapping batches checked against a
  per-key count-of-containing-calls oracle, duplicates, negative/extreme keys,
  empty input, unaffected keys, max boundary and overflow atomicity in both
  input orders. Service ownership, repeated initialization, missing-world and
  worker-thread rejection, teardown and post-teardown rejection pass.
- Author diff review complete: all mutation follows overflow validation; service
  mutation is private and authority-gated. No independent review claimed.
- git diff --check clean; new/modified source files have no trailing whitespace.
  All 14 relative link targets in checkpoint docs resolve. Build.cs, TerrainTypes
  and ITerrainBackend unchanged against the base commit. Includes
  are engine/game-owned only; no new game-owned type/file name contains Voxel.
  Content, Config and uproject unchanged. Checkpoint docs now reflect CP-010.
- No live server/client authority, multiplayer PIE, production backend, disk
  persistence or compaction result is claimed. All existing terrain risks and
  drift flags remain open. T-112.5 engine upgrade has not started.

## Next safe actions as written at CP-010 — superseded, kept as history

1. Read required docs and this handoff. Verify the enclosing CP-010 commit,
   main/origin state and worktree. Sync a clean tree with git pull --ff-only.
2. Recite CP-010 and T-112.5. T-112.3 is complete; do not repeat its API planning,
   onboarding or tests unless new changes/failures justify doing so.
3. T-112.5 is next under D-025. Confirm its bounded task/risk plan and verify its
   engine/tooling prerequisites before changes. T-113 follows; neither has started.

---

## T-112.5 working breadcrumb (D-028) — kept as the record of how it went

- 2026-09-06, Claude, Implementer. Base `3aedeac` (CP-010), clean on receipt,
  `git pull --ff-only` already up to date. Risk class **R2**; plan approved by the
  Director in plan mode. No source or doc file changed yet other than this breadcrumb.
- **Blocked on a Director action.** UE 5.8 is **not installed** on this machine:
  `LauncherInstalled.dat` lists only `UE_5.7` (5.7.4). 719 GB free on C:.
  Director ruled: install `C:\Program Files\Epic Games\UE_5.8` **side-by-side, keeping
  5.7** as the rollback path. Nothing else starts until that install completes.
- Director also ruled the increment is **split**: **T-112.5a** = engine/plugin bump plus
  the CP-006 verification set re-run green (one commit); **T-112.5b** = enable
  `ModelContextProtocol` + `AllToolsets` editor-only, generate `.mcp.json`, add the D-025
  drift-guard line to AGENTS §9 (second commit).
- **D-025's premise re-verified against the live source, and it holds.**
  VoxelPluginFreeLegacy's README now advertises
  `VoxelFree-434-159fd19a0-5.8-Binaries.zip` next to the 5.7 zip, so
  `Tools/Install-VoxelFreeLegacy.ps1` only repoints. Note the plugin version moves
  **432 → 434**; this is a plugin build change, not only an engine repoint, so the
  CP-006 dig/invoker checks are a real regression gate, not a formality.
- UE 5.8 released 2026-06-17. Unreal MCP serves `http://127.0.0.1:8000/mcp`, loopback,
  no auth, and Epic labels it **Experimental** — a RISKS line against R-008 at
  checkpoint. `.mcp.json` comes from `ModelContextProtocol.GenerateClientConfig
  ClaudeCode` in the editor console. Auto Start Server stays **off** unless the Director
  says otherwise.
- Next safe action: confirm `C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\
  Build.bat` exists, then run T-112.5a step 1 (`Install-VoxelFreeLegacy.ps1 -Force`).
  Full plan: `C:\Users\harja\.claude\plans\whimsical-herding-lagoon.md`.

### T-112.5a evidence — automated checks green under UE 5.8.2

Engine `5.8.2-56702186+++UE5+Release-5.8` (compatible 5.8.0), installed side-by-side
with 5.7.4. Backend: Voxel Plugin Free Legacy **434 / 159fd19a0 / engine 5.8.0**,
prebuilt Win64 binaries, mounted (`LogPluginManager: Mounting Project plugin VoxelFree`).

| CP-006 check | Result under 5.8 |
|---|---|
| `VoxelWorldEditor Win64 Development` build | `Result: Succeeded`, exit 0, 56 actions, 92.7s cold. Zero compile errors |
| Five TerrainCore automation tests | All `Result={Success}`, **zero failures**, `**** TEST COMPLETE. EXIT CODE: 0 ****` at 2026-09-07 01:24:44 UTC |
| Headless boot | `/Script/TerrainCore.TerrainService` resolves as a UClass; `Lvl_ThirdPerson` loads; `VoxelWorld_T101A` present; PlayerStart at (-8228.66, 0, 150) — identical to CP-006. 65 actors. Probe exit 0 |
| `#include` boundary probe | With `#include "VoxelTools/VoxelDataTools.h"` in `Source/TerrainCore/Private/TerrainCore.cpp`: `fatal error C1083: Cannot open include file` / `Result: Failed`. Removed and rebuilt: `Result: Succeeded`. **D-011 remains compiler-enforced on 5.8.** File restored byte-identical to HEAD (md5 verified) |
| Standalone (`Tools/Play-Solo.ps1`) | `World NetMode = Standalone`; `LogVoxel: Voxel Invoker enabled; Name: VoxelInvokerAutoCameraComponent_0`; `No Voxel Invoker found, using camera as invoker`. **Zero `LogVoxel: Error` / fatal lines.** The multiplayer-invoker failure line (R-010) did not appear |
| LMB/RMB dig by hand | **Director-run; outstanding.** Not claimed here |

Evidence logs: `Saved/Logs/VoxelWorld.log` (build/tests/boot) and
`Saved/Logs/Standalone_T101A.log`, both local and gitignored, overwritten by later runs.

**Three findings, all fixed in this increment — none of them were in the plan:**

1. **The 5.8 plugin archive has no wrapping folder.** `VoxelFree.uplugin` sits at the
   archive root, so `$SourceRoot` equalled `$Staging` and the installer's progress
   report threw `Substring: startIndex cannot be larger than length of string` *after*
   a successful 1.56 GB download. Guarded; it now prints `<archive root>`.
2. **`-Force` left the old install inside `Plugins\`.** UnrealBuildTool scans that tree
   recursively, so every module's `Build.cs` was seen twice and the build died with
   `CS0101 ... already contains a definition for 'Voxel'` before compiling anything.
   Backups now go to `Tools\downloads\VoxelFree.bak-<timestamp>-engine<ver>`, outside
   the scanned tree and gitignored. The 5.7/432 install is preserved there as rollback.
3. **Both `Target.cs` files pinned `BuildSettingsVersion.V6` / `Unreal5_7`.** On 5.8 UBT
   refuses this: *"VoxelWorldEditor modifies the values of properties: [
   UnreachableCodeWarningLevel, ReturnTypeWarningLevel, DanglingWarningLevel ]. This is
   not allowed, as VoxelWorldEditor has build products in common with UnrealEditor."*
   Bumped to **V7 / `Unreal5_8`**, the engine's own suggested fix. The alternative,
   `TargetBuildEnvironment.Unique`, would require a source-built engine. **No
   `Source/**` code change was needed** for the new include order — the targets are the
   only source-tree edit, and TerrainCore compiled unmodified.

Also of note: `Install-VoxelFreeLegacy.ps1` is now parametrised `-EngineVersion`
(default `5.8`; `5.7` still reachable), with a per-version known-good fallback table
and README discovery driven by the same value. `Docs/SETUP.md` is the environment build
*record*, so its 5.7 walkthrough is left standing and a header note routes a new machine
to 5.8 — rewriting it would falsify the record.

### T-112.5a — Director hand check: PASS

Director ran the standalone dig path on UE 5.8 (2026-09-06) and reported it worked:
**LMB digs and RMB places.** This is the CP-006 by-hand check, reproduced on 5.8
through the unchanged `BP_ThirdPersonCharacter` wiring. The T-101A input bindings and
the `AddSphere`/`RemoveSphere` path therefore survive both the engine bump and the
plugin bump 432 -> 434.

**"No hill visible" is expected, pre-existing, and not a 5.8 regression.**
`T-101A_FINDINGS.md` §2e already records that `AVoxelWorld::SaveObject` defaults to
null, so voxel edits never reach disk and any process loading the level from disk gets
a bare `VoxelFlatGenerator` plane. §2e's own evidence line is
`took 0.251192s to generate ... no hill` under 5.7; this run logged
`took 0.130172s to generate` and the same flat plane. `place_voxel_world.py` says the
same thing in its comments. The hill exists only inside an editor session that has run
that script. Digging was verified on the flat plane, which is what CP-006 verified too.

**Cold-launch caveat worth carrying.** The first 5.8 standalone launch dropped the
Director through the floor into an endless fall. The log shows why: 150 PSO creation
hitches and six Path Tracing RTPSO compiles of 18-55 seconds each, at spawn, on a cold
DDC. The character spawns at Z+150 above the plane, so a multi-second stall before the
voxel collision mesh exists means an unopposed fall with no floor below. The relaunch
on a warm cache generated the world in 0.130s instead of 2.804s, with no hitch storm,
and played correctly. Not a terrain defect and not caused by the upgrade, but it
compounds the R-010/T-101B "no KillZ, no respawn volume" gap already on record: today
the only recovery from a fall is to quit. Worth a KillZ before anyone plays for real.

## T-112.5b — Unreal MCP adopted, editor-only

Both plugins are engine plugins shipped with 5.8 at
`Engine/Plugins/Experimental/ModelContextProtocol` and
`Engine/Plugins/Experimental/Toolsets/AllToolsets`. Both are marked
`IsExperimentalVersion: true` and `EnabledByDefault: false` by Epic.

**The D-025 guard is load-bearing, not decorative.** `ModelContextProtocol.uplugin`
declares **Runtime** modules (`ModelContextProtocol`, `ModelContextProtocolEngine`)
alongside its Editor ones. Nothing about the plugin is inherently editor-only, so the
`"TargetAllowList": ["Editor"]` on both entries in `VoxelWorld.uproject` is the only
thing keeping them out of a game target. AGENTS §9 now says so explicitly.

| Check | Result |
|---|---|
| `VoxelWorldEditor Win64 Development` with plugins enabled | `Result: Succeeded`, exit 0 |
| `VoxelWorld Win64 Development` (game target) | `Result: Succeeded`, exit 0, 73.1s |
| Guard proven from the build receipts | `VoxelWorldEditor.target` `BuildPlugins` (278) contains `ModelContextProtocol`, `AllToolsets`, `ToolsetRegistry` and 22 toolsets, with 70 matching build products. `VoxelWorld.target` `BuildPlugins` (234) contains **none of them** and **0** matching build products |
| Five TerrainCore tests with MCP enabled | All `Result={Success}`, zero failures, `**** TEST COMPLETE. EXIT CODE: 0 ****` at 2026-09-07 02:18:39 UTC, process exit 0 |
| `grep -ri "ModelContextProtocol\|StartServer\|AllToolsets" Source/` | **no hits** |
| `.mcp.json` generated | `LogModelContextProtocol: Display: MCP client configuration written to: .../Dev/VoxelWorld/.mcp.json`. 114 bytes: one `unreal-mcp` http entry at `http://127.0.0.1:8000/mcp`. No token, no credential — safe to commit under AGENTS §8 |
| MCP server live | Started with `ModelContextProtocol.StartServer`; port 8000 listening; a JSON-RPC `initialize` POST returned **HTTP 200** with `protocolVersion 2025-06-18` and a `tools.listChanged` capability. 52 toolsets registered as discoverable |

**Auto Start Server is left OFF.** The server is started deliberately with
`ModelContextProtocol.StartServer`. It binds loopback with **no authentication**, so an
always-on server in an editor that is often merely open is not a default worth having.
The Director can overrule this in Editor Preferences > General > Model Context Protocol.

**Console-command gotcha for whoever automates this next.** `-ExecCmds` splits on
commas, not semicolons, for this command: `-ExecCmds=... ClaudeCode; Quit` passes the
client name as `ClaudeCode;` and the plugin answers
`Unknown client "ClaudeCode;". Supported: ClaudeCode, Cursor, VSCode, Gemini, Codex, All`.
With a comma the config generates, but the editor then never processes `Quit` and idles
until killed. `Automation RunTests TerrainCore; Quit` still exits cleanly with the
semicolon. Use the semicolon for automation runs and the comma for the config
generation, and expect to kill the process after the latter.

**Limits.** Unreal MCP is Epic-Experimental; its APIs and formats may change, which is a
RISKS line against R-008 rather than a blocker for a dev-time tool. No gameplay, terrain,
architecture or numbered decision changed. UE 5.8's Mesh Terrain was not evaluated; it
stays a D-025 watch item with no evidence either way.

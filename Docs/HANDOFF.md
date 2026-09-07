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

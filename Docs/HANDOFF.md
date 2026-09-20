→ No action. For your reading only.

# HANDOFF.md - T-101B step 4 persistence proposal and review

## Identity, authority and Git state

- Updated **2026-09-20**. Outgoing **Codex**, implementation author and P-003 proposal
  author; independent P-003 reviewer **Claude Opus**. Incoming **either**.
  One active Implementer per workspace (D-028).
- Director's latest request: **"commit, push, write the handoff"**. This wrap-up is
  **R0 documentation/Git work**, limited to this handoff. No checkpoint was requested;
  STATE, BACKLOG, DECISIONS and RISKS remain checkpoint records at **CP-013**.
- Verified base: **`4c8c4269d30a026856735e90dfd7908f3be310f9`**, branch `main`.
  Incoming worktree was clean; `git pull --ff-only` reported already up to date.
  This handoff belongs to its enclosing documentation commit. Verify that commit and
  worktree on receipt; no other uncommitted work was present during preparation.
- Active continuation: **T-101B build step 4 prerequisite design, P-003**, **R3**.
  The proposal is **not adopted**; DEF-1/2/9 remain open. No persistence implementation
  or new dependency has been introduced. Step 3 implementation/data-convergence work
  is committed; the full T-101B adoption gate is still open.
- No implementation, test or reviewer process remains running. Authentication is resolved;
  there is no pending Director action for sign-in. No technical ruling is requested by
  this handoff. Follow the existing D-023/D-032 authority and review boundaries.

## Saved work and current blocker

| Commit | Saved result |
|---|---|
| `52ccee9` | P-002 representable box partitions and step-3 contracts |
| `21e3a2c` | Step-3 authoritative edit replication and validation evidence |
| `9a8dd7b` | Reconciled older lifetime/fairness text with adopted step-3 contracts |
| `b1b93e4` | P-003 persistence commit/recovery proposal draft |
| `4c8c426` | Independent P-003 review and authentication resolution |

All five commits were pushed to `origin/main`. The runtime base remains `21e3a2c`.
The evidence and file map below describe that runtime increment, not new work in this wrap-up.

Read [P-003](proposals/P-003-persistence-commit-and-recovery.md) and its
[independent review](reviews/P-003-review-claude.md) before continuing.

- Proposal: durable journal commit authority, idempotent SQLite settlement, complete global
  checkpoint manifests with reusable chunk payloads, two roots, retained recovery tails,
  exact base identity and coherent offline migration. The existing 58-byte operation stays
  unchanged; the persistence envelope's exact byte layout and storage packet are still needed.
- Author audit forbids snapshotting provisional mutations during storage-fault teardown;
  otherwise shutdown could publish RAM ahead of the durable journal. The completed review
  considered this safeguard and agreed with it.
- **Independent verdict: not ready for adoption, B1-B6.** Reconcile the actual counterexamples
  before revising the proposal; reviewer findings are not automatically adopted requirements:
  1. Durable request/retry identity across reconnect or restart, beyond connection-local IDs.
  2. Separate terrain recovery and settlement cursors, avoiding replay over baked terrain.
  3. Coherent entity-store restore and retention rules for older database backups.
  4. Bounded manifest size and checkpoint capture cost as edited history grows.
  5. Explicit replacement/amendment of ARCHITECTURE section 4.7's schema and world identity.
  6. Broadcast ordering and the cost of two durability barriers against the 8 ms target.
- Additional review ambiguities: game-thread capture versus I/O/flush work; generation of
  pristine chunks from the recorded base during replay; failed settlement capacity or deleted
  inventory handling (DEF-6). Review polish includes fixed-size root files, validating both
  roots before deletion, and manifest count/byte limits. Read the review for exact severity.
- This is an independent **P-003 proposal review**. It does not supply an independent review
  of P-002 or the step-3 code. No such code review or Director interactive acceptance is claimed.

## Review execution and platform evidence

- The initial installed Claude CLI attempt failed with HTTP 401 because an inherited
  `ANTHROPIC_API_KEY` overrode the valid Claude subscription login. A wrapper console-encoding
  failure followed; neither changed source. Initial evidence: `Saved/P003-review-output.txt`.
- Removing `ANTHROPIC_API_KEY` **only from the reviewer child-process environment** produced
  `AUTH_OK`, exit 0, using the existing subscription. No machine/user setting or credential
  changed. Do not ask the Director to sign in again for this resolved failure.
- Independent review then completed, exit 0, using installed
  `C:/Users/harja/.local/bin/claude.exe`, `-p --model opus --tools Read,Glob,Grep
  --allowedTools Read,Glob,Grep --strict-mcp-config --no-session-persistence --output-format text`.
  The prompt required read-only independent review; the reviewer ran no tests and wrote no
  files. Its response was saved verbatim to the tracked review file. Raw local output:
  `Saved/P003-review-output-authenticated.txt`. Review base: `b1b93e4`.
- Installed UE 5.8 `WindowsPlatformFile.cpp:933` implements `FFileHandleWindows::Flush`
  with `FlushFileBuffers`; `OpenWrite` at line 1638 returns that handle. This verifies the
  file-content primitive only. Namespace publication, crash recovery and power-loss behavior
  still need verification; no fault-injection test has run.

## Completed runtime increment - step 3

Make multiplayer terrain edits flow from an owning connection through server validation
and a serialized queue, then replay consistently on relevant clients.

All placements are beneath **`C:/Dev/VoxelWorld/`**. Complete files are committed in `21e3a2c`.

| Files | Behavior |
|---|---|
| `Source/TerrainCore/Public/TerrainEdit.h` | Reflected request/rejection/receipt types extracted from service header; request IDs, queued versus applied; no client source/material authority |
| `Source/TerrainCore/{Public,Private}/TerrainOpGeometry.*` | Canonical bounds/counts, bounded exact box subdivision under P-002, overflow checks; sphere never approximated by splitting |
| `Source/TerrainCore/{Public,Private}/TerrainEditQueue.*` | Global 256/per-source 16 operation slots; 64 cached receipts/source; monotonic IDs; token/charge reservation; commit revalidation; per-transaction round-robin; cancellation; queue/apply maxima |
| `Source/TerrainCore/{Public,Private}/TerrainStreamComponent.*` | Reliable owner-only PlayerController RPC transport, session check, pristine subscription acknowledgements, operation/revision envelope, explicit gap detection; development test RPCs |
| `Source/TerrainCore/Public/TerrainService.h`, `Private/TerrainService.cpp`, `Private/TerrainServiceReplication.cpp` | Backend lifetime, connection identity, quantization, reach/tool/permission/bounds/residency/clearance checks, commit revisions, distance subscriptions, client replay |
| `Source/VoxelWorld/TerrainInteractionLibrary.*` | Existing Blueprint nodes send intent through owning controller; true means queued; no client terrain prediction; legacy SourceId parameter ignored |
| `Source/TerrainBackendVPLegacy/Private/VPLegacyBackend.*` | Canonical W/B and complete-cell density semantics for spheres/boxes; preflight before mutation; exact changed chunks; plugin-owned data lock; thread guards |
| `Source/TerrainBackendVPLegacy/Private/VPLegacyDensityGenerator.h`, `Source/TerrainCore/Public/ITerrainBackend.h` | Immutable shared field lifetime retained by asynchronous generator instances |
| `Source/TerrainCore/{Public,Private}/MemoryTerrainBackend.*` | Full interface thread refusal; canonical write-set cap instead of changed-sample cap |
| `Source/TerrainCore/Private/Tests/BackendConformance.cpp` | Off-thread refusal and unchanged-state checks across the interface |
| `Source/TerrainCore/Private/Tests/TerrainServiceLifecycleTest.cpp` | Four states, teardown re-entry/order, unavailable queries, delayed immutable consumer release |
| `Source/TerrainCore/Private/Tests/TerrainAdmissionTest.cpp` | Dedup/eviction, fairness, queue caps, rate/charge reservation, revalidation, disconnect, cancel, split sequences |
| `Source/TerrainCore/Private/Tests/TerrainSplitTest.cpp` | Exact split geometry and whole/split Remove/Add/Paint hash equivalence on reference backend |
| `Source/TerrainCore/Private/Tests/TerrainReplayValidationTest.cpp` | Revision gaps/duplicates/malformed envelopes cannot mutate, resync quarantine, reach/permission/quantization refusal |
| `Source/TerrainCore/Private/Tests/TerrainAdapterChecks.cpp` | Standalone production density contract, 20 replay runs against four pinned hashes, idempotence, exact W, whole-cell occupancy, atomic refusal |
| `Source/TerrainCore/Private/TerrainMultiplayerTest.cpp`, `Tools/Test-TerrainMultiplayer.ps1` | Dedicated editor server + three real editing clients + optional distant observer; hash comparison; repeated server travel; PID-scoped cleanup |
| `Docs/ARCHITECTURE.md`, `Docs/proposals/P-002-box-split-representability.md` | Adopted correction, implementation determinations, honest current limits |

No `.Build.cs`, `.uproject`, config, `.uasset` or `.umap` changed. No dependency added.
The existing 58-byte operation wire and reference-backend golden values are unchanged.

## Decisions and review findings

- **P-002 adopted under explicit Director delegation:** split along longest eligible axis
  (X/Y/Z ties), nearest balanced even widths (lower-coordinate tie). The 42^3 example
  becomes 20x42x42 plus 22x42x42, exact union and no overlap. Cap below eight is impossible.
  Bounded splitting reserves every child before admission; spheres over cap reject.
- **AR-7:** service field ownership alone did not protect outstanding plugin generator
  instances. Shared immutable ownership survives service teardown until the last worker
  consumer releases it. No game-thread wait for meshing/collision, no plugin types leak.
- **Scheduling reconciliation:** preserve contiguous child OpSeq by selecting a transaction
  across frame pumps, round-robin between transactions. A split may therefore delay another
  source by several slices; the per-source 16-child bound prevents unbounded monopolization.
- **Prototype policy:** existing tool 0, no consumable, world-wide permission; no fabricated
  inventory/zone implementation. Queue accepts server-owned state and rechecks it. Server
  pawn position controls reach; Add checks all pawns' collision bounds. Removal readiness
  remains DEF-8/step 7.
- **Kernel correction:** stock plugin writes beyond canonical W (radius+2); clipping alone
  still leaves some wholly contained cells partially filled. Adapter clips W and enforces
  full empty/solid for those cells, retaining plugin ramp on boundary cells. A radius-four
  solid dig now touches **257**, not historical 895 samples. This is intentional compliance
  with adopted §4.10, not a fixture update to conceal an unexplained difference.
- **Release-build review fix:** geometry calculation and revision advancement were initially
  inside `check(...)`; those side effects disappear when checks compile out. They now execute
  unconditionally, with fatal integrity handling for impossible post-mutation violations.
  Both targets rebuilt and the real-network smoke rerun after this correction.
- **Test-only failure fixed:** new malformed-envelope fixture tried `Array.Add(Array[0])`,
  triggering UE's self-add assertion. Copy before adding. Final 17-test suite passes.
- **Harness review fix:** an intermediate round result used `continue` before exit/timeout
  checks. Removed so failed later rounds still time out. Final PowerShell AST parses cleanly.

## Verification - executed

UE **5.8.2**, Legacy **434**, Win64. Logs below are local/gitignored.

| Check | Result / evidence |
|---|---|
| Editor Development build | `Result: Succeeded`, exit 0; final rebuild after commit-path review at ~15:03 UTC |
| Game Development build | `Result: Succeeded`, exit 0; final rebuild at ~15:03 UTC |
| TerrainCore automation | **17 Success, zero failures**, automation/process exit 0, **2026-09-17 15:02:34 UTC**, `Saved/Logs/T101B-Validation-Final.log` |
| Production density regression | **PASS, 20 runs, zero failures**, pinned four hashes; `Saved/Logs/T101B-Adapter-Final.log`, normal exit 0 |
| Standalone service smoke | `Terrain.SelfTest: PASS`, radius-four dig 257 changed samples over eight chunks; same adapter log |
| 3x60-second MP + observer | **Three PASS rounds**, commits **426 / 428 / 426**, two nonseamless server travels, all three editing clients match four full server chunks each round; zero replay failures, observer zero received ops. `Saved/Logs/T101B-MP-20260917-105802/` |
| Final commit-path MP smoke | **PASS**, three editing clients + observer, 35 commits; `Saved/Logs/T101B-MP-20260917-110336/`, after final C++ change |
| D-011 boundary | Source scan: no plugin include in TerrainCore/gameplay; existing `VoxelWorld.cpp` includes its own game-module header. Module dependencies unchanged. No new include-poison compile probe claimed |
| D-025 guard | Fresh `Binaries/Win64/VoxelWorld.target`: **0 MCP plugins, 0 MCP build products**; no MCP/AllToolsets/StartServer source references; uproject Editor allowlists unchanged |
| Diff / script | `git diff --check` clean; PowerShell parser reports no errors; no generated assets/config changes |

The headless suite was run after replay/lifetime/golden-fixture changes. The later final
commit-path correction was verified by both builds and the real-network smoke; no repeated
unrelated suite is claimed. The three long MP rounds already used the final density kernel,
queue, replay and lifetime logic; the final edit moves mandatory work out of debug assertions.

### Exact commands and expected output

Run from `C:/Dev/VoxelWorld`:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' VoxelWorldEditor Win64 Development '-Project=C:\Dev\VoxelWorld\VoxelWorld.uproject' -WaitMutex
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' VoxelWorld Win64 Development '-Project=C:\Dev\VoxelWorld\VoxelWorld.uproject' -WaitMutex
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'C:\Dev\VoxelWorld\VoxelWorld.uproject' '-ExecCmds=Automation RunTests TerrainCore; Quit' -unattended -nopause -nosplash -nullrhi '-abslog=C:\Dev\VoxelWorld\Saved\Logs\T101B-Validation-Final.log'
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'C:\Dev\VoxelWorld\VoxelWorld.uproject' /Game/ThirdPerson/Lvl_ThirdPerson -game -nullrhi -unattended -nosplash -nosound '-ExecCmds=Terrain.AdapterChecks,Terrain.SelfTest' -seconds=15 '-abslog=C:\Dev\VoxelWorld\Saved\Logs\T101B-Adapter-Final.log'
.\Tools\Test-TerrainMultiplayer.ps1 -DurationSeconds 60 -IncludeObserver -Rounds 3
```

Expected: two successful builds, 17 successful automation cases and exit 0, standalone
`Terrain.SelfTest: PASS` and `Adapter.DensityContract: PASS runs=20 failures=0`, then
three `MP.Convergence: PASS clients=4 chunks=4` lines. The script removes only processes
it launches. Reruns overwrite the named standalone/automation logs; MP logs are timestamped.

## Limits and remaining work

1. **Full T-101B gate remains open.** Step 4 persistence/crash safety (DEF-1/2/9), step 5
   modified-chunk JIP/resync transfer (DEF-3), step 6 materials/yield/economy (DEF-6/K9),
   step 7 collision readiness/movement safety (DEF-8) are not implemented by this increment.
   Resolve each step's bound defects before starting its implementation.
2. **Density-only production evidence.** Materials remain zero in transfer/hash; Paint
   and yield remain unsupported. Full production `Backend.Conformance` and parallel-kernel
   `Adapter.Determinism` are not passed. The adapter's supported edit path is synchronous
   single-threaded on both peers; cross-platform/toolchain determinism is not proven.
3. **Data convergence is not movement safety.** Harness disables pawn movement during
   editing to isolate known R-010. NullRHI gives no visual/collision/interactive PIE claim.
   Logs retain existing proc-mesh network-reference warnings. Add burial prevention is
   implemented; removal-under-player and spawn-into-excavation still await step 7.
4. **Performance remains measured, not cleared.** Across the long rounds, maximum queue
   age was 47.582 / 49.337 / 47.867 ms; maximum backend apply 7.828 / 5.599 / **8.317 ms**.
   One small-brush call exceeded the 8 ms target. This was several processes on one machine
   with concurrent validation/build load, not proof of the at-cap or 16-32-player budget.
   Carry this evidence into the next checkpoint performance assessment; do not claim the
   performance gate passed. R-014 concerns cross-platform determinism, not performance.
5. Modified unknown chunks are not falsely subscribed; revision gaps quarantine replay
   and request the future resync path. Clients joining after terrain edits cannot reconstruct
   those modified chunks yet. This limitation is explicit, not a working JIP claim.
6. Session descriptor checks initial protocol/backend module/generator/seed/grid. Protocol 1
   names this initial canonical kernel/envelope. Future incompatible backend/kernel builds
   require compatibility version changes; no existing save or journal is introduced here.
7. Engine logs also contain baseline asset-manager/GameFeatures and editor-toolset Python
   initialization errors in `-game/-server` editor processes. No claim of an error-free engine
   log. Successful final runs have no terrain assertion/fatal or `LogVoxel: Error`.
8. Final script cleanup force-stops its owned processes after verification; the two actual
   server travels supply live teardown evidence. Abrupt-process cleanup itself does not prove
   graceful shutdown or persistence. Queue cancellation and delayed field lifetime have
   separate automated tests.

## Next safe actions

1. Read AGENTS and STATE/VISION/ARCHITECTURE/RISKS plus this handoff under OPERATIONS
   section 5.1. Verify the enclosing commit, branch, status and recent history. Sync only
   a clean tree with `git pull --ff-only`; preserve any later work.
2. Continue the **P-003 revision**, not save implementation. Read the actual review and
   validate each counterexample against the contracts/code, especially reconnect/retry
   semantics. Preserve already adopted P-002 and step-3 behavior.
3. Revise P-003 to resolve B1-B6 and the additional ambiguities, then obtain independent
   re-review under the existing R3 process. Update this rolling handoff after meaningful
   findings. Do not treat the first review as approval or silently adopt new requirements.
4. Only after the applicable R3 ruling and architecture update, plus a bounded exact-format
   and storage packet, begin step 4 implementation. Follow D-023/D-032 for delegated
   technical choices; escalate decisions outside that authority. DEF-1/2/9 remain open
   until actually resolved. Later JIP/material/collision increments remain separate.
5. On explicit `checkpoint`, reconcile checkpoint documents with the saved step-3 result,
   proposal/review status and outstanding limits. This commit/push/handoff request does
   not authorize claiming a new checkpoint or completing the full T-101B gate.

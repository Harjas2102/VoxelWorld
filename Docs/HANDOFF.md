→ No action. For your reading only.

# HANDOFF.md — T-101B step 4, P-003 revision 3

## Identity, authority and Git state

- Updated **2026-09-20**. Outgoing **Codex**, proposal author; independent reviewer
  **Claude Opus**. Incoming **either**. One active Implementer per workspace (D-028).
- Director: **Start the next step/"T-XXX" phase.** The next safe increment is
  **T-101B build step 4 prerequisite design, P-003**, R3. D-023/D-032 govern technical
  rulings; independent cross-vendor review is required. No runtime persistence code.
- Latest Director instruction: **"commit and push so that the other agent can continue."**
  This is an authorized documentation/Git wrap-up, not a new checkpoint.
- Received at **`b72a663`**, branch `main`, clean tree; `git pull --ff-only` was already
  up to date. That commit encloses the earlier handoff based on `4c8c426`. Runtime
  base remains `21e3a2c`. This handoff belongs to its enclosing documentation commit;
  verify that commit, remote synchronization and worktree state on receipt.
  STATE/BACKLOG/DECISIONS/RISKS remain CP-013 checkpoint records.
- Changed: P-003, ARCHITECTURE, this handoff and new independent reviews for
  revisions 2 and 3. No assets, runtime source, config or dependency changes.

## Current result and review boundary

- Read [P-003](proposals/P-003-persistence-commit-and-recovery.md) and the
  [revision-2 independent review](reviews/P-003-review-claude-r2.md). The original
  [revision-1 review](reviews/P-003-review-claude.md) is preserved unchanged.
- Revision 2 addressed all original B1-B6 according to the reviewer, who also corrected
  the earlier automatic-reconnect-RPC premise. Actual code has connection-local IDs,
  no application resend/outbox, and no incarnation token. Proposed protocol 2 fences
  stale sessions without inventing a durable reconnect retry service.
- Review R2-B1–R2-B6 then identified boot-validation ambiguity, global capture stalls,
  missing dirty residency pins, N=1 settlement throughput, entity-store rollback checks,
  and missing durable journal discovery. **Revision 3 closed all six in the
  independent reviewer's verdict: adoptable at architecture level, no blockers.**
- Current mechanism: explicit eager boot verification; immutable path-copied chunk index;
  copy-before-write capture at global G with transaction preflight and residency pins;
  two dirty banks bounded at 4,096 keys each (up to 1 GiB raw samples plus backend costs);
  N=32 unsettled records, SQLite batches up to 16; W at least the highest structurally
  valid retained checkpoint G; dual fixed journal anchors; preallocated-container
  fallback if durable new-name publication cannot be established.
- Added the author-found capture sentinel guard: memory ReadRegion returns successful
  default Empty when nonresident, while VPLegacy returns false. Neither is evidence of
  pristine equality. Capture requires residency, successful full samples and exact base
  comparison before emitting Empty. No raw live admission token is persisted.
- Read the [revision-3 review](reviews/P-003-review-claude-r3.md). Technical ruling
  under D-023/D-032 recorded in ARCHITECTURE and P-003: **adopt §§1–7 at architecture
  level**, incorporating requested throughput, fairness, read/write-footprint, offline
  repair and measurement clarifications. Replaced obsolete §4.7 schema-1 sketch;
  preserved/re-homed Dense transfer layout and amended ordering/retry/lifetime rules.
  No Director programming ruling is pending. No numbered decision/checkpoint changed.
- Exact bytes/storage module boundaries, OS durability proof, performance, material
  fidelity and integration remain future work. **DEF-1/2/9 and full T-101B remain open.**
  No runtime crash, save, inventory or production performance claim follows from docs.

## Evidence and processes for this increment

- Geometry bound audit enumerated **71,820** sorted even-width box triples within the
  existing 65,536-sample cap: worst-aligned 32-voxel chunk coverage **2,052**; the sphere
  read ceiling bounds coverage by **27**. Both fit the proposed single-bank capacity.
  This is arithmetic/resource evidence, not measured ReadRegion or checkpoint latency.
- `git diff --check` passes. Proposal local review links resolve. The r2 tracked review
  matches the CLI's complete saved response verbatim. Engine content-flush path was
  rechecked at `WindowsPlatformFile.cpp:933`; namespace/power-loss proof remains absent.
- R2 Claude CLI exited **0**, reviewed proposal SHA256
  `d2ad00de9396bfb82522688645e8bac6e6d4a216d7056c3e35cd6e65d0470d11`.
  Wrapper console printing then failed on cp1252 **after** the review file was saved;
  this did not invalidate the review. R3 wrapper prints ASCII status only.
- Focused R3 review **completed, exit 0**, via `Saved/review_p003_r3.py`. Reviewed SHA256:
  `acdeca2fd4f176f1ace53b244072d2809f495ef136bced38e4ed26cb75e31981`.
  Output/stderr: `Saved/P003-review-r3-{output,stderr}.txt`; tracked complete response:
  `Docs/reviews/P-003-review-claude-r3.md`. After review only adoption/status text and
  its requested nonblocking clarifications were added. Both reviews are preserved
  verbatim. **No reviewer or wrapper process remains running.**
- Reviewer: installed `C:/Users/harja/.local/bin/claude.exe`, Opus, Read/Glob/Grep only,
  strict empty MCP, no session persistence. Existing subscription works when inherited
  ANTHROPIC_API_KEY is omitted **only from the child environment**. No machine/user
  authentication setting changed and no sign-in request is pending.
- No UE/build/test process launched in this documentation increment. Historical runtime
  evidence below is from the committed step-3 increment, not newly rerun tests.

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

1. Read the adopted P-003 and independent revision-3 review plus ARCHITECTURE §4.7.
   Verify this handoff's enclosing commit, branch, recent history and worktree state.
   On a clean tree use `git pull --ff-only`; preserve any later work. The five-file
   documentation increment follows `b72a663`; no runtime change or checkpoint occurred.
2. Next bounded increment: **exact format/storage specification packet**, independently
   reviewed before codecs. Fix byte tables, counts/caps, checksums, protocol-2 receipt
   fields, storage/module ownership, journal discovery/rotation, namespace mode and
   offline repair. Reuse the adopted design; do not restart the architectural debate.
3. Keep DEF-1/2/9 open until their named evidence exists. Benchmark bulk ReadRegion,
   capture throughput/fence delay, settlement drain/commit rate, pinned RSS, eager boot
   cost and durable storage primitives before claiming production readiness. Materials,
   real economy/DEF-6, client JIP and collision remain separately gated.
4. On explicit checkpoint, reconcile checkpoint records with step 3 and persistence
   design status. This commit/push request does not authorize a new checkpoint or
   claiming that persistence implementation and DEF-1/2/9 validation are complete.

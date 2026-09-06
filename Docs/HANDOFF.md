# HANDOFF.md — Current agent handoff

> Rolling work log and formal handoff for the same agent or a different agent.
> Follow `OPERATIONS.md` §5.1. STATE owns checkpoint/task status; ARCHITECTURE and
> DECISIONS own approved choices. This file records actionable context and links.

## Identity and status

- **Updated:** 2026-09-06, CP-009 documentation wrap-up.
- **Outgoing:** Astra (Codex), Implementer. **Incoming:** whoever is available;
  Claude is expected for T-112.3 (D-028), not required.
- **Last completed code task:** T-112.2, committed and pushed as `306348a` (CP-008).
- **Current task:** README/workflow refresh complete, documentation only. This handoff
  is included in the enclosing CP-009 documentation commit; confirm that commit's
  push from git. No work remains in this task beyond saving the enclosing commit.
- **Next code task:** T-112.3, not started. No leftover code edits or active test process.
- **Authorization:** Director requested docs refresh, alternating agents, breadcrumbs,
  formal handoff and automatic commit/push. This is not approval to start T-112.3 now.
- **Risk/scope:** R0 documentation execution; approved changes are README and active
  governance/handoff docs. No source, assets, engine settings or dependency changes.

## Accomplishments and decision breadcrumbs

| Task/result | Reason or evidence | Durable source |
|---|---|---|
| T-112.2: eleven-method backend, density declaration, memory backend, reusable conformance and point tests | Extends T-112.1 through game-owned interfaces; no plugin/world or dependency change | STATE CP-008; source commit `306348a` |
| Single `Sample(FIntVector)` returning density/material | Packet conflicted with former §4.6 Density/Material/Version; Director explicitly delegated resolution | D-027; ARCHITECTURE CP-008 header / §4.6 |
| Data plus interest required for residency | Null field must not fabricate air; explicit Dense writes supply data, interest gates access | ARCHITECTURE CP-008 header; MemoryTerrainBackend.h |
| Position-sensitive hash and immediate point queries | Density-only and material-only swaps must change hash; distinct corner fixtures catch index errors | BackendConformance.cpp |
| Full edit-result assertions limited to Server | §4.3 allows Client results to be ignored; client terrain is still checked | BackendConformance.cpp, RunEdits |
| README/workflow refresh | Skimmed all 26 tracked project Markdown files; active docs still assumed fixed vendor ownership and lacked a live handoff | D-028; OPERATIONS §5.1; README |

**Useful failed approach:** the first compile used a global `Count` constant; MSVC
reported C4459 from UE container templates. Renamed to specific memory/conformance
constants. Do not repeat the generic global name. Build/tests then passed.

## Verification and limits

- Last code build: supplied UE 5.7 `VoxelWorldEditor Win64 Development` command →
  `Result: Succeeded`. See README for exact commands.
- Last automation: 2026-09-06 16:53 UTC; `TerrainCore.Backend.Conformance`,
  `TerrainCore.Op.Codec.RoundTrip`, `TerrainCore.Op.Quantisation.Stable`,
  `TerrainCore.Query.Point` all succeeded; 0 failures; automation and process exit 0.
- Log: `Saved/Logs/VoxelWorld.log` (local/gitignored, may be overwritten; summarized
  evidence is preserved in STATE CP-008). CP-009 is docs-only; no new code-test claim.
- `TerrainCore.Build.cs` was unchanged against the prior commit, diff exit 0.
- CP-009 verification: documentation whitespace check clean; all 14 relative links
  in updated docs resolve, including the OPERATIONS §5.1 target; source/assets/config
  diff exit 0. Code tests were not rerun for this docs-only change.
- Memory kernels/accounting are reference choices. Flatten/Smooth unsupported;
  SparseDiff/Empty restoration deferred. No field implementers before T-108.
- No production adapter, persistence, multiplayer or PIE result is implied by these
  tests. DEF-5/DEF-6 and the two gameplay drift flags remain open.

## Next safe steps — T-112.3

1. Check worktree/branch and sync a clean tree with `git pull --ff-only`. Read AGENTS,
   STATE, VISION, ARCHITECTURE, RISKS, relevant DECISIONS, then this handoff. Recite the
   current checkpoint/task. Treat stale worktree or doc conflicts explicitly.
2. Inspect `TerrainTypes.h`, `TerrainService.h/.cpp`, backend interfaces and the
   existing tests. Read ARCHITECTURE AR-4, §4.4, §6.1 and §9. Confirm scope with the
   Director before writing code; `resume` alone still means read, recite and wait.
3. Propose the bounded T-112.3 file/API plan and classify it under AGENTS §3. The
   revision-index API and exact service-skeleton surface have not been specified by
   this handoff. If those require a localized design, use R2 approval; route unresolved
   architectural choices to the Architect. D-027 was bounded to T-112.2.
4. Implement only the approved in-memory revision index/service skeleton. Revisions
   never decrease; a multi-chunk edit bumps each affected chunk exactly once, including
   duplicate-key input coverage. Keep authority in the service. No plugin dependency,
   save schema, replication protocol, compaction or engine upgrade in this increment.
5. Add `TerrainCore.Revision.Monotonic`, guarded like existing automation. Build and
   run the README commands: expect five tests green, 0 failures, exit 0. Verify
   Build.cs stays Core/CoreUObject/Engine and inspect the diff before commit.
6. Leave new breadcrumbs and finalize this handoff for either agent. Checkpoint and
   push when authorized; T-112.5 remains after T-112 completes, not part of T-112.3.

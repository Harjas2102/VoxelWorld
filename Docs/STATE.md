# STATE.md — Current Project State

> **Read this file + VISION.md at the start of every session** (Claude Code reads them
> from the repo; see `AGENTS.md` section 1). Updated at every checkpoint.

---

**Checkpoint:** CP-003 · **Date:** 2026-09-05
**Phase:** 1 — Terrain Feasibility

## What happened at CP-003

Same evening as CP-002, but the state moved enough to warrant its own number.

- **T-100 done.** Visual Studio Community 2022 **17.14.37614.0** with the "Game
  development with C++" workload, MSVC **14.44.35207**, Windows SDK **10.0.26100.0**.
  Verified with `vswhere -requires ...NativeGame` and by locating `cl.exe` — winget
  reports success when the bootstrapper hands off, minutes before the workload lands.
  **A Windows reboot is pending.**
- **D-019 ruled** — the repository stays public; the inert Android token stays in history.
- `CHAT_OPENER.md` rewritten vendor-neutral and made the single canonical opener;
  **T-006 rescoped** to the four editor skills T-101A actually needs (confirmed not
  started since June).
- **⛔ Major backend finding (R-008): Voxel Graphs are Pro-gated.** The first attempt at
  T-101A produced an empty world and a blue sky. Cause:
  `Voxel: Running Voxel Graphs require Voxel Plugin Pro`. All 106 example generators are
  unusable at runtime and **fail silently**. Free ships two runnable generators, both
  C++. → **T-108** (C++ `UVoxelGenerator`) is now a Phase 1 requirement blocking 1C.
  Details: `Docs/T-101A_FINDINGS.md` section 2b.
- **✅ The D-011 yield hook is live.** Sculpting the workaround hill exercised
  `AddSphere` and returned populated `FModifiedVoxelValue` arrays — 861,781 voxels across
  five spheres, counts scaling with edit volume. Reachable and working on real data;
  accuracy and determinism remain 1C's job. Section 2c.

## What happened at CP-002

External architecture review (GPT-5.6, two documents) plus a response review (Claude
Fable 5.1), **accepted in full by the Director**. Recorded as **D-010 … D-016**.
Governance became vendor-neutral (`AGENTS.md`); the roadmap was reordered around a
terrain feasibility gate. Both reviews are archived in `Docs/reviews/`.

Three June errors corrected: D-001 bundled the gameplay requirement with the vendor;
terrain multiplayer was deferred to Phase 3 despite D-002; and the "voxel terrain is
non-Nanite" assumption was stale. Plus: the 1 km² island was the wrong first target, and
`CLAUDE.md` as constitution was vendor lock.

## What exists right now

**In-engine:**

- UE 5.7 Third Person template project **VoxelWorld** (Blueprint, Desktop, Max quality,
  Starter Content OFF), shaders compiled, runs clean.
- 3-player PIE replication verified (Listen Server, movement replicates across all three
  windows).
- **Voxel Plugin Free Legacy is installed** at `Plugins/VoxelFree/` —
  **v432 / `e9648b302` / EngineVersion 5.7.0**, prebuilt Win64 binaries, obtained
  2026-07-06. The editor log confirms `Mounting Project plugin VoxelFree` and clean
  `LogVoxel` init. Project plugins auto-enable, so this worked without a `.uproject`
  entry. **Not committed** — `Plugins/VoxelFree/` is gitignored; reinstall via
  `Tools/Install-VoxelFreeLegacy.ps1`.
- **`Content/Maps/VoxelSandbox.umap`** exists with one `AVoxelWorld` actor, configured at
  CP-003 to 50 cm voxels / 1024 voxels (512 m) / `VoxelFlatGenerator`.
- Known non-fatal issue: an `ensure` on `AVoxelWorld::DestroyWorldInternal`
  (GeneratorCache) fires at editor shutdown. Cosmetic; logged, not chased.

**Repository:**

- Git + LFS repo, doc pack committed, pushed to
  **https://github.com/Harjas2102/VoxelWorld** — **visibility: PUBLIC**
  (verified 2026-09-05 via the GitHub API; CP-001 recorded "private" — that was the
  stale record, now corrected). **Ruled to stay public: D-019.** The repo contains no
  secrets after CP-002; the inert Android token left in history is covered by D-019.
- Governance: `AGENTS.md` (constitution) + `CLAUDE.md` (adapter) + `Docs/` (truth).

- `Content/Maps/VoxelSandbox.umap` currently holds the **failed-graph state** (empty
  world) and is intentionally **left uncommitted** — committing an empty world as the
  test map would mislead anyone restoring it. It gets committed after the re-run.

No gameplay systems yet. No authoritative terrain layer yet. No C++ module yet (the
toolchain is now ready for one — T-108).

## Current task

- **T-101A** *(in progress — tooling done, PIE test outstanding)*
  - ✅ Plugin verified, plugins enabled, editor launches clean, API surveyed.
  - ✅ `Tools/Editor/place_voxel_world.py` works: flat generator + a hill sculpted from
    five `AddSphere` calls, 512 m world at 50 cm voxels.
  - ⏳ **Next, in order:** reboot (pending from T-100) → re-run the placement script →
    Ctrl+S → wire the dig Blueprint (`T-101A_RUNBOOK.md` step 6) → PIE test → screenshots
    → fill in `T-101A_FINDINGS.md` sections 5 and 6.
- **T-108** *(new, queued)* — C++ `UVoxelGenerator` for the strata + ore-body test world.
  Blocks sub-step 1C. Propose before implementing (R2/R3 boundary).

**7-day rule (R-009):** if the first hole is not dug by **2026-09-12**, the plan is wrong,
not the Director — cut the step smaller.

## Definition of done for T-101A

1. Screenshot of a dug hole **and** an added mound in PIE.
2. A tunnel or an overhang — proof the representation is genuinely volumetric.
3. No errors in `Saved/Logs/VoxelWorld.log` beyond the known shutdown `ensure`.
4. Script and `.uproject` changes committed and pushed.
5. `Docs/T-101A_FINDINGS.md` written: plugin API notes, and what the plugin does and does
   not provide for materials, saves, and multiplayer.

Solo is acceptable **for 1A only**. Per **D-013**, no backend is accepted on solo
sculpting — concurrency, save/restart, and join-in-progress are T-101B pass criteria.

## Blockers

None.

## Open decisions

- **D-008** — working title
- **D-017** — terrain architecture v1 (from the blind benchmark,
  `Docs/DUAL_AGENT_SETUP.md` section 6)
- **D-018** — primary implementer for Phase 1B+ (same benchmark)
- Survival meter set — resolved by testing, not by convention (D-016)
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
| Git + LFS | ✅ git-lfs 3.7.1, 238 LFS files, push credentials verified |
| Claude Code | ✅ Installed, verified in-repo |
| **Voxel Plugin Free Legacy** | ✅ **v432 / e9648b302 / 5.7 — installed, mounts clean** (gitignored) |
| Voxel Plugin 2 | ❌ Paid, gated on owning Pro Legacy — upgrade candidate only (R-008) |
| Python Editor Script Plugin | ✅ Enabled at CP-002 (agent-driven editor scripting) |
| Editor Scripting Utilities | ✅ Enabled at CP-002 |
| Visual Studio 2022 (C++ workload) | ✅ 17.14.37614.0, MSVC 14.44.35207, Win SDK 10.0.26100 — **reboot pending** |

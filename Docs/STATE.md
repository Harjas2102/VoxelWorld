# STATE.md — Current Project State

> **Paste this file + VISION.md at the start of every Claude session**
> (or let Claude Code read them from the repo). Updated at every checkpoint.

---

**Checkpoint:** CP-001 · **Date:** 2026-06-12
**Phase:** 0 complete → entering Phase 1 (Voxel Island Sandbox)

## What exists right now

- UE 5.7 Third Person template project **VoxelWorld** (Blueprint, Desktop,
  Max quality, Starter Content OFF), shaders compiled, runs clean.
- 3-player PIE replication verified (Listen Server, movement replicates
  across all three windows).
- Git + LFS repository with the full doc pack committed, pushed to GitHub
  (private): **https://github.com/Harjas2102/VoxelWorld**
- Claude Code installed, opened in the repo, verified against the docs
  (read CLAUDE.md, summarized STATE/VISION correctly).

No gameplay systems yet. No voxel terrain yet.

## Current task

- **T-006** *(ongoing, parallel, 2–4 wks)* — Editor fluency drills:
  viewport navigation, Content Browser, placing/transforming actors,
  Blueprint editor, Epic's "first hour" learning path.
- **T-101** — Voxel Plugin free tier from **Fab**: install into the
  project, generate a small island, dig and add terrain in PIE.

## Definition of done for T-101

Director reports all of the following:
1. Voxel Plugin (free tier) acquired from Fab and enabled in the project
   (shows in Edit → Plugins, project restarts cleanly).
2. A voxel world actor in the level producing a small island of smooth
   (non-blocky) terrain.
3. In PIE: terrain can be **dug** (material removed) and **added**
   (material placed) at runtime.
4. Project still compiles/opens with no errors; changes committed and
   pushed.

(Multiplayer voxel-edit sync is **not** part of T-101 — single-player
sandbox first, per the backlog phasing.)

## Blockers

None.

## Open decisions

D-008 (working title) · survival meter set · T3 transport method ·
structural integrity model

## Toolchain status

| Tool | Status |
|---|---|
| UE 5.7 | ✅ Installed, shaders compiled |
| Git + LFS | ✅ Installed, repo live at github.com/Harjas2102/VoxelWorld |
| Claude Code | ✅ Installed, verified in-repo |
| Voxel Plugin (free) | Not yet acquired — **this is T-101** |
| Visual Studio 2022 (C++ workload) | Deferred until first C++ (~T-104) |

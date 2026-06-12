# BACKLOG.md — Ordered Task List

> Tasks are pulled top-down. New ideas are **appended** to their phase, never
> inserted, unless the Director explicitly reorders. Completed tasks move to
> the Done log at the bottom with their checkpoint number.

---

## Phase 0 — Environment (T-001…T-005 done, see Done log)

- **T-006** *(parallel, 2–4 wks, ongoing)* Editor fluency drills: viewport
  navigation, Content Browser, placing/transforming actors, opening Blueprint
  editor, Epic's official "first hour" learning path.

## Phase 1 — Voxel Island Sandbox (solo) (NOW)

- **T-101** Acquire Voxel Plugin free tier. Generate a small island. Dig and
  add terrain in PIE.
- **T-102** First gameplay: harvestable tree → wood enters an inventory
  counter (Blueprint, replicated).
- **T-103** Mineable rock/ore as voxel material removal — mining = terraforming.
- **T-104** Inventory system v1 (C++ base + Blueprint UI child).
- **T-105** Day/night cycle.
- **T-106** Save/load voxel deltas, single-player.
- **T-107** First-person camera rig (D-009): head-socket camera, FOV tuning,
  near-clip + owner-only head hiding, third-person toggle.

## Phase 2 — Build & Craft

- **T-201** Socket building set: foundation, wall, ceiling, doorframe, door.
- **T-202** Workbench + recipe/crafting system.
- **T-203** Campfire + first survival meter(s) *(blocked on meter decision)*.
- **T-204** Full world save/load (terrain + structures + inventories).

## Phase 3 — Multiplayer Proper

- **T-301** All Phase 1–2 systems verified in 3-player PIE.
- **T-302** Engine-from-source build; Linux dedicated server target compiles.
- **T-303** Server hosted on LAN box; port forwarding; friends direct-connect
  from outside the network.
- **T-304** Server-side persistence: world survives restarts.
- **T-305** 🏁 **Vertical slice milestone:** first real friends playtest —
  island, dig/build/harvest, campfire, day/night, 2–4 players.

## Phase 4 — Electricity (T2 tech)

- **T-401** Fuel generator (placeable, consumes fuel, produces power).
- **T-402** Cable network — server-side power graph sim (no per-machine tick).
- **T-403** First powered machines: light, electric furnace, auto-miner.
- **T-404** Machine/grid state persistence.

## Phase 5+ — Parked (do not start)

Biome expansion + World Partition · PvP zones (needs integrity model) ·
weather · T3 automation transport · 16–32 load testing · Year 2: hostile
creatures, taming, breeding · MCP editor-control evaluation (~M3+).

---

## Done

- **T-001** *(CP-001)* Install Epic Games Launcher + UE 5.7.x to NVMe.
- **T-002** *(CP-001)* Third Person template project created (Blueprint,
  Desktop, Max quality, Starter Content **OFF** — amended from ON to stay
  inside GitHub's free LFS quota). Shader compile survived.
- **T-003** *(CP-001)* Multiplayer PIE test: 3 players, Listen Server —
  movement replicated across all three windows.
- **T-004** *(CP-001)* Git + LFS init, doc pack committed, pushed to
  https://github.com/Harjas2102/VoxelWorld (private).
- **T-005** *(CP-001)* Claude Code installed, opened in repo, verified it
  reads CLAUDE.md and summarizes the docs correctly.

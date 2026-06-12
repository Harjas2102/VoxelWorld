# GDD.md — Game Design Document v0.1

> Living document. Changes freely as decisions land. Sections marked **TBD**
> are open — see BACKLOG.md and DECISIONS.md. VISION.md outranks this file.

**Updated:** 2026-06-12 (CP-000, rev A — added D-009 perspective)

---

## Core Loop

**Harvest → Craft → Build → Power → Automate → Expand** (→ Contest, once PvP zones exist)

Every system must feed this loop or it gets cut.

---

## Perspective & Camera (D-009)

- **True first person:** camera at the head socket of the full-body mesh —
  you see your own body, Rust/Tarkov feel. Head bone hidden for the owner
  only; everyone else always sees the full character.
- **Third-person toggle** (Ark-style) on a boom camera. Camera state is
  client-side only — no replication cost.
- Reticle-based look-at interaction (trace from camera + interact key).
- **Open:** whether PvP zones force first person (rule alongside D-004).

---

## World

- **One planet.** Launch world: ~1 km² temperate island (the vertical slice).
  Expands to a multi-biome continent later via World Partition.
- **Terrain:** smooth voxel (marching cubes), fully deformable — dig, raise,
  flatten, tunnel. Caves exist wherever players dig them. (D-001)
- **Biome candidates** (post-slice, final set TBD): temperate forest (start),
  alpine, desert, wetland, volcanic, coastal. Each biome = a resource identity
  + an environmental hazard. PCG (production-ready in UE 5.7) populates them.
- **Day/night cycle** from Phase 1. Weather: TBD.

## Terrain Manipulation

- Ops: sphere/box dig, add material, flatten, smooth. All server-validated,
  replicated as operations (never meshes). (D-002, D-006 patterns)
- Resource yield is tied to the material voxels removed — dirt, stone, and
  ore types are voxel materials. Mining IS terraforming.
- Tools scale with tech: hand tools → powered excavation in later tiers.

## Building

- **Socket-based structural pieces** (Rust-style): foundation, wall, ceiling,
  doorframe + door. Plus freeform-placed props (storage, stations, machines).
- Structures are permanent and persist with the world.
- Structural integrity / decay model: **TBD** — must be decided before PvP
  zones ship.

## Tech Progression

| Tier | Name | Contents |
|---|---|---|
| T0 | Primitive | Hand tools, campfire, basic shelter, hunting |
| T1 | Stations | Workbench, furnace, storage, better tools |
| T2 | Electricity | Fuel generator, cables, lights, electric furnace, auto-miner |
| T3 | Automation | Item transport (conveyor vs pipe vs drone: **TBD**), automated crafting |
| T4+ | Year 2 | Advanced materials, logic/circuits, creature integration |

## Multiplayer

- Self-hosted **dedicated Linux server**, 16–32 players, direct IP connect
  for friends. No matchmaking, no platform services. (D-002, D-003)
- Server-authoritative everything. Distance-based net relevancy on all
  replicated actors.
- **Persistence:** voxel chunk-delta files + SQLite (players, inventories,
  machine/grid state). Flat, inspectable formats.
- Join-in-progress: new client receives compressed modified-chunk deltas.

## Combat & Survival

- **PvE first.** Survival meters: **TBD** (hunger/thirst/temperature — pick
  the set before Phase 2). Environmental hazards per biome. Wildlife threats
  arrive with creatures.
- **PvP zones** (~Month 12+): server-side zone volumes flip damage and build
  permissions. Richest resources live in contested zones; home biomes stay
  protected. (D-004)

## Creatures

- **Year 1:** passive, huntable wildlife (deer-class land animal, fish) on
  simple behavior trees. Feeds the survival loop cheaply.
- **Year 2:** hostile creatures, taming, breeding — the Ark layer. (D-005)

## Art Direction

- Realistic. Fab/Megascans assets, Lumen GI, Nanite for static meshes,
  Nanite Foliage (experimental in 5.7 — evaluate), FSR upscaling.
- Voxel terrain itself is non-Nanite (engine constraint) — terrain material
  quality carries the look. Budget effort there.
- Placeholder-quality is acceptable everywhere until systems are proven.

---

## Top Open Questions

1. Survival meter set (hunger / thirst / temperature?)
2. T3 item transport method (conveyors, pipes, drones?)
3. Structural integrity & decay model
4. Working title (D-008)
5. Do PvP zones force first person? (corner-peek balance — rule with D-004)

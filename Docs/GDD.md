# GDD.md — Game Design Document v0.2

> Living document. Changes freely as decisions land. Sections marked **TBD** are open —
> see BACKLOG.md and DECISIONS.md. VISION.md outranks this file.

**Updated:** 2026-09-05 (CP-002 — architecture review adopted, D-010…D-016)

---

## Core Loop

**Harvest → Craft → Build → Power → Automate → Expand** (→ Contest, once PvP zones exist)

Every system must feed this loop or it gets cut.

---

## Perspective & Camera (D-009)

- **True first person:** camera at the head socket of the full-body mesh — you see your
  own body, Rust/Tarkov feel. Head bone hidden for the owner only; everyone else always
  sees the full character.
- **Third-person toggle** (Ark-style) on a boom camera. Camera state is client-side only
  — no replication cost.
- Reticle-based look-at interaction (trace from camera + interact key).
- **Open:** whether PvP zones force first person (rule alongside D-004).

---

## World

- **World authoring: procedural foundation + authored geography + player-created
  history.** Procedural systems generate the geology foundation, biome masks, erosion-like
  shapes, ore distribution, and base terrain. Landmarks — valleys, ridges, rivers, passes,
  high-value regions, biome transitions — are then authored deliberately. Players supply
  the third layer, and it is the one that matters.
- **First test world: a 256–512 m hill** with several material strata, an underground ore
  body, a forest area, water or lowland, and an exposed cliff — plus enough vertical depth
  for a real tunnel and space for a small base. **Not** a 1 km² island. If one hill cannot
  survive the finished game's requirements, a continent does not matter.
- **One planet.** The launch world expands to a multi-biome continent later via World
  Partition.
- **Terrain:** smooth volumetric, fully deformable — dig, raise, flatten, tunnel. Caves
  exist wherever players dig them. Backend is **Voxel Plugin Free Legacy, provisional**
  until the T-101B gate rules (D-010).
- **Biome candidates** (post-slice, final set TBD): temperate forest (start), alpine,
  desert, wetland, volcanic, coastal. Each biome = a resource identity + an environmental
  hazard. PCG populates them.
- **Day/night cycle** from Phase 5. Weather: TBD.

## Terrain Manipulation

- Ops: sphere/box dig, add material, flatten, smooth. All server-validated, replicated as
  operations, never meshes. (D-002, D-011)
- **Resource yield is simulation-owned** (D-011). The server knows material occupancy
  before and after an edit and computes yield from the volume actually removed — soil,
  stone, ore-bearing material — with tool efficiency and a recovery factor applied.
  Mining IS terraforming. It is never "raycast a rock node, add ore," and the rendered
  mesh is never asked what disappeared.
- This is what later enables drill efficiency, excavator bucket capacity, ore grade,
  processing efficiency, waste rock, and prospecting.
- Tools scale with tech: hand tools → powered excavation in later stages.

## Building

- **Socket-based structural pieces** (Rust-style): foundation, wall, ceiling, doorframe +
  door. Plus freeform-placed props (storage, stations, machines). Conventional meshes, not
  voxels (D-015).
- Structures are permanent and persist with the world, stored as data/entities.
- Structural integrity / decay model: **TBD** — must be decided before PvP zones ship.
- **Terrain-under-structure policy: TBD** — decide it, do not simulate it by accident.
  First version is either "edits blocked beneath critical structure bounds" or "the
  building floats until a support system exists."

## Technology Progression — increasing scale of environmental control (D-016)

Progression is not a generic tech tree. It is the story of how large a piece of the world
the player can change.

| Stage | Name | The player can | The world shows |
|---|---|---|---|
| **0** | Human labour | Hand digging, chopping, carrying, campfire, crude shelter, hand mining, simple storage | The landscape dominates the player |
| **1** | Organized workshop | Furnace, workbench, carts and containers, better tools, stronger building, paths, prospecting | Cleared ground, a worked site |
| **2** | Powered control | Fuel generator, cables, lights, pumps, electric furnace, power tools, small drill, powered saw | Lit, dry, worked terrain |
| **3** | Industrial logistics | Conveyors/pipes/vehicles, automated material handling, powered quarry, processing lines, grid management | A working pit, spoil heaps, roads |
| **4** | Landscape-scale infrastructure | Excavator, bulldozer, industrial drill, large quarry, rail, drainage, substations, automated mining | The valley itself is reshaped |

The defining arc: **the hole that took ten minutes with pickaxes early in the server can
later be widened by powered equipment in seconds.** The world reveals technological
progress.

Against Satisfactory, this is the distinguishing proposition:

> The factory does not merely grow on top of the map. **The map physically records the
> factory's growth.**

## Multiplayer

- Self-hosted **dedicated Linux server**, 16–32 players, direct IP connect for friends.
  No matchmaking, no platform services. (D-002, D-003)
- Server-authoritative everything. Distance-based net relevancy on all replicated actors.
- **Persistence (D-012):** deterministic base world + per-modified-chunk snapshots +
  append-only operation journal + compaction; SQLite for players, inventories, structures,
  machines, and grids. Flat, inspectable, versioned formats with migration paths.
- **Join-in-progress:** snapshot at revision R plus the operations since. A joining client
  never needs the server's lifetime history.

## Combat & Survival (D-016)

- **PvE first.** Survival pressure comes from **environment mastery**, not bar
  maintenance: temperature, weather exposure, darkness, injury, biome hazards, carry
  capacity, shelter, stamina.
- The intended relationship: *environment threatens the player → the player builds shelter
  and tools → technology reduces the threat → infrastructure makes the region safer → the
  player expands into a harsher region.*
- Food exists, but it does not become a repetitive tax. "Every ten minutes, open inventory,
  eat item, continue" is explicitly not the goal **unless testing proves that loop is
  fun**.
- Survival meter set: **open, resolved by testing** — not by genre convention.
- **PvP zones** (~Month 12+): server-side zone volumes flip damage and build permissions.
  Richest resources live in contested zones; home biomes stay protected. (D-004)

## Creatures

- **Year 1:** passive, huntable wildlife (deer-class land animal, fish) on simple behavior
  trees. Feeds the survival loop cheaply.
- **Year 2:** hostile creatures, taming, breeding — the Ark layer. (D-005)

## Art Direction

- Realistic. Fab/Megascans assets, Lumen GI, Nanite for static meshes, Nanite Foliage
  (experimental in 5.7 — evaluate), FSR upscaling.
- **Terrain rendering:** Voxel Plugin 2 supports runtime Nanite and Lumen for terrain;
  **Voxel Plugin Free Legacy — the current provisional backend — does not.** Until a VP2
  upgrade is justified (D-010, R-008), **terrain material quality carries the look**.
  Budget effort there.
- **Only terrain is volumetric** (D-015). Buildings, machines, props, trees, rocks, and
  foliage are conventional meshes and PCG.
- Placeholder-quality is acceptable everywhere until systems are proven.

---

## Top Open Questions

1. Survival meter set — resolved by testing, not by convention (D-016)
2. Stage 3 item transport method (conveyors, pipes, vehicles, drones?)
3. Structural integrity & decay model
4. Terrain-under-structure policy (block edits vs. allow floating)
5. Working title (D-008)
6. Do PvP zones force first person? (corner-peek balance — rule with D-004)
7. Terrain architecture v1 and primary implementer (D-017 / D-018, from the blind
   benchmark)

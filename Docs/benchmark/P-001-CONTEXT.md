# P-001-CONTEXT.md — Blind Benchmark Context Packet

> **What this file is.** One self-contained context packet for the **blind architecture
> benchmark** defined in `Docs/DUAL_AGENT_SETUP.md` section 6. It carries the challenge
> and every project document an architect needs to answer it, so that two vendors can be
> given **demonstrably identical context** without a connector, a repo clone, or a
> file-upload step.
>
> **Assembled:** 2026-09-06, at checkpoint **CP-004**, immediately after T-101A passed.
> **Assembled by:** Claude Code (Implementer role) at the Director's instruction.
> Assembling this packet is a mechanical task; nothing in it is a proposal.

---

## 0. How to use this packet

**If you are an Architect responding to this packet:**

1. Read all of it before writing anything. Sections 2–9 are the project constitution and
   current state; section 10 is the factual API surface of the backend you must design
   against.
2. Answer **only** the challenge in section 1. Save your answer as
   `Docs/proposals/P-001-terrain-<vendor>.md`.
3. **You will not be shown the other vendor's answer.** That is the point of the
   exercise. Do not ask for it, and do not speculate about it.
4. Where the evidence does not determine a choice, **say "unknown — prototype this"** and
   name the experiment that would resolve it. `AGENTS.md` section 10 makes stopping at
   ambiguity correct behaviour, and the scoring criteria in `DUAL_AGENT_SETUP.md`
   section 6 step 5 reward it explicitly.
5. `/Docs` outranks you. `VISION.md` outranks `GDD.md`. A numbered decision in
   `DECISIONS.md` is not yours to overturn — if you believe one is wrong, say so as a
   flagged recommendation for the Director, and design to the decision as written.

**Conventions inside this file.** Sections 2–9 reproduce repository documents
**verbatim**, each between explicit `BEGIN`/`END` markers. Their original Markdown
heading levels are preserved unchanged, so heading levels inside those blocks do not
nest under this file's numbering. Nothing in those blocks has been edited, summarised,
reordered, or abridged.

**Provenance.** Every document below is reproduced from the repository at commit
`b7d0375` on branch `main`, public at <https://github.com/Harjas2102/VoxelWorld>.
Section 10 is not a repository document — it was extracted for this packet from the
plugin source at `Plugins/VoxelFree/Source/` (which is gitignored and therefore *not*
readable from the public repo; that is the reason it is included here).

**Contents**

| § | Content | Source |
|---|---|---|
| 1 | The challenge | `Docs/DUAL_AGENT_SETUP.md` section 6 |
| 2 | Constitution | `AGENTS.md` (repo root) |
| 3 | Vision | `Docs/VISION.md` |
| 4 | Game design | `Docs/GDD.md` |
| 5 | Decision log | `Docs/DECISIONS.md` |
| 6 | Current state | `Docs/STATE.md` |
| 7 | Risk register | `Docs/RISKS.md` |
| 8 | Architecture v0 stub | `Docs/ARCHITECTURE.md` |
| 9 | Backend findings | `Docs/T-101A_FINDINGS.md` |
| 10 | Plugin API reference | extracted from `Plugins/VoxelFree/Source/` headers |

**Size.** Roughly 3,000 lines / 160 KB — on the order of 40,000 tokens. It is meant to
be pasted whole, in one message, at the start of a fresh chat. If a vendor UI refuses a
paste that large, attach it as a file rather than splitting it: a split paste is how the
two vendors stop having identical context.

---

## 1. The challenge

Reproduced from `Docs/DUAL_AGENT_SETUP.md` section 6, *"First joint task — the blind
benchmark (from the GPT handoff, §39)."*

### 1.1 The task as recorded

This both chooses the primary implementer *and* produces the architecture the
project needs next. Run it right after T-101A (once the plugin's real API has
been touched).

1. Identical context to both vendors: AGENTS, VISION, GDD, DECISIONS, STATE,
   RISKS, the two review files, and T-101A findings. **Do not show either the
   other's answer.**
2. Identical challenge to both:
   > Design a server-authoritative, persistent terrain-editing architecture
   > for the current UE 5.7 project using Voxel Plugin Free Legacy as the
   > provisional backend behind a replaceable adapter, supporting 16–32
   > players, join-in-progress, mining yield from removed material,
   > snapshot + operation persistence, chunk relevancy, concurrent edits,
   > future powered excavation, and backend replacement. Require: class/
   > subsystem boundaries, data structures, server/client sequence,
   > persistence schema, chunk revision model, concurrency, JIP, failure
   > recovery, tests, performance risks, what stays plugin-specific.
3. Save as `Docs/proposals/P-001-terrain-astra.md` and
   `Docs/proposals/P-001-terrain-claude.md`.
4. Swap: each reviews the other. Save both reviews in `Docs/reviews/`.
5. Score (GPT §39 criteria): correct reading of vision, real risks found,
   avoids unnecessary complexity, UE correctness, persistence/networking
   correctness, testability, replaceability, willingness to say "unknown —
   prototype this," survives critique.
6. Director rules: `rule D-017: terrain architecture = …` and
   `rule D-018: primary implementer = …`. ARCHITECTURE.md v1 written from the
   winning proposal (with the loser's valid critiques folded in).

### 1.2 The prompt, isolated

The block quoted at step 2 above is the whole of what is being asked. Restated on its
own so it cannot be missed:

> Design a server-authoritative, persistent terrain-editing architecture for the current
> UE 5.7 project using Voxel Plugin Free Legacy as the provisional backend behind a
> replaceable adapter, supporting 16–32 players, join-in-progress, mining yield from
> removed material, snapshot + operation persistence, chunk relevancy, concurrent edits,
> future powered excavation, and backend replacement. Require: class/subsystem
> boundaries, data structures, server/client sequence, persistence schema, chunk revision
> model, concurrency, JIP, failure recovery, tests, performance risks, what stays
> plugin-specific.

### 1.3 Required deliverables

The challenge names twelve things the answer must contain. They are listed here as a
checklist so an answer can be checked against them:

1. Class / subsystem boundaries
2. Data structures
3. Server/client sequence
4. Persistence schema
5. Chunk revision model
6. Concurrency
7. Join-in-progress
8. Failure recovery
9. Tests
10. Performance risks
11. What stays plugin-specific
12. Backend replacement (how the adapter is swapped)

And nine constraints it must satisfy: **server-authoritative**, **persistent**, **Voxel
Plugin Free Legacy as provisional backend**, **behind a replaceable adapter**, **16–32
players**, **join-in-progress**, **mining yield derived from removed material**,
**snapshot + operation persistence**, **chunk relevancy**, **concurrent edits**, and
**future powered excavation**.

### 1.4 A note on step 1 — the two review files

Step 1 lists "the two review files" as part of the identical context. Those are the
CP-002 architecture reviews archived in `Docs/reviews/`. They are **not** reproduced in
this packet, for one reason: everything the Director accepted from them was ruled into
`DECISIONS.md` as **D-010 … D-016**, which *is* reproduced here in full (section 5), and
the rulings — not the reviews that prompted them — are what binds. If a vendor wants the
original review text, it is in the public repo at
`Docs/reviews/2026-09-05_GPT56_Master_Handoff.md` and
`Docs/reviews/2026-09-05_Claude_Review.md`. This omission
applies equally to both vendors, so the contexts remain identical.

---

## 2. Constitution — `AGENTS.md`

The vendor-neutral project constitution. It binds any model from any vendor in any tool.
Section 3 defines the risk classes (this task is **R3**), section 4 the non-negotiable
engineering rules, section 9 the drift guard, and section 10 the escalation rule that
makes "stop and report the ambiguity" the correct response to an underdetermined
specification.

**Source:** `AGENTS.md` · reproduced verbatim, unedited.

--- BEGIN AGENTS.md ---

# AGENTS.md — Project Constitution

**Project:** realistic smooth-terrain open-world multiplayer survival/industrial game,
UE 5.7. Codename `VoxelWorld` (D-015: the codename is not the identity).

This file is vendor-neutral and authoritative. Any model, from any vendor, in any tool,
obeys it. Tool-specific adapters (`CLAUDE.md`, a Codex instruction file, etc.) add
session notes only — they never contradict this file.

**Effective:** CP-002 (2026-09-05) · Governance basis: **D-014**

---

## 1. Session start — before anything else

1. Read `Docs/STATE.md` and `Docs/VISION.md`. Also read `Docs/ARCHITECTURE.md` and
   `Docs/RISKS.md` when they exist.
2. Confirm the current task with the Director before writing code.
3. Never rely on chat history or memory for project facts. `/Docs` is the only truth.
4. If the docs and the conversation conflict, **ask**.

## 2. Roles

Roles are functions, not brands. Who holds each role today is an operational choice
recorded in `Docs/STATE.md` — never in this file.

| Role | Authority | Limits |
|---|---|---|
| **Director** (human) | Every ruling. Pillars, scope, acceptance, UE testing. | — |
| **Architect** | Proposals only: options, tradeoffs, risks, one recommendation. | Cannot change a numbered decision. Proposes one. |
| **Implementer** | Bounded increments inside approved architecture. | **No architectural authority.** Stops on ambiguity. Obeys allowed-file and dependency boundaries. |
| **Independent reviewer** | Reviews architecture or diffs assuming the author is wrong. | No implementation. Finds flaws; separates blocker from polish. |

**Writer is not reviewer for R3 work.** No model silently changes a numbered decision:
agents propose, the Director rules, the decision is recorded.

## 3. Risk classes

Classify every task before starting it.

| Class | Meaning | Examples | Process |
|---|---|---|---|
| **R0** | Mechanical | rename, formatting, comment, trivial config | Implement; build/diff check |
| **R1** | Bounded implementation | serializer, validator, small function, test | Implement; review before merge |
| **R2** | Localized design | internal data structure, small component API | Plan mode; Director approves the plan |
| **R3** | Subsystem architecture | terrain, replication, persistence, join-in-progress, power graph, threading, save migration | **Proposal file + independent review + Director ruling, before any implementation** |
| **R4** | Vision / irreversible | abandon deformable terrain, switch engine, change player scale, remove a pillar | **Director only.** No AI decides. |

R3 artifacts live at `Docs/proposals/P-XXX-<slug>.md` and
`Docs/reviews/P-XXX-review-<vendor>.md`.

## 4. Engineering rules — non-negotiable

- **Server-authoritative always.** Assume a dedicated server. Never trust the client.
- **C++ owns** simulation, networking, and data: terrain edit sync, power graph,
  persistence, inventory core. **Blueprint owns** feel, UI, tuning, asset hookup.
  Pattern: `ABaseHarvestable` → `BP_Harvestable_Tree`.
- **Never one ticking replicated actor per machine.** Power and automation run as a
  server-side graph simulation; replicate state deltas only.
- **Terrain edits replicate as operations** (op type + params), never meshes.
- **Distance-based net relevancy** on everything replicated.
- **Gameplay never calls the terrain plugin directly.** All terrain access goes through
  the game-owned terrain service / adapter (**D-011**). Plugin-specific types stay
  inside the adapter module. Gameplay code never includes plugin headers.
- **Persistence formats are versioned, flat, and inspectable.** Every persistent format
  carries a schema version and a migration path. Old-save fixtures live in
  `Tests/Saves/`.
- **Small increments.** One system per session. It compiles and passes its multiplayer
  PIE test before anything new starts.
- **No new architectural dependency without a decision entry.**

## 5. Output format for implementation tasks

1. **Goal** — one line
2. **Complete code / files** — never fragments, never pseudocode
3. **Exact placement** — full paths, exact menu locations
4. **How to run / compile**
5. **Expected output** — what success looks like

Blueprint work = numbered node-by-node instructions; the Director screenshots the graph
and the agent verifies it against the intended wiring.

## 6. Action words — execute exactly

These are plain words the Director types. They work because this file defines them, so
any model that has read it executes them identically. (Definitions from
`Docs/OPERATIONS.md` section 4.)

| You type | The agent does |
|---|---|
| `resume` | Reads AGENTS.md + STATE.md + VISION.md; recites CP number and current task in one line; waits |
| `status` | `git status` + `git log -3` + one-paragraph state summary |
| `next` | Proposes the next BACKLOG task with exact steps in the standard format; waits for go |
| `go` | Executes the proposed plan |
| `checkpoint` | Updates STATE/BACKLOG/DECISIONS/RISKS, commits (`type(scope): summary`), pushes, confirms |
| `push` | Commits current work with a message and pushes (no doc updates) |
| `pull` | `git pull` — use before starting if you edited docs from your phone |
| `recite the vision` | Drift test: restates pillars, scope guards, and current decisions from the docs |
| `stuck` | You are lost in the UE editor: gives exact click paths / keyboard sequence for the current step |
| `explain <thing>` | Plain-language explanation of a file, system, or error, no changes |
| `undo` | Reverts the last change (asks first if it means a commit) |
| `review this` | Adversarial mode: assume the pasted proposal/diff has a subtle flaw; find it; no implementation |
| `packet <task>` | Writes a bounded worker packet (allowed files, invariants, tests, done criteria) — for delegating |
| `rule D-0XX: <text>` | Records a Director ruling as a numbered decision |
| `park <idea>` | Appends the idea to BACKLOG Phase 5+ without acting on it |

Anything not on this list is a normal request.

## 7. Checkpoint protocol

On the word `checkpoint`, or at session end:

1. Update `Docs/STATE.md` — new CP number, date, state, current task.
2. Add `Docs/DECISIONS.md` entries for any rulings made this session.
3. Move completed `Docs/BACKLOG.md` items into the Done log with their CP number.
4. Update `Docs/RISKS.md` — new risks, results, closed risks.
5. Commit as `type(scope): summary` (feat / fix / docs / chore / test).
6. Push. Confirm the push line shows `main -> main`. **Not pushed = not saved.**

## 8. Git rules

- One scoped task per commit. Never mix architecture with unrelated changes.
- **Never rewrite history.** No force-push, no amending a pushed commit.
- **Never commit secrets or tokens.** Generated config tokens count.
- Touch `.uasset` / `.umap` only when the task explicitly requires it.
- Inspect the diff before committing. Rollback must stay easy.

## 9. Drift guard

Flag and request a decision entry before proceeding if requested work contradicts
`Docs/VISION.md`:

- blocky terrain
- client-authoritative shortcuts
- space travel / multiple planets
- 100+ player features
- commercial features
- **direct plugin calls from gameplay code** (violates D-011)

## 10. Escalation rule

> **If a specification does not uniquely determine an architectural choice, stop and
> report the ambiguity. Do not invent a design.**

Escalate on: global vs. per-chunk revision IDs; Subsystem vs. ActorComponent when
unspecified; a plugin type needing to leak into a gameplay module; a save-schema change;
a conflict between two stated requirements; a task that cannot be completed inside its
allowed files. Stopping at ambiguity is correct behaviour, not failure.

## 11. Director profile

- Intermediate technical builder. Strong at debugging (returns logs, tests hypotheses),
  Linux, and networking.
- **Git novice — always give exact commands.**
- **Struggles with UE editor navigation.** Prefer, in this order: config file edits →
  C++ → Python-scripted editor actions → Blueprint node instructions → menu clicks.
  When menus are unavoidable, give the exact click path.
- Never explain terminal basics, pip, or copy/paste.


--- END AGENTS.md ---

---

## 3. Vision — `Docs/VISION.md`

The founding document, immutable except by numbered decision. It outranks every other
document here. The pillars are rank-ordered and lower numbers win conflicts. Read the
identity statement and the D-015 amendment carefully: **only terrain and geology are
volumetric**, and the voxel grid is meant to be as invisible to the player as the physics
broadphase.

**Source:** `Docs/VISION.md` · reproduced verbatim, unedited.

--- BEGIN Docs/VISION.md ---

# VISION.md — [Working Title: TBD, see D-008]

> **The founding document.** Immutable except by numbered entry in DECISIONS.md.
> Loaded at the start of every working session, alongside STATE.md.
> If current work contradicts this file, the work is wrong or a decision is missing.

**Founded:** 2026-06-12 · **Founder/Director:** Harjas · **Architect/Programmer:** Claude

---

## The Original Idea (founder's words, preserved)

An open-world multiplayer game where other people join. You can live off of the
world and manipulate it — harvest resources, terraform, create permanent
structures. More than just mining nodes in Rust and chopping trees.

World manipulation in the style of Minecraft, but the game looks and feels
realistic, like Rust or Satisfactory. **No blocks.**

Technology matters — not just primitive tools. Electricity and machines are a
thing.

A combination of Rust, Minecraft (modded with tech modpacks), Satisfactory,
No Man's Sky / Star Citizen, and Ark Survival Ascended. Interplanetary flight
is too big a scope — **one planet for the foreseeable future**, with different
regions and biomes.

Harvest everything Unreal Engine 5.7 has to offer. The game should look
super, super real.

Built the way Minecraft was built: vanilla before modded — few ores, few mobs,
simple terrain generation first, then grow.

---

## Identity (D-015)

**The world permanently records what the players did to it.**

That sentence, not any technology, is the game. A mountain existed when a friend group
first joined the server. They tunnelled through it, found ore, widened the excavation,
built a road, installed power, established a quarry, automated the material handling —
and months later the shape of that valley visibly records the history of the server.

### Voxels are infrastructure, not art direction

Only **terrain and geology** are volumetric: soil, stone, ore, tunnels, trenches, slopes,
quarry faces, basements, embankments.

Everything else is conventional: buildings, machines, props, trees, rocks, and foliage are
normal meshes, Nanite, and PCG. Player structures are a modular mesh/entity system, not
cubes.

> **The test for every asset decision: the voxel grid should be as invisible to the player
> as the physics broadphase.**

The player never sees a "voxel." They see dirt, rock, ore, cliff, tunnel walls, and quarry
faces. "Voxel" describes a possible internal representation; it is not the emotional
identity of this game, and `VoxelWorld` is a codename only — the final title should not
mention voxels.

---

## Inspiration Matrix — what we take, what we leave

| Source | We take | We leave |
|---|---|---|
| **Rust** | Survival loop, socket building, server community feel, realistic art bar | Full-loot 100+ player brutality, offline raiding misery |
| **Minecraft (tech modpacks)** | Free-form world manipulation, deep tech trees, automation joy, alpha-first dev philosophy | Blocky aesthetic |
| **Satisfactory** | Machines, power networks, logistics, "the factory grows" satisfaction | Pure factory focus — we keep survival at the core |
| **Ark: Survival Ascended** | Creature ecosystem, taming fantasy, biome danger | Grind-heavy balance (creatures = Year 2, per D-005) |
| **No Man's Sky / Star Citizen** | Sense of planetary scale and wonder | Space flight, multiple planets — **out of scope indefinitely** |

---

## Pillars (rank-ordered — when pillars conflict, lower number wins)

1. **The world is malleable and persistent.** Dig, terraform, tunnel, build
   anywhere. The server remembers everything at next login.
2. **Realistic, never blocky.** Smooth voxel terrain, Lumen lighting, scanned
   assets. Rust-quality visuals are the bar.
3. **Technology progression.** Primitive tools → crafting stations →
   electricity → machines → automation.
4. **Built for friends.** 16–32 players on a self-hosted dedicated server.
   Fun > polish > profit. No commercial pressure, ever.
5. **PvE heart, PvP edges.** Safe home regions to build; contested rich
   zones worth fighting over.

---

## What this game is NOT (scope guards)

- **Not commercial.** No Steam release, no monetization, no marketing.
- **Not space.** One planet. No ships, no orbits, no other worlds.
- **Not blocks.** If terrain ever reads as cubes, we have failed Pillar 2.
- **Not Rust-scale.** 16–32 players, not 100+. No custom netcode arms race.
- **Not pretty before playable.** Placeholder art is acceptable for all of
  Year 1. Systems first.

---

## Drift Checks

Run at every checkpoint. Any unchecked box halts feature work until resolved.

- [ ] Terrain is smooth-voxel and player-deformable
- [ ] Every gameplay system is server-authoritative
- [ ] The tech path still leads to electricity and machines
- [ ] Scope is still one planet, 16–32 players
- [ ] Development is still incremental, Minecraft-alpha style
- [ ] The five inspiration games above are still the reference set
- [ ] The terrain backend remains replaceable (D-010, D-011)
- [ ] Voxels are still invisible to the player (D-015)

---

## Success Definition

A weekend night, six-plus friends on the server: someone is terraforming a
base perimeter, someone is wiring a generator into a new machine, someone is
hunting in the forest, someone is pushing into a contested zone for rare ore.
Nobody is asking when the game will be "done." The world remembers all of it
tomorrow.


--- END Docs/VISION.md ---

---

## 4. Game design — `Docs/GDD.md`

Living design document; `VISION.md` outranks it. The sections that bear directly on
this challenge are **Terrain Manipulation** (yield is simulation-owned, computed from the
volume actually removed), **Multiplayer** (the D-012 persistence shape), and **Technology
Progression** (Stage 4 landscape-scale excavation is what "future powered excavation" in
the challenge refers to).

**Source:** `Docs/GDD.md` · reproduced verbatim, unedited.

--- BEGIN Docs/GDD.md ---

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


--- END Docs/GDD.md ---

---

## 5. Decision log — `Docs/DECISIONS.md`

Every ruling the Director has made. **D-010, D-011, D-012 and D-013 are the four that
constrain this design most directly**; D-002, D-003 and D-006 set the multiplayer and
code-ownership frame; D-020 and D-021 record what the T-101A test setup actually was.
A numbered decision is not the Architect's to overturn — see section 0 of this packet.

**Source:** `Docs/DECISIONS.md` · reproduced verbatim, unedited.

--- BEGIN Docs/DECISIONS.md ---

# DECISIONS.md — Decision Log

> Every design or architecture fork gets a numbered entry. The Director rules;
> Claude records. Entries are never deleted — superseded decisions get status
> SUPERSEDED with a pointer to the replacement.

**Template:**
```
## D-0XX — Title (YYYY-MM-DD) — STATUS
Context · Decision · Consequences
```

---

## D-001 — Smooth voxel terrain via Voxel Plugin (2026-06-12) — ACCEPTED
**Context:** Minecraft-grade world manipulation with Rust-grade realism. Stock
UE Landscape cannot deform at runtime; blocky voxels violate Pillar 2.
**Decision:** Marching-cubes smooth voxel terrain using Voxel Plugin (free
tier to prototype, Pro when justified). Nanite reserved for static meshes.
**Consequences:** Terrain look depends on materials, not Nanite. A custom
replication + persistence layer for voxel edits is our engineering burden.
**Superseded in part by D-010 (backend provisional; requirement retained).**

## D-002 — Server-authoritative from day one (2026-06-12) — ACCEPTED
**Context:** Retrofitting multiplayer into a single-player UE project is
effectively a rewrite.
**Decision:** Every system is written and tested against a dedicated-server
model from its first version. Multiplayer PIE testing is the daily loop.
Dedicated Linux server is the shipping target.
**Consequences:** Slightly slower feature development; zero MP-retrofit risk.
Requires engine-from-source build for the Linux server target (~Phase 3).

## D-003 — Scale target: 16–32 players, self-hosted (2026-06-12) — ACCEPTED
**Context:** Rust-scale (50–100+) demands custom netcode; co-op (2–8) wastes
the concept's social potential.
**Decision:** 16–32 players on one self-hosted dedicated server. Stock UE
replication with aggressive distance relevancy. Direct IP connect.
**Consequences:** No platform services, no matchmaking, no anti-cheat arms
race. Replication discipline (D-006 rules) is mandatory, not optional.

## D-004 — PvE core, zoned PvP later (2026-06-12) — ACCEPTED
**Context:** Full-loot PvP forces raid protection, balance, and anti-cheat
work from day one and risks friend-group burnout.
**Decision:** Ship PvE-only first. Add server-side PvP zone volumes (~M12+)
that flip damage/build rules. Contested zones hold the richest resources.
**Consequences:** Year 1 stays buildable. Structural integrity/decay must be
decided before PvP ships (GDD open question #3).

## D-005 — Creatures deferred (2026-06-12) — ACCEPTED
**Context:** Ark-style taming/breeding/command AI is the single largest
content sink in the inspiration set.
**Decision:** Year 1: passive huntable wildlife only (simple behavior trees).
Year 2: hostiles, taming, breeding.
**Consequences:** Survival loop gets meat early; the Ark fantasy waits.

## D-006 — Code strategy: C++-forward hybrid (2026-06-12) — ACCEPTED
**Context:** Claude (primary programmer) writes complete C++ files fluently
but cannot draw Blueprint graphs — only describe them and verify screenshots.
**Decision:** C++ owns simulation, networking, and data (voxel sync, power
graph, persistence, inventory core). Blueprints stay thin children for feel,
UI, tuning, and asset hookup, built from Claude's numbered instructions and
verified by screenshot. Pattern: C++ base (`ABaseHarvestable`) → BP child
(`BP_Harvestable_Tree`).
**Consequences:** Director needs editor fluency + compile workflow, not C++
authorship. Core engineering rules: no per-machine ticking replicated actors
(power/automation = server-side graph sim, replicate state deltas); voxel
edits replicate as operations; distance relevancy everywhere.

## D-007 — Collaboration & continuity system (2026-06-12) — ACCEPTED
**Context:** LLM context decays in long chats; new chats start near-blank.
The founder's original vision must be un-forgettable.
**Decision:** Repo `/Docs` is the single source of truth (VISION, GDD,
DECISIONS, STATE, BACKLOG). Checkpoint protocol: on "checkpoint" or session
end, Claude emits updated STATE.md + decision entries + commit message.
Chats are short, task-scoped, and disposable; every session opens by loading
STATE.md + VISION.md. Toolchain: chat = design office; Claude Code in-repo =
primary programmer (reads CLAUDE.md); UE 5.7 in-editor AI = concierge; MCP
editor control = evaluate ~M3+.
**Consequences:** Continuity survives any context loss, any model change,
any gap in development.

## D-008 — Working title (—) — OPEN
**Context:** The project needs a name for the repo, server, and docs.
**Decision:** Pending Director. Claude to present candidates on request.

## D-009 — Perspective: true first person + 3P toggle (2026-06-12) — ACCEPTED
**Context:** Director ruled the game is first-person. In multiplayer, every
player needs a full-body animated mesh regardless of camera — others always
see whole bodies — so camera style was the real fork. References: Rust and
Tarkov (true FP, visible body), Ark (perspective toggle).
**Decision:** True first person — camera at the head socket of the full-body
character, own body visible looking down — plus an Ark-style toggle to a
third-person boom camera. Still built on the Third Person template; SETUP.md
unchanged.
**Consequences:** Phase 1 gains T-107 (camera rig: head-socket camera, FOV
tuning, near-clip fixes, owner-only head hiding, toggle). Camera is purely
client-side — zero networking cost. Held tools attach to hand sockets once
and work in both views (no duplicate viewmodel meshes, a hidden tax of the
floating-arms approach we avoided). **Known flag:** third-person corner-
peeking is a PvP balance problem — decide whether PvP zones force first
person when D-004 ships.

## D-010 — Terrain requirement vs. backend (2026-09-05) — ACCEPTED
**Context:** D-001 fused three separate decisions into one: the gameplay requirement,
the world representation, and the vendor. Only the first is durable.
**Decision:** The game requires a persistent, server-authoritative, deformable world
supporting arbitrary volumetric excavation and addition — tunnels, mining, earthworks,
and later industrial-scale terraforming. **This requirement is durable.** The terrain
backend is **provisional**: the initial candidate is **Voxel Plugin Free Legacy** (free,
ships 5.7 binaries); **Voxel Plugin 2** is the upgrade candidate when budget and engine
compatibility allow. No backend is permanent until it passes the **T-101B feasibility
gate**.
**Consequences:** Backend code is isolated behind the D-011 adapter. R-008 tracks the
licensing and version risk. A successful solo dig proves almost nothing — the gate
decides.

## D-011 — Authoritative terrain service (2026-09-05) — ACCEPTED
**Context:** If gameplay calls the terrain plugin directly, the plugin API embeds itself
across the whole game: resource logic depends on the renderer, network messages and save
formats become plugin-specific, and replacing the backend becomes a rewrite.
**Decision:** Gameplay owns edit operations, material semantics, permissions, revisions,
and resource yield. The plugin sits behind a game-owned adapter and is **never the
economic authority**. Gameplay code never includes plugin headers directly.
**Consequences:** The backend is replaceable and the service is testable without a
renderer. Plugin-specific types stay inside the adapter module. Mining yield is computed
by the simulation from material actually removed, never inferred from the rendered mesh.

## D-012 — Persistence model (2026-09-05) — ACCEPTED (provisional)
**Context:** Persistence is the pillar. "We changed the struct and the server world is
gone" must never become normal — for a friends server the emotional value of the world
eventually exceeds that of the code.
**Decision:** The deterministic base world (seed + generator version + authored stamps)
is never saved redundantly. Each modified chunk stores a snapshot at revision R plus an
append-only operation journal after R, compacted on thresholds. Revisions are monotonic.
SQLite holds entities — players, inventories, structures, machines, grids; chunk data
lives in versioned files.
**Consequences:** Join-in-progress = snapshot + operations since. Every format carries a
schema version and a migration path. Old-save fixtures are kept under `Tests/Saves/`.

## D-013 — Terrain multiplayer moves into the gate (2026-09-05) — ACCEPTED
**Context:** D-002 requires server authority from day one, yet the June BACKLOG scoped
T-101 as solo and deferred terrain edit sync to Phase 3 — exempting the riskiest system
from the project's own principle.
**Decision:** **No terrain backend is accepted on solo sculpting.** Concurrent edits,
save/restart, and join-in-progress are pass criteria of **T-101B**. This supersedes the
June BACKLOG phasing that deferred sync to Phase 3.
**Consequences:** Phase 1 is terrain feasibility. Ordinary survival content waits until
one hill is trustworthy.

## D-014 — Vendor-neutral AI governance (2026-09-05) — ACCEPTED
**Context:** `CLAUDE.md` as the project constitution made one vendor structurally
load-bearing. The 3-month gap and a cross-vendor review proved the repo-brain works — and
that it should not be branded.
**Decision:** `/Docs` plus `AGENTS.md` are the constitution. Roles — Architect,
Implementer, Reviewer — are functions held by replaceable models. Current implementer:
**Claude Code on Opus under Claude Pro**. The primary implementer for Phase 1B onward is
chosen by the blind benchmark (`Docs/DUAL_AGENT_SETUP.md` section 6). **Writer is not
reviewer for R3 work.** No API spending until programmatic orchestration has real value.
The GLM worker strategy is **parked** until at least 10 bounded tasks exist.
**Consequences:** `CLAUDE.md` becomes a thin adapter. Any vendor can be swapped without
changing the workflow.

## D-015 — VISION amendment: voxels are infrastructure, not art direction (2026-09-05) — ACCEPTED
**Context:** The codename made an implementation detail sound like the identity. The June
GDD already built this way in practice; the principle was simply never stated.
**Decision:** Add to VISION.md the principle that **only terrain and geology are
volumetric** — buildings, machines, props, trees, rocks, and foliage are conventional
meshes / Nanite / PCG. The voxel grid should be as invisible to the player as the physics
broadphase. Add the identity statement: **the world permanently records what the players
did to it.** Add two drift checks: "terrain backend remains replaceable" and "voxels are
invisible to the player."
**Consequences:** The codename stays a codename; the final title should not mention
voxels. Every asset decision is tested against the invisibility rule.

## D-016 — Progression identity (2026-09-05) — ACCEPTED
**Context:** "Primitive → workbench → electricity → automation" is a generic tech tree. It
describes any survival game and distinguishes none.
**Decision:** Technology progression is framed as **increasing scale of environmental
control** — Stage 0 human labour, Stage 1 organized workshop, Stage 2 powered control,
Stage 3 industrial logistics, Stage 4 landscape-scale infrastructure. **"The map
physically records the factory's growth."** Survival pressure comes from environment
mastery — temperature, exposure, darkness, hazards — not default bar-maintenance chores.
Food exists without becoming a tax unless testing proves that loop is fun.
**Consequences:** The GDD progression and survival sections are rewritten. Survival meters
remain an open question resolved by testing, not by genre convention.

## D-019 — Repository stays public (2026-09-05) — ACCEPTED
**Context:** The repo was created private (CP-001) and the Director made it public
manually so an external ChatGPT audit could read it — the audit that became the CP-002
architecture review. STATE still said "private" until CP-002 corrected the record. With
dual-vendor review now part of the workflow (D-014), the question is whether public is
the right steady state or just an artifact of that one audit.
**Decision:** **The repository stays public.** Any vendor can then read the constitution
and state from a raw URL with no connector, no auth, and no upload — which is exactly
what the blind benchmark (`Docs/DUAL_AGENT_SETUP.md` section 6) needs, since both
vendors must demonstrably see identical context. Raw URLs are listed in
`Docs/CHAT_OPENER.md`.
Note this is a convenience, not a requirement: Codex and similar tools can authenticate
against a private repo. Public is chosen because the friction saved is real and the cost
is currently near zero.
**Consequences:**
- The repo must stay free of secrets. The CP-002 scan is clean and `.gitignore` now
  blocks `*.env` and `**/secrets*`.
- **The generated Android File Server token in history at `4549474` is left in place**,
  per Director ruling. It only ever authorized the Android File Server (USB/network file
  push to an Android device running this project); that plugin is now disabled for the
  project, so nothing regenerates or honours it. Rewriting history to purge it would
  violate the git rules for no security gain.
- One concrete cost to watch: **public repos serve Git LFS objects to anyone who clones,
  and LFS bandwidth is billed to the repo owner** against GitHub's free monthly quota.
  238 LFS files today. Not a problem at this obscurity, but it is the thing that would
  make private worth revisiting — not the source code.
- Revisit if the project ever holds server credentials, private playtest builds, or
  anything with commercial value. Flipping to private later is one click; un-publishing
  what was already cloned is not.

## D-020 — `Lvl_ThirdPerson` is the T-101A test map; `VoxelSandbox` is abandoned (2026-09-06) — ACCEPTED
**Context:** `Content/Maps/VoxelSandbox.umap` was created at CP-002 as the dedicated
terrain sandbox. At CP-003 it ended up holding the failed Voxel-Graph state (an empty
world) and was deliberately left uncommitted. When `place_voxel_world.py` was re-run on
2026-09-06 the editor happened to be on the ThirdPerson template map, so the voxel world
was spawned into `Lvl_ThirdPerson` instead — and that turned out to be the better host,
because it already carries the PlayerStart, GameMode and `BP_ThirdPersonCharacter` that
the dig test needs. `VoxelSandbox` has none of them.
**Decision:** **`Content/ThirdPerson/Lvl_ThirdPerson` is the T-101A map of record.**
`Content/Maps/VoxelSandbox.umap` was reverted to its committed state and is abandoned for
this task, not deleted.
**Consequences:**
- The voxel world actor, the moved PlayerStart and the dig wiring all live in
  `Lvl_ThirdPerson` and its `__ExternalActors__` packages. That level uses One File Per
  Actor, so **each actor is its own package and saving the level does not save them** —
  a trap that cost three failed test launches at T-101A. Scripts must save
  `actor.get_package()`, never `actor.get_outer().get_outermost()`.
- Editing a template map means template content is mixed with test content. Acceptable
  for a smoke test; T-101B should get a purpose-built map with its own PlayerStart and
  GameMode rather than inheriting this one.
- `VoxelSandbox.umap` stays in the repo at its CP-002 state. If a clean sandbox is wanted
  later it needs a PlayerStart and a GameMode added before it is usable.

## D-021 — Solo terrain work runs standalone, not PIE (2026-09-06) — ACCEPTED
**Context:** PIE is configured for the CP-001 three-player replication test
(`PlayNetMode=PIE_ListenServer`, `PlayNumberOfClients=3`). In any non-standalone net mode
Voxel Plugin Free Legacy refuses to use the player camera as its LOD invoker, never
subdivides the render octree, and shows the world as one coarse blob that line traces
miss — while spamming `ClientAdjustPosition` failures, because
`VoxelProceduralMeshComponent` cannot serve as a replicated movement base
(`T-101A_FINDINGS.md` 2d, **R-010**).
**Decision:** **PIE stays on the three-player settings** — that is what T-101B needs.
Solo terrain testing launches standalone via `Tools\Play-Solo.ps1`, which runs as a
separate process so the editor can stay open beside it.
**Consequences:**
- Any solo terrain result obtained through PIE is invalid and should be re-run standalone.
- Multiplayer terrain testing is **blocked on adding a `VoxelInvokerComponent`** to the
  character. That is a hard requirement, not polish, and it is now a T-101B entry cost.
- Standalone writes to `Saved\Logs\Standalone_T101A.log`, separate from the editor's
  `VoxelWorld.log`, so "check the log" stays unambiguous while both run.


--- END Docs/DECISIONS.md ---

---

## 6. Current state — `Docs/STATE.md`

Checkpoint CP-004, 2026-09-06 — written at the end of the session that passed T-101A,
immediately before this benchmark. It records what exists in the project right now
(which is: a test map, a sculpted hill, a dig Blueprint, five Python tools, and no C++
module at all), and the two failed drift checks that this architecture is expected to
resolve.

**Source:** `Docs/STATE.md` · reproduced verbatim, unedited.

--- BEGIN Docs/STATE.md ---

# STATE.md — Current Project State

> **Read this file + VISION.md at the start of every session** (Claude Code reads them
> from the repo; see `AGENTS.md` section 1). Updated at every checkpoint.

---

**Checkpoint:** CP-004 · **Date:** 2026-09-06
**Phase:** 1 — Terrain Feasibility

## What happened at CP-004

**T-101A is done. The first hole is dug, and the tunnel goes all the way through.**

A mound built entirely from `AddSphere`, then tunnelled with `RemoveSphere` until it
broke out the far side — **rock spanning open air with sky visible through the opening**
(`Docs/images/T-101A_tunnel.png`). No heightfield can represent that geometry. The
terrain reads smooth and organic, never blocky (Pillar 2, D-015). Log clean.

**Verdict: PASS, with caveats.** Per **D-013** this does *not* adopt the backend; it says
the backend is worth testing properly at T-101B.

> **The honest one-line summary: the representation is proven; everything that makes it a
> persistent multiplayer world is not.**

Four costs were discovered and are now carried into T-101B as known entry costs rather
than surprises. All four are written up in `Docs/T-101A_FINDINGS.md`:

| § | Finding | Consequence |
|---|---|---|
| 2d | `VoxelProceduralMeshComponent` is `NOT Supported` by `FNetGUIDCache`, so a character standing on terrain has an unresolvable movement base; and the plugin refuses camera-as-invoker outside standalone | A `VoxelInvokerComponent` on the character is a **hard requirement** for any multiplayer terrain test. → **R-010** |
| 2e | **Voxel edits do not persist.** `SaveObject` defaults to null, so the world regenerates from the generator on every load | Pillar 1's core promise is an unmeasured opt-in step. `SaveData()` is editor-only; the runtime path is `UVoxelDataTools`. → **R-003** |
| 2f | Editing near your own feet drops the player through the floor into an endless fall | Edit/collision atomicity is a gameplay-facing bug, not cosmetic. → **R-010** |
| 2b | *(from CP-003)* Voxel Graphs are Pro-gated | Procedural generation must be C++. → **T-108** |

Also this session:

- **D-020** — `Lvl_ThirdPerson` is the T-101A map of record; `VoxelSandbox` reverted and
  abandoned for this task. The template map already had the PlayerStart, GameMode and
  character the dig test needed.
- **D-021** — solo terrain work runs **standalone** (`Tools\Play-Solo.ps1`), not PIE.
  PIE stays on the three-player settings because T-101B needs them.
- **R-009 first test passed.** The 7-day deadline was 2026-09-12; the hole and the tunnel
  landed on the **6th**, six days early. The rule stays in force.
- **A One File Per Actor trap cost three failed test launches**, and is worth never
  repeating: `Lvl_ThirdPerson` stores each actor in its own package, so saving the level
  does **not** save its actors. Scripts must save `actor.get_package()` — *not*
  `actor.get_outer().get_outermost()`, which silently saves the map instead. Symptom:
  PIE looks correct (it duplicates the in-memory world) while standalone, which loads
  from disk, does not.

## What happened at CP-003

T-100 done (VS 2022 Community 17.14.37614.0, MSVC 14.44.35207, Win SDK 10.0.26100.0).
D-019 ruled the repo stays public. `CHAT_OPENER.md` made the single canonical opener and
T-006 rescoped. **R-008 found: Voxel Graphs are Pro-gated**, making T-108 a Phase 1
requirement. The D-011 yield hook was proven live — 861,781 voxels across five spheres.

## What happened at CP-002

External architecture review (GPT-5.6) plus a response review (Claude Fable 5.1),
accepted in full as **D-010 … D-016**. Governance became vendor-neutral (`AGENTS.md`);
the roadmap was reordered around a terrain feasibility gate. Reviews archived in
`Docs/reviews/`.

## What exists right now

**In-engine:**

- UE 5.7 Third Person template project **VoxelWorld** (Blueprint, Desktop, Max quality,
  Starter Content OFF), shaders compiled, runs clean.
- **`Content/ThirdPerson/Lvl_ThirdPerson` — the T-101A map of record (D-020).** Contains:
  - `VoxelWorld_T101A` — 50 cm voxels, 1024 voxels (512 m), `VoxelFlatGenerator`,
    collisions on, `WorldGridMaterial` (the engine checker grid — projected from world
    position, so it reads correctly on UV-less procedural meshes and makes holes and
    overhangs legible; plain `BasicShapeMaterial` rendered white-on-white).
  - PlayerStart moved to **(-8228.66, 0, 150)**, ~82 m west of the hill centre, facing it.
  - `BP_ThirdPersonCharacter` wired for digging: LMB → line trace → `RemoveSphere`,
    RMB → `AddSphere`, radius 200, 1000 uu reach, both behind a hit `Branch`.
- **Voxel Plugin Free Legacy** at `Plugins/VoxelFree/` — **v432 / `e9648b302` / 5.7.0**,
  prebuilt Win64 binaries. **Not committed** (gitignored); reinstall via
  `Tools/Install-VoxelFreeLegacy.ps1`.
- `Content/Maps/VoxelSandbox.umap` — at its CP-002 committed state, abandoned (D-020).
  Would need a PlayerStart and GameMode before it is usable.
- Known non-fatal issue: an `ensure` on `AVoxelWorld::DestroyWorldInternal`
  (GeneratorCache) at editor shutdown. Cosmetic; logged, not chased.
- 3-player PIE replication verified at CP-001 (movement only, no terrain).

**Tooling (all scripted; no menu navigation required):**

| Tool | Does |
|---|---|
| `Tools/Editor/place_voxel_world.py` | Places and configures the voxel world, sculpts the test hill, saves the actor package |
| `Tools/Editor/fix_player_spawn.py` | Measures the hill's real reach and moves every PlayerStart clear of it, saving the actor's own package |
| `Tools/Editor/clean_duplicate_skysphere.py` | Removes an accidental duplicate sky sphere |
| `Tools/Play-Solo.ps1` | Launches standalone single-player (D-021); separate process, own log |
| `Tools/Install-VoxelFreeLegacy.ps1` | Idempotent plugin reinstall |

**Repository:**

- Git + LFS, pushed to **https://github.com/Harjas2102/VoxelWorld** — **PUBLIC** (D-019).
- Governance: `AGENTS.md` (constitution) + `CLAUDE.md` (adapter) + `Docs/` (truth).

No gameplay systems yet. No authoritative terrain layer yet. No C++ module yet.

## Current task

**Blind benchmark** — `Docs/DUAL_AGENT_SETUP.md` section 6. Identical context and an
identical terrain-authority challenge to two vendors, blind, then cross-reviewed.
Produces **ARCHITECTURE.md v1**, **D-017** (terrain architecture) and **D-018** (primary
implementer). It runs now, after T-101A, so the proposals are written against the
plugin's real API and its four measured costs rather than against a guess.

Queued behind it:

- **T-101B** — Terrain Feasibility Gate (tiered). Entry cost: a `VoxelInvokerComponent`
  on the character, or there is no multiplayer terrain to test at all.
- **T-108** — C++ `UVoxelGenerator` for strata + ore bodies. Blocks sub-step 1C.
  Propose before implementing (R2/R3 boundary).

**7-day rule (R-009):** still in force for every task. Passed its first test at T-101A.

## Drift checks (VISION.md, run at CP-004)

- [x] Terrain is smooth-voxel and player-deformable — **proven today**
- [ ] **Every gameplay system is server-authoritative — FLAGGED**
- [x] The tech path still leads to electricity and machines
- [x] Scope is still one planet, 16–32 players
- [x] Development is still incremental, Minecraft-alpha style
- [x] The five inspiration games are still the reference set
- [ ] **The terrain backend remains replaceable (D-010, D-011) — FLAGGED**
- [x] Voxels are still invisible to the player (D-015) — grid material is placeholder

**Both flags have the same cause: the T-101A dig wiring.**
`BP_ThirdPersonCharacter` calls `UVoxelSphereTools::RemoveSphere` / `AddSphere`
**directly, on the client, from gameplay code.** That is simultaneously:

- a violation of **D-011** and `AGENTS.md` section 4 — *gameplay never calls the terrain
  plugin directly; all terrain access goes through the game-owned terrain service* — and
  it is named explicitly in the section 9 drift guard as "direct plugin calls from
  gameplay code"; and
- client-authoritative, which `AGENTS.md` section 4 forbids outright.

**This is accepted for T-101A only, and must not survive it.** A smoke test whose entire
purpose is to find out whether the plugin can dig at all has nothing to route through
yet — the adapter's shape is what the blind benchmark and D-017 exist to decide. Writing
one first would have been inventing the architecture the gate is supposed to produce.

The obligation this creates: **the first thing built after D-017 is the terrain adapter,
and this Blueprint is rewired through it or deleted.** It is test scaffolding with a
gameplay-shaped silhouette, which is exactly the kind of thing that quietly becomes
permanent. Feature work does not start on top of it.

## Blockers

None.

## Open decisions

- **D-008** — working title
- **D-017** — terrain architecture v1 (from the blind benchmark)
- **D-018** — primary implementer for Phase 1B+ (same benchmark)
- Survival meter set — resolved by testing, not convention (D-016)
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
| Git + LFS | ✅ git-lfs 3.7.1, push credentials verified |
| Claude Code | ✅ Installed, verified in-repo |
| **Voxel Plugin Free Legacy** | ✅ **v432 / e9648b302 / 5.7 — mounts clean** (gitignored) |
| Voxel Plugin 2 | ❌ Paid, gated on owning Pro Legacy — upgrade candidate only (R-008) |
| Python Editor Script Plugin | ✅ Enabled at CP-002 — the primary way work gets done here |
| Editor Scripting Utilities | ✅ Enabled at CP-002 |
| Visual Studio 2022 (C++ workload) | ✅ 17.14.37614.0, MSVC 14.44.35207, Win SDK 10.0.26100 — **not yet exercised; no C++ module has been built** |
| Editor revision control | ⚠️ Enabled and failing checkout on every scripted save, popping a modal each time. Saves succeed anyway. Set Provider to None when it gets in the way. |


--- END Docs/STATE.md ---

---

## 7. Risk register — `Docs/RISKS.md`

Ten open architectural risks, each with the experiment that would close it. R-001
(edit synchronisation), R-002 (join-in-progress), R-003 (save growth), R-004 (yield
accuracy) and R-010 (voxel terrain as a multiplayer movement base) are the ones this
architecture must have an answer for. A risk is closed by a result and a decision, never
by an opinion.

**Source:** `Docs/RISKS.md` · reproduced verbatim, unedited.

--- BEGIN Docs/RISKS.md ---

# RISKS.md — Architectural Risk Register

> The top architectural unknowns, each with an experiment that resolves it. A risk is
> closed only by a **result** and a **decision**, never by an opinion. Updated at every
> checkpoint (AGENTS.md section 7).

**Opened:** CP-002 (2026-09-05)

Severity / probability scale: **High · Medium · Low**.

---

## R-001 — Terrain backend multiplayer synchronization

- **Severity:** High — Pillar 1 depends on it.
- **Probability:** High — Voxel Plugin Free Legacy provides no server-authoritative
  edit replication out of the box.
- **Mitigation experiment:** T-101B Gate-Critical — client requests an edit, server
  validates and applies, all clients converge. 2–3 clients editing the same region with
  deterministic ordering and no permanent divergence.
- **Owner / task:** T-101B (sub-step 1B)
- **Result:** *open*
- **Decision:** *open*

## R-002 — Join-in-progress modified-chunk transfer

- **Severity:** High — a friends server is joined mid-session constantly.
- **Probability:** High — Legacy has no documented JIP state-transfer path.
- **Mitigation experiment:** Server holds heavily edited terrain; a fresh client joins
  and reconstructs modified chunks from snapshot + ops since, without replaying full
  server history and without visible divergence.
- **Owner / task:** T-101B (sub-step 1E)
- **Result:** *open*
- **Decision:** *open*

## R-003 — Terrain save growth and compaction

- **Severity:** Medium — a server that cannot be saved cheaply cannot run for months.
- **Probability:** Medium
- **Mitigation experiment:** Hundreds to thousands of edits, then measure save-file
  growth, snapshot size after compaction, and bytes per edit. Establish whether the
  operation journal can be compacted into a compact chunk snapshot at revision R.
- **Owner / task:** T-101B (sub-steps 1D, 1F)
- **Result:** *open*
- **Decision:** *open*

## R-004 — Material-yield accuracy

- **Severity:** High — mining as measured removal is the mechanic that makes the game
  (D-011, D-016). If yield cannot be derived from removed volume, mining degenerates
  into "raycast node, add ore."
- **Probability:** Medium — the sphere tools return `ModifiedValues`, so the hook
  exists; whether it is accurate and deterministic is unproven.
- **Mitigation experiment:** T-101B Gate-Critical — dig a known volume of a known
  material and confirm the server computes soil / stone / ore quantities
  deterministically from the edit, not from the rendered mesh.
- **Owner / task:** T-101B (sub-step 1C)
- **Result:** *open*
- **Decision:** *open*

## R-005 — PCG / foliage invalidation after edits

- **Severity:** Low — cosmetic in Year 1.
- **Probability:** High — trees left floating over an excavation is the default
  behaviour of most systems.
- **Mitigation experiment:** Gate-Observe — remove terrain under foliage, document what
  happens, define a response model. Not solved at the gate.
- **Owner / task:** T-101B (sub-step 1F, Gate-Observe)
- **Result:** *open*
- **Decision:** *open*

## R-006 — Dynamic navigation after edits

- **Severity:** Medium — gates creature AI (D-005 Year 1 wildlife).
- **Probability:** Medium
- **Mitigation experiment:** Gate-Observe — measure nav dirtying and rebuild behaviour
  after terrain edits. Does not need to be production-ready; it needs to be known.
- **Owner / task:** T-101B (sub-step 1F, Gate-Observe)
- **Result:** *open*
- **Decision:** *open*

## R-007 — Dedicated-server / plugin compatibility

- **Severity:** High — D-002 makes a Linux dedicated server the shipping target.
- **Probability:** Medium — the plugin declares Win64/Linux/Mac module support, but a
  server build has never been attempted and the shipped binaries are editor DLLs.
- **Mitigation experiment:** Confirm the architecture never requires client-only plugin
  behaviour; attempt a server target build at Phase 4 (T-302 successor).
- **Owner / task:** T-101B (architecture check) → Phase 4 (build proof)
- **Result:** *open*
- **Decision:** *open*

## R-008 — Plugin licensing and version risk

- **Severity:** High — this is the backend the whole gate runs against.
- **Probability:** High.
  - Voxel Plugin **Free Legacy** is maintenance-mode. Installed build: **v432 /
    `e9648b302` / EngineVersion 5.7.0**.
  - **Voxel Plugin 2 is paid** and engine-version-gated: distributed via the Fab "Voxel
    Plugin Installer," which requires owning Voxel Plugin Pro Legacy. Documented engine
    targets at last docs snapshot were 5.5/5.6 — re-verify before relying on it.
- **Mitigation experiment:** The D-011 adapter keeps the backend replaceable, and the
  T-101B gate decides whether Free Legacy is adopted at all. No gameplay code takes a
  dependency on plugin types.
- **Owner / task:** D-010, D-011, T-101B
- **Result — FIRST HIT, 2026-09-05 (T-101A):** **Voxel Graphs are Pro-gated at runtime.**
  `Voxel: Running Voxel Graphs require Voxel Plugin Pro`. All 106 `VoxelGraphGenerator`
  example assets produce an **empty world with no error** — the asset loads,
  `SetGeneratorObject` succeeds, `IsCreated()` returns true, the world reports a
  generation time, and the density field is empty. Free ships exactly two runnable
  generators, both C++: `UVoxelFlatGenerator` and `UVoxelEmptyGenerator`.
  → Procedural generation must be a C++ `UVoxelGenerator` subclass (**T-108**), promoted
  from "later" to a Phase 1 requirement. Full write-up: `T-101A_FINDINGS.md` section 2b.
- **What this changes about the risk:** the exposure is worse than "the free tier has
  fewer features." Gates are **silent at runtime** and are not documented in the headers,
  so each one is found by something quietly not working. Assume more exist; budget for
  discovery in every sub-step of T-101B rather than treating each as a surprise.
- **Decision:** *open* — resolved by the T-101B exit (PASS / CONDITIONAL / FAIL /
  VISION CHANGE). This finding does not by itself argue for VP2: the C++ generator is
  work we owed D-011 regardless.

## R-009 — Director availability and project stall

- **Severity:** High — the project's largest observed failure mode. The repo sat at
  CP-001 for nearly three months: four commits, ~5,000 lines of planning against 0 lines
  of gameplay.
- **Probability:** High — the Director's time is genuinely constrained.
- **Mitigation experiment:** The **7-day rule** — if the first hole is not dug within
  seven days of CP-002, the plan is wrong, not the Director: cut the step smaller. Every
  task is scoped to one evening. Agents prefer config, C++, and Python-scripted editor
  actions over menu instructions (AGENTS.md section 11).
- **Owner / task:** T-101A, and every task definition thereafter
- **Result:** ✅ **First test passed.** The deadline was 2026-09-12; the first hole *and*
  the tunnel landed **2026-09-06**, six days early, in one working session. The rule has
  now been exercised once and held. It stays in force for every subsequent task — one
  success does not retire the largest observed failure mode of this project.
- **Decision:** *open* — keep the rule; re-evaluate after T-101B, which is a much larger
  task and the real test of whether one-evening scoping survives contact with the gate.

## R-010 — Voxel terrain is a hostile multiplayer movement base

- **Severity:** High — it sits directly on Pillar 1 and on the T-101B pass criteria.
  Players stand on terrain constantly; if standing on it is unsound under replication,
  no amount of correct edit-sync saves the feel.
- **Probability:** Observed, not hypothetical — reproduced on the first PIE run at
  T-101A (2026-09-06), which was accidentally still on the CP-001 3-player settings.
- **Evidence:** `VoxelProceduralMeshComponent` is reported `NOT Supported` by
  `FNetGUIDCache::SupportsObject`, so when the server sets it as a character's relative
  movement base the client cannot resolve it and **every `ClientAdjustPosition`
  correction is discarded**. Separately, `AVoxelWorld` refuses to use the player camera
  as its LOD invoker outside standalone net mode, so multiplayer terrain does not render
  at all without a `VoxelInvokerComponent` on the character. Full write-up:
  `T-101A_FINDINGS.md` section 2d.
- **What this changes:** two items the T-101B gate did not have. The invoker is a hard
  requirement, not polish. The movement-base failure needs a deliberate test — a player
  standing on terrain while another player edits it — because that is the exact case the
  game is built around and the exact case this breaks in.
- **Owner / task:** T-101B; the invoker also blocks any multiplayer terrain test at all
- **Result:** *open*
- **Decision:** *open* — feeds D-017 (terrain architecture v1)


--- END Docs/RISKS.md ---

---

## 8. Architecture v0 — `Docs/ARCHITECTURE.md`

The current architecture file. It is a **v0 stub and every type name in it is an
explicitly disclaimed placeholder** — `ITerrainBackend`, `FTerrainEditOp` and
`UTerrainAuthoritySubsystem` are naming sketches from the CP-002 review documents, not
approved API. Its section 5, "What v1 must add", is the same list of deliverables as the
challenge in section 1 of this packet. **Producing v1 is the point of this exercise**;
do not treat v0's names as constraints, and do not feel obliged to keep them.

**Source:** `Docs/ARCHITECTURE.md` · reproduced verbatim, unedited.

--- BEGIN Docs/ARCHITECTURE.md ---

# ARCHITECTURE.md — v0 (stub)

> **Status: v0, deliberately incomplete.** This file records the *shape* the Director
> ruled at CP-002 (D-011, D-012, D-013). It is not yet a design.
>
> **Every type name below is a placeholder.** `ITerrainBackend`, `FTerrainEditOp`,
> `UTerrainAuthoritySubsystem` and friends are naming sketches from the review
> documents, not approved API. They become real only when **T-101A findings** (what the
> plugin's actual API provides) and the **blind benchmark**
> (`Docs/DUAL_AGENT_SETUP.md` section 6) produce **ARCHITECTURE v1** under **D-017**.
>
> Do not implement against these names. Interface design comes *after* contact with the
> real API — that is the whole point of the review's scope-down.

**Version:** v0 · **Opened:** CP-002 (2026-09-05) · **Basis:** D-011, D-012, D-013

---

## 1. Layers

```text
Gameplay / Tools / Machines
        |
        v
Authoritative Terrain Service          <-- game-owned, C++, server truth
  - permissions
  - validation
  - materials
  - operation semantics
  - revisions
  - resource yield
        |
        v
Persistent World State
  - deterministic base (seed + generator version + authored stamps)
  - chunk snapshots
  - edit journal
        |
        v
Terrain Backend Adapter                <-- the only code that knows the plugin
  - Voxel Plugin Free Legacy initially
  - replaceable (D-010)
        |
        v
Rendered terrain / collision / surface queries
```

**The plugin does not own the game's truth.** The game owns terrain semantics; the
backend implements and renders them. Plugin-specific types never cross out of the
adapter module, and gameplay code never includes plugin headers (D-011, AGENTS.md
section 4).

Why this matters: if gameplay called the plugin directly, the plugin API would embed
itself across the whole game, resource logic would depend on the renderer, network
messages and save files would become plugin-specific, replacing or upgrading the backend
would become a rewrite, and terrain could not be tested without a renderer.

## 2. Authority and sequencing

- **The server is the sequencing authority.** It assigns monotonically increasing
  operation IDs and per-chunk revisions. Clients apply the authoritative order; they
  never decide it.
- **Multi-chunk operations are sequenced once, globally.** One edit may touch several
  chunks. The affected chunk set is computed, the operation is sequenced once, and
  clients must never observe a half-applied operation indefinitely. The transaction
  mechanism may start simple, but it must be defined rather than implied.
- **Prediction is deferred.** The first version accepts round-trip latency: client
  requests, server confirms, client sees the change. Prediction is not part of the
  T-101B feasibility gate unless latency proves intolerable.
- **Relevancy is spatial.** Players receive terrain state for nearby and relevant chunks
  only, never every operation across the world. The server keeps global truth and
  replicates spatially.

## 3. Validation

The server validates every edit request even on a friends-only server — this prevents
accidental corruption and produces sane architecture before PvP ever ships:

- player reach to the target position
- operation radius within limits
- operation frequency (rate limiting)
- tool ownership, cooldown, durability, fuel/power
- zone permissions
- target chunk loaded and available
- resulting resource payout

## 4. Persistence shape (D-012, provisional)

- The deterministic base world is never saved redundantly — it is regenerated from seed
  + generator version + authored stamps.
- Each modified chunk carries a snapshot at revision R plus an append-only journal of
  operations after R.
- Compaction folds base + operations into a new snapshot on thresholds (operation count,
  bytes, idle time, shutdown).
- Join-in-progress = snapshot + operations since.
- SQLite holds structured entities: players, inventories, structures, machines, grids.
  Chunk data lives in versioned files.
- Every format carries a schema version and a migration path; old-save fixtures live in
  `Tests/Saves/`.

## 5. What v1 must add

Deferred to **D-017** / ARCHITECTURE v1, informed by T-101A findings and the blind
benchmark: concrete class and subsystem boundaries · data structures and wire format ·
server/client sequence diagrams · persistence schema · the chunk revision model ·
concurrency semantics · join-in-progress protocol · failure recovery · test plan ·
performance risks and budgets · the exact line between plugin-specific and game-owned.


--- END Docs/ARCHITECTURE.md ---

---

## 9. Backend findings — `Docs/T-101A_FINDINGS.md`

What was measured against the real plugin at T-101A, one day before this packet was
assembled. This is the empirical half of the backend picture; section 10 is the
declarative half. The four costs in section 6 of this document — Pro-gated graphs, the
invoker requirement and the unreplicable movement base, no persistence by default, and
the edit/collision atomicity bug — are known entry costs, not open questions.

One reproduction artifact: the embedded Markdown image link in section 5 of that document
(`images/T-101A_tunnel.png`) is relative to `Docs/` and will not resolve from this
packet's location or from a chat window. The image is at
`Docs/images/T-101A_tunnel.png` in the repository. It shows the tunnel, and it is
evidence for one claim only: that the representation supports rock spanning open air.

**Source:** `Docs/T-101A_FINDINGS.md` · reproduced verbatim, unedited.

--- BEGIN Docs/T-101A_FINDINGS.md ---

# T-101A_FINDINGS.md — What Voxel Plugin Free Legacy actually gives us

**Task:** T-101A · **Opened:** CP-002 (2026-09-05)
**Backend under test:** Voxel Plugin Free Legacy **v432 / `e9648b302` / EngineVersion 5.7.0**
**Status:** API survey **complete**. PIE run sections marked *(to be filled in from the
PIE run)* are the Director's to answer — see `Docs/T-101A_RUNBOOK.md` step 7.

> **Scope (D-013).** This document records what the plugin *provides*. It does not adopt
> it. Adoption is T-101B, where concurrency, restart, join-in-progress, and yield are
> pass criteria. A solo dig proves the renderer works and nothing else.

---

## 1. Environment verified

| Fact | Value |
|---|---|
| Engine | UE **5.7.4** (`5.7.4-51494982+++UE5+Release-5.7`) |
| Plugin | Voxel Plugin Free Legacy v432 / `e9648b302`, `EngineVersion 5.7.0` |
| Binaries | Prebuilt Win64 editor DLLs shipped — **no compiler needed** to open the project |
| Modules | `Voxel`, `VoxelGraph`, `VoxelHelpers` (Runtime), `VoxelEditor`, `VoxelGraphEditor`, `VoxelEditorDefault` (Editor), `VoxelExamples` (Runtime) |
| Platform allow-list | Win64, Linux, Mac (`VoxelExamples`: Win64, Linux) |
| Plugin dependencies | Niagara, ProceduralMeshComponent |
| Content mount | **`/Voxel/`** (402 assets) — *not* `/VoxelFree/`, despite the plugin folder name |
| Mounts cleanly | ✅ `LogPluginManager: Mounting Project plugin VoxelFree`, zero errors on editor start |

**Known cosmetic issue:** an `ensure` on `AVoxelWorld::DestroyWorldInternal`
(GeneratorCache, `VoxelWorld.cpp:1071`) fires at editor *shutdown*, logged as an error
with a callstack. Harmless. Do not chase it.

**Generators:** see section 2b — **Voxel Graphs are Pro-only**, so only the two C++
generators run on Free. This is the headline limitation of the free tier.

**Rendering:** Free Legacy renders terrain through `VoxelProceduralMeshComponent` —
**no runtime Nanite**. Voxel Plugin 2 supports runtime Nanite and Lumen, but it is paid
and gated on owning Pro Legacy (R-008). Until then, **terrain material quality carries
the look** (GDD, Art Direction).

## 2. API surface that matters

All names verified against the headers *and* the live `unreal` Python module.

### Terrain actor — `AVoxelWorld` (`unreal.VoxelWorld`)

| Property | Default | Notes |
|---|---|---|
| `VoxelSize` | 100.0 | cm per voxel |
| `Generator` | empty | `FVoxelGeneratorPicker` — `{type, class_, object, parameters}`, type ∈ `CLASS`/`OBJECT` |
| `RenderOctreeDepth` | 10 | `WorldSizeInVoxel = RENDER_CHUNK_SIZE * 2^Depth` |
| `WorldSizeInVoxel` | 32768 | set indirectly via `SetWorldSize(n)` |
| `bCreateWorldAutomatically` | **False** | must be **True** or PIE starts with no terrain |
| `bEnableCollisions` | True | |
| `MaterialConfig` | `RGB` | pair with `/Voxel/Examples/Materials/RGB/M_VoxelMaterial_Colors` |
| `bUseCustomWorldBounds` / `CustomWorldBounds` | off | |

Methods: `CreateWorld(FVoxelWorldCreateInfo)` — **takes a required argument**;
`IsCreated()`; `SetGeneratorObject()` / `SetGeneratorClass()`; `SetWorldSize()`;
`SetRenderOctreeDepth()`; `GetGeneratorInit()`.

> Gotcha found by dry run: an `AVoxelWorld` spawned in the editor is **already created**
> (`IsCreated()` is True immediately), and `CreateWorld()` with no argument raises. To
> apply changed properties use `UVoxelBlueprintLibrary::Recreate(World, bSaveData=true)`.

### Edit tools — `UVoxelSphereTools`

`RemoveSphere` · `AddSphere` · `SetValueSphere` · `SetMaterialSphere` · `SmoothSphere` ·
`SmoothMaterialSphere` · `ApplyKernelSphere` · `TrimSphere` · `RevertSphere` ·
`RevertSphereToGenerator` — each with an `...Async` variant. Box and level (flatten)
equivalents live in `VoxelBoxTools` / `VoxelLevelTools`.

```cpp
static void RemoveSphere(
    TArray<FModifiedVoxelValue>& ModifiedValues,   // <-- the yield hook
    FVoxelIntBox& EditedBounds,
    AVoxelWorld* VoxelWorld,
    const FVector& Position,
    float Radius,
    bool bMultiThreaded = true,
    bool bRecordModifiedValues = true,
    bool bConvertToVoxelSpace = true,
    bool bUpdateRender = true);
```

### ⭐ The most important finding — `ModifiedValues`

Every sphere tool returns `TArray<FModifiedVoxelValue>`, documented in the header as:

> *"Record the Values modified by this function. Useful to track the amount of edit done,
> for instance to give resources when digging."*

**This is exactly the hook D-011 requires.** Yield can be computed from material actually
removed, on the server, rather than asking the rendered mesh what disappeared. The
mechanic the whole design rests on is reachable without patching the plugin.

Caveat for T-101B (sub-step 1C): it is *reachable*, not *proven*. Whether the counts are
accurate, deterministic, and stable across `bMultiThreaded` and the async variants is a
Gate-Critical test, not an assumption. `bRecordModifiedValues=false` makes edits faster
and returns nothing — the authoritative path must never take that shortcut.

### Material queries — `UVoxelDataTools`

`GetValue` / `GetInterpolatedValue` / `SetValue` · `GetMaterial` / `SetMaterial` ·
`CacheValues` / `CacheMaterials` (+ async variants). So **soil/stone/ore queries are
available to gameplay** (Gate-Critical #2) — the remaining question is how the material
*field* gets authored with strata and an ore body, which is 1C work.

### Persistence — `UVoxelDataTools` + `UVoxelBlueprintLibrary`

`GetSave` / `GetCompressedSave` (+ async) · `LoadFromSave` ·
`FVoxelUncompressedWorldSaveImpl` / `FVoxelCompressedWorldSaveImpl` (`VoxelData/VoxelSave.h`) ·
`UVoxelBlueprintLibrary::SaveFrame` (undo frame, not disk) · `GetSpawnersSave` /
`LoadFromSpawnersSave`.

**What this means for D-012:** the plugin offers a **whole-world save blob**, compressed
or not. That is *not* the model D-012 specifies — we want a deterministic base plus
per-chunk snapshots plus an append-only operation journal, so that join-in-progress can
ship one chunk rather than the world. The plugin's blob is a usable **fallback and a
correctness oracle** (save, restart, compare), but **the journal is ours to build.**
Confirm in 1D whether per-chunk extraction is feasible or whether we journal our own ops
and only use the blob for compaction snapshots.

### Multiplayer — `VoxelMultiplayer/`

`UVoxelMultiplayerInterface` (abstract) with `IVoxelMultiplayerClient` /
`IVoxelMultiplayerServer`, and one concrete implementation,
`UVoxelMultiplayerTcpInterface`:

```cpp
bool StartServer(FString& OutError, const FString& Ip = "0.0.0.0", int32 Port = 10000);
bool ConnectToServer(FString& OutError, const FString& Ip = "127.0.0.1", int32 Port = 10000);
```

Example maps: `/Voxel/Examples/Maps/Multiplayer/VoxelExample_TcpMultiplayerMap` and
`VoxelExample_ManualMultiplayerMap`.

**Assessment — this is a reference, not our architecture.** It is a side-channel TCP
socket on its own port, entirely outside UE's replication, relevancy, and authority
model. It carries voxel diffs; it knows nothing about who is allowed to dig, tool
cooldowns, reach validation, or resource payout. Using it directly would put terrain
truth outside the server's authority and violate D-002 and D-011 in one step.

Read it for how diffs are packed and applied. **Build the authoritative layer ourselves**
(D-011), which is precisely what T-101B sub-step 1B exists to prove.

## 2b. ⛔ Free Legacy cannot run Voxel Graphs — found the hard way

```
Voxel: Running Voxel Graphs require Voxel Plugin Pro
```

Assigning `VoxelExample_IQNoise` (a `VoxelGraphGenerator`) produced **an empty world and
a blue sky**. The asset loaded fine, `SetGeneratorObject` accepted it, `IsCreated()`
returned true, and the world reported `took 0.230478s to generate`. Everything looked
healthy. The generator simply produced no density.

**This is the most important limitation found in T-101A**, for three reasons:

1. **It is silent.** Not an error, not a warning — one `Voxel:` line among hundreds. The
   symptom is empty sky, which reads as "I placed the actor wrong."
2. **It voids the example library.** All **106** `VoxelGraphGenerator` assets shipped in
   `/Voxel/Examples/` are Pro-gated at runtime. They are readable as documentation and
   useless as code.
3. **The only runnable generators in Free are the two C++ ones:**
   `UVoxelFlatGenerator` and `UVoxelEmptyGenerator`. That is the entire list.

**Consequence for the roadmap — procedural world generation must be C++.** A
`UVoxelGenerator` subclass is now a **Phase 1 requirement**, not a later nicety: strata,
an ore body, and the 256–512 m authored hill (GDD, World) cannot come from a graph asset
on this backend. T-100 landing the same evening is what makes that path open.

**Consequence for the architecture — mild, and arguably positive.** D-011 already says
gameplay owns material semantics and generation; shipping a vendor's graph asset as the
authoritative world generator would have violated it. What is lost is the *prototyping
shortcut*, not the design. Recorded against **R-008**: this is exactly the class of
"maintenance-mode free tier" limitation that risk exists to track.

**Workaround in use for T-101A:** `VoxelFlatGenerator` for the ground, then the hill is
**sculpted with `AddSphere`** from `Tools/Editor/place_voxel_world.py` — using the same
tools gameplay will use.

## 2c. ✅ The yield hook works, with real numbers

Sculpting the hill exercised `UVoxelSphereTools::AddSphere` from Python and it returned
`(ModifiedValues, EditedBounds)` on every call:

```
+sphere r=3000 at (0, 0, 0)        -> 514627 voxels changed
+sphere r=2400 at (0, 0, 1400)     -> 121175 voxels changed
+sphere r=1500 at (600, -400, 2600) ->  27922 voxels changed
+sphere r=1900 at (2800, 1800, 200) -> 121581 voxels changed
+sphere r=1700 at (-2400, -2000, 100) -> 76476 voxels changed
total voxels modified: 861781
```

So `FModifiedVoxelValue` is not merely present in a header — it is **populated with
plausible counts, reachable from script, and it scales with edit volume**. That is the
D-011 yield mechanism working on real data.

It is still not *proof*: 1C must confirm the counts are accurate against known volumes,
deterministic across runs, and stable with `bMultiThreaded` and the async variants. But
the mechanism the whole economic design rests on is demonstrably live.

## 2d. ⛔ The plugin's mesh cannot be a multiplayer movement base

Found by accident: PIE was still on the CP-001 replication settings
(`PlayNetMode=PIE_ListenServer`, `PlayNumberOfClients=3`) when the first dig test ran.
Two things broke at once, and both are architecture input, not noise.

**1. No invoker means no terrain.** The plugin logs, once per PIE client:

```
Voxel World: Can't use camera as invoker in multiplayer!
You need to add a VoxelInvokerComponent to your character
```

In any non-standalone net mode `AVoxelWorld` refuses to treat the player camera as its
LOD invoker. The render octree then never subdivides: the world generates
(`took 0.123081s to generate`), collision exists, but it renders as a single coarse blob
and line traces into it return no hit. **Multiplayer terrain is therefore gated on
putting a `VoxelInvokerComponent` on the character** — it is not optional polish, it is
the difference between visible terrain and none.

**2. The procedural mesh is not a replicable object.** Repeated, once per correction:

```
LogNetPackageMap: Warning: FNetGUIDCache::SupportsObject:
  VoxelProceduralMeshComponent ...VoxelWorld_UAID_....Root.VoxelProceduralMeshComponent_0
  NOT Supported.
LogNetPlayerMovement: Warning: ClientAdjustPosition_Implementation could not resolve the
  new relative movement base actor, ignoring server correction!
  Client currently at world location X=-8228.700 Y=70.000 Z=92.100
  on base VoxelProceduralMeshComponent_0
```

The character stands on the voxel mesh, so the server sets that component as the
**relative movement base** — and the client cannot resolve it, because the component has
no net GUID. Every movement correction is discarded. On flat ground this is survivable;
the open question is what it does to a player standing on terrain that is actively being
edited underneath them.

This is a **new T-101B question and a new risk**: standing on voxel terrain currently
degrades client movement correction in the exact way that matters for a dig-while-someone
-else-digs test. It was not on the gate list; it is now.

## 2e. ⛔ Voxel edits do not persist — the world regenerates on every load

The hill sculpted in section 2c **does not survive into any other process.** Confirmed
three ways:

1. `AVoxelWorld::SaveObject` defaults to null (`VoxelWorld.h:109`), and its comment is
   `// Automatically loaded on creation`. With no save object there is nothing to load,
   so `CreateWorldInternal` falls back to the generator.
2. The voxel world's actor package is **4745 bytes**, written *after* an edit touching
   861,781 voxels. The data is demonstrably not in the level.
3. A standalone launch logs `VoxelWorld_... took 0.251192s to generate` and puts the
   player on a bare flat plane — the `VoxelFlatGenerator` output, with no hill.

This is how Free Legacy works, not a misconfiguration. Two consequences:

- **Anything sculpted from the editor is a session-local prop.** The T-101A test terrain
  has to be rebuilt by script each session, or the world has to be given a save object.
- **Persistence is an explicit, opt-in step**, and its cost is unmeasured. `SaveObject`
  is a `UVoxelWorldSaveObject` asset; `UVoxelDataTools::GetCompressedSave` /
  `LoadFromCompressedSave` and `GetSave` / `LoadFromSave` are the Blueprint-exposed
  paths. **`AVoxelWorld::SaveData()` is `WITH_EDITOR` only** — the editor's save button
  is not available at runtime, so the shipping path must go through the data tools.

For Pillar 1 — *the world permanently records what the players did to it* — this is the
whole ballgame, and it is now a measured unknown rather than an assumption. It stays
T-101B's question (Critical #5, #8, R-003): how large does the save get, how long does it
take, and does a restart reproduce the world exactly.

## 2f. ⚠️ Editing near your own feet drops you through the floor

Reported from the first playable standalone run: adding a sphere too close to the
character **glitches the player through the ground and into an endless fall**. Digging at
range is fine.

Most likely the collision mesh is rebuilt asynchronously after an edit, leaving a window
with no collision under the capsule — the character falls through before the new mesh
lands, and there is no floor below to catch it.

Not chased at T-101A (it is a solo smoke test), but it is a **gameplay-facing** problem,
not cosmetic: digging beneath yourself is a thing players do constantly in this genre.
Carried to T-101B as an edit-latency question, and it wants a KillZ or a respawn volume
before anyone plays for real.

## 3. Useful assets discovered

| Asset | Why it matters |
|---|---|
| `/Voxel/Examples/VoxelGraphs/IQNoise/VoxelExample_IQNoise` | ⛔ Pro-gated — produced an empty world (section 2b) |
| `UVoxelFlatGenerator` (C++) | The generator actually in use for T-101A |
| `/Voxel/Examples/Materials/RGB/M_VoxelMaterial_Colors` | Matches the default `RGB` material config |
| `/Voxel/Examples/Maps/Tools/HighResolutionDigging` | A working digging demo to compare against |
| `/Voxel/Examples/Maps/Multiplayer/VoxelExample_TcpMultiplayerMap` | The edit-sync reference above |
| `/Voxel/Examples/VoxelGraphs/Cliffs`, `Cave`, `Ravines`, `Erosion` | Readable references for the C++ generator we must now write (1C) |
| 106 `VoxelGraphGenerator` assets total | ⛔ **All Pro-gated at runtime** (section 2b) — readable as documentation only |

## 4. Answers T-101B still needs

Nothing here is evidence about the questions that decide adoption. Carried into the gate:

| Question | Gate item | Risk |
|---|---|---|
| Do concurrent edits from 2–3 clients converge deterministically? | Critical #4 | R-001 |
| Can a joining client reconstruct modified chunks without full history? | Critical #6 | R-002 |
| Does the save survive restart exactly, and how fast does it grow? | Critical #5, #8 | R-003 |
| Is `ModifiedValues` accurate and deterministic enough to pay resources from? | Critical #7 | R-004 |
| What happens to foliage and navigation over removed terrain? | Observe | R-005, R-006 |
| Does any of this require client-only plugin behaviour? | architecture check | R-007 |
| **Does an unreplicable mesh as movement base break players standing on edited terrain?** | **new, from 2d** | **R-010** |
| **Cost of a `VoxelInvokerComponent` per character, and its LOD behaviour at 16–32 players?** | **new, from 2d** | **R-010** |
| **How big and how slow is a `UVoxelWorldSaveObject` for a real dug-out world?** | **new, from 2e** | **R-003** |
| **Is there a no-collision window after an edit, and does it drop players?** | **new, from 2f** | **R-010** |
| **How much work is a C++ `UVoxelGenerator` with strata and an ore body?** | **new, from 2b** | **R-008** |

---

## 5. Run results — 2026-09-06, standalone

Run **standalone, not PIE**, and deliberately so: PIE is on the CP-001 three-player
settings, where the plugin refuses the camera as LOD invoker and renders nothing usable
(section 2d, R-010). Launched with `Tools\Play-Solo.ps1`. Log:
`Saved\Logs\Standalone_T101A.log`.

- **Dig / add work:** ✅ **Yes.** `RemoveSphere` on LMB and `AddSphere` on RMB, wired in
  `BP_ThirdPersonCharacter` via a camera-forward line trace at 1000 uu with a 200 uu
  brush. Both fire reliably. `Trace Complex` was never needed — the procedural mesh is
  hit by an ordinary Visibility trace with simple collision.
- **Tunnel or overhang achieved:** ✅ **Yes — decisively.**
  ![Tunnel through a player-built mound](images/T-101A_tunnel.png)
  A mound built entirely from `AddSphere`, then tunnelled through with `RemoveSphere`
  until it broke out the far side. **Rock spans above open air with sky visible through
  the opening.** No heightfield can represent that. This is the single result T-101A
  existed to obtain.
- **Terrain reads as smooth, not blocky:** ✅ **Yes** — no cubes, no stair-stepping, no
  voxel grid visible in the silhouette (Pillar 2, D-015). It reads as organic rock.
  Caveat: the surface shows obvious **sphere-brush lobing** — the mound is legibly a pile
  of spheres. That is a *brush and material* problem, not a representation problem, and
  belongs to tooling later. The checkerboard exaggerates it.
- **Collision correct after edits:** ⚠️ **Mostly.** Added ground is immediately solid and
  walkable; removed ground stops supporting the player. **But** editing close to your own
  feet drops you through the floor into an endless fall — see section 2f. So collision
  updates, but not atomically with the edit.
- **Editing feel / latency:** **Not measured.** No stutter was reported at a 200 uu brush
  on a 512 m world, but no frame timings were taken. Do not treat this as evidence;
  T-101B measures it.
- **New errors in the log:** ✅ **None.** The only warnings are two missing VisionOS
  editor icons (`Platform_VisionOS_24x.png`), entirely unrelated to this project. The
  known `DestroyWorldInternal` shutdown `ensure` is editor-only and did not appear.
- **Screenshots:** `Docs/images/T-101A_tunnel.png`. One image, and it carries the
  mound, the excavation and the overhang together. A separate flat-ground pit shot was
  not taken.

**What this run did NOT test**, and must not be read as evidence about: concurrency,
persistence, join-in-progress, yield accuracy, or performance under load. Per **D-013**
none of those can be judged from solo sculpting.

## 6. Verdict — PASS, with caveats

**Does Free Legacy clear the bar to continue into T-101B? Yes.**

It renders smooth deformable terrain, both edit tools work from gameplay code, the
representation is genuinely volumetric, and the yield hook the economic design depends on
returns real data (section 2c). That is everything T-101A asked for.

**This verdict does not adopt the backend.** It says the backend is worth testing
properly. Four findings go into T-101B as known costs, not surprises:

| Finding | Cost it imposes |
|---|---|
| 2b — Voxel Graphs are Pro-gated | procedural generation must be C++ (**T-108**) |
| 2d — mesh is not a replicable movement base; invoker required | multiplayer terrain is not free, and standing on terrain is the failure case |
| 2e — no persistence by default | Pillar 1's core promise is an unmeasured opt-in step |
| 2f — edit near your feet drops you through the floor | edit/collision atomicity is a gameplay-facing bug |

The honest summary: **the representation is proven; everything that makes it a
multiplayer persistent world is still unproven.** That is exactly the state T-101A was
scoped to produce.


--- END Docs/T-101A_FINDINGS.md ---

---

## 10. Voxel Plugin Free Legacy — API reference

> **Status: factual extract. No design opinions.** This section lists what exists in the
> installed plugin source: module layout, type names, function names, signatures,
> property names, enumerator names, and compile-time constants. Where a behaviour is
> stated (for example that a function is a stub), the file and line it was read from is
> given so it can be checked independently.
>
> **Why it is here.** `Plugins/VoxelFree/` is gitignored (`Docs/STATE.md`), so it cannot
> be read from the public repository. Both vendors need the same view of the API they are
> being asked to design against.
>
> **Read alongside section 9.** `T-101A_FINDINGS.md` covers the same plugin from the
> point of view of *what was measured at runtime*. This section covers *what is declared
> in the source*. Where they overlap they agree; where this section adds something the
> findings do not record, it is marked **NEW**.

### 10.1 Provenance

| Fact | Value |
|---|---|
| Extracted from | `Plugins/VoxelFree/Source/` (local install; gitignored) |
| Plugin version | `"Version": 432`, `"VersionName": "e9648b302"` (`VoxelFree.uplugin`) |
| Friendly name | `Voxel Plugin Free Legacy` |
| Declared engine version | `"EngineVersion": "5.7.0"` |
| Engine in use | UE 5.7.4 |
| `CanContainContent` | `true` (content mounts at `/Voxel/`) |
| `Installed` | `true` (prebuilt binaries; no compile needed to open the project) |
| Public headers counted | 424 `.h` files across all modules |
| Exported types in `Voxel/Public` | 188 (`class`/`struct` carrying an `*_API` macro) |

**Modules** (`VoxelFree.uplugin`):

| Module | Type | LoadingPhase | PlatformAllowList |
|---|---|---|---|
| `Voxel` | Runtime | `PostConfigInit` | Win64, Linux, Mac |
| `VoxelGraph` | Runtime | `Default` | Win64, Linux, Mac |
| `VoxelHelpers` | Runtime | `PostConfigInit` | Win64, Linux, Mac |
| `VoxelEditor` | Editor | `PostEngineInit` | Win64, Linux, Mac |
| `VoxelGraphEditor` | Editor | `PostEngineInit` | Win64, Linux, Mac |
| `VoxelEditorDefault` | Editor | — | Win64, Linux, Mac |
| `VoxelExamples` | Runtime | — | Win64, Linux |

**`Voxel/Public` directory layout** — the organisation of the runtime API:

```text
Voxel/Public/
  FastNoise/           VoxelFastNoise + Perlin / Simplex / Cellular / Cubic / Value /
                       White noise, gradient perturb, LUT, math
  VoxelAssets/         VoxelDataAsset, VoxelHeightmapAsset (+ Data / Instance /
                       SamplerWrapper variants)
  VoxelComponents/     VoxelInvokerComponent, VoxelNoClippingComponent,
                       VoxelPhysicsRelevancyComponent
  VoxelContainers/     NoGrowArray, VoxelArray3, VoxelArrayView, VoxelSparseArray,
                       VoxelStaticArray
  VoxelData/           VoxelData, VoxelDataAccelerator, VoxelDataOctree (+ LeafData,
                       LeafMultiplayer, LeafUndoRedo), VoxelDataLock, VoxelSave,
                       VoxelSaveUtilities
  VoxelDebug/          VoxelDebugManager, VoxelDebugUtilities, VoxelLineBatchComponent
  VoxelEvents/         VoxelEventManager
  VoxelGenerators/     VoxelGenerator, VoxelGeneratorInstance, VoxelFlatGenerator,
                       VoxelEmptyGenerator, VoxelGeneratorCache, VoxelGeneratorPicker,
                       VoxelGeneratorInit, VoxelGeneratorParameters,
                       VoxelGeneratorTools, VoxelGeneratorHelpers,
                       VoxelTransformableGeneratorHelper
  VoxelImporters/      VoxelLandscapeImporter, VoxelMeshImporter
  VoxelMultiplayer/    VoxelMultiplayerInterface, VoxelMultiplayerInterfaceWithSocket,
                       VoxelMultiplayerTcp
  VoxelPlaceableItems/ VoxelPlaceableItem, VoxelPlaceableItemManager, ...Utilities
  VoxelRender/         IVoxelRenderer, IVoxelLODManager, VoxelProceduralMeshComponent,
                       VoxelChunkMesh, VoxelMaterialInterface, VoxelMaterialIndices,
                       VoxelProcMeshBuffers, VoxelToolRendering, ...
  VoxelShaders/        VoxelDistanceFieldShader, VoxelErosion
  VoxelSpawners/       VoxelSpawner, VoxelMeshSpawner, VoxelAssetSpawner,
                       VoxelSpawnerConfig, VoxelSpawnerActor, VoxelHISMComponent, ...
  VoxelTools/          VoxelBlueprintLibrary, VoxelDataTools, VoxelSurfaceTools,
                       VoxelAssetTools, VoxelProjectionTools, VoxelPhysics,
                       VoxelToolManager, VoxelPaintMaterial, VoxelHardnessHandler,
                       VoxelMathLibrary, VoxelTextureTools, VoxelTestLibrary
    VoxelTools/Gen/    VoxelToolsBase, VoxelSphereTools, VoxelBoxTools, VoxelLevelTools
    VoxelTools/Impl/   VoxelToolsBaseImpl, VoxelSphereToolsImpl, VoxelBoxToolsImpl,
                       VoxelLevelToolsImpl, VoxelSurfaceToolsImpl
  VoxelUtilities/      21 utility headers (SDF, distance field, math, octree, material,
                       serialization, threading, texture, vector, range, ...)
```

### 10.2 Compile-time constants and build-time configuration

From `Voxel/Public/VoxelDefinitions.h`:

| Macro | Value | Line |
|---|---|---|
| `RENDER_CHUNK_SIZE` | `32` | 92 |
| `DATA_CHUNK_SIZE` | `16` | 98 |
| `EIGHT_BITS_VOXEL_VALUE` | `0` | 136 |
| `VOXEL_MATERIAL_ENABLE_R` / `_G` / `_B` / `_A` | `1` / `1` / `1` / `1` | 150–159 |
| `VOXEL_MATERIAL_ENABLE_UV0` / `_UV1` | `1` / `1` | 164–167 |
| `VOXEL_MATERIAL_ENABLE_UV2` / `_UV3` | `0` / `0` | 170–173 |

So a render chunk is 32³ voxels and a data-octree leaf is 16³ voxels.

`Voxel/Public/VoxelUserDefinitions.h` is the project-overridable header. **In this
install every override in it is commented out**, so the defaults above are in force. The
overrides it offers, all currently inactive: `VOXEL_DOUBLE_PRECISION`, `VOXEL_DEBUG`,
`EIGHT_BITS_VOXEL_VALUE 1`, a single-byte material layout (`VOXEL_MATERIAL_ENABLE_R/G/B
0`, `_A 1`, all UVs `0`), and `VOXEL_MATERIAL_ENABLE_UV2/UV3 1`.

**NEW** — changing any of these is a source-level edit to the plugin, and the save format
records the value configuration: see `EVoxelValueConfigFlag` and `GVoxelValueConfigFlag`
in `VoxelValue.h`, and the save version enumerator
`FVoxelSaveVersion::ValueConfigFlagAndSaveGUIDs` in `VoxelSave.h`.

### 10.3 Core value types

**`FVoxelValue`** (`VoxelValue.h`) — alias of `TVoxelValueImpl<T>`, where `T` is `int16`
when `EIGHT_BITS_VOXEL_VALUE` is `0` (the current setting) and `int8` otherwise.

- Named constructors: `Full()`, `Empty()`, `Special()`, `Precision()`
- Constants: `MAX_VOXELVALUE`, `MIN_VOXELVALUE`, `INVALID_VOXELVALUE`
- Queries: `IsNull()`, `IsEmpty()`, `IsTotallyEmpty()`, `IsTotallyFull()`, `Sign()`,
  `ToFloat()`, `GetInverse()`
- Operators: `==`, `!=`, `<`, `>`, `<=`, `>=`, `+`, `-`, `+=`, `-=` (clamped to storage)

`ToFloat()` is `float(GetStorage()) / float(MAX_VOXELVALUE)` — the normalised signed
density.

**`FVoxelMaterial`** (`VoxelMaterial.h`) — `TVoxelMaterialStorage<T>`; the channel set is
determined by the `VOXEL_MATERIAL_ENABLE_*` macros above (currently R, G, B, A, UV0,
UV1). `TVoxelMaterialStorage<uint32>` is the form used for material indices in saves.

**`FVoxelIntBox`** (`VoxelIntBox.h`) — the bounds type used by every edit and query in
the API; `Min` and `Max` as `FIntVector`. Selected members: `Infinite` (static),
`SafeConstruct(A, B)`, `Size()`, `SizeIs32Bit()`, `IsValid()`,
`Contains(int32,int32,int32)`, `Contains(FIntVector)`, `Contains(FVoxelIntBox)`,
`ContainsTemplate`, `ContainsFloat` (float / `FVector` / `FVoxelVector` / `FBox`),
`Clamp(P)`, `Clamp(FVoxelIntBox)`, `Intersect`, `IsMultipleOf(Step)`,
`MakeMultipleOfBigger`, `MakeMultipleOfSmaller`, `MakeMultipleOfRoundUp`, `Scale`,
`Extend`, `Translate`, `RemoveTranslation`, `GetMurmurHash()`, `Iterate(Step, Lambda)`,
`ParallelSplit(Lambda, bForceSingleThread)`,
`ParallelIterate(Lambda, bForceSingleThread)`, `operator*`, `operator+`, `GetTypeHash`.

`FVoxelIntBoxWithValidity` wraps it with an `IsValid()` flag.

`UVoxelIntBoxLibrary` is the Blueprint surface: `MakeIntBox`, `BreakIntBox`,
`MakeIntBoxWithValidity`, `BreakIntBoxWithValidity`, `InfiniteBox`, `TranslateBox`,
`Center`, `RemoveTranslation`, `Conv_IntBoxToString`,
`MakeBoxFromLocalPositionAndRadius`, `MakeBoxFromPositionAndRadius`,
`IsIntVectorInsideBox`, `IsVectorInsideBox`, `GetSize`, `GetCenter`, `GetCorners`,
`Intersect`, `Contains`, `IsValid`, `Overlap`, `Extend`, `Extend_IntVector`,
`ApplyTransform`, `AddPoint`, `AddBox`, `Scale`, `EqualEqual_IntBoxIntBox`,
`NotEqual_IntBoxIntBox`, `MakeIntBoxFromPoints`.

**`FVoxelVector`** (`VoxelVector.h`) — a `v_flt`-typed vector. `v_flt` is `float` unless
`VOXEL_DOUBLE_PRECISION` is defined; it is not.

**`FModifiedVoxelValue`** (`VoxelTools/Gen/VoxelToolsBase.h`, line 16) — the return
payload of every edit:

```cpp
USTRUCT(BlueprintType)
struct FModifiedVoxelValue
{
    UPROPERTY(...) FIntVector Position = FIntVector(ForceInit);
    UPROPERTY(...) float OldValue = 0;
    UPROPERTY(...) float NewValue = 0;

    FModifiedVoxelValue(const FIntVector& Position, float OldValue, float NewValue);
    FModifiedVoxelValue(const FIntVector& Position, FVoxelValue OldValue, FVoxelValue NewValue);
};
```

**NEW — the struct carries `Position`, `OldValue` and `NewValue` per voxel, and nothing
else.** It does not carry the material at that voxel; material at a position is a
separate query (`UVoxelDataTools::GetMaterial`, section 10.9) or a separate record type
(`FModifiedVoxelMaterial`).

**`FModifiedVoxelMaterial`** (same header, line 47) — `FIntVector Position`,
`FVoxelMaterial OldMaterial`, `FVoxelMaterial NewMaterial`.

**`UVoxelToolsBase`** (same header) — two UFUNCTIONs:
`GetModifiedVoxelValuesBounds(const TArray<FModifiedVoxelValue>&)` → `FVoxelIntBox`,
`GetModifiedVoxelMaterialsBounds(const TArray<FModifiedVoxelMaterial>&)` → `FVoxelIntBox`.

Delegates declared in the same header: `FOnVoxelToolComplete`,
`FOnVoxelToolComplete_WithModifiedValues(const TArray<FModifiedVoxelValue>&)`,
`FOnVoxelToolComplete_WithModifiedMaterials(const TArray<FModifiedVoxelMaterial>&)`.

### 10.4 `AVoxelWorld` — the terrain actor

`Voxel/Public/VoxelWorld.h`. Derives from `AVoxelWorldInterface`.

**Blueprint-assignable delegates:** `OnGenerateWorld`, `OnWorldLoaded`,
`OnWorldDestroyed`, `OnMaxFoliageInstancesReached`.

**Components:** `UVoxelWorldRootComponent* WorldRoot` (accessor `GetWorldRoot()`),
`UVoxelLineBatchComponent* LineBatchComponent` (accessor `GetLineBatchComponent()`).

**UFUNCTIONs:** `CreateWorld` · `DestroyWorld` · `SetGeneratorObject` ·
`SetGeneratorClass` · `SetRenderOctreeDepth` · `SetWorldSize` · `IsCreated` · `IsLoaded` ·
`GlobalToLocal` · `GlobalToLocalFloatBP` · `LocalToGlobal` · `LocalToGlobalFloatBP` ·
`GetNeighboringPositions` · `SetOffset` · `AddOffset` · `K2_GetGeneratorCache` ·
`GetGeneratorInit` · `CreateMultiplayerInterfaceInstance` ·
`GetMultiplayerInterfaceInstance` · `SetCollisionResponseToChannel`.

**Non-UFUNCTION public accessors:** `GetData()` → `FVoxelData&` · `GetDebugManager()` ·
`GetEventManager()` · `GetToolRenderingManager()` · `GetPlayType()` → `EVoxelPlayType` ·
`GetWorldBounds()` → `FVoxelIntBox` · `GetGeneratorInit()` → `FVoxelGeneratorInit`.
Overrides include `BeginPlay` and `EndPlay`.

**Properties, by category** (name — default):

*Save:* `SaveObject` (`UVoxelWorldSaveObject*`) — `nullptr`, line 109, commented
`// Automatically loaded on creation` · `SaveFilePath` (`FString`) ·
`bAutomaticallySaveToFile` — `false` · `bAppendDateToSavePath` — `false`.

*Bake:* `bRecomputeNormalsBeforeBaking` — `false` · `BakedMeshTemplate` ·
`BakedMeshComponentTemplate` · `BakedDataPath` — `/Game/VoxelStaticData`.

*Preview (editor only):* `NumberOfThreadsForPreview` — `2` · `bEnableFoliageInEditor` —
`true` · `bAutomaticallyRefreshMaterials` — `true` · `bAutomaticallyRefreshFoliage` —
`true` · `EditorOnly_NewScale` — `(2,2,2)`.

*General:* `VoxelSize` — `100` · `Generator` (`FVoxelGeneratorPicker`) ·
`PlaceableItemManager` · `Seeds` (`TMap<FName,int32>`, marked DEPRECATED) ·
`bCreateWorldAutomatically` — `false` · `bUseCameraIfNoInvokersFound` — `false` ·
`bEnableUndoRedo` — `false` · `bEnableCustomWorldRebasing` — `false` ·
`bMergeAssetActors` — `true` · `bMergeDisableEditsBoxes` — `true` ·
`bDisableOnScreenMessages` — `false` · `bUseCustomWorldBounds` — `false` ·
`CustomWorldBounds` (`FVoxelIntBox`).

*LOD:* `MaxLOD` — `FVoxelUtilities::ClampMesherDepth(32)` · `MinLOD` — `0` ·
`InvokerDistanceThreshold` — `100` · `MinDelayBetweenLODUpdates` — `0.1` ·
`bConstantLOD` — `false`.

*Materials:* `MaterialConfig` — `EVoxelMaterialConfig::RGB` · `VoxelMaterial` ·
`MaterialCollection` · `LODMaterials` · `LODMaterialCollections` · `UVConfig` —
`EVoxelUVConfig::GlobalUVs` · `UVScale` — `1` · `NormalConfig` —
`EVoxelNormalConfig::GradientNormal` · `RGBHardness` —
`EVoxelRGBHardness::FiveWayBlend` · `MaterialsHardness` (`TMap<FString,float>`) ·
`bHardColorTransitions` — `false` · `bOneMaterialPerCubeSide` — `false` ·
`HolesMaterials` (`TArray<uint8>`) · `MaterialsMeshConfigs`
(`TMap<uint8, FVoxelMeshConfig>`) · `bHalfPrecisionCoordinates` — `false` ·
`bInterpolateColors` — `false` · `bInterpolateUVs` — `false` · `bSRGBColors` — `false`.

*Rendering:* `RenderType` — `EVoxelRenderType::MarchingCubes` · `RenderSharpness` — `0` ·
`bCreateMaterialInstances` — `true` · `bDitherChunks` — `true` ·
`ChunksDitheringDuration` — `1` · `ChunksCullingLOD` —
`FVoxelUtilities::ClampDepth<RENDER_CHUNK_SIZE>(32)` · `bRenderWorld` — `true` ·
`bStaticWorld` — `false` · `bOptimizeIndices` — `false` · `bGenerateDistanceFields` —
`false` · `MaxDistanceFieldLOD` — `4` · `DistanceFieldBoundsExtension` — `4` ·
`DistanceFieldResolutionDivisor` — `1` · `DistanceFieldSelfShadowBias` — `0` ·
`bEnableTransitions` — `true` · `bMergeChunks` — `false` · `ChunksClustersSize` — `64` ·
`bDoNotMergeCollisionsAndNavmesh` — `true` · `BoundsExtension` — `100` ·
`PrimitiveSettings` (`FVoxelPrimitiveComponentSettings`).

*Spawners:* `SpawnerConfig` (`UVoxelSpawnerConfig*`) · `HISMChunkSize` — `2048` ·
`SpawnersCollisionDistanceInVoxel` — `64`. (Stubbed in Free — see 10.14.)

*Collisions:* `bEnableCollisions` — `true` · `CollisionPresets` (`FBodyInstance`) ·
`bNotifyRigidBodyCollision` — `false` · `bGenerateOverlapEvents` — `false` ·
`bComputeVisibleChunksCollisions` — `true` · `VisibleChunksCollisionsMaxLOD` — `5` ·
`PhysMaterialOverride` · `bUseCCD` — `false` · `NumConvexHullsPerAxis` — `2` ·
`bCleanCollisionMeshes` — `true`.

*Navmesh:* `bEnableNavmesh` — `false` · `bComputeVisibleChunksNavmesh` — `true` ·
`VisibleChunksNavmeshMaxLOD` — `0`.

*Threading / performance:* `bCreateGlobalPool` — `true` · `NumberOfThreads` — `2` ·
`PriorityCategories` (`TMap<EVoxelTaskType,int32>`) · `PriorityOffsets` ·
`bConstantPriorities` — `false` · `PriorityDuration` — `0.5` · `MeshUpdatesBudget` —
`1000` · `EventsTickRate` — `15` · `DataOctreeInitialSubdivisionDepth` — `4`.

*Multiplayer:* `bEnableMultiplayer` — `false` · `MultiplayerSyncRate` — `15`.
(Stubbed in Free — see 10.12.)

**`FVoxelWorldCreateInfo`** (`VoxelWorldCreateInfo.h`) — the required argument to
`CreateWorld`:

```cpp
USTRUCT(BlueprintType)
struct FVoxelWorldCreateInfo
{
    UPROPERTY(...) bool bOverrideSave = false;
    UPROPERTY(...) FVoxelUncompressedWorldSave SaveOverride;
    UPROPERTY(...) bool bOverrideData = false;
    UPROPERTY(...) TObjectPtr<AVoxelWorld> DataOverride = nullptr;
    TVoxelSharedPtr<FVoxelData> DataOverride_Raw;   // not a UPROPERTY
};
```

**NEW — `bOverrideData` / `DataOverride` / `DataOverride_Raw` allow a world to be created
on an existing `FVoxelData` instance instead of a fresh one.** `VoxelWorld.cpp` emits
`"Info.bOverrideData is true, but DataOverride is null!"` when it is set without a
source, and `"Cannot use Info.bOverrideSave if Info.bOverrideData is true!"` when both
are set — the two paths are mutually exclusive.

Related actors and components: **`AVoxelWorldInterface`** (`VoxelWorldInterface.h`), the
base actor · **`UVoxelWorldRootComponent`** (`VoxelWorldRootComponent.h`), a
`UPrimitiveComponent` overriding `GetBodySetup()`, `CreateSceneProxy()` and
`CalcBounds()` · **`AVoxelStaticWorld`** (`VoxelStaticWorld.h`), a plain `AActor` ·
**`AVoxelCharacter`** (`VoxelCharacter.h`), an `ACharacter` subclass shipped by the
plugin.

### 10.5 Invokers — `VoxelComponents/VoxelInvokerComponent.h`

Five classes:

**`UVoxelInvokerComponentBase : USceneComponent`**
- Properties: `bUseForEvents` — `true` · `bUseForPriorities` — `true` · `bStartsEnabled`
  — `true` · `bIsInvokerEnabled` — `false`
- `BlueprintNativeEvent`s: `IsLocalInvoker() const` → `bool` ·
  `GetInvokerVoxelPosition(AVoxelWorldInterface*) const` → `FIntVector` ·
  `GetInvokerSettings(AVoxelWorldInterface*) const` → `FVoxelInvokerSettings`
- Native implementations: `IsLocalInvoker_Implementation`,
  `GetInvokerVoxelPosition_Implementation`, `GetInvokerSettings_Implementation`
- `BlueprintCallable`: `EnableInvoker()` · `DisableInvoker()` · `IsInvokerEnabled()` ·
  `static RefreshAllVoxelInvokers()`
- Statics: `GetInvokers(UWorld*)` →
  `const TArray<TWeakObjectPtr<UVoxelInvokerComponentBase>>&` ·
  `FSimpleMulticastDelegate OnForceRefreshInvokers` ·
  `TMap<TWeakObjectPtr<UWorld>, TArray<TWeakObjectPtr<UVoxelInvokerComponentBase>>> Components`
- Overrides `OnRegister()` / `OnUnregister()`

**`UVoxelSimpleInvokerComponent : UVoxelInvokerComponentBase`**
- LOD: `bUseForLOD` — `true` · `LODToSet` (clamped 0–26) · `LODRange` — `1000`
- Collisions: `bUseForCollisions` — `true` · `CollisionsRange` — `1000`
- Navmesh: `bUseForNavmesh` — `true` · `NavmeshRange` — `1000`
- `BlueprintNativeEvent` `GetInvokerGlobalPosition() const` → `FVector`, with
  `GetInvokerGlobalPosition_Implementation`

**`UVoxelInvokerWithPredictionComponent : UVoxelSimpleInvokerComponent`** —
`bEnablePrediction` — `false` · `PredictionTime` — `1`; overrides
`GetInvokerGlobalPosition_Implementation`.

**`UVoxelInvokerAutoCameraComponent : UVoxelSimpleInvokerComponent`** — overrides
`GetInvokerGlobalPosition_Implementation`.

**`UVoxelLODVolumeInvokerComponent : UVoxelInvokerComponentBase`** — `bUseForLOD` —
`true`, `LODToSet`; used by `AVoxelLODVolume`.

`FVoxelInvokerSettings` is declared in `VoxelInvokerSettings.h`.

**NEW — `IsLocalInvoker` is a `BlueprintNativeEvent`,** i.e. the "is this invoker the
local player's" test is an overridable per-invoker predicate rather than a fixed engine
rule. The runtime consequence of having no invoker in a networked session is recorded in
section 9, `T-101A_FINDINGS.md` 2d.

### 10.6 Rendering

**`UVoxelProceduralMeshComponent : UPrimitiveComponent`**
(`VoxelRender/VoxelProceduralMeshComponent.h`) — the component the terrain is drawn and
collided with.
- `Init(int32 InDebugLOD, ...)`, `ClearInit()`
- Properties: `LOD` — `0` · `PriorityDuration` — `0` · `NumConvexHullsPerAxis` — `2` ·
  `DistanceFieldSelfShadowBias` — `0`
- `BlueprintImplementableEvent`: `InitChunk(uint8 ChunkLOD, FVoxelIntBox ChunkBounds)`
- Section management: `AddProcMeshSection(FVoxelProcMeshSectionSettings, TUniquePtr<FVoxelProcMeshBuffers>, EVoxelProcMeshSectionUpdate)` ·
  `SetProcMeshSection(int32 Index, ...)` · `ReplaceProcMeshSection(...)` ·
  `ClearSections(EVoxelProcMeshSectionUpdate)` · `FinishSectionsUpdates()`
- Collision / navigation: `UpdatePhysicalMaterials()` · `UpdateLocalBounds()` ·
  `UpdateNavigation()` · `UpdateCollision()` · `FinishCollisionUpdate()` ·
  `PhysicsCookerCallback(uint64 CookerId)`
- `SetDistanceFieldData(const TVoxelSharedPtr<const FDistanceFieldVolumeData>&)`

Related: `IVoxelRenderer`, `IVoxelLODManager`,
`IVoxelProceduralMeshComponent_PhysicsCallbackHandler`, `FVoxelChunkMeshBuffers`,
`FVoxelProcMeshBuffers`, `FVoxelProcMeshSectionSettings`, `FVoxelProcMeshTangent`,
`FVoxelRawStaticIndexBuffer`, `FVoxelChunkToUpdate`, `FVoxelMaterialInterface`,
`FVoxelMaterialInterfaceManager`, `FVoxelMaterialIndices`, `FVoxelLODMaterials`,
`FVoxelLODMaterialCollections`, `FVoxelMeshConfig`, `FVoxelToolRenderingManager`,
`FVoxelMesherAsyncWork`.

### 10.7 The data layer

**`FVoxelDataSettings`** (`VoxelData/VoxelData.h`, line 74) — all `const`:

```cpp
struct VOXEL_API FVoxelDataSettings
{
    const int32 Depth;
    const FVoxelIntBox WorldBounds;
    const TVoxelSharedRef<FVoxelGeneratorInstance> Generator;
    const bool bEnableMultiplayer;
    const bool bEnableUndoRedo;

    FVoxelDataSettings(const AVoxelWorld* World, EVoxelPlayType PlayType);
    FVoxelDataSettings(int32 Depth, const TVoxelSharedRef<FVoxelGeneratorInstance>&,
                       bool bEnableMultiplayer, bool bEnableUndoRedo);
    FVoxelDataSettings(const FVoxelIntBox& WorldBounds,
                       const TVoxelSharedRef<FVoxelGeneratorInstance>&,
                       bool bEnableMultiplayer, bool bEnableUndoRedo);
};
```

**`FVoxelData : IVoxelData, TVoxelSharedFromThis<FVoxelData>`** (line 98) — the
authoritative in-memory voxel store. The constructor is private; construction is through
statics.

- Lifecycle: `static Create(const FVoxelDataSettings&, int32 DataOctreeInitialSubdivisionDepth = 0)`
  → `TVoxelSharedRef<FVoxelData>` · `Clone() const` ("Clone without keeping the voxel
  data") · destructor
- Geometry: `Size()` = `DATA_CHUNK_SIZE << Depth` · `GetOctree()` →
  `FVoxelDataOctreeBase&` · `IsInWorld(X,Y,Z)` / `IsInWorld(P)` · `ClampToWorld(...)`
- **Locking:** `Lock(EVoxelLockType LockType, const FVoxelIntBox& Bounds, FName Name) const`
  → `TUniquePtr<FVoxelDataLockInfo>` · `Unlock(TUniquePtr<FVoxelDataLockInfo>) const`.
  A `MainLock` (`FVoxelSharedMutex`) is held as read while any bounds lock is held, and
  as write to clear the octree.
- Bulk data: `ClearData()` · `ClearOctreeData(TArray<FVoxelIntBox>& OutBoundsToUpdate)` ·
  `CacheBounds(const FVoxelIntBox&, bool bMultiThreaded)` ·
  `ClearCacheInBounds(const FVoxelIntBox&)` · `CheckIsSingle(const FVoxelIntBox&)`
- Access: `Get<T>(TVoxelQueryZone<T>&, int32 LOD) const` ·
  `IsEmpty(const FVoxelIntBox&, int32 LOD) const` · `Set<T>(const FVoxelIntBox&, F Apply)` ·
  `ParallelSet<T>(const FVoxelIntBox&, F Apply, bool bForceSingleThread = false)` ·
  `Set<T>(int32 X, int32 Y, int32 Z, const T& Value)`
- **Persistence:** `GetSave(FVoxelUncompressedWorldSaveImpl& OutSave, TArray<FVoxelObjectArchiveEntry>& OutObjects)` ·
  `LoadFromSave(const FVoxelUncompressedWorldSaveImpl& Save, const FVoxelPlaceableItemLoadInfo& LoadInfo, TArray<FVoxelIntBox>* OutBoundsToUpdate = nullptr)` → `bool`
- **Undo / redo:** `Undo(TArray<FVoxelIntBox>& OutBoundsToUpdate)` → `bool` ·
  `Redo(TArray<FVoxelIntBox>& OutBoundsToUpdate)` → `bool` · `ClearFrames()` ·
  `SaveFrame(const FVoxelIntBox& Bounds)` · `IsCurrentFrameEmpty()` → `bool`
- Items: `RemoveItem(TVoxelWeakPtr<TVoxelDataItemWrapper<T>>&, FString& OutError)` ·
  `AddItemToLeafData(...)` · `RemoveItemFromLeafData(...)` ·
  `MigrateLeafDataToNewGenerator(...)`

**Octree types:** `FVoxelDataOctreeBase`, `FVoxelDataOctreeLeaf`,
`FVoxelDataOctreeParent` (`VoxelData/VoxelDataOctree.h`),
`FVoxelDataOctreeLeafData` (`VoxelDataOctreeLeafData.h`).

**`FVoxelDataOctreeLeafUndoRedo`** (`VoxelDataOctreeLeafUndoRedo.h`, line 23) — with
inner `TModifiedValue`, `FFrame`, `FAlreadyModified`. **NEW — undo/redo history is stored
per leaf as a stack of frames of modified values, not globally.**

**`FVoxelDataOctreeLeafMultiplayer`** (`VoxelDataOctreeLeafMultiplayer.h`) — with inner
`FDirty` and `TEmptyArray`. **NEW — per-leaf dirty tracking for the plugin's own
multiplayer diffing exists as a type, but its consumer (`VoxelMultiplayerTcp`) is stubbed
in Free (10.12).** `FVoxelDiff` is declared in `VoxelDiff.h`.

**`FVoxelDataAccelerator`** (`VoxelDataAccelerator.h`) — a caching read/write cursor over
`FVoxelData`; `GetFloatValue(X, Y, Z, LOD, bool* bIsGeneratorValue = nullptr)`,
`SetImpl(X, Y, Z, TLambda EditValue)`, `StoreOctreeInCache(FVoxelDataOctreeBase&)`.
The `bIsGeneratorValue` out-parameter reports whether the value read came from the
generator rather than from stored data.

**`FVoxelDataLockInfo`** and `TVoxelScopeLock` (`VoxelData/VoxelDataLock.h`) — the RAII
lock handle and scope guard; `FVoxelDataLockInfo` is non-copyable and `friend class
FVoxelData`. `EVoxelLockType` itself is declared in `VoxelSharedMutex.h`.
**`TVoxelQueryZone<T>`** (`VoxelQueryZone.h`) — the strided query window passed to
generators.
**`FVoxelItemStack`** (`VoxelItemStack.h`) — the placeable-item stack passed through
every generator query.

### 10.8 Generators

**`UVoxelGenerator : UObject`** (`VoxelGenerators/VoxelGenerator.h`, line 18):

```cpp
virtual void ApplyParameters(const TMap<FName, FString>& Parameters);
virtual void GetParameters(TArray<FVoxelGeneratorParameter>& OutParameters) const;
virtual TVoxelSharedRef<FVoxelGeneratorInstance> GetInstance(const TMap<FName, FString>& Parameters);
virtual TVoxelSharedRef<FVoxelGeneratorInstance> GetInstance();
```

**`UVoxelTransformableGenerator : UVoxelGenerator`** (line 37) — adds
`GetTransformableInstance(...)`; overrides `GetInstance`.
**`UVoxelTransformableGeneratorWithBounds : UVoxelTransformableGenerator`** (line 54) —
adds `virtual FVoxelIntBox GetBounds() const`.

**`FVoxelGeneratorInstance : TVoxelSharedFromThis<FVoxelGeneratorInstance>`**
(`VoxelGeneratorInstance.h`, line 24) — the runtime object the data layer queries:

```cpp
virtual void Init(const FVoxelGeneratorInit& InitStruct) {}
virtual void InitArea(const FVoxelIntBox& Bounds, int32 LOD) {}
virtual void SetupMaterialInstance(int32 ChunkLOD, const FVoxelIntBox& ChunkBounds,
                                   UMaterialInstanceDynamic* Instance) {}

virtual void GetValues   (TVoxelQueryZone<FVoxelValue>&    QueryZone, int32 LOD,
                          const FVoxelItemStack& Items) const = 0;
virtual void GetMaterials(TVoxelQueryZone<FVoxelMaterial>& QueryZone, int32 LOD,
                          const FVoxelItemStack& Items) const = 0;
virtual FVector GetUpVector(v_flt X, v_flt Y, v_flt Z) const = 0;
```

Non-virtual helpers: `GetValue(X,Y,Z,LOD,Items)` → `v_flt` ·
`GetMaterial(X,Y,Z,LOD,Items)` → `FVoxelMaterial` ·
`GetValueRange(const FVoxelIntBox&, LOD, Items)` → `TVoxelRange<v_flt>` ·
`Get<T>(...)` in point, `FIntVector` and `TVoxelQueryZone<T>` forms ·
`GetCustomOutput<T>(T DefaultValue, FName Name, ...)` ·
`GetCustomOutputRange<T>(...)`.
Function-pointer tables `FBaseFunctionPtrs` and `FCustomFunctionPtrs` are members; the
instance stores `const TSubclassOf<UVoxelGenerator> Class`.

**`FVoxelTransformableGeneratorInstance : FVoxelGeneratorInstance`** (line 126) — the
same surface with a leading `const FTransform& LocalToWorld`:
`GetValues_Transform`, `GetMaterials_Transform`, `GetValue_Transform`,
`GetMaterial_Transform`, `GetValueRange_Transform`, `Get_Transform`,
`GetCustomOutput_Transform`, `GetCustomOutputRange_Transform`, plus
`FBaseFunctionPtrs_Transform` / `FCustomFunctionPtrs_Transform`.

**Three concrete generators exist as C++ classes:** `UVoxelFlatGenerator`,
`UVoxelEmptyGenerator`, and `UVoxelGraphGenerator` (in the `VoxelGraph` module). The
third is stubbed in Free — see 10.14. Neither `VoxelGenerator.h`,
`VoxelGeneratorInstance.h`, `VoxelFlatGenerator.h` nor `VoxelEmptyGenerator.h` declares
any `UFUNCTION`.

Supporting types: `FVoxelGeneratorInit` (`VoxelGeneratorInit.h`),
`FVoxelGeneratorPicker` / `FVoxelTransformableGeneratorPicker`
(`VoxelGeneratorPicker.h`), `FVoxelGeneratorParameter`,
`FVoxelGeneratorParameterType`, `FVoxelGeneratorParameterTerminalType`
(`VoxelGeneratorParameters.h`), `UVoxelGeneratorInstanceWrapper` /
`UVoxelTransformableGeneratorInstanceWrapper` (`VoxelGeneratorInstanceWrapper.h`),
`TVoxelGeneratorHelper` (`VoxelGeneratorHelpers.h`),
`TVoxelTransformableGeneratorHelper` (`VoxelTransformableGeneratorHelper.h`).

**`UVoxelGeneratorCache`** (`VoxelGeneratorCache.h`) — UFUNCTIONs
`MakeGeneratorInstance`, `MakeTransformableGeneratorInstance`. Reached from
`AVoxelWorld::K2_GetGeneratorCache`.

**`UVoxelGeneratorTools`** (`VoxelGeneratorTools.h`) — `MakeGeneratorInstance` ·
`MakeTransformableGeneratorInstance` · `MakeGeneratorPickerFromObject` ·
`MakeTransformableGeneratorPickerFromObject` · `MakeGeneratorPickerFromClass` ·
`MakeTransformableGeneratorPickerFromClass` · `IsValid_GeneratorPicker` ·
`IsValid_TransformableGeneratorPicker` · `SetGeneratorParameter` ·
`SetTransformableGeneratorParameter` · `CreateFloatTextureFromGenerator` (+`Async`) ·
`CreateColorTextureFromGenerator` (+`Async`).

### 10.9 Edit tools

All edit tools live in `VoxelTools/`. The generated Blueprint layer is
`VoxelTools/Gen/`; the templated C++ implementation layer is `VoxelTools/Impl/`
(`FVoxelToolsBaseImpl`, `FVoxelSphereToolsImpl`, `FVoxelBoxToolsImpl`,
`FVoxelLevelToolsImpl`, `FVoxelSurfaceEditToolsImpl`, plus `.inl` files).

**`UVoxelSphereTools`** (`Gen/VoxelSphereTools.h`) — eleven operations, each in three
overloads (Blueprint synchronous, Blueprint latent `...Async`, and a C++ form with
optional out-parameters):

`SetValueSphere` · `RemoveSphere` · `AddSphere` · `SetMaterialSphere` ·
`ApplyKernelSphere` · `ApplyMaterialKernelSphere` · `SmoothSphere` ·
`SmoothMaterialSphere` · `TrimSphere` · `RevertSphere` · `RevertSphereToGenerator`
— and `SetValueSphereAsync`, `RemoveSphereAsync`, `AddSphereAsync`,
`SetMaterialSphereAsync`, `ApplyKernelSphereAsync`, `ApplyMaterialKernelSphereAsync`,
`SmoothSphereAsync`, `SmoothMaterialSphereAsync`, `TrimSphereAsync`,
`RevertSphereAsync`, `RevertSphereToGeneratorAsync`.

The three shapes, using `SetValueSphere` as the representative (`RemoveSphere` and
`AddSphere` are identical minus the `Value` parameter):

```cpp
// 1. Blueprint synchronous
UFUNCTION(BlueprintCallable, Category = "Voxel|Tools|Sphere Tools",
          meta = (DefaultToSelf = "VoxelWorld",
                  AdvancedDisplay = "bMultiThreaded, bRecordModifiedValues, bConvertToVoxelSpace, bUpdateRender"))
static void SetValueSphere(
    TArray<FModifiedVoxelValue>& ModifiedValues,
    FVoxelIntBox& EditedBounds,
    AVoxelWorld* VoxelWorld,
    const FVector& Position,
    float Radius,
    float Value,
    bool bMultiThreaded = true,
    bool bRecordModifiedValues = true,
    bool bConvertToVoxelSpace = true,
    bool bUpdateRender = true);

// 2. Blueprint latent (background thread)
UFUNCTION(BlueprintCallable, ..., meta = (Latent, LatentInfo = "LatentInfo",
                                          WorldContext = "WorldContextObject", ...))
static void SetValueSphereAsync(
    UObject* WorldContextObject,
    FLatentActionInfo LatentInfo,
    TArray<FModifiedVoxelValue>& ModifiedValues,
    FVoxelIntBox& EditedBounds,
    AVoxelWorld* VoxelWorld,
    const FVector& Position,
    float Radius,
    float Value,
    bool bMultiThreaded = false,      // NOTE: default differs from the sync form
    bool bRecordModifiedValues = true,
    bool bConvertToVoxelSpace = true,
    bool bUpdateRender = true,
    bool bHideLatentWarnings = false);

// 3. C++ form — out-parameters optional, "Will append to existing values"
static void SetValueSphere(
    AVoxelWorld* VoxelWorld,
    const FVector& Position,
    float Radius,
    float Value,
    TArray<FModifiedVoxelValue>* OutModifiedValues = nullptr,
    FVoxelIntBox* OutEditedBounds = nullptr,
    bool bMultiThreaded = true,
    bool bConvertToVoxelSpace = true,
    bool bUpdateRender = true);
```

Header documentation for the shared parameters, quoted verbatim:

| Parameter | Header comment |
|---|---|
| `ModifiedValues` | "Record the Values modified by this function. Useful to track the amount of edit done, for instance to give resources when digging" |
| `EditedBounds` | "Returns the bounds edited by this function" |
| `Position` | "The position of the center. In world space (unreal units) if bConvertToVoxelSpace is true. In voxel space if false." |
| `Radius` | "The radius. In unreal units if bConvertToVoxelSpace is true. In voxels if false." |
| `bMultiThreaded` | "If true, multiple threads will be used to make the edit faster." |
| `bRecordModifiedValues` | "If false, will not fill ModifiedValues, making the edit faster." |
| `bConvertToVoxelSpace` | "If true, Position and Radius will be converted to voxel space. Else they will be used directly." |
| `bUpdateRender` | "If false, will only edit the data and not update the render. Rarely needed." |
| `bHideLatentWarnings` | "Hide latent warnings caused by calling a node before its previous call completion." |
| `OutModifiedValues` (C++ form) | "Optional. … Will append to existing values." |

**NEW — three facts visible only in the signatures.** (1) `bMultiThreaded` defaults to
`true` in the synchronous form and `false` in the `...Async` form. (2) `bUpdateRender`
allows a data-only edit with no render update. (3) The C++ overload *appends* to
`OutModifiedValues` rather than replacing its contents.

**`UVoxelBoxTools`** (`Gen/VoxelBoxTools.h`) — `SetValueBox` · `AddBox` · `RemoveBox` ·
`SetMaterialBox`, each with an `...Async` variant and the same three-overload pattern.

**`UVoxelLevelTools`** (`Gen/VoxelLevelTools.h`) — `Level` · `LevelAsync` (flatten).

**`UVoxelSurfaceTools`** (`VoxelSurfaceTools.h`) — surface-voxel discovery and a
composable edit stack: `FindSurfaceVoxels` (+`Async`) ·
`FindSurfaceVoxelsFromDistanceField` · `FindSurfaceVoxels2D` (+`Async`) · `AddToStack` ·
`ApplyStack` (+`Async`) · `GetBounds` · `ApplyConstantStrength` · `ApplyStrengthCurve` ·
`ApplyFalloff` · `ApplyStrengthMask` · `GetStrengthMaskScale` · `ApplyTerrace` ·
`ApplyFlatten` · `DebugSurfaceVoxels`. Types in `VoxelSurfaceEdits.h`:
`FVoxelSurfaceEditsStack`, `FVoxelSurfaceEditsStackElement`,
`FVoxelSurfaceEditToolsImpl`. (`ApplyStrengthMask` and `GetStrengthMaskScale` are
stubbed in Free — see 10.14.)

**`UVoxelAssetTools`** (`VoxelAssetTools.h`) — `ImportAssetAsReference` (+`Async`) ·
`ImportModifierAsset` (+`Async`) · `ImportAsset` (+`Async`) · `ImportDataAssetFast`
(+`Async`) · `InvertDataAsset` · `SetDataAssetMaterial` ·
`CreateDataAssetFromWorldSection` · `AddDisableEditsBox` (+`Async`).

**`UVoxelProjectionTools`** (`VoxelProjectionTools.h`) — `MakeVoxelLineTraceParameters` ·
`FindProjectionVoxels` (+`Async`) · `GetHitsPositions` · `GetHitsAverageNormal` ·
`GetHitsAveragePosition` · `CreateSurfaceVoxelsFromHits` ·
`CreateSurfaceVoxelsFromHitsWithExactValues`.

**`UVoxelPhysicsTools`** (`VoxelPhysics.h`) — `ApplyVoxelPhysics` (stubbed in Free, 10.14).
Related: `IVoxelPhysicsPartSpawner`, `IVoxelPhysicsPartSpawnerResult`,
`UVoxelPhysicsPartSpawner_Cubes` / `_GetVoxels` / `_VoxelWorlds` and their `Result`
counterparts.

**`UVoxelToolManager`** (`VoxelToolManager.h`) — an interactive-tool framework:
`K2_GetSharedConfig` · `GetActiveTool` · `GetTools` · `CreateDefaultTools` ·
`SetActiveTool` · `SetActiveToolByClass` · `SetActiveToolByName`. Tool classes:
`UVoxelTool`, `UVoxelToolBase`, `UVoxelToolWithAlignment`, `UVoxelSphereToolBase`,
`UVoxelSphereTool`, `UVoxelSmoothTool`, `UVoxelSurfaceTool`, `UVoxelTrimTool`,
`UVoxelFlattenTool`, `UVoxelLevelTool`, `UVoxelMeshTool`, `UVoxelRevertTool`,
`UVoxelToolSharedConfig`, `UVoxelToolLibrary`.

**`FVoxelPaintMaterial`** (`VoxelPaintMaterial.h`) — the material payload for
`SetMaterial*` operations; constructed via the `UVoxelBlueprintLibrary::Create*PaintMaterial`
family (10.11).

**`FVoxelHardnessHandler`** (`VoxelHardnessHandler.h`) — reads `AVoxelWorld`'s
`MaterialsHardness` map and `RGBHardness` setting. No UFUNCTIONs.

### 10.10 Data queries and persistence — `UVoxelDataTools`

`VoxelTools/VoxelDataTools.h`. All are `UFUNCTION`s on a Blueprint function library.

**Per-voxel value and material** — note that positions here are **voxel-space
`FIntVector`**, unlike the edit tools, which take world-space `FVector` by default:

```cpp
static void GetValue(float& Value, AVoxelWorld* World, FIntVector Position);
static void GetInterpolatedValue(float& Value, AVoxelWorld* World, /* FVector */ ...);
static void SetValue(AVoxelWorld* World, FIntVector Position, float Value);
static void GetMaterial(FVoxelMaterial& Material, AVoxelWorld* World, FIntVector Position);
static void SetMaterial(AVoxelWorld* World, FIntVector Position, /* FVoxelMaterial */ ...);
```

Async variants: `GetValueAsync`, `SetValueAsync`, `GetMaterialAsync`,
`SetMaterialAsync`.

**Bulk / caching:** `CacheValues` · `CacheMaterials` · `ClearCachedValues` ·
`ClearCachedMaterials` · `GetVoxelsValueAndMaterial` · `RoundVoxels` ·
`ClearUnusedMaterials` · `CheckForSingleValues` · `CheckForSingleMaterials` ·
`SetBoxAsDirty` · `FindClosestNonEmptyVoxel` · `RoundToGenerator` ·
`CheckIfSameAsGenerator` · `CompressIntoHeightmap` · `GetDataMemoryUsageInMB` — each of
these except the last two has an `...Async` variant.

**NEW — `CheckIfSameAsGenerator` and `RoundToGenerator` exist.** They compare stored
data against what the generator would produce, and reset data to the generator's output
respectively.

**Persistence:**

```cpp
static void GetSave(AVoxelWorld* World, FVoxelUncompressedWorldSave& OutSave);
static void GetSave(AVoxelWorld* World, FVoxelUncompressedWorldSaveImpl& OutSave,
                    TArray<FVoxelObjectArchiveEntry>& OutObjects);
static void GetCompressedSave(AVoxelWorld* World, FVoxelCompressedWorldSave& OutSave);
static void GetCompressedSave(AVoxelWorld* World, FVoxelCompressedWorldSaveImpl& OutSave,
                              TArray<FVoxelObjectArchiveEntry>& OutObjects);
static void GetSaveAsync(UObject* WorldContextObject, FLatentActionInfo LatentInfo, ...);
static void GetCompressedSaveAsync(...);

static bool LoadFromSave(const AVoxelWorld* World, const FVoxelUncompressedWorldSave& Save);
static bool LoadFromSave(const AVoxelWorld* World, const FVoxelUncompressedWorldSaveImpl& Save,
                         const TArray<FVoxelObjectArchiveEntry>& Objects);
static bool LoadFromCompressedSave(const AVoxelWorld* World,
                                   const FVoxelCompressedWorldSave& Save);
```

**NEW — every save entry point takes an `AVoxelWorld*` and no bounds argument.** There is
no per-chunk or per-region save or load function on this library; the unit is the whole
world. (`FVoxelData::GetSave` / `LoadFromSave`, section 10.7, is likewise whole-data;
`LoadFromSave` there does accept an `OutBoundsToUpdate` array, which reports which
regions changed.)

### 10.11 `UVoxelBlueprintLibrary`

`VoxelTools/VoxelBlueprintLibrary.h` — the general-purpose library. Grouped by subject:

*Licence / diagnostics:* `IsVoxelPluginPro` · `RaiseInfo` · `RaiseWarning` · `RaiseError` ·
`NumberOfCores` · `GetMemoryUsageInMB` · `GetPeakMemoryUsageInMB` · `LogMemoryStats` ·
`GetEstimatedCollisionsMemoryUsageInMB`.

**NEW — `IsVoxelPluginPro()` exists as a runtime query,** so the Pro/Free distinction is
introspectable at runtime rather than only observable through failures.

*Coordinates and world lookup:* `TransformGlobalBoxToVoxelBox` ·
`TransformVoxelBoxToGlobalBox` · `GetVoxelWorldContainingPosition` ·
`GetAllVoxelWorldsContainingPosition` · `GetVoxelWorldOverlappingBox` ·
`GetAllVoxelWorldsOverlappingBox` · `GetVoxelWorldOverlappingActor` ·
`GetAllVoxelWorldsOverlappingActor` · `MakeIntBoxFromGlobalPositionAndRadius` ·
`GetRenderBoundsOverlappingDataBounds` · `AddNeighborsToSet` · the `IntVector` operator
family (`Add_IntVectorIntVector`, `Substract_IntVectorIntVector`,
`Multiply_IntVectorIntVector`, `Divide_IntVectorInt`, `Multiply_IntVectorInt`,
`Multiply_IntIntVector`, `GetMax_Intvector`, `GetMin_Intvector`).

*Spawners / foliage:* `SpawnVoxelSpawnerActorsInArea` ·
`SpawnVoxelSpawnerActorByInstanceIndex` · `AddInstances` · `RegenerateSpawners` ·
`MarkSpawnersDirty` · `GetSpawnersSave` · `LoadFromSpawnersSave` ·
`IsVoxelWorldFoliageLoading` · `RecreateSpawners`. (All stubbed in Free — 10.14.)

*Undo / redo:* `Undo` · `Redo` · `SaveFrame` · `ClearFrames` · `GetHistoryPosition`.

*Generator outputs:* `GetNormal` · `GetFloatOutput` · `GetIntOutput` · `GetBounds`.

*Data lifecycle:* `ClearAllData` · `ClearValueData` · `ClearMaterialData` ·
`HasValueData` · `HasMaterialData` · `ClearDirtyData` · `ScaleData`.

*Update / rebuild:* `UpdatePosition` · `UpdateBounds` · `UpdateAll` · `ApplyLODSettings` ·
`AreCollisionsEnabled` · `GetTaskCount` · `IsVoxelWorldMeshLoading` ·
`ApplyNewMaterials` · `RecreateRender` · `RecreateSpawners` · **`Recreate`**.

*Events:* `BindVoxelChunkEvents` · `BindVoxelGenerationEvent` · `IsValidRef`.

*Tool rendering (brush preview):* `CreateToolRendering` · `DestroyToolRendering` ·
`SetToolRenderingMaterial` · `SetToolRenderingBounds` · `SetToolRenderingEnabled`.

*Thread pools:* `CreateGlobalVoxelThreadPool` · `DestroyGlobalVoxelThreadPool` ·
`IsGlobalVoxelPoolCreated` · `CreateWorldVoxelThreadPool` ·
`DestroyWorldVoxelThreadPool` · `IsWorldVoxelPoolCreated`.

*Paint materials:* `CreateColorPaintMaterial` · `CreateFiveWayBlendPaintMaterial` ·
`CreateSingleIndexPaintMaterial` · `CreateMultiIndexPaintMaterial` ·
`CreateMultiIndexWetnessPaintMaterial` · `CreateMultiIndexRawPaintMaterial` ·
`CreateUVPaintMaterial` · `ApplyPaintMaterial` · `GetColor` · `GetSingleIndex` ·
`GetMultiIndex` · `GetUV` · `GetRawMaterial` · `MakeRawMaterial` · `MakeMaterialMask`.

*Voxel textures:* `CreateOrUpdateTextureFromVoxelFloatTexture` ·
`CreateTextureFromVoxelFloatTexture` · `CreateVoxelFloatTextureFromTextureChannel` ·
`CreateOrUpdateTextureFromVoxelColorTexture` · `CreateTextureFromVoxelColorTexture` ·
`CreateVoxelColorTextureFromVoxelFloatTexture` · `GetVoxelFloatTextureSize` ·
`GetVoxelColorTextureSize` · `IsVoxelFloatTextureValid` · `IsVoxelColorTextureValid`.

### 10.12 Save types and multiplayer types

**`FVoxelSaveVersion`** (`VoxelData/VoxelSave.h`, lines 15–40) — an `enum Type : int32`
with `LatestVersion = VersionPlusOne - 1`. Enumerators in order:
`BeforeCustomVersionWasAdded`, `PlaceableItemsInSave`,
`SHARED_AssetItemsImportValueMaterials`, `SHARED_DataAssetScale`,
`SHARED_RemoveVoxelGrass`, `SHARED_DataAssetTransform`,
`RemoveEnableVoxelSpawnedActorsEnableVoxelGrass`, `FoliagePaint`,
`ValueConfigFlagAndSaveGUIDs`, `SingleValues`,
`SHARED_NoVoxelMaterialInHeightmapAssets`, `SHARED_FixMissingMaterialsInHeightmapAssets`,
`AddUserFlagsToSaves`, `SHARED_StoreSpawnerMatricesRelativeToComponent`,
`StoreMaterialChannelsIndividuallyAndRemoveFoliage`,
`ProperlySerializePlaceableItemsObjects`.

**`FVoxelUncompressedWorldSaveImpl`** (line 42) — public surface:
`GetDepth()` · `GetGuid()` · `HasValues()` · `HasMaterials()` ·
`ApplyCustomFixes(TLambda)` · `SetUserFlags(uint64)` · `GetUserFlags()` ·
`GetAllocatedSize()` · `UpdateAllocatedSize()` · `Serialize(FArchive&)` ·
`operator==` (compares `Guid` only).

Private layout — **NEW, and the closest thing in the plugin to a chunk-addressed
format**:

```cpp
struct FVoxelChunkSave
{
    FIntVector Position;
    int32 ValuesIndex = -1;
    int32 MaterialsIndex = -1;   // index into MaterialsIndices
    bool  bSingleValue = false;
};
static constexpr uint32 MaterialIndexSingleValueFlag = 1u << 31;

int32   Version = -1;
FGuid   Guid;
int32   Depth = -1;
uint64  UserFlags = 0;

TNoGrowArray<FVoxelValue>                    ValueBuffers;
TNoGrowArray<FVoxelValue>                    SingleValues;
TNoGrowArray<TVoxelMaterialStorage<uint32>>  MaterialsIndices;
TNoGrowArray<uint8>                          MaterialBuffers;
TNoGrowArray<uint8>                          SingleMaterials;
TNoGrowArray<FVoxelChunkSave>                Chunks;
TArray<uint8>                                PlaceableItems;
```

`friend class FVoxelSaveBuilder; friend class FVoxelSaveLoader;` — these two are the
only writers.

`ApplyCustomFixes` is documented in the header as the migration hook, with a worked
example of using `UserFlags` as a caller-defined version tag: *"Use in combination with
SetUserFlags to do custom fixes (note that the plugin handles loading values/materials
saved with different defines on its own)."*

**`FVoxelCompressedWorldSaveImpl`** (line 172) — `GetDepth()`, `Serialize(FArchive&)`,
`UpdateAllocatedSize()`, `operator==` (by `Guid`). Private: `int32 Version`, `FGuid Guid`,
`int32 Depth`, `TArray<uint8> CompressedData`; `friend class UVoxelSaveUtilities`.

**`FVoxelUncompressedWorldSave` / `FVoxelCompressedWorldSave`** (lines 206, 219) —
`USTRUCT(BlueprintType)` wrappers over the `Impl` types, described in the header as
"Blueprint wrapper that's cheap to copy around", each carrying
`TArray<FVoxelObjectArchiveEntry> Objects`.

**`UVoxelWorldSaveObject : UObject`** (line 239) — `UCLASS(Blueprintable)` with
`FVoxelCompressedWorldSave Save`, `int32 Depth`, `PostLoad()`, `CopyDepthFromSave()`.
This is the asset type `AVoxelWorld::SaveObject` points at.

**`UVoxelSaveUtilities`** (`VoxelData/VoxelSaveUtilities.h`) — `CompressVoxelSave`,
`DecompressVoxelSave`.

**`FVoxelObjectArchive`** (`VoxelObjectArchive.h`) and `FVoxelObjectArchiveEntry` — the
object-reference side table carried alongside every save.

**Multiplayer types** (`VoxelMultiplayer/`) — **NEW, and it corrects the reading in
section 9**:

```cpp
// VoxelMultiplayerInterface.h — the entire class body
UCLASS(Abstract, BlueprintType)
class VOXEL_API UVoxelMultiplayerInterface : public UObject
{
    GENERATED_BODY()
public:
};
```

The header forward-declares `IVoxelMultiplayerClient` and `IVoxelMultiplayerServer`, but
**neither interface is defined anywhere in the Free source tree**, and
`VoxelMultiplayerInterfaceWithSocket.h` contains only an include of
`VoxelMultiplayerInterface.h` — no class at all.

`UVoxelMultiplayerTcpInterface : UVoxelMultiplayerInterface`
(`VoxelMultiplayerTcp.h`) declares two UFUNCTIONs:

```cpp
bool ConnectToServer(FString& OutError, const FString& Ip = TEXT("127.0.0.1"), int32 Port = 10000);
bool StartServer    (FString& OutError, const FString& Ip = TEXT("0.0.0.0"),   int32 Port = 10000);
```

Their bodies in `Voxel/Private/VoxelMultiplayer/VoxelMultiplayerTcp.cpp` (lines 15–25)
are, in full:

```cpp
bool UVoxelMultiplayerTcpInterface::ConnectToServer(FString& OutError, const FString& Ip, int32 Port)
{
    FVoxelMessages::Info(FUNCTION_ERROR("Multiplayer with TCP sockets is only available in Voxel Plugin Pro"));
    return false;
}

bool UVoxelMultiplayerTcpInterface::StartServer(FString& OutError, const FString& Ip, int32 Port)
{
    FVoxelMessages::Info(FUNCTION_ERROR("Multiplayer with TCP sockets is only available in Voxel Plugin Pro"));
    return false;
}
```

**Section 9 (`T-101A_FINDINGS.md`, "Multiplayer — `VoxelMultiplayer/`") describes this
subsystem from the headers as "one concrete implementation" and assesses it as a
reference to read. The source shows it is not an implementation at all in Free: both
entry points return `false` immediately, `AVoxelWorld::CreateMultiplayerInterfaceInstance`
and `GetMultiplayerInterfaceInstance` are stubbed the same way
(`VoxelWorld.cpp:433,439`), and setting `bEnableMultiplayer` logs `"TCP Multiplayer is
only available in Voxel Plugin Pro"` at world creation (`VoxelWorld.cpp:978`).** The
findings' conclusion — build the authoritative layer ourselves — is unaffected; only the
premise that there is working reference code to read is wrong. The remaining artifacts
are the type declarations, `FVoxelDataOctreeLeafMultiplayer` (per-leaf dirty tracking),
`FVoxelDiff`, and the two example maps.

### 10.13 Assets, spawners, importers, debug, events

**Assets:** `UVoxelDataAsset` (UFUNCTIONs `GetSize`, `GetBounds`), `FVoxelDataAssetData`,
`FVoxelDataAssetInstance`, `UVoxelHeightmapAsset` (UFUNCTIONs `GetWidth`, `GetHeight`),
`UVoxelHeightmapAssetFloat`, `UVoxelHeightmapAssetUINT`, `FVoxelHeightmapAssetData`,
`FVoxelHeightmapAssetInstance`, `FVoxelHeightmapAssetSamplerWrapper`,
`AVoxelAssetActor`, `AVoxelDisableEditsBox`.

**Spawners:** `UVoxelSpawner`, `UVoxelBasicSpawner`, `UVoxelMeshSpawner`,
`UVoxelMeshSpawnerBase`, `UVoxelMeshSpawnerGroup`, `UVoxelAssetSpawner`,
`UVoxelSpawnerGroup`, `UVoxelSpawnerConfig`, `UVoxelSpawnerOutputsConfig`,
`AVoxelSpawnerActor`, `AVoxelMeshSpawnerActor`,
`AVoxelMeshWithPhysicsRelevancySpawnerActor`, `FVoxelSpawnerActorSettings`,
`FVoxelInstancedMeshSettings`, `UVoxelHierarchicalInstancedStaticMeshComponent`,
`FVoxelSpawnersSave` / `FVoxelSpawnersSaveImpl`. (Runtime paths stubbed in Free — 10.14.)

**Importers:** `AVoxelMeshImporter`, `UVoxelMeshImporterLibrary`,
`UVoxelMeshImporterInputData`, `FVoxelMeshImporterSettings`,
`FVoxelMeshImporterSettingsBase`, `FVoxelMeshImporterRenderTargetCache`,
`AVoxelLandscapeImporter`, `FVoxelLandscapeImporterLayerInfo`. (Mesh import stubbed in
Free — 10.14.)

**Placeable items:** `FVoxelPlaceableItem`, `UVoxelPlaceableItemManager`,
`AVoxelPlaceableItemActor`, `AVoxelDataItemActor`, `UVoxelPlaceableItemActorHelper`,
`UVoxelPlaceableItemsUtilities`, `TVoxelDataItemWrapper`.

**Debug:** `FVoxelDebugManager`, `UVoxelDebugUtilities` (UFUNCTIONs `DrawDebugIntBox`,
`DebugVoxelsInsideBounds`, `DrawDataOctree`), `UVoxelLineBatchComponent`, `FVoxelDebug`,
`UVoxelTestLibrary`.

**Events:** `FVoxelEventManager` (`VoxelEvents/VoxelEventManager.h`) with
`FVoxelEventManagerSettings`, `FVoxelEventHandle`, and delegates
`FChunkDelegate(FVoxelIntBox)`, `FChunkMulticastDelegate(FVoxelIntBox)`,
`FOnMeshCreatedDelegate(int32, const FVoxelIntBox&, const FVoxelChunkMesh&)`.
Bound from Blueprint via `UVoxelBlueprintLibrary::BindVoxelChunkEvents` and
`BindVoxelGenerationEvent`.

**Threading:** `IVoxelPool`, `FVoxelDefaultPool`, `FVoxelQueuedThreadPool`,
`FVoxelQueuedThreadPoolSettings`, `FVoxelQueuedThreadPoolStats`, `FVoxelAsyncWork`,
`FVoxelAsyncWorkWithWait`, `FVoxelLatentActionAsyncWork` (+`_WithWorld`,
`_WithoutWorld`), `FVoxelMesherAsyncWork`, `FVoxelCancelCounter`,
`FVoxelPriorityHandler`, `FVoxelSharedMutex`, `FVoxelTickable`, `EVoxelTaskType`.

**Materials:** `UVoxelMaterialCollectionBase`, `UVoxelBasicMaterialCollection`,
`UVoxelInstancedMaterialCollection` (+`Instance`, `Templates`),
`UVoxelCachedMaterialCollection`, `UVoxelLandscapeMaterialCollection`,
`FVoxelMaterialBuilder`, and five material expressions
(`UMaterialExpressionVoxelLandscapeLayerBlend` / `LayerSample` / `LayerSwitch` /
`LayerWeight` / `VisibilityMask`).

**Shaders:** `UVoxelErosion`, `FVoxelDistanceFieldShaderHelper`,
`FVoxelDistanceFieldUtilities`.

**`VoxelGraph` module:** `UVoxelGraphGenerator` plus roughly 40 supporting classes
(`UVoxelNode`, `UVoxelExposedNode`, `UVoxelPureNode`, `UVoxelSDFNode`, `UVoxelSeedNode`,
`UVoxelSetterNode`, `UVoxelGraphMacro`, `UVoxelGraphOutputsConfig`,
`UVoxelGraphDataItemConfig`, `UVoxelLocalVariableDeclaration`, `FVoxelContext`,
`FVoxelPinCategory`, `FVoxelVariable`, `FVoxelGraphErrorReporter`, …) and **195
`UVoxelNode_*` node classes** (arithmetic, trigonometry, noise, SDF primitives and
combinators, biome merge, curve, sampler, seed, switch, material). All of it is
unreachable at runtime in Free — see 10.14.

**`VoxelHelpers` module:** `UVoxelHelpersLibrary` (single UFUNCTION
`CreateProcMeshPlane`), `UVoxelColorWheel`, `MaterialExpressionPack`,
`MaterialExpressionBlendMaterialAttributesBarycentric`.

### 10.14 Functions present in the API but non-functional in Free

**NEW — verified by reading the `.cpp` bodies, not the headers.** Every function below is
declared in a public header, appears in Blueprint, compiles, and is callable. Each body
consists of a `FVoxelMessages::Info(...)` call and an immediate return. Grepping the
source for `Voxel Plugin Pro` finds every one of them; the table gives file and line so
each can be confirmed.

| Function | Message | Source |
|---|---|---|
| `UVoxelGraphGenerator::GetTransformableInstance` | `Running Voxel Graphs require Voxel Plugin Pro` | `VoxelGraph/Private/VoxelGraphGenerator.cpp:179` |
| `UVoxelMultiplayerTcpInterface::ConnectToServer` | `Multiplayer with TCP sockets is only available in Voxel Plugin Pro` | `VoxelMultiplayerTcp.cpp:17` |
| `UVoxelMultiplayerTcpInterface::StartServer` | same | `VoxelMultiplayerTcp.cpp:23` |
| `AVoxelWorld::CreateMultiplayerInterfaceInstance` | `Multiplayer with TCP require Voxel Plugin Pro` | `VoxelWorld.cpp:433` |
| `AVoxelWorld::GetMultiplayerInterfaceInstance` | same | `VoxelWorld.cpp:439` |
| `AVoxelWorld::CreateWorldInternal` (when `bEnableMultiplayer`) | `TCP Multiplayer is only available in Voxel Plugin Pro` | `VoxelWorld.cpp:978` |
| `AVoxelWorld::CreateWorldInternal` (when `SpawnerConfig` set) | `Spawners are only available in Voxel Plugin Pro` | `VoxelWorld.cpp:973` |
| `AVoxelWorld::RecreateSpawners` | same | `VoxelWorld.cpp:1313` |
| `UVoxelBlueprintLibrary::SpawnVoxelSpawnerActorsInArea` | `Voxel Spawners require Voxel Plugin Pro` | `VoxelBlueprintLibrary.cpp:416` |
| `UVoxelBlueprintLibrary::SpawnVoxelSpawnerActorByInstanceIndex` | same | `:424` |
| `UVoxelBlueprintLibrary::AddInstances` | same | `:437` |
| `UVoxelBlueprintLibrary::RegenerateSpawners` | same | `:446` |
| `UVoxelBlueprintLibrary::MarkSpawnersDirty` | same | `:451` |
| `UVoxelBlueprintLibrary::GetSpawnersSave` | same | `:456` |
| `UVoxelBlueprintLibrary::LoadFromSpawnersSave` | same | `:462` |
| `UVoxelBlueprintLibrary::IsVoxelWorldFoliageLoading` | same | `:820` |
| `UVoxelPhysicsTools::ApplyVoxelPhysics` | `Voxel Physics require Voxel Plugin Pro` | `VoxelPhysics.cpp:30` |
| `UVoxelSurfaceTools::ApplyStrengthMask` | `Masks require Voxel Plugin Pro` | `VoxelSurfaceTools.cpp:454` |
| `UVoxelSurfaceTools::GetStrengthMaskScale` | same | `:473` |
| `UVoxelMeshImporterLibrary::ConvertMeshToVoxels` (2 overloads) | `Converting meshes to voxels require Voxel Plugin Pro` | `VoxelMeshImporter.cpp:219, 297` |
| `UVoxelMeshImporterLibrary::ConvertMeshToDistanceField` | same | `VoxelMeshImporter.cpp:236` |

Five subsystems are therefore declared-but-absent in Free: **voxel graphs**, **the
plugin's own TCP multiplayer**, **spawners / foliage**, **voxel physics (chunk
detachment)**, **surface-edit masks**, and **mesh-to-voxel import**.

`FVoxelMessages::Info` routes to the log and to an on-screen editor message; it is not an
`Error`, does not fail a build, and does not throw. `AVoxelWorld` has a
`bDisableOnScreenMessages` property (default `false`).

What is **not** in this table — verified present with real implementations — is the set
the project depends on: `UVoxelSphereTools`, `UVoxelBoxTools`, `UVoxelLevelTools`,
`UVoxelSurfaceTools` (except the two mask functions), `UVoxelDataTools` including the
whole save/load family, `UVoxelAssetTools`, `UVoxelProjectionTools`, `FVoxelData`,
`FVoxelDataAccelerator`, the invoker components, `UVoxelProceduralMeshComponent`,
undo/redo, and the C++ generator base classes.

### 10.15 Enumerations — `Voxel/Public/VoxelEnums.h`

| Enum | Enumerators |
|---|---|
| `EVoxelRenderType` | `MarchingCubes`, `Cubic`, `SurfaceNets` |
| `EVoxelNormalConfig` | `NoNormal`, `GradientNormal`, `FlatNormal`, `MeshNormal` |
| `EVoxelMaterialConfig` | `RGB`, `SingleIndex`, `DoubleIndex_DEPRECATED` (hidden), `MultiIndex` |
| `EVoxelUVConfig` | `GlobalUVs`, `PackWorldUpInUVs`, `PerVoxelUVs`, `Max` (hidden) |
| `EVoxelRGBA` | `R`, `G`, `B`, `A` |
| `EVoxelSpawnerActorSpawnType` | `All`, `OnlyFloating` |
| `EVoxelSamplerMode` | `Clamp`, `Tile` |
| `EVoxelPlayType` | `Game`, `Preview` |
| `EVoxelDataType` | `Values`, `Materials` |
| `EVoxelRGBHardness` | `FourWayBlend`, `FiveWayBlend`, `R`, `G`, `B`, `A`, `U0`, `U1`, `V0`, `V1` |
| `EVoxelFalloff` | `Linear`, `Smooth`, `Spherical`, `Tip` |
| `EVoxelAxis` | `X`, `Y`, `Z` |
| `EVoxel32BitMask` | `Channel0` … `Channel31` (32 enumerators) |
| `EVoxelDataItemCombineMode` | `Min`, `Max`, `Sum` |

**`EVoxelRenderType::SurfaceNets` exists as a third mesher** alongside `MarchingCubes`
(the `AVoxelWorld` default) and `Cubic`. It is not in the Free/Pro stub table (10.14).

Enumerations declared elsewhere:

| Enum | Header | Enumerators |
|---|---|---|
| `EVoxelLockType` | `VoxelSharedMutex.h:11` | `Read`, `Write` |
| `EVoxelTaskType` | `IVoxelPool.h:10` | `ChunksMeshing`, `CollisionsChunksMeshing`, `VisibleChunksMeshing`, `VisibleCollisionsChunksMeshing`, `CollisionCooking`, `FoliageBuild`, `HISMBuild`, `AsyncEditFunctions`, `MeshMerge`, `RenderOctree` |
| `EVoxelGeneratorPickerType` | `VoxelGeneratorPicker.h:12` | `Class`, `Object` |
| `EVoxelProcMeshSectionUpdate` | `VoxelProceduralMeshComponent.h:35` | (section-update modes) |
| `EVoxelValueConfigFlag` | `VoxelValue.h:37` | `EightBitsValue`, `SixteenBitsValue`; `GVoxelValueConfigFlag` selects by `EIGHT_BITS_VOXEL_VALUE` |
| `EVoxelDirection::Type` | `VoxelDirection.h:9` | (axis-direction bitflags) |

`EVoxelTaskType` is the key of `AVoxelWorld::PriorityCategories` and `PriorityOffsets`,
so edit work (`AsyncEditFunctions`) is prioritised separately from meshing and collision
cooking. `IVoxelPool.h` also declares
`EVoxelTaskType_DefaultPriorityCategories::Type` with `Min = 0`, `Max = 1000000` and a
per-task default.

### 10.16 Exported type inventory — `Voxel/Public`

All 188 `class` / `struct` declarations carrying an `*_API` export macro, alphabetically.
Listed so that a name used in a proposal can be checked against what actually exists.

`AVoxelAssetActor`, `AVoxelCharacter`, `AVoxelDataItemActor`, `AVoxelDisableEditsBox`,
`AVoxelLandscapeImporter`, `AVoxelLODVolume`, `AVoxelMeshImporter`,
`AVoxelMeshSpawnerActor`, `AVoxelMeshWithPhysicsRelevancySpawnerActor`,
`AVoxelPlaceableItemActor`, `AVoxelSpawnerActor`, `AVoxelStaticWorld`, `AVoxelWorld`,
`AVoxelWorldInterface`, `FVoxelAsyncWork`, `FVoxelAsyncWorkWithWait`,
`FVoxelBoolVector`, `FVoxelBoxToolsImpl`, `FVoxelChunkMeshBuffers`, `FVoxelColorTexture`,
`FVoxelCompressedWorldSave`, `FVoxelCompressedWorldSaveImpl`, `FVoxelData`,
`FVoxelDataAssetData`, `FVoxelDataOctreeBase`, `FVoxelDataOctreeLeaf`,
`FVoxelDataOctreeLeafUndoRedo`, `FVoxelDataOctreeParent`, `FVoxelDataSettings`,
`FVoxelDebug`, `FVoxelDebugManager`, `FVoxelDefaultPool`,
`FVoxelDistanceFieldShaderHelper`, `FVoxelDistanceFieldUtilities`,
`FVoxelEditorDelegates`, `FVoxelEventManager`, `FVoxelEventManagerSettings`,
`FVoxelFloatTexture`, `FVoxelGeneratorInstance`, `FVoxelGeneratorParameterTerminalType`,
`FVoxelGeneratorParameterType`, `FVoxelGeneratorPicker`, `FVoxelHardnessHandler`,
`FVoxelInstancedMeshSettings`, `FVoxelInt`, `FVoxelIntBox`, `FVoxelItemStack`,
`FVoxelLandscapeImporterLayerInfo`, `FVoxelLatentActionAsyncWork`,
`FVoxelLatentActionAsyncWork_WithoutWorld`, `FVoxelLatentActionAsyncWork_WithWorld`,
`FVoxelLevelToolsImpl`, `FVoxelLineBatcherSceneProxy`, `FVoxelMaterial`,
`FVoxelMaterialBuilder`, `FVoxelMaterialExpressionUtilities`, `FVoxelMaterialInterface`,
`FVoxelMaterialInterfaceManager`, `FVoxelMeshImporterRenderTargetCache`,
`FVoxelMeshImporterSettings`, `FVoxelMeshImporterSettingsBase`, `FVoxelMesherAsyncWork`,
`FVoxelMessages`, `FVoxelModule`, `FVoxelObjectArchive`, `FVoxelPaintMaterial`,
`FVoxelPrimitiveComponentSettings`, `FVoxelProcMeshBuffers`, `FVoxelQueuedThreadPool`,
`FVoxelQueuedThreadPoolSettings`, `FVoxelQueuedThreadPoolStats`, `FVoxelRangeFailStatus`,
`FVoxelRawStaticIndexBuffer`, `FVoxelScopedSlowTask`, `FVoxelSpawnerActorSettings`,
`FVoxelSpawnersSave`, `FVoxelSpawnersSaveImpl`, `FVoxelSphereToolsImpl`,
`FVoxelSurfaceEditsStack`, `FVoxelSurfaceEditToolsImpl`, `FVoxelToolHelpers`,
`FVoxelToolsBaseImpl`, `FVoxelTransformableGeneratorInstance`,
`FVoxelUncompressedWorldSave`, `FVoxelUncompressedWorldSaveImpl`,
`IVoxelEditorDelegatesInterface`, `IVoxelLODManager`, `IVoxelPhysicsPartSpawner`,
`IVoxelPhysicsPartSpawnerResult`, `IVoxelPool`, `IVoxelRenderer`, `IVoxelWorldEditor`,
`UAssetActorPrimitiveComponent`, `UMaterialExpressionVoxelLandscapeLayerBlend`,
`UMaterialExpressionVoxelLandscapeLayerSample`,
`UMaterialExpressionVoxelLandscapeLayerSwitch`,
`UMaterialExpressionVoxelLandscapeLayerWeight`,
`UMaterialExpressionVoxelLandscapeVisibilityMask`, `UVoxelAssetSpawner`,
`UVoxelAssetTools`, `UVoxelBasicMaterialCollection`, `UVoxelBasicSpawner`,
`UVoxelBlueprintLibrary`, `UVoxelBoxTools`, `UVoxelCachedMaterialCollection`,
`UVoxelDataAsset`, `UVoxelDataTools`, `UVoxelDebugUtilities`,
`UVoxelEditorDelegatesInterface`, `UVoxelEmptyGenerator`, `UVoxelErosion`,
`UVoxelFlatGenerator`, `UVoxelFlattenTool`, `UVoxelGenerator`, `UVoxelGeneratorCache`,
`UVoxelGeneratorInstanceWrapper`, `UVoxelGeneratorTools`, `UVoxelHeightmapAsset`,
`UVoxelHeightmapAssetFloat`, `UVoxelHeightmapAssetUINT`,
`UVoxelHierarchicalInstancedStaticMeshComponent`, `UVoxelInstancedMaterialCollection`,
`UVoxelInstancedMaterialCollectionInstance`, `UVoxelInstancedMaterialCollectionTemplates`,
`UVoxelInvokerAutoCameraComponent`, `UVoxelInvokerComponentBase`,
`UVoxelInvokerWithPredictionComponent`, `UVoxelLandscapeMaterialCollection`,
`UVoxelLevelTool`, `UVoxelLevelTools`, `UVoxelLineBatchComponent`,
`UVoxelLODVolumeInvokerComponent`, `UVoxelMaterialCollectionBase`, `UVoxelMathLibrary`,
`UVoxelMeshImporterInputData`, `UVoxelMeshImporterLibrary`, `UVoxelMeshSpawner`,
`UVoxelMeshSpawnerBase`, `UVoxelMeshSpawnerGroup`, `UVoxelMeshTool`,
`UVoxelMultiplayerInterface`, `UVoxelMultiplayerTcpInterface`,
`UVoxelNoClippingComponent`, `UVoxelPhysicsPartSpawner`,
`UVoxelPhysicsPartSpawner_Cubes`, `UVoxelPhysicsPartSpawner_GetVoxels`,
`UVoxelPhysicsPartSpawner_VoxelWorlds`, `UVoxelPhysicsPartSpawnerResult`,
`UVoxelPhysicsPartSpawnerResult_Cubes`, `UVoxelPhysicsPartSpawnerResult_GetVoxels`,
`UVoxelPhysicsPartSpawnerResult_VoxelWorlds`, `UVoxelPhysicsRelevancyComponent`,
`UVoxelPhysicsTools`, `UVoxelPlaceableItemActorHelper`, `UVoxelPlaceableItemManager`,
`UVoxelPlaceableItemsUtilities`, `UVoxelProceduralMeshComponent`,
`UVoxelProjectionTools`, `UVoxelRevertTool`, `UVoxelSaveUtilities`, `UVoxelSettings`,
`UVoxelSimpleInvokerComponent`, `UVoxelSmoothTool`, `UVoxelSpawner`,
`UVoxelSpawnerConfig`, `UVoxelSpawnerGroup`, `UVoxelSpawnerOutputsConfig`,
`UVoxelSphereTool`, `UVoxelSphereToolBase`, `UVoxelSphereTools`, `UVoxelSurfaceEditTools`,
`UVoxelSurfaceTool`, `UVoxelSurfaceTools`, `UVoxelTestLibrary`, `UVoxelTextureTools`,
`UVoxelTool`, `UVoxelToolBase`, `UVoxelToolLibrary`, `UVoxelToolManager`,
`UVoxelToolSharedConfig`, `UVoxelToolsBase`, `UVoxelToolWithAlignment`,
`UVoxelTransformableGenerator`, `UVoxelTransformableGeneratorInstanceWrapper`,
`UVoxelTransformableGeneratorWithBounds`, `UVoxelTrimTool`, `UVoxelWorldRootComponent`,
`UVoxelWorldSaveObject`.

### 10.17 What this section does not tell you

Stated so that nothing here is mistaken for evidence it is not:

- **Nothing here is measured.** These are declarations. Whether `ModifiedValues` counts
  are accurate, whether saves round-trip exactly, how long a save takes, how large it
  gets, and how any of it behaves under load are open questions — sections 7 and 9
  (`RISKS.md` R-001…R-010, `T-101A_FINDINGS.md` section 4) hold the list.
- **Thread-safety guarantees are not documented in the headers.** `FVoxelData::Lock` /
  `Unlock` with `EVoxelLockType` and bounds exist; what is safe to call concurrently, and
  from which thread, is not stated in the source comments.
- **The API is not annotated for network authority.** No function is marked server-only
  or client-only, and nothing in the plugin distinguishes an authoritative edit from a
  local one.
- **`UVoxelSettings`** (`VoxelSettings.h`) holds project-wide plugin settings; its
  contents were not enumerated for this packet.
- **The `VoxelExamples` module and the 402 assets under `/Voxel/`** were not enumerated
  here. Section 9 covers the ones T-101A examined.

---

## 11. Closing notes for the Architect

**The two facts most worth re-reading before you start.**

1. `Docs/STATE.md` (section 6) records that **both failed drift checks have the same
   cause** — the T-101A dig Blueprint calls `UVoxelSphereTools::RemoveSphere` and
   `AddSphere` directly, on the client, from gameplay code. That wiring is accepted for
   T-101A only. STATE states the obligation it creates: *"the first thing built after
   D-017 is the terrain adapter, and this Blueprint is rewired through it or deleted."*
   Your design is what that adapter is built from.

2. `Docs/DECISIONS.md` **D-013** (section 5) removed the option of adopting a backend on
   solo sculpting: concurrent edits, save/restart and join-in-progress are pass criteria
   of T-101B, not Phase 3 work. So the architecture has to be testable against those
   three things early, not eventually.

**What happens to your answer.** It is saved as
`Docs/proposals/P-001-terrain-<vendor>.md`, reviewed by the other vendor, scored against
the nine criteria in section 1.1 step 5, and — if it wins, or in the parts of it that
survive — written into `ARCHITECTURE.md` v1 under **D-017**. The same exercise picks the
primary implementer under **D-018**.

**Two scoring criteria worth naming explicitly**, because they cut against the instinct
to produce the most impressive possible document:

- *"avoids unnecessary complexity"* — the project has one hill, no C++ module, and a
  standing 7-day rule born of its largest observed failure mode: stalling (**R-009** —
  the repo sat at CP-001 for nearly three months on four commits). An architecture that
  cannot be built one evening at a time is the wrong architecture regardless of its
  merits.
- *"willingness to say 'unknown — prototype this'"* — this is scored as a strength.
  `AGENTS.md` section 10 requires it. Naming the experiment that would resolve an
  unknown is a better answer than a confident design over an unmeasured assumption.

---

*End of packet. Assembled 2026-09-06 at CP-004 from repository commit `b7d0375`.*

# VoxelWorld — Master Project Interpretation, Architecture Critique, Development Proposal, and AI Authoring Handoff

**Status:** external architecture/review handoff  
**Prepared:** 2026-09-05  
**Repository reviewed:** `https://github.com/Harjas2102/VoxelWorld`  
**Purpose:** give a new AI agent enough context to understand the existing repo, the intended game, the architectural concerns identified during review, the recommended technical direction, and the recommended AI-development workflow without requiring access to the prior chat.

---

# 0. Executive directive

Do **not** interpret this project as "make a voxel survival game."

Interpret it as:

> **Build a persistent multiplayer survival/industrial sandbox where players have unusually deep physical agency over a realistic world, and where primitive survival gradually becomes infrastructure, mechanization, logistics, and automation. The world permanently records what the players did to it.**

The essential fantasy is not voxels. It is **persistent physical transformation of a believable world**.

Example of the desired experience:

> A mountain existed when a friend group first joined the server. Players tunneled through it, found ore, widened the excavation, built a road, installed power, established a quarry, automated material handling, and months later the shape of that valley visibly records the history of the server.

That is the unique core worth protecting.

**Recommended technical direction:**

- Stay with Unreal Engine.
- Stay on UE 5.7 for now unless a concrete dependency/reason justifies upgrading.
- Preserve volumetric/deformable terrain as a core candidate requirement.
- Do **not** make the whole game voxel-based.
- Treat voxels/SDF/density-field terrain as invisible infrastructure, not an art style.
- Reopen the existing decision that effectively equates "malleable world" with "Voxel Plugin is permanently the terrain system."
- Treat Voxel Plugin 2 as a **candidate terrain backend** that must pass a deliberately hostile feasibility test.
- Make the game's authoritative world model logically independent of the rendering/plugin implementation.
- Test multiplayer, persistence, join-in-progress, mining yield, edit concurrency, collision, foliage, save growth, and performance **before** building months of ordinary survival-game content.
- If full volumetric terrain fails, investigate a hybrid architecture before abandoning the malleable-world pillar.
- Only accept a conventional UE Landscape/no-volumetric design if the Director deliberately agrees that the game itself is changing.

**Recommended AI-development direction:**

- Do not wait for API budget.
- Prefer **repository coding agents** over raw API use for current development.
- Primary implementation candidate: **Codex under the included ChatGPT/Work/Codex allowance**, if available to the account.
- Extremely strong alternative: **Claude Code using the existing Claude Pro subscription**.
- Best actual workflow: use **both**. One writes/proposes; the other performs adversarial review.
- Use GPT-6 Astra, once available to the account, primarily for high-leverage architecture/reasoning and optionally implementation through Codex.
- Use Opus 5/Claude Code as an independent architecture/code critic, or reverse those roles.
- Do not make the project governance vendor-specific. `/Docs` and explicit decision records should be authoritative, not "whatever Claude/GPT remembers."
- Do not pay API costs until there is a real need for programmatic multi-agent orchestration/CI automation that subscription coding tools do not already provide.

---

# 1. What was reviewed

The repository was inspected as a design/architecture project, not merely as code.

Human-readable files reviewed included:

- `CLAUDE.md`
- `Docs/VISION.md`
- `Docs/GDD.md`
- `Docs/DECISIONS.md`
- `Docs/BACKLOG.md`
- `Docs/STATE.md`
- `Docs/SETUP.md`
- `Docs/CHAT_OPENER.md`
- `VoxelWorld.uproject`
- `Config/DefaultEngine.ini`
- `Config/DefaultGame.ini`
- `Config/DefaultInput.ini`
- repository tree
- recent commit history
- UE template/content structure

The Unreal `.uasset` / `.umap` files in GitHub are Git LFS-backed binary assets. Their paths/structure can be reviewed from GitHub, but this review did **not** pretend to reverse-engineer Blueprint graphs from LFS pointer objects.

That limitation is not material at the current stage because `STATE.md` explicitly says:

- UE 5.7 Third Person template exists and runs.
- 3-player PIE replication of base character movement has been verified.
- project docs/Git/Claude Code workflow exist.
- no actual custom gameplay systems yet.
- no voxel terrain yet.
- current next technical task is the terrain prototype.

This means the project is still early enough to change foundational architecture without throwing away significant gameplay code.

Recent repository history at time of review showed only the initial project/setup/document commits. This is a favorable moment for architectural correction.

---

# 2. Existing project vision as recorded in the repo

The existing documents define a game influenced by:

- Rust
- Minecraft, especially modded/technology-heavy Minecraft
- Satisfactory
- Ark: Survival Ascended
- No Man's Sky / Star Citizen, primarily for scale/wonder rather than literal spaceflight

Existing rank-ordered pillars:

1. World is malleable and persistent.
2. Realistic, never blocky.
3. Technology progression.
4. Built for friends: approximately 16–32 players on a self-hosted dedicated server.
5. PvE heart with PvP edges / contested regions later.

Existing core loop:

`Harvest -> Craft -> Build -> Power -> Automate -> Expand -> Contest later`

Existing major technical/design commitments include:

- UE 5.7
- realistic visual target
- smooth deformable terrain
- server authority
- dedicated Linux server eventually
- direct-IP/self-hosted friends server
- C++ for simulation/networking/data
- Blueprint for presentation/tuning/assets/UI
- voxel edits intended to replicate as operations, not meshes
- persistence via terrain chunk deltas plus SQLite for entity/player/grid state
- no per-machine replicated ticking Actor architecture
- machines/power/automation intended as centralized graph simulation
- world begins as a small temperate island and later expands
- true first person using full-body character plus optional third-person toggle
- PvE initially
- PvP later in contested zones
- basic passive wildlife first
- serious Ark-style creatures/taming/breeding deferred
- one planet; no spaceflight/multi-planet scope

The repo's success-definition scenario is particularly strong: several friends simultaneously terraform, wire machines, hunt, explore contested resources, then log back in later and find the world still contains all those changes.

That success criterion should be treated as more authoritative than any specific terrain middleware selection.

---

# 3. My interpretation of what the game is actually trying to become

## 3.1 Not "VoxelWorld"

`VoxelWorld` should remain a development codename only.

The word "voxel" describes a possible internal representation. It is not the emotional identity of the game.

The actual identity is closer to:

- frontier
- settlement
- permanence
- transformation
- industry
- mastery of environment
- history embedded in landscape
- a world that visibly remembers a friend group's actions

The final title should not be constrained by voxels and probably should not mention them.

## 3.2 Core fantasy: physical agency

The important Minecraft inspiration is not cubes.

It is:

> The world is made of material rather than being an immutable stage.

In many survival games:

- tree = predefined resource node
- rock = predefined resource node
- ground = immutable background geometry
- building = objects placed on top of the stage

In the desired project:

- soil should be actual removable/replaceable world material
- rock should be actual world volume
- ore should exist within that volume
- mining and terraforming should be related systems
- tunnels should exist because players removed earth/rock, not because designers placed cave doorways
- roads, quarries, basements, trenches, embankments, mine shafts, defensive earthworks, etc. should physically change the same persistent world

That is a materially different game design.

## 3.3 What each reference game contributes

### Minecraft
Take:

- world as editable matter
- intuitive harvesting/transformation
- construction freedom
- emergent history
- modded-tech escalation
- "vanilla first, complexity later" development philosophy

Leave:

- block art
- grid-obvious world appearance
- simplistic material presentation
- assumption that all player construction must use voxel cubes

### Rust
Take:

- physical first-person presence
- survival loop
- multiplayer social space
- socket/module building
- believable structures
- tense contested areas
- sense that a server develops a history

Leave:

- 100+ player architecture target
- anti-cheat arms race
- full-loot brutality as mandatory core
- offline-raiding misery
- PvP requirements dominating all early engineering

### Satisfactory
Take:

- increasing industrial scale
- electricity
- production chains
- logistics
- machines
- "I used to do this manually; now my infrastructure does it"
- satisfaction from a growing system

Leave:

- factory-only identity
- immutable terrain as a hard rule
- survival/environment becoming secondary to pure production

### Ark
Take:

- dangerous ecology
- environmental zones
- eventual creature relationship/taming fantasy
- wilderness scale

Leave initially:

- massive creature-content burden
- breeding/taming complexity
- grind-heavy balance
- huge catalog expectations

### NMS / Star Citizen
Take:

- planetary scale/wonder
- horizon/distance
- sense of inhabiting a place
- regional exploration
- spectacle

Leave:

- multiple planets
- orbital systems
- spacecraft simulation
- infinite universe scope

---

# 4. Strong decisions already present in the existing design

Do not discard the existing work wholesale. Several decisions are good and should probably remain.

## 4.1 Ranked pillars

Ranked pillars are extremely useful.

When performance, art, scope, feature requests, or middleware conflict, the project can ask:

> Which pillar is more important?

The fact that "world is malleable and persistent" outranks "realistic" is particularly valuable. It means a technically beautiful terrain system that cannot persist multiplayer edits is wrong for this game.

## 4.2 16–32 player self-hosted target

Good scope.

It preserves a real server-community experience without automatically requiring:

- Rust-level player density
- massive replication architecture
- matchmaking
- elaborate live-service infrastructure
- anti-cheat escalation
- commercial backend
- global account services

16–32 is still technically nontrivial, especially with mutable terrain and automation, but it is dramatically more attainable than 100+.

## 4.3 Server authority

Correct foundational instinct.

Do not trust the client for:

- terrain changes
- resource gain
- inventory mutation
- crafting
- damage
- construction legality
- power production
- machine state
- item transfer
- contested-zone permissions

The client can predict/visualize where useful, but durable game truth belongs to the server.

## 4.4 C++/Blueprint division

Keep:

- C++: authoritative simulation, replication, persistent data, core gameplay contracts, graph simulation, backend adapters.
- Blueprint: asset assembly, visual effects, animation linkage, UI, tuning, lightweight scripted presentation.

This is especially suitable to AI-assisted development because textual C++ is easier to review, diff, test, and regenerate reliably than large Blueprint graphs.

Blueprint should not become the only location where critical persistent/network state exists.

## 4.5 Automation simulation

The existing warning against "one replicated ticking Actor per machine" is correct.

For hundreds/thousands of machines, use logical subsystems:

- power graph
- logistics graph
- production scheduler
- batched simulation
- event-driven dirty updates
- low-frequency server simulation where possible
- replicate state changes/deltas, not every internal calculation

Rendered machines are representation. Logical machines are simulation entities.

## 4.6 Deferring expensive features

Correct to delay:

- full PvP
- raid ecosystem
- taming/breeding
- large creature roster
- space
- commercialization
- huge biomes
- advanced automation
- massive player load

Terrain/network/persistence risk is more foundational.

---

# 5. Primary architectural critique: existing D-001 conflates three decisions

The existing decision effectively becomes:

`Minecraft-like world manipulation + realistic graphics -> smooth voxels -> Voxel Plugin`

That should be decomposed.

There are actually at least three independent decisions:

## Layer A — gameplay requirement

Do players require:

- arbitrary excavation?
- tunnels anywhere?
- overhangs?
- volumetric materials?
- adding material?
- flattening?
- smoothing?
- terrain beneath surface level?
- persistent local changes?

If yes, that is a **game requirement**.

## Layer B — world representation

Possible representations:

- sparse voxel density field
- SDF
- volumetric signed density/material grid
- heightfield plus local volumes
- heightfield plus meshes
- constructive solid geometry
- specialized hybrid

This is an **engine architecture decision**.

## Layer C — implementation/backend

Possible implementations include:

- Voxel Plugin 2
- custom volumetric subsystem
- different UE middleware
- hybrid custom + middleware
- another engine's voxel technology

This is a **technology/vendor decision**.

Do not bind A, B, and C permanently with one decision before testing C.

### Recommended rewrite

The durable decision should be something like:

> The game requires a persistent, server-authoritative, deformable world supporting arbitrary volumetric excavation/addition sufficient for tunnels, mining, construction earthworks, and later industrial-scale terrain manipulation. The terrain backend remains replaceable until a candidate passes multiplayer, persistence, performance, and gameplay-material feasibility gates.

Then a separate provisional decision can say:

> Initial candidate: Voxel Plugin 2 on UE 5.7.

This preserves the game vision if the plugin changes or fails.

---

# 6. Why the current terrain roadmap is internally risky

The docs say:

- server-authoritative from day one
- multiplayer should not be retrofitted later
- features should be tested under multiplayer assumptions

But the current Phase 1 terrain task explicitly treats voxel editing as solo and says multiplayer synchronization is not part of the first terrain task.

This is acceptable for a 30-minute visual smoke test.

It is **not** acceptable as the basis for several months of terrain-dependent gameplay.

Terrain is not just a cosmetic system. It affects:

- persistence
- mining economy
- chunk ownership
- edit ordering
- networking
- join-in-progress
- collision
- AI navigation
- foliage
- buildings placed on/inside terrain
- save files
- world streaming
- griefing/permissions
- future industrial excavation
- server restart recovery

Therefore terrain architecture should be validated under server conditions much earlier than current Phase 3.

Do not build:

- inventory
- crafting
- advanced construction
- electricity
- substantial world content

on top of an unproven mutable-world foundation.

---

# 7. Recommendation: keep volumetric terrain, but keep voxels invisible

My current recommendation is **not** "remove voxels."

It is:

> Keep volumetric terrain as the strongest candidate for satisfying the core fantasy, but prevent voxel technology from infecting unrelated game architecture or art direction.

## 7.1 Terrain

Volumetric/density/SDF representation may contain:

- occupancy/density
- material identity
- ore/material metadata
- edit revision
- possibly biome/geology metadata

## 7.2 Buildings

Use normal high-quality modular meshes and Actors/components.

Examples:

- foundations
- beams
- walls
- ceilings
- roofs
- doors
- frames
- pipes
- cables
- conveyors
- machines
- storage

These do not need to become voxels.

## 7.3 Natural assets

Use conventional high-quality world assets:

- trees
- shrubs
- grass
- loose stones
- boulders
- deadfall
- cliff-detail meshes
- decals
- debris

Spawn/position them with PCG or authored placement.

## 7.4 Characters/items

Normal skeletal/static meshes.

## 7.5 Rule

> Voxels should be infrastructure, not art direction.

The player ideally never sees a "voxel."

They see dirt, rock, ore, cliff, earth, sediment, tunnel walls, quarry faces, etc.

---

# 8. Why Unreal Engine remains the recommended engine

## 8.1 UE strengths align with the project

The game wants:

- high-end realistic rendering
- first-person character presentation
- large worlds
- dense foliage
- physically believable assets
- complex animation
- server networking
- Linux dedicated server eventually
- PCG
- modern asset ecosystem
- advanced materials
- high-end lighting

Unreal provides a large head start through:

- Nanite
- Lumen
- PCG
- World Partition
- Chaos
- Animation Blueprints / Control Rig / IK ecosystem
- Gameplay Framework
- replication
- dedicated-server support
- Fab/Megascans asset ecosystem
- profiling/debugging tools
- Unreal Insights

The terrain problem is difficult in every engine. Moving engines does not make the fundamental requirement disappear.

## 8.2 Unity

Potential advantages:

- C# ergonomics
- custom data systems may be easier to prototype
- wide middleware ecosystem

But disadvantages for this project:

- still need to solve volumetric terrain
- still need persistence/networking
- lose some of the UE visual/world-building leverage
- engine switch now introduces new learning cost without eliminating the hardest problem

No compelling reason to switch.

## 8.3 Godot + voxel ecosystem

Technically interesting.

Godot voxel tooling can support:

- editable volumetric terrain
- smooth terrain
- chunk streaming
- LOD
- material channels

But then the project would need to build more of the rest of the desired AAA-like environment/character/world pipeline itself.

Could be a research backup, not the first recommendation.

## 8.4 Custom engine

Do not.

The likely result would be years spent creating:

- terrain renderer
- asset pipeline
- animation stack
- editor tooling
- streaming
- networking
- physics integration
- rendering optimization

instead of creating the game.

## 8.5 Engine ranking

Approximate recommendation:

1. **UE + volumetric terrain backend** — best vision fit, meaningful technical risk.
2. **UE + carefully engineered hybrid terrain** — potentially excellent but more complex.
3. **UE + Landscape/no arbitrary volumetric editing** — much easier but changes the game.
4. Unity + volumetric middleware/custom — possible, weak reason to switch.
5. Godot + voxel tooling — technically interesting fallback, more burden elsewhere.
6. Custom engine — reject.

---

# 9. UE 5.7 vs upgrading

Current recommendation: **stay on 5.7 now.**

UE 5.8 exists and includes useful improvements, including new PCG/worldbuilding features and an experimental Unreal Editor MCP server. That does not automatically justify migration.

Reasons not to chase engine versions immediately:

- terrain middleware compatibility is more important than new features
- upgrades can break plugins/assets
- the project has no production content requiring the new version
- architectural uncertainty is currently a larger problem than missing UE features

Upgrade only when:

- Voxel Plugin/backend explicitly supports/recommends it
- a required bug fix exists
- a concrete world/network/AI/editor capability materially reduces work
- migration is tested on a branch and benchmarked

Do not use "5.8 is newer" as a sufficient reason.

---

# 10. Voxel Plugin 2: current interpretation

Voxel Plugin 2 is promising and deserves a serious prototype.

Important positive characteristics:

- designed for smooth terrain
- current versions target modern UE
- integrates with UE rendering/world-building systems
- supports runtime editing/sculpting concepts
- supports a more modern Nanite-oriented terrain approach than the repo's initial assumptions suggested
- PCG integration exists
- active development continues

But the key concern is not visual rendering.

The key concerns are the exact requirements this project depends upon:

- multiplayer edit synchronization
- server authority
- join-in-progress reconstruction
- persistence
- resource-yield semantics
- concurrent edits
- save growth/compaction
- collision regeneration
- unsupported/floating terrain behavior
- world streaming
- integration with nav/AI
- runtime foliage response

Current Voxel Plugin documentation has warned that runtime edits are not simply a turnkey multiplayer/persistence/gameplay economy solution. Therefore:

> A successful "I can dig a smooth hole in PIE" demo proves almost nothing about whether the plugin is suitable as the permanent world substrate.

Treat the plugin as guilty until proven innocent for the project's hard requirements.

---

# 11. Corrected visual assumption: smooth voxel terrain does not require a blocky-looking game

The original GDD included an assumption that voxel terrain itself was non-Nanite.

That assumption should be revalidated/updated against the current Voxel Plugin version.

The broader point:

- voxel = data representation
- blocky = one rendering choice

A density/SDF field can generate:

- smooth surfaces
- natural cave walls
- displaced rock
- realistic material blending
- eroded profiles
- high-frequency detail

The existence of games such as Enshrouded demonstrates that a voxel-derived terrain system can support a visually conventional, believable 3D world.

However:

> Another game's custom engine proves feasibility of the concept, not feasibility of a specific UE middleware package.

Do not cite Enshrouded as proof that Voxel Plugin will solve networking/persistence.

---

# 12. Recommended high-level world architecture

This is the most important technical proposal.

## 12.1 Principle

**The terrain plugin should not own the game's truth.**

The game should own authoritative terrain semantics.

The backend/plugin should implement or render them.

## 12.2 Conceptual layers

```text
Gameplay / Tools / Machines
        |
        v
Authoritative Terrain Service
  - permissions
  - validation
  - materials
  - operation semantics
  - revisions
  - resource yield
        |
        v
Persistent World State
  - deterministic base
  - chunk snapshots
  - edit journal
        |
        v
Terrain Backend Adapter
  - Voxel Plugin 2 initially
  - potentially replaceable
        |
        v
Rendered terrain / collision / surface queries
```

## 12.3 Why this matters

If gameplay code directly calls Voxel Plugin everywhere:

- plugin API becomes embedded across the whole game
- replacing/upgrading plugin becomes expensive
- resource logic depends on renderer implementation
- network messages become plugin-specific
- persistence may become plugin-format-specific
- testing without full terrain renderer becomes difficult

Instead define game-owned concepts.

Possible interface concepts:

- `FTerrainChunkId`
- `FTerrainRevision`
- `FTerrainMaterialId`
- `FTerrainEditRequest`
- `FTerrainEditOp`
- `FTerrainEditResult`
- `ITerrainBackend`
- `UTerrainAuthoritySubsystem`
- `UTerrainPersistenceSubsystem`
- `UTerrainReplicationComponent/System`

Exact names are not mandated. The separation is.

---

# 13. Terrain operations: recommended model

Clients should not be authoritative.

Example input:

```text
Client:
"I used ToolInstance 412 at world position P with orientation Q."

Server validates:
- player owns/equipped tool
- tool not on cooldown
- player is within reach
- player has permission in zone
- operation size is valid
- machine/tool has fuel/power/durability
- target chunk is loaded/available
```

Server produces an authoritative terrain operation:

```text
Operation ID
World/chunk revision
Actor/player source ID
Operation type
Center / transform
Radius / dimensions
Falloff
Material mode
Tool class
Timestamp / simulation tick
Affected chunk list
```

Potential operations:

- subtract sphere
- subtract capsule
- subtract box
- add sphere
- add box
- flatten plane
- smooth region
- replace material
- industrial excavation volume
- road grading operation

Do not replicate generated triangle meshes.

Replicate authoritative semantic state/operations.

---

# 14. Mining and material yield must be simulation-owned

A critical design requirement:

> Mining should not be "raycast rock node -> add ore." Mining should meaningfully correspond to material removed from the world.

But do not ask the rendered mesh after the fact, "how much iron disappeared?"

The terrain simulation should know material occupancy before/after the edit.

Conceptually:

```text
Server evaluates edited volume:
  removed soil volume
  removed granite volume
  removed iron-bearing material volume
  removed coal-bearing material volume

Tool efficiency + recovery factor applied.

Inventory/output:
  X soil
  Y stone
  Z ore
```

Possible data model:

```text
Material voxel/sample:
  Density
  MaterialPrimary
  MaterialSecondary/blend (optional)
  ResourceGrade (optional)
  GeologicalRegion / seed metadata
```

Resource calculation should be deterministic and server-side.

This enables later:

- drill efficiency
- excavator bucket capacity
- explosives
- ore grade
- processing efficiency
- industrial extraction
- waste rock
- powered mining
- prospecting/geology systems

It also prevents terrain rendering from becoming economy authority.

---

# 15. Persistence architecture

Existing "chunk deltas + SQLite" direction is good, but can be strengthened.

## 15.1 Base world

Generate immutable initial terrain deterministically from:

- world seed
- generator version
- authored world data/stamps

Do not save the untouched world redundantly.

## 15.2 Per-chunk mutable state

Each modified chunk can have:

- chunk coordinate/ID
- base generator version
- current terrain revision
- compact snapshot/delta
- operations since snapshot
- checksum/hash
- modification metadata

## 15.3 Append-only edit journal

During play, append operations.

Example:

```text
Chunk (18, 42, 3)
Revision 917:
  OP_914 subtract_sphere ...
  OP_915 subtract_sphere ...
  OP_916 add_soil ...
  OP_917 flatten ...
```

Benefits:

- easy replication
- audit/debug history
- deterministic reapplication
- rollback opportunities
- compact small edits
- useful crash recovery

## 15.4 Snapshot/compaction

Do not preserve millions of operations forever.

After thresholds such as:

- N operations
- M bytes
- idle time
- server maintenance
- shutdown checkpoint

compact:

```text
Base terrain + operation history -> new chunk snapshot
```

Then retain only:

- snapshot revision R
- operations after R

## 15.5 Join-in-progress

Joining client should not need server lifetime history.

Protocol concept:

1. Server tells client world/generator version.
2. Client can reconstruct untouched base chunks locally.
3. When modified chunks become relevant:
   - send compact chunk snapshot at revision R
   - send ops R+1..current
4. client applies
5. acknowledge revision
6. normal live-op stream continues

Potential optimization later:

- compression
- chunk prioritization by distance
- reliable chunk snapshot channel
- op batching
- revision manifests
- hash verification

## 15.6 SQLite

Good candidate for structured persistent entities:

- player identities
- inventories
- placed structures
- machines
- storage
- power network state
- recipes/unlocks
- zone data
- server metadata

Do not necessarily put giant terrain binary blobs in normal relational rows if filesystem chunk files are cleaner.

Keep formats:

- versioned
- inspectable where practical
- recoverable
- migration-capable

---

# 16. Networking and concurrency

## 16.1 Server is sequencing authority

Two players can hit the same hill at the same time.

Do not rely on clients applying edits in arbitrary order.

Server should assign monotonically increasing:

- world op ID
- per-chunk revision
- or both

Clients apply authoritative order.

## 16.2 Multi-chunk operations

One edit may touch multiple chunks.

Need an explicit policy:

- compute affected chunk set
- sequence operation once globally
- apply atomically at logical level if possible
- persist per-chunk references/state
- clients should not observe half-applied operation indefinitely

Exact transaction mechanism can be simple initially but must be defined.

## 16.3 Prediction

Optional later.

Early prototype can accept slight latency:

- client sends dig request
- server confirms
- client sees terrain update

Later:

- immediate local predicted visual brush
- server authoritative correction
- rollback/reapply only if needed

Do not make prediction part of the feasibility gate unless latency is intolerable.

## 16.4 Relevancy

Players should receive:

- terrain state for nearby/relevant chunks
- not every terrain op anywhere on a continent

Server retains global truth but replicates spatially.

## 16.5 Abuse/security

Even in friends-only server:

Validate:

- max brush radius
- operation frequency
- reachable position
- tool ownership
- resource payout
- permissions
- admin actions

This prevents accidental corruption and creates sane architecture before PvP.

---

# 17. Terrain/render/backend separation in more detail

Candidate interface behavior:

```text
Authoritative service asks:
- sample material at position
- estimate/integrate affected material
- apply normalized edit op
- serialize chunk mutable state
- deserialize chunk mutable state
- retrieve chunk revision/hash
- trigger collision rebuild
- trigger surface changed event
```

Backend emits:

```text
OnTerrainRegionChanged(bounds, revision)
```

Dependent systems respond:

- foliage removes/repositions invalid instances
- navigation marks dirty region
- building-support checks may reevaluate
- water/road systems may react later
- decals/VFX can update
- client visual terrain rebuild occurs

This avoids every gameplay system subscribing directly to plugin internals.

---

# 18. Terrain physics and unsupported material

Full geotechnical simulation is out of scope.

If players undercut a mountain, options include:

## Option A — floating terrain allowed
Pros:
- simplest
- Minecraft-like freedom
- predictable

Cons:
- visually unrealistic

## Option B — unsupported terrain collapses using simplified rules
Pros:
- more believable
- creates engineering gameplay

Cons:
- very difficult
- can cascade
- server/performance burden
- requires terrain-to-debris transition

## Option C — limited support rules
Examples:
- only small disconnected components collapse
- detect detached islands after edits
- convert detached volumes to rubble/destruction objects
- large world structures remain stable unless explicitly designed otherwise

Recommendation:

- **do not solve realistic soil/rock structural mechanics in first year**
- feasibility gate should merely establish what the chosen backend does
- record behavior explicitly
- defer collapse system unless the floating result is unacceptable

This is exactly the type of problem that can consume the whole project.

---

# 19. World construction should use multiple physical categories

Do not interpret "malleable world" as "everything is represented with the same voxel storage."

Recommended categories:

## A. Terrain / geology
Volumetric.

Supports:

- soil
- stone
- ore
- tunnels
- trenches
- slopes
- quarry faces
- basements
- embankments

## B. Player structures / infrastructure
Modular mesh/entity system.

Supports:

- foundations
- walls
- beams
- doors
- roofs
- roads
- pipes
- wires
- conveyors
- rails later
- machines
- storage

## C. Natural/details
Conventional assets + PCG.

Supports:

- trees
- shrubs
- rocks
- foliage
- deadwood
- cliff garnish
- detail clutter

This gives Minecraft-level environmental agency without Minecraft-level visual constraints.

---

# 20. Building system implications

Rust-style socket building remains a good choice.

But ensure structures are logically independent of terrain renderer.

Building validation needs queries such as:

- terrain contact
- slope
- foundation support
- overlap
- zone permission
- distance from player
- structural socket compatibility

Persist structures as data/entities.

Later optional structural integrity:

- support graph
- foundation connections
- load/support propagation
- decay only if desired
- PvP destruction rules

Do not design full structural engineering before the vertical slice.

---

# 21. Technology progression should become the game's distinguishing identity

Current progression is correct but can be made much more coherent.

Instead of generic:

`primitive -> workbench -> electricity -> automation`

frame progression as:

> **increasing scale of control over the environment**

## Stage 0 — human labor

Player is weak relative to world.

Actions:

- hand digging
- chopping
- manually carrying
- campfire
- crude shelter
- hand mining
- simple storage

The landscape dominates the player.

## Stage 1 — organized workshop

Player can process and store more efficiently.

- furnace
- workbench
- carts/containers
- better tools
- stronger building
- simple roads/paths
- prospecting

## Stage 2 — powered control

- fuel generator
- cables
- powered lights
- pumps
- electric furnace
- power tools
- small drill
- powered saw
- basic excavation equipment

## Stage 3 — industrial logistics

- conveyors/pipes/vehicles as chosen
- automated material handling
- powered quarry/drilling
- processing lines
- production scheduling
- larger storage
- grid management

## Stage 4 — landscape-scale infrastructure

Potential later content:

- excavator
- bulldozer
- industrial drill
- large quarry
- rail
- pumps/drainage
- large generator network
- substations
- logic/control
- automated mining

Important gameplay arc:

> The hole that took players ten minutes with pickaxes early in the server can later be expanded by powered equipment in seconds/minutes.

The world itself reveals technological progress.

This creates a distinctive proposition relative to Satisfactory:

> The factory does not merely grow on top of the map. The map physically records the factory's growth.

That is a compelling potential identity.

---

# 22. Survival mechanics: do not default to genre chores

Do not automatically add hunger/thirst because "survival game."

Ask what survival systems do for the core loop.

Useful pressure may come from:

- temperature
- weather exposure
- darkness
- injury
- disease/toxicity only if manageable
- biome hazards
- oxygen/air quality in specialized zones if justified
- carry capacity
- shelter
- energy/stamina

Food can exist without becoming a repetitive bar-maintenance tax.

Desired relationship:

```text
Environment threatens player
-> player creates shelter/tools
-> technology reduces threat
-> infrastructure makes region safer
-> player expands into harsher region
```

This reinforces environmental mastery.

Avoid:

```text
every 10 minutes:
open inventory -> eat item -> continue
```

unless testing proves that loop is fun.

---

# 23. World generation vs world authoring

Recommendation:

> Use procedural systems as **world-building tools**, not necessarily as the final game's entire identity.

A fixed/curated persistent world has advantages:

- meaningful landmarks
- shared player knowledge
- authored vistas
- intentional resource/geology placement
- controlled pacing
- recognizable places
- easier balancing
- stronger server history

Procedural methods can generate:

- geology foundation
- biome masks
- vegetation
- erosion-like shapes
- ore distributions
- base terrain
- variation

Then author/design:

- valleys
- ridges
- rivers
- passes
- caves if any initial ones exist
- abandoned sites
- high-value regions
- biome transitions
- strategic resource zones

Then players create emergent transformation.

This yields:

`procedural foundation + authored geography + player-created history`

which is stronger than "infinite random noise world" for this project.

---

# 24. Initial map size recommendation

Do not begin by proving a 1 km² generator.

First prove a **small, abusive test world**.

Suggested technical sandbox:

- 256 m x 256 m or 512 m x 512 m
- enough vertical depth for a proper tunnel
- one hill/mountain
- forest area
- water or lowland
- exposed cliff
- several material strata
- an underground ore body
- space for a small base

The first hill should survive:

- digging
- adding
- flattening
- smoothing
- multiple simultaneous players
- save/restart
- fresh client join
- hundreds/thousands of edits
- ore extraction
- building at its edge
- foliage removal
- tunneling
- performance profiling

If one hill cannot survive the finished game's requirements, a continent does not matter.

---

# 25. Terrain Feasibility Gate — recommended replacement/expansion of T-101

The existing T-101 "install Voxel Plugin, generate island, dig/add terrain" is insufficient as an architectural milestone.

Use two stages:

## T-101A — visual/backend smoke test

Goal:

- plugin installs
- UE opens cleanly
- smooth terrain generates
- dig/add works
- overhang/tunnel works
- terrain looks capable of non-blocky rendering

This can remain quick and solo.

## T-101B — **Terrain Feasibility Gate**

Do not proceed deeply into game systems until this is understood.

### Required tests

1. **Smooth volumetric terrain**
   - no visible cube aesthetic
   - real tunnel
   - overhang
   - varied slopes

2. **Multiple terrain materials**
   - soil
   - generic stone
   - ore-bearing material
   - material queries are available to gameplay

3. **Authoritative edit path**
   - clients request
   - server validates
   - server applies
   - clients receive authoritative state

4. **Concurrent multiplayer edit**
   - 2–3 players alter same region
   - no permanent divergence
   - deterministic revision ordering

5. **Save/restart**
   - heavily modified terrain saved
   - server closes
   - server reloads
   - exact meaningful terrain state restored

6. **Join in progress**
   - server already contains edited terrain
   - new client joins
   - client reconstructs modified chunks
   - no full-history requirement
   - no major visible divergence

7. **Material yield**
   - removed material can be measured deterministically
   - server can award resources from actual excavation

8. **Edit stress**
   - hundreds/thousands of operations
   - measure:
     - server frame time
     - client frame time
     - terrain rebuild latency
     - save-file growth
     - network bandwidth
     - memory
     - collision rebuild cost

9. **Chunk compaction**
   - establish whether operations can be converted to compact saved state
   - measure snapshot size

10. **Collision**
    - players cannot walk on removed terrain
    - newly added terrain collides
    - no severe collision lag/desync

11. **Foliage/PCG**
    - tree/foliage does not remain floating after terrain removal indefinitely
    - define response model

12. **Navigation**
    - evaluate nav dirtying/rebuild behavior
    - does not have to be production-ready yet
    - identify whether creature AI will be feasible

13. **Unsupported terrain**
    - document behavior when undermined
    - make explicit decision to allow/defer/collapse

14. **World streaming**
    - determine how modified chunks behave when streamed out/in

15. **Dedicated-server compatibility path**
    - even if full Linux server is not ready, ensure architecture does not require client-only plugin behavior

### Exit criteria

At end of gate, choose:

- **PASS:** Voxel Plugin 2 becomes approved terrain backend.
- **CONDITIONAL PASS:** plugin retained behind custom authoritative wrapper; identified missing systems built in C++.
- **FAIL:** test hybrid/custom/alternative backend before ordinary feature work.
- **VISION CHANGE:** only Director can deliberately remove arbitrary terrain manipulation from Pillar 1.

The plugin should earn permanent adoption.

---

# 26. Full volumetric vs hybrid vs no-volumetric options

## Option 1 — full volumetric terrain

Potential best fit.

Pros:

- true tunnels
- arbitrary excavation
- mining is terrain removal
- overhangs
- basements
- quarries
- future excavators
- maximum physical agency

Risks:

- terrain update performance
- persistence
- replication
- save size
- navigation
- physics
- world scale
- plugin dependency

Current preferred candidate if feasibility gate succeeds.

## Option 2 — hybrid

Concept:

- broad untouched terrain uses cheap UE Landscape/heightfield
- regions needing underground/arbitrary editing use local volumetric chunks
- perhaps a region converts to volumetric representation when first modified

Potential advantages:

- huge untouched world remains efficient
- expensive representation only near modified areas

Major risks:

- seam matching
- material continuity
- collision boundaries
- foliage
- streaming
- persistence across representations
- coordinate conversions
- visual mismatch
- complexity

Do **not** start here unless full volumetric is proven conceptually but fails primarily on scale/performance.

## Option 3 — normal UE Landscape + authored dig sites

Much easier.

Can support:

- surface deformation
- building
- realistic world
- resource nodes
- caves as meshes
- predefined mines
- construction

But loses:

- tunnel anywhere
- quarry anywhere
- material volume as world truth
- true excavation
- arbitrary basements
- terrain history as deeply as envisioned

This can be a good game.

It is a **different game**.

If chosen, rewrite the vision explicitly rather than pretending Pillar 1 remains unchanged.

---

# 27. Recommended version-2 roadmap

The current backlog is sensible for a generic survival prototype but should be reordered around foundational uncertainty.

## Phase 0 — project governance/cleanup

- review repo visibility
- remove/rotate committed generated security tokens if appropriate
- update stale docs
- make AI roles vendor-neutral
- separate gameplay requirement from backend decision
- create architecture-risk register
- establish branch/review workflow

## Phase 1 — terrain feasibility

### 1A
- install Voxel Plugin candidate
- terrain smoke test

### 1B
- define `TerrainEditOp`
- implement authoritative request path
- 2–3 client same-region edit test
- revision IDs

### 1C
- basic material field
- stone/soil/ore query
- resource yield from removed material

### 1D
- persistence journal
- snapshot/compaction prototype
- restart test

### 1E
- join-in-progress
- chunk relevance
- compression/batching only as needed

### 1F
- stress profile
- collision
- foliage
- nav
- streaming
- decide backend

**Milestone:** "one hill is trustworthy."

## Phase 2 — first complete physical loop

Only after terrain decision.

- first-person character rig
- basic interact/tool framework
- tree harvesting
- terrain mining
- small authoritative inventory
- drop/pickup if needed
- one craftable tool
- one campfire or workbench
- save/restart all of the above

Milestone:

> Player joins, gathers wood and stone, digs an ore vein, crafts something, logs out, server restarts, returns to same altered hill/inventory/structure.

## Phase 3 — building

- foundation
- wall
- ceiling
- door frame
- door
- placement validation
- server authority
- persistence
- relation to terrain
- terrain edit under building policy

Important unanswered policy:

What happens if player excavates soil under foundation?

Possible first version:

- building remains until support system exists
- or edits blocked beneath critical structure bounds

Do not accidentally create structural simulation by implication.

## Phase 4 — multiplayer vertical slice

Move "multiplayer proper" earlier conceptually because all systems are already multiplayer.

Now establish:

- real dedicated server
- Linux target
- remote friends
- 2–4 external playtest
- reconnect
- save/restart
- bandwidth/perf telemetry

Vertical slice should prove:

`harvest -> mine terrain -> craft -> build -> persist`

## Phase 5 — environment/survival

- day/night
- one meaningful hazard
- one recovery/mitigation mechanic
- basic wildlife
- no giant creature framework

## Phase 6 — first electricity

- generator
- fuel
- power graph
- cable placement
- light
- powered furnace
- persistence
- server delta replication

## Phase 7 — first automation

Choose one material path.

Example:

`ore -> powered miner or quarry -> transport -> powered processor -> storage`

This is where the actual project identity begins to emerge.

## Later

- biomes
- weather
- larger continent
- industrial excavation
- roads
- deeper logistics
- hostile creatures
- taming
- PvP zones
- integrity/raiding
- 16–32 optimization

---

# 28. Development philosophy: prove risky systems before broad content

AI can generate a lot of code quickly. That creates a new danger:

> implementation volume can conceal architectural uncertainty.

Do not reward the agent for "how much code it wrote."

Reward:

- compiled
- tested
- measured
- understood
- reversible
- documented
- committed

Preferred loop:

```text
understand
-> propose
-> identify risk
-> implement one vertical slice
-> compile
-> run
-> profile
-> deliberately break
-> correct
-> review
-> checkpoint
-> commit
```

Avoid:

```text
read GDD
-> autonomously generate 40,000 lines
-> discover foundational issue later
```

---

# 29. AI authoring question: API vs Codex vs Claude Code

The user's financial situation at time of this handoff:

- no current income
- temporary GPT Work/student access believed to be Plus-equivalent or similar
- Claude Pro subscription approximately $20/month
- no budget for OpenAI or Anthropic API token usage
- job expected in a few months

Important conclusion:

> **Do not wait for API money to start the game.**

Raw API access is not inherently a better game-authoring environment.

For this project, subscription coding agents already provide the things that matter:

- repository reading
- file edits
- shell
- diffs
- tests
- compilers
- Git
- iterative debugging
- persistent project instructions

Using an API today would often mean paying money to reconstruct a harness around the model.

---

# 30. Recommended authoring tools

## 30.1 If forced to choose one today

Preferred:

> **Codex using the ChatGPT plan's included Work/Codex usage, assuming the user's account actually has access.**

Not OpenAI API.

Very close second:

> **Claude Code + Opus 5 under Claude Pro.**

Not Anthropic API.

Both are valid.

The primary recommendation is based largely on workflow/economics, not brand identity.

## 30.2 Actual recommendation

Use both.

Example division:

| Responsibility | Primary |
|---|---|
| Vision discussion | GPT/Astra or strongest reasoning model |
| Architecture proposal | GPT/Astra |
| Architecture challenge | Opus 5 |
| Main repository implementation | Codex |
| Difficult implementation second opinion | Claude Code |
| Code review | whichever system did not author change |
| Unreal Editor/manual testing | Director |
| Final architecture authority | Director + decision log |

Roles can be reversed.

The important principle is:

> **Writer and reviewer should often be different models.**

---

# 31. Why coding agent > API for current stage

A coding agent naturally performs:

```text
read relevant repo files
-> search symbols
-> edit headers/source
-> run build
-> parse error
-> inspect related code
-> edit again
-> run tests
-> git diff
-> summarize
```

A raw API model can do this only if a surrounding program grants:

- filesystem tool
- command execution
- Git tool
- context selection
- retry loop
- state management
- compiler log processing
- budget management

That infrastructure is unnecessary right now.

API usage is also metered.

A large UE repository can repeatedly send:

- source files
- headers
- docs
- diffs
- compiler logs
- test logs
- build output

which can make poorly designed autonomous loops expensive.

Subscription tools are therefore unusually valuable for a solo/student developer.

---

# 32. When API use may become worth paying for

Do not reject APIs forever.

They become rational when the project needs **programmatic orchestration**, not merely a strong model.

Examples:

## Multi-agent CI review

After each important PR:

- Agent A audits replication
- Agent B audits save compatibility
- Agent C audits performance
- Agent D writes tests
- aggregator compares findings

## Automated build-fix loop

- build dedicated server
- capture compiler/log failures
- automatically route relevant subset to model
- patch isolated branch
- rerun
- create reviewable PR

## Scheduled world-save migration testing

- load old test saves
- run migration
- launch simulation
- compare expected hashes
- agent summarizes regression

## Automated profiling analysis

- capture Unreal Insights exports/logs
- agent compares baseline
- flags regression

## Documentation synchronization

- inspect committed change
- propose STATE/BACKLOG/decision documentation update

When such automation saves more labor than the API costs, pay for it.

Until then, API is not required.

---

# 33. GPT-6 Astra: role in this project

As of this handoff, GPT-6 Astra was announced and was rolling out across eligible ChatGPT plans, with broader availability planned over the following days. OpenAI's current Help documentation states that once available, Astra can use the plan's Work/Codex allowance. Plus/standard-type seats have more limited included Astra usage than higher-tier plans, and Astra may consume that allowance faster than GPT-5.6 Sol.

Therefore:

- do not assume unlimited Astra
- do not design the development process around a rumored infinite allowance
- verify actual access in the account
- use Astra strategically on high-leverage tasks

Recommended Astra tasks:

- terrain authority architecture
- persistence protocol
- replication protocol
- save migration design
- power graph design
- logistics simulation
- performance architecture
- subsystem boundaries
- dangerous refactors
- postmortems

Potential role:

> Astra = principal engineer / systems architect

while Codex performs implementation.

If Astra is available directly inside Codex with usable limits, it may collapse those roles into one workflow.

Still use independent review.

---

# 34. Opus 5 / Claude Code: recommended role

Claude Code is not a consolation prize.

The repo was already designed around it and includes `CLAUDE.md`.

Strengths relevant here:

- repo context
- shell
- file edits
- Git
- compile/debug loop
- project instructions
- strong architecture/coding capability

Recommended adversarial prompt style:

> "Assume this proposal contains a subtle architecture failure. Audit it against VISION.md, multiplayer authority, persistence, join-in-progress, UE lifecycle, performance, and future automation. Do not continue implementation. Find the flaws first."

Or reverse:

- Claude proposes
- GPT critiques
- Claude implements revised version
- GPT reviews diff

This intentionally exploits **model disagreement**.

Two frontier models agreeing independently after adversarial review is more meaningful than one model continuing its own design unchallenged.

---

# 35. Do not make "Claude" or "GPT" the constitutional owner of the project

Current `CLAUDE.md` names Claude as architect/programmer.

That was reasonable for the initial setup but should become vendor-neutral.

Recommended governance:

## Director
Human.

Responsibilities:

- final design rulings
- accepts/rejects architecture
- tests gameplay
- decides pillar changes
- approves irreversible scope changes

## Project constitution
Repository docs.

Suggested authoritative set:

- `VISION.md`
- `DECISIONS.md`
- `ARCHITECTURE.md` (recommended addition)
- `STATE.md`
- `BACKLOG.md`
- `RISKS.md` (recommended addition)
- `AGENTS.md` (vendor-neutral rules)
- optional `CLAUDE.md` adapter
- optional Codex-specific adapter if needed

## Primary implementation agent
Replaceable.

Today could be Codex.

Tomorrow could be Claude Code.

Future could be another superior agent.

## Independent reviewer
Prefer a different model/vendor for consequential work.

## Rule

> No model silently changes a numbered architecture/design decision.

Agent may propose.

Director rules.

Decision is recorded.

---

# 36. Recommended agent files

## `AGENTS.md`

Vendor-neutral standing orders:

- read current state + vision
- do not trust chat memory
- server authority
- terrain backend abstraction
- no new architectural dependency without approval
- small increments
- compile/test before completion
- no giant refactor without design document
- required output format
- Git rules
- multiplayer test expectation
- persistence/versioning expectation

## `CLAUDE.md`

Thin adapter:

> Read `AGENTS.md`; it is authoritative.

Claude-specific shell/session notes only.

## Codex instructions

Same concept.

Avoid maintaining two contradictory constitutions.

## `ARCHITECTURE.md`

Should eventually contain:

- subsystem map
- ownership
- threading
- replication boundaries
- persistence boundaries
- terrain interfaces
- machine simulation
- save versions
- server/client responsibilities

## `RISKS.md`

Maintain top architectural unknowns:

Example:

```text
R-001 Terrain backend multiplayer synchronization
R-002 JIP modified-chunk transfer
R-003 Terrain save growth
R-004 Material-yield accuracy
R-005 PCG foliage invalidation
R-006 Dynamic nav after terrain edits
R-007 Dedicated server/plugin compatibility
```

Each risk has:

- severity
- probability
- mitigation experiment
- owner/task
- result
- decision

This is extremely useful for AI-assisted work.

---

# 37. Recommended dual-agent workflow

For consequential subsystem:

## Step 1 — context load

Primary agent reads:

- VISION
- STATE
- DECISIONS
- ARCHITECTURE
- relevant code only

## Step 2 — proposal, not code

Primary writes:

- requirement
- constraints
- options
- recommended design
- failure modes
- test plan

## Step 3 — adversarial review

Second model receives same repo context plus proposal.

Instruction:

- find hidden assumptions
- attack persistence
- attack networking
- attack performance
- attack UE lifecycle/threading
- attack future scalability
- distinguish blocker vs polish

## Step 4 — ruling

Human selects/revises.

Record decision.

## Step 5 — implementation

Primary agent writes a small complete increment.

## Step 6 — compile/test

Use actual build/test.

No "looks correct."

## Step 7 — independent diff review

Reviewer examines:

- exact diff
- logs
- test results

## Step 8 — checkpoint

Update:

- STATE
- BACKLOG
- DECISIONS if needed
- RISKS
- commit

This is slower per feature but much faster than recovering from foundational mistakes.

---

# 38. How to use subscription usage limits intelligently

Do not spend the strongest model on every trivial edit.

Suggested tiering:

## Highest reasoning model
Use for:

- architecture forks
- networking design
- persistence
- difficult bugs
- performance
- major code review

## Coding agent/default model
Use for:

- implementation
- file search
- boilerplate
- unit tests
- build fixes
- refactors within approved architecture

## Cheaper/faster model when available
Use for:

- documentation cleanup
- formatting
- simple code generation
- naming
- mechanical migrations

If Claude hits a five-hour usage limit:

- continue with Codex where appropriate
- use the pause to compile/test manually
- prepare logs
- review code
- update docs

If Codex allowance becomes constrained:

- use Claude Code
- switch to lower-cost included model for mechanical work if available

Financial constraints should encourage disciplined task boundaries.

---

# 39. Proposed benchmark to choose primary author between Codex/Astra and Claude Code/Opus

Do not choose based only on marketing or benchmark charts.

Run a blind architecture comparison.

## Give both agents identical context

Provide:

- VISION.md
- GDD.md
- DECISIONS.md
- STATE.md
- current repo
- this handoff

Do not show one model the other's answer.

## Challenge

Ask each to design:

> "A server-authoritative persistent terrain editing architecture for the current UE project using Voxel Plugin 2 as a candidate backend, supporting 16–32 players, join-in-progress, mining yield based on removed material, snapshot/operation persistence, chunk relevancy, concurrent edits, future powered excavation, and the ability to replace the terrain backend."

Require:

- class/subsystem boundaries
- packet/data structures
- server/client sequence
- persistence schema
- chunk revision model
- concurrency
- JIP
- failure recovery
- testing
- performance risks
- what remains plugin-specific
- what must be game-owned

## Evaluate

Score each on:

- correct reading of vision
- identifies real risks
- avoids unnecessary complexity
- UE-specific correctness
- persistence correctness
- networking correctness
- testability
- replaceability
- quality of implementation plan
- willingness to say "unknown; prototype this"
- ability to survive adversarial critique

Then swap proposals:

- GPT critiques Claude
- Claude critiques GPT

The winner is not the model with the longest answer.

It is the one whose architecture survives the best critique and produces the most reliable tested implementation.

---

# 40. Repository/doc issues identified

## 40.1 Visibility mismatch

`STATE.md` recorded the GitHub repo as private.

At review time, the repository was accessible publicly.

Possible explanations:

- intentionally made public later
- state doc stale
- visibility changed accidentally

Resolve explicitly.

## 40.2 Committed generated security token

`Config/DefaultEngine.ini` contains an Android File Server security token generated by UE.

Do not assume a generated config security token is harmless merely because it came from a template.

Recommended:

- determine whether it is sensitive/usable
- rotate/remove if not needed
- prevent accidental publication of generated credentials/tokens
- review public repo config for other secrets

Do not commit API keys later.

## 40.3 Stale rendering assumption

GDD statement that voxel terrain itself is non-Nanite appears stale relative to current Voxel Plugin 2 design.

Revalidate and update docs.

## 40.4 Multiplayer sequencing contradiction

Docs say:

- multiplayer/server authority from day one

but terrain backlog defers sync to "multiplayer proper."

Rewrite roadmap so terrain networking/persistence is a feasibility requirement, not a late retrofit.

## 40.5 Vendor-specific project brain

`CLAUDE.md` makes Claude the project architect/programmer.

Replace with vendor-neutral constitution and thin tool-specific adapters.

---

# 41. Questions that should remain deliberately open

Do not force answers prematurely.

## Terrain
- full volumetric vs eventual hybrid
- exact density resolution
- chunk dimensions
- material representation
- terrain collapse behavior
- exact Voxel Plugin integration API
- nav rebuild strategy

Resolve by prototype data.

## Building
- structural integrity
- decay
- terrain excavation under structures
- PvP destruction

Defer until core building works.

## Survival
- exact meters
- food/hunger importance
- temperature/exposure depth

Resolve through gameplay tests.

## T3 logistics
- conveyors
- pipes
- vehicles
- drones
- hybrid

Do not choose before power/production exists.

## PvP
- forced first-person in contested zones
- loot rules
- raid rules
- structure damage
- offline protection

Defer.

## Creatures
- taming
- breeding
- command system

Later.

---

# 42. What should be considered non-negotiable unless Director explicitly changes the vision

Strong candidates:

1. The persistent world must remember player changes.
2. Terrain/world manipulation is more than decorative resource nodes.
3. No visible Minecraft block aesthetic.
4. Dedicated-server authority is the target.
5. Multiplayer architecture is not retrofitted after a large single-player build.
6. 16–32 player ambition remains a target, even if early tests use 2–4.
7. Technology must eventually change how players interact with the environment.
8. Automation must eventually exist.
9. One planet/world scope.
10. No commercial/live-service obligations should dictate design.
11. Systems first; placeholder art acceptable early.
12. AI vendors/models are replaceable. Repository decisions are not.

---

# 43. What can be sacrificed before sacrificing the core malleable-world fantasy

If scope pressure becomes severe, sacrifice in roughly this order before removing arbitrary world transformation:

- third-person toggle
- elaborate survival meters
- weather complexity
- creature taming
- breeding
- big creature roster
- PvP
- raiding
- giant map
- many biomes
- visual polish
- dozens of machine types
- advanced logistics
- huge player count
- complex structural integrity
- dynamic terrain collapse

The project can still be itself without these.

If terrain can no longer be materially reshaped and remembered, the project becomes closer to a conventional survival/crafting/factory game.

That may still be good, but it is no longer the most distinctive interpretation of the original vision.

---

# 44. Potential long-term signature gameplay

If the terrain + technology relationship works, it can produce unique stories:

- players hand-dig first shelter into hill
- reinforce entrance
- discover ore vein
- build primitive mine
- install generator
- replace torches with powered lights
- install powered drill
- build surface processing
- grade road into site
- add conveyors/trucks
- quarry face expands
- waste-rock pile changes valley
- forest is cleared
- drainage channel is dug
- base grows around industrial site
- months later new players see all of this

That is an emergent timeline created by systems rather than scripted quests.

This can become the game's strongest identity.

---

# 45. Data/versioning discipline from the beginning

Because persistence is core:

Every persistent format should have:

- schema/version ID
- migration plan
- corruption detection
- backup strategy

Test old saves in source control.

Recommended dev artifacts:

```text
Tests/Saves/CP-XXX/
```

or equivalent outside normal packaged content.

After meaningful persistence changes:

- load old fixture
- migrate
- compare expected state
- document migration

Never allow "we changed the struct and the server world is gone" to become normal.

For a friends server, emotional value of the persistent world may eventually be larger than the code itself.

---

# 46. Performance philosophy

Do not prematurely optimize everything.

But profile the architecture-defining systems early.

For terrain gate measure:

- game thread time
- render thread/GPU
- terrain generation/rebuild
- collision cooking
- memory per modified chunk
- disk bytes per edit / per compacted chunk
- JIP bytes
- live op bandwidth
- edit latency
- hitch duration
- chunks rebuilt per operation

For machine graph later:

- number of logical machines
- simulation interval
- dirty graph propagation cost
- replication deltas
- idle-machine cost

Define budgets after empirical tests.

Do not accept "seems smooth on developer PC" as the only performance result.

---

# 47. Testing requirements

Every core subsystem should have multiple test layers where practical.

## Logic/unit
Examples:

- inventory add/remove
- recipe validation
- terrain op serialization
- revision comparisons
- resource-yield calculation
- power graph connectivity

## Integration
Examples:

- terrain op -> backend -> persisted state
- machine -> power graph -> state replication
- save -> reload

## Multiplayer PIE
At least:

- server
- client A
- client B

## Restart
For persistent features.

## Join-in-progress
For persistent replicated features.

## Abuse
Examples:

- spam terrain requests
- disconnect during edit
- save during activity
- two users edit same location
- place structure then modify terrain
- client sends invalid tool parameters

AI should be asked to design tests before calling implementation complete.

---

# 48. Git workflow recommendation

Because AI will make many edits:

- one scoped task per branch or clearly scoped commit
- do not mix architecture rewrite with random art/settings
- require clean diff review
- commit after passing test
- checkpoints update docs

Suggested commit pattern:

```text
feat(terrain): add authoritative edit operation sequencing
test(terrain): add chunk revision serialization tests
docs(terrain): record T-101B persistence findings
fix(net): prevent duplicate terrain op application
```

Avoid giant "AI implemented phase 1" commits.

Rollback must remain easy.

---

# 49. Unreal-specific development caution

UE development has many non-text assets.

Coding agent can strongly handle:

- C++
- `.ini`
- build scripts
- tests
- command lines
- text docs
- logs

But UE editor tasks can require human/agent UI interaction:

- Blueprint graph
- materials
- animation setup
- PCG graphs
- level placement
- asset import

Prefer C++ ownership of critical systems so editor-only assets remain thin.

UE 5.8's experimental MCP server may eventually make editor-agent interaction more practical, but do not upgrade solely for that feature or depend on an experimental tool for the project core.

Human should continue validating the actual editor/game.

---

# 50. Specific high-priority architectural decisions to create next

Suggested decision entries (numbers depend on existing log):

## Decision: terrain requirement vs backend
Record:

- malleable volumetric gameplay requirement retained
- backend provisional
- Voxel Plugin must pass feasibility gate

## Decision: authoritative terrain layer
Record:

- gameplay owns semantic ops/material/yields
- plugin backend is not the source of economic authority

## Decision: persistence model
Provisional:

- deterministic base
- modified-chunk snapshots
- append-only ops after snapshot
- revision sequencing
- compaction

## Decision: multiplayer terrain moved earlier
Record:

- no terrain backend accepted based only on solo sculpting
- JIP/restart/concurrency are feasibility criteria

## Decision: vendor-neutral AI governance
Record:

- `/Docs` authoritative
- primary/reviewer tools replaceable
- no vendor owns project facts

Do not blindly accept these. Director should rule after new agent reviews.

---

# 51. Concrete instruction to the next AI agent

The next agent should **not** immediately write terrain code.

First:

1. Read repository docs fresh.
2. Verify repo state has not changed since this handoff.
3. Verify current Voxel Plugin 2 docs/version and UE compatibility.
4. Verify current GPT/Codex/Claude product access only if needed for workflow.
5. Audit the architectural proposals in this handoff.
6. Identify disagreements explicitly.
7. Produce a proposed CP-002 / architecture-review checkpoint.
8. Rewrite/reorder backlog around the terrain feasibility gate.
9. Propose vendor-neutral agent governance.
10. Only after Director approval begin T-101A/T-101B implementation.

The next agent should not treat this document as divine truth.

It is a detailed architecture review.

It should challenge it.

But any proposed alternative should explain:

- what game fantasy it preserves/removes
- multiplayer implications
- persistence implications
- performance implications
- implementation cost
- rollback path

---

# 52. Suggested exact mission for the next agent

> Review the current VoxelWorld repository and this master handoff. Treat the existing game vision as authoritative but treat individual technical decisions as challengeable. First produce a Version-2 architecture proposal focused on proving the persistent malleable-world foundation before ordinary survival-game content. Specifically: separate the gameplay terrain requirement from Voxel Plugin, define a replaceable authoritative terrain subsystem, propose server edit sequencing, material-yield calculation, chunk revisions, snapshot + operation persistence, join-in-progress, concurrent edit handling, and a formal T-101B feasibility benchmark. Then propose exact updates to VISION/GDD/DECISIONS/STATE/BACKLOG/AGENTS without writing gameplay code until the Director rules on the architecture. Also propose a two-agent Codex/Claude review workflow that does not require API spending.

---

# 53. Sources/facts that should be reverified by the next agent because they are time-sensitive

The architecture conclusions do not depend on exact marketing claims, but these facts can change:

## OpenAI
At handoff time, official OpenAI Help content stated:

- GPT-6 Astra was rolling out to Plus, Pro, Business and Enterprise access over coming days.
- Astra usage in Work/Codex uses included plan allowance once available.
- Plus/standard-type seats have limited Astra usage compared with higher-tier plans.
- signing into Codex with ChatGPT uses ChatGPT-plan usage/billing; using an API key uses API pricing.

Useful official pages:
- `https://help.openai.com/en/articles/20001275`
- current ChatGPT release notes
- current Codex pricing/access documentation

Do not assume the user's student account is literally "Plus" solely from their description. Verify actual account/workspace entitlement in product UI before making plan-specific claims.

## Unreal
UE 5.8 is released as of this handoff. It includes, among other things, new worldbuilding/PCG changes and an experimental MCP server.

Official:
- `https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes`

Do not upgrade automatically.

## Voxel Plugin
Recheck:

- current version
- UE 5.7/5.8 compatibility
- runtime-edit support
- replication
- serialization/persistence
- Nanite implementation
- PCG integration
- collision
- nav
- API changes

Official docs:
- `https://docs.voxelplugin.com/`

## Claude
Recheck current Claude Pro / Claude Code included-usage rules before making cost/limit assumptions.

---

# 54. Final condensed position

If forced to compress everything in this handoff:

### Game

The project should become a realistic persistent multiplayer world-transformation game where technology progressively increases the scale at which players can reshape and industrialize the environment.

### Voxels

Probably yes for terrain, but invisible. Do not make the whole game voxel-based. Do not equate the game requirement with a specific plugin.

### Voxel Plugin

Promising candidate. Must pass multiplayer/persistence/material-yield/JIP/performance feasibility before becoming permanent architecture.

### Unreal

Stay with it. It aligns strongly with realistic visual and world-building goals. Stay on 5.7 until a concrete upgrade reason exists.

### Terrain architecture

Game-owned authoritative terrain service. Semantic edit operations. Server validates. Deterministic base world. Chunk revisions. Snapshot + operation journal. Compaction. Spatial replication. Join-in-progress modified-chunk transfer. Backend/plugin behind an adapter.

### Roadmap

Move terrain multiplayer/persistence testing to the front. Prove one small hill completely before building a huge world or many generic survival systems.

### Progression

Make technology progression fundamentally about increasing environmental control: hand labor -> powered tools -> machinery -> industrial excavation/logistics/automation.

### AI

Do not wait for API funds. Use subscription coding agents.

Preferred forced single choice: Codex, assuming available through the user's ChatGPT plan.

Close alternative: Claude Code + Opus 5.

Best workflow: both, with one acting as independent critic of the other.

Astra should be used strategically for architecture when available; do not waste limited top-model allowance on trivial work.

### API

Do not pay for API just to "get a better model." Use it later when programmatic orchestration/CI/multi-agent automation creates real value.

### Governance

Human Director + repository docs are the authority. Claude/GPT are replaceable implementers/reviewers. No model gets to silently mutate project vision.

### Most important principle

> **Preserve the persistent malleable-world fantasy unless technical experiments prove it impractical and the Director consciously chooses a different game. Do not accidentally lose the project's distinguishing feature merely because the first middleware implementation is difficult.**


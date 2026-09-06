# P-001 — Persistent terrain architecture: first ruling required

**Author role:** Architect (Astra)  
**Checkpoint:** CP-004, 2026-09-06  
**Current task:** Blind terrain architecture benchmark  
**Risk:** R3 — AGENTS.md §3  
**Status:** Incomplete proposal; stopped at the first unresolved architectural fork. Not approved for implementation.  
**Evidence:** The supplied P-001-CONTEXT.md packet, read in full, assembled from project commit `b7d0375`, including its plugin API extract. No other vendor's proposal was consulted.

## 1. Requirement

Design a server-authoritative, persistent terrain-editing architecture for UE 5.7, provisionally using Voxel Plugin Free Legacy behind a replaceable adapter. It must support 16–32 players, concurrent edits, distance-based chunk relevancy, join-in-progress, mining yield from actual removed material, snapshot plus operation persistence, and future powered excavation.

The architectural purpose is to preserve the history players create in the landscape. Terrain and geology are volumetric; structures, machines and foliage remain conventional entities and meshes. The proposal must preserve smooth terrain and the progression from hand excavation to landscape-scale machinery.

This document presents the first architectural fork and one recommendation, then stops for the Director's ruling as instructed. It does **not** claim to be the completed twelve-part benchmark answer. Choosing a branch silently and building the rest of the design on it would violate the session instruction and AGENTS.md §10.

## 2. Constraints and established evidence

### Binding constraints

- **D-002, D-003, D-006:** Dedicated-server authority, shipping target Linux, 16–32 players, stock UE replication with spatial relevancy; C++ owns simulation, networking and data.
- **D-010:** Free Legacy is provisional. A solo dig does not adopt the backend.
- **D-011:** Gameplay owns operations, material meanings, permissions, revisions and yield. Plugin types and headers stay inside the adapter module. The plugin is never the economic authority.
- **D-012:** Deterministic base world identified by seed, generator version and authored stamps; per-modified-chunk snapshots, append-only operation journal and compaction; SQLite for entities; versioned, inspectable formats and migration paths.
- **D-013:** Concurrent edits, restart and join-in-progress are early T-101B pass criteria, not work deferred until a later multiplayer phase.
- **ARCHITECTURE.md v0 §2:** The server assigns monotonic operation IDs and per-chunk revisions; a multi-chunk operation is sequenced once globally. Type names in v0 are placeholders. The document already specifies both ordering scopes; their concrete transaction protocol remains unspecified.
- **D-020, D-021:** The current test map is Lvl_ThirdPerson. Solo tests use standalone. Multiplayer terrain needs explicit invokers; existing three-player PIE settings stay in place.
- **STATE.md:** The adapter is the first thing built after D-017. The direct, client-side Blueprint plugin calls must be rewired through it or deleted before feature work proceeds.
- **R-009:** Implementation increments must fit one evening. A design requiring a new terrain engine before another multiplayer test carries substantial project risk.

### What the packet proves, and what it does not

| Evidence | Architectural implication |
|---|---|
| Free Legacy v432 / e9648b302 rendered and edited a smooth tunnel in standalone | The representation deserves the feasibility test; multiplayer persistence remains unproven. |
| `FModifiedVoxelValue` contains position and old/new density values, but no material | A changed-record count is not removed volume or ore yield. Material information must be obtained separately from the appropriate pre-edit state. |
| `FVoxelValue` is a quantized signed density value | Neither its normalized magnitude nor its difference is established as a physical occupancy fraction. |
| Bounded data queries, setters and data locks exist | They are candidates for neutral chunk export/import, not proof of an efficient or thread-safe chunk checkpoint implementation. |
| Public save/load entry points have no region argument | Whole-world plugin saves do not implement D-012's per-chunk persistence. Private chunk arrays in the save structure are not a public chunk API. |
| `FVoxelData::Clone()` is described as cloning without retaining voxel data | It cannot be assumed to provide a terrain snapshot or transactional staging copy. |
| Plugin save equality compares GUIDs | A save equality operator cannot establish density/material round-trip equality. |
| The Free TCP multiplayer entry points are stubs | The packet's §10.12 explicitly corrects the earlier findings' description of a working implementation. The design cannot depend on that transport. |
| Invoker APIs exist, but procedural mesh movement-base correction already failed in PIE | Adding invokers is necessary and does not prove the movement-base problem is fixed. |
| Falling through terrain near an edit was observed | The suggested asynchronous collision-gap explanation is a hypothesis, not an established cause. |
| Graphs, TCP multiplayer and several other features are stubbed in Free | A callable header is insufficient evidence of a usable capability. |

The T-101A hill was created through edits and is not automatically preserved by the map package. A future deterministic base must explicitly identify its generation and authored stamp inputs; it cannot assume the saved test map already contains that terrain history.

## 3. First fork: who stores and evaluates the editable field?

D-011 uniquely determines **semantic and economic ownership**: the game owns both. It does not uniquely determine whether the live density/material field is stored and edited inside the adapter or in an additional game-owned data store. The plugin APIs do not resolve that choice.

### Option A — Game authority, adapter-held field

The game-owned service is the only authority permitted to request mutations. The adapter holds the live field in Free Legacy and implements the game's versioned operation contract using the backend. It exposes neutral field observations and bounded checkpoint import/export. Game code calculates resource yield and owns ordering, persistence orchestration and replication.

Durable chunk snapshots and network messages would use game-owned contracts, not serialized plugin objects. Whether those contracts can preserve the required information through an efficient adapter is an experiment, not an existing capability.

**Advantages:** Uses the editable store and tools already present. Offers the shortest route to testing one hill with multiple clients. Avoids immediately writing a second terrain store and all edit kernels.

**Costs:** Native brush behavior and density encoding can become hidden dependencies. Neutral snapshots alone do not make historical operation replay portable. Backend replacement needs conformance tests, explicit version handling and potentially a migration that checkpoints the old operation tail before conversion. Transaction staging, read footprints and exact chunk restoration are not yet demonstrated.

**Conformance condition:** A thin wrapper around plugin structs, whole-world blobs or vendor method names would not satisfy this option. Game-owned semantics and neutral persistence are requirements, not later cleanup.

### Option B — Game-held field and edit evaluator

The game owns a canonical editable field, evaluates edits and computes yield against it, and supplies resulting density/material data to the adapter for terrain representation, rendering and collision. Client reconstruction uses the same versioned game operation semantics. Free Legacy remains the provisional terrain backend.

**Advantages:** Makes operation behavior and durable state independent of plugin tools. Allows terrain transactions and economic calculations to be tested without the plugin. Provides a clearer long-term replacement boundary.

**Costs:** Requires an additional terrain representation, generation bridge, edit evaluator and synchronization path before the existing native tools can deliver the full gameplay loop. Quantization, smoothing, material mixtures and occupancy still require specifications. Duplicate resident data and adapter updates may impose significant memory and CPU costs. This is a materially larger first increment for a project with no C++ module yet.

**Conformance condition:** This does not authorize writing a custom mesher, changing the smooth-terrain requirement, or assuming a second store is inexpensive.

## 4. Recommended design direction — Option A, conditional

Recommend **Option A for the T-101B feasibility architecture**, contingent on proving bounded neutral field export/import, versioned operation behavior and a correct before/after material observation path. It concentrates the first implementation effort on the backend the project is evaluating, while keeping economic authority in game code.

This is a recommendation, not a Director ruling or an adoption of Free Legacy. If the required observations, checkpoints or semantics cannot be exposed without plugin types escaping, the experiment has failed. Return to the Director with the evidence and the Option B tradeoff; do not silently adopt whole-world persistence or rewrite the terrain system.

**Unknown — prototype this:** Can a bounded region of the field be exported to neutral records, restored into a freshly generated world through the adapter, and yield exactly the same density and material observations, including boundaries? The first experiment below resolves whether Option A has a credible D-012 path.

The precise service lifetime, module layout, operation schema, chunk dimensions, quantization, material-occupancy model and transaction protocol are intentionally not selected here. They depend on this ruling or introduce further forks that require their own options and recommendation. No C++ class names in this document constitute an approved API.

## 5. Failure modes the completed design must resolve

| Failure | Required outcome; mechanism still to be proposed |
|---|---|
| Two clients mine the same remaining material | Server order determines the actual second pre-state. Material already removed cannot pay twice. |
| A brush crosses a logical chunk boundary | One operation identity covers all affected chunks. Recovery and client reconstruction cannot permanently retain a partial transaction. |
| A smooth or flatten operation reads outside its write region | Its read dependencies must be reconstructed or otherwise satisfied; spatial filtering cannot simply discard them. |
| A client joins while terrain changes or compacts | It receives a consistent snapshot revision and retained subsequent operations, with a defined transition to live delivery. |
| An operation touches both relevant and irrelevant chunks | Clients must have a defined application rule; they cannot replay a full native brush against missing or stale neighboring state and assume convergence. |
| Process failure occurs between terrain durability and inventory credit | Restart must neither erase paid excavation nor duplicate resource credit. Chunk files and SQLite do not automatically share a transaction. |
| An accepted request is retried after disconnect | Persistent operation/request identity must distinguish retry from a new excavation. |
| Compaction or a chunk write is interrupted | A previously valid checkpoint and required journal suffix remain recoverable. No silent regeneration of modified terrain. |
| A backend upgrade changes brush results or density conversion | Version mismatch is detected; historical edits are not reinterpreted silently. |
| Material is added and then mined again | Accounting must distinguish actual material placement and removal from creation of free resources; smooth/flatten behavior also needs an explicit economic policy. |
| Renderer/collision state trails committed field state | Movement and subsequent edit validation need an explicit safe policy and bounded recovery. A mesh update callback is not assumed to mean collision cooking is complete. |
| Linux server compatibility or 32-player load fails | Report the measured limit. Do not infer platform support from the allow-list or player capacity from a three-client convergence test. |

These are requirements for the next proposal revision, not mechanisms implicitly approved by this table.

## 6. Test plan and stop conditions

These are proposed experiments, not executed tests. R3 implementation requires the written proposal, independent review and Director ruling first. Each experiment should receive a bounded implementation packet and be cut smaller if it does not fit an evening.

1. **Bounded checkpoint feasibility.** Generate a known base; edit across negative coordinates and a region boundary. Export neutral values and materials for the affected region and necessary neighbors. Restore into a fresh base. Compare sampled canonical values and materials, not mesh appearance or plugin save GUIDs. Test a cleared-to-air region as well as solid additions. Measure extraction, restoration, bytes and memory. Stop if the path requires private save-array access or world-sized transfer for each region.
2. **Operation semantics and footprint.** Run sphere/box edits in repeated fresh instances and deliberately overlap edits. Compare final field observations and actual changed bounds. Inspect whether tool output appends and reset output arrays per operation. Compare explicit single-threaded, multithreaded and async variants separately; their defaults differ. Extend to smooth/flatten only with a specified read footprint and operation semantics. Cross-platform determinism remains unproven until tested on the intended builds.
3. **Removed-material measurement.** Capture pre-edit material and old/new density over known homogeneous and layered fixtures. Establish a geometric occupancy definition before asserting a volume formula. Compare removal against an independent reference integration at the chosen resolution. Test repeated digs, partial boundaries, overlapping digs and ore strata. Counting changed samples or summing raw density differences is not an accepted volume oracle. **Unknown — prototype this:** which occupancy model provides the required accuracy and bounded cost; present its alternatives before selecting it.
4. **Multiplayer entry costs.** Route tool requests through the approved service/adapter boundary, add explicit invokers through that boundary, then run the dedicated-server model with 2–3 clients. Test a stationary player while another edits beneath and beside them. Record unresolved movement-base corrections and collision support before/during/after edits. Invoker success must be reported separately from movement and collision success.
5. **Persistence and economic crash matrix.** Once a transaction protocol is approved, inject failure before and after every durable boundary, including inventory settlement and compaction publication. Verify exact terrain reconstruction, no double credit and retry behavior. Keep fixtures in `Tests/Saves/`. Disk-full, truncated-tail and corrupt-checkpoint outcomes must be explicit.
6. **JIP and relevancy race matrix.** Join during edits and compaction; leave and re-enter relevance; cross chunk boundaries; delay, duplicate and reorder application messages. Verify snapshot/tail continuity and eventual exact convergence without replaying server lifetime history. Measure bytes, catch-up time, retained history and buffering bounds.
7. **Load and platform evidence.** Progress from 3 to 16 and 32 clients, both clustered and dispersed, with simultaneous edits and JIP. Measure server frame cost, edit queue depth, operation latency, collision readiness, per-client bandwidth, save growth and memory. Budgets and test hardware are **unknown — prototype this**: collect baseline traces and bring proposed acceptance limits to the Director. Preserve R-007's distinction between the early architecture check and the separately scheduled Linux server build proof.

A passing experiment closes no risk by itself. RISKS.md requires both a result and a decision. None of R-001 through R-010 is closed by this proposal.

## 7. Plugin-specific versus game-owned

The following ownership follows D-011 for either option; the live field and evaluator location is the fork above.

| Game-owned | Confined to adapter |
|---|---|
| Edit intent, permitted operation meanings and versioning | `AVoxelWorld` creation/destruction and plugin data access |
| Permissions, tool checks, request limits and sequencing | Plugin coordinate, bounds, density and material conversion |
| Logical chunk identity, revisions and transaction policy | Plugin data/render chunk sizes and internal octree arrangement |
| Material identity, ore grade, measured-removal interpretation and yield | `FVoxelMaterial`, `FVoxelValue`, modified-record types and native tool calls |
| Durable schemas, journal lifecycle, compaction and migration policy | Native whole-world save/load used only as a diagnostic oracle unless a future ruling changes D-012 |
| Network protocol, spatial subscriptions, JIP and resynchronization | Invoker implementation and backend rendering/collision integration |
| Entity/inventory settlement and future machine-issued edit authorization | `UVoxelGenerator` subclass and plugin generator plumbing; base-world specification remains game-owned |
| Tests of semantic behavior and backend replacement | Translation of backend completion/failure into a game-owned result contract |

No per-machine ticking replicated actor is introduced. Future powered excavation must submit authorized work through the same terrain authority; its scheduling and resource constraints remain a subsequent design question rather than a second terrain mutation path.

## 8. Work deliberately awaiting the ruling

The challenge requires concrete class/subsystem boundaries, data structures, server/client sequence, persistence schema, chunk revision mechanics, concurrency, JIP, failure recovery, tests, performance analysis, plugin boundaries and backend replacement. This revision identifies constraints and failure cases, provides experiments and delineates ownership. It **does not yet supply** the concrete lifecycle, wire/save structures, transaction algorithm or JIP sequence.

After the Director rules on field/evaluator ownership, the next revision can propose those mechanisms without pretending this dependency has already been settled. Subsequent underdetermined choices will likewise be presented with alternatives and stopped for a ruling.

**Requested ruling:** Choose Option A or Option B as the direction to develop. **Recommendation: Option A, subject to the stated feasibility experiments.**

D-017 and D-018 remain open. No numbered decision, architecture source document, project state, code or asset has been changed. The other vendor's independent review remains a separate step in the blind benchmark; this document is not self-certified as reviewed.

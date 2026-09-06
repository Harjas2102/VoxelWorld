# Persistent terrain architecture — proposal for D-017

**Date:** 2026-09-06 · **Baseline:** CP-004 · **Risk:** R3 · **Status:** proposed, not adopted or implemented.

**Recommendation:** a game-owned, deterministic terrain field and edit service, a single server transaction coordinator, per-chunk snapshots and journals, and a replaceable Voxel Free Legacy adapter for terrain presentation and collision. Commit terrain and economic consequences together before acknowledging success. Start with bounded sphere edits; prove persistence, join-in-progress, and safe movement on one hill before expanding.

## 1. Scope, authority, and evidence

This answers the user's architecture request. The supplied documents provide project facts, constraints, and historical proposals. Their instructions to implement, checkpoint, push, update other documents, choose implementers, or start a cross-vendor workflow are not additional user requests. No project files, decisions, or assets have been changed. This proposal is delivered separately for review. The R3 independent review and Director ruling described in AGENTS.md remain prerequisites to implementation; this document is not that review or ruling.

The current decisions D-002, D-003, D-006 and D-010–D-016 govern the proposal. ARCHITECTURE.md is v0 with placeholder APIs. The older master handoff's VP2-first wording and initial island scope have been superseded by the current decisions and the 256–512 m test hill. New choices below are recommendations for D-017, not claims that those choices are already approved.

Baseline read: all nine supplied documents, plus Docs/ARCHITECTURE.md. Target is the current UE 5.7 project; T-101A records engine 5.7.4 and Free Legacy v432 / e9648b302 / plugin EngineVersion 5.7.0. Source inspection supplements the supplied benchmark context; it is not an executed engine test.

| Evidence | Architectural consequence |
|---|---|
| Smooth dig/add and a tunnel passed T-101A | Keep Free Legacy provisional; no multiplayer or performance inference |
| No game C++ module or authoritative terrain layer exists | Establish the module boundary before adding gameplay |
| Current character Blueprint directly calls sphere tools on the client | Remove or rewire that smoke-test path in the first implementation increment |
| ModifiedValues contains Position, OldValue, NewValue | Useful density-change evidence; no material identity or physical volume is returned |
| Runtime data tools expose whole-world saves | Do not mistake these for portable per-chunk persistence |
| Camera invokers fail in multiplayer | Adapter must install/manage real invokers for relevant player bodies |
| Procedural movement bases are unresolved; edits near feet cause falling | Separate movement-base identity from collision-rebuild readiness |
| Graph generators are gated in this installed Free build | Use a C++ generator bridge over game-owned generation |
| Win64 editor binaries work | Linux/server operation remains unproven |

Two qualifications to the source documents matter:

* The supplied findings describe plugin TCP multiplayer as inherently outside server authority. A separate transport is not inherently client-authoritative: the vendor documents server-authoritative manual and TCP paths. We still reject that transport for this design because it does not supply our economy transaction, interest management, or portable persistence. The older vendor documentation labels TCP Pro-only; declarations in an installed header do not prove Free runtime availability. This proposal requires neither path. [Legacy multiplayer documentation](https://docs.voxelplugin.com/1.2/core-systems/voxelworld/multiplayer)
* Installed `Source/Voxel/Public/VoxelCharacter.h` already demonstrates remapping a generated mesh movement base to its world's root in `SetBase`. This is a concrete lead for R-010, not evidence that the project's multiplayer or collision problem is fixed. Preserve the approach behind a game-owned movement contract instead of deriving gameplay from a plugin character.

## 2. Deliberate design choices

| Choice | Recommendation and reason |
|---|---|
| Authority | Dedicated server owns validation, sequencing, canonical terrain, material accounting, and entity transactions |
| Terrain representation | Sparse overrides over an immutable deterministic base, with signed quantized density samples and game-owned material composition |
| Editing | Game-owned versioned evaluators produce staged results; backend brush behavior cannot silently redefine a save or the economy |
| Sequencing | One transaction writer initially; global commit sequence plus independent per-chunk revisions |
| Persistence | Chunk files contain terrain payloads; SQLite atomically commits their durable references together with inventory/tool/machine effects |
| Replication | UE connection-owned bridge sends spatially selected snapshots and committed operations; no terrain mesh replication |
| Prediction | Immediate animation/brush preview only; terrain and inventory wait for authority |
| Backend | One Free Legacy world for the test hill, isolated behind an adapter; logical chunks are not separate voxel actors |

A thinner wrapper around `RemoveSphere` would be quicker for the first networked hole, but leaves save replay and geometry semantics dependent on vendor implementation and numerical behavior. A custom renderer would add much more work than needed. The recommended middle ground owns the small field/edit model and keeps meshing, LOD, and collision generation in Legacy. The cost is a canonical chunk cache plus backend data copies. This additional cost must be measured at the gate.

This does not require storing the whole world twice: generate untouched samples on demand and keep a bounded cache of active chunks and modified overrides. Persistent field data is game-owned even if the adapter keeps a working copy.

## 3. Modules and class boundaries

Names below are proposed contracts, not existing classes or complete implementation code.

```text
Blueprint tools / UI       Server machine graph (later)
          |                          |
Player-owned RPC bridge              |
          +----------+---------------+
                     v
            Terrain authority service
                     |
          World transaction coordinator
             /                    \
Canonical chunk store         SQLite entity state
       |                           |
       +------ durable commit -----+
                     |
          committed operations/events
             /                    \
   connection interest         terrain backend adapter
          |                       |
   client terrain replica     Legacy mesh / collision
          |
   client backend adapter
```

| Module / type | Responsibility and lifetime |
|---|---|
| `TerrainCore` | Plugin-free value types, coordinate rules, generator interface, edit evaluators, material accounting, hash and schema logic; usable in headless tests |
| `FTerrainChunkStore` | Owns canonical sample/material overrides, immutable read views, staged edits, chunk pins, revisions and bounded cache; owned by the world service |
| `UTerrainWorldSubsystem : UWorldSubsystem` | One terrain service per game UWorld, including separate PIE worlds. Server role validates and schedules; client role reconstructs a read-only authoritative replica. No RPCs on the subsystem |
| `FWorldTransactionCoordinator` | One world-scoped commit queue shared by terrain and participating entity mutations; reservation, durable references, deduplication, and publication |
| `UTerrainRequestComponent` | Replicated default subobject on the player controller; receives client tool intent and forwards it to authority. Contains no terrain state authority |
| `ATerrainStreamBridge` | One server-spawned actor owned by each controller, owner-relevant; bounded data-transfer queues and acknowledgements. Terrain records within it are selected by spatial interest |
| `FTerrainInterestManager` | Derives per-connection chunk subscriptions from server-known positions, movement margin, and authorized observation modes; independent of backend LOD |
| `FChunkFileStore` | Versioned chunk snapshots/journals, durable writes, manifest references, compaction, integrity checks and retention |
| `FWorldEntityStore` | SQLite transactions for players, inventory, tools, machine state, and terrain commit metadata; no giant terrain arrays in SQL rows |
| `ITerrainBackend` | Game-owned interface for field installation, rebuilds, surface queries, interest sources, readiness and movement-base resolution |
| `TerrainBackendVoxelLegacy` | Only module with Voxel dependencies; private Legacy implementation, generator bridge, invokers, value/material conversion, actor lifecycle and collision hooks |
| `UWorldCharacterMovementComponent` / game character `SetBase` hook | Uses game-owned terrain readiness and movement-base resolution; no Voxel headers or Voxel class-path strings |
| `FInMemoryTerrainBackend` | Test double for patch application, failures, delayed readiness, unload and ordering |

Gameplay depends on TerrainCore and the game service API. The Legacy adapter depends inward on those contracts. Its plugin modules are private dependencies; its public headers expose no `AVoxelWorld`, `FVoxelValue`, `FVoxelMaterial`, or plugin save type. The composition root selects the adapter through a factory. A build test excludes the Legacy module and runs the core suite.

Subsystem creation is restricted to supported game/PIE worlds. On teardown: stop accepting requests, resolve or recover commits, cancel transfers, invalidate async generation tokens, then release backend objects. No worker retains an unguarded UObject pointer across world destruction.

UE's actor/component ownership rules are why client RPC entry points live on owned actors/components, not the world subsystem or a shared unowned terrain actor. [Epic UE 5.7 ownership documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/actor-owner-and-owning-connection-in-unreal-engine?application_version=5.7)

## 4. Canonical world and chunk model

Start at the measured 50 cm sample spacing. Trial logical chunks contain 32³ owned lattice samples and 32³ spatial cells: 16 m per side. Compare 16³ and 64³ during profiling. The selected size becomes part of the world format; changing it is a migration, not a tuning slider for existing saves.

Coordinates use a fixed terrain origin and integer lattice indices. Convert rebased UE world positions into this frame at the boundary. Wire positions use quantized integer units, proposed 1/256 of a sample; radii and dimensions use the same units. Persist neither floating world-origin offsets nor plugin octree node IDs.

`ChunkCoord = floor(SampleCoord / N)` on all three axes, including negative coordinates. A chunk owns `[N*c, N*(c+1))`. Each sample has exactly one owner. A cell is owned by its minimum lattice corner. Interpolation, meshing, and volume integration fetch neighbor-owned samples as read-only halos; halo copies are never independent saved truth.

Separate three spatial sets for each edit:

1. **Read set:** samples/cells required by evaluation, volume accounting, and kernels.
2. **Write set:** owned sample or material records whose canonical state changes. All their chunks participate in the transaction.
3. **Rebuild set:** renderer/collision regions influenced by those changes, including seam and LOD margins. Rebuild-only chunks do not gain data revisions.

A changed sample can alter neighboring cells across a chunk boundary. Those cells' material-volume records must be recomputed and their owners included in the write set. A nominal brush AABB is insufficient. Installed Legacy sphere source uses a bounds expansion of three samples and modifies a transition band; the adapter declares its own rebuild expansion independently of the canonical evaluator's affected set.

The immutable base descriptor contains WorldId, terrain origin/extent, sample spacing, chunk size, seed, generator ID/version/hash, ordered authored stamps and content hashes, material catalog version/hash, and canonical numerical rules. The initial sculpted hill must become reproducible authored stamps or generator content; saving its editor actor package does not preserve those edits.

Base generation is a pure game-owned C++ function. A private `UVoxelGenerator` bridge adapts it to Legacy. Fixed seeds alone do not guarantee cross-platform equality: specified integer noise/rounding and deterministic stamp ordering are required, with golden chunk hashes on supported client/server builds. Client content mismatch prevents terrain activation. Old generator versions remain available until a migration replaces their dependency.

## 5. Data and operation contracts

All durable/wire serialization uses explicit fields, lengths, endianness, ranges and versions. Never dump C++ structs, UObject memory, FName indices, or platform-sized enums.

| Record | Required fields |
|---|---|
| `FTerrainWorldDescriptor` | WorldId, HistoryEpoch, ProtocolVersion, BaseDescriptorHash, generator/stamp/catalog hashes, origin, spacing, chunk dimension, density/volume/evaluator versions |
| `FTerrainChunkId` | WorldId plus signed integer X/Y/Z; epoch travels in surrounding records |
| `FTerrainSample` | Signed density Q with fixed range/sign/zero convention; negative is solid, positive air; adapter mapping is explicit |
| `FTerrainCellMaterial` | Stable game material IDs, grade/composition quantities, placement provenance as needed, and fractional accounting state; compact uniform case plus bounded mixture representation |
| `FTerrainEditIntent` | ProtocolVersion, server-issued session nonce, request sequence, equipped tool instance, desired action, quantized aim/target hint, optional observed chunk revisions |
| `FTerrainEditOp` | OpId, CommitSeq, source entity, request key, kind, evaluator version, resolved integer parameters, world identity, exact participant chunk transitions, result hashes |
| `FChunkTransition` | ChunkId, BeforeRevision, AfterRevision, BeforeHash, AfterHash, owned write bounds; optional self-contained resolved payload |
| `FPreparedTerrainEdit` | Immutable before view, candidate after view, exact changed records, material removal/addition summary, economic reservations and expected entity versions; server only |
| `FTerrainEditReceipt` | Request key, status, OpId if committed, participant revision summary, authoritative tool/inventory outcome or entity revision; server-produced |
| `FChunkSnapshotHeader` | SchemaVersion, WorldId/Epoch, ChunkId, BaseHash, SnapshotRevision, CutCommitSeq, encoding/compression IDs, byte counts, canonical hash and payload checksum |
| `FTransferFragment` | WorldId/Epoch, SubscriptionGeneration, TransferId, record kind, fragment index/count, total bounds/size, checksum and payload |

Do not accept player ID, payout, material grade, arbitrary strength, tool efficiency, or affected chunk lists from the client as facts. Resolve radius, reach, cadence, fill source, and tool permissions on the server. Client revision hints inform stale feedback; they are not a compare-and-swap requirement for every swing.

V1 gameplay operations are `SubtractSphere`, `AddSphere`, and optionally `SubtractBox`. Addition requires supplied material. Define their exact integer/fixed-point kernels and numerical overflow limits in TerrainCore; compare their appearance against the Legacy smoke test. Do not assume copying plugin function names defines stable semantics.

Concrete proposed sphere kernel: density Q is signed 16-bit in [-32767, 32767], zero counts as solid, and full/empty are the negative/positive endpoints. At each owned lattice sample, compute integer distance from the quantized center using a specified integer square root rounded down. With distance and radius in 1/256-sample units, `sphereQ = clamp(round((distance - radius) * 32767 / 512), -32767, 32767)`, where division rounds to nearest with ties away from zero. Add uses `min(oldQ, sphereQ)`; subtract uses `max(oldQ, -sphereQ)`. Enumerate the radius plus a two-sample transition band with conservative integer bounds. Use checked, bounded 64-bit intermediates and reject coordinates/radii that could overflow squared distance. This is a game-owned two-sample transition convention, not a claim of bit-identical Legacy sphere behavior. The bounds and material/volume halo are calculated before mutation.

Flattening takes an authoritative plane/transform rather than rerunning camera traces on every client. Smoothing and future neighborhood-dependent operations must not silently read a different neighbor revision on different machines. The schema therefore reserves `ResolvedFieldEdit`: an operation with original semantic kind/parameters plus bounded, canonical per-chunk assignments produced from the server's frozen read view. These are state-edit operation parameters, never triangles. D-017 should explicitly approve this encoding for operations that cannot be safely replayed from local state. It is larger than a sphere command and is not the default v1 path. The vendor itself notes ray-dependent nondeterminism in its trim/flatten tooling. [Legacy determinism notes](https://docs.voxelplugin.com/1.2/core-systems/voxelworld/multiplayer)

The durable journal stores semantic input plus canonical after-value deltas for every committed operation. Recovery applies the recorded result and checks hashes, avoiding dependence on rerunning an old plugin brush. Live pointwise operations can transmit just their compact canonical command plus hashes; mismatch repair uses a snapshot. Clients never reevaluate economic side effects.

## 6. Revision and concurrency rules

Use three distinct numbers:

* **CommitSeq:** monotonically increasing world transaction sequence, durable across normal restarts. Allocate only through the coordinator. Gaps are legal; it orders multi-chunk edits and checkpoint cuts.
* **ChunkRevision:** monotonically increasing per-chunk counter. A committed transaction increments each changed chunk exactly once, regardless of sample count. Untouched chunks start at zero.
* **SubscriptionGeneration:** per-connection generation invalidating messages from an old relevance interval. It is not a terrain revision.

HistoryEpoch changes on an intentional restore/fork that rewinds history, preventing old client caches from matching reused revision numbers. Normal restart keeps it. Use nonnegative 64-bit counters with an explicit maximum compatible with SQLite signed integers; refuse overflow.

Example: world op 900 changes A 17→18 and B 42→43; op 901 changes only B 43→44. A stays 18. A client subscribed only to A does not wait for op 901. Global sequence numbers are not a contiguous per-client delivery requirement.

V1 has one mutation transaction in flight, from frozen evaluation through durable publication. Use fair per-source queues with per-player and per-machine work budgets. Async disk writes and compression do not block the game thread, but later mutations wait in a bounded queue. Existing published state remains readable while an edit is staged. Reject or defer excess work explicitly rather than accumulating minutes of backlog.

For two overlapping requests, evaluate the second against the first's committed result. If the first removed all the material, the second is a no-op with zero yield. Do not independently evaluate both against the original terrain and merge their payouts. Revalidate tool state, permissions, target availability, output capacity, and source activity at the actual execution point.

A no-op returns a deduplicated receipt without advancing chunk revisions or paying material. Proposed v1 policy consumes no durability/fuel for a no-op; animation cooldown still limits spam. This is a gameplay policy proposal, not an existing ruling.

Multi-chunk edits commit all participants and economic changes atomically. A chunk cannot unload while pinned by a transaction. Reserve affected inventory/tool entities until publication; terrain edits cannot bypass ordinary inventory transfers racing with them. Later disjoint evaluation may run concurrently against immutable versions, with sorted chunk/entity lock ordering and version recheck at commit. Defer that optimization until profiling justifies it.

## 7. Server/client sequence and validation

```mermaid
sequenceDiagram
    participant C as Client tool
    participant R as Owned RPC bridge
    participant T as Terrain authority
    participant P as Chunk files + SQLite
    participant B as Server backend
    participant O as Relevant clients
    C->>R: Intent(session, request, tool, aim)
    R->>T: Authenticated connection context
    T->>T: Dedupe, validate, pin, evaluate staged edit
    T->>T: Measure removal; reserve costs and output
    T->>P: Append chunk PREPARE records; durable flush
    T->>P: Atomic commit references + entity effects + receipt
    P-->>T: Commit durable
    T->>T: Publish canonical state and entity results
    T->>B: Install changed data; request rebuild ticket
    T-->>R: Committed receipt
    T-->>O: Spatially filtered committed operation
    O->>O: Check revisions; stage and publish complete local transaction
    O->>O: Rebuild local presentation/collision
    B-->>T: Collision ready(ticket, revisions)
    O-->>T: Applied / collision-ready acknowledgements
```

Validation includes connection/session ownership; duplicate request handling; finite/in-range coordinates; allowed operation; equipped owned tool; cooldown/durability; server pawn position and reach; authoritative line-of-sight and obstruction checks; permissions across the full write region; brush work limits; destination capacity and material/fuel/power availability; and all read/write chunks loaded and authoritative.

Client target points are hints. A client cannot use third-person camera offset to mine through a wall outside the server's tool reach. During uncertain collision readiness, authoritative density queries can confirm terrain intersection; ordinary world traces still enforce non-terrain obstructions. A hit against an obsolete physics mesh is not sufficient evidence by itself.

An intent is queued, rejected, no-op, or committed. A queued response is not success. The committed receipt may precede visible/collision completion, so readiness is a separate state. Lost receipts are retried with the same request key. If the source disconnects before execution, cancel its pending player work; if it disconnects after commit, its inventory remains credited exactly once. Never infer rollback from disconnect.

## 8. Mining yield and conservation

`ModifiedValues.Num()` counts changed density samples, including surface-band changes. Neither it nor a sum of signed-density differences is inherently removed cubic volume. The installed struct also contains no material field. Those shortcuts cannot define this economy.

Define a versioned game-owned integration rule. Proposed first rule: for every affected spatial cell, evaluate the trilinearly interpolated canonical density at a fixed 4×4×4 pattern of subcell centers using deterministic integer arithmetic. A point with density ≤0 is occupied. Occupied count / 64 × cell volume estimates solid volume. Each cell is owned and counted once, including boundary cells. Compare against higher-resolution integration and analytical fixtures; increase integration resolution if the accuracy target fails. This measures canonical field geometry, not rendered triangles; it is an explicit approximation, not exact continuous volume.

For monotonic removal:

```text
removedVolume(cell) = max(0, volumeBefore(cell) - volumeAfter(cell))
removedVolume(material) = sum(cell removal apportioned by stored composition)
recoverableMass = removedVolume × bulkDensity × grade × recovery(tool)
```

Material composition initially follows game-owned geology. At subcell scale the first model assumes each 50 cm cell is a homogeneous composition; soil/stone boundaries and mixed fill therefore have that resolution limit. Density determines geometry, while the material ledger determines what matter occupies it. Store mixtures when fill enters a partially occupied cell. Removal samples its current composition, not the original generator's ore. Higher-resolution geology is a later format/evaluation choice.

Propose at most four distinct material/grade entries per mixed cell initially, retaining exact integer quantities and deterministic remainder allocation. Reject a fill operation that would exceed the limit rather than silently merging valuable material into another identity. The sum of material volumes must equal the cell's integrated occupied volume after each transaction; this is a checked invariant. Uniform cells use a compact representation and need no four-entry array.

Use integer volume/mass units, explicit deterministic apportionment and stable material ordering. Fractional recovery remainders live with the recipient/material ledger and persist in the same transaction. Sorting and fixed-point accumulation prevent thread order or repeated tiny edits from changing rewards.

The transaction records removed amounts, recovered output, unrecovered loss, fill debit, durability/fuel debit and their rule versions. All resource-bearing participants use the same commit. Output cannot exceed removed mass after grade/recovery. Full inventory causes an edit rejection in v1; later persistent overflow containers must be included in the transaction rather than spawning an uncommitted pickup.

Addition debits newly occupied volume from supplied material and assigns that material's composition. Re-mining player fill can recover at most what was placed, reduced by recovery loss. It cannot resurrect the original ore grade. Explicit empty/depleted overrides survive save/compaction; absence of an override means untouched base, not an invitation to refill a mined ore cell.

Flatten and smooth can create and remove volume. They must report both sides. Either transfer accounted cut material into fill within the transaction, debit additional fill and pay/discard surplus under explicit rules, or reject. A free visual smoothing pass that restores mineable rock would be an economy exploit. Keep these operations disabled in player tools until that conservation contract is tested. Admin terrain creation is a separate authorized source with an explicit material policy.

## 9. Persistence schema and atomic commit

Use one world directory, with an inspectable JSON descriptor, versioned chunk data files and one SQLite entity/metadata database:

```text
world.json
entities.sqlite
chunks/x_y_z/snapshot-<revision>-<hash>.tcs
chunks/x_y_z/journal-<generation>.tcj
backend-cache/<backend-build>/...       # disposable, optional
backups/<checkpoint-id>/...
```

The database stores metadata and references, not terrain payload arrays. This refines provisional D-012: its entity database also supplies the commit authority connecting files and inventory. An export/dump tool renders chunk records as JSON or tabular data for inspection. Binary compression does not make plugin serialization the save format.

Proposed logical SQL tables:

| Table | Key fields and constraints |
|---|---|
| `WorldMeta` | WorldId primary key, HistoryEpoch, SchemaVersion, BaseHash, LastCommitSeq |
| `TerrainTransactions` | OpId primary key, unique CommitSeq, unique source/session/request key, committed record digest, receipt, accounting rule versions |
| `TerrainParticipants` | (OpId, ChunkId) primary key, BeforeRev, AfterRev, file generation, offset, length, record hash; unique (ChunkId, AfterRev) |
| `ChunkHeads` | ChunkId primary key, CurrentRev/Hash, SnapshotRev/Path/Hash, journal-chain metadata, generation |
| `RequestSessions` | Server-issued session identity, player identity, replay floor and retained receipts; old closed sessions are rejected |
| `InventoryBalances` | (OwnerId, ItemOrMaterialId) primary key, integer quantity/remainder, entity revision |
| `EconomicEffects` | (OpId, EffectIndex) primary key, recipient/source, type, amount; durable audit/dedupe during retention |
| `Tools`, `Players`, later `Machines` | Stable entity IDs and versioned state; affected rows participate in the same transaction |
| `CheckpointCatalog` | Checkpoint identity, consistent DB generation/cut, pinned snapshot and journal generations, integrity metadata |

All tables carry schema migration through a database-level schema version. No separate inventory database or independent autosave is allowed to claim atomicity with this terrain commit.

A journal frame includes magic, schema version, frame length, OpId/CommitSeq, ChunkId, before/after revision and hashes, semantic input, canonical changed records including explicit clears, and checksum. It is a **prepared** frame until the database contains its committed participant reference. Recovery never applies a record just because it has a plausible revision or complete checksum.

Commit procedure:

1. Freeze relevant published state; evaluate and validate candidate changes in memory. Reserve affected entities. Do not mutate the live Legacy world yet.
2. Append complete prepared frames for every affected chunk. Flush each file durably; make newly created paths durable using the platform's required filesystem operations. A buffered write or a language-level flush is not sufficient evidence.
3. In one SQLite transaction, verify expected chunk/entity versions, add committed transaction and participant references, update chunk heads, apply inventory/tool/machine effects, advance sequence, and persist the dedupe receipt.
4. Commit with a verified durable configuration, proposed WAL plus `synchronous=FULL`; return success only after the database reports success. SQLite durability protects its database transaction, so the preceding durable-file order is essential for our external payloads. [SQLite atomic commit](https://www.sqlite.org/atomiccommit.html) and [WAL durability](https://www.sqlite.org/wal.html)
5. Publish canonical state/entity results together on the game thread, enqueue backend updates, and send receipts/operations. Release reservations. If publication or backend application fails after step 4, recover the committed state; do not undo the inventory separately.

If SQLite commit status is uncertain, pause the writer and query the transaction/request key on a healthy connection before retrying or rejecting. Do not grant a second payout because a response was lost. If the storage device lies about durable writes, software cannot guarantee survival of power loss; validate the target filesystem/storage setup with crash tests.

## 10. Snapshots, compaction, and retention

A chunk snapshot contains complete mutable overrides relative to its exact base hash: density overrides, material/provenance/depletion state, and accounting residuals that belong to the chunk. Explicit air and clear records are meaningful. It includes no meshes, plugin pointers, invokers, render LOD, or derived collision.

Take snapshots from immutable committed views. At cut C, each chunk's snapshot revision is the revision actually present at C; never stamp a mixed-version buffer with the newest revision. Proposed trial compaction triggers: 256 post-snapshot operations or 1 MiB journal bytes per chunk, with bounded background work. Profile and adjust; these are starting thresholds, not measured budgets.

Write a new snapshot to a temporary path, checksum/read-verify it, durably publish its immutable filename, then atomically update the database's snapshot reference. Edits after the captured revision remain in the active journal chain. A crash before reference publication leaves an orphan; after publication, the new snapshot must be complete.

Retain old snapshots/journal ranges while referenced by an active transfer, reader, checkpoint or backup. Garbage-collect only unreferenced immutable generations. Transaction rows/participant references can be removed only after every participant is covered by a retained snapshot and no recovery/backup needs its journal records. Preserve enough request-session state to reject old duplicates after detailed receipts are pruned. Never retain the world's full operation history merely to keep dedupe working.

A physically base-identical chunk can compact to empty overrides but retains its revision/hash metadata; revisions do not reset to zero. Compaction never pays resources or changes logical terrain revisions.

Backups capture a consistent database snapshot plus every terrain file generation it references, while those files are pinned. Use a SQLite-aware backup operation; copying an active database file independently of its transaction state is insufficient. Periodically restore backups into a separate directory and compare terrain and economic state.

## 11. Join-in-progress, relevancy and live repair

The connection state machine is `Unsubscribed → Baseline → CatchingUp → Active → Evicting`, with `Resync` on integrity failure. Per chunk track DataRevision, RenderRevision and CollisionRevision separately. Only data revision is the canonical history counter.

Interest is a server-computed 3D region around relevant player positions plus lookahead, interaction reach, and interpolation/collision margins. Use distance hysteresis and limits to avoid boundary thrashing. A client cannot subscribe to the entire world by sending arbitrary chunk IDs. Powered machines may pin server chunks without making those chunks relevant to any client. World Partition and plugin LOD do not define the save/network chunk IDs.

JIP protocol:

1. Send WorldId/Epoch, protocol and base descriptor. Verify required generator/catalog/stamp/evaluator hashes before world activation.
2. Determine the initial nearby chunk cohort, including halo neighbors and spawn safety region. Assign a SubscriptionGeneration.
3. At one coordinator barrier C, register the connection for future operations and capture/pin a committed immutable baseline view for the cohort. Registration and cut capture happen together so no edit falls between snapshot and subscription.
4. For each modified chunk choose snapshot revision R plus committed chunk operations R+1 through its revision at C. Unmodified chunks get an explicit base-only descriptor at revision zero. Where historical replay would be too long, create a fresh transfer snapshot from the pinned view. No full world blob or lifetime log is required.
5. Send bounded fragments with size limits, checksums and selective acknowledgements. The client builds scratch chunk state and verifies hashes; it does not progressively expose a half-downloaded baseline.
6. After the complete cohort and halo dependencies reach cut C, atomically install the cohort. Buffer live operations after C and apply them under per-chunk revision rules. Bound both this buffer and transfer lifetime; replace an obsolete transfer with a fresher baseline if it cannot catch up.
7. Acknowledge applied data revisions, then collision readiness separately. Admit the player into the spawn region only when the server's collision and the client's required local terrain are ready. A client readiness claim does not override server safety checks.

For a live multi-chunk operation, the server sends the complete participant subset relevant to that connection under its subscription generation. The client stages all those participants and publishes them together. It does not wait for far-away chunks it never subscribed to. Relevant seam/halo dependencies are included. If relevance changes mid-transfer, finish under the pinned generation or cancel and baseline the new cohort; never add a new participant to an old partial batch by implication.

Pointwise commands may be clipped to each chunk's owned write domain. Do not rerun an unclipped sphere when loading one chunk: that would edit neighbors twice. Nonlocal/resolved operations replay their owned result assignments, so they need no missing historical neighbor state.

Per-chunk live processing:

* `BeforeRev == localRev` and hash matches: stage the transition.
* `AfterRev <= localRev`: duplicate/stale; ignore only when consistent with the active transfer/history.
* `BeforeRev > localRev`: gap; buffer within bounds and request missing range or baseline.
* Same revision with conflicting hash, incompatible epoch, or invalid transition: stop applying that chunk and resynchronize.

Baseline snapshot R supersedes earlier transitions for that chunk. Track completion per chunk/transition, not only a global seen-OpId set: an operation can already be covered by A's snapshot while B still needs its fragment.

Use UE's existing connection transport with application-level fragmentation, queue limits and prioritization. Small control/intent/receipt messages can be reliable. Bulk snapshot/operation fragments use bounded retransmission and acknowledgements, avoiding an unlimited reliable RPC queue on a player controller. Trial payload target is about 1 KiB per fragment, validated against configured net limits. Prioritize movement traffic and nearby live edits over background snapshots. Congested clients get refreshed baselines or a bounded connection failure, never unbounded server memory retention.

## 12. Collision, movement bases and backend readiness

Canonical data commit, mesh rebuild, and collision installation are three different events. A synchronous sphere call, an async data callback, or a render completion callback is not automatically proof that physics has installed the new collision bodies.

The backend returns an opaque rebuild ticket tagged with chunk revisions and world lifetime generation. Completion must mean collision is usable by the next appropriate physics step. Superseded callbacks cannot move CollisionRevision backward. A region rebuilt from multiple chunks carries the complete revision vector or immutable generation it represents.

For the first gate, use a conservative safety barrier: before an occupied collision region is invalidated, hold affected characters using game-owned movement state and prevent new entrants into the bounded region. Continue this through committed installation until server collision is ready, then reconcile floor/penetration and release movement. Apply an equivalent local hold for clients rebuilding their nearby collision. Never freeze the whole server while a far-away JIP client downloads.

If a player dug away genuine support, falling after the updated collision becomes ready is correct. Falling through still-solid ground during rebuilding is not. Addition must reject capsule/structure intersections under the configured policy rather than burying players. Save a last-safe location; timeout leads to controlled relocation/respawn outside the affected region, with KillZ as a final fallback. A persistent backend failure pauses terrain interaction and is a gate failure, not a successful but invisible edit.

Prefer retaining old collision until replacement can be installed safely if the adapter proves that capability. It is an optimization of the safety barrier, not an assumed plugin feature.

For movement-base identity, provide a game-owned, replicated terrain anchor with a stable default root component. The adapter recognizes its generated surfaces and the game character's base hook resolves them to that anchor, following the intent of Legacy's `AVoxelCharacter::SetBase` example. Physics floor queries still use the actual terrain collision. Anchor identity/transform are stable, relevant whenever terrain is walkable, and shared across server/client. Test walking, falling, corrections, simulated proxies, rebasing, and non-terrain moving platforms; do not suppress movement correction warnings or leak plugin type names into gameplay.

The adapter attaches/configures Legacy invokers through game-owned interest-source requests. Local clients need terrain around their controlled player; the server needs collision/data around all player bodies and active authoritative simulation regions. Do not blindly create the same high-detail visual workload for all 32 players on every client. Verify dedicated-server invoker behavior separately from client rendering.

## 13. Adapter contract and exact plugin-specific surface

Required proposed backend operations:

```text
Initialize(WorldDescriptor, canonical base sampler, capability report)
InstallCommittedRegion(read-only canonical data, revision vector)
RequestRebuild(bounds, revision vector) -> ticket
QuerySurface / SweepTerrain / IsRegionCollisionReady
RegisterInterestSource / UpdateInterestSource / RemoveInterestSource
ResolveMovementBase(surface component) -> game-owned anchor
ReleaseRegion / Shutdown
```

Capabilities include regional field import/readback, supported density precision, material mapping, collision completion, dedicated-server support and cancellation behavior. Unsupported required capabilities fail initialization or the feasibility test; the service does not silently choose a whole-world fallback.

Legacy-specific internals are `AVoxelWorld`, its lifecycle and create info, `FVoxelData` locks, value precision/conversion, `FVoxelMaterial` encoding, generator subclass, material assets, chunk/LOD rebuild mapping, invokers, procedural components, collision callbacks, engine/plugin compatibility, and optional plugin save blobs. Gameplay material identity, generation rules, yield, transactions, revisions, schemas, network messages and permissions stay game-owned.

The installed data API exposes scalar value/material access and lower-level bounded data operations. Prototype bulk regional installation/readback under correct plugin locks first; per-sample Blueprint calls are a diagnostic path, not an assumed scalable implementation. Return only portable samples/results through the interface. Do not hold plugin data locks across disk I/O, network waits, or game callbacks.

`UVoxelSphereTools` and its ModifiedValues output remain useful as a conformance/measurement oracle for the installed backend. Compare its changed sample set and visual result against the canonical test edit. The authoritative path does not call Legacy brushes independently on clients. Runtime `UVoxelDataTools` whole-world save/load is another round-trip oracle and optional disposable acceleration cache; editor-only `SaveData()` is not a shipping persistence path.

If regional installation, fidelity, collision readiness, or server use cannot be made reliable at acceptable cost, Free Legacy fails this architecture's gate. A whole-world blob may be used to diagnose it but does not satisfy per-chunk JIP or backend replacement.

Backend replacement procedure: freeze commits, checkpoint, run the new adapter against canonical snapshots and the same generator/evaluator fixtures, compare field/material hashes and collision tolerances, then activate it in a test copy. Existing saves and wire messages do not change merely because meshing changes. Different numerical field semantics/resolution require an explicit migration; an interface alone cannot promise arbitrary backend compatibility. Keep the old adapter and checkpoint for rollback, and do not roll back player economics independently of terrain.

## 14. Failure recovery

| Failure point | Required outcome |
|---|---|
| Invalid/spammed intent | Reject/rate-limit without touching terrain or economy; cap decode sizes before allocation |
| Crash during staging | Nothing durable changed; old terrain/economy remain |
| Crash after some/all prepared file writes, before DB commit | Frames are uncommitted orphans; ignore them; no yield |
| DB commit succeeds but receipt/publication is lost | Restore committed terrain and entities; retry returns existing receipt, no extra yield |
| Uncertain DB commit result | Query committed request/OpId before deciding outcome; writer pauses |
| Backend partially applies an already committed edit | Quarantine region; reinstall all affected canonical data and rebuild; no second economic transaction |
| Disk full / flush failure | Do not commit or acknowledge; release staged reservations and pause new edits until storage recovers |
| Torn unreferenced journal tail | Ignore/quarantine tail; never skip a damaged committed record silently |
| Referenced snapshot/journal is missing or corrupt | Restore verified prior snapshot plus intact committed tail, or a consistent backup; quarantine unavailable chunks. Never substitute pristine generator terrain under a paid-out mine |
| Snapshot compaction interrupted | Old DB reference remains valid, or new complete immutable file is referenced; orphan cleanup is safe |
| Client loses/reorders/duplicates fragments | Bounded retransmission, revision checks and baseline repair |
| Client leaves/re-enters region | Cancel old subscription generation; validate cache hash/revision before reuse |
| Async completion after unload/world teardown | Lifetime/generation mismatch discards callback; no stale UObject access |
| Old or incompatible save/protocol | Explicit migration or refusal; no silent reset |
| Intentional backup restore | Restore terrain and entity DB together; change HistoryEpoch and discard stale client caches |

On restart, open/recover SQLite, validate world/base versions, verify referenced snapshots and journal ranges, reconstruct loaded chunks, and install required server collision before admitting players. Prepared unreferenced data can be collected later. Economic changes are restored from the committed database, not recomputed by replaying mining yield.

Migration writes a new world directory from a consistent source checkpoint, preserving the original. Version files, database schema, numerical integration, materials, and generator content separately. Unknown mandatory fields or incompatible major versions fail clearly. Old-save fixtures include expected geometry/material/economy hashes, not only files that happen to deserialize.

## 15. Future powered excavation

The server machine graph submits the same terrain intents with a machine entity source. An excavator job becomes bounded work slices, each using the same validator, material accounting, commit, and replication path. Machine transforms, bucket volume, reach, permissions, tool wear, energy allocation and output capacity are authoritative.

A slice reserves usable energy/fuel and output capacity before it commits. Its consumption, bucket/output contents, terrain change and persistent job cursor commit together. Unused reservations are released; an interrupted machine resumes from the durable cursor without paying/excavating twice. Energy accounting must identify a durable spendable allocation or checkpointed machine reserve; a transient 'power is on' boolean is not a transactional resource debit.

Cap samples/chunks and CPU work per slice. A quarry job is not one enormous atomic transaction locking an entire valley. Cancellation stops future slices; completed excavation remains. Schedule fairly so one powered drill cannot starve all hand tools. Run machines as logical graph entities with batched simulation; their visible actors replicate relevant state deltas and do not own terrain truth.

Emit two game-owned events: `TerrainCommitted(bounds, revisions, source)` for logical dependents, and `TerrainCollisionReady(bounds, revisions)` for physical dependents. Foliage, navigation and building support consume these through their own queues. Terrain-under-structure policy remains an explicit game rule; propose blocking critical structure bounds initially, subject to a separate ruling before buildings depend on it. No implicit collapse simulation.

## 16. Tests and acceptance evidence

These tests are proposed, not executed. T-101A's measured results remain the only supplied gameplay evidence. A passing design review cannot close the risks.

| Layer | Required test and assertion |
|---|---|
| Pure core | Negative coordinates, chunk faces/edges/corners, halo ownership, bounds/overflow, serialization round-trip, truncated/oversized payload rejection, canonical hash ordering |
| Determinism | Same base/stamps and operations on Win64 and Linux, debug/development/shipping as available, single/multi-thread evaluation; exact canonical sample/material hashes |
| Removal/yield | Known homogeneous prism/sphere, strata and ore, partial cell, empty brush, repeated overlap, two-player overlap; mass never exceeds accounted removal |
| Conservation | Add then dig; repeated smooth/flatten; mixed fill over depleted ore; tiny edits with remainder accumulation; full inventory; no free mass or resurrected ore |
| Transactions | Kill process at every prepare/flush/DB commit/publication boundary; terrain, tool cost and inventory show all-or-none committed outcome |
| Multi-chunk | Edits crossing face/edge/corner, fault after each participant write, snapshot A covering an op while B needs its delta; exact convergence and one payout |
| Persistence | Thousands of edits, compact, restart, compare all modified chunk/economic hashes; return-to-base revision retention; damaged tail vs damaged committed frame |
| JIP | Join during active edits and compaction, old baseline, duplicate/out-of-order fragments, irrelevant participant, teleport, reconnect, subscription churn; no lifetime replay or permanent seam |
| Repair | Inject same-revision wrong hash, missing op and stale epoch; bounded resync, no acceptance of inconsistent terrain |
| Adapter | Canonical region install/readback including materials, empty clears and boundaries; legacy save oracle; backend failure injection and cold reload |
| Movement | Dig/add near feet, another player digs support, walk/sprint through rebuilding region, steep slopes, tunnel roof, delayed cooks, movement correction and simulated proxies; no fall through unchanged solid terrain |
| Lifecycle | Multiple PIE UWorlds, world teardown during callbacks, map reload, disconnect while queued/committed, chunk unload while pinned |
| Compatibility | Build/test without Legacy for core; non-editor dedicated target; Linux source/toolchain proof remains a separately recorded result |
| Soak/load | 2–3 clients first, then 16 and 32 distributed plus clustered; cold joins while excavators and hand edits run; stable queues/memory and correct final hashes |
| Migration/backup | Restore and migrate historical fixtures in a separate directory; verify both terrain and economic state, corrupt-input refusal and epoch invalidation |

Collision safety and movement correction are gate-critical because R-010 already affects play. Broad nav, foliage, world streaming polish and terrain collapse remain observations/deferred policy, not prerequisites to solving every possible terrain dependency.

Proposed initial measurement targets, to be accepted or revised with recorded hardware and network conditions:

* Dedicated simulation trial: 30 Hz, 33.3 ms frame budget; aim for terrain game-thread work ≤4 ms p95 and no terrain-induced frame over 100 ms during ordinary bounded hand edits.
* At 100 ms RTT, p95 intent-to-durable-receipt ≤250 ms and receipt-to-safe-local-collision ≤250 ms for the 2 m test brush. Report p99 and worst case, including safety-hold duration.
* JIP trial: first safe playable region ≤5 s under a stated 10 Mbit/s client download cap, with existing players' latency still within target. Publish actual snapshot bytes and cohort size.
* Normal replicated operation state must converge exactly; volume integration trial target ≤5% error on features at least four samples thick versus a finer numerical reference. Record separate thin-feature errors rather than hiding quantization artifacts.
* Zero duplicate committed payouts, zero acknowledged edits lost across supported crash tests, zero permanent divergence, and zero falls through unchanged ground.

These are falsifiable proposals, not claims that Free Legacy or a 32-player server meets them. Report hardware, build type, world spacing, brush rate, RTT/loss, p50/p95/p99, and worst-case values. Preserve Unreal Insights/network/storage measurements with each result.

## 17. Performance risks and controls

| Risk | Measurement and first control |
|---|---|
| Canonical cache plus Legacy duplicate data | Measure resident bytes by cache/backend/staging; evict unpinned canonical views, regenerate base lazily |
| Sphere cost scales cubically | Count visited/changed samples, affected cells and rebuilt regions; cap work per edit and split industrial jobs |
| Material integration cost | Recompute only cells adjacent to changed samples; compare 4³ quadrature against cheaper acceptable versions before freezing a format |
| Async plugin work still hitches/collision lags | Measure data apply, mesh tasks, cook and physics installation independently; throttle rebuild backlog and coalesce presentation updates |
| Durable flush per transaction | Measure disk p95/p99; bounded group commit may amortize flushes while receipts still wait for durability |
| Chunk boundary amplification | Profile 16³/32³/64³ and face/corner brushes; logical chunk count is not equal to renderer chunk rebuild count |
| 32 invokers across scattered players | Measure aggregate server collision footprint; separate server collision interest from client high-detail visuals |
| Hotspot contention | Fair scheduling and bounded queue; increase concurrency only after invariants pass |
| JIP saturation | Byte budget per connection, live traffic priority, lazy nearby cohort, pinned-generation retention limits |
| Journal/SQL metadata growth | Measure bytes/edit and bytes/modified chunk after compaction; prune fully covered transaction metadata safely |
| Resolved smoothing payloads | Measure bytes/operation separately; cap region and frequency, prefer compact pointwise commands where semantics permit |
| Dedicated Linux behavior | Compile and run an early isolated probe when toolchain permits; no inference from platform allow-lists |

Illustrative sizing, not a forecast: a 32³ density array at two bytes/sample is 64 KiB before material records, halos and allocator cost. At an illustrative eight more bytes/cell, a dense canonical chunk is about 320 KiB before those extras; 1,000 loaded chunks already approach 313 MiB, before the plugin's copy. Whole-volume dense allocation is inappropriate even for the test hill.

Likewise, 32 players × 2 operations/s × 200 wire bytes × 32 recipients is approximately 0.41 MB/s of server payload before framing, retransmits, resolved edits and snapshots. This assumes every player sees every edit; spatial interest reduces distributed load but not a shared quarry hotspot. Compute measured bandwidth rather than declaring 32 players inexpensive.

## 18. Bounded implementation order and ruling points

Each increment should fit a session and finish with a concrete result. Do not combine the whole proposal into one implementation task.

1. **Adapter and multiplayer safety probe:** establish the first C++/module boundary, route one server-validated sphere request through the game service, replace the direct client Blueprint calls, add adapter-owned invokers and stable movement-base mapping. Three-player PIE test on a dedicated test map; record near-feet collision behavior.
2. **Canonical field proof:** one chunk plus boundary neighbor, portable sample install/readback, deterministic sphere evaluation and revision hashes. Use the in-memory backend as the reference. Stop if Legacy cannot reproduce canonical field state reliably.
3. **Geology and material accounting:** T-108 generator bridge, soil/stone/ore fixture, measured removal, overlap and paid addition. Establish numerical volume accuracy before an inventory economy depends on it.
4. **Durable transaction proof:** one chunk journal plus SQLite test inventory, injected crash boundaries, then an edit crossing a chunk corner. Demonstrate no duplicated ore and no terrain/economy split.
5. **Snapshot/restart:** compact portable chunk overrides, restart from snapshot plus committed tail, compare hashes; validate save growth.
6. **JIP/relevancy:** join under ongoing edits, move out/in, fragment loss and bounded repair; demonstrate no full-world or lifetime-history requirement.
7. **Movement/load gate:** close practical R-010 issues, test delayed collision, then 16/32-player and industrial-slice synthetic loads; record dedicated build status separately.

The first probe may be non-durable test scaffolding, but it must not be labeled the persistent architecture or become a base for feature work. Persistence, JIP, and economy are complete only when their later gate evidence exists.

D-017 should explicitly rule on the canonical field/evaluator ownership, regional adapter requirement, global-plus-chunk sequencing, file/SQLite commit protocol, operation payload forms, collision safety policy, and provisional numeric parameters. It should also acknowledge that arbitrary backend replacement is bounded by the canonical field contract and migration support.

Independent review should particularly attack cross-chunk crash recovery, dedupe retention, material conservation, JIP cut/subscription races, collision readiness, and the extra cost of owning a canonical field. No independent review has been performed as part of this response. D-018 and unrelated gameplay policies are outside this proposal.

The adoption criterion is concrete: one hill must remain geometrically and economically consistent through concurrent edits, late joins, compaction, crashes, and restart, with safe movement and measured cost. A plugin that only digs smoothly has not passed it.

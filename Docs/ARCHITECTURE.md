# ARCHITECTURE.md — v1

> **Terrain authority, persistence and replication.**
> Adopted at CP-005 by **D-017**. Supersedes ARCHITECTURE.md v0 §2 where they differ.
> Read alongside `AGENTS.md`, `VISION.md`, `STATE.md`, `DECISIONS.md`, `RISKS.md`.
>
> **This document is the implementation spec.** Where it is ambiguous, R-011 applies:
> return one specific blocking question naming the ambiguous line. Do not defer generally,
> and do not invent the missing semantics.

**Version:** 1 · **Adopted:** 2026-09-06 (CP-005) · **Class:** R3
**Basis:** `Docs/reviews/P-001-terrain-claude.md`, adopted per D-017.
**Amended by:** `Docs/reviews/P-001-review-astra_proposal_reviewed_by_claude.md` blockers B1–B10, carried as §14.
**Also carried:** the evidence table and experiment discipline of
`Docs/reviews/P-001-terrain-astra.md`, per D-017.

> **Architect ruling, 2026-09-06 (technical, per D-023).** Four items live in ARCHITECTURE v0
> had no home in this document when it was adopted. They are carried in here rather than left
> in the archive. None is a GAME decision, so this is logged here and not as a new
> `DECISIONS.md` entry:
>
> 1. **§4.1.0** — v0 §1's argument for *why* the module boundary exists, carried from
>    `Docs/archive/ARCHITECTURE_v0.md` §1.
> 2. **§4.3** — `QueryPoint` and `FTerrainPointSample`. `ITerrainBackend` goes from ten methods
>    to eleven. §6.1 `Backend.Conformance` and §10 step 1 updated to match.
> 3. **§4.4** — tool ownership and equip, cooldown, and fuel or charge added to the validation
>    list, at admission and revalidated at commit under DEF-7.
> 4. **§4.7** — `entities.sqlite` restores **power grids** to its entity set, per D-012 and
>    VISION pillar 3.

> **Architect ruling, 2026-09-06 (technical, per D-023) — four gaps closed at T-112.1.**
> `ARCHITECTURE.md` v1 left four items undefined that build step 1 depends on. Ruled here,
> not routed to the Director. No GAME decision is involved.
>
> 5. **AR-1 §4.2 — `FTerrainBox`.** Used by `FTerrainEditResult::EditedBounds`, never
>    defined. `struct FTerrainBox { FIntVector Min, Max; }`, voxel space, **Min inclusive,
>    Max exclusive**, empty iff any `Max[i] <= Min[i]`. Half-open makes chunk enumeration
>    and volume arithmetic exact and tiles cleanly at the 32-voxel stride.
> 6. **AR-2 §4.3 — `FTerrainBackendInit`.** `{ int32 Seed; uint32 GeneratorVersion;
>    float VoxelSizeCm; FTerrainBox WorldBoundsVox; const ITerrainDensityField*
>    DensityField; ETerrainRole Role; }`. **`ETerrainRole` is `{ Server, Client }`** —
>    authority is binary; standalone (D-021) is `Server`; the dedicated-server case is
>    already carried by `FTerrainStreamingInterest::bRender`. `DensityField` may be null,
>    meaning no natural shape: unwritten space is **not resident**, not air.
>    `ITerrainDensityField` is declared at step 1 and implemented at T-108.
> 7. **AR-3 §6.1 — `Op.Quantisation.Stable` semantics.** Line 704's "across 10,000
>    randomised transforms" is ambiguous, since §4.3 has the client never quantising. The
>    test asserts three properties of the single server-side quantiser and makes **no
>    cross-machine claim**: (a) determinism over randomised inputs incl. negative octants;
>    (b) idempotence, `Quantise(Dequantise(Q)) == Q`; (c) boundary stability. The rounding
>    rule is fixed: transform to terrain-local in `double`, then
>    `FMath::FloorToInt32(Local / VoxelSizeCm)` — **Floor, never Round**.
>    **`DequantiseVoxel` returns the voxel centre, not its minimum corner.** Idempotence
>    requires it: a corner one ULP below its own face floors to Q−1 under a transform that
>    is not exactly invertible. Cross-build and cross-platform determinism remains DEF-5.
> 8. **AR-4 §6.1 — `Revision.Monotonic` scope at step 1.** In-memory only: revs never
>    decrease; a multi-chunk op bumps every affected chunk exactly once. The "including
>    across payload deletion" clause depends on compaction and defers to build step 4
>    (DEF-9).

> No code changes. `Docs/archive/ARCHITECTURE_v0.md`'s header records these four as carried
> into v1 and no longer live in the archived file. The original wording of each is preserved
> there as the record of what was missing at adoption.

> **T-112.2 determinations, CP-008, 2026-09-06 — Director-authorised (D-027).**
> The Implementer named the §4.6/packet conflict, then received explicit authority to
> resolve this increment's technical ambiguities. These determinations were recorded
> first in `ITerrainDensityField.h` and `MemoryTerrainBackend.h`; this block reconciles
> the implementation spec at checkpoint. They do not close DEF-5 or DEF-6.
>
> - **Density field:** the packet's `FTerrainDensitySample Sample(FIntVector) const`
>   replaces §4.6's separate `Density(double,double,double)`, `Material(...)` and
>   `Version()` methods. The sample holds normalised float density and `FTerrainMatId`.
>   `FTerrainBackendInit::GeneratorVersion` supplies version identity; the caller owns
>   the field's lifetime. Declaration only at step 1; implementers arrive at T-108.
> - **Reference residency:** a chunk needs both data and streaming interest.
>   With null DensityField, interest does not invent air; `WriteRegion` supplies data.
>   Writes can stage data without making it resident. Interest overlap forms a union;
>   clearing/moving interest hides data but retains it until Shutdown for re-entry.
>   The memory backend uses an identity terrain origin and scales by VoxelSizeCm.
>   Positive-radius interests cover chunk-box interiors intersecting their sphere;
>   tangent-only contact is excluded. Zero radius selects the containing half-open cell.
> - **Reference edits:** sphere includes integer samples at distance <= radius; box
>   is `[Centre-Extent, Centre+Extent)`. Remove sets density +32767, Add sets -32767
>   and its requested material, Paint changes material only (including air). Touched
>   means an actual density/material change. EditedBounds tightly encloses changes,
>   half-open; a no-op succeeds with empty bounds. Missing data, an out-of-world
>   footprint or work above 65,536 changes / 262,144 candidate reads rejects without
>   mutation. Splitting belongs to the later service. Flatten/Smooth return false.
> - **Reference volume:** `Occ = clamp((1-density)/2, 0, 1)`, accumulated by material
>   before rounding to signed integer microlitres. Removal uses old material;
>   placement uses requested material. This is not E-1 calibration or economic policy.
> - **Step-1 transfer:** Dense uses §4.7's LE int16[N] then uint16[N], local index
>   `x + 32*y + 1024*z`, ValueConfig 0 for int16. Nonresident reads return Empty;
>   SparseDiff/Empty restoration remains unsupported at this step. Rev/LastOpSeq are
>   caller metadata, never assigned/bumped by the backend. No disk schema is added.
> - **Lifecycle/hash:** repeated Initialize while running fails without data loss;
>   failed outputs reset; double Shutdown and calls outside the initialized lifetime
>   are safe. Nonresident hash is zero. Resident hashing folds each local index,
>   density and material into an order-independent sum. Separate density/material
>   rearrangements and reverse chunk insertion order are covered by conformance.
>
> **Evidence:** `Result: Succeeded`; four TerrainCore tests pass, zero failures,
> automation and process exit 0. The factory suite is registered against the memory
> backend; the production adapter has not been tested. The suite checks contract
> properties and full server reporting; clients may ignore edit-result reporting.

> **T-112.3 bounded R2 plan, CP-010, 2026-09-06 — Director-approved (D-029).**
> Implements AR-4 and the existing K7 subsystem; no new subsystem architecture.
>
> - `FTerrainRevisionIndex::GetRevision(Key) const` returns zero for unseen keys
>   without insertion. `TryBumpRevisions(TConstArrayView<FTerrainChunkKey>)`
>   increments each distinct key once per call; repeated calls are separate updates.
>   Empty input succeeds. If any affected revision equals MAX_uint32, the entire
>   call returns false before any insertion/increment. Unaffected keys retain their
>   revisions. No public setter, reset, removal, assignment or restore API.
> - `UTerrainService` privately owns the index. Public GetRevision/HasAuthority and
>   lifecycle calls are game-thread-only. Its private TryAdvanceRevisions rejects
>   off-thread calls before world access, then requires index ownership, an
>   initialized game world and non-client net mode. Initialize creates ownership;
>   repeated Initialize retains history; Deinitialize releases it.
> - This is a **metadata-only skeleton**, with no backend execution, global OpSeq
>   assignment or public gameplay mutation entry point. Later edit integration must
>   address revision exhaustion before mutating terrain; metadata atomicity does
>   not resolve DEF-7. Persistence/compaction remain step 4 under AR-4/DEF-9.
> - Revision.Monotonic stays worldless under §6.1: 256 overlapping batches against
>   a per-key oracle, duplicates, empty input, extreme/negative and unaffected keys,
>   overflow atomicity, service ownership/lifecycle and worldless/thread rejection.
>   Development-only friends seed fixtures without shipping setters. Live net-mode
>   authority behavior remains later multiplayer verification.
>
> **Evidence:** UE 5.7 build succeeded; five TerrainCore tests passed, zero failures,
> automation/process exit 0 at 2026-09-06 17:25:08 UTC. T-112 complete. Build.cs and
> existing value/backend interfaces unchanged; no save or wire format change.

> **T-113 determinations, CP-012, 2026-09-07 — Director-authorised in session.**
> Asked to begin T-113, the Director said "I trust you on all accounts to execute anything
> as needed for the implementation. Begin everything necessary." That is the same shape of
> in-session authority as D-027, and it is what the determinations below were made under.
> They are recorded first in the headers of the files they govern; this block reconciles the
> implementation spec at checkpoint. **None closes a defect and none changes a numbered
> decision.**
>
> 9. **AR-5 §4.3 — `FTerrainBackendInit` gains `UWorld* World` and
>    `FTransform OriginTransform`.** §4.3 lists `Initialize`'s inputs as "seed, gen version,
>    voxel size, bounds, density field, role" and stops there. That is complete for a backend
>    owning only memory. It is **not** complete for `FVPLegacyBackend`, which must find or
>    spawn an `AVoxelWorld`, attach invoker components to it and destroy them at teardown —
>    every one of which needs a `UWorld`. Without the field the adapter would have to reach
>    for `GWorld` and guess, which is strictly worse than saying so in the contract.
>    `OriginTransform` is carried for a separate reason: §8.1 assigns coordinate policy to the
>    **game**, and §4.3 requires the server to quantise exactly once, so the service must state
>    where the grid starts and the backend must conform its actor to it. A backend that read
>    the origin off its own actor would silently move every existing edit by however far that
>    actor had been dragged. **Both are engine types, not plugin types**, so this widens what
>    the game tells a backend without widening what a backend may tell the game. Null `World`
>    is legal and means headless. `FMemoryTerrainBackend` ignores both fields, still compiles
>    and runs with no engine world (§6.1), and `Backend.Conformance` leaves both defaulted and
>    still passes — which is the evidence that AR-5 costs the headless path nothing.
>
> 10. **Step-2 scope, stated as limits rather than left to be discovered.** `FVPLegacyBackend`
>     implements Remove and Add spheres and refuses everything else: Flatten and Smooth have
>     no ruled plane/strength/iteration/falloff semantics (DEF-5), Paint has no game-id to
>     plugin-index table until K9 at step 6, and box ops have no producer. An unsupported
>     operation is **refused, never approximated**. `FTerrainEditResult::Removed` is left
>     **empty**: §2.3 records that `FModifiedVoxelValue` carries no material, so yield needs a
>     separate bulk material read *and* the K9 table, both step 6 — and a plausible number
>     derived from the current RGB config would be inventing the economy that §4.2 moved above
>     the backend precisely to prevent. `ReadRegion`/`WriteRegion`/`HashRegion` move **density
>     only**, materials zero, in the §4.7 dense layout: a working convergence oracle, **not**
>     the snapshot format, which is step 4 under K3 and DEF-9. `FlushPendingWork` is a
>     deliberate no-op, per §4.5's "rendering and collision updates remain the plugin's own
>     async work and are explicitly not serialised by us".
>
>     **Consequence, stated plainly: `FVPLegacyBackend` does not yet pass the full §6.1
>     `Backend.Conformance` suite, and T-113 makes no claim that it does.** §10 makes that
>     pass the operational meaning of "replaceable", so replaceability is proven for the
>     memory backend and *not* for the production adapter. Tracked as **R-013**.
>
> 11. **`UTerrainService::RequestEdit` implements part of the §4.4 sequence, and only part.**
>     Present: admission on authority and well-formedness, the single quantisation, the §7.1
>     work bound, world-bounds rejection, execution, OpSeq assignment **at commit**, and the
>     per-chunk revision bump. Absent, because §9 binds them to later steps behind open
>     defects: `ServerRequestEdit`/`ClientApplyOp` and the subscription set, commit-time
>     revalidation, request dedup and split operations (step 3; DEF-4, DEF-5, DEF-7); the
>     journal and any durability at all (step 4; DEF-1, DEF-2, K5); yield settlement (step 6;
>     DEF-6); reach, permission, rate limit and self-clearance (step 3; DEF-7, DEF-8).
>
>     One ordering point is worth recording because it is narrower than it looks: **revision
>     exhaustion is checked over the op's predicted footprint *before* the backend is called**,
>     so "mutated terrain that no revision records" cannot arise from overflow. That is
>     strictly narrower than DEF-7's no-change-or-committed-result invariant, which stays open:
>     a backend failing halfway still reports one boolean, and nothing here can tell that from
>     a clean refusal.
>
> 12. **Backend selection is a config string, and TerrainCore never links a backend.**
>     `FTerrainBackendRegistry` maps a module name to a creator; the adapter registers itself
>     from its own `StartupModule`; `UTerrainService` loads the module named by
>     `UTerrainSettings::BackendModule` and looks the name up. This is what makes §10 step 4
>     ("set `BackendModule=TerrainBackendX` in config") true rather than aspirational, and it
>     keeps the §4.1 dependency direction one-way. `TerrainCore.Backend.Registry` asserts the
>     mechanism against `FMemoryTerrainBackend`, deliberately: a test that loaded the adapter
>     to prove TerrainCore does not need the adapter would prove nothing.
>
> **Evidence:** both targets `Result: Succeeded`; seven TerrainCore tests green, zero
> failures, exit 0; the `#include` boundary probe fails to compile in **both** `TerrainCore`
> and `VoxelWorld`, files restored byte-identical; standalone boots with the backend ready and
> zero `LogVoxel: Error`; `Terrain.SelfTest` PASSes 13 checks, digging 438 voxels across 8
> chunks and advancing the chunk revision. No save or wire format changed. `TerrainOp.h`,
> `TerrainQuantise.h`, `TerrainRevisionIndex.h`, `MemoryTerrainBackend.*` and both
> `Build.cs` dependency lists are unchanged against CP-011.

---

## 1. Requirement

The server owns a volumetric world. Players and, later, machines request edits to it. The
server decides whether each edit is legal, applies it to its own authoritative copy, derives
the material actually removed and pays it out, orders the edit globally, writes it durably,
and distributes it to the players near enough to care. A player joining a world that has been
dug for six months receives, for the region around them, the current shape of the world — not
the history that produced it. The renderer that draws all this is replaceable and never owns
any of it.

### 1.1 Acceptance criteria

Each is a T-101B pass criterion under D-013.

| # | Criterion | Risk |
|---|---|---|
| A1 | Two or three clients editing the same region converge to identical data, deterministically ordered | R-001 |
| A2 | A client joining mid-session reconstructs the modified world near it without replaying server history | R-002 |
| A3 | Server restart reproduces the world exactly; save size and time are measured, not assumed | R-003 |
| A4 | Yield is computed from material actually removed, on the server, deterministically | R-004 |
| A5 | No gameplay code includes a plugin header; the service runs against a non-plugin backend in a headless test | D-011 |
| A6 | A player standing on terrain that another player edits is not broken by it | R-010 |

**A criterion is not met until its bound defects in §14 are closed.**

---

## 2. Constraints

### 2.1 Ruled (not negotiable in this document)

- **D-002 / AGENTS §4** — dedicated-server model, server authority, never trust the client.
- **D-003** — 16–32 players, stock UE replication, aggressive distance relevancy. No custom netcode.
- **D-006** — C++ owns simulation, networking, data. Blueprint is a thin child.
- **D-010** — the backend is *provisional*; the requirement is durable.
- **D-011** — gameplay owns edit operations, material semantics, permissions, revisions, yield.
  The plugin is never the economic authority; plugin types stay inside the adapter module.
- **D-012** — deterministic base + per-modified-chunk snapshot at revision R + append-only op
  journal + compaction; SQLite for entities; versioned, inspectable formats.
- **D-013** — concurrency, restart and JIP are *gate* criteria, not Phase 3 work.
- **D-015** — voxels are invisible to the player; game-owned names do not say "Voxel".
- **D-020 / D-021** — `Lvl_ThirdPerson` is the map of record; solo work runs standalone.
- **AGENTS §10** — stop at ambiguity rather than invent. §14 and §15 are that stop.
- **R-009** — every increment fits one evening.
- **R-011** — no unstarted increments; block with a named ambiguity or proceed.

### 2.2 Situational

- **There is no C++ module in this project yet.** VS 2022 is installed and never exercised.
  "The project builds from source" is its own task with its own failure mode (build step 0),
  not a precondition that already holds.
- **The T-101A dig Blueprint is a client-authoritative direct plugin call** and is the sole
  cause of both failed drift checks. Build step 2 deletes it. That is the earliest point at
  which the drift checks can pass, so it is scheduled second, not last.

### 2.3 Backend facts

Load-bearing. Source column: §9 = `T-101A_FINDINGS.md`, §10 = the plugin API extract.
Where the findings and the source-level extract disagree, **the extract wins.**

| Fact | Source | Consequence |
|---|---|---|
| `DATA_CHUNK_SIZE = 16`, `RENDER_CHUNK_SIZE = 32` | §10.2 | Authority chunks are aligned to 32 voxels as an optimisation, so a chunk maps onto whole data leaves and whole render chunks. **This is an alignment choice, not an API requirement** |
| `FVoxelValue` is `int16` normalised density (`EIGHT_BITS_VOXEL_VALUE = 0`) | §10.2, 10.3 | The portable save encoding is straight `int16` for the current config |
| The value config flag is recorded in the plugin's save format | §10.2 | Our snapshot header records it too, or an 8-bit rebuild silently corrupts old saves |
| `FModifiedVoxelValue = {Position, OldValue, NewValue}` — **carries no material** | §10.3 | Yield needs a *separate* material read over the edited bounds. Largest single cost in the edit path |
| `GetVoxelsValueAndMaterial`, `CacheMaterials`; `FVoxelDataAccelerator` with `bIsGeneratorValue` | §10.9, 10.7 | Bulk region read exists. `bIsGeneratorValue` reports **provenance** (generated vs stored), not value equality — see DEF-9 |
| `FVoxelData::Set<T>` / `ParallelSet<T>` over `FVoxelIntBox`, and `Lock(EVoxelLockType, Bounds, Name)` | §10.7 | Bulk region write and bounds-scoped locking exist in C++, not Blueprint. Snapshot restore does not need the plugin's save format |
| Every save entry point takes a world and **no bounds**; the chunk-addressed save layout is private with `friend` accessors | §10.10, 10.12 | **The plugin's save format cannot be our per-chunk store.** We own the snapshot codec. The plugin blob survives only as a correctness oracle (§8.2) |
| `FVoxelWorldCreateInfo::bOverrideData` and `bOverrideSave` are **mutually exclusive** | §10.4 | Restore-at-boot uses `bOverrideData` (build data, then create the world on it). `bOverrideSave` is the whole-world blob D-012 rejects and is not used |
| `CheckIfSameAsGenerator`, `RoundToGenerator` | §10.10 | Compaction can delete chunks players reverted to natural shape. The save can shrink |
| Voxel Graphs are Pro-gated and fail **silently** | §9 2b, §10.14 | The generator is C++ (`T-108`) and therefore ours — which §4.6 exploits |
| The plugin's TCP multiplayer is **not an implementation**; both entry points return `false`, and `bEnableMultiplayer` logs a Pro message | §10.12, 10.14 | No reference code exists. **Do not set `bEnableMultiplayer`.** All replication is ours |
| No plugin function is annotated for network authority | §10.17 | Authority is entirely a property of *our* call sites. No safety net |
| Plugin refuses camera-as-invoker outside standalone; `VoxelProceduralMeshComponent` is `NOT Supported` by `FNetGUIDCache` | §9 2d, R-010 | Streaming interest is an entry cost on client *and* server; the movement base needs a deliberate answer (§7.4) |
| Editing near your own feet drops the player through the floor | §9 2f | Belongs in the **validation** layer, not in tuning — but see DEF-8, the current rule is insufficient |
| Thread-safety guarantees are undocumented | §10.17 | The concurrency model must be safe *without* relying on undocumented guarantees. See DEF-4 |
| The async tools take `UObject* WorldContextObject` + `FLatentActionInfo` | §10.9 | **Latent-action lifetime is the issuing UObject's lifetime.** The authoritative edit path is never player-scoped (§4.4) |
| `VOXEL_MATERIAL_ENABLE_*` and value-config macros are source-level edits to a **gitignored** plugin | STATE | The config a save was written under is not recoverable from the repo. It must be in our own headers (§4.7) |

---

## 3. Design decisions

The forks from P-001 §12. **All ten were ruled at CP-005 by D-024**, under the technical/GAME
decision split of D-023. The "Blocks step" column is retained as the record of which build
step each fork gated. Fork ids are stable and are never reused.

| Fork | Question | Position | Status | Blocks step |
|---|---|---|---|---|
| **K1** | Revision model: global sequence, per-chunk revision, or both | **Both** — global `OpSeq` and per-chunk `Rev` | **Ruled (D-024)** | 3 |
| **K2** | Authority chunk size: 16³ / 32³ / 64³ voxels | **32³**, revisited only if E-3 fails | **Ruled (D-024)** | 4 |
| **K3** | Snapshot content: dense, sparse-diff-vs-generator, ops-only | **Sparse diff, dense fallback**, chosen per chunk by byte size | **Ruled (D-024)** | 4 |
| **K4** | Edit execution: game thread / one terrain thread / parallel workers | **Game thread**, bounded by `MaxVoxelsPerOp`. A dedicated terrain thread only if §7.1 measurement demands it and E-5 supports it. Plugin thread-safety is undocumented; do not assume it | **Ruled (D-024)** | 3 |
| **K5** | Durability point | **Journal record durable before inventory is credited.** Slower and correct. Revisit only with measurement | **Ruled (D-024)** | 4 |
| **K6** | Client terrain delivery: stream data vs generator+ops | **Generator + ops**; snapshots only on subscribe (already required by AGENTS §4) | **Ruled (D-024)** | — |
| **K7** | Service shape: `UWorldSubsystem` vs actor | **`UWorldSubsystem`.** The service is authority, not a thing in the world | **Ruled (D-024)** | 0 |
| **K8** | Module naming: `TerrainCore` / `TerrainBackendVPLegacy` | **`TerrainCore`, `TerrainBackendVPLegacy`** — no "Voxel" in game-owned names, per D-015 | **Ruled (D-024)** | 0 |
| **K9** | `MaterialConfig`: `RGB` (current) vs `SingleIndex` | **`SingleIndex`**, game-owned id↔index table in the adapter, 255 terrain materials max. Nothing visible changes while terrain is on the placeholder grid material; if it later affects how terrain looks, that becomes a GAME decision at that time | **Ruled (D-024)** | 6 |
| **K10** | Field ownership: adapter-held / game-held / journal-as-truth / split | **Adapter-held density, journal as the durable record.** Split ownership is reconsidered at T-108, when the C++ density field exists and the comparison is concrete | **Ruled (D-024)** | 4 |

**K10 note.** Astra's proposal framed this as a two-way fork. Two further options exist and are
recorded here so the ruling is made against the real set: *journal-as-truth* (the append-only op
log is canonical and the plugin field is a rebuildable cache) and *split ownership* (the game
owns material analytically through the T-108 generator while density stays in the adapter).
Split ownership is close to free because T-108 already forces us to write the strata generator.
K10 was ruled at CP-005 (D-024) as adapter-held density with the journal as the durable record;
split ownership is reconsidered at T-108, when the C++ density field exists.

---

## 4. The design

### 4.1.0 Why the boundary exists

Carried from `Docs/archive/ARCHITECTURE_v0.md` §1. This subsection is not background. It is the
answer when an implementer proposes to shortcut the boundary — to include one plugin header in
gameplay "just for this", to read a plugin material directly for yield, to put a plugin type in
a save record or an RPC. The rest of §4 says what the boundary *is*; this says what it costs to
lose, and that cost is not paid at the moment of the shortcut.

**The plugin does not own the game's truth.** The game owns terrain semantics; the backend
implements and renders them. If gameplay called the plugin directly:

- **The plugin API would embed itself across the whole game.** Not in one adapter file — in
  every call site that ever touched terrain. The boundary is cheap to hold and expensive to
  reinstate, because reinstating it means finding all of them.
- **Resource logic would depend on the renderer.** Yield would be computed from whatever the
  meshing layer happened to expose, which makes the economy a property of the draw path. That is
  the failure DEF-6 guards against inside the adapter, at a larger scale.
- **Wire and save formats would become plugin-specific.** Network messages and save files
  written in plugin types outlive the plugin. A backend change would then be a save migration
  and a protocol break, not a config line (§10).
- **Replacing or upgrading the backend would become a rewrite.** D-010 says the backend is
  provisional and the requirement is durable. A rewrite-to-swap makes D-010 false in practice
  while leaving it true on paper, which is worse than never having ruled it.
- **Terrain could not be tested without a renderer.** No headless test, so no test on every
  build, so the invariants in §6.1 are unverifiable and A5 is unreachable. This is the one that
  bites first and is noticed last.

The v0 layering that produced this argument — gameplay, tools and machines above an
authoritative terrain service, above persistent world state, above the backend adapter, above
rendering, collision and surface queries — is realised by §4.1's three modules, §4.4's service,
§4.7's stores and §4.3's `QueryPoint`. Plugin-specific types never cross out of the adapter
module and gameplay code never includes plugin headers (D-011, `AGENTS.md` §4).

### 4.1 Modules

Three modules. The boundary is a **build-system** boundary: `TerrainCore` does not list the
plugin in its `Build.cs`, so a plugin include in gameplay code is a compile error rather than a
code-review finding. That is the enforcement mechanism for D-011 and the AGENTS §9 drift guard.

```text
VoxelWorld              (primary game module — gameplay, characters, tools, UI hooks)
   |  depends on
   v
TerrainCore             (game-owned. No plugin dependency. Compiles headless.)
   - UTerrainService            authority, validation, sequencing, revisions, yield
   - ITerrainBackend            pure interface, game types only
   - ITerrainDensityField       the world's shape, as pure C++
   - FTerrainJournal, FTerrainSnapshotStore, FTerrainRevisionIndex
   - UTerrainStreamComponent    per-connection relevancy + transport
   - UTerrainStreamingComponent per-actor streaming interest  (see §7.4 / DEF-10)
   - FMemoryTerrainBackend      dense test backend — no renderer, no plugin
   ^  implemented by
   |
TerrainBackendVPLegacy  (the ONLY module that includes plugin headers)
   - FVPLegacyBackend : ITerrainBackend
   - UVPLegacyDensityGenerator : UVoxelGenerator   forwards to ITerrainDensityField
```

`VoxelWorld` depends on `TerrainCore` **only**. `TerrainBackendVPLegacy` is loaded and
registered at startup by name from config:

```ini
[/Script/TerrainCore.TerrainSettings]
BackendModule=TerrainBackendVPLegacy
```

Swapping backends is a config line plus a new module. §10 is the concrete procedure.

> Module names avoid "Voxel" per D-015. The codename is not the identity, and these are the
> class names the game carries for years.

### 4.2 Data structures

All in `TerrainCore`. No plugin type appears in any of them.

```cpp
// ---- identity ----------------------------------------------------------
using FTerrainOpSeq = uint64;   // global, monotonic, server-assigned
using FTerrainRev   = uint32;   // per chunk, monotonic
using FTerrainMatId = uint16;   // GAME material id. Never a plugin index.

struct FTerrainChunkKey { int32 X, Y, Z; };   // chunk space, 32 voxels per unit

// ---- the operation (wire format AND journal record body) ----------------
enum class ETerrainOpKind : uint8 { Remove, Add, Flatten, Smooth, Paint };
enum class ETerrainShape  : uint8 { Sphere, Box };
enum class ETerrainSource : uint8 { Player, Machine, Admin, Worldgen };

struct FTerrainOp
{
    FTerrainOpSeq   OpSeq         = 0;
    uint64          TransactionId = 0;  // equal across sub-ops of one split edit
    ETerrainOpKind  Kind          = ETerrainOpKind::Remove;
    ETerrainShape   Shape         = ETerrainShape::Sphere;
    ETerrainSource  Source        = ETerrainSource::Player;
    uint32          SourceId      = 0;  // PlayerId or MachineId
    uint32          ToolId        = 0;  // tool/tier — affects yield, not geometry
    FIntVector      CentreVox     = {}; // VOXEL SPACE. Integer. See §4.3.
    int32           RadiusVoxQ16  = 0;  // voxels, 16.16 fixed point
    FIntVector      ExtentVox     = {}; // box ops only
    FTerrainMatId   MaterialId    = 0;  // Add / Paint only
    uint8           Flags         = 0;
};
```

**Serialisation is explicit and little-endian**, field by field in declaration order, with no
struct padding on the wire and no reliance on `sizeof`. The encoded body is **58 bytes**. Any
claim about in-memory size is irrelevant to the format. `Op.Codec.RoundTrip` (§6.1) is the
authority on the encoding.

```cpp
// ---- what the backend gives back ---------------------------------------
struct FTerrainMaterialVolume
{
    FTerrainMatId MaterialId;
    int64         MicroLitres;   // signed: + removed, - placed.
                                 // int64 µL spans ±9.2e12 m³. See DEF-9.
};

struct FTerrainEditResult
{
    FTerrainBox                    EditedBounds;   // voxel space, game type
    TArray<FTerrainChunkKey>       AffectedChunks;
    TArray<FTerrainMaterialVolume> Removed;        // physical material only
    int64                          VoxelsTouched = 0;
    int64                          VoxelsScanned = 0;  // read footprint, not write
    bool                           bTruncated    = false;
};

// ---- region transfer (snapshots and JIP) --------------------------------
enum class ETerrainRegionEncoding : uint8 { Dense, SparseDiff, Empty };

struct FTerrainRegionData
{
    FTerrainChunkKey       Key;
    FTerrainRev            Rev = 0;
    FTerrainOpSeq          LastOpSeq = 0;
    ETerrainRegionEncoding Encoding = ETerrainRegionEncoding::Empty;
    uint32                 GeneratorVersion = 0;
    uint8                  ValueConfig = 0;   // mirrors the plugin's value config flag
    TArray<uint8>          Payload;           // see §4.7
};

// ---- streaming interest (see §7.4 / DEF-10) -----------------------------
struct FTerrainStreamingInterest
{
    uint32  InterestId = 0;   // game-owned handle
    FVector WorldLocation = FVector::ZeroVector;
    double  RadiusCm = 0.0;
    bool    bCollision = true;
    bool    bRender = false;  // false on a dedicated server
};
```

**`FTerrainEditResult::Removed` carries physical material only.** Tool efficiency, recovery
factor and every other economic conversion happen in `UTerrainService`, above the backend.
This is a correction to P-001 §4.9, which placed efficiency in the adapter and thereby made
backend replacement able to change the economy by accident (DEF-6).

**Deliberate omission.** `FTerrainEditResult` does not carry a per-voxel delta array. The
plugin's modified-value array (20 B/voxel; ~10 MB for the measured 514k-voxel sculpt sphere)
stays inside the adapter, is consumed there to produce `Removed`, and is freed. Only the
aggregate crosses the boundary. That is the difference between a boundary that is cheap and
one that exists on paper.

### 4.3 The backend interface

```cpp
class ITerrainBackend
{
public:
    virtual ~ITerrainBackend() = default;

    virtual bool Initialize(const FTerrainBackendInit& Init) = 0;  // seed, gen version,
                                                                   // voxel size, bounds,
                                                                   // density field, role
    virtual void Shutdown() = 0;

    // Authoritative mutation. Server: full result. Client: result ignored.
    virtual bool ApplyOp(const FTerrainOp& Op, FTerrainEditResult& Out) = 0;

    // Region transfer — snapshot capture and restore.
    virtual bool ReadRegion (const FTerrainChunkKey& Key, FTerrainRegionData& Out) = 0;
    virtual bool WriteRegion(const FTerrainRegionData& In) = 0;

    // Convergence oracle. Position-sensitive hash of a region's values + materials,
    // independent of the iteration order used to compute it. See DEF-5.
    virtual uint64 HashRegion(const FTerrainChunkKey& Key) const = 0;

    virtual bool IsRegionResident(const FTerrainChunkKey& Key) const = 0;
    virtual void FlushPendingWork() = 0;

    // Point query — material identity and solidity at one voxel. Not for aiming.
    virtual bool QueryPoint(const FIntVector& VoxelPos,
                            FTerrainPointSample& Out) const = 0;

    // Streaming interest — the backend-neutral replacement for the plugin invoker.
    virtual void SetStreamingInterest  (const FTerrainStreamingInterest& In) = 0;
    virtual void ClearStreamingInterest(uint32 InterestId) = 0;
};
```

`FTerrainPointSample` is a `TerrainCore` type, alongside the §4.2 structures:

```cpp
struct FTerrainPointSample
{
    float         Density    = 0.f;  // < 0 solid, normalised
    FTerrainMatId MaterialId = 0;
    bool          bResident  = false;
};
```

**Aiming does not use this interface.** World-space aiming is a standard UE collision trace
against the rendered mesh. It returns an `FHitResult`, involves no plugin type, and needs no
backend method — so the T-101A trace path is legal gameplay code and stays legal after the
build-step-2 rewire. What changes at step 2 is what happens *after* the trace: the hit location
becomes an edit request to the service instead of a direct `RemoveSphere` call.

**`QueryPoint` exists for material identity and solidity questions** — ore assay before
committing a dig, placement validity over a structure footprint, machine surface snapping. Those
ask *what is at this voxel*, which a collision trace against a mesh cannot answer. `bResident`
false means the backend holds no data there and `Density` and `MaterialId` are meaningless; a
caller that ignores `bResident` reads a solid world as empty at streaming boundaries. The query
is read-only, takes integer voxel coordinates for the quantisation reason below, and carries no
authority of its own — a server-side rule that acts on the result still runs through §4.4.

**Eleven methods.** Everything the game does to terrain goes through them, which is what makes
`FMemoryTerrainBackend` (a dense `TMap<FTerrainChunkKey, TArray<int16>>`) a complete stand-in
for the plugin in tests — and therefore what makes A5 achievable on day one.

**Why the wire op is integer voxel space.** The plugin's tools take an `FVector` world position
and convert with `bConvertToVoxelSpace = true`. If the server broadcast a world position, server
and client would each perform that float conversion independently, against possibly differing
actor transforms, and round differently at voxel boundaries. The divergence would be one voxel
wide, invisible for a week, and then a client would see a wall where the server sees air. So:
**the server quantises once, at validation time, and the quantised integers are the op** — for
the wire, for the journal, and for its own application. The adapter calls the plugin with
`bConvertToVoxelSpace = false`.

This removes one conversion hazard. It does **not** by itself establish deterministic replay
across builds, platforms or backends — see DEF-5.

Two further adapter rules:

- `bRecordModifiedValues` is **always true** on the authoritative path; yield depends on it.
- The C++ tool overload **appends** to the out-array. The adapter's scratch array is reset per
  op, explicitly. A missed reset is a yield-inflation bug that grows over a session.

### 4.4 The service, and the server sequence

`UTerrainService` (K7 — ruling required) exists on both server and client; `HasAuthority` gates
the authoritative half.

**The authoritative edit path is never player-scoped.** No `PlayerController`, character, or
tool component owns an in-flight edit. The plugin's async tools anchor completion to the issuing
`UObject`'s lifetime; a player disconnecting mid-dig would tear down the callback that advances
the revision, journals the op and settles inventory, after the mutation had already landed. The
service is the sole issuer and the sole owner of every in-flight operation.

```text
CLIENT                         SERVER (game thread)          TERRAIN EXECUTION
------                         --------------------          -----------------
input, camera trace
  |
  |  ServerRequestEdit(Req)     validate  ────────────────┐
  |  [reliable, on the           - request identity/dedup  │  reject → ClientEditRejected
  |   PlayerController's         - reach (camera -> target)│  (ReqId + reason enum)
  |   TerrainStreamComponent]    - radius <= tool max      │
  |                              - rate limit per source   │
  |                              - tool / power / durability│
  |                              - zone permission (D-004) │
  |                              - tool owned and equipped │
  |                              - tool off cooldown       │
  |                              - fuel / charge if needed │
  |                              - self-clearance (DEF-8)  │
  |                              - target chunks resident  │
  |                              - quantise to voxel space │
  |                                     |                  │
  |                              enqueue FTerrainOp ───────┴───►  ordered queue
  |                                                                 |
  |                                                                 | bulk-read materials
  |                                                                 |   over read bounds
  |                                                                 | backend->ApplyOp()
  |                                                                 | accumulate per-material
  |                                                                 |◄─ FTerrainEditResult
  |                                     ┌───────────────────────────┘
  |                              assign OpSeq (monotonic)
  |                              bump Rev for each affected chunk
  |                              append journal record
  |                              settle yield                 ← ordering is DEF-1
  |                              for each subscriber of any affected chunk:
  |                                  ClientApplyOp(Op, PerChunkRev[])
  |◄────────────────────────────────────┘
apply Op locally via the same
adapter, same integers
update per-chunk Rev; a
non-contiguous Rev → request
resync for that chunk
```

**The relative ordering of journal append, yield settlement and client acknowledgement is not
settled by this document.** It is DEF-1, bound to build step 4, and K5 must be ruled before that
step starts. The diagram shows the steps, not their durability boundaries.

Notes:

- **Tool state is validated, not assumed.** The requester owns and has equipped the tool named
  by `ToolId`; the tool is off cooldown; and the tool has fuel or charge if its type requires it.
  `ToolId` is client-supplied input and it affects yield (§4.9), so accepting it unchecked makes
  the client the economic authority, which D-011 forbids. **These are admission checks and are
  revalidated at commit under DEF-7**, for the same reason as the resource check there: a
  requester can unequip, exhaust a charge, or disconnect while the op sits in the queue. The
  fuel-and-charge rule is stated here as a validation input; the machine power model that
  supplies charge is not specified by this document.
- **OpSeq is assigned at commit, not at request.** A rejected request never consumes a sequence
  number, so the journal has no holes and "replay everything after N" is unambiguous. The
  no-change-or-committed-result invariant this depends on is DEF-7.
- **Multi-chunk ops are one op.** Sequenced once, applied once, broadcast once, with the full
  affected-chunk list. There is no cross-chunk transaction protocol because there is no
  cross-chunk parallelism. This satisfies "never observe a half-applied operation" **on the
  server**; it does not by itself satisfy it during JIP, which is DEF-3.
- **Very large ops are split into sub-ops sharing a `TransactionId`,** each under
  `MaxVoxelsPerOp`. Stage 4 excavation (D-016) is why this exists now. Whether a split is
  geometrically equivalent to the unsplit op is operation-dependent and is part of DEF-7.
- **Prediction is deferred.** The client shows the change on `ClientApplyOp`. If latency proves
  intolerable (E-7), client-side prediction with resync-based reconciliation is additive: the
  client already has an apply path and a per-chunk resync path.

### 4.5 Concurrency

- One serialised execution path, FIFO, ordering identical to `OpSeq` order.
- **That path is the game thread** — K4, ruled at CP-005 by D-024, bounded by `MaxVoxelsPerOp`.
  A dedicated terrain thread only if the §7.1 measurement demands it and E-5 supports it. The
  plugin documents no thread-safety guarantees and no calling-thread contract, so **DEF-4 still
  requires a thread-affinity and ownership table before the edit path is implemented** — the
  ruling picks the thread, it does not discharge the defect.
- The backend's `ApplyOp` may internally use the plugin's parallel edit mode, **but this is an
  assumption and E-2 tests it.** If E-2 shows any nondeterminism, the authoritative path drops
  to single-threaded — and so does the client path, because client results supply collision and
  are not merely cosmetic (DEF-5).
- **Rendering and collision updates remain the plugin's own async work** and are explicitly not
  serialised by us. This is the seam where the fall-through-the-floor bug lives: the data edit
  completes before the collision cook does. The answer is validation plus §7.4, not an attempt
  to make the plugin atomic — and the current validation rule is insufficient (DEF-8).

### 4.5.1 Thread affinity, ownership, shutdown and cancellation — DEF-4 resolution

**Architect determination, T-114, 2026-09-07** (technical, per D-023). K4/D-024 ruled *which*
thread; §4.5 records that the ruling "picks the thread, it does not discharge the defect." This
subsection is the table, the state machine and the cancellation rule DEF-4 asked for.

#### The affinity and ownership table

"Owner" is who may create and destroy the thing. "Caller" is who may invoke it. A backend does
not trust either — it checks and refuses, because a backend that trusted its callers would turn
a governance rule into a crash.

| Activity | Thread | Owner | Legal caller |
|---|---|---|---|
| `ITerrainBackend::Initialize` / `Shutdown` | Game thread only | `UTerrainService` | The service, once each |
| `ApplyOp` | Game thread only | The service's queue | The service, from the serialised path |
| `ReadRegion` / `WriteRegion` / `HashRegion` | Game thread only | — | The service |
| `QueryPoint` / `IsRegionResident` | Game thread only | — | The service and gameplay through it |
| `SetStreamingInterest` / `ClearStreamingInterest` | Game thread only | The service (ids) / the backend (whatever it creates for one) | The service |
| `FlushPendingWork` | Game thread only | — | The service. **A no-op by design** (§4.5) |
| `ITerrainDensityField::Sample` / `SampleRange` | **Any thread, concurrently** | `UTerrainService` | The backend, and through it the plugin's mesher workers |
| Meshing, LOD selection, collision cooking, render invalidation | Plugin worker threads | Plugin | **Nobody on our side.** We never call in and never block on it |
| The voxel world actor and its invoker components | Game thread | The backend that created them | The backend only |

**Rule 1 — the whole `ITerrainBackend` surface is game-thread only.** All eleven methods. There
is no read/write split and no "reads are safe from anywhere" concession, because the plugin
documents no thread-safety guarantee for any of them and a read that races a mesher's internal
write is the same defect as a write that does.

**Rule 2 — `ITerrainDensityField` is the single any-thread exception, and it is const, immutable
and free of memoisation.** It is safe by construction rather than by lock, which is what makes it
callable from the plugin's mesher threads at all. This is stated on the interface in code.

**Rule 3 — no lock of ours is ever held across a call into the plugin.** DEF-4 named the hazard
exactly: "an external bounds lock may conflict with one the wrapper takes internally." The answer
is not a better lock ordering, it is holding no lock: the serialised path is one thread, so
mutual exclusion is already established by the thread itself and a second mechanism would only be
able to deadlock against the plugin's own.

**Rule 4 — no `UObject` outside the service owns an in-flight operation.** §4.4 already states
this for player scoping; the affinity table extends it to machines and to admin tools. The
service is the sole issuer and the sole owner.

**Rule 5 — the density field outlives the backend.** `FTerrainBackendInit` *borrows* it (AR-2),
so the service releases it strictly after `Shutdown` returns. Reversing that order is a
use-after-free visible only when a mesher thread is still in flight, which is the hardest form of
this bug to reproduce and therefore the one worth naming.

#### The shutdown state machine

Four states, in `UTerrainService`. Every transition is on the game thread.

| State | Entered when | `RequestEdit` | Backend calls |
|---|---|---|---|
| **Uninitialised** | Subsystem constructed; also re-entered after teardown | Rejected `NotReady` | None permitted |
| **Ready** | `Initialize` returned true at `OnWorldBeginPlay` | Normal | Normal |
| **Draining** | First of: `EndPlay`, world teardown, `Deinitialize`, level travel, PIE exit | Rejected `ShuttingDown` | Only teardown calls |
| **TornDown** | Teardown sequence complete | Rejected `NotReady` | None permitted |

`ShuttingDown` is a new `ETerrainEditRejection` value. It is distinct from `NotReady` on purpose:
`NotReady` means "not yet", `ShuttingDown` means "never again in this world", and a client that
cannot tell them apart will retry forever into a world that is going away.

**The teardown sequence is fixed and is the exact reverse of construction:**

1. Enter `Draining`. Every later `RequestEdit` is refused from here on.
2. **Discard the pending queue.** Not drain — discard. A queued op has no `OpSeq`, no journal
   record and no broadcast, so discarding it is precisely the *no-change* half of the DEF-7
   invariant (§4.11) and leaves nothing half-done. Draining instead would run arbitrary work
   during teardown and could journal an op no client will ever hear about.
3. Release every reservation held by a discarded request (§4.11).
4. `ClearStreamingInterest` for every live interest, through the backend, so the backend destroys
   what it created for each one.
5. `Shutdown()` on the backend. The backend destroys only actors and components **it** spawned.
6. Release the backend.
7. Release the density field. **Strictly last** (Rule 5).
8. Enter `TornDown`.

#### Cancellation

**There is nothing to cancel mid-operation, and that is a design property rather than an
observation.** `ApplyOp` is synchronous on the game thread and cannot be pre-empted by `EndPlay`,
travel or PIE exit, all of which are themselves game-thread events. So no operation is ever
partially applied at teardown.

What *is* cancelled is the pending queue, at step 2 above. Cancellation is therefore a queue
operation, not a plugin operation.

**We never register a completion callback that can outlive the service.** The plugin's async
tool overloads anchor completion to the issuing `UObject`; the authoritative path uses the
synchronous overloads only, so there is no callback to cancel and no lifetime to police. The
plugin's own meshing and collision work is anchored to the voxel world actor and dies with it —
we do not cancel it, and we must not wait for it, because waiting on the game thread for a worker
that wants the game thread is the deadlock this rule exists to prevent.

**Travel and PIE exit are not special cases.** They reach us as `Deinitialize` on the world
subsystem, which is why the state machine keys off the first teardown signal rather than trying
to enumerate causes. A world subsystem's lifetime is a world's lifetime, so a new world gets a
new service, a new backend and a new field.

#### What DEF-4 does not cover, and what it hands on

- **The plugin's internal thread safety remains unaudited.** This resolution constrains what *we*
  do; it makes no claim about what the plugin does behind `RemoveSphere`. E-2 (determinism across
  threading modes) and E-5 are the experiments that probe it, and until E-2 reports, edits run
  with `bMultiThreaded = false` on both server and client (§4.5, DEF-5).
- **Collision readiness is DEF-8, not this.** Nothing here says when a cook has finished.
- **Durability ordering is DEF-1, not this.** Nothing here says when a record is safe on disk.

**Evidence.** `TerrainCore.Backend.Conformance` asserts that every method refuses when called off
the game thread and that the state machine rejects with the right reason in each state.
`Adapter.Determinism` (§6.2) covers the threading-mode claim. `MP.*` (§6.3) exercises teardown
under load through repeated travel.

### 4.6 The generator

Voxel Graphs are Pro-gated, so the generator must be C++ (T-108). That is usually a cost; here
it is leverage.

```cpp
// TerrainCore — no plugin dependency
struct FTerrainDensitySample
{
    float         Density = 0.f;  // normalised [-1, 1], < 0 solid
    FTerrainMatId MaterialId = 0;
};

struct FTerrainDensityRange          // AR-6
{
    float Min = -1.f;
    float Max =  1.f;
};

class ITerrainDensityField
{
public:
    virtual ~ITerrainDensityField() = default;
    virtual FTerrainDensitySample Sample(FIntVector Position) const = 0;

    // AR-6. Conservative bounds over a half-open voxel box. The default returns
    // the full [-1, 1], which is always correct and merely forfeits the skip.
    virtual FTerrainDensityRange SampleRange(const FTerrainBox& Box) const;
};
```

**AR-6 (Architect determination, T-108, 2026-09-07).** `Sample` alone is enough to FILL a chunk
and not enough to SKIP one. A backend octree that cannot ask "is this whole region certainly
solid, or certainly empty?" must sample every voxel of every region at every LOD, which across a
512 m world of 50 cm voxels is a measurable cost rather than a theoretical one. The default
implementation keeps every existing implementer valid and adds no required method.

**Threading.** Implementations are safe for unsynchronised concurrent const calls and hold no
mutable state, memoisation included — the plugin queries its generator from mesher worker
threads. This is §4.5.1 Rule 2, and it is the single any-thread exception in the whole design.

`UVPLegacyDensityGenerator : UVoxelGenerator` (adapter) implements the plugin's value and
material queries by forwarding iteration to the field. The world's shape — strata, ore bodies,
the authored hill — is plain C++ that unit-tests without an engine and survives a backend swap
untouched. `FTerrainBackendInit::GeneratorVersion` is the first-class save-schema
input K3 requires, bumped on any change to the field. The CP-008 header determination
supersedes the former three-method declaration; no field implementer exists at step 1.

`FMemoryTerrainBackend` can sample the same field when supplied. Step-1 tests use
explicit Dense fixtures and a null field.

**BUILT AT T-108 (2026-09-07), build step 8.** `FTerrainWorldField` in `TerrainCore` is the
implementer: a 280 m hill east of the origin with 65 m of relief, a west-facing escarpment
exposing the strata, a lowland basin, topsoil/dirt/stone/deep-stone/bedrock by depth below the
local surface, and an iron ore body that never breaks the surface. Roughness is integer-hashed
value noise — no RNG and no float bit tricks, for the 4.10.4(b) reason.
`UVPLegacyDensityGenerator` in the adapter forwards plugin value and material queries to it and
decides nothing. `GeneratorVersion` moved 0 → 1 with it.

**The T-101A hill was sculpted by script, not generated** — it was not reproducible from a seed
and did not survive a map load (finding 2e, R-003), which is why every standalone process showed
a flat plane until T-108. That is now closed: the world is a pure function of position and seed
and needs no save file to come back. D-012's deterministic base names `GeneratorVersion` and the
seed as its generation inputs; there are no authored stamps.

### 4.7 Persistence schema

```text
Saved/World/<WorldId>/
  world.json                     # human-readable, versioned, small
  chunks/<X>_<Y>_<Z>.chunk       # snapshot at revision R  (binary, versioned)
  journal/<NNNNNN>.tjl           # append-only op log, size-segmented
  entities.sqlite                # players, inventories, structures, machines, power grids
                                 #   (D-012; grids per VISION pillar 3)
Tests/Saves/                     # old-save fixtures, loaded by automation on every build
```

**`world.json`** — the one file a human opens first:

```json
{
  "schema": 1,
  "worldId": "…guid…",
  "seed": 1234567,
  "generatorVersion": 3,
  "voxelSizeCm": 50,
  "chunkSizeVoxels": 32,
  "valueConfig": "int16",
  "materialConfig": "SingleIndex",
  "materialCatalogVersion": 1,
  "backendId": "VPLegacy",
  "backendVersion": "v432/e9648b302",
  "latestOpSeq": 918273,
  "journalBaseOpSeq": 900000,
  "createdUtc": "…", "updatedUtc": "…"
}
```

`backendVersion` and `materialCatalogVersion` are here because the plugin's compile-time config
macros are source-level edits to a gitignored tree — the repo cannot tell us what a save was
written under, so the save must tell us itself.

**Chunk snapshot** (`.chunk`) — header then payload, all little-endian:

| Field | Bytes | Notes |
|---|---|---|
| magic `TCHK` | 4 | |
| schemaVersion | 4 | migration entry point |
| chunkKey X, Y, Z | 12 | |
| rev | 4 | |
| lastOpSeq | 8 | ops with `OpSeq <= lastOpSeq` are baked in |
| generatorVersion | 4 | K3 correctness gate |
| valueConfig, materialConfig, encoding, reserved | 4 | |
| payloadBytes, payloadCrc32 | 8 | torn-write detection |
| payload | n | **Dense:** `int16[N]` values then `uint16[N]` materials. **SparseDiff:** `uint32 count`, then `count × {uint32 localIndex, int16 value, uint16 material}`, relative to generator output. **Empty:** zero bytes |

**Snapshot metadata survives payload deletion.** A chunk that reverts to natural shape keeps a
zero-payload `Empty` record carrying its rev and lastOpSeq, rather than being deleted outright.
Deleting the record would discard monotonic revision history and let the chunk return as
revision zero (DEF-9).

**Journal record** (`.tjl`) — fixed prefix, variable lists, all little-endian:

| Field | Bytes |
|---|---|
| magic `TJOP`, schemaVersion | 8 |
| `FTerrainOp` encoded body (§4.2) | 58 |
| serverUtcMillis | 8 |
| yield entry count, then `{matId u16, microLitres i64}` × count | 2 + 10n |
| affected chunk count, then `{key 12 B, newRev 4 B}` × count | 2 + 16n |
| crc32 | 4 |

Typical record, one material, two chunks: **~122 bytes per edit.** A million-edit server-year is
~122 MB before compaction.

**Compaction.** Per chunk, when `opsSinceSnapshot > 256`, or `journalBytesForChunk > 64 KB`, or
the chunk has been idle 5 minutes, or at shutdown: re-read the region, write a new `.chunk` at
the current rev, update `lastOpSeq`. **Journal segment retention is dependency-aware**: a segment
is deletable only when every consumer has advanced past it — terrain snapshots *and* the entity
store's settlement watermark. The naive "min lastOpSeq across chunk files" rule is insufficient
in both directions and is part of DEF-9.

**Migration.** Every format carries `schemaVersion` in its own header. A load of version <
current runs a registered migration chain with a `.bak` written first. `Tests/Saves/` fixtures
load on every build, which turns "we have a migration path" from a claim into a check.

### 4.8 Relevancy and join-in-progress

Each `APlayerController` gets a `UTerrainStreamComponent` (owner-only). The server maintains per
connection:

```cpp
TSet<FTerrainChunkKey>                Subscribed;   // acked and live
TMap<FTerrainChunkKey, FTerrainRev>   ClientRev;    // last rev we know they have
TQueue<FTerrainChunkKey>              PendingSync;  // subscribe backlog
uint32                                SyncGeneration; // stale-fragment discrimination
```

Subscription is recomputed every 500 ms from the pawn position, with hysteresis: subscribe inside
`SubscribeRadius`, unsubscribe outside `SubscribeRadius × 1.5`. A player walking a boundary must
not thrash a snapshot stream.

**Per chunk, on new subscription:**

1. No `.chunk` record and no journal ops for the key → `ChunkIsPristine(Key)`. The client's
   generator already produces the right thing. **This is the common case and it costs 12 bytes.**
2. Else the snapshot at rev R (fragmented, §7.3), then every journal op for that chunk with
   `OpSeq > snapshot.lastOpSeq`, then `ChunkSyncComplete(Key, Rev)`.
3. Ops committed *during* sync are queued per chunk and flushed after step 2.
4. The client compares its resulting `Rev` with the server's and requests full resync on mismatch.

**Resync is the repair primitive** — for gaps, missed ops, suspended clients, and bugs not yet
found. It is deliberately blunt.

**This protocol is incomplete and must not be implemented as written.** A multi-chunk operation
can be replayed into a chunk whose snapshot already contains it, and the sync/live handoff has no
defined atomic transition. That is DEF-3, bound to build step 5.

### 4.9 Yield

Server-only. Physical measurement in the adapter; economic conversion in the service.

**In the adapter (`FVPLegacyBackend::ApplyOp`):**

1. Compute the op's read bounds from its integer parameters — the write sphere plus the margin
   any read-dependent kernel needs. **Read bounds exceed write bounds**; at radius 25 voxels the
   sphere is ~65,450 samples but the read box is ~140,000 positions. Both are reported
   (`VoxelsTouched`, `VoxelsScanned`).
2. Bulk-read values and materials over those bounds **once** into a scratch buffer. Not per-voxel
   material calls — that is a lock acquisition per voxel and would dominate the frame.
3. Apply the op.
4. For each modified value: `Δocc = Occ(OldValue) − Occ(NewValue)`, look the material up in the
   scratch buffer by position, accumulate `Δocc × VoxelVolume` per material id, signed.
5. Return `Removed` as **physical volume only**.

**In the service:**

6. Apply tool efficiency, recovery factor and any placement debit, from game data keyed by
   `ToolId` and `Kind`. Machines later substitute a different efficiency curve and nothing else
   changes.

`Occ()` is a monotone map from normalised signed density to occupancy in [0,1]. Its exact form —
and whether `int16` density is linear enough in the transition band for volume to be accurate — is
**unknown; E-1 is the experiment.** The calibration test: remove a sphere of radius R in
homogeneous material and compare `Σ Δocc × VoxelVolume` against `4/3 π R³`.

**The material scratch buffer holds pre-edit material only.** For `Add` and `Paint`, negative
occupancy attributed to the old material does not describe what was placed, and `Smooth` can
remove at some positions and place at others. **Separate accounting for removal and placement,
and the economic policy for placement, smoothing and material conversion, are not specified here.**
Without them, add-then-mine mints resources. That is DEF-6, bound to build step 6.

**Material encoding.** Game `FTerrainMatId` maps to a per-voxel plugin material. The world is
currently on the plugin's colour material config, which has no clean id channel. K9 recommends
switching to a single-index config with a game-owned id↔index table in the adapter, capping the
game at 255 simultaneous terrain materials. This is a visible change — terrain rendering moves to
a material collection rather than vertex colours — so it needs a Director ruling before step 6.

### 4.10 Operation semantics and determinism — DEF-5 resolution

**Architect determination, T-114, 2026-09-07** (technical, per D-023).

DEF-5 asked for canonical semantics per operation including read bounds and rounding, version
compatibility rules, and golden fixtures; it also required `HashRegion` to be position-sensitive
and forbade the client keeping a nondeterministic mode the server drops.

#### 4.10.1 The operation set is closed. Flatten and Smooth are not in it.

DEF-5's complaint — "Flatten and Smooth are named without plane, strength, iteration or falloff
semantics" — is answered by **removing them from the operation set rather than by inventing
semantics for them.**

`ETerrainOpKind` keeps all five enumerators, because the wire encoding is permanent (§4.2) and
renumbering it is a save-format change. But:

- **`Remove`, `Add` and `Paint` are the operation set.** They are specified below.
- **`Flatten` and `Smooth` are RESERVED.** Every backend refuses them and every service admission
  check refuses them, permanently, until a numbered decision specifies plane, strength, iteration
  count and falloff. An unimplemented op that is refused is a closed question; an unspecified op
  that is approximated is an open one, and DEF-5 exists because the document did the second.
- A build that meets a reserved kind on the wire **rejects the op** rather than treating it as a
  no-op, because a no-op is indistinguishable from success to a replaying client.

#### 4.10.2 Canonical geometry — the write set and the read bounds

All of this is in integer voxel space. The op is already integers by §4.3, and the quantisation
and rounding rule for getting there is fixed in `TerrainQuantise.h`: **transform to terrain-local
in double, then floor.** Not round — floor is what makes a voxel the half-open cell `[n, n+1)`
that `FTerrainBox` assumes. `DequantiseVoxel` returns the voxel **centre** (AR-3), so
`QuantiseEdit(DequantiseVoxel(Q)) == Q` for every `Q`.

Let `C = Op.CentreVox` and `r = Op.RadiusVoxQ16 / 65536.0`, evaluated in `double`. The division
is exact: a Q16 value is an integer and 65536 is a power of two, so `r` carries no rounding of
its own, on any IEEE-754 platform.

**Sphere write set** — the voxels an op may change:

```
W = { v : (v.x-C.x)² + (v.y-C.y)² + (v.z-C.z)² <= r² }
```

Squared distance, compared to `r²`, in `double`, with **`<=`**. Both sides are computed from
integers and one exact `r`, so the comparison is exact for every radius the game can express;
there is no epsilon and none may be added. `r` itself is never compared against a square root.

**Sphere read bounds** — the axis-aligned box the kernel scans and the box that render and
collision invalidation must cover:

```
B = [ C - floor(r) , C + floor(r) + 1 )        // Min inclusive, Max exclusive
```

`floor(r)` is exact rather than conservative: the largest integer offset `d` with `|d| <= r` is
`floor(r)`, so `B` contains `W` with no slack on any axis. `VoxelsScanned` is the product of
`B`'s three extents.

**Box write set** is `[C - ExtentVox, C + ExtentVox)`; the read bounds are the same box. A box op
with any non-positive extent is rejected.

**Nothing outside `W` may change.** This is the property that makes the read bounds meaningful,
and it is asserted rather than assumed.

#### 4.10.3 Canonical per-operation meaning

`occ(value)` is occupancy: `1` for fully solid, `0` for fully empty, monotone in between. The
density convention is §4.2's: normalised, negative is solid.

| Op | Density | Material | Required properties |
|---|---|---|---|
| `Remove` | Every voxel in `W` becomes **no more solid** than it was. Voxels whose whole cell is inside `W` become totally empty | **Preserved.** Removing rock does not repaint it | Monotone toward empty; idempotent |
| `Add` | Every voxel in `W` becomes **no less solid** than it was. Voxels whose whole cell is inside `W` become totally solid | Set to `Op.MaterialId` wherever density increased | Monotone toward solid; idempotent |
| `Paint` | **Unchanged, exactly.** Not "approximately" | Set to `Op.MaterialId` throughout `W` | Density-preserving; idempotent |

**Idempotence is the load-bearing property.** Applying the same op twice must report
`VoxelsTouched == 0` the second time. It is cheap to test, it is backend-neutral, and it is what
makes replay safe: DEF-3's duplicate-application hazard during JIP is survivable for an
idempotent op and is corruption for a non-idempotent one. An op kind that cannot be made
idempotent does not belong in the set — which is a second, independent reason `Smooth` is not in
it.

**Monotonicity** is what lets validation reason about an op without simulating it. A `Remove`
can never create solid rock, so a clearance check that passes before the op cannot be invalidated
by the op.

#### 4.10.4 What determinism means here, stated precisely

DEF-5's core observation was right: integer inputs remove one hazard and prove nothing on their
own. So the claim is split into three, and only two of them are made.

**(a) Same backend, same build, same inputs → identical output. REQUIRED.**
Same op sequence, same seed, same `GeneratorVersion`, same starting state ⇒ identical
`HashRegion` for every chunk. This is what replay, journal compaction and the convergence test
all depend on. Evidence: `Adapter.Determinism` (§6.2), 20 runs.

**(b) Same backend, different build or platform → identical output. REQUIRED, and at risk.**
The op is integers; `r` is exact; the sphere test is an exact integer comparison. The residual
risk is entirely in the *kernel's* floating-point arithmetic — compiler contraction of a
multiply-add, a different vectorisation, a fast-math flag. The mitigations are: the game side
uses no float in the geometry (above); `bMultiThreaded = false` on **both** server and client
until E-2 reports, because client results supply collision and are not cosmetic; and the golden
fixtures in 4.10.6 fail loudly if a toolchain change moves a value. **This is stated as a
requirement with a named residual risk, not as a proof.**

**(c) Different backends → identical output. NOT REQUIRED. Explicitly out of scope.**
§8.1 assigns "sphere/box/level edit kernels" to the **plugin** while assigning "edit semantics —
what Remove, Add, Paint mean" to the **game**. Those two are consistent only if the game
specifies *properties* and the backend supplies *values*. So two conforming backends may write
different densities for the same op — `FMemoryTerrainBackend` writes a binary fill, the plugin
writes its own signed-distance ramp — and both are correct.

**The consequence is a real one and it sharpens FM-9 rather than solving it: a backend swap is a
resample migration for every EDITED chunk, not a format-compatible reload.** Pristine chunks
regenerate from the field and are unaffected. FM-9 previously flagged only differing voxel size
or grid alignment; differing kernels belong on the same line. `backendVersion` in the snapshot
header (§4.7) is what makes the mismatch detectable instead of silent.

`Backend.Conformance` therefore asserts the **contract** — write set, read bounds, monotonicity,
idempotence, material rules, result accounting, refusal behaviour — and never asserts equality of
densities between two backends. That is what "replaceable" means operationally, and it is a
weaker claim than the one §10 could be read as making.

#### 4.10.5 `HashRegion`

- **Position-sensitive.** The voxel's index within the chunk is folded in, so two chunks holding
  the same multiset of values in different places hash differently. Without this, rearranged
  terrain hashes identically and FM-1's convergence test proves nothing.
- **Iteration-order-independent.** A backend that walks a chunk in a different order must produce
  the same hash, so the hash is a sum of per-voxel mixes rather than a rolling chain.
- **Covers values and materials**, and only resident data. A non-resident region hashes `0`, and
  `0` is never a valid hash of resident data.
- **Comparable only within one backend and build**, by 4.10.4(c).

`FMemoryTerrainBackend::HashRegion` already satisfies all four and is the reference.

#### 4.10.6 Version compatibility rules

Three versions travel with saved data (§4.7): `generatorVersion`, `backendVersion`, and the
journal/snapshot format version. They answer different questions and are not interchangeable.

| Mismatch | Pristine chunk | Edited chunk (has a payload) |
|---|---|---|
| **`generatorVersion`** differs | Regenerate from the field. The new world is the world | **Payload is authoritative.** Never re-derive it from ops, because the ops were applied to a different baseline |
| **`backendVersion`** differs | Regenerate | **Payload is authoritative; op replay is FORBIDDEN.** A different kernel applied to the same ops is a different world (4.10.4c) |
| **Format version** differs | Migration, `.bak` first (FM-2) | Migration, `.bak` first |
| **Unknown wire enum** on an op | — | Reject the op. Already enforced by `DeserializeTerrainOp` |

The rule underneath all four rows: **ops are only ever replayed against the exact
(generator, backend, format) triple they were recorded under.** Anything else uses the snapshot
payload or regenerates. What remains open is *recovering* an edited chunk when its payload has
been compacted away and the triple has changed — that is DEF-9, and it stays open.

#### 4.10.7 Golden fixtures

The fixtures are committed, not generated at test time, so a change in behaviour shows up as a
failing test rather than as a quietly updated expectation.

`TerrainCore.Op.Semantics.Golden` (§6.1) runs a fixed script of `Remove`/`Add`/`Paint` ops —
including negative coordinates, chunk-boundary straddles, the exact-`r` boundary case, a
zero-effect repeat of each op, and a rejected op — against `FMemoryTerrainBackend` from a fixed
starting state, and compares every affected chunk's `HashRegion` against values recorded in the
test itself.

**A failure of this test is never fixed by updating the number.** It means either the kernel
changed, which needs a `backendVersion` bump, or the toolchain moved under it, which is 4.10.4(b)
reporting for duty. Either way the number is evidence, not a parameter.

The plugin adapter has its own fixtures under `Adapter.Determinism` (§6.2) with its own expected
hashes, for the same reason and with no cross-comparison to these.

### 4.11 Admission, commit and split operations — DEF-7 resolution

**Architect determination, T-114, 2026-09-07** (technical, per D-023).

DEF-7 asked for trusted request inputs, full quantised-footprint validation, request identity and
retry dedup, resource reservation and revalidation, queue limits and fairness, the
no-change-or-committed-result invariant, and explicit split-operation semantics.

#### 4.11.1 What the server takes from the client, and what it refuses to

A request is **evidence of intent**, never a description of what will happen. The split below is
the whole of it; anything not in the left column is not an input.

| The client may supply | The server derives, and never reads from the request |
|---|---|
| `RequestId` — client-scoped, monotonic per connection | `SourceId` — from the connection, never from the payload |
| `Kind` — one of the three live kinds (§4.10.1) | `Source` — `Player` for a connection, `Machine`/`Admin`/`Worldgen` are server-originated only |
| An aim point in world space | `CentreVox` — the server quantises, once (§4.3) |
| A requested radius in cm | The effective radius — `min(requested, tool max, MaxEditRadiusCm)` |
| `ToolId` | `MaterialId` for `Add` — from the tool and the requester's inventory |
| — | `OpSeq`, `TransactionId` — assigned at commit |
| — | Reach — recomputed from the **server's** pawn transform, with a tolerance. The client's camera is not an input |

`ToolId` is the interesting one: it is client-supplied *and* it affects yield (§4.9), so it is
validated as owned, equipped, off cooldown and charged — never trusted. §4.4 already says this;
DEF-7's addition is that the same checks run **again at commit** (4.11.4).

#### 4.11.2 Validation runs on the quantised footprint, not the request

Every admission check is evaluated over the **integer** write set `W` and read bounds `B` of
§4.10.2, computed from the already-quantised op — not over the float sphere the client asked for.

- world bounds: `B` entirely inside `WorldBoundsVox`
- residency: every chunk `B` touches is resident
- zone permission (D-004): evaluated for every chunk `W` touches, not just the centre's
- clearance (DEF-8): evaluated against `W`
- size: `|W| <= MaxVoxelsPerOp`, and `|B|` recorded as the read cost (§7.1)

**Why this is a rule and not an implementation detail.** A check performed on the requested float
sphere and an application performed on the integer op can disagree by exactly one voxel at a
boundary. One voxel of disagreement between "permitted" and "changed" is a permission bypass at
the edge of every protected zone in the game, and it is invisible until someone looks for it.

#### 4.11.3 Request identity and retry dedup

- `FTerrainEditRequest` carries a `RequestId`, unique and monotonic **within a connection**. The
  identity of a request is the pair `(SourceId, RequestId)`; a client cannot forge another
  client's identity because it does not supply `SourceId`.
- The server keeps, per connection, a bounded ring of recently **resolved** request identities and
  their receipts — 64 entries, which at the §7.1 rate limits is several seconds of history.
- **A repeat of a resolved identity returns the stored receipt and mutates nothing.** It is not a
  new op, does not consume an `OpSeq`, and does not settle yield a second time.
- A repeat of an identity that is still *queued* is dropped, and the original resolves normally.
- An identity older than the ring is rejected `StaleRequest` rather than executed. Executing it
  would be the mining-twice bug the ring exists to prevent, and rejecting a very old retry is
  always safe: the client can ask again with a new id.

Reliable RPCs are re-sent across reconnects and seamless travel. Without this, a re-sent dig
mines the same rock twice and credits the ore twice.

#### 4.11.4 Reservation and revalidation — the two-phase rule

DEF-7: "several admitted edits can pass the same remaining-resource check; a requester can move,
lose permission or disconnect while queued."

**Phase 1 — admission.** The checks in 4.11.2 run, and the request **reserves** what it will
consume: a queue slot, a rate-limit token, and any tool charge or fuel the op will cost. A
reservation is held against the requester and is visible to the next request's checks, so two
admitted ops cannot both pass the same remaining-charge test.

**Phase 2 — commit.** Immediately before `ApplyOp`, on the serialised path, **every check is run
again** against current state: the requester still exists and is connected; the tool is still
owned, equipped, off cooldown and charged; the zone still permits it; reach still holds against
the requester's *current* server-side position; every chunk in `B` is still resident; clearance
still holds.

Revalidation failure releases the reservation and rejects the request. **Nothing has been mutated
at that point**, so this is clean by construction rather than by rollback — the design has no
rollback and does not need one.

Reservations are released on exactly three events: commit, rejection, and Draining (§4.5.1 step
3). A disconnect is not a fourth event; it is detected at revalidation.

#### 4.11.5 Queue limits and fairness

- **Bounded, twice.** A global depth cap and a per-source depth cap. Exceeding either rejects the
  new request `QueueFull` at admission. An unbounded queue under FM-7's scripted client is a
  memory-growth failure that looks like a leak.
- **Round-robin across sources, FIFO within a source.** One player holding down the mine button
  cannot starve another. Machines share the same queue at the same priority.
- **No priority classes.** A priority class is a starvation bug that only appears under the load
  you cannot reproduce, and nothing in the design needs one: `MaxVoxelsPerOp` already bounds the
  worst single op, so head-of-line blocking is bounded by the §7.1 budget of 8 ms.
- **Queue age is measured, not assumed** (§7.1), and is the direct input to whether K4's
  game-thread ruling survives contact with 16–32 players.

#### 4.11.6 The no-change-or-committed-result invariant

**For every request, exactly one of two outcomes occurs. There is no third.**

| Rejected | Committed |
|---|---|
| No voxel changed | Every voxel in `W` that the kernel decided to change, changed |
| No `OpSeq` assigned | `OpSeq` assigned, monotonic |
| No journal record | Journal record appended |
| No client broadcast | Broadcast to every subscriber of every affected chunk |
| No yield settled | Yield settled |
| No chunk revision moved | Every affected chunk's revision bumped **exactly once** |
| A receipt with a reason | A receipt with the result |

Two consequences follow, and both are changes to what the document previously allowed:

**`bTruncated` is removed from the contract as a success signal.** DEF-7 named it precisely:
"`bTruncated` and a boolean failure both imply possible partial mutation, which would permit
unjournalled terrain." A backend that would have truncated must instead **fail the whole op and
mutate nothing**. The field stays in `FTerrainEditResult` for wire and struct stability but is
`false` on every successful result; a backend setting it true on success is non-conforming.

**`ApplyOp` returning false means nothing changed.** Not "something may have changed". The
implementation rule that makes this true differs by backend and both are required to reach it:

- `FMemoryTerrainBackend` **stages** every change and validates the entire write set before
  writing a single voxel. It already does this.
- `FVPLegacyBackend` cannot stage inside the plugin, so it **pre-validates the entire footprint**
  — bounds, residency, and the write count against `MaxVoxelsPerOp` — before calling the kernel,
  so the call has no remaining precondition to fail on. The plugin's sphere kernel writes a
  bounded set synchronously and has no partial-failure return; **that is an assumption about the
  plugin, it is stated here rather than buried, and `Backend.Conformance` probes it** by
  attempting ops that fail each precondition and asserting the region hash is unchanged.

#### 4.11.7 Split operations

DEF-7 required "explicit split-operation semantics"; §4.4 previously said only that very large
ops split into sub-ops sharing a `TransactionId` and that geometric equivalence "is
operation-dependent".

**Ruling: only `Box` ops split. A `Sphere` over the cap is rejected `TooLarge`, never split.**

A sphere cannot be partitioned into smaller spheres whose union is the original, and the wire
encoding is permanent at 58 bytes (§4.2), so there is nowhere to put the clip box a correct
partition would need. Inventing an approximate split would make the same request produce
different terrain depending on whether it happened to exceed a cap — which is a determinism bug
wearing a performance feature's clothes. A box partitions exactly, using the `ExtentVox` field
that already exists.

Split rules:

1. **Partition, do not re-shape.** The box is halved along its longest axis, recursively, until
   every sub-box is within `MaxVoxelsPerOp`. Sub-boxes are disjoint and their union is exactly
   the original write set.
2. **One `TransactionId`, many `OpSeq`.** Sub-ops share the transaction id and each receives its
   own `OpSeq`, increasing, contiguous within the transaction.
3. **The transaction is NOT atomic.** Each sub-op commits independently and satisfies 4.11.6
   independently. A snapshot taken between two sub-ops is legal, a client may observe a partly
   excavated region, and a crash between sub-ops leaves the completed ones committed. This is
   stated rather than assumed because the alternative — a cross-op transaction — would need a
   durability protocol that DEF-1 has not yet defined and step 4 has not yet built.
4. **Admission is all-or-nothing; commit is per sub-op.** The whole transaction is validated and
   reserved at admission, so a split excavation cannot begin and then be refused halfway for
   want of charge. Each sub-op still revalidates at commit (4.11.4), and a sub-op that fails
   revalidation ends the transaction: later sub-ops are dropped, not retried.
5. **Yield settles per sub-op.** Summation over a transaction is DEF-6's problem, not this one.

Evidence: `TerrainCore.Split.Equivalence` (§6.1) — a box op applied whole and the same op applied
as its split produce identical region hashes, including when the split boundary falls on a chunk
boundary and when it does not.

#### 4.11.8 New rejection reasons

`ETerrainEditRejection` gains four values. Each exists because a client that cannot tell it from
its neighbour will do the wrong thing:

| Reason | Meaning | What a client should do |
|---|---|---|
| `ShuttingDown` | The world is going away (§4.5.1) | Stop. Do not retry |
| `QueueFull` | Global or per-source depth cap hit | Back off, retry later |
| `StaleRequest` | Identity older than the dedup ring | Retry with a **new** `RequestId` |
| `Revalidation` | Passed admission, failed at commit | Re-check local state, then retry with a new id |

---

## 5. Failure modes

| # | Failure | Mechanism | Response | Residual |
|---|---|---|---|---|
| FM-1 | **Silent client/server divergence.** Client sees a wall the server calls air | Float rounding; a dropped op; plugin nondeterminism | Integer-voxel ops; per-chunk rev gap detection; `HashRegion` in the convergence test; blunt resync. §4.10.5 requires the hash to be position-sensitive and order-independent, and `FMemoryTerrainBackend` is the reference | Divergence *inside* a chunk with matching revs. The residual is the kernel's own floating point — 4.10.4(b) — and E-2 is what probes it |
| FM-2 | **Save corruption or loss.** The world is gone. Per D-012 the unacceptable one | Torn write; bad migration; generator change invalidating sparse snapshots | CRC per record and snapshot; torn tail truncated; `.bak` before migration; `generatorVersion` in every header, load refuses on mismatch | Recovery from a compacted journal is not generally possible — DEF-9 |
| FM-3 | **Yield/terrain divergence.** Ore in the bag, no hole, or the reverse | Crash between apply and credit; two stores, two orderings | **Unresolved — DEF-1.** A watermark alone does not make two disks atomic | Blocks build step 4 |
| FM-4 | **JIP burst saturates the connection** | Snapshot stream competing with movement on the same reliable channel | Per-connection byte budget; pristine fast path; nearest-first ordering; outstanding-byte limit | A 100-chunk dense region is 12.8 MB ≈ **50 s** at 256 KiB/s, not sub-second. Compression or a smaller radius is required, and E-3/E-6 decide which |
| FM-5 | **Player falls through the world after editing** | Collision cook lags the data edit | Self-clearance validation; KillZ + respawn as the net; E-4 looks for a cook-complete signal | Protects only the requester. DEF-8 |
| FM-6 | **Movement corrections discarded while standing on terrain** | The proc-mesh has no net GUID | §7.4 — attempt static mobility so the mesh is never a *relative* base | Genuinely unknown (E-9). If no configuration works, this is a backend-adoption argument for the T-101B verdict |
| FM-7 | **One client DoSes the edit queue** | Scripted client spamming max-radius ops | Per-source rate limit and `MaxVoxelsPerOp` before the queue; queue depth cap; queue-age metric | Friends server, low priority, but D-002 says never trust the client |
| FM-8 | **Plugin async work never completes / world not created** | `bCreateWorldAutomatically` defaults false; task starvation; the known shutdown `ensure` | Service refuses to leave `Initializing` until the backend reports ready; edits queued, not dropped; flush on shutdown before final compaction | Server startup ordering is a real integration risk on a dedicated build (R-007) |
| FM-9 | **Backend swap invalidates saves** | Snapshot encoding was backend-native; **and two conforming backends write different densities for the same op** | Payload is `int16` normalised density + game material ids — portable by construction; `valueConfig` and `backendVersion` recorded, and §4.10.6 forbids replaying ops across a `backendVersion` change | **Sharpened by DEF-5.** A swap is a resample migration for every EDITED chunk, not only for a differing voxel size or alignment. Pristine chunks regenerate and are unaffected. Documented, not solved |
| FM-10 | **The adapter leaks.** A plugin header in gameplay | Convenience under deadline | `TerrainCore` has no plugin dependency in `Build.cs` — compile error | The streaming component was the pressure point; §7.4 removes it |

---

## 6. Test plan

### 6.1 Headless — `TerrainCore` automation, no engine world, no plugin

Run against `FMemoryTerrainBackend`. Seconds, on every build.

| Test | Asserts |
|---|---|
| `Op.Codec.RoundTrip` | Every `FTerrainOp` serialises and deserialises byte-identically, including negative coordinates and max radius. **Defines the 58-byte encoding** |
| `Op.Quantisation.Stable` | The same world-space request quantises to the same integers across 10,000 randomised transforms |
| `Op.Semantics.Golden` | A fixed op script from a fixed starting state reproduces committed per-chunk hashes. **The DEF-5 golden fixtures** (§4.10.7). Never fixed by updating the number |
| `Op.Semantics.Contract` | Write set, read bounds, monotonicity, idempotence, material rules and refusal of reserved kinds, per §4.10.2–4.10.3 |
| `Split.Equivalence` | A box op applied whole and applied as its split produce identical region hashes, on and off chunk boundaries (§4.11.7) |
| `Field.Shape` | The generated world has the GDD's hill, cliff and basin, and the open plain never rises above world Z = 0 |
| `Field.Strata` | Strata are ordered by depth; the ore body exists, is finite, and never breaks the surface |
| `Field.Range` | `SampleRange` never excludes a value `Sample` can produce (AR-6), and saturates for sky and deep rock |
| `Field.Determinism` | Two fields with one seed agree bitwise; a field does not drift as it is used; a different seed changes the world |
| `Journal.RoundTrip` | Write N records, reopen, read N identical |
| `Journal.TornTail` | Truncate mid-record → loader recovers N−1 and reports the truncation |
| `Journal.BadCrc` | A flipped byte is detected, not loaded |
| `Snapshot.Codec.Dense/Sparse/Empty` | Round-trip for all three; sparse and dense produce identical regions |
| `Snapshot.EncodingChoice` | The smaller encoding is selected; both decode |
| `Snapshot.MaterialOnlyChange` | A material-only edit survives sparse encoding and is not misclassified as pristine |
| `Revision.Monotonic` | Chunk revs never decrease, including across payload deletion; a multi-chunk op bumps every affected chunk exactly once |
| `Replay.Equivalence` | `snapshot@R + ops after R` == `apply all ops from base`. **The central persistence invariant** |
| `Compaction.Equivalence` | Region hash before == after |
| `Compaction.Reverted` | A chunk restored to natural shape keeps its metadata and reloads identically |
| `Retention.Dependency` | A segment is not deletable while any consumer, terrain or entity, still needs it |
| `Yield.Accumulation` | With a known synthetic field, removing a known volume yields the expected per-material totals |
| `Yield.MaxVolume` | A maximum-permitted single-material edit does not overflow the yield field |
| `Migration.Fixtures` | Every fixture in `Tests/Saves/` loads and produces its expected region hash |
| `Query.Point` | `QueryPoint` returns the material and density sign the region was written with, at chunk interiors and at all eight chunk corners; reports `bResident` false outside loaded regions; never returns a stale sample after `ApplyOp` or `WriteRegion` |
| `Backend.Conformance` | A shared suite run against **both** `FMemoryTerrainBackend` and `FVPLegacyBackend`, covering all eleven methods, `QueryPoint` included, plus: off-game-thread calls are refused (§4.5.1); the state machine rejects with the right reason per state; a failed `ApplyOp` leaves the region hash unchanged (§4.11.6). It asserts the **contract**, never equality of densities between two backends (§4.10.4c). Any future backend must pass it. **This is the operational meaning of "replaceable"** |

**Test identifiers are prefixed `TerrainCore.` in code.** `Automation RunTests` does a
substring match (`AutomationCommandline.cpp`), so the bare names in this table match
nothing on the command line. The table names the assertion; the prefix names the test.

### 6.2 In-engine, single process

| Test | Asserts | Risk |
|---|---|---|
| `Adapter.ApplyOp.Matches` | Conformance suite against the real plugin | D-011 |
| `Adapter.Determinism` | Same op sequence, same seed, same `HashRegion` over 20 runs, across both threading modes | R-001 |
| `Yield.Volume` | Remove r = 2 m in homogeneous stone; `Σ Δocc × V` within tolerance of `4/3 π r³` | R-004 |
| `Yield.MixedGeology` | Partial, overlapping and strata-boundary digs account correctly | R-004 |
| `Restart.Identity` | Dig, shut down, boot, compare every chunk hash | R-003 |
| `Restart.CrashMatrix` | Crash injected before and after every durable boundary; no duplicated or missing payout, no durable ore without durable removal | R-003, DEF-1 |
| `Save.Growth` | 1,000 scripted edits; bytes/edit, snapshot size after compaction, compaction wall time | R-003 |

### 6.3 Multiplayer PIE / standalone — the T-101B gate proper

Requires streaming interest (§7.4) as an entry cost, and PIE at the three-client settings (D-021).

| Test | Asserts | Criterion |
|---|---|---|
| `MP.Convergence` | 3 clients dig the same 5 m region for 60 s; final `HashRegion` identical on server and all clients | A1 / R-001 |
| `MP.JoinInProgress` | Server dug for 10 min; fresh client joins; nearby chunk hashes match; measure bytes and time | A2 / R-002 |
| `MP.JIP.UnequalCuts` | Snapshots taken at different revs around a multi-chunk op; both chunks match the server | DEF-3 |
| `MP.StandingOnEdit` | Client A stands on terrain; **client B** removes it beneath them; A falls correctly and is never desynced | A6 / R-010 |
| `MP.SpawnIntoExcavation` | A joining client spawns into a dug region; collision is ready before movement is permitted | DEF-8 |
| `MP.Relevancy` | A client 500 m away receives zero ops for the dig site; verified by counter, not by eye | AGENTS §4 |
| `MP.Resync` | Force-drop an op; the rev gap is detected and the chunk repairs itself | FM-1 |
| `MP.Bandwidth` | 8 clients digging continuously; per-connection bytes/s for ops and snapshots | FM-4 |

---

## 7. Performance

Budgets are **starting numbers to measure against**, not claims.

### 7.1 Per-op server cost

A hand tool at 200 uu with 50 cm voxels is a 4-voxel radius — ~270 voxels, trivial. The measured
sculpting sphere at r = 3000 was 514,627 voxels and its modified-value array alone was ~10 MB.

- **`MaxVoxelsPerOp = 65,536`** written (≈ r = 25 voxels ≈ 12.5 m sphere). Larger requests split
  into sub-ops sharing a `TransactionId`.
- **The read footprint at that cap is ~140,000 positions**, more than double the write count.
  Scratch memory and render/collision invalidation scale with the read box, not the write sphere.
- Execution budget: **≤ 8 ms per op** at the cap. At the 16–32 player upper bound this is
  `32 × 3 × 8 ms = 768 ms` of serialised work per second before snapshot capture and accounting —
  **thin headroom on a single serialised path.** Queue age and tail latency are measured, not
  assumed, and this is direct input to K4.

### 7.2 Bandwidth

- Op broadcast: the 58-byte body plus per-chunk revisions, affected keys, transport and fragment
  headers, acks and resync traffic. **The 58 bytes is the body, not the message.** The real
  per-op wire cost is measured in `MP.Bandwidth`.
- JIP: pristine chunks cost 12 bytes. A dug chunk costs its snapshot. **A 100-chunk dense region
  is 12.8 MB — roughly 50 seconds at 256 KiB/s.** A 10× sparse win still costs ~5 s. Either
  compression carries it (E-3) or the subscribe radius comes down (E-6). This is the most likely
  thing in the design to be wrong.

### 7.3 Fragmentation and backpressure

Snapshots are fragmented under UE's reliable-bunch limits, sent nearest-first, and bounded by a
per-connection **outstanding-byte and outstanding-fragment limit**, not only a mean byte budget.
Fragments carry `SyncGeneration` so that a resync after unsubscribe/re-entry discards stale
fragments from the previous generation. Whether catch-up can finish while edits continue is
measured, not assumed.

### 7.4 Streaming interest and the movement base

**Streaming interest.** The plugin requires an invoker on every character on both client and
server, and putting a plugin component on `BP_ThirdPersonCharacter` is exactly the leak D-011
forbids. Resolution: **`TerrainCore` owns `UTerrainStreamingComponent`**, a game class with no
plugin dependency. It registers an `FTerrainStreamingInterest` with `UTerrainService` and updates
it on movement. The service forwards to `ITerrainBackend::SetStreamingInterest`, and
`FVPLegacyBackend` creates, moves and destroys the plugin invoker internally.

Gameplay attaches a `TerrainCore` class. No gameplay code and **no Blueprint asset** holds a
reference to an adapter class, so a backend swap needs no gameplay or asset change. Interest
handles are owned by the service and released on `EndPlay`, travel and connection loss. Server
interests are collision-only; client interests carry render. Cost at 32 dispersed players is
**unknown — E-8**; dispersed is the normal case on a build-anywhere server, against a default of
two worker threads.

**Movement base.** The proc-mesh has no net GUID, so every position correction on a player
standing on terrain is discarded. `MovementBaseUtility` only stores a *relative* base for movable
primitives, so if the proc-mesh can be forced to static mobility while still updating on edit,
the character stores an absolute base and corrections resolve normally. **Unknown — E-9.** The
fallback is a movement-component override refusing the terrain mesh as a relative base; if that
also fails, this is a genuine argument in the T-101B adoption verdict rather than a design
problem. Passing E-9 does not close the independent collision-readiness issue (DEF-8).

---

## 8. Plugin-specific vs game-owned

### 8.1 The line

| Concern | Owner |
|---|---|
| Edit semantics — what Remove, Add, Flatten, Smooth, Paint mean | **Game** |
| Op ordering, `OpSeq`, chunk revisions, transaction policy | **Game** |
| Validation, permissions, reach, rate limits, request identity | **Game** |
| Material identity, ore grade, catalog versioning | **Game** |
| Yield policy, tool efficiency, placement debit, economy | **Game** |
| World shape — density and material field | **Game** (`ITerrainDensityField`) |
| Persistence format, journal, compaction, retention, migration | **Game** |
| Relevancy, subscription, JIP protocol, transport | **Game** |
| Chunk keys and coordinate policy | **Game** |
| Streaming intent (where terrain must be resident, and why) | **Game** |
| — | — |
| Density storage and octree | Plugin |
| Meshing, LOD, materials-as-rendered, collision cooking | Plugin |
| Sphere/box/level edit kernels | Plugin |
| Invoker mechanics | Plugin, behind `SetStreamingInterest` |
| Threading of edit and mesh work internals | Plugin — we serialise *entry*, not internals |

### 8.2 Explicitly not used

- **The plugin's TCP multiplayer.** Not an implementation in Free; both entry points return
  false immediately. Even in Pro it is a side channel outside UE authority and would violate
  D-002 and D-011 in one step.
- **The plugin's whole-world save object as the persistence mechanism.** The unit is the whole
  world and the editor-only save path cannot serve a dedicated server. **Kept as a correctness
  oracle:** `Restart.Identity` compares our reconstruction against a plugin whole-world
  save/load of the same session. An independent second opinion on our own save code is cheap.
- **`bOverrideSave`** at world creation — it is the whole-world blob D-012 rejects, and it is
  mutually exclusive with `bOverrideData`, which is the path we use.
- **Spawners, voxel physics, mesh import, surface masks** — stubbed in Free. Nothing here
  touches them. This means R-005 (foliage over excavations) cannot be observed through plugin
  spawners on this backend; foliage is PCG and conventional meshes per D-015 anyway.
- **Undo/redo.** Per-leaf frame stacks are an editor affordance. Our journal is the history.
  Do not enable undo on the server: memory growth with no consumer.

---

## 9. Build order

Each step compiles, is testable alone, and moves at least one risk. **A step may not start while
an unresolved defect or unruled fork is bound to it.**

| # | Step | Ends with | Bound |
|---|---|---|---|
| 0 | Create the `VoxelWorld` and `TerrainCore` C++ modules with one empty subsystem | **The project builds from source for the first time.** Nothing else changes | K7, K8 |
| 1 | `FTerrainOp`, chunk keys, `ITerrainBackend`, `FMemoryTerrainBackend`, `UTerrainService` skeleton | Codec, quantisation and revision tests pass, headless | — |
| 2 | `FVPLegacyBackend` + `UTerrainStreamingComponent`; **rewire the T-101A Blueprint to `RequestEdit` and delete the direct plugin calls** | Digging works as today, through the service, server-authoritative in standalone | — |
| 3 | Server validation, serialised execution, `ClientApplyOp`, subscription set | 3-client PIE convergence | K1, K4 (ruled D-024); **DEF-4, DEF-5, DEF-7 — all resolved at T-114. Step 3 is UNBLOCKED** |
| 4 | Journal + snapshot + compaction + boot replay | Restart identity, crash matrix, save growth | K2, K3, K5, K10; DEF-1, DEF-2, DEF-9 |
| 5 | JIP protocol, fragmentation, resync | Join-in-progress with measured bytes | DEF-3 |
| 6 | Yield pipeline + material config + inventory settlement | Volume accuracy, mixed geology | K9; DEF-6 |
| 7 | Collision-readiness policy, movement-base experiment | Standing-on-edit, spawn-into-excavation | DEF-8 |
| 8 | C++ `ITerrainDensityField` with strata and ore bodies (T-108), forwarded through the adapter generator | The test hill generates instead of being sculpted by script | R-008 (a risk, not a defect — §14's rule never bound this step) — **DONE 2026-09-07** |

Steps 3–7 map one-to-one onto the T-101B sub-steps. The architecture is built by running the
gate, not before it.

**Steps are not required to run in order.** §14's rule is about *defects*, not about sequence:
a step may start as soon as nothing unresolved is bound to it. Step 8 was taken out of order at
T-108 for exactly that reason — steps 3–7 were blocked by DEF-4, DEF-5 and DEF-7 while step 8 was
bound only to a risk, and step 8 was the one that moved the Phase 1 milestone.

**Step 2 closes both flagged drift checks in standalone only.** Server-authority is not proven
until the multiplayer route is exercised at step 3. Record the narrower result at each step.

---

## 10. Backend replacement

1. Write `TerrainBackendX` implementing the eleven `ITerrainBackend` methods.
2. Run the **`Backend.Conformance` suite** against it. It passes or the backend is not a
   candidate. This suite is the definition of the contract; there is no other one.
3. Implement `ITerrainDensityField` forwarding for the new backend's generator concept.
4. Set `BackendModule=TerrainBackendX` in config. No gameplay code changes, no asset changes, no
   save format changes — unless voxel size or grid alignment differ, in which case a resample
   migration is required and is bounded, known work (FM-9).
5. Run §6.2 and §6.3 unchanged. The tests are written against the service, not the plugin.
6. The swap test must exercise **character attachment, respawn and teardown**, not only density
   operations.

If a swap requires touching anything in `TerrainCore` or `VoxelWorld`, the boundary has failed
and that is a bug in this architecture, not in the new backend.

---

## 11. Unknown — prototype this

Nine things this design rests on that evidence does not establish. Naming them is correct
behaviour under AGENTS §10, not a gap.

| # | Unknown | Experiment | If unfavourable |
|---|---|---|---|
| **E-1** | Density-to-occupancy mapping, and whether `int16` density is linear enough in the transition band for accurate volume | Remove spheres r = 1, 2, 5 m in homogeneous material; compare `Σ Δocc × V` to `4/3 π r³`; 20 runs. Extend to layered and repeated-dig fixtures | Yield falls back to counting fully transitioned voxels with a calibrated factor — less elegant, still simulation-owned |
| **E-2** | Whether parallel edits are bit-deterministic, and whether the sync and async overloads agree | Same op sequence, 20 runs, all four combinations; compare `HashRegion` | Both server *and* client paths force single-threaded; measure the throughput cost against §7.1 |
| **E-3** | Real compression ratio of sparse-diff for a realistically tunnelled 32³ chunk | Dig a 20 m tunnel; write both encodings; compare bytes | Drop to 16³ chunks (K2) — one constant — and re-measure. If still poor, FM-4 forces a smaller subscribe radius |
| **E-4** | Whether a collision-cook-complete signal is reachable | Instrument an edit; log the interval between data commit and collision availability | Feeds DEF-8. Whether the weaker fallback is acceptable is a Director call, not an Architect one |
| **E-5** | Plugin lock semantics — what is safe concurrently and from which thread | Stress two threads on disjoint and overlapping bounds under the plugin's debug build. **Must test the K4 path actually chosen** | An unfavourable result does not "confirm" a serialised thread; it may force game-thread execution |
| **E-6** | JIP payload size and time for a heavily excavated region; correct fragment size | Script 5,000 edits in a 100 m radius; join a fresh client; measure bytes, wall time, hitching, and whether catch-up finishes while edits continue | Lower the subscribe radius or move snapshots to a separate channel — a larger change, so measure early |
| **E-7** | Whether round-trip latency without prediction is tolerable for digging on a friends server | Play it. Subjective; the Director owns the verdict | Client-side prediction is additive, not a redesign |
| **E-8** | Cost of streaming interest per character at 16–32 dispersed players | Spawn 32 interest-bearing pawns; measure octree update cost and server frame time | May force a coarse server-side interest scheme, one per cluster of players |
| **E-9** | Whether the proc-mesh can be a non-relative movement base while still updating on edit | Set static mobility; stand on terrain in 3-client PIE; watch for discarded corrections and for edits still landing | Movement-component override; failing that, R-010 becomes a backend-adoption argument for the T-101B verdict |

**E-1, E-2 and E-9 could change this design rather than tune it.** They are cheap and run inside
build steps 2 and 3, not after the architecture is committed.

**A passing experiment closes no risk by itself.** RISKS.md requires a result *and* a decision.
None of R-001…R-010 is closed by this document.

---

## 12. Cross-platform and Linux

Nothing here establishes Linux dedicated-server compatibility. Determinism proven in same-process
runs is not cross-platform determinism. The Linux server build proof remains separately scheduled
per R-007 and is not inferred from any test in §6.

---

## 13. Foliage and navigation

Foliage invalidation over excavations (R-005) and navmesh behaviour under edits are **observed and
measured** during the gate, not implemented. They are recorded as gate observations and become
production tasks only by a later ruling.

---

## 14. Defect list

Adopted from `Docs/reviews/P-001-review-astra_proposal_reviewed_by_claude.md` per D-017. Each defect is bound to the earliest
build step that depends on it. **A build step may not start while an unresolved defect is bound to
it.** Closing a defect requires a written resolution in this document plus its named evidence.

| # | Defect | Bound | Status |
|---|---|---|---|
| **DEF-1** | **The commit protocol is not cross-store safe.** Crediting inventory before the journal record is durable leaves ore with no hole; the reverse leaves a hole with no ore, recoverable only while the record is retained. A watermark cannot reconstruct a missing record. Requires: the durable commit point, ack semantics, ordering of every terrain and entity effect, restart reconciliation, retention consumers, disk-error behaviour. Evidence: crash injection before and after every write, flush, entity transaction, ack and segment deletion, including transfer/spend after mining | 4 | **Open** — K5 |
| **DEF-2** | **Snapshot data and revision metadata have no atomic publication point.** A capture can observe post-edit data before the metadata commit and label it with the preceding revision; replay then applies an op already baked in. Overwriting a snapshot leaves no previous recoverable generation. Requires: a consistent capture boundary, crash-safe publication, the source of truth for the next global sequence, behaviour when mutation succeeds but persistence fails | 4 | **Open** |
| **DEF-3** | **Per-chunk JIP can replay an operation into a chunk that already contains it.** A multi-chunk op where one chunk's snapshot precedes it and another's includes it applies twice; arrival order changes the result. A global "already applied" flag is insufficient. Requires: application scope, read halos or a coordinated baseline set, duplicate handling, a finite sync cut, atomic sync/live handoff, stale-fragment discrimination, cancellation and backpressure | 5 | **Open** — partially mitigated by `SyncGeneration` (§4.8) |
| **DEF-4** | **A serialised execution path does not establish plugin thread or lifetime safety.** Meshing, collision, world destruction and generator access remain concurrent. An external bounds lock may conflict with one the wrapper takes internally. Requires: a thread-affinity and ownership table covering init, mutation, reads, render invalidation, callbacks and destruction; the shutdown state machine; cancellation on `EndPlay`, travel and PIE exit | 3 | **Resolved** (T-114, §4.5.1) — the affinity/ownership table, five rules, the four-state shutdown machine with its fixed eight-step teardown order, and the cancellation rule. The lock hazard is answered by holding **no** lock across the plugin boundary; cancellation is a queue operation because the path is synchronous and cannot be pre-empted. Evidence: `Backend.Conformance` (off-thread refusal, state-machine rejection reasons), `Adapter.Determinism`, `MP.*` under repeated travel. **The plugin's own internal thread safety stays unaudited — that is E-2/E-5, not this defect** |
| **DEF-5** | **Deterministic replay and operation semantics are underspecified.** Integer inputs remove one hazard but do not prove identical generator or kernel output across builds, platforms and backends. Flatten and Smooth are named without plane, strength, iteration or falloff semantics. `HashRegion` must be **position-sensitive**, or rearranged terrain hashes identically. Client results supply collision and are not cosmetic, so the client path cannot keep a nondeterministic mode the server drops. Requires: canonical semantics per operation including read bounds and rounding, version compatibility rules, golden fixtures | 3 | **Resolved** (T-114, §4.10). Flatten and Smooth are **removed from the operation set** and permanently refused rather than given invented semantics. Write set, read bounds, rounding, per-op meaning, monotonicity and idempotence are canonical. Determinism is split into three claims and only two are made: cross-**backend** value identity is explicitly **out of scope**, because §8.1 gives the kernel to the plugin — which sharpens FM-9 rather than solving it. `HashRegion` rules fixed. Evidence: `Op.Semantics.Golden` (committed fixtures), `Op.Semantics.Contract`, `Adapter.Determinism` |
| **DEF-6** | **Yield conservation and placement policy are unspecified.** Pre-edit material does not describe placed material; Smooth both removes and places; there is no material-debit rule for Add or Paint, so add-then-mine can mint resources. Requires: separate removal and placement accounting, placement cost, smoothing recovery, material conversion, capacity overflow, fractional residue. Evidence: mixed-material boundaries, Add/Paint/Remove cycles, repeated Smooth, split-vs-unsplit equivalence | 6 | **Open** — K9 |
| **DEF-7** | **Admission validation has no commit-time revalidation, and partial failure is undefined.** Several admitted edits can pass the same remaining-resource check; a requester can move, lose permission or disconnect while queued. `bTruncated` and a boolean failure both imply possible partial mutation, which would permit unjournalled terrain. Requires: trusted request inputs, full quantised-footprint validation, request identity and retry dedup, resource reservation and revalidation, queue limits and fairness, the no-change-or-committed-result invariant, and explicit split-operation semantics | 3 | **Resolved** (T-114, §4.11). Trusted-input split tabulated; validation runs on the **quantised** footprint; `(SourceId, RequestId)` identity with a 64-entry per-connection dedup ring; two-phase reserve-then-revalidate; bounded queue, round-robin across sources, no priority classes. `bTruncated` is **removed as a success signal** — a backend that would truncate must fail the whole op. **Only `Box` ops split**; an over-cap `Sphere` is rejected, because a sphere has no exact partition and the 58-byte wire has nowhere to put a clip box. Evidence: `Split.Equivalence`, `Backend.Conformance` |
| **DEF-8** | **Collision safety protects only the requester.** Player B can edit beneath player A while satisfying clearance from B's own capsule — which is the design's own central multiplayer test. A machine has no capsule. `ChunkSyncComplete` establishes no collision-readiness revision, so a joining player can receive matching data while collision is absent. Requires: safety for every affected occupant, joining player and machine; distinct data, mesh and collision readiness states; movement gating into unsynchronised terrain | 7 | **Open** — E-4, E-9 |
| **DEF-9** | **The save schema loses recovery information.** Pristine deletion would discard revision history; the naive retention minimum both over-retains for cold chunks and under-retains for economic consumers; `bIsGeneratorValue` reports provenance, not value or material equality; generator mismatch cannot generally recover from a compacted journal. Requires: dependency-aware reclamation, a recoverable migration protocol, and identification of authored stamps and catalog version sufficient to reproduce the ruled deterministic base | 4 | **Partially resolved** — yield field widened to signed `int64` µL; metadata preserved on payload deletion; framing and byte order made explicit (§4.2, §4.7). Retention and generator-mismatch recovery remain open |
| **DEF-10** | **The streaming attachment was not backend-independent.** Gameplay attaching an adapter class re-creates the dependency the boundary forbids, in code or in the asset | 2 | **Resolved** — §4.3 and §7.4: `UTerrainStreamingComponent` lives in `TerrainCore`; the backend receives intent through `SetStreamingInterest`/`ClearStreamingInterest`. Ownership and teardown specified. The swap test in §10 exercises attachment, respawn and teardown |

---

## 15. Open forks

**None.** K1–K10 were ruled at CP-005 by **D-024**, as a technical ruling under D-023. The rulings
are recorded in §3 and in `DECISIONS.md`. Nothing in this section routes to the Director.

---

*ARCHITECTURE.md v1. Adopted at CP-005 by D-017. No risk in RISKS.md is closed by this document.*

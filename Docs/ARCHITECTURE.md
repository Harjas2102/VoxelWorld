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

    // Streaming interest — the backend-neutral replacement for the plugin invoker.
    virtual void SetStreamingInterest  (const FTerrainStreamingInterest& In) = 0;
    virtual void ClearStreamingInterest(uint32 InterestId) = 0;
};
```

**Ten methods.** Everything the game does to terrain goes through them, which is what makes
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

### 4.6 The generator

Voxel Graphs are Pro-gated, so the generator must be C++ (T-108). That is usually a cost; here
it is leverage.

```cpp
// TerrainCore — no plugin dependency
class ITerrainDensityField
{
public:
    virtual float         Density (double X, double Y, double Z) const = 0;  // < 0 solid
    virtual FTerrainMatId Material(double X, double Y, double Z) const = 0;
    virtual uint32        Version () const = 0;   // bumped on ANY change to the field
};
```

`UVPLegacyDensityGenerator : UVoxelGenerator` (adapter) implements the plugin's value and
material queries by forwarding iteration to the field. The world's shape — strata, ore bodies,
the authored hill — is plain C++ that unit-tests without an engine and survives a backend swap
untouched. `Version()` is a first-class save-schema input, which K3 requires.

`FMemoryTerrainBackend` uses the same field, so headless tests exercise the real world shape.

**The T-101A hill was sculpted by script, not generated.** It is not reproducible from a seed and
is not preserved by the map package. The deterministic base under D-012 must explicitly name its
generation inputs and any authored stamps; it cannot inherit them from the saved test map.

### 4.7 Persistence schema

```text
Saved/World/<WorldId>/
  world.json                     # human-readable, versioned, small
  chunks/<X>_<Y>_<Z>.chunk       # snapshot at revision R  (binary, versioned)
  journal/<NNNNNN>.tjl           # append-only op log, size-segmented
  entities.sqlite                # players, inventories, structures, machines (D-012)
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

---

## 5. Failure modes

| # | Failure | Mechanism | Response | Residual |
|---|---|---|---|---|
| FM-1 | **Silent client/server divergence.** Client sees a wall the server calls air | Float rounding; a dropped op; plugin nondeterminism | Integer-voxel ops; per-chunk rev gap detection; `HashRegion` in the convergence test; blunt resync | Divergence *inside* a chunk with matching revs. E-2 and the hash test exist for this. Hash must be position-sensitive (DEF-5) |
| FM-2 | **Save corruption or loss.** The world is gone. Per D-012 the unacceptable one | Torn write; bad migration; generator change invalidating sparse snapshots | CRC per record and snapshot; torn tail truncated; `.bak` before migration; `generatorVersion` in every header, load refuses on mismatch | Recovery from a compacted journal is not generally possible — DEF-9 |
| FM-3 | **Yield/terrain divergence.** Ore in the bag, no hole, or the reverse | Crash between apply and credit; two stores, two orderings | **Unresolved — DEF-1.** A watermark alone does not make two disks atomic | Blocks build step 4 |
| FM-4 | **JIP burst saturates the connection** | Snapshot stream competing with movement on the same reliable channel | Per-connection byte budget; pristine fast path; nearest-first ordering; outstanding-byte limit | A 100-chunk dense region is 12.8 MB ≈ **50 s** at 256 KiB/s, not sub-second. Compression or a smaller radius is required, and E-3/E-6 decide which |
| FM-5 | **Player falls through the world after editing** | Collision cook lags the data edit | Self-clearance validation; KillZ + respawn as the net; E-4 looks for a cook-complete signal | Protects only the requester. DEF-8 |
| FM-6 | **Movement corrections discarded while standing on terrain** | The proc-mesh has no net GUID | §7.4 — attempt static mobility so the mesh is never a *relative* base | Genuinely unknown (E-9). If no configuration works, this is a backend-adoption argument for the T-101B verdict |
| FM-7 | **One client DoSes the edit queue** | Scripted client spamming max-radius ops | Per-source rate limit and `MaxVoxelsPerOp` before the queue; queue depth cap; queue-age metric | Friends server, low priority, but D-002 says never trust the client |
| FM-8 | **Plugin async work never completes / world not created** | `bCreateWorldAutomatically` defaults false; task starvation; the known shutdown `ensure` | Service refuses to leave `Initializing` until the backend reports ready; edits queued, not dropped; flush on shutdown before final compaction | Server startup ordering is a real integration risk on a dedicated build (R-007) |
| FM-9 | **Backend swap invalidates saves** | Snapshot encoding was backend-native | Payload is `int16` normalised density + game material ids — portable by construction; `valueConfig` and `backendVersion` recorded | A backend with different voxel size or grid alignment needs a resample migration. Documented, not solved |
| FM-10 | **The adapter leaks.** A plugin header in gameplay | Convenience under deadline | `TerrainCore` has no plugin dependency in `Build.cs` — compile error | The streaming component was the pressure point; §7.4 removes it |

---

## 6. Test plan

### 6.1 Headless — `TerrainCore` automation, no engine world, no plugin

Run against `FMemoryTerrainBackend`. Seconds, on every build.

| Test | Asserts |
|---|---|
| `Op.Codec.RoundTrip` | Every `FTerrainOp` serialises and deserialises byte-identically, including negative coordinates and max radius. **Defines the 58-byte encoding** |
| `Op.Quantisation.Stable` | The same world-space request quantises to the same integers across 10,000 randomised transforms |
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
| `Backend.Conformance` | A shared suite run against **both** `FMemoryTerrainBackend` and `FVPLegacyBackend`. Any future backend must pass it. **This is the operational meaning of "replaceable"** |

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
| 3 | Server validation, serialised execution, `ClientApplyOp`, subscription set | 3-client PIE convergence | K1, K4; DEF-4, DEF-5, DEF-7 |
| 4 | Journal + snapshot + compaction + boot replay | Restart identity, crash matrix, save growth | K2, K3, K5, K10; DEF-1, DEF-2, DEF-9 |
| 5 | JIP protocol, fragmentation, resync | Join-in-progress with measured bytes | DEF-3 |
| 6 | Yield pipeline + material config + inventory settlement | Volume accuracy, mixed geology | K9; DEF-6 |
| 7 | Collision-readiness policy, movement-base experiment | Standing-on-edit, spawn-into-excavation | DEF-8 |
| 8 | C++ `ITerrainDensityField` with strata and ore bodies (T-108), forwarded through the adapter generator | The test hill generates instead of being sculpted by script | R-008 |

Steps 3–7 map one-to-one onto the T-101B sub-steps. The architecture is built by running the
gate, not before it.

**Step 2 closes both flagged drift checks in standalone only.** Server-authority is not proven
until the multiplayer route is exercised at step 3. Record the narrower result at each step.

---

## 10. Backend replacement

1. Write `TerrainBackendX` implementing the ten `ITerrainBackend` methods.
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
| **DEF-4** | **A serialised execution path does not establish plugin thread or lifetime safety.** Meshing, collision, world destruction and generator access remain concurrent. An external bounds lock may conflict with one the wrapper takes internally. Requires: a thread-affinity and ownership table covering init, mutation, reads, render invalidation, callbacks and destruction; the shutdown state machine; cancellation on `EndPlay`, travel and PIE exit | 3 | **Open** — K4, E-5 |
| **DEF-5** | **Deterministic replay and operation semantics are underspecified.** Integer inputs remove one hazard but do not prove identical generator or kernel output across builds, platforms and backends. Flatten and Smooth are named without plane, strength, iteration or falloff semantics. `HashRegion` must be **position-sensitive**, or rearranged terrain hashes identically. Client results supply collision and are not cosmetic, so the client path cannot keep a nondeterministic mode the server drops. Requires: canonical semantics per operation including read bounds and rounding, version compatibility rules, golden fixtures | 3 | **Open** |
| **DEF-6** | **Yield conservation and placement policy are unspecified.** Pre-edit material does not describe placed material; Smooth both removes and places; there is no material-debit rule for Add or Paint, so add-then-mine can mint resources. Requires: separate removal and placement accounting, placement cost, smoothing recovery, material conversion, capacity overflow, fractional residue. Evidence: mixed-material boundaries, Add/Paint/Remove cycles, repeated Smooth, split-vs-unsplit equivalence | 6 | **Open** — K9 |
| **DEF-7** | **Admission validation has no commit-time revalidation, and partial failure is undefined.** Several admitted edits can pass the same remaining-resource check; a requester can move, lose permission or disconnect while queued. `bTruncated` and a boolean failure both imply possible partial mutation, which would permit unjournalled terrain. Requires: trusted request inputs, full quantised-footprint validation, request identity and retry dedup, resource reservation and revalidation, queue limits and fairness, the no-change-or-committed-result invariant, and explicit split-operation semantics | 3 | **Open** |
| **DEF-8** | **Collision safety protects only the requester.** Player B can edit beneath player A while satisfying clearance from B's own capsule — which is the design's own central multiplayer test. A machine has no capsule. `ChunkSyncComplete` establishes no collision-readiness revision, so a joining player can receive matching data while collision is absent. Requires: safety for every affected occupant, joining player and machine; distinct data, mesh and collision readiness states; movement gating into unsynchronised terrain | 7 | **Open** — E-4, E-9 |
| **DEF-9** | **The save schema loses recovery information.** Pristine deletion would discard revision history; the naive retention minimum both over-retains for cold chunks and under-retains for economic consumers; `bIsGeneratorValue` reports provenance, not value or material equality; generator mismatch cannot generally recover from a compacted journal. Requires: dependency-aware reclamation, a recoverable migration protocol, and identification of authored stamps and catalog version sufficient to reproduce the ruled deterministic base | 4 | **Partially resolved** — yield field widened to signed `int64` µL; metadata preserved on payload deletion; framing and byte order made explicit (§4.2, §4.7). Retention and generator-mismatch recovery remain open |
| **DEF-10** | **The streaming attachment was not backend-independent.** Gameplay attaching an adapter class re-creates the dependency the boundary forbids, in code or in the asset | 2 | **Resolved** — §4.3 and §7.4: `UTerrainStreamingComponent` lives in `TerrainCore`; the backend receives intent through `SetStreamingInterest`/`ClearStreamingInterest`. Ownership and teardown specified. The swap test in §10 exercises attachment, respawn and teardown |

---

## 15. Open forks

**None.** K1–K10 were ruled at CP-005 by **D-024**, as a technical ruling under D-023. The rulings
are recorded in §3 and in `DECISIONS.md`. Nothing in this section routes to the Director.

---

*ARCHITECTURE.md v1. Adopted at CP-005 by D-017. No risk in RISKS.md is closed by this document.*

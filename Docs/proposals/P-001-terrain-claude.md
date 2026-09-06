# P-001 — Server-authoritative persistent terrain architecture

**Class:** R3 (subsystem architecture) · **Author role:** Architect · **Vendor:** claude
**Written at:** CP-004, 2026-09-06 · **Basis:** AGENTS.md, VISION.md, GDD.md, DECISIONS.md
D-002/D-003/D-006/D-010/D-011/D-012/D-013/D-015/D-016/D-020/D-021, STATE.md CP-004,
RISKS.md R-001…R-010, T-101A_FINDINGS.md, plugin API extract (packet §10).

> **Status: proposal only.** Nothing here is implemented, and nothing here overturns a
> numbered decision. §12 lists the forks that need a Director ruling before the
> corresponding code is written; §11 lists what is genuinely unknown and the experiment
> that resolves each one. Where this document and the packet's §9 disagree about the
> plugin, §10 (source-level) is treated as authoritative — see §2.3.

---

## 1. Requirement

Restated from the challenge, in the terms this design will use:

The server owns a volumetric world. Players and (later) machines request edits to it. The
server decides whether each edit is legal, applies it to its own authoritative copy,
derives the material actually removed and pays it out, orders the edit globally, writes it
durably, and distributes it to the players who are near enough to care. A player joining a
world that has been dug for six months receives, for the region around them, the current
shape of the world — not the history that produced it. The renderer that draws all this is
replaceable and never owns any of it.

**Derived acceptance criteria** (each one is a T-101B pass criterion under D-013):

| # | Criterion | Risk |
|---|---|---|
| A1 | Two or three clients editing the same region converge to identical data, deterministically ordered | R-001 |
| A2 | A client joining mid-session reconstructs the modified world near it without replaying server history | R-002 |
| A3 | Server restart reproduces the world exactly; save size and time are measured, not assumed | R-003 |
| A4 | Yield is computed from material actually removed, on the server, deterministically | R-004 |
| A5 | No gameplay code includes a plugin header; the service runs against a non-plugin backend in a headless test | D-011 |
| A6 | A player standing on terrain that another player edits is not broken by it | R-010 |

---

## 2. Constraints

### 2.1 Ruled constraints (not negotiable here)

- **D-002 / AGENTS §4** — dedicated-server model, server authority, never trust the client.
- **D-003** — 16–32 players, stock UE replication, aggressive distance relevancy. No custom netcode.
- **D-006** — C++ owns simulation, networking, data. Blueprint is a thin child.
- **D-010** — the backend is *provisional*; the requirement is durable.
- **D-011** — gameplay owns edit ops, material semantics, permissions, revisions, yield.
  The plugin is never the economic authority; plugin types stay inside the adapter module.
- **D-012** — deterministic base + per-modified-chunk snapshot at revision R + append-only
  op journal + compaction; SQLite for entities; versioned inspectable formats.
- **D-013** — concurrency, restart and JIP are *gate* criteria, not Phase 3 work.
- **AGENTS §4** — terrain edits replicate as **operations**, never meshes. Distance relevancy on everything.
- **AGENTS §10** — stop at ambiguity rather than invent. §11 and §12 below are that stop.

### 2.2 Situational constraints

- **There is no C++ module in this project yet** (STATE, CP-004). The first deliverable of
  this architecture is also the first compile the project has ever done. VS 2022 is
  installed and *never exercised*. Treat "the project builds from source" as its own
  one-evening task with its own failure mode, not as a precondition that already holds.
- **R-009 / the 7-day rule.** The largest observed failure mode of this project is
  stalling, not wrong design. Every part of §9 is scoped so that a single evening ends
  with something that compiles and can be shown to work.
- **The T-101A dig Blueprint is a client-authoritative direct plugin call** and is the sole
  cause of both failed drift checks (STATE, CP-004). Step 2 of §9 deletes it. That is the
  earliest point at which the drift checks can pass, so it is scheduled second, not last.

### 2.3 Backend constraints, from measurement and source

Everything in this table is load-bearing for the design. Source column: §9 =
T-101A_FINDINGS, §10 = API extract.

| Fact | Source | Consequence for this design |
|---|---|---|
| `DATA_CHUNK_SIZE = 16`, `RENDER_CHUNK_SIZE = 32` | §10.2 | Our authority chunk must be an axis-aligned multiple of 16 voxels, so a chunk maps onto whole data leaves |
| `FVoxelValue` is `int16` normalised density (`EIGHT_BITS_VOXEL_VALUE = 0`) | §10.2, 10.3 | The portable save encoding can be a straight `int16` — no lossy conversion needed for the current config |
| The value config flag is *recorded in the save format* (`EVoxelValueConfigFlag`, `FVoxelSaveVersion::ValueConfigFlagAndSaveGUIDs`) | §10.2 | Our own snapshot header must record it too, or an 8-bit rebuild silently corrupts old saves |
| `FModifiedVoxelValue` = `{Position, OldValue, NewValue}` and **carries no material** | §10.3 | Yield needs a *separate* material read over the edited bounds. This is the single biggest cost in the edit path |
| `UVoxelDataTools::GetVoxelsValueAndMaterial`, `CacheMaterials` exist; `FVoxelDataAccelerator` gives a caching cursor with a `bIsGeneratorValue` out-param | §10.9, 10.7 | Bulk region read is available, and we can tell stored data from generated data — which is what makes a diff-vs-generator snapshot possible |
| `FVoxelData::Set<T>` / `ParallelSet<T>` over an `FVoxelIntBox`, and `Lock(EVoxelLockType, Bounds, Name)` | §10.7 | Bulk region **write** and bounds-scoped locking exist in C++ (not in Blueprint). Snapshot restore does not need the plugin's save format |
| Every `UVoxelDataTools` save entry point takes a world and **no bounds**; `FVoxelUncompressedWorldSaveImpl`'s chunk-addressed layout is private with `friend FVoxelSaveBuilder/Loader` | §10.10, 10.12 | **The plugin's save format is unusable as our per-chunk store.** We own the chunk snapshot codec. The plugin blob survives only as a correctness oracle (§8.2) |
| `FVoxelWorldCreateInfo::bOverrideData` can create a world on an existing `FVoxelData` | §10.4 | Restore-at-boot has a second path: build data first, then create the world on it. Useful if `ParallelSet` before `CreateWorld` proves awkward |
| `CheckIfSameAsGenerator`, `RoundToGenerator` | §10.10 | Compaction can *delete* chunks the players have reverted to natural shape — save size can go down, not only up |
| Voxel Graphs are Pro-gated and **silent** | §9 2b, §10.14 | The generator is a C++ `UVoxelGenerator` (T-108) and therefore ours, which this design exploits (§4.6) |
| The plugin's TCP multiplayer is **not an implementation** — both entry points are `FVoxelMessages::Info` + `return false`, and `bEnableMultiplayer` logs a Pro message at world creation | §10.12, 10.14 | There is no reference code to read, contrary to §9's reading. `FVoxelDataOctreeLeafMultiplayer` (per-leaf dirty tracking) and `FVoxelDiff` are type declarations with no consumer. **Do not set `bEnableMultiplayer`.** Everything replication-shaped is ours |
| No plugin function is annotated for network authority; nothing distinguishes an authoritative edit from a local one | §10.17 | Authority is entirely a property of *our* call sites. There is no safety net in the plugin |
| Plugin refuses camera-as-invoker outside standalone; `VoxelProceduralMeshComponent` is `NOT Supported` by `FNetGUIDCache` | §9 2d, R-010 | An invoker is an entry cost on both client *and server*; the movement-base problem needs a deliberate architectural answer (§7.4) |
| Editing near your own feet drops the player through the floor | §9 2f | Belongs in the **validation** layer, not in tuning (§4.4) |
| Thread-safety guarantees are not documented anywhere in the plugin headers | §10.17 | The concurrency model must be one that is safe *without* relying on undocumented guarantees (§4.5) |

---

## 3. Design forks — options and tradeoffs

Six forks matter. Each is presented as the challenge requires: options, tradeoffs, one
recommendation. Forks marked **⚑** are named in AGENTS §10 as escalation triggers and
therefore need an explicit Director ruling before implementation (§12).

### F1 ⚑ — Revision model: global sequence vs per-chunk revision

| Option | Tradeoffs |
|---|---|
| **A. Global op sequence only.** One monotonic `OpSeq`; JIP means "everything since OpSeq N". | Simplest. But a client joining near one quarry must be told about every op in the world since its snapshot, or the server must scan the whole journal to filter. Relevancy becomes a post-filter over global history — exactly the "replay the server's lifetime" failure D-012 forbids. |
| **B. Per-chunk revision only.** Each chunk has `Rev`, incremented on each touching op. | JIP per chunk is trivial. But an op spanning four chunks has no single identity, so cross-chunk ordering between two overlapping ops is undefined, and the "never observe a half-applied operation" rule in ARCHITECTURE v0 §2 has nothing to hang on. |
| **C. Both.** Server assigns a global `OpSeq` at commit; each affected chunk's `Rev` increments and records the `OpSeq` that caused it. | Two counters to keep consistent, and the journal record must carry the affected-chunk list. In exchange: global ordering for transactions and determinism, per-chunk addressing for relevancy and JIP, and a cheap gap-detection invariant on the client. |

**Recommend C.** They answer different questions and neither alone answers both. The
cost is one extra `uint32` per chunk and an affected-chunk list per journal record — tens
of bytes. B alone cannot express a multi-chunk op; A alone cannot express relevancy.

### F2 — Authority chunk size

Must be a multiple of `DATA_CHUNK_SIZE` (16). At `VoxelSize = 50 cm`:

| Option | Metres | Voxels/chunk | Dense snapshot (2 B value + 2 B material) | Character |
|---|---|---|---|---|
| 16³ | 8 m | 4,096 | 16 KB | Fine revision granularity, many keys, more per-chunk overhead in journal and subscription sets |
| **32³** | 16 m | 32,768 | 128 KB | Aligns to `RENDER_CHUNK_SIZE` as well as the data leaf; a typical dug tunnel touches 1–3 chunks |
| 64³ | 32 m | 262,144 | 1 MB | Few keys, but one pickaxe swing dirties a megabyte of snapshot and drags a huge JIP payload |

**Recommend 32³ voxels.** It aligns with both plugin constants, and the dense figure is a
worst case that §4.7's sparse encoding should reduce by one to two orders of magnitude for
realistic chunks. **This is a measurement, not a belief** — E-3 in §11 decides whether it
holds, and the chunk size is a single constant if it does not.

### F3 — What a chunk snapshot stores

| Option | Tradeoffs |
|---|---|
| **A. Dense values + materials.** | Simple, backend-agnostic, restore is one bulk write. 128 KB/chunk before compression; a 100-chunk quarry is 12.8 MB raw. |
| **B. Sparse diff vs generator** — store only voxels where the stored value differs from what the generator produces, using `FVoxelDataAccelerator`'s `bIsGeneratorValue`. | A tunnel through a hill touches a small fraction of its chunk's volume, so this should be dramatically smaller. Cost: restore requires the generator to be byte-reproducible, so `GeneratorVersion` becomes part of the save contract, and a generator change means a migration (or a forced compaction to dense before the change lands). |
| **C. Ops only, no snapshot.** | Smallest file. But JIP and boot both become unbounded replay — precisely what D-012 rules out. Rejected. |

**Recommend B, with A as the fallback encoding selected per-chunk by a byte-size
comparison at write time.** The snapshot header names its encoding, so both can coexist
and the fallback is not a rewrite. Also flag one edge B handles well: `CheckIfSameAsGenerator`
lets compaction *delete* a chunk file entirely when players have filled a hole back in.

### F4 — Where the authoritative edit executes

| Option | Tradeoffs |
|---|---|
| **A. Game thread, synchronous.** | Trivially deterministic and ordered. But a 500k-voxel op (measured: r=3000 sculpt sphere = 514,627 voxels, §9 2c) stalls the server frame. |
| **B. Single dedicated terrain thread with an ordered queue.** | Still fully serialised, therefore still trivially deterministic and free of the concurrent-edit divergence risk. Ops never touch the game thread except to enqueue and to receive the completed result. One thread means edit throughput is bounded — acceptable at 16–32 players with per-source rate limits. |
| **C. Parallel workers with `FVoxelData::Lock` on disjoint bounds.** | Highest throughput. Requires trusting thread-safety guarantees the plugin headers do not document (§10.17), and makes ordering between overlapping ops a real problem rather than a non-problem. |

**Recommend B.** It removes R-001 as a correctness question and reduces it to a throughput
question, which is measurable. C stays available later — the interface does not change,
only the dispatcher — and should not be attempted before E-5 (§11) documents actual
concurrency behaviour.

### F5 — Durability point (when is an edit "real")

| Option | Tradeoffs |
|---|---|
| **A. fsync before ack.** | Zero edits lost on crash. Adds disk latency to every swing; on a cheap disk this is visibly bad. |
| **B. Group commit — buffer, flush + fsync on 250 ms or 1 MB, ack after flush.** | Bounded loss window of one flush interval; adds up to 250 ms to the dig round trip. |
| **C. Apply and ack immediately; flush journal asynchronously.** | Fastest. On a crash, up to one flush interval of edits are lost *and* their yield may already be in inventory — a divergence between two stores. |

**Recommend C plus a single-ordering-authority rule that removes its divergence:** the
journal record carries the yield and the recipient, and the inventory row carries
`LastAppliedOpSeq`. On boot, any journalled op newer than the inventory's watermark is
re-credited; any op lost from the tail was never credited either, because the credit is
written from the same record. The failure becomes "the last few seconds of digging did not
happen," which is coherent and acceptable on a friends server, rather than "the hole is
there but the ore is not." **This is a Director ruling** (§12, F5) because it trades a
guarantee for feel and only the Director owns that trade.

### F6 — How clients get terrain state

| Option | Tradeoffs |
|---|---|
| **A. Server streams voxel data continuously.** | Violates AGENTS §4 ("operations, never meshes" — and never raw data as a steady state). Rejected. |
| **B. Client runs the same generator and applies ops; snapshots only on subscribe.** | Bandwidth is ~50 bytes per edit. Requires the client's generator to be identical to the server's, and requires ops to be deterministic client-side (§4.3). |

**Recommend B**, which is what D-012 and AGENTS §4 already imply. Named as a fork only
because it imposes a hard requirement worth stating out loud: **op parameters go on the
wire in integer voxel space, never in world-space floats**, or client and server round
differently and diverge silently. See §4.3.

---

## 4. Recommended design

### 4.1 Modules and boundaries

Three modules. The boundary is a compile-time one: `TerrainCore` does not list the plugin
in its `Build.cs`, so a plugin include in gameplay code is a **build error**, not a code
review finding. That is the enforcement mechanism for D-011 and for the AGENTS §9 drift
guard, and it is worth more than any convention.

```text
VoxelWorld            (primary game module — gameplay, characters, tools, UI hooks)
   |  depends on
   v
TerrainCore           (game-owned. No plugin dependency. Compiles headless.)
   - UTerrainService (authority, validation, sequencing, revisions, yield)
   - ITerrainBackend (pure interface, game types only)
   - ITerrainDensityField (the world's shape, as pure C++)
   - FTerrainJournal, FTerrainSnapshotStore, FTerrainRevisionIndex
   - UTerrainStreamComponent (per-connection relevancy + transport)
   - FMemoryTerrainBackend (dense test backend — no renderer, no plugin)
   ^  implemented by
   |
TerrainBackendVPLegacy (the ONLY module that includes plugin headers)
   - FVPLegacyBackend : ITerrainBackend
   - UVPLegacyDensityGenerator : UVoxelGenerator  (forwards to ITerrainDensityField)
   - UTerrainStreamingProxyComponent (owns the plugin invoker; see §7.4)
```

`VoxelWorld` depends on `TerrainCore` only. `TerrainBackendVPLegacy` is loaded and
registered at startup by name from config (`[/Script/TerrainCore.TerrainSettings]
BackendModule=TerrainBackendVPLegacy`), so swapping backends is a config line plus a new
module — which is the concrete answer to "backend replacement" (§10).

> Module names deliberately avoid "Voxel" per D-015 — the codename is not the identity, and
> these are the class names the game will carry for years.

### 4.2 Data structures

All in `TerrainCore`. No plugin type appears in any of them.

```cpp
// ---- identity ----------------------------------------------------------
using FTerrainOpSeq   = uint64;   // global, monotonic, server-assigned
using FTerrainRev     = uint32;   // per chunk, monotonic
using FTerrainMatId   = uint16;   // GAME material id. Never a plugin index.

struct FTerrainChunkKey { int32 X, Y, Z; };   // chunk-space, 32 voxels per unit

// ---- the operation (this is the wire format AND the journal record body) --
enum class ETerrainOpKind : uint8 { Remove, Add, Flatten, Smooth, Paint };
enum class ETerrainShape  : uint8 { Sphere, Box };
enum class ETerrainSource : uint8 { Player, Machine, Admin, Worldgen };

struct FTerrainOp
{
    FTerrainOpSeq   OpSeq        = 0;
    uint64          TransactionId= 0;   // equal across sub-ops of one split edit
    ETerrainOpKind  Kind         = ETerrainOpKind::Remove;
    ETerrainShape   Shape        = ETerrainShape::Sphere;
    ETerrainSource  Source       = ETerrainSource::Player;
    uint32          SourceId     = 0;   // PlayerId or MachineId
    uint32          ToolId       = 0;   // tool/tier — affects yield, not geometry
    FIntVector      CentreVox    = {};  // VOXEL SPACE. Integer. See §4.3.
    int32           RadiusVoxQ16 = 0;   // voxels, 16.16 fixed point
    FIntVector      ExtentVox    = {};  // box ops only
    FTerrainMatId   MaterialId   = 0;   // Add / Paint only
    uint8           Flags        = 0;
};                                       // 64 bytes packed; ~48 on the wire

// ---- what the backend gives back ---------------------------------------
struct FTerrainMaterialVolume { FTerrainMatId MaterialId; double CubicMetres; };

struct FTerrainEditResult
{
    FTerrainBox                     EditedBounds;   // voxel space, game type
    TArray<FTerrainChunkKey>        AffectedChunks;
    TArray<FTerrainMaterialVolume>  Removed;        // + = removed, - = placed
    int64                           VoxelsTouched = 0;
    bool                            bTruncated    = false;  // hit the per-op cap
};

// ---- region transfer (snapshots and JIP) --------------------------------
enum class ETerrainRegionEncoding : uint8 { Dense, SparseDiff, Empty };

struct FTerrainRegionData
{
    FTerrainChunkKey        Key;
    FTerrainRev             Rev = 0;
    FTerrainOpSeq           LastOpSeq = 0;
    ETerrainRegionEncoding  Encoding = ETerrainRegionEncoding::Empty;
    uint32                  GeneratorVersion = 0;
    uint8                   ValueConfig = 0;    // mirrors EVoxelValueConfigFlag
    TArray<uint8>           Payload;            // see §4.7
};
```

**Deliberate omission:** `FTerrainEditResult` does **not** carry a per-voxel delta array.
The plugin's `TArray<FModifiedVoxelValue>` (20 bytes/voxel; 10 MB for the measured 514k-voxel
sculpt sphere) stays inside the adapter, is consumed there to produce `Removed`, and is
freed. Only the aggregate crosses the module boundary. This is the difference between a
boundary that is cheap and one that exists on paper.

### 4.3 The backend interface

```cpp
class ITerrainBackend
{
public:
    virtual ~ITerrainBackend() = default;

    virtual bool Initialize(const FTerrainBackendInit& Init) = 0;   // seed, gen version,
                                                                    // voxel size, bounds,
                                                                    // density field, role
    virtual void Shutdown() = 0;

    // Authoritative mutation. Server: full result. Client: result ignored.
    virtual bool ApplyOp(const FTerrainOp& Op, FTerrainEditResult& Out) = 0;

    // Region transfer — snapshot capture and restore.
    virtual bool ReadRegion (const FTerrainChunkKey& Key, FTerrainRegionData& Out) = 0;
    virtual bool WriteRegion(const FTerrainRegionData& In) = 0;

    // Convergence oracle. Order-independent hash of a region's values+materials.
    virtual uint64 HashRegion(const FTerrainChunkKey& Key) const = 0;

    virtual bool IsRegionResident(const FTerrainChunkKey& Key) const = 0;
    virtual void FlushPendingWork() = 0;      // for tests and for shutdown
};
```

Seven methods. Everything the game does to terrain goes through them, which is what makes
`FMemoryTerrainBackend` (a dense `TMap<FTerrainChunkKey, TArray<int16>>`) a complete stand-in
for the plugin in tests — and therefore what makes A5 achievable on day one rather than
aspirationally.

**Why the wire op is integer voxel space.** `UVoxelSphereTools` takes an `FVector` world
position and converts with `bConvertToVoxelSpace = true`. If the server broadcast the world
position, server and client would each perform that float conversion independently, against
possibly differing actor transforms, and round differently at voxel boundaries. The
divergence would be one voxel wide, invisible for a week, and then a client would see a wall
where the server sees air. So: **the server quantises once, at validation time, and the
quantised integers are the op** — for the wire, for the journal, and for its own application.
The adapter calls the plugin with `bConvertToVoxelSpace = false`. This single rule removes
an entire class of convergence bug and costs nothing.

Two more adapter rules, both from §10.9:
- `bRecordModifiedValues` is **always true** on the authoritative path (yield depends on it).
- The C++ overload **appends** to `OutModifiedValues`. The adapter's scratch array is reset
  per op, explicitly. A missed reset is a yield-inflation bug that grows over a session.

### 4.4 The service, and the server sequence

`UTerrainService` is a `UWorldSubsystem` (not an actor: it has no transform, no relevancy of
its own, and must exist before any pawn). It exists on both server and client; `HasAuthority`
gates the authoritative half.

```text
CLIENT                         SERVER (game thread)          TERRAIN THREAD
------                         --------------------          --------------
input, camera trace
  |
  |  ServerRequestEdit(Req)     validate  ────────────────┐
  |  [reliable, on the           - reach (camera->target)  │  reject → ClientEditRejected
  |   PlayerController's         - radius <= tool max      │  (compact: ReqId + reason)
  |   TerrainStreamComponent]    - rate limit per source   │
                                 - tool/power/durability   │
                                 - zone permission (D-004) │
                                 - NOT within SelfClearance│  ← §9 2f lives here
                                   of the requester capsule│
                                 - target chunks resident  │
                                 - quantise to voxel space │
                                        |                  │
                                 enqueue FTerrainOp ───────┴──────►  serialised queue
                                                                      |
                                                                      | bulk-read materials
                                                                      |   over predicted bounds
                                                                      | backend->ApplyOp()
                                                                      | accumulate per-material
                                                                      |   removed volume
                                                                      |◄─ FTerrainEditResult
                                        ┌─────────────────────────────┘
                                 assign OpSeq (monotonic)
                                 bump Rev for each affected chunk
                                 credit yield to inventory (LastAppliedOpSeq)
                                 append journal record (async flush, F5)
                                 for each subscriber of any affected chunk:
                                     ClientApplyOp(Op, PerChunkRev[])
  |◄─────────────────────────────────────┘
apply Op locally via the same
adapter, same integers
update per-chunk Rev; if a Rev
arrives non-contiguous → request
resync for that chunk
```

Notes on the sequence:

- **OpSeq is assigned at commit, not at request.** A rejected or truncated request never
  consumes a sequence number, so the journal has no holes and "replay everything after N"
  is unambiguous.
- **Multi-chunk ops are one op.** The op is sequenced once, applied once, and broadcast once,
  with the full affected-chunk list. There is no cross-chunk transaction protocol because
  there is no cross-chunk parallelism (F4-B). ARCHITECTURE v0 §2's "never observe a
  half-applied operation" is satisfied structurally rather than by a mechanism.
- **Very large ops are split by the server into sub-ops sharing a `TransactionId`,** each
  under `MaxVoxelsPerOp`. Stage 4 excavation (D-016) is the reason this exists now rather
  than later; a bulldozer blade is a box op that will exceed any sane single-op budget.
- **Prediction is deferred** per ARCHITECTURE v0 §2. The client shows the change on
  `ClientApplyOp`. If latency proves intolerable (E-7), client-side prediction with
  server-reconciliation-by-resync is an additive change to this design, not a replacement:
  the client already has an apply path and a per-chunk resync path.
- **Rejection is silent-ish by design.** `ClientEditRejected` carries a request id and a
  small enum, for UI feedback only. It never carries world state.

### 4.5 Concurrency

- One terrain thread, one queue, FIFO. Ordering is total and identical to `OpSeq` order.
- The backend's `ApplyOp` may internally use `bMultiThreaded = true` (the plugin's parallel
  edit), because per-voxel writes within one op are independent — **but this is an assumption
  and E-2 tests it** (the sync and async plugin overloads even disagree on the default, §10.9).
  If E-2 shows any nondeterminism, the authoritative path drops to `bMultiThreaded = false`
  and only the client's cosmetic apply keeps it.
- The game thread never touches `FVoxelData`. All plugin data access is on the terrain
  thread, via `FVoxelData::Lock` with the op's bounds, which is the only concurrency
  discipline the plugin documents at all.
- **Rendering and collision updates remain the plugin's own async work** and are explicitly
  *not* serialised by us. This is the seam where §9 2f (fall through the floor) lives:
  the data edit completes before the collision cook does. The architecture's answer is the
  `SelfClearance` validation rule above plus §7.4, not an attempt to make the plugin atomic.

### 4.6 The generator, and why it belongs in the core

Voxel Graphs are Pro-gated (§9 2b), so the generator must be C++ (T-108). That is usually
described as a cost. It is an opportunity here:

```cpp
// TerrainCore — no plugin dependency
class ITerrainDensityField
{
public:
    virtual float    Density  (double X, double Y, double Z) const = 0;  // <0 solid
    virtual FTerrainMatId Material(double X, double Y, double Z) const = 0;
    virtual uint32   Version() const = 0;    // bumped on ANY change to the field
};
```

`UVPLegacyDensityGenerator : UVoxelGenerator` (adapter) implements `GetValues` /
`GetMaterials` by forwarding `TVoxelQueryZone` iteration to the field. The world's shape —
strata, ore bodies, the authored 256–512 m hill — is then plain C++ that unit-tests without
an engine, is byte-identical on server and client by construction, and survives a backend
swap untouched. It also makes `Version()` a first-class save-schema input, which F3-B
requires.

`FMemoryTerrainBackend` uses the same field, so the headless tests exercise the real world
shape, not a flat plane.

### 4.7 Persistence schema

```text
Saved/World/<WorldId>/
  world.json                     # human-readable, versioned, small
  chunks/<X>_<Y>_<Z>.chunk       # snapshot at revision R  (binary, versioned)
  journal/<NNNNNN>.tjl           # append-only op log, size-segmented
  entities.sqlite                # players, inventories, structures, machines (D-012)
  Tests/Saves/…                  # old-save fixtures live in the repo, per AGENTS §4
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
  "latestOpSeq": 918273,
  "journalBaseOpSeq": 900000,
  "createdUtc": "…", "updatedUtc": "…"
}
```

**Chunk snapshot** (`.chunk`) — header then payload:

| Field | Bytes | Notes |
|---|---|---|
| magic `TCHK` | 4 | |
| schemaVersion | 4 | migration entry point |
| chunkKey X,Y,Z | 12 | |
| rev | 4 | |
| lastOpSeq | 8 | ops with `OpSeq <= lastOpSeq` are baked in |
| generatorVersion | 4 | F3-B correctness gate |
| valueConfig, materialConfig, encoding, reserved | 4 | |
| payloadBytes, payloadCrc32 | 8 | torn-write detection |
| payload | n | Dense: `int16[N]` values then `uint16[N]` materials. SparseDiff: `uint32 count`, then `count ×` `{uint16 localIndex-hi/lo, int16 value, uint16 material}`, all relative to generator output. Empty: zero bytes. |

**Journal record** (`.tjl`) — fixed prefix, variable chunk list:

| Field | Bytes |
|---|---|
| magic `TJOP`, schemaVersion | 8 |
| `FTerrainOp` body (§4.2) | 64 |
| serverUtcMillis | 8 |
| yield entry count, then `{matId u16, milliLitres u32}` × count | 2 + 6n |
| affected chunk count, then `{key 12 B, newRev 4 B}` × count | 2 + 16n |
| crc32 | 4 |

Typical record with one material and two chunks: **~124 bytes per edit**. A million-edit
server-year is ~124 MB of journal before compaction — and compaction is what keeps it from
being that.

**Compaction.** Per chunk, when (`opsSinceSnapshot > 256`) OR (`journalBytesForChunk > 64 KB`)
OR (chunk idle > 5 min) OR (shutdown): re-`ReadRegion` the chunk, write a new `.chunk` at the
current rev, and update its `lastOpSeq`. A journal segment is deletable once
`segment.maxOpSeq <= min(lastOpSeq)` across all chunk files. If `CheckIfSameAsGenerator`
reports a chunk is back to natural shape, delete the `.chunk` file entirely and let it fall
back to the generator — **the save can shrink**, which matters over months.

**Migration.** Every format above carries `schemaVersion` in its own header. A load of
version < current runs a registered `FTerrainMigration_N_to_M` chain, in place, with a
`.bak` written first. Old-save fixtures in `Tests/Saves/` are loaded by an automation test
on every build, which is what turns "we have a migration path" from a claim into a check.

### 4.8 Chunk relevancy and join-in-progress

Each `APlayerController` gets a `UTerrainStreamComponent` (owner-only). The server maintains
per-connection:

```cpp
TSet<FTerrainChunkKey> Subscribed;               // acked and live
TMap<FTerrainChunkKey, FTerrainRev> ClientRev;   // last rev we know they have
TQueue<FTerrainChunkKey> PendingSync;            // subscribe backlog
```

Subscription is recomputed every 500 ms from the pawn position, with hysteresis: subscribe
inside `SubscribeRadius`, unsubscribe outside `SubscribeRadius × 1.5`. A player walking a
boundary must not thrash a snapshot stream.

**JIP / new subscription, per chunk:**

1. If no `.chunk` file and no journal ops for the key → send `ChunkIsPristine(Key)`. The
   client's generator already produces the right thing. **This is the common case and it
   costs 12 bytes.**
2. Else send the snapshot at rev R (fragmented, §7.3), then every journal op for that chunk
   with `OpSeq > snapshot.lastOpSeq`, then `ChunkSyncComplete(Key, Rev)`.
3. Ops committed *during* the sync are queued per-chunk and flushed after step 2, so the
   client never applies an op ahead of the snapshot it belongs to.
4. The client applies, compares its resulting `Rev` with the server's, and requests a full
   resync for that chunk on mismatch. **Resync is the universal repair primitive** — for
   gaps, for missed ops, for a client that was suspended, and for any bug we have not found
   yet. It is deliberately blunt and always correct.

This is the whole of A2: a joining client receives, per nearby chunk, one snapshot and a
bounded tail. Never the server's history.

### 4.9 Yield

Server-only, in the adapter, before the aggregate crosses the boundary:

1. Compute the op's bounds from its integer parameters (radius + 1 voxel margin).
2. **Bulk-read materials over those bounds once** (`GetVoxelsValueAndMaterial` /
   `FVoxelDataAccelerator`) into a scratch buffer. Not per-voxel `GetMaterial` calls —
   that is a lock acquisition per voxel and would dominate the frame.
3. `ApplyOp`.
4. For each `FModifiedVoxelValue`: `Δocc = Occ(OldValue) − Occ(NewValue)`, look the material
   up in the scratch buffer by position, accumulate `Δocc × VoxelVolume` per material id.
5. Apply tool efficiency and recovery factor (game data, `ToolId`), producing the final
   `Removed` list. Machines later substitute a different efficiency curve; nothing else changes.

`Occ()` is a monotone map from normalised signed density to occupancy in [0,1]. Its exact
form — and whether `int16` density is linear enough in the transition band for volume to be
accurate — is **unknown; E-1 in §11 is the experiment.** The calibration test is
unambiguous: remove a sphere of radius R in homogeneous material and compare
`Σ Δocc × VoxelVolume` against `4/3 π R³`.

**Material encoding.** Game `FTerrainMatId` must map to a per-voxel plugin material.
`MaterialConfig` is currently `RGB` (colour), which has no clean id channel.
**Recommend `SingleIndex`** (`uint8` index, §10.15) with a game-owned id↔index table in the
adapter, capping the game at 255 simultaneous terrain materials — far beyond any Year 1 need
— and giving an exact, lossless per-voxel material read. This is a visible change: it means
terrain rendering goes through a `UVoxelInstancedMaterialCollection` rather than vertex
colours, which is material work the GDD's Art Direction section already says must carry the
look. **Director ruling required** (§12, F7) because it touches how the game looks, not only
how it computes.

---

## 5. Failure modes

Ordered by how much they would hurt, not by likelihood.

| # | Failure | Mechanism | Design response | Residual |
|---|---|---|---|---|
| FM-1 | **Silent client/server divergence.** A client sees a wall the server calls air. | Float rounding at voxel boundaries; a dropped op; a plugin nondeterminism | Integer-voxel-space ops (§4.3); per-chunk rev gap detection; `HashRegion` compared in the convergence test; blunt per-chunk resync | Undetected divergence *inside* a chunk with matching revs — E-2 and the hash test exist for this |
| FM-2 | **Save corruption or loss.** The world is gone. Per D-012, this is the unacceptable one. | Torn write on crash; a bad migration; a generator change invalidating SparseDiff snapshots | CRC per record and per snapshot; torn tail detected and truncated; `.bak` before migration; `generatorVersion` in every snapshot header, load refuses on mismatch and forces regeneration from journal | A generator change with no migration path forces a dense recompaction pass before it lands — this must be a release-checklist item, not a hope |
| FM-3 | **Yield/terrain divergence.** Ore in the bag, no hole — or the reverse. | Crash between apply and credit; two stores, two orderings | Single ordering authority: yield lives in the journal record; inventory carries `LastAppliedOpSeq`; boot re-credits the gap (F5) | A lost journal tail loses both, coherently |
| FM-4 | **JIP burst saturates the connection.** A player joins next to the megaquarry and everyone rubber-bands. | Snapshot stream competing with movement on the same reliable channel | Per-connection byte budget (§7.3); pristine-chunk fast path; nearest-first ordering of `PendingSync` | Unmeasured until E-3/E-6. This is the most likely thing to be wrong |
| FM-5 | **Player falls through the world after editing.** | Collision cook lags the data edit (§9 2f) | `SelfClearance` validation rule rejects edits within R of the requester's own capsule; KillZ + respawn as the net; E-4 tests whether a collision-completion signal exists | If the true window is larger than the clearance radius, this needs the plugin-level answer E-4 looks for |
| FM-6 | **Movement corrections discarded while standing on terrain.** | `VoxelProceduralMeshComponent` has no net GUID (§9 2d, R-010) | §7.4 — try `Mobility = Static` on the proc-mesh so it is never used as a *relative* base | Genuinely unknown. If no configuration fixes it, this is a backend-adoption argument, and it belongs in the T-101B verdict, not in this design |
| FM-7 | **One player DoSes the terrain thread.** | A scripted client spamming max-radius ops | Per-source rate limit and `MaxVoxelsPerOp` in validation, before the queue; queue depth cap with oldest-request rejection | Friends server, low priority — but the check costs nothing and D-002 says never trust the client |
| FM-8 | **The plugin's async work never completes / world not created.** | `bCreateWorldAutomatically` default false; task starvation; the known `DestroyWorldInternal` ensure | Service refuses to leave `Initializing` until the backend reports ready; edits queued, not dropped, while not ready; `FlushPendingWork` on shutdown before the final compaction | Server startup ordering is a real integration risk on a dedicated build (R-007) |
| FM-9 | **Backend swap invalidates saves.** | Snapshot encoding was backend-native | Snapshot payload is `int16` normalised density + game material ids — portable by construction; `valueConfig` recorded so an 8-bit rebuild is detected, not silently misread | A backend with different voxel size or grid alignment needs a resample migration. Documented, not solved |
| FM-10 | **The adapter leaks.** Someone includes a plugin header in gameplay. | Convenience under deadline | `TerrainCore` has no plugin dependency in `Build.cs` — it is a compile error | The `UVoxelInvokerComponent` on the character is the one real pressure point; §7.4 handles it |

---

## 6. Test plan

Three tiers. The first tier is the one that makes this architecture worth its cost.

### 6.1 Headless — `TerrainCore` automation tests, no engine world, no plugin

Run against `FMemoryTerrainBackend`. These run on every build in seconds.

| Test | Asserts |
|---|---|
| `Op.Codec.RoundTrip` | Every `FTerrainOp` serialises and deserialises byte-identically, incl. negative coords and max radius |
| `Op.Quantisation.Stable` | The same world-space request quantises to the same integers across 10,000 randomised transforms |
| `Journal.RoundTrip` | Write N records, reopen, read N records identical |
| `Journal.TornTail` | Truncate the file mid-record → loader recovers N−1 records and reports the truncation |
| `Journal.BadCrc` | A flipped byte is detected, not loaded |
| `Snapshot.Codec.Dense/Sparse/Empty` | Round-trip for all three encodings; sparse-vs-dense produce identical regions |
| `Snapshot.EncodingChoice` | The smaller encoding is selected; both decode |
| `Revision.Monotonic` | Chunk revs never decrease; a multi-chunk op bumps every affected chunk exactly once |
| `Replay.Equivalence` | `snapshot@R + ops after R` == `apply all ops from base`. **This is the central persistence invariant** |
| `Compaction.Equivalence` | Region hash before compaction == after |
| `Compaction.Reverted` | A chunk restored to natural shape is deleted and reloads identically from the generator |
| `Yield.Accumulation` | With a known synthetic density field, removing a known volume yields the expected per-material totals (backend-independent half of A4) |
| `Migration.Fixtures` | Every fixture in `Tests/Saves/` loads and produces its expected region hash |
| `Backend.Conformance` | A shared suite run against **both** `FMemoryTerrainBackend` and `FVPLegacyBackend`. Any future backend must pass it. This is the operational meaning of "replaceable" |

### 6.2 In-engine, single process

| Test | Asserts | Risk |
|---|---|---|
| `Adapter.ApplyOp.Matches` | Backend conformance suite against the real plugin | D-011 |
| `Adapter.Determinism` | The same op sequence from the same seed produces the same `HashRegion`, over 20 runs, and across `bMultiThreaded` true/false | R-001 |
| `Yield.Volume` | Remove r = 2 m in homogeneous stone; `ΣΔocc × V` within tolerance of `4/3πr³` | R-004 |
| `Restart.Identity` | Dig, shut down, boot, compare every chunk hash | R-003 |
| `Save.Growth` | 1,000 scripted edits; record bytes/edit, snapshot size after compaction, compaction wall time | R-003 |

### 6.3 Multiplayer PIE / standalone — the T-101B gate proper

Requires the invoker (§7.4) as an entry cost, and PIE at the 3-client settings (D-021 keeps
those; solo work stays standalone).

| Test | Asserts | Criterion |
|---|---|---|
| `MP.Convergence` | 3 clients dig the same 5 m region for 60 s; final `HashRegion` identical on server and all clients | A1 / R-001 |
| `MP.JoinInProgress` | Server dug for 10 min; fresh client joins; nearby chunk hashes match server; measure bytes and time to `ChunkSyncComplete` | A2 / R-002 |
| `MP.StandingOnEdit` | Client A stands on terrain; client B removes it beneath them; A falls correctly and is never desynced or teleported | A6 / R-010 |
| `MP.SelfClearance` | Digging at the clearance limit never drops the digger through the floor | FM-5 / R-010 |
| `MP.Relevancy` | A client 500 m away receives zero ops for the dig site; verified by op counter, not by eye | AGENTS §4 |
| `MP.Resync` | Force-drop an op to one client; the rev gap is detected and the chunk repairs itself | FM-1 |
| `MP.Bandwidth` | 8 clients digging continuously; per-connection bytes/s for ops and for snapshots | FM-4 |

---

## 7. Performance risks and budgets

Budgets are proposed as *starting* numbers to measure against, not as claims.

### 7.1 Per-op server cost

The dominant cost is the material bulk-read over the op bounds, not the edit. A hand tool at
r = 200 uu with 50 cm voxels is a 4-voxel radius — roughly 270 voxels, trivial. The measured
sculpting sphere at r = 3000 was **514,627 voxels** (§9 2c) and its `ModifiedValues` array
alone is ~10 MB.

- **`MaxVoxelsPerOp = 65,536`** (≈1.3 MB of `ModifiedValues`, ≈ r = 25 voxels ≈ 12.5 m sphere).
  Larger requests split into sub-ops sharing a `TransactionId`.
- Terrain-thread budget: **≤ 8 ms per op** at the cap. If E-2 shows a capped op exceeds this,
  lower the cap — the split mechanism already exists.

### 7.2 Journal and snapshot

- **~124 bytes per edit** journalled (§4.7). Target: measure against R-003's real number.
- Dense 32³ snapshot is 128 KB; **SparseDiff should reduce a typical tunnelled chunk by 10–100×**.
  That factor is the whole basis of F2 and F3 and it is **unmeasured** — E-3.
- Compaction target: **< 50 ms per chunk**, off the game thread, throttled to N chunks/second.

### 7.3 Bandwidth

- **Ops:** ~48 bytes on the wire. 32 players × 3 edits/s × ~6 subscribers each ≈ **28 KB/s**
  server egress total. Comfortable.
- **Snapshots:** the real risk (FM-4). Reliable RPCs must be fragmented — UE's per-bunch
  limits make a 128 KB payload in one RPC a non-starter. Proposed: **16 KB fragments,
  per-connection budget 256 KB/s**, nearest-chunk-first ordering, and snapshot traffic on
  the owning `TerrainStreamComponent` so it competes only within that connection.
- A 100-modified-chunk JIP at 256 KB/s is 5 s at dense encoding and well under 1 s if
  SparseDiff performs. **E-6 measures it.**

### 7.4 The two R-010 costs, architecturally

**Invoker.** A plugin invoker is required on every character on both client *and* server
(§9 2d) — and putting `UVoxelInvokerComponent` on `BP_ThirdPersonCharacter` is a plugin type
in gameplay, i.e. exactly the leak D-011 forbids. Resolution: `TerrainBackendVPLegacy`
provides `UTerrainStreamingProxyComponent`, a game-named component whose *implementation*
creates and configures the plugin invoker at runtime. Gameplay attaches the game class and
never names a plugin type. Server-side invokers are configured collision-range-only
(`bUseForLOD = false`, `bUseForCollisions = true`), since a dedicated server needs collision
around players and nothing else. Cost at 32 players is **unknown — E-8**.

**Movement base.** `VoxelProceduralMeshComponent` has no net GUID, so every
`ClientAdjustPosition` on a player standing on terrain is discarded (§9 2d). One promising
architectural mitigation, cheap to test: **`MovementBaseUtility` only stores a *relative*
base for movable primitives** — so if the proc-mesh components can be forced to
`Mobility = Static` (via `AVoxelWorld::PrimitiveSettings` and/or `bStaticWorld`, §10.4)
while still updating on edit, the character stores an absolute base and corrections resolve
normally. **Unknown — E-9.** If no configuration achieves it, the fallback is a movement-
component override that refuses the voxel mesh as a relative base, and if *that* fails,
this becomes a genuine argument in the T-101B adoption verdict rather than a design problem.

---

## 8. What stays plugin-specific vs game-owned

### 8.1 The line

| Concern | Owner | Note |
|---|---|---|
| Edit semantics (what Remove means) | **Game** | `ETerrainOpKind` |
| Op ordering, `OpSeq`, chunk revisions | **Game** | The plugin has no concept of either |
| Validation, permissions, reach, rate limits | **Game** | The plugin has no authority concept at all (§10.17) |
| Material identity and semantics | **Game** | `FTerrainMatId`; the plugin index is an adapter-local encoding |
| Yield computation and economy | **Game** | D-011, non-negotiable |
| World shape (density + material field) | **Game** | `ITerrainDensityField` in `TerrainCore`; §4.6 |
| Persistence format, journal, compaction, migration | **Game** | The plugin's whole-world blob cannot express it (§2.3) |
| Relevancy, subscription, JIP protocol, transport | **Game** | Stock UE replication per D-003 |
| Chunk keys and coordinate policy | **Game** | Aligned to plugin constants, defined by us |
| — | — | — |
| Density storage and octree | Plugin | `FVoxelData` |
| Meshing, LOD, materials-as-rendered, collision cooking | Plugin | |
| Sphere/box/level edit kernels | Plugin | `UVoxelSphereTools` etc. |
| Invoker/LOD streaming mechanics | Plugin | wrapped by §7.4 |
| Threading of edit and mesh work | Plugin | we serialise *entry*, not internals |

### 8.2 Explicitly *not* used, and why

- **`bEnableMultiplayer` / `UVoxelMultiplayerTcpInterface`** — not an implementation in Free
  (§10.12/10.14: both entry points return `false` immediately). Even in Pro it is a
  side-channel outside UE authority and would violate D-002 and D-011 in one step.
- **`UVoxelWorldSaveObject` / `AVoxelWorld::SaveObject`** as the persistence mechanism — the
  unit is the whole world, and `SaveData()` is editor-only (§9 2e). **But keep it as a
  correctness oracle:** in the `Restart.Identity` test, compare our reconstruction against a
  plugin whole-world save/load of the same session. An independent second opinion on our own
  save code is cheap and worth having.
- **Spawners, voxel physics, mesh import, surface masks** — stubbed in Free (§10.14). Nothing
  in this design touches them. Note this means R-005 (foliage over excavations) cannot even
  be *observed* through plugin spawners on this backend; foliage is PCG and conventional
  meshes anyway per D-015, so its invalidation is a game-side problem.
- **Undo/redo (`SaveFrame`, `Undo`)** — per-leaf frame stacks (§10.7) are an editor
  affordance. Our journal is the history. Do not enable `bEnableUndoRedo` on the server:
  it is memory growth with no consumer.

---

## 9. Build order — one evening each

Each step compiles, is testable on its own, and moves at least one risk. R-009 is the reason
this section exists at all.

| # | Step | Ends with | Closes / moves |
|---|---|---|---|
| 0 | Create the C++ modules (`VoxelWorld` primary, `TerrainCore`) with one empty subsystem | **The project builds from source for the first time.** Nothing else changes | The unexercised-VS risk (§2.2) |
| 1 | `FTerrainOp`, chunk keys, `ITerrainBackend`, `FMemoryTerrainBackend`, `UTerrainService` skeleton | All §6.1 codec and revision tests pass, headless | A5 begins |
| 2 | `FVPLegacyBackend` + `UTerrainStreamingProxyComponent`; **rewire the T-101A Blueprint to `RequestEdit` and delete the direct plugin calls** | Digging works exactly as today, through the service, server-authoritative in standalone | **Both failed drift checks pass.** STATE's stated obligation is discharged |
| 3 | Server validation, terrain thread, `ClientApplyOp`, subscription set | 3-client PIE convergence test | A1 / R-001 |
| 4 | Journal + snapshot + compaction + boot replay | Restart identity and save-growth tests | A3 / R-003 |
| 5 | JIP protocol, fragmentation, resync | Join-in-progress test with measured bytes | A2 / R-002 |
| 6 | Yield pipeline + `SingleIndex` materials + inventory credit with `LastAppliedOpSeq` | Volume-accuracy test | A4 / R-004 |
| 7 | `SelfClearance`, KillZ net, movement-base experiment | Standing-on-edit test | A6 / R-010 |
| 8 | C++ `ITerrainDensityField` with strata + ore body (T-108), forwarded through the adapter generator | The test hill generates instead of being sculpted by script each session | R-008, and the D-011 debt on world generation |

Steps 3–7 map one-to-one onto the T-101B sub-steps, which is deliberate: the architecture is
built by running the gate, not before it.

---

## 10. Backend replacement — the concrete procedure

1. Write `TerrainBackendX` implementing the seven `ITerrainBackend` methods.
2. Run the **`Backend.Conformance` suite** (§6.1) against it. It passes or the backend is not
   a candidate. This suite is the definition of the contract; there is no other one.
3. Implement `ITerrainDensityField` forwarding for the new backend's generator concept.
4. Set `BackendModule=TerrainBackendX` in config. No gameplay code changes; no save format
   changes, unless voxel size or grid alignment differ, in which case a resample migration is
   required and is a known, bounded piece of work (FM-9).
5. Run §6.2 and §6.3 unchanged. The tests are written against the service, not the plugin.

If a swap requires touching anything in `TerrainCore` or `VoxelWorld`, the boundary has
failed and that is a bug in this architecture, not in the new backend.

---

## 11. Unknown — prototype this

Nine things this design rests on that are **not** established by evidence in the packet. Each
has an experiment that resolves it and a stated consequence if the answer is unfavourable.
Per AGENTS §10, naming these is the correct behaviour, not a gap in the proposal.

| # | Unknown | Experiment | If unfavourable |
|---|---|---|---|
| **E-1** | The mapping from `FVoxelValue` density to occupancy, and whether `int16` density is linear enough in the transition band for accurate volume | Remove spheres of r = 1, 2, 5 m in homogeneous material; compare `ΣΔocc × V` to `4/3πr³`; repeat 20× for determinism | Yield falls back to counting *fully* transitioned voxels with a calibrated fudge factor — less elegant, still simulation-owned, still not "raycast a node" |
| **E-2** | Whether `bMultiThreaded = true` edits are bit-deterministic, and whether the sync/async overloads agree | Same op sequence, 20 runs each, all four combinations; compare `HashRegion` | Authoritative path forces single-threaded; measure the throughput cost against §7.1 |
| **E-3** | The real compression ratio of SparseDiff for a realistically tunnelled 32³ chunk | Dig a 20 m tunnel; write both encodings; compare bytes | If the ratio is poor, drop to 16³ chunks (F2) — one constant — and re-measure |
| **E-4** | Whether a collision-cook-complete signal is reachable (`BindVoxelChunkEvents`, `IsVoxelWorldMeshLoading`, `GetTaskCount`) | Instrument an edit; log the interval between data commit and collision availability | `SelfClearance` + KillZ carries FM-5 alone, which is weaker but shippable |
| **E-5** | `FVoxelData::Lock` semantics — what is actually safe concurrently, and from which thread (undocumented, §10.17) | Stress two threads on disjoint and on overlapping bounds under `VOXEL_DEBUG` | Confirms F4-B was the right call; blocks F4-C permanently if bad |
| **E-6** | JIP payload size and time for a heavily excavated region, and the right fragment size under UE's reliable-bunch limits | Script 5,000 edits in a 100 m radius; join a fresh client; measure bytes, wall time, and movement hitching | Lower the subscribe radius, or stream snapshots over a separate unreliable+ack channel — a larger change, so measure early |
| **E-7** | Whether round-trip latency without prediction is tolerable for digging on a LAN/friends server | Play it. Subjective, and the Director owns the verdict | Client-side prediction is additive (§4.4), not a redesign |
| **E-8** | Cost of a `UVoxelInvokerComponent` per character at 16–32 players, and its LOD behaviour on a dedicated server | Spawn 32 invoker-bearing pawns; measure octree update cost and server frame time | May force a server-only coarse-invoker scheme (one invoker per cluster of players) |
| **E-9** | Whether the proc-mesh can be made a non-relative movement base (`Mobility = Static` / `bStaticWorld` / `PrimitiveSettings`) while still updating on edit | Set it, stand on terrain in 3-client PIE, watch for `ClientAdjustPosition` warnings and for edit updates still landing | Movement-component override; failing that, R-010 becomes a genuine backend-adoption argument for the T-101B verdict |

**E-1, E-2 and E-9 are the three that could change this design rather than just tune it.**
They are cheap and should run first — E-1 and E-2 fit inside build step 2, E-9 inside step 3.

---

## 12. Director rulings required before implementation

Per AGENTS §2, the Architect proposes and the Director rules. These are the forks where a
choice was made that the Architect does not have the authority to make alone. Each is stated
so it can be ruled with a single `rule D-0XX:` line.

| Fork | Question | Recommendation |
|---|---|---|
| **F1** ⚑ | Global op sequence, per-chunk revision, or both? (named explicitly in AGENTS §10) | **Both** (§3 F1) |
| **F2** | Authority chunk size: 16³, 32³ or 64³ voxels | **32³**, subject to E-3 |
| **F3** | Snapshot content: dense, sparse-diff-vs-generator, or ops-only | **Sparse with dense fallback**, selected per chunk by size |
| **F4** ⚑ | Edit execution: game thread, one terrain thread, or parallel workers (a threading decision, R3 by AGENTS §3) | **One terrain thread** |
| **F5** | Durability: fsync-before-ack, group commit, or async flush with journal-as-single-ordering-authority | **Async flush + `LastAppliedOpSeq` reconciliation** — trades a small loss window for dig feel |
| **F6** | Subsystem vs actor for the terrain service (named in AGENTS §10) | **`UWorldSubsystem`** — no transform, no relevancy of its own, exists before any pawn |
| **F7** | Change `MaterialConfig` from `RGB` to `SingleIndex`, accepting the material-collection rendering work | **Yes** — it is what makes per-voxel material identity exact, and Art Direction already budgets material work |
| **F8** | Module naming: `TerrainCore` / `TerrainBackendVPLegacy`, avoiding "Voxel" in game-owned names per D-015 | **Yes** — these names outlive the codename |

**One flagged recommendation, not a request to overturn anything.** D-012 says "chunk data
lives in versioned files" and this design honours that. But it is worth the Director knowing
that the *entities* half — SQLite for inventories — becomes coupled to the terrain journal by
F5's reconciliation rule (yield ordering lives in the journal, inventory carries a watermark).
That is a deliberate coupling and the alternative is a two-phase commit across two stores,
which is more machinery than a friends server should carry. If the Director prefers the
stores fully independent, F5 must instead be ruled to option A or B, and the latency cost
lands on the dig.

---

## 13. Summary

Six load-bearing choices, in one paragraph each:

**The boundary is a build-system boundary.** `TerrainCore` cannot include a plugin header
because its `Build.cs` does not list the plugin. D-011 becomes a compile error rather than a
convention, and a dense in-memory test backend makes the entire authority layer testable
with no renderer, no plugin and no engine world.

**Ops are integers in voxel space.** The server quantises once, at validation; those integers
are the wire format, the journal record and the argument to the plugin. An entire class of
silent one-voxel divergence is removed for free.

**One terrain thread.** Concurrent-edit convergence stops being a correctness question and
becomes a throughput question, which is measurable and tunable. This is the single largest
complexity saving in the design and it is why R-001 is a test rather than a protocol.

**Both revision counters.** Global `OpSeq` for ordering and transactions, per-chunk `Rev` for
relevancy, JIP and gap detection. Neither alone answers both questions.

**We own the save.** The plugin's save is whole-world and its chunk-addressed internals are
private, so it cannot be D-012's store — but `FVoxelData`'s bulk read/write and
`FVoxelDataAccelerator`'s generator-vs-stored discrimination give us everything needed to
build a per-chunk, diff-encoded, versioned, migratable one. The plugin's blob survives as a
correctness oracle, which is a better use for it.

**Nine things are unknown and named.** Three of them (density-to-occupancy accuracy, edit
determinism, the movement base) could change this design rather than tune it, they are cheap,
and they run inside build steps 2 and 3 rather than after the architecture is committed.

The honest one-line summary, in the same spirit as STATE's: *this design is a plan for making
the representation into a world, and its riskiest assumption is that removed voxel volume
converts accurately into ore.*

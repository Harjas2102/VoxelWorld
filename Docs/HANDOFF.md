# HANDOFF.md — Current agent handoff

> Rolling work log and formal handoff for either agent. Follow OPERATIONS §5.1.
> STATE owns checkpoint status; approved architecture remains in ARCHITECTURE and
> DECISIONS. This file records actionable evidence, not a new ruling.

## Identity and status

- Updated: **2026-09-07, CP-012 checkpoint.** T-113 verified this session. The CP-011 and
  CP-010 records below are retained as history.
- Outgoing: **Claude**, Implementer. Incoming: either agent; one active Implementer.
- Task: **T-113 / build step 2 complete at CP-012**, with one item outstanding — the
  Director's by-hand LMB/RMB dig. Digging now routes through `UTerrainService::RequestEdit`;
  both drift checks flagged since T-101A are cleared **for standalone only**.
- Risk/authorization: R2 inside the approved R3 architecture. The Director said "I trust you
  on all accounts to execute anything as needed for the implementation. Begin everything
  necessary", then typed "checkpoint". Recorded as **D-031**. Authority is bounded to this
  increment, its one commit, this checkpoint and its push.
- Base: `cbcacf4` (CP-011), branch `main`. Clean on receipt; `git pull --ff-only` reported
  already up to date. Implementation landed as **`0eabf48`**, pushed `main -> main`. This
  handoff belongs to the enclosing CP-012 commit; verify its final commit/push and a clean
  worktree from Git on receipt.
- Checkpoint scope: STATE, BACKLOG, DECISIONS, RISKS, ARCHITECTURE's CP-012 determination
  block, and this handoff. **No active build, test or editor process remains** — every
  UnrealEditor process this session started has been stopped and that was verified. No
  unfinished implementation. Preserve any unexpected dirty work on pickup.
- **The session hit the usage limit mid-task and was resumed.** The breadcrumb below was
  written before the risky editor work, which is what made the pickup cheap. Keep doing that.

## Next safe actions (CP-012)

> **Largely superseded by the T-108 and T-114 breadcrumbs below, which are later. Read those
> first.** Specifically:
> - Item 3 is **DONE** — the Director ran the by-hand dig and reported "Play solo worked... I can
>   place and dig", so build step 2 is closed. Do not ask him for it again.
> - Item 4 is **OBSOLETE**. DEF-4, DEF-5 and DEF-7 were closed at T-114 and **build step 3 is
>   unblocked.** Its instruction to open a proposal file for a Director ruling was also wrong
>   under **D-023**: these are technical decisions, ruled and logged by the Architect. The
>   Director restated that in the strongest terms on 2026-09-07 — do not send him a technical
>   proposal.
> - Item 7 is **DONE** — AR-5 confirmed at T-108, and AR-6 added there. Both want DECISIONS
>   entries at the next checkpoint.
> - Items 1, 2, 5 and 6 still hold.

1. Read the required docs and this handoff. Verify the CP-012 commit, main/origin state and
   worktree; sync with `git pull --ff-only`.
2. Recite CP-012. **T-113 is complete — do not rebuild the adapter, the service entry point
   or the Blueprint rewire** unless new changes or failures justify it. The rewire script is
   idempotent and reports "already rewired" if run again.
3. **Ask the Director for the by-hand dig result** if it has not been reported:
   `Tools\Play-Solo.ps1`, standalone not PIE, LMB digs and RMB places. `Terrain.SelfTest` in
   the console gives the same answer without a mouse.
4. **Build step 3 may not start.** §9 binds it to DEF-4, DEF-5 and DEF-7, all open, and §14
   forbids starting a step while a defect is bound to it. The real next task is closing those
   three, which is **R3**: proposal at `Docs/proposals/`, independent review by whichever
   vendor did not author, then a Director ruling. STATE's "Current task" lists what each
   defect actually requires.
5. Build and test with **5.8** paths (`C:\Program Files\Epic Games\UE_5.8`); README carries
   the exact two commands. Two console-command gotchas, both learned the hard way:
   `-ExecCmds=Automation RunTests TerrainCore; Quit` works with a **semicolon**, but
   `-ExecCmds` splits on **commas** and does not reliably deliver argument lists to a
   deferred console command — so anything driven from a command line must be a **single
   argument-free command**. That is why `Terrain.SelfTest` takes no arguments and self-delays
   two seconds: the voxel world finishes generating *after* the first map-load commands fire.
6. **Three open items this session surfaced but did not fix**, all in RISKS: **R-013** (the
   production adapter has not passed `Backend.Conformance`, and the §6.2 harness that would
   run it does not exist or have an owner); **R-010's KillZ**, still a prerequisite for
   anyone actually playing, plus the new dual-invoker observation; and **R-008's**
   un-evaluated Mesh Terrain watch item. None is build step 3's job unless the Director says
   so.
7. **AR-5 is awaiting confirmation.** It is one struct's two fields and is cheap to overrule
   while it stays that way.

---

## T-114 working breadcrumb (D-028) — DEF-4, DEF-5 and DEF-7 closed. Build step 3 is unblocked.

- 2026-09-07, Claude, Implementer/Architect. Base `db4cb72` (T-108), clean on receipt.
  **STATE / BACKLOG / DECISIONS / RISKS are still not updated** — AGENTS §1 reserves that for
  the word "checkpoint". ARCHITECTURE.md *is* updated, because §14 says a defect closes with
  "a written resolution **in this document**".
- Director instruction for this increment was one word: **"Ok proceed."** Under D-023 these are
  technical decisions, so they are ruled and logged here, not sent to him. He gets one line.
- **This increment writes the SPECIFICATION and its headless evidence. It does not implement
  build step 3.** Server validation, `ServerRequestEdit`/`ClientApplyOp`, the subscription set,
  the queue, the dedup ring and split ops are the next increment and are a large one.

### What was blocking, and what changed

§9 bound step 3 to K1, K4, DEF-4, DEF-5 and DEF-7. K1 and K4 were ruled at CP-005 (D-024); the
three defects were open, and §14 forbids starting a step while a defect is bound to it. All
three now read **Resolved** in §14 with their resolution sections named.

### DEF-4 → §4.5.1 Thread affinity, ownership, shutdown and cancellation

- An affinity/ownership table over init, mutation, reads, invalidation, callbacks, destruction.
- **Five rules.** The whole `ITerrainBackend` surface is game-thread only — no read/write split,
  because a read that races a mesher's internal write is the same defect as a write that does.
  `ITerrainDensityField` is the single any-thread exception and is safe by construction.
  **No lock of ours is ever held across a call into the plugin** — that is the answer to the
  defect's "an external bounds lock may conflict with one the wrapper takes internally": not a
  better lock order, no lock at all. No `UObject` outside the service owns an in-flight op. The
  density field outlives the backend.
- **A four-state machine** — Uninitialised / Ready / Draining / TornDown — with a fixed
  eight-step teardown order. The pending queue is **discarded, not drained**: a queued op has no
  `OpSeq`, no journal record and no broadcast, so discarding it is exactly the *no-change* half
  of the DEF-7 invariant.
- **Cancellation is a queue operation, not a plugin operation.** `ApplyOp` is synchronous on the
  game thread and cannot be pre-empted by `EndPlay`, travel or PIE exit — all of which are
  themselves game-thread events — so no op is ever partially applied at teardown. We register no
  completion callback that can outlive the service, and we never wait on plugin async work,
  because waiting on the game thread for a worker that wants the game thread is the deadlock the
  rule exists to prevent.
- **Explicitly still open and handed on:** the plugin's own internal thread safety (E-2/E-5),
  collision readiness (DEF-8), durability ordering (DEF-1).

### DEF-5 → §4.10 Operation semantics and determinism

- **The operation set is CLOSED at Remove, Add, Paint. `Flatten` and `Smooth` are removed from
  it** and permanently refused, rather than being given invented semantics. The defect's
  complaint was that they were "named without plane, strength, iteration or falloff semantics";
  the answer is to stop naming them. The enumerators stay because the 58-byte wire is permanent.
- **Canonical geometry.** Write set `W = { v : |v-C|² <= r² }` in double with `<=` and **no
  epsilon**; `r = RadiusVoxQ16/65536` is exact on any IEEE platform. Read bounds
  `B = [C-floor(r), C+floor(r)+1)`, which contains `W` with no slack. Nothing outside `W` may
  change. Rounding is `TerrainQuantise.h`'s existing floor rule, unchanged.
- **Per-op meaning** plus two required properties: **monotonicity** (a Remove can never create
  solid rock, which is what lets a clearance check survive the op it validated) and
  **idempotence** (which is what makes DEF-3's duplicate JIP application survivable rather than
  corrupting — and the second, independent reason Smooth is not in the set).
- **Determinism split into three claims, and only two are made.** (a) same backend/build:
  required. (b) same backend, different build or platform: required, **with the kernel's own
  floating point named as the residual risk** rather than papered over. (c) **cross-backend value
  identity: explicitly OUT OF SCOPE**, because §8.1 gives "sphere/box edit kernels" to the plugin
  while giving "what Remove/Add/Paint mean" to the game — those two are consistent only if the
  game specifies properties and the backend supplies values.
- **That has a real consequence and it is written down: a backend swap is a resample migration
  for every EDITED chunk, not a format-compatible reload.** FM-9 previously flagged only voxel
  size and grid alignment. Pristine chunks regenerate and are unaffected. `Backend.Conformance`
  accordingly asserts the contract and **never** density equality between two backends — a
  weaker claim than §10 could be read as making, and the honest one.
- **`HashRegion` rules fixed** — position-sensitive, iteration-order-independent, values and
  materials, zero for non-resident, comparable only within one backend and build.
  `FMemoryTerrainBackend` already satisfied all four and is now named the reference.
- **Version compatibility table**: ops are only ever replayed against the exact
  (generator, backend, format) triple they were recorded under; anything else uses the payload
  or regenerates. Recovering an edited chunk whose payload was compacted away stays **DEF-9**.

### DEF-7 → §4.11 Admission, commit and split operations

- **Trusted-input table.** What the client may supply, and what the server derives and never
  reads from the request — `SourceId` from the connection, the quantised centre, the effective
  radius, `MaterialId`, and reach recomputed from the **server's** pawn transform.
- **Validation runs on the quantised footprint**, not on the float request. One voxel of
  disagreement between "permitted" and "changed" is a permission bypass at the edge of every
  protected zone in the game, and it is invisible until someone looks for it.
- **Identity is `(SourceId, RequestId)`** with a 64-entry per-connection ring of resolved
  receipts. A repeat returns the stored receipt and mutates nothing; an identity older than the
  ring is rejected `StaleRequest` rather than executed. Reliable RPCs are re-sent across
  reconnects, and without this a re-sent dig mines the same rock twice.
- **Two-phase reserve-then-revalidate.** Admission reserves; commit re-runs every check
  immediately before `ApplyOp`. Failure releases and rejects — **nothing has been mutated at that
  point, so this is clean by construction and the design needs no rollback.**
- **Bounded queue, twice** (global and per-source), round-robin across sources, FIFO within one,
  **no priority classes** — a priority class is a starvation bug that only shows up under the
  load you cannot reproduce.
- **The no-change-or-committed invariant, tabulated.** Two consequences, both changes to what the
  document previously allowed: **`bTruncated` is removed as a success signal** (a backend that
  would truncate must fail the whole op), and **`ApplyOp` returning false means nothing changed**,
  not "something may have changed". The adapter reaches that by pre-validating the whole
  footprint; that rests on an assumption about the plugin kernel, which is **stated in §4.11.6
  rather than buried**, and `Backend.Conformance` probes it.
- **Only `Box` ops split. An over-cap `Sphere` is rejected `TooLarge`, never split.** A sphere
  has no exact partition, and the 58-byte wire has nowhere to put the clip box a correct one
  would need — so an approximate split would make the same request produce different terrain
  depending on whether it crossed a cap, which is a determinism bug wearing a performance
  feature's clothes. A transaction is **not atomic across sub-ops**, and that is stated rather
  than assumed because the alternative needs a durability protocol DEF-1 has not defined.
- Four new `ETerrainEditRejection` values — `ShuttingDown`, `QueueFull`, `StaleRequest`,
  `Revalidation` — each present because a client that cannot tell it from its neighbour will do
  the wrong thing with it.

### Code written this increment

| File | What |
|---|---|
| `Private/Tests/TerrainOpSemanticsTest.cpp` (new) | `Op.Semantics.Contract` and `Op.Semantics.Golden` — the DEF-5 evidence |
| `Public/TerrainService.h` | The four DEF-7 rejection reasons; `ETerrainEditKind` doc corrected now that DEF-5 is closed |
| `Private/TerrainService.cpp` | Their names in the log switch |
| `Public/TerrainTypes.h` | `bTruncated` marked as no longer a success signal (§4.11.6) |

**`Op.Semantics.Golden` deserves one note.** Its seven expected hashes were recorded once, from
the run that first produced them, and the test says in its own error text that a failure is never
fixed by updating the number. Two of the seven are load-bearing on their own: step 2 (a repeated
op) equals step 1, and step 6 (a refused op) equals step 5 — so idempotence and the
no-change-on-failure rule are pinned by the fixture as well as by the contract test.

### Verification

| Check | Result |
|---|---|
| `VoxelWorldEditor` / `VoxelWorld` builds | Both `Result: Succeeded` |
| TerrainCore automation | **Thirteen** tests, all `Result={Success}`, `EXIT CODE: 0`. The eleven from T-108 plus `Op.Semantics.Contract` and `Op.Semantics.Golden` |

One assertion had to be corrected while writing, and the correction is the interesting part: the
straddle test asserted 8 affected chunks and got 7, because an earlier op in the same fixture had
already emptied the chunk-0 side of that sphere and `AffectedChunks` reports chunks that actually
**changed**. The backend was right. The test now runs that block on its own backend so it
measures the contract rather than test ordering.

### What is NOT done

- **Build step 3 itself.** Nothing here implements validation, replication, the queue, dedup,
  reservations or splitting. §4.11 and §4.5.1 are the specification those will be built to, and
  the §6.1 tests they name (`Split.Equivalence`, the new `Backend.Conformance` clauses) do not
  exist yet.
- **DEF-1, DEF-2, DEF-3, DEF-6, DEF-8 remain open**; DEF-9 partially resolved. Steps 4–7 stay
  blocked by them.
- **R-013 unchanged** — the production adapter still has not passed `Backend.Conformance`, and
  §4.10.4(c) now explains why that suite can never mean "writes the same densities as the
  reference": it means "obeys the contract".

### Next safe action

`checkpoint`, then build step 3 as its own increment. It is the largest single piece of work in
the phase and should not be started at the tail of another one.

---

## T-108 working breadcrumb (D-028) — build step 8, IMPLEMENTATION COMPLETE

- 2026-09-07, Claude, Implementer. Base `a83e5e2` (CP-012), clean on receipt;
  `git pull --ff-only` reported already up to date. **Nothing in STATE / BACKLOG /
  DECISIONS / RISKS is updated** — AGENTS §1 reserves that for the word "checkpoint",
  which has not been typed for this increment.
- **The Director reported the CP-012 hand check: "Play solo worked... I can place and dig."
  That closes the last outstanding item of T-113 / build step 2.** He added "still don't
  see a hill", which is what this increment is about.
- **Why step 8 and not step 3.** §9 binds step 3 to DEF-4, DEF-5 and DEF-7, all open, and
  §14 forbids starting a step while a defect is bound to it. Step 8 is bound only to
  **R-008, a risk — not a defect and not an unruled fork** — so §14's rule does not reach
  it and it was clear to start. It is also the one remaining step that moves the Phase 1
  milestone, which BACKLOG states as **"one hill is trustworthy."**
- **Director instruction recorded, and it is D-023.** He restated that technical proposals
  and rulings must not come to him: *"if you need me to decide something, it sure as hell
  better not be nuanced programming you already know I don't have a clue about."* D-023
  already says exactly this — GAME decisions to the Director, TECHNICAL decisions ruled and
  logged by the Architect, escalated only if a player would notice, scope changes, or it
  costs money. AR-5 and AR-6 are therefore ruled, not asked. **Do not open a proposal file
  for a technical decision expecting him to read it.**

### Why the world was a plane, and what replaced it

T-101A finding 2b: **Voxel Graphs are Pro-gated and fail silently**, so the only runnable
generators on Free are `VoxelFlatGenerator` and `VoxelEmptyGenerator` (R-008). The T-101A
hill was therefore **sculpted by a Python script**, and finding 2e / R-003 records that a
sculpted hill does not survive a map load: every standalone process regenerated a flat plane.
That is the whole reason the Director has never seen a hill in the game.

**`FTerrainWorldField` (TerrainCore) is now the world's shape**, and it is a pure function of
position and seed, so the hill comes back on every load in every process with no save file.

### AR-5 and AR-6 — two Architect determinations

- **AR-5 stands as written at T-113** (`FTerrainBackendInit` gains `UWorld* World` and
  `FTransform OriginTransform`). It has now been in service through two increments and the
  T-108 generator depends on it: `ConformVoxelWorld` forcing the actor onto the game's origin
  and voxel size is what makes plugin voxel coordinates identical to game voxel coordinates,
  which is why the generator needs no coordinate conversion at all. **Confirmed, not asked.**
- **AR-6 (new, this increment): `ITerrainDensityField` gains
  `FTerrainDensityRange SampleRange(const FTerrainBox&) const`, with a default implementation
  returning the full [-1, 1].** §4.6 declared `Sample` alone, which is enough to FILL a chunk
  and not enough to SKIP one. Without it the plugin's octree must sample every voxel of every
  region at every LOD across a 512 m world of 50 cm voxels. The default is always correct and
  merely forfeits the skip, so no existing implementer breaks and no method becomes required.
  Both AR-5 and AR-6 want a DECISIONS entry at checkpoint.

### What is written

New in `TerrainCore`:

| File | What it is |
|---|---|
| `Public/TerrainMaterials.h`, `Private/TerrainMaterials.cpp` | The GAME's material id catalog: Unknown/Air/Topsoil/Dirt/Stone/DeepStone/Bedrock/IronOre. Ids are permanent; **K9 (id to plugin index) and the yield economy are untouched** |
| `Public/TerrainWorldField.h`, `Private/TerrainWorldField.cpp` | `FTerrainWorldField : ITerrainDensityField` — the hill, the escarpment, the basin, the strata and the ore body. No plugin, no UObject, no `UWorld` |
| `Private/Tests/TerrainWorldFieldTest.cpp` | Four new automation tests |
| `Public/ITerrainDensityField.h` | AR-6 `SampleRange` plus the threading rule |
| `TerrainService.h/.cpp` | Owns the field, lends it to the backend, releases it **strictly after** `Shutdown` |

New in `TerrainBackendVPLegacy`: `Private/VPLegacyDensityGenerator.h/.cpp` —
`UVPLegacyDensityGenerator : UVoxelGenerator` plus its instance. It decides nothing; it
converts a plugin query into a field query. **Private on purpose**, like `VPLegacyBackend.h`.
`VPLegacyBackend` installs it before `CreateWorld` and forces a recreate if it changed.

`Config/DefaultEngine.ini`: **`GeneratorVersion=0` to `1`.** Version 0 was the flat plane the
project ran on from T-101A to CP-012; version 1 is this world. §4.2 / K3 put it in every
snapshot header so a chunk saved under one world is never reinterpreted by another.

### The world, in numbers (voxel space, 1 voxel = 50 cm)

- **Plain at -6 voxels (-3 m), deliberately below world Z = 0.** PlayerStart is at world
  Z = 150 cm; a plain that could bulge above zero would spawn the player inside the ground on
  some seeds. `Field.Shape` asserts the invariant so a later tweak cannot quietly break it.
- **Hill** centred at voxel (140, 0) — **east of the origin, not on it**, because the origin is
  where eight chunks meet, where `Terrain.SelfTest` digs, and what PlayerStart faces from
  -82 m. Radius 280 voxels, so a **280 m** hill (GDD asks 256-512 m), with **65 m** of relief.
- **The cliff faces WEST, at the player.** An escarpment at X = 20 cuts the hill down to a
  24-voxel shelf over a 2-voxel blend: about **18 m of vertical rock with the strata showing**,
  which is the point — geology you can see without digging for it.
- **Basin** at (-260, -260), radius 150, 12 m deep — the GDD's lowland.
- **Strata by depth:** topsoil 2 m, dirt 10 m, stone 45 m, then deep stone; bedrock below
  Z = -420 voxels.
- **Ore body:** an ellipsoid at (140, 0, -30), radii (60, 40, 25) voxels, under the hill.
  Gated so it **never breaks the surface** — free surface ore would make GDD "Mining IS
  terraforming" optional, and `Field.Strata` asserts it across the whole world.
- Roughness is 3-octave integer-hashed value noise, amplitude 6 voxels. **No RNG and no float
  bit tricks**, because DEF-5's hazard is a generator that produces different bits elsewhere.

### Verification — executed, not reasoned about

Engine `C:\Program Files\Epic Games\UE_5.8`, run from `C:/Dev/VoxelWorld`.

| Check | Result |
|---|---|
| `VoxelWorldEditor Win64 Development` | `Result: Succeeded`. Only the two pre-existing `C4305` in the plugin's own headers |
| `VoxelWorld Win64 Development` (game target) | `Result: Succeeded` |
| TerrainCore automation | **Eleven** tests, all `Result={Success}`, zero failures, `EXIT CODE: 0`. The seven from CP-012 plus `Field.Shape`, `Field.Strata`, `Field.Range`, `Field.Determinism` |
| **D-011 `#include` boundary probe** | `VoxelTools/Gen/VoxelSphereTools.h` in `TerrainCore/Private/TerrainWorldField.cpp` gave `fatal error C1083` / `Result: Failed`. **File restored byte-identical, md5 `55344B436ADCE46E51F3CF53FD83DFB5` verified.** No `.Build.cs` and no `.uproject` change in this increment, so the D-025 guard is structurally untouched |
| Standalone boot | `Installing the game density field as the voxel world generator (was 'VoxelFlatGenerator')`; `created=yes, role=Server`; `Material config: 0` (= **RGB**, so the strata colours can render); `generator version 1`. **Zero `LogVoxel: Error`, zero fatals** |
| `Terrain.SelfTest` in standalone | **PASS**, 13 checks, zero failures |

**The single most direct piece of evidence that the world changed** is in that self-test:

```
voxel (0,0,0) chunk (0,0,0) rev 0, density -1.0000, resident 1
Remove r=200cm -> applied=1 reason=None opseq=1 chunks=8 voxels=895
```

At CP-012 the same line read `density 0.0010` and `voxels=438`. The origin used to be the
surface of a flat plane; it is now **buried in solid rock** under the shelf west of the
escarpment, so the same brush removes twice as much. Nothing else in the project changed that.

`Terrain.SelfTest` itself needed one fix: its Add previously placed at a fixed offset that was
only guaranteed to be empty on a flat plane. It now refills the hole it just dug, at a smaller
radius — the one address that is empty whatever shape the world has.

Evidence log `Saved/Logs/Standalone_T108.log` is local, gitignored, and overwritten by later runs.

### What this does NOT do

- **No K9 material mapping and no yield.** `TerrainMaterialDebugColor` in the adapter is a
  **cosmetic** palette so strata are visible in the cliff face. Nothing reads a colour back,
  nothing persists one, no yield is computed from one. `FTerrainEditResult::Removed` is still
  empty (step 6, DEF-6).
- **DEF-5 is not closed.** `Field.Determinism` pins same-build reproducibility only. Identical
  output across builds and platforms is DEF-5's question and this does not answer it.
- **R-013 unchanged** — the production adapter still has not passed `Backend.Conformance`.
- **R-010's KillZ unchanged**, and it now matters slightly more: the plain sits 3 m lower than
  the old flat plane, so the spawn drop is longer.
- Steps 3-7 are untouched and still blocked by DEF-4, DEF-5, DEF-7.

### Next safe action

**The Director looks at it.** `Tools\Play-Solo.ps1`, standalone. The spawn faces east; there
should be a large hill ahead with a bare rock face on its near side. Then `checkpoint`.
Still **do not start build step 3.**

---

## T-113 working breadcrumb (D-028) — build step 2, IMPLEMENTATION COMPLETE

- 2026-09-06/07, Claude, Implementer. Base `cbcacf4` (CP-011), clean on receipt;
  `git pull --ff-only` reported already up to date. Director typed "I trust you on all
  accounts to execute anything as needed for the implementation. Begin everything
  necessary", which is the authorisation for this increment. **Nothing is committed yet.**
- Risk class: the task is build step 2 in ARCHITECTURE §9. **No defect is bound to step 2**
  (DEF-10, the only one that was, is Resolved), so the step is clear to start under §14's
  own rule. Work stays inside the approved architecture; no numbered decision is changed.

### One architecture gap found and closed as AR-5 — read this first

§4.3 lists `Initialize`'s inputs as "seed, gen version, voxel size, bounds, density field,
role" and stops there. That is complete for a backend that owns only memory. It is NOT
complete for `FVPLegacyBackend`, which must find or spawn an `AVoxelWorld`, attach invoker
components to it and destroy them at teardown — every one of which needs a `UWorld`. The
alternative was for the adapter to reach for `GWorld` and guess.

**AR-5 adds two fields to `FTerrainBackendInit`: `UWorld* World` and
`FTransform OriginTransform`.** Both are ENGINE types, not plugin types, so this widens what
the game tells a backend without widening what a backend may tell the game. `OriginTransform`
exists because §8.1 assigns coordinate policy to the GAME: the service states where the grid
starts and the adapter conforms its actor to it, rather than the adapter reading the origin
off an actor someone may have dragged. `FMemoryTerrainBackend` ignores both and still runs
headless; the conformance suite leaves them defaulted and still passes.

This follows the AR-1..AR-4 precedent (an Architect determination recorded in the docs, not a
new numbered decision). **It wants a DECISIONS entry at checkpoint and a Director ruling.**

### What is written and building

New in `TerrainCore`: `TerrainChunk.h/.cpp` (the one definition of voxel-to-chunk keying),
`TerrainSettings.h/.cpp` (the `[/Script/TerrainCore.TerrainSettings]` section §4.1 names),
`TerrainBackendRegistry.h/.cpp` (name-to-factory, so TerrainCore never links an adapter),
`TerrainStreamingComponent.h/.cpp` (§7.4 / DEF-10), and `UTerrainService::RequestEdit` plus
backend and interest lifecycle. New module `TerrainBackendVPLegacy` holds `FVPLegacyBackend`
and is **the only module that may include a plugin header**. New in `VoxelWorld`:
`UTerrainInteractionLibrary`, the trace-then-request node the Blueprint will call.

Green as of this breadcrumb: `VoxelWorldEditor` and `VoxelWorld` both `Result: Succeeded`;
**seven** TerrainCore tests all `Result={Success}`, `EXIT CODE: 0` — the five from CP-010
plus new `Chunk.Keys` and `Backend.Registry`.

### Deliberate step-2 limits, recorded so nobody reads more into this than is there

- `FTerrainEditResult::Removed` is left EMPTY. Yield is step 6, DEF-6 is open, and §2.3
  records that `FModifiedVoxelValue` carries no material — so a number here would be invented.
- `ReadRegion`/`WriteRegion`/`HashRegion` move DENSITY ONLY, materials zero (K9, step 6).
  Useful as a convergence oracle; **not** the snapshot format, which is step 4 under K3/DEF-9.
- Therefore `FVPLegacyBackend` does **not** pass the full `Backend.Conformance` suite yet and
  no such claim is made. §10 requires that pass before "replaceable" is proven.
- `FlushPendingWork` is a deliberate no-op (§4.5: mesh and collision work is explicitly not
  serialised by us). Flatten, Smooth, Paint and box ops are refused, not approximated.
- Still absent by design: `ServerRequestEdit`/`ClientApplyOp`, journal, yield, reach and
  permission validation, split ops. All bound to steps 3+ behind DEF-4/5/7.

### Verification — everything below was executed, not reasoned about

Engine `C:\Program Files\Epic Games\UE_5.8`, run from `C:/Dev/VoxelWorld`.

| Check | Result |
|---|---|
| `VoxelWorldEditor Win64 Development` build | `Result: Succeeded`. The only warnings are two pre-existing `C4305` in the plugin's own headers |
| `VoxelWorld Win64 Development` (game target) build | `Result: Succeeded` |
| TerrainCore automation | **Seven** tests, all `Result={Success}`, zero failures, `**** TEST COMPLETE. EXIT CODE: 0 ****`. The five from CP-010 plus new `Chunk.Keys` and `Backend.Registry` |
| **D-011 `#include` boundary probe, both game modules** | With `#include "VoxelTools/VoxelDataTools.h"` in `TerrainCore/Private/TerrainCore.cpp`: `fatal error C1083` / `Result: Failed`. With `#include "VoxelTools/Gen/VoxelSphereTools.h"` in `VoxelWorld/VoxelWorld.cpp`: `fatal error C1083` / `Result: Failed`. **Both files then restored byte-identical, md5 verified.** The boundary is compiler-enforced on *both* game modules, not only TerrainCore |
| D-025 guard (AGENTS §9) still holding | Editor target: 278 plugins including `ModelContextProtocol` and `AllToolsets`, 71 matching build products. **Game target: 234 plugins, neither present, 0 matching products.** Unchanged by this increment |
| Blueprint asset scrubbed | `grep -oE "/Script/Voxel[A-Za-z]*"` over `BP_ThirdPersonCharacter.uasset` returns **only `/Script/VoxelWorld`**, our own game module. **No `/Script/Voxel` plugin reference remains in the asset** |
| Standalone boot | `LogTerrainCore: Terrain backend registered: TerrainBackendVPLegacy`; `FVPLegacyBackend ready ... voxel 50.0 cm, world 1024 voxels, created=yes, role=Server`; `bounds=[-512,-512,-512)-[512,512,512)`. `LogVoxel: Voxel Invoker enabled; Name: VoxelSimpleInvokerComponent_0` — our streaming interest, created through the backend. **Zero `LogVoxel: Error`, zero fatals** |
| `Terrain.SelfTest` in standalone | **PASS**, 13 checks, zero failures |

`Terrain.SelfTest` is the evidence that matters, because it drives the same `RequestEdit`
path the rewired Blueprint drives:

```
backend='TerrainBackendVPLegacy' ready=1 authority=1 interests=1
voxel (0,0,0) chunk (0,0,0) rev 0, density 0.0010, resident 1
Remove r=200cm -> applied=1 reason=None opseq=1 chunks=8 voxels=438
density 0.0010 -> 1.0000, rev 0 -> 1
ok  Add placed material            ok  OpSeq is monotonic across operations
ok  an over-large radius is refused as RadiusTooLarge
ok  an edit outside the world is refused as OutOfBounds
ok  a negative radius is refused as BadRequest
**** Terrain.SelfTest: PASS ****
```

438 voxels over **8** chunks is correct rather than surprising: the test digs at the world
origin, which is exactly where eight chunks meet, and `Chunk.Keys` asserts that case
separately. Evidence logs `Saved/Logs/VoxelWorld.log`, `Standalone_T113.log` and
`Standalone_T113edit.log` are local, gitignored, and overwritten by later runs.

The game target is **monolithic**, so it produces no separate adapter DLL and no
`Terrain`-named build product; its clean build with the module in the `.uproject` is what
shows the adapter is in it. The editor target does produce
`UnrealEditor-TerrainBackendVPLegacy.dll`.

### The Blueprint rewire, and how it was done

`Tools/Editor/rewire_dig_through_service.py` (committed, idempotent, re-runnable) did it.

**Unreal MCP was not used and could not be.** The MCP server binds loopback on demand and
Auto Start Server is off, by D-025's own reasoning — so it was not listening when this
session started and its tools were unavailable for the whole session. That turned out not to
matter: UE 5.8 ships a full Blueprint graph API to plain Python
(`unreal.BlueprintGraphEditor`, `unreal.BlueprintGraphPinLibrary`,
`unreal.BlueprintEditorLibrary` — `list_all_nodes`, `remove_nodes`,
`add_call_function_node`, `try_create_connection`, `set_pin_value`, `remove_member_variable`,
`compile_blueprint`). That is AGENTS §11's third rung, and it is better than the fourth.
**Whoever automates editor work next should reach for that before starting the MCP server.**

The graph went from **38 nodes to 19**. Deleted: both `LineTraceByChannel` chains, both
`BreakHitResult`, both `Branch`, `RemoveSphere`, `AddSphere`, both camera-manager chains, and
the `Event BeginPlay -> GetActorOfClass(VoxelWorld) -> Set TargetVoxelWorld` chain. Deleted
variable: `TargetVoxelWorld`, an `AVoxelWorld` reference **held in the asset** — §7.4 forbids
that separately from the code rule, and no compiler could ever have caught it. Kept: the Left
and Right Mouse Button events, each now feeding one `RequestTerrainEditFromView` node with
`Kind=Remove` / `Kind=Add`, `RadiusCm=200`, `TraceDistanceCm=1000` — the T-101A numbers
exactly, so the hand check compares like with like. Also attached
`UTerrainStreamingComponent`. Blueprint compiled clean and saved.

### What is outstanding

1. **The Director's hand check. It is the only thing between here and "step 2 done".**
   Run `Tools\Play-Solo.ps1`, standalone and not PIE. **LMB should dig and RMB should place,
   exactly as they did at T-112.5a** — that is the whole test. It is the CP-006 by-hand check
   reproduced once more, now through the service. In the console, `Terrain.SelfTest` gives
   the same answer without a mouse, and `Terrain.Status` / `Terrain.Edit <Remove|Add> X Y Z
   [RadiusCm]` are there for poking at it by hand.
   *One note for that run:* the plugin still logs `No Voxel Invoker found, using camera as
   invoker` a millisecond before ours registers, so both invokers end up live in standalone.
   Harmless here, but it means the camera invoker is still doing some of the LOD work, and
   that stops being true on a dedicated server. Worth a look at build step 3, not now.
2. **AR-5 wants a Director ruling and a DECISIONS entry** — see the top of this section.
3. **STATE / BACKLOG / DECISIONS / RISKS are deliberately NOT updated.** AGENTS §1 reserves
   checkpoint text for the word "checkpoint", which has not been typed for this increment.
   The **two flagged drift checks in STATE therefore still read FLAGGED**, and should flip to
   passing **for standalone only** at that checkpoint — server authority is not proven until
   build step 3, and §9 says to record the narrower result.

### Next safe action

Hand check, then `checkpoint`. **Do not start build step 3**: it is bound to DEF-4, DEF-5 and
DEF-7, all open, and §14's rule is that a step may not start while an unresolved defect is
bound to it.

## Completed files and decisions — CP-010 (history)

All source paths are relative to `C:/Dev/VoxelWorld/Source/TerrainCore/`.

| File | Result |
|---|---|
| Public/TerrainRevisionIndex.h (new) | Plain index API, no setter/reset/assignment or persistence API |
| Private/TerrainRevisionIndex.cpp (new) | Unseen reads return zero without insertion; distinct affected keys bump once per call |
| Public/TerrainService.h (modified) | Private owned index, public const queries and private metadata update helper |
| Private/TerrainService.cpp (modified) | Lifecycle ownership and game-thread/initialized-game-world/non-client gate |
| Private/Tests/TerrainRevisionTest.cpp (new) | TerrainCore.Revision.Monotonic, guarded by WITH_DEV_AUTOMATION_TESTS |

- AR-4 fixes in-memory monotonicity and exactly one bump per affected chunk.
  D-029 and ARCHITECTURE's CP-010 block record the approved bounded API plan.
- TryBumpRevisions deduplicates within one call. Repeated calls remain separate
  updates; retry deduplication/global OpSeq assignment remain later service work.
- Before any mutation, all affected revisions are checked against MAX_uint32.
  Overflow returns false without insertion or increments. Empty input succeeds.
  A saturated chunk does not block updates that affect only other chunks.
- Service Initialize creates ownership; repeated Initialize preserves history.
  Deinitialize releases the index; double teardown is safe. Public access is
  game-thread-only. Private TryAdvanceRevisions rejects off-thread calls before
  touching UObject/world state and otherwise requires HasAuthority.
- This helper updates metadata only; it does not execute backend edits. Future
  integration must handle revision exhaustion before terrain mutation; this
  increment does not resolve DEF-7's mutation/commit atomicity.
- ARCHITECTURE §6.1 explicitly requires no engine world. Tests therefore exercise
  the pure index and worldless service lifecycle/rejection. No UWorld is created.
  A development-only friend seeds overflow and owned-index fixtures; there is no
  shipping setter, role switch or public mutation bypass.

## Verification and limits — CP-010 (history; 5.7 commands, superseded by 5.8)

Executed from `C:/Dev/VoxelWorld`:

```powershell
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat' VoxelWorldEditor Win64 Development '-Project=C:\Dev\VoxelWorld\VoxelWorld.uproject' -WaitMutex
& 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'C:\Dev\VoxelWorld\VoxelWorld.uproject' '-ExecCmds=Automation RunTests TerrainCore; Quit' -unattended -nopause -nosplash -nullrhi -log
```

- Build: Result: Succeeded, exit 0; UHT passed, seven build actions, 4.85 seconds.
- Automation: 2026-09-06 17:25:08 UTC, five Result={Success} entries:
  Backend.Conformance, Op.Codec.RoundTrip, Op.Quantisation.Stable, Query.Point,
  Revision.Monotonic (all prefixed TerrainCore). Zero failures; automation and
  process exit 0. Evidence: Saved/Logs/VoxelWorld.log, lines 1113–1145, local and
  gitignored; may be overwritten by later runs.
- Revision coverage: 256 deterministic overlapping batches checked against a
  per-key count-of-containing-calls oracle, duplicates, negative/extreme keys,
  empty input, unaffected keys, max boundary and overflow atomicity in both
  input orders. Service ownership, repeated initialization, missing-world and
  worker-thread rejection, teardown and post-teardown rejection pass.
- Author diff review complete: all mutation follows overflow validation; service
  mutation is private and authority-gated. No independent review claimed.
- git diff --check clean; new/modified source files have no trailing whitespace.
  All 14 relative link targets in checkpoint docs resolve. Build.cs, TerrainTypes
  and ITerrainBackend unchanged against the base commit. Includes
  are engine/game-owned only; no new game-owned type/file name contains Voxel.
  Content, Config and uproject unchanged. Checkpoint docs now reflect CP-010.
- No live server/client authority, multiplayer PIE, production backend, disk
  persistence or compaction result is claimed. All existing terrain risks and
  drift flags remain open. T-112.5 engine upgrade has not started.

## Next safe actions as written at CP-010 — superseded, kept as history

1. Read required docs and this handoff. Verify the enclosing CP-010 commit,
   main/origin state and worktree. Sync a clean tree with git pull --ff-only.
2. Recite CP-010 and T-112.5. T-112.3 is complete; do not repeat its API planning,
   onboarding or tests unless new changes/failures justify doing so.
3. T-112.5 is next under D-025. Confirm its bounded task/risk plan and verify its
   engine/tooling prerequisites before changes. T-113 follows; neither has started.

---

## T-112.5 working breadcrumb (D-028) — kept as the record of how it went

- 2026-09-06, Claude, Implementer. Base `3aedeac` (CP-010), clean on receipt,
  `git pull --ff-only` already up to date. Risk class **R2**; plan approved by the
  Director in plan mode. No source or doc file changed yet other than this breadcrumb.
- **Blocked on a Director action.** UE 5.8 is **not installed** on this machine:
  `LauncherInstalled.dat` lists only `UE_5.7` (5.7.4). 719 GB free on C:.
  Director ruled: install `C:\Program Files\Epic Games\UE_5.8` **side-by-side, keeping
  5.7** as the rollback path. Nothing else starts until that install completes.
- Director also ruled the increment is **split**: **T-112.5a** = engine/plugin bump plus
  the CP-006 verification set re-run green (one commit); **T-112.5b** = enable
  `ModelContextProtocol` + `AllToolsets` editor-only, generate `.mcp.json`, add the D-025
  drift-guard line to AGENTS §9 (second commit).
- **D-025's premise re-verified against the live source, and it holds.**
  VoxelPluginFreeLegacy's README now advertises
  `VoxelFree-434-159fd19a0-5.8-Binaries.zip` next to the 5.7 zip, so
  `Tools/Install-VoxelFreeLegacy.ps1` only repoints. Note the plugin version moves
  **432 → 434**; this is a plugin build change, not only an engine repoint, so the
  CP-006 dig/invoker checks are a real regression gate, not a formality.
- UE 5.8 released 2026-06-17. Unreal MCP serves `http://127.0.0.1:8000/mcp`, loopback,
  no auth, and Epic labels it **Experimental** — a RISKS line against R-008 at
  checkpoint. `.mcp.json` comes from `ModelContextProtocol.GenerateClientConfig
  ClaudeCode` in the editor console. Auto Start Server stays **off** unless the Director
  says otherwise.
- Next safe action: confirm `C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\
  Build.bat` exists, then run T-112.5a step 1 (`Install-VoxelFreeLegacy.ps1 -Force`).
  Full plan: `C:\Users\harja\.claude\plans\whimsical-herding-lagoon.md`.

### T-112.5a evidence — automated checks green under UE 5.8.2

Engine `5.8.2-56702186+++UE5+Release-5.8` (compatible 5.8.0), installed side-by-side
with 5.7.4. Backend: Voxel Plugin Free Legacy **434 / 159fd19a0 / engine 5.8.0**,
prebuilt Win64 binaries, mounted (`LogPluginManager: Mounting Project plugin VoxelFree`).

| CP-006 check | Result under 5.8 |
|---|---|
| `VoxelWorldEditor Win64 Development` build | `Result: Succeeded`, exit 0, 56 actions, 92.7s cold. Zero compile errors |
| Five TerrainCore automation tests | All `Result={Success}`, **zero failures**, `**** TEST COMPLETE. EXIT CODE: 0 ****` at 2026-09-07 01:24:44 UTC |
| Headless boot | `/Script/TerrainCore.TerrainService` resolves as a UClass; `Lvl_ThirdPerson` loads; `VoxelWorld_T101A` present; PlayerStart at (-8228.66, 0, 150) — identical to CP-006. 65 actors. Probe exit 0 |
| `#include` boundary probe | With `#include "VoxelTools/VoxelDataTools.h"` in `Source/TerrainCore/Private/TerrainCore.cpp`: `fatal error C1083: Cannot open include file` / `Result: Failed`. Removed and rebuilt: `Result: Succeeded`. **D-011 remains compiler-enforced on 5.8.** File restored byte-identical to HEAD (md5 verified) |
| Standalone (`Tools/Play-Solo.ps1`) | `World NetMode = Standalone`; `LogVoxel: Voxel Invoker enabled; Name: VoxelInvokerAutoCameraComponent_0`; `No Voxel Invoker found, using camera as invoker`. **Zero `LogVoxel: Error` / fatal lines.** The multiplayer-invoker failure line (R-010) did not appear |
| LMB/RMB dig by hand | **Director-run; outstanding.** Not claimed here |

Evidence logs: `Saved/Logs/VoxelWorld.log` (build/tests/boot) and
`Saved/Logs/Standalone_T101A.log`, both local and gitignored, overwritten by later runs.

**Three findings, all fixed in this increment — none of them were in the plan:**

1. **The 5.8 plugin archive has no wrapping folder.** `VoxelFree.uplugin` sits at the
   archive root, so `$SourceRoot` equalled `$Staging` and the installer's progress
   report threw `Substring: startIndex cannot be larger than length of string` *after*
   a successful 1.56 GB download. Guarded; it now prints `<archive root>`.
2. **`-Force` left the old install inside `Plugins\`.** UnrealBuildTool scans that tree
   recursively, so every module's `Build.cs` was seen twice and the build died with
   `CS0101 ... already contains a definition for 'Voxel'` before compiling anything.
   Backups now go to `Tools\downloads\VoxelFree.bak-<timestamp>-engine<ver>`, outside
   the scanned tree and gitignored. The 5.7/432 install is preserved there as rollback.
3. **Both `Target.cs` files pinned `BuildSettingsVersion.V6` / `Unreal5_7`.** On 5.8 UBT
   refuses this: *"VoxelWorldEditor modifies the values of properties: [
   UnreachableCodeWarningLevel, ReturnTypeWarningLevel, DanglingWarningLevel ]. This is
   not allowed, as VoxelWorldEditor has build products in common with UnrealEditor."*
   Bumped to **V7 / `Unreal5_8`**, the engine's own suggested fix. The alternative,
   `TargetBuildEnvironment.Unique`, would require a source-built engine. **No
   `Source/**` code change was needed** for the new include order — the targets are the
   only source-tree edit, and TerrainCore compiled unmodified.

Also of note: `Install-VoxelFreeLegacy.ps1` is now parametrised `-EngineVersion`
(default `5.8`; `5.7` still reachable), with a per-version known-good fallback table
and README discovery driven by the same value. `Docs/SETUP.md` is the environment build
*record*, so its 5.7 walkthrough is left standing and a header note routes a new machine
to 5.8 — rewriting it would falsify the record.

### T-112.5a — Director hand check: PASS

Director ran the standalone dig path on UE 5.8 (2026-09-06) and reported it worked:
**LMB digs and RMB places.** This is the CP-006 by-hand check, reproduced on 5.8
through the unchanged `BP_ThirdPersonCharacter` wiring. The T-101A input bindings and
the `AddSphere`/`RemoveSphere` path therefore survive both the engine bump and the
plugin bump 432 -> 434.

**"No hill visible" is expected, pre-existing, and not a 5.8 regression.**
`T-101A_FINDINGS.md` §2e already records that `AVoxelWorld::SaveObject` defaults to
null, so voxel edits never reach disk and any process loading the level from disk gets
a bare `VoxelFlatGenerator` plane. §2e's own evidence line is
`took 0.251192s to generate ... no hill` under 5.7; this run logged
`took 0.130172s to generate` and the same flat plane. `place_voxel_world.py` says the
same thing in its comments. The hill exists only inside an editor session that has run
that script. Digging was verified on the flat plane, which is what CP-006 verified too.

**Cold-launch caveat worth carrying.** The first 5.8 standalone launch dropped the
Director through the floor into an endless fall. The log shows why: 150 PSO creation
hitches and six Path Tracing RTPSO compiles of 18-55 seconds each, at spawn, on a cold
DDC. The character spawns at Z+150 above the plane, so a multi-second stall before the
voxel collision mesh exists means an unopposed fall with no floor below. The relaunch
on a warm cache generated the world in 0.130s instead of 2.804s, with no hitch storm,
and played correctly. Not a terrain defect and not caused by the upgrade, but it
compounds the R-010/T-101B "no KillZ, no respawn volume" gap already on record: today
the only recovery from a fall is to quit. Worth a KillZ before anyone plays for real.

## T-112.5b — Unreal MCP adopted, editor-only

Both plugins are engine plugins shipped with 5.8 at
`Engine/Plugins/Experimental/ModelContextProtocol` and
`Engine/Plugins/Experimental/Toolsets/AllToolsets`. Both are marked
`IsExperimentalVersion: true` and `EnabledByDefault: false` by Epic.

**The D-025 guard is load-bearing, not decorative.** `ModelContextProtocol.uplugin`
declares **Runtime** modules (`ModelContextProtocol`, `ModelContextProtocolEngine`)
alongside its Editor ones. Nothing about the plugin is inherently editor-only, so the
`"TargetAllowList": ["Editor"]` on both entries in `VoxelWorld.uproject` is the only
thing keeping them out of a game target. AGENTS §9 now says so explicitly.

| Check | Result |
|---|---|
| `VoxelWorldEditor Win64 Development` with plugins enabled | `Result: Succeeded`, exit 0 |
| `VoxelWorld Win64 Development` (game target) | `Result: Succeeded`, exit 0, 73.1s |
| Guard proven from the build receipts | `VoxelWorldEditor.target` `BuildPlugins` (278) contains `ModelContextProtocol`, `AllToolsets`, `ToolsetRegistry` and 22 toolsets, with 70 matching build products. `VoxelWorld.target` `BuildPlugins` (234) contains **none of them** and **0** matching build products |
| Five TerrainCore tests with MCP enabled | All `Result={Success}`, zero failures, `**** TEST COMPLETE. EXIT CODE: 0 ****` at 2026-09-07 02:18:39 UTC, process exit 0 |
| `grep -ri "ModelContextProtocol\|StartServer\|AllToolsets" Source/` | **no hits** |
| `.mcp.json` generated | `LogModelContextProtocol: Display: MCP client configuration written to: .../Dev/VoxelWorld/.mcp.json`. 114 bytes: one `unreal-mcp` http entry at `http://127.0.0.1:8000/mcp`. No token, no credential — safe to commit under AGENTS §8 |
| MCP server live | Started with `ModelContextProtocol.StartServer`; port 8000 listening; a JSON-RPC `initialize` POST returned **HTTP 200** with `protocolVersion 2025-06-18` and a `tools.listChanged` capability. 52 toolsets registered as discoverable |

**Auto Start Server is left OFF.** The server is started deliberately with
`ModelContextProtocol.StartServer`. It binds loopback with **no authentication**, so an
always-on server in an editor that is often merely open is not a default worth having.
The Director can overrule this in Editor Preferences > General > Model Context Protocol.

**Console-command gotcha for whoever automates this next.** `-ExecCmds` splits on
commas, not semicolons, for this command: `-ExecCmds=... ClaudeCode; Quit` passes the
client name as `ClaudeCode;` and the plugin answers
`Unknown client "ClaudeCode;". Supported: ClaudeCode, Cursor, VSCode, Gemini, Codex, All`.
With a comma the config generates, but the editor then never processes `Quit` and idles
until killed. `Automation RunTests TerrainCore; Quit` still exits cleanly with the
semicolon. Use the semicolon for automation runs and the comma for the config
generation, and expect to kill the process after the latter.

**Limits.** Unreal MCP is Epic-Experimental; its APIs and formats may change, which is a
RISKS line against R-008 rather than a blocker for a dev-time tool. No gameplay, terrain,
architecture or numbered decision changed. UE 5.8's Mesh Terrain was not evaluated; it
stays a D-025 watch item with no evidence either way.

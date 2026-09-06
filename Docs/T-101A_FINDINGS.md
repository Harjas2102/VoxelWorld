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

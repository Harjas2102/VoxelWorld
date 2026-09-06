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

## 3. Useful assets discovered

| Asset | Why it matters |
|---|---|
| `/Voxel/Examples/VoxelGraphs/IQNoise/VoxelExample_IQNoise` | Hilly noise generator — the T-101A test terrain |
| `/Voxel/Examples/Materials/RGB/M_VoxelMaterial_Colors` | Matches the default `RGB` material config |
| `/Voxel/Examples/Maps/Tools/HighResolutionDigging` | A working digging demo to compare against |
| `/Voxel/Examples/Maps/Multiplayer/VoxelExample_TcpMultiplayerMap` | The edit-sync reference above |
| `/Voxel/Examples/VoxelGraphs/Cliffs`, `Cave`, `Ravines`, `Erosion` | Generator references for strata and an ore body (1C) |
| 106 `VoxelGraphGenerator` assets total | A large worked-example library |

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

---

## 5. PIE run results *(to be filled in from the PIE run)*

- **Dig / add work in PIE:** *pending*
- **Tunnel or overhang achieved:** *pending* — the one that proves the representation is
  genuinely volumetric and not a deformed heightfield
- **Terrain reads as smooth, not blocky:** *pending* (Pillar 2)
- **Editing feel / latency:** *pending*
- **Collision correct after edits** (cannot walk on removed ground, added ground is
  solid): *pending*
- **New errors in the log:** *pending*
- **Screenshots:** *pending*

## 6. Verdict *(to be filled in from the PIE run)*

*Does Free Legacy clear the bar to continue into T-101B? Yes / No / With caveats.*

Remember what this verdict is and is not: passing T-101A means the backend is worth
testing properly. **It does not adopt it.**

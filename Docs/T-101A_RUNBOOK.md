# T-101A_RUNBOOK.md — Dig the first hole

> Re-followable procedure for the terrain smoke test. If T-101A has to be redone on a
> fresh machine or a fresh clone, start at step 1 and work down.

**Task:** T-101A · **Written:** CP-002 (2026-09-05) · **Target:** one evening
**7-day rule (R-009):** if the hole is not dug by **2026-09-12**, the step is too big —
say so and it gets cut smaller. That is the rule working, not a failure.

**Scope reminder (D-013):** this is a *solo* smoke test and it proves the plugin can
render and edit terrain. It proves **nothing** about multiplayer, persistence, or yield.
Those are T-101B, and no backend is adopted on the strength of a solo dig.

---

## Step 1 — Install the plugin *(already done; here for reproducibility)*

Voxel Plugin Free Legacy **v432 / `e9648b302` / engine 5.7.0** is installed at
`Plugins\VoxelFree\` with prebuilt Win64 binaries, so no compiler is needed to open the
editor. It is **gitignored** — the binaries are hundreds of MB of `.pdb`.

To reinstall it (fresh clone, new machine):

```powershell
cd C:\Dev\VoxelWorld
.\Tools\Install-VoxelFreeLegacy.ps1
```

The script is idempotent: if the plugin is already present it prints the version and
exits without downloading. It fetches the current 5.7 binaries link from the project
README rather than trusting a hardcoded URL, and inspects the archive's real internal
layout before normalizing it into `Plugins\VoxelFree\`.

If the download fails it prints the exact URL to fetch in a browser, then:

```powershell
.\Tools\Install-VoxelFreeLegacy.ps1 -ZipPath C:\Users\harja\Downloads\<the-zip>.zip
```

Source of truth: <https://github.com/VoxelPlugin/VoxelPluginFreeLegacy>

## Step 2 — Plugins are enabled in `VoxelWorld.uproject`

Already done at CP-002. The `Plugins` array contains `VoxelFree`, `PythonScriptPlugin`,
and `EditorScriptingUtilities`.

- `VoxelFree` — the terrain backend. A plugin in a project's `Plugins\` folder is
  enabled by default, so this entry is documentation rather than a switch.
- `PythonScriptPlugin` and `EditorScriptingUtilities` — both ship **disabled** in the
  engine and must be listed explicitly. They are what let the agent do editor work for
  you instead of giving you menu paths (OPERATIONS.md section 9).

`VoxelPluginInstaller` was **removed** at CP-002: it installs the paid Voxel Plugin 2,
which requires owning Voxel Plugin Pro Legacy, and it provides no terrain of its own.
The engine-level install is untouched, so it can be re-enabled if a VP2 upgrade is ever
justified (D-010, R-008).

## Step 3 — Open the editor and check the log

```powershell
& "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" "C:\Dev\VoxelWorld\VoxelWorld.uproject"
```

Then confirm the plugins mounted:

```powershell
Select-String -Path C:\Dev\VoxelWorld\Saved\Logs\VoxelWorld.log -Pattern "Mounting Project plugin VoxelFree|Mounting Engine plugin PythonScriptPlugin|Mounting Engine plugin EditorScriptingUtilities"
```

**Expected:** three lines, one per plugin.

If shader compilation starts (a counter in the bottom-right of the editor), **wait it
out** — it is normal after a plugin change and only happens once.

**Known and harmless:** an `ensure` on `AVoxelWorld::DestroyWorldInternal`
(GeneratorCache) fires at editor *shutdown*. It is logged as an error with a callstack.
It is cosmetic. Do not chase it.

## Step 4 — Plugin API reference

Gathered from the plugin headers and verified against the live `unreal` Python module.
Full notes in `Docs/T-101A_FINDINGS.md`. The names that matter:

| Thing | Name |
|---|---|
| Terrain actor | `AVoxelWorld` (`unreal.VoxelWorld`) |
| Dig | `UVoxelSphereTools::RemoveSphere` (BP: **Remove Sphere**) |
| Add | `UVoxelSphereTools::AddSphere` (BP: **Add Sphere**) |
| Paint material | `SetMaterialSphere` · Smooth: `SmoothSphere` · Flatten: see `VoxelLevelTools` |
| **Yield hook** | every sphere tool returns `TArray<FModifiedVoxelValue>& ModifiedValues` |
| Save / rebuild | `UVoxelBlueprintLibrary::SaveFrame`, `Recreate`, `HasMaterialData` |
| Multiplayer reference | `VoxelMultiplayer/VoxelMultiplayerTcp.h`, `/Voxel/Examples/Maps/Multiplayer/` |

`ModifiedValues` is the single most important find of T-101A: it is the hook D-011 needs
to compute resource yield from material actually removed, rather than asking the rendered
mesh what disappeared.

## Step 5 — Place and configure the terrain (scripted; no menus)

1. Open the level: **Content Browser → Content → Maps → `VoxelSandbox`** (double-click).
2. **Window → Output Log.**
3. In the box at the bottom of the Output Log, change the dropdown from **Cmd** to
   **Python**, then paste and press Enter:

```
C:\Dev\VoxelWorld\Tools\Editor\place_voxel_world.py
```

Or leave the dropdown on **Cmd** and type:

```
py C:\Dev\VoxelWorld\Tools\Editor\place_voxel_world.py
```

**Expected output** (verified by running it):

```
[place_voxel_world]    voxel_size            = 50.0 cm
[place_voxel_world]    generator             = VoxelFlatGenerator (C++; graphs are Pro-only)
[place_voxel_world]    voxel_material        = BasicShapeMaterial
[place_voxel_world]    world_size_in_voxel   = 1024  (render_octree_depth 5)
[place_voxel_world]    -> 512 m across at 50 cm voxels
[place_voxel_world]    recreated (existing voxel edits DISCARDED - clean baseline)
[place_voxel_world] Sculpting a hill (add_sphere)
[place_voxel_world]    +sphere r=3000   at (0, 0, 0)  -> 514627 voxels changed
[place_voxel_world]    ... (5 spheres)
[place_voxel_world]    total voxels modified: 861781
```

> ### ⚠️ Why not a noise generator?
> **Voxel Graphs require Voxel Plugin Pro.** Assigning any of the 106 example graph
> generators gives you `Voxel: Running Voxel Graphs require Voxel Plugin Pro` and an
> **empty world — blue sky, no error**. Free ships exactly two runnable generators, both
> C++: `VoxelFlatGenerator` and `VoxelEmptyGenerator`.
>
> So the script generates flat ground and then **sculpts the hill with `AddSphere`** —
> the same tool the gameplay will use. Full write-up in `T-101A_FINDINGS.md` section 2b.
>
> `PRESERVE_EDITS = False` at the top of the script means a re-run **discards voxel
> edits** and rebuilds a clean baseline. Correct for first setup. **Flip it to `True`
> once you have digging worth keeping.**

4. **Press Ctrl+S to save the level.** The script never saves for you.
5. You should see a **grey hill** around the origin. If the viewport looks empty, select
   `VoxelWorld_T101A` in the World Outliner and press **F** to focus it.

**About the numbers:** 50 cm voxels across 1024 voxels is a **512 m** world — the upper
end of the GDD's 256–512 m first test world. 50 cm rather than the default 100 cm matters
for the next step: at 100 cm voxels a 100 cm dig brush removes a *single* voxel and looks
like nothing happened.

**About duplicates:** if the level holds more than one `AVoxelWorld`, overlapping worlds
make the test meaningless, so the script keeps the first and removes the rest. To keep
them, set `CLEAN_DUPLICATES = False` at the top of the script.

## Step 6 — Wire up digging (Blueprint)

No C++ and no compiler — T-100 is not required. **No new assets either:** plain key-event
nodes are used, so no Input Action asset and no edit to `IMC_Default`.

Open **Content → ThirdPerson → Blueprints → `BP_ThirdPersonCharacter`** (double-click),
then in the **Event Graph**:

**A. Cache the voxel world (once, on BeginPlay)**

1. Find the existing **Event BeginPlay** node, or right-click empty graph space, search
   `Event BeginPlay`, add it.
2. Drag off its white execution pin, search **Get Actor Of Class**, add it.
3. On that node, set **Actor Class** to **Voxel World**.
4. Drag off its blue **Return Value** pin → **Promote to Variable**.
5. Rename the new variable **`TargetVoxelWorld`** (F2 in the My Blueprint panel).

**B. Left mouse = dig**

6. Right-click empty graph space, search **Left Mouse Button**, add the key event node.
   (This is the plain key event, not an Input Action — nothing to create.)
7. Right-click, search **Get Player Camera Manager**, add it. Leave Player Index 0.
8. Drag off its Return Value → search **Get Camera Location** → add.
9. Drag off the same Return Value → search **Get Actor Forward Vector** → add.
10. Drag off the Forward Vector output → search **Multiply** (vector × float) → set the
    float to **1000.0**.
11. Right-click, search **Add** (vector + vector) → plug **Camera Location** into A and
    the multiply result into B. This is your trace End.
12. Right-click, search **Line Trace By Channel**, add it.
    - **Start** ← Get Camera Location
    - **End** ← the Add result
    - Check **Draw Debug Type → For Duration** while testing so you can see the trace.
13. Connect **Left Mouse Button → Pressed** into **Line Trace By Channel**'s exec input.
14. Drag off Line Trace's **exec** output, search **Remove Sphere**, add it.
    (Category: *Voxel → Tools → Sphere Tools*.)
15. Wire **Remove Sphere**:
    - **Voxel World** ← drag in the `TargetVoxelWorld` variable
    - **Position** ← drag off Line Trace's **Out Hit** pin → **Break Hit Result** →
      **Location**
    - **Radius** ← **200.0**
16. Optionally connect Line Trace's red **Return Value** (bool) through a **Branch** so
    the tool only fires on a hit.

**C. Right mouse = add**

17. Repeat steps 6–15 with a **Right Mouse Button** event and **Add Sphere** instead of
    **Remove Sphere**. Same Position, same Radius 200.0.

18. Click **Compile**, then **Save**.

**Screenshot the graph and send it** — the agent verifies it against the intended wiring
before you spend time debugging in PIE.

**If a node will not appear in the search:** uncheck **Context Sensitive** in the
top-right of the node search box.

**Alternative if key-event nodes feel wrong:** legacy action mappings can be added to
`Config\DefaultInput.ini` (`+ActionMappings=(ActionName="VoxelDig",Key=LeftMouseButton)`)
and used with `InputAction VoxelDig` nodes — also no asset creation. Ask for this if
preferred.

## Step 7 — Test in PIE and record the result

1. Press **Play**.
2. Walk to a hillside. **Left-click** to dig, **right-click** to add.
3. Get all three of these:
   - a clear **hole**
   - a clear **mound**
   - a **tunnel or overhang** — dig horizontally into a slope, then look at it from
     outside. This is the one that actually matters: it is what proves the
     representation is genuinely volumetric rather than a deformed heightfield.
4. **Screenshot each.**
5. Press **Esc**.
6. Check the log for anything new:

```powershell
Select-String -Path C:\Dev\VoxelWorld\Saved\Logs\VoxelWorld.log -Pattern "Error:" | Select-Object -Last 20
```

Ignore the known shutdown `ensure` from step 3.

## Step 8 — Definition of done

T-101A is done when **all** of these are true:

1. Screenshot of a dug hole **and** an added mound in PIE.
2. Screenshot of a tunnel or overhang.
3. No errors in `Saved\Logs\VoxelWorld.log` beyond the known shutdown `ensure`.
4. Script and `.uproject` changes committed and pushed.
5. `Docs\T-101A_FINDINGS.md` completed — its "to be filled in from the PIE run" sections
   answered with what actually happened.

Then say `checkpoint`, and the next task is the blind benchmark
(`Docs/DUAL_AGENT_SETUP.md` section 6), which produces ARCHITECTURE v1, D-017 and D-018
before any terrain code gets written.

## If you get stuck

Type `stuck` and paste a screenshot plus `Saved\Logs\VoxelWorld.log`. That is the
intended path, not a failure (OPERATIONS.md section 9).

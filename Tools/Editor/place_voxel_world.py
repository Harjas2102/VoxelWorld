"""
place_voxel_world.py — configure the T-101A test terrain without touching a menu.

WHAT IT DOES
    Finds the AVoxelWorld actor in the currently open level (or spawns one at the
    origin if there is none), configures it into the 512 m test hill the GDD asks
    for, and rebuilds it. Optionally removes duplicate voxel worlds, because more
    than one overlapping voxel world makes the smoke test meaningless.

HOW TO RUN
    1. Open the VoxelSandbox level in the editor.
    2. Window > Output Log, then in the Cmd box at the bottom switch the dropdown
       from "Cmd" to "Python", or just type:
         py C:\\Dev\\VoxelWorld\\Tools\\Editor\\place_voxel_world.py
    3. Read the summary it prints.
    4. Press Ctrl+S to save the level. THIS SCRIPT NEVER SAVES FOR YOU.

WHY THESE NUMBERS
    voxel_size 50 cm with a 1024-voxel world = a 512 m square world, the upper end
    of the GDD's "256-512 m first test world". 50 cm voxels matter: at the default
    100 cm a 100 cm dig brush removes a single voxel and looks like nothing. At
    50 cm, the 200 cm brush in the runbook clears ~4 voxels and reads as a hole.

    Project: VoxelWorld · Task: T-101A · Added at CP-002 (2026-09-05)
    Every API name here was verified against the live `unreal` module, not guessed.
"""

import unreal

# --- Tunables ----------------------------------------------------------------
GENERATOR_PATH = "/Voxel/Examples/VoxelGraphs/IQNoise/VoxelExample_IQNoise"
MATERIAL_PATH = "/Voxel/Examples/Materials/RGB/M_VoxelMaterial_Colors"
VOXEL_SIZE = 50.0          # cm per voxel
WORLD_SIZE_VOXELS = 1024   # 1024 * 50 cm = 512 m across
ENABLE_COLLISIONS = True
CREATE_WORLD_AUTOMATICALLY = True   # required, or PIE starts with no terrain
CLEAN_DUPLICATES = True    # set False to keep every existing voxel world
ACTOR_LABEL = "VoxelWorld_T101A"

_LOG = []


def say(msg):
    _LOG.append(msg)
    unreal.log("[place_voxel_world] " + str(msg))


def warn(msg):
    _LOG.append("WARNING: " + str(msg))
    unreal.log_warning("[place_voxel_world] " + str(msg))


def load(path, what):
    """Load an asset, forcing a registry scan first (plugin content is often unscanned)."""
    try:
        ar = unreal.AssetRegistryHelpers.get_asset_registry()
        ar.scan_paths_synchronous([path.rsplit("/", 1)[0]], force_rescan=False)
    except Exception:
        pass
    try:
        asset = unreal.load_asset(path)
    except Exception as exc:
        warn("could not load %s (%s): %r" % (what, path, exc))
        return None
    if asset is None:
        warn("%s not found at %s" % (what, path))
    return asset


def main():
    say("=" * 62)
    say("T-101A terrain setup")
    say("=" * 62)

    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    # --- 1. What is already in this level? -----------------------------------
    existing = [a for a in actor_sub.get_all_level_actors()
                if isinstance(a, unreal.VoxelWorld)]
    say("Existing AVoxelWorld actors in this level: %d" % len(existing))
    for a in existing:
        say("   - %s at %s" % (a.get_actor_label(), a.get_actor_location()))

    if not existing:
        say("None found. Spawning one at the origin.")
        world = actor_sub.spawn_actor_from_class(
            unreal.VoxelWorld, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))
        if world is None:
            warn("spawn failed - aborting.")
            return
    else:
        world = existing[0]
        say("Reusing '%s'." % world.get_actor_label())
        extras = existing[1:]
        if extras:
            if CLEAN_DUPLICATES:
                warn("Removing %d duplicate voxel world(s) - overlapping worlds make "
                     "the smoke test meaningless. Set CLEAN_DUPLICATES=False to keep them."
                     % len(extras))
                for a in extras:
                    say("   removing %s" % a.get_actor_label())
                    actor_sub.destroy_actor(a)
            else:
                warn("%d duplicate voxel world(s) left in place; they will overlap."
                     % len(extras))

    try:
        world.set_actor_label(ACTOR_LABEL)
    except Exception:
        pass
    try:
        world.set_actor_location(unreal.Vector(0.0, 0.0, 0.0), False, False)
    except Exception:
        pass

    # --- 2. Configure --------------------------------------------------------
    say("-" * 62)
    say("Configuring")

    world.set_editor_property("voxel_size", VOXEL_SIZE)
    say("   voxel_size            = %s cm" % world.get_editor_property("voxel_size"))

    generator = load(GENERATOR_PATH, "generator graph")
    if generator is not None:
        world.set_generator_object(generator)
        say("   generator             = %s" % generator.get_name())
    else:
        warn("keeping the existing generator; terrain may be empty or flat.")

    material = load(MATERIAL_PATH, "RGB voxel material")
    if material is not None:
        try:
            world.set_editor_property("voxel_material", material)
            say("   voxel_material        = %s" % material.get_name())
        except Exception as exc:
            warn("could not set voxel_material: %r" % (exc,))
    else:
        warn("no material set - terrain will render with the default material. "
             "Cosmetic only; digging still works.")

    world.set_world_size(WORLD_SIZE_VOXELS)
    size_v = world.get_editor_property("world_size_in_voxel")
    depth = world.get_editor_property("render_octree_depth")
    say("   world_size_in_voxel   = %s  (render_octree_depth %s)" % (size_v, depth))
    say("   -> %.0f m across at %.0f cm voxels" % (size_v * VOXEL_SIZE / 100.0, VOXEL_SIZE))

    world.set_editor_property("enable_collisions", ENABLE_COLLISIONS)
    world.set_editor_property("create_world_automatically", CREATE_WORLD_AUTOMATICALLY)
    say("   enable_collisions     = %s" % ENABLE_COLLISIONS)
    say("   create_world_auto     = %s   (needed or PIE starts with no terrain)"
        % CREATE_WORLD_AUTOMATICALLY)

    # --- 3. Rebuild ----------------------------------------------------------
    say("-" * 62)
    say("Rebuilding")
    # Most of the properties above are marked "Recreate": the world must be rebuilt
    # for them to take effect. create_world() needs an FVoxelWorldCreateInfo, so it
    # is only used when the world is not already created.
    try:
        if world.is_created():
            unreal.VoxelBlueprintLibrary.recreate(world, save_data=True)
            say("   recreated (existing edits preserved)")
        else:
            world.create_world(unreal.VoxelWorldCreateInfo())
            say("   created")
    except Exception as exc:
        warn("rebuild failed: %r" % (exc,))
        warn("Fallback: select the actor and use the 'Toggle' button in its Details panel.")

    say("   is_created = %s" % world.is_created())

    # --- 4. What to do next --------------------------------------------------
    say("=" * 62)
    say("DONE. Next:")
    say("  1. Press Ctrl+S to SAVE THE LEVEL. This script does not save.")
    say("  2. You should see hilly terrain around the origin in the viewport.")
    say("     If the viewport looks empty, press F to focus the selected actor.")
    say("  3. Wire up digging: Docs/T-101A_RUNBOOK.md, step 6.")
    say("=" * 62)


main()

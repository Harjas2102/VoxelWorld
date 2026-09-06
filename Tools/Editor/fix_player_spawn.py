r"""
fix_player_spawn.py — move the player spawn out of the sculpted hill.

WHY THIS EXISTS
    place_voxel_world.py sculpts the T-101A hill from a sphere of radius 3000
    (30 m) centred on the voxel world's origin. The UE ThirdPerson template's
    PlayerStart sits about 12 m from that same origin, so PIE spawns the player
    encased in solid voxel: no view, no movement. Symptom reported at T-101A:
    "I don't see the hillside and I can't leave spawn."

WHAT IT DOES
    Finds the voxel world, computes how far the sculpted hill actually reaches in
    XY, and moves every PlayerStart (and any character pawn placed directly in the
    level) to open flat ground west of it, facing the hill. Then saves the level.

HOW TO RUN
    Output Log > switch the dropdown from "Cmd" to "Python", or leave it on Cmd:
      py C:\Dev\VoxelWorld\Tools\Editor\fix_player_spawn.py

    Unlike place_voxel_world.py, THIS SCRIPT SAVES THE LEVEL for you and prints
    whether the save succeeded.

    Project: VoxelWorld · Task: T-101A · Added 2026-09-06
"""

import math
import unreal

# --- Tunables ----------------------------------------------------------------
# MUST MATCH the `lumps` list in place_voxel_world.py. If you re-sculpt with
# different spheres, update this too or the spawn may land inside the new terrain.
HILL_LUMPS = [
    (0.0, 0.0, 0.0, 3000.0),
    (0.0, 0.0, 1400.0, 2400.0),
    (600.0, -400.0, 2600.0, 1500.0),
    (2800.0, 1800.0, 200.0, 1900.0),
    (-2400.0, -2000.0, 100.0, 1700.0),
]
CLEARANCE = 3000.0   # cm of open ground between the spawn and the hill's edge
SPAWN_Z = 150.0      # cm above the flat generator's ground plane (surface = actor Z)
LANE_SPACING = 400.0 # cm between multiple PlayerStarts so they cannot overlap
SAVE_LEVEL = True


def say(msg):
    unreal.log("[fix_player_spawn] " + str(msg))


def warn(msg):
    unreal.log_warning("[fix_player_spawn] " + str(msg))


def hill_reach():
    """Farthest the sculpted hill extends from the voxel world origin, in XY."""
    reach = 0.0
    for dx, dy, _dz, radius in HILL_LUMPS:
        reach = max(reach, math.hypot(dx, dy) + radius)
    return reach


def inside_hill(loc, origin):
    """Is this world-space point inside any sculpted sphere? Returns the lump or None."""
    for dx, dy, dz, radius in HILL_LUMPS:
        cx, cy, cz = origin.x + dx, origin.y + dy, origin.z + dz
        d = math.sqrt((loc.x - cx) ** 2 + (loc.y - cy) ** 2 + (loc.z - cz) ** 2)
        if d <= radius:
            return (dx, dy, dz, radius, d)
    return None


def main():
    say("=" * 62)
    say("T-101A spawn fix")
    say("=" * 62)

    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    all_actors = actor_sub.get_all_level_actors()

    # --- 1. Locate the voxel world ------------------------------------------
    worlds = [a for a in all_actors if isinstance(a, unreal.VoxelWorld)]
    if not worlds:
        warn("No AVoxelWorld in this level. Are you in the level you ran")
        warn("place_voxel_world.py against? Nothing changed.")
        return
    world = worlds[0]
    origin = world.get_actor_location()
    say("Voxel world '%s' at (%.0f, %.0f, %.0f)"
        % (world.get_actor_label(), origin.x, origin.y, origin.z))

    reach = hill_reach()
    safe_x = origin.x - (reach + CLEARANCE)
    say("Hill reaches %.0f cm (%.1f m) from that origin in XY." % (reach, reach / 100.0))
    say("Safe ground starts at X = %.1f (%.1f m west of the hill's edge)."
        % (safe_x, CLEARANCE / 100.0))

    # --- 2. Find everything that decides where the player appears ------------
    # unreal.PlayerStart is normally exposed, but resolve it defensively: if the
    # binding is missing, fall back to matching the class name so the script still
    # does its job instead of dying on an AttributeError.
    ps_class = getattr(unreal, "PlayerStart", None)
    if ps_class is not None:
        starts = [a for a in all_actors if isinstance(a, ps_class)]
    else:
        warn("unreal.PlayerStart binding missing - matching by class name instead.")
        starts = [a for a in all_actors
                  if a.get_class().get_name() in ("PlayerStart", "PlayerStartPIE")]
    pawns = [a for a in all_actors if isinstance(a, unreal.Character)]
    say("-" * 62)
    say("Found %d PlayerStart(s) and %d placed character pawn(s)." % (len(starts), len(pawns)))

    if not starts and not pawns:
        say("No PlayerStart in this level. Spawning one on safe ground.")
        if ps_class is None:
            warn("cannot spawn a PlayerStart without the binding - aborting.")
            return
        made = actor_sub.spawn_actor_from_class(
            ps_class,
            unreal.Vector(safe_x, origin.y, origin.z + SPAWN_Z),
            unreal.Rotator(0.0, 0.0, 0.0))   # yaw 0 = facing +X, toward the hill
        if made is None:
            warn("spawn failed - aborting.")
            return
        made.set_actor_label("PlayerStart_T101A")
        starts = [made]

    # --- 3. Move them onto open ground, facing the hill ----------------------
    say("-" * 62)
    moved = 0
    moved_actors = []
    for i, actor in enumerate(starts + pawns):
        before = actor.get_actor_location()
        hit = inside_hill(before, origin)
        if hit:
            dx, dy, dz, radius, d = hit
            say("%s was BURIED - %.0f cm inside the r=%.0f sphere at (%.0f, %.0f, %.0f)."
                % (actor.get_actor_label(), radius - d, radius, dx, dy, dz))
        else:
            say("%s is already clear of the hill (expected on a re-run); "
                "re-placing it so the test always starts from one known spot."
                % actor.get_actor_label())

        target = unreal.Vector(safe_x,
                               origin.y + (i * LANE_SPACING),
                               origin.z + SPAWN_Z)
        actor.set_actor_location(target, False, False)
        actor.set_actor_rotation(unreal.Rotator(0.0, 0.0, 0.0), False)
        say("   -> (%.0f, %.0f, %.0f), facing +X toward the hill."
            % (target.x, target.y, target.z))
        moved_actors.append(actor)
        moved += 1

    # --- 4. Save -------------------------------------------------------------
    say("-" * 62)
    if SAVE_LEVEL:
        # Under One File Per Actor each actor lives in its OWN package, separate
        # from the .umap. Saving the level does not save them, so the move would
        # exist only in editor memory: PIE looks right (it duplicates the
        # in-memory world) while a standalone launch, which loads from disk,
        # still spawns the player inside the hill.
        #
        # NOTE: it must be actor.get_package(), NOT actor.get_outer().get_outermost().
        # The actor's outer is the ULevel, whose outermost is the MAP package - that
        # returns /Game/.../Lvl_ThirdPerson and silently saves the wrong thing.
        level_pkg_name = None
        try:
            level_pkg_name = unreal.get_editor_subsystem(
                unreal.UnrealEditorSubsystem).get_editor_world().get_name()
        except Exception:
            pass

        packages = []
        for actor in moved_actors:
            pkg = None
            for accessor in ("get_package", "get_outermost"):
                fn = getattr(actor, accessor, None)
                if fn is None:
                    continue
                try:
                    pkg = fn()
                except Exception:
                    pkg = None
                if pkg is not None:
                    break
            if pkg is None:
                warn("could not resolve a package for %s" % actor.get_actor_label())
                continue
            name = pkg.get_name()
            if pkg not in packages:
                packages.append(pkg)
                marker = ""
                if level_pkg_name and name.endswith("/" + level_pkg_name):
                    marker = "   <- this is the MAP package, not an external actor"
                say("   package: %s%s" % (name, marker))

        if packages:
            try:
                ok = unreal.EditorLoadingAndSavingUtils.save_packages(packages, False)
                say("Actor package(s) SAVED." if ok
                    else "save_packages() returned False - see the warnings above.")
            except Exception as exc:
                warn("saving actor packages failed: %r" % (exc,))

        try:
            les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
            ok = les.save_current_level()
            say("Level SAVED." if ok else "save_current_level() returned False.")
        except Exception as exc:
            warn("could not save the level (%r) - press Ctrl+S yourself." % (exc,))

        # Safety net: anything the two calls above missed.
        try:
            unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
        except Exception as exc:
            warn("save_dirty_packages failed: %r" % (exc,))

        # Verify by asking the editor what is STILL dirty. (unreal.Package has no
        # is_dirty() binding - that was a bug in the previous version of this file.)
        try:
            dirty = set()
            for getter in ("get_dirty_map_packages", "get_dirty_content_packages"):
                fn = getattr(unreal.EditorLoadingAndSavingUtils, getter, None)
                if fn is None:
                    continue
                for pkg in (fn() or []):
                    dirty.add(pkg.get_name())
            leftover = [p.get_name() for p in packages if p.get_name() in dirty]
            if leftover:
                warn("STILL UNSAVED: %s" % ", ".join(leftover))
                warn("Use File > Save All before launching standalone.")
            else:
                say("Verified: nothing we touched is still dirty.")
        except Exception as exc:
            warn("could not verify dirty state (%r) - harmless; the saves above "
                 "already ran." % (exc,))
    else:
        say("SAVE_LEVEL is False - press Ctrl+S yourself.")

    say("=" * 62)
    say("DONE. Moved %d actor(s)." % moved)
    say("Now launch standalone - NOT PIE, which is still on the 3-player")
    say("replication settings and gives you no terrain (RISKS.md R-010):")
    say(r"    .\Tools\Play-Solo.ps1")
    say("You should be on flat grey ground, hill centre ~%.0f m ahead."
        % ((reach + CLEARANCE) / 100.0))
    say("=" * 62)


main()

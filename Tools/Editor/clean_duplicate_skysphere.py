r"""
clean_duplicate_skysphere.py - remove the accidental duplicate sky sphere.

WHY THIS EXISTS
    A second SM_SkySphere StaticMeshActor appeared in Lvl_ThirdPerson during the
    T-101A session (external actor package D/GU/..., created 2026-09-06 00:41).
    The original is tracked in git from 2026-06-12; this one is a stray duplicate,
    most likely an accidental Ctrl+D or alt-drag in the viewport. Two overlapping
    sky spheres are harmless to look at and pure noise to commit.

    Keeps the actor whose label is exactly "SM_SkySphere" if there is one, else the
    first found. Deletes the rest. Reports everything before acting.

HOW TO RUN
    py C:\Dev\VoxelWorld\Tools\Editor\clean_duplicate_skysphere.py

    Project: VoxelWorld - Task: T-101A - Added 2026-09-06
"""

import unreal

MESH_NAME = "SM_SkySphere"
DRY_RUN = False


def say(msg):
    unreal.log("[clean_skysphere] " + str(msg))


def warn(msg):
    unreal.log_warning("[clean_skysphere] " + str(msg))


def mesh_name_of(actor):
    comp = actor.get_component_by_class(unreal.StaticMeshComponent)
    if comp is None:
        return None
    mesh = comp.get_editor_property("static_mesh")
    return mesh.get_name() if mesh is not None else None


def main():
    say("=" * 62)
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    found = []
    for actor in actor_sub.get_all_level_actors():
        if not isinstance(actor, unreal.StaticMeshActor):
            continue
        try:
            if mesh_name_of(actor) == MESH_NAME:
                found.append(actor)
        except Exception as exc:
            warn("could not inspect %s: %r" % (actor.get_actor_label(), exc))

    say("Found %d actor(s) using %s:" % (len(found), MESH_NAME))
    for a in found:
        say("   - label='%s'  package=%s" % (a.get_actor_label(), a.get_package().get_name()))

    if len(found) < 2:
        say("Nothing to clean - %d found, expected 2+ for a duplicate." % len(found))
        say("=" * 62)
        return

    keep = None
    for a in found:
        if a.get_actor_label() == MESH_NAME:
            keep = a
            break
    if keep is None:
        keep = found[0]
    say("Keeping '%s'." % keep.get_actor_label())

    doomed = [a for a in found if a is not keep]
    if DRY_RUN:
        warn("DRY_RUN is True - would delete %d actor(s), doing nothing." % len(doomed))
        say("=" * 62)
        return

    for a in doomed:
        say("   deleting '%s'" % a.get_actor_label())
        actor_sub.destroy_actor(a)

    try:
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
        say("Level saved.")
    except Exception as exc:
        warn("save failed (%r) - use File > Save All." % (exc,))

    say("Deleted %d duplicate(s)." % len(doomed))
    say("=" * 62)


main()

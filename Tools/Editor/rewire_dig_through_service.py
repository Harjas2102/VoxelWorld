"""
rewire_dig_through_service.py — T-113 / build step 2: route digging through the service.

WHAT IT DOES
    Rewrites BP_ThirdPersonCharacter's event graph so that Left and Right Mouse Button call
    UTerrainInteractionLibrary::RequestTerrainEditFromView instead of calling the voxel
    plugin directly, and deletes every plugin reference the asset carried.

WHY IT EXISTS AS A SCRIPT
    AGENTS.md section 11: prefer config edits, then C++, then Python-scripted editor actions,
    then Blueprint node instructions, then menu clicks. This is the third rung. It is also
    re-runnable and idempotent, which a set of click instructions is not — and the asset it
    edits is version-controlled, so a bad run is `git checkout` away from undone.

WHAT IT REMOVES, AND WHY THAT IS THE WHOLE POINT
    T-101A wired digging as: Left Mouse Button -> camera line trace -> BreakHitResult ->
    UVoxelSphereTools::RemoveSphere, against an AVoxelWorld held in a `TargetVoxelWorld`
    variable that Event BeginPlay filled with GetActorOfClass(VoxelWorld). That is:

      - a direct plugin call from gameplay, which D-011, AGENTS.md section 4 and the
        section 9 drift guard all forbid by name; and
      - client-authoritative terrain editing, which AGENTS.md section 4 forbids outright; and
      - a plugin type referenced from an ASSET, which ARCHITECTURE.md 7.4 forbids separately
        and which no compiler can catch: "no gameplay code and no Blueprint asset holds a
        reference to an adapter class, so a backend swap needs no gameplay or asset change."

    STATE.md has carried both drift checks as FLAGGED since T-101A on the explicit
    understanding that the first thing built afterwards would delete this. Deleting it is
    what closes them — for standalone only; server authority is not proven until build
    step 3 exercises the multiplayer route.

WHAT IT KEEPS
    The Left/Right Mouse Button input events, and nothing else from the dig chain. The trace
    moves into C++ because it is the same trace either way (ARCHITECTURE.md 4.3: aiming is a
    normal collision trace, is legal gameplay code, and stays legal), and moving it means the
    Blueprint is one node per button instead of nine.

HOW TO RUN
    From a built editor, with the project open or headless:
      py C:/Dev/VoxelWorld/Tools/Editor/rewire_dig_through_service.py
    or
      UnrealEditor-Cmd.exe VoxelWorld.uproject -unattended -nopause -nosplash -nullrhi ^
        -ExecutePythonScript="C:/Dev/VoxelWorld/Tools/Editor/rewire_dig_through_service.py"

    It saves the Blueprint itself. Re-running it on an already-rewired asset reports
    "already rewired" and changes nothing.

    Project: VoxelWorld - Task: T-113 - Added at CP-012
"""

import unreal

BP_PATH = "/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"
EDIT_FUNCTION = "/Script/VoxelWorld.TerrainInteractionLibrary.RequestTerrainEditFromView"
STREAMING_COMPONENT = "/Script/TerrainCore.TerrainStreamingComponent"
PLUGIN_VARIABLE = "TargetVoxelWorld"

BEL = unreal.BlueprintEditorLibrary
BGE = unreal.BlueprintGraphEditor

# Every node title in the T-101A dig chain, by the title the graph reports. Titles rather
# than indices: indices move the moment anyone opens the graph, titles do not.
DIG_CHAIN_TITLES = {
    "GetActorOfClass",
    "Set TargetVoxelWorld",
    "GetPlayerCameraManager",
    "GetCameraLocation",
    "GetActorForwardVector",
    "vector * vector",
    "vector + vector",
    "Line Trace By Channel",
    "BreakHitResult",
    "Branch",
    "RemoveSphere",
    "AddSphere",
    "Event BeginPlay",
}

# The two buttons, and what each one now asks the terrain service to do.
BUTTONS = [
    ("Left Mouse Button", "Remove"),
    ("Right Mouse Button", "Add"),
]

_LOG = []


def say(msg):
    _LOG.append(str(msg))
    unreal.log("[rewire] %s" % msg)


def warn(msg):
    _LOG.append("WARNING: %s" % msg)
    unreal.log_warning("[rewire] %s" % msg)


def title_of(node):
    try:
        return str(node.get_node_title()).split("\n")[0].strip()
    except Exception:
        return ""


def find_pin(node, name):
    """Input pin by name, or None. find_input_pin raises rather than returning null."""
    try:
        return node.find_input_pin(name)
    except Exception:
        return None


def main():
    say("=" * 66)
    say("T-113 build step 2: rewire the dig through UTerrainService")
    say("=" * 66)

    bp = unreal.load_asset(BP_PATH)
    if bp is None:
        warn("could not load %s - aborting." % BP_PATH)
        return

    graph = BEL.find_event_graph(bp)
    if graph is None:
        warn("no event graph on %s - aborting." % BP_PATH)
        return
    editor = BGE.get_graph_editor(graph)

    nodes = editor.list_all_nodes()
    say("event graph holds %d nodes before the rewire" % len(nodes))

    # --- 1. Is this already done? -------------------------------------------------------
    already = [n for n in nodes if "Request Terrain Edit From View" in title_of(n)
               or "RequestTerrainEditFromView" in title_of(n)]
    doomed = [n for n in nodes if title_of(n) in DIG_CHAIN_TITLES]
    if already and not doomed:
        say("Already rewired: %d service call node(s), no plugin dig chain left. Nothing to do."
            % len(already))
        return

    # --- 2. Find the buttons we are keeping ---------------------------------------------
    buttons = {}
    for node in nodes:
        t = title_of(node)
        for name, _kind in BUTTONS:
            if t == name:
                buttons[name] = node
    for name, _kind in BUTTONS:
        if name not in buttons:
            warn("no '%s' input event in the graph - aborting rather than half-rewiring." % name)
            return
    say("kept input events: %s" % ", ".join(sorted(buttons)))

    # --- 3. Add one service call per button ---------------------------------------------
    # Added BEFORE the old chain is deleted, so a failure here leaves a graph that still
    # digs the old way rather than one that does nothing at all.
    added = []
    for name, kind in BUTTONS:
        button = buttons[name]
        try:
            call = editor.add_call_function_node(EDIT_FUNCTION)
        except Exception as exc:
            warn("could not create the service call node for %s: %r" % (name, exc))
            warn("Is VoxelWorld compiled with UTerrainInteractionLibrary? Aborting.")
            return
        if call is None:
            warn("add_call_function_node returned nothing for %s - aborting." % name)
            return

        pos = button.get_node_pos()
        call.set_node_pos(unreal.IntPoint(pos.x + 420, pos.y))

        kind_pin = find_pin(call, "Kind")
        if kind_pin is None or not kind_pin.set_pin_value(kind):
            warn("could not set Kind=%s on the %s node; check it in the editor." % (kind, name))
        else:
            say("   %s -> RequestTerrainEditFromView(Kind=%s)" % (name, kind))

        pressed = None
        for pin in button.list_all_pins():
            if str(pin.get_pin_name()) == "Pressed":
                pressed = pin
                break
        if pressed is None:
            warn("no 'Pressed' pin on %s - aborting." % name)
            return

        # The old chain still owns this exec link; drop it before making the new one.
        pressed.break_pin_links()
        if not pressed.try_create_connection(call.find_execute_pin()):
            warn("could not connect %s.Pressed to the service call - aborting." % name)
            return
        added.append(call)

    # --- 4. Delete the plugin dig chain --------------------------------------------------
    doomed = [n for n in editor.list_all_nodes() if title_of(n) in DIG_CHAIN_TITLES]
    if doomed:
        say("removing %d node(s) of the T-101A plugin dig chain:" % len(doomed))
        for n in doomed:
            say("   - %s" % title_of(n))
        editor.remove_nodes(doomed)
    else:
        say("no plugin dig chain nodes found to remove")

    # --- 5. Delete the plugin-typed variable ---------------------------------------------
    # ARCHITECTURE.md 7.4: an AVoxelWorld reference in the ASSET is the leak no compiler can
    # catch, and the one that would survive a backend swap unnoticed.
    own_vars = list(BEL.list_member_variable_names(bp, False))
    if PLUGIN_VARIABLE in own_vars:
        if editor.remove_member_variable(PLUGIN_VARIABLE):
            say("removed the '%s' variable (an AVoxelWorld reference held in the asset)"
                % PLUGIN_VARIABLE)
        else:
            warn("could not remove the '%s' variable; remove it by hand in My Blueprint."
                 % PLUGIN_VARIABLE)
    else:
        say("no '%s' variable present" % PLUGIN_VARIABLE)

    # --- 6. Streaming interest (7.4 / DEF-10) --------------------------------------------
    add_streaming_component(bp)

    # --- 7. Compile and save --------------------------------------------------------------
    say("-" * 66)
    if BEL.compile_blueprint(bp):
        say("Blueprint compiled clean")
    else:
        warn("Blueprint compiled with errors - open it and read the Compiler Results.")

    if unreal.EditorAssetLibrary.save_loaded_asset(bp, False):
        say("saved %s" % BP_PATH)
    else:
        warn("could not save %s - use File > Save All." % BP_PATH)

    report(bp)


def add_streaming_component(bp):
    """Attach UTerrainStreamingComponent, the game-owned class that replaces the invoker."""
    comp_class = unreal.load_object(None, STREAMING_COMPONENT)
    if comp_class is None:
        warn("could not load %s; skipping the streaming component." % STREAMING_COMPONENT)
        return

    sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = sub.k2_gather_subobject_data_for_blueprint(bp)
    if not handles:
        warn("no subobject data for %s; skipping the streaming component." % BP_PATH)
        return

    for handle in handles:
        data = sub.k2_find_subobject_data_from_handle(handle)
        obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
        if obj is not None and obj.get_class().get_name() == "TerrainStreamingComponent":
            say("streaming component already attached")
            return

    params = unreal.AddNewSubobjectParams()
    params.set_editor_property("parent_handle", handles[0])
    params.set_editor_property("new_class", comp_class)
    params.set_editor_property("blueprint_context", bp)
    try:
        new_handle, fail = sub.add_new_subobject(params)
    except Exception as exc:
        warn("could not attach UTerrainStreamingComponent: %r" % (exc,))
        return
    if fail and str(fail):
        warn("could not attach UTerrainStreamingComponent: %s" % fail)
        return
    sub.rename_subobject(new_handle, unreal.Text("TerrainStreaming"))
    say("attached UTerrainStreamingComponent - a TerrainCore class, not a plugin invoker "
        "(ARCHITECTURE.md 7.4, DEF-10)")


def report(bp):
    """Prove the result rather than assert it: re-read the graph and say what is there."""
    say("=" * 66)
    graph = BEL.find_event_graph(bp)
    editor = BGE.get_graph_editor(graph)
    nodes = editor.list_all_nodes()
    say("event graph holds %d nodes after the rewire" % len(nodes))

    leftovers = [title_of(n) for n in nodes if title_of(n) in DIG_CHAIN_TITLES]
    if leftovers:
        warn("plugin dig chain nodes STILL PRESENT: %s" % ", ".join(sorted(set(leftovers))))
    else:
        say("no T-101A plugin dig-chain node remains")

    own_vars = list(BEL.list_member_variable_names(bp, False))
    say("own variables: %s" % (", ".join(own_vars) if own_vars else "(none)"))

    say("")
    say("Next:")
    say("  1. Standalone, NOT PIE:  .\\Tools\\Play-Solo.ps1")
    say("  2. LMB digs, RMB places, exactly as before - but through UTerrainService now.")
    say("  3. Expect 'Terrain backend TerrainBackendVPLegacy ready' in the log, and")
    say("     LogTerrainCore ops instead of direct plugin calls.")
    say("=" * 66)


main()

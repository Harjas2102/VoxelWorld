→ No action. For your reading only.

# P-013 — The 1F Gate-Observe pass: what the terrain does after it is edited

**Status: measurement report and Architect rulings, 2026-09-22. Implemented as T-134; recorded at CP-024 (D-051).**
**Author:** Claude Opus. **Risk:** R2 (observation, plus one server collision setting in the
adapter). **Base:** `ee7bbc9` (CP-023).
**Authority:** BACKLOG Phase 1 Gate-Observe ("documented, not solved"), ARCHITECTURE §7.4, §11
(E-4, E-9), §13, R-005, R-006, R-010, DEF-8, D-023.

Writer and reviewer are the same agent. §9 is the self-review.

---

## 1. What was asked, and how it was measured

The gate lists five behaviours to observe, not solve:
- collision edge cases;
- foliage and PCG over removed terrain;
- nav dirtying and rebuild;
- unsupported and floating terrain when undermined;
- world streaming of modified chunks.

**`Terrain.Observe`** (development only, `Source/TerrainCore/Private/TerrainObserve.cpp`) runs
ten scripted trials through the real edit path: `RequestEdit`, the queue, group commit. Each
trial is at a site 40–96 m from the PlayerStart, with its own collision interest of 10 m. A line
trace every frame records what the physics scene believes is there:
- **latency:** from commit to the first frame the new shape is visible;
- **stale frames:** the old shape still there;
- **gap frames:** no collision at all, the T-101A §2f fall-through window.

It runs in a standalone game, or on a dedicated server started with `-TerrainObserve`, which
lets the console edit path run there and nowhere else. The dedicated server is the case that
matters: it decides where players stand, and it has no camera.

## 2. Collision: one real defect found and fixed (GO-1)

**GO-1: the server's collision did not show small digs.** On the dedicated server, a 1.5 m or
3 m dig **never** reached collision: 8 s later the trace still hit the old ground. Fills and an
8 m dig did show.
- **The cause:** the plugin gives collision to every chunk it would *draw*, up to LOD 5
  (`bComputeVisibleChunksCollisions`). A server draws nothing up close, so that is one coarse
  chunk about 512 m across, covering the world. That coarse collision sat **over** the
  full-detail collision our interests build.
- **What that meant:** a small dig updated the full-detail layer underneath and not the coarse
  one above. The server decides where players stand, so players would have been held up over
  holes every client could see. That is rubber-banding exactly where people dig. The trace proved
  it: it hit a chunk about 18 m across before the dig, and a chunk about 514 m across afterwards.
- **The fix (Architect ruling, D-051):** on the server role, visible-chunk collision is off, so
  the server's collision comes only from interests. Clients keep the default: what a client draws
  near its player is full detail anyway. One adapter setting; no gameplay code.

**After the fix:**

| | Dedicated server | Standalone |
|---|---|---|
| An edit reaches collision | **99–101 ms** (3 frames at 30 Hz), all 8 edits | 7–26 ms, all 8 edits |
| **Frames with no collision at all** | **0** | **0** |
| Stale frames (old shape still there) | 2 per edit | 6–29 per edit (at about 1 ms frames) |
| First collision after a new interest | 33–266 ms (median 168) | 0–1 ms (the player's 100 m interest already covers it) |
| Collision chunk hit | about 18 m across, full detail, every trial | the same |

**What this settles:**
- **The fall-through gap does not exist.** The plugin double-buffers collision: a new body is
  cooked, then swapped in, so the old shape stays until the new one is ready. Across 16 edits in
  two configurations, not one frame had no collision. The §2f fall was the capsule being
  *engulfed* by an add near the player's feet, not a hole in collision. Placement inside any
  pawn is already refused (`UnsafePlacement`), for every pawn and not only the requester. That
  closes the add half of DEF-8.
- **The stale window is about 100 ms on the server.** Digging under a standing player drops them
  into the hole three frames late. That is correct behaviour, slightly delayed. Filling cannot
  engulf anyone.
- **Spawn and join:** on the server, collision appears 33–266 ms after an interest is created.
  A player spawned 150 cm up falls about 36 cm in 266 ms, so collision is there before they land.
  That is a small margin, and it is measured on this machine only. **A KillZ or respawn volume is
  still not built** (R-010), and it remains the net for everything else.
- **Still not measured:** the removal half of DEF-8. Player B digging under player A drops A
  into the hole, which is intended. Whether that needs a rule, such as refusing a dig within a
  radius of another player, is a game decision, not a physics one.

## 3. Standing on terrain in multiplayer: R-010 is live (E-9 is next)

The multiplayer harness's own logs, from the T-133 run, count the plugin's
`ClientAdjustPosition … could not resolve the new relative movement base` warnings.
**Every server correction to every client standing on terrain was discarded:** 81–142 per client
per run, and 0 were applied.
- **The mechanism:** the terrain mesh is `Movable`
  (`VoxelProceduralMeshComponent.cpp:95`), so Unreal treats it as a moving floor and sends
  positions relative to it. The component has no network identity (`NOT Supported` thousands of
  times per client log), so the client cannot resolve the floor and ignores the correction.
- **What players would feel:** a client that drifts from the server is never pulled back while
  on terrain. Harmless on flat ground at low latency; wrong the moment the ground or the player
  disagrees.
- **E-9 is the experiment:** make the terrain mesh non-movable, so corrections become absolute.
  It is scheduled as the next task (T-135), because it can change the adoption verdict.

## 4. Undermined terrain

A cavity of 2.5 m radius, 6 m below the surface:
- the surface above stayed **exactly** where it was (Z unchanged), in both configurations;
- a trace up from inside the cavity hit its roof (Z = −565).

**Nothing is simulated structurally.** Undermined terrain, overhangs and fully detached lumps
stay where they are, forever. That is the representation Pillar 1 wanted (T-101A's tunnel), and
it is also why a player can leave a floating island. If floating terrain ever needs to fall, it is
a game system (connectivity checks over edited chunks), not a plugin feature. **No action for
Phase 1.**

## 5. Streaming of modified chunks

- **Data never unloads.** The plugin keeps every voxel in memory for the whole world, and edits
  included. The adapter's residency is "inside the world bounds" (`IsRegionResident`). The server
  also pins every edited chunk (P-003 §4). Streaming out cannot lose an edit.
- **Collision is streamed, and correctly so after GO-1.** On the server, 3 s after the last
  interest near a dug site was released, **its collision was gone**. Re-acquiring brought it back
  in **232 ms, with the dig in it** (Z matched to the centimetre). So physics exists only near
  players, and nothing away from players stands on terrain. That is right for a server, and it
  constrains Phase 5: a creature with no player nearby has no ground unless it carries an
  interest.
- **Clients** stream edited chunks through subscriptions and snapshots (P-008), verified
  under load at E-6 (P-011): 262 edited chunks identical on a late joiner.

## 6. Foliage and PCG (R-005)

- **The level has none:** no `InstancedFoliageActor`, no PCG component or volume. Nothing floats
  today because nothing grows.
- **The plugin's own foliage ("spawners") requires Voxel Plugin Pro.** Every spawner call in Free
  logs "Voxel Spawners require Voxel Plugin Pro" (`VoxelBlueprintLibrary.cpp:416–462`). It is not
  an option.
- **Unreal's own foliage instances are static**, and would float over a dig by default.
- **The response model, ruled for later:** foliage becomes a game system keyed by terrain chunk.
  The service already publishes every committed edit's affected chunks, after the flush and in
  order (P-012). A foliage owner subscribes to that and removes or re-seats instances whose ground
  changed. PCG, if used, regenerates per affected chunk the same way. It is cosmetic in Year 1
  (R-005 severity Low). It becomes a task only when vegetation is authored.

## 7. Navigation (R-006): source-traced, not yet measured

- **What the source says:** the terrain mesh can affect navigation
  (`bCanEverAffectNavigation = true`). With the plugin's navmesh switch on, every mesh update
  calls `FNavigationSystem::UpdateComponentData`, which dirties the navmesh tiles under it. A
  navmesh with `RuntimeGeneration=Dynamic` rebuilds dirtied tiles. So edits *would* reach nav.
- **Why it is not measured:** the level has no `NavMeshBoundsVolume` and the plugin's navmesh
  switch is off, so there is no navmesh to dirty. The trials ran with the switch on
  (`-TerrainNavmesh`) and dynamic generation, but a navmesh needs bounds, and a bounds volume
  needs brush geometry that cannot be built at runtime. `SetBuildBounds` did not create
  navigation data.
- **What that means:** nothing uses navigation until Phase 5 wildlife (D-005). The measurement
  waits until that level has a navmesh: dig in its bounds and time the tile rebuild. The harness
  already does that when navigation data exists (`NavWatch`). R-006 stays open with that named
  experiment.

## 8. What our wrapper must own: the CONDITIONAL list

If the backend is adopted, it is adopted **behind our own authoritative wrapper**, which already
owns edits, persistence, replication, yield and settlement. The Gate-Observe pass adds:

1. **Server collision policy** (done, GO-1): interests only, full detail.
2. **The movement base** (R-010, E-9): unsolved; T-135.
3. **A KillZ or respawn net** (R-010): unsolved; small.
4. **Foliage keyed by terrain chunk** (R-005): a design, for when vegetation exists.
5. **Navmesh** (R-006): configuration plus one measurement, for Phase 5.
6. **Ground for things away from players:** an interest per creature, or per cluster of
   creatures (E-8), in Phase 5.

The plugin itself is the store of voxels, the mesher and the collision cooker. Everything that
makes it a multiplayer persistent world is already ours.

## 9. Self-review: what could still be wrong

- **One machine, `-nullrhi`.** Collision cooking is CPU work and should not care about the
  renderer, but frame timing does, and the Linux server is unmeasured (R-007).
- **Trace-based observation sees the physics scene, not a character.** A capsule could still find
  an edge a thin line trace does not. It is a strong proxy, not the character itself.
- **Ten sites and one terrain shape.** Steep slopes, chunk seams under a standing player, and
  concurrent edits in one chunk were not separately trialled.
- **GO-1's fix trades coarse far-away collision for none on the server.** Nothing on the server
  currently needs ground away from players. Phase 5 must remember §5's last point.
- **The movement-base count was repeated after GO-1:** still 55–127 discarded corrections per
  client over 3 rounds, and 0 applied. GO-1 does not touch it; E-9 (T-135) must.

## 10. Evidence that nothing else moved (final source)

- 43/43 automation; both targets build.
- `MP.Convergence`: 3 rounds with an observer and capture, PASS.
- Stress, 5,000 edits: PASS at 99 edits/s median, frames p95 34 ms. The worst frame dropped to
  69–84 ms from 88–96, because the server keeps fewer collision chunks.
- Logs: `Saved/Logs/Observe-7d2d0816.log` (server), `Observe-d588bfeb.log` (standalone),
  `Observe-14c3e29b.log` (server before GO-1), `T101B-MP-20260922-005939`,
  `StressTest-b336e3e49167`.

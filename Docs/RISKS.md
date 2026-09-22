# RISKS.md — Architectural Risk Register

> The top architectural unknowns, each with an experiment that resolves it. A risk is
> closed only by a **result** and a **decision**, never by an opinion. Updated at every
> checkpoint (AGENTS.md section 7).

**Opened:** CP-002 (2026-09-05)

Severity / probability scale: **High · Medium · Low**.

---

## R-001 — Terrain backend multiplayer synchronization

- **Severity:** High — Pillar 1 depends on it.
- **Probability:** High — Voxel Plugin Free Legacy provides no server-authoritative
  edit replication out of the box.
- **Mitigation experiment:** T-101B Gate-Critical — client requests an edit, server
  validates and applies, all clients converge. 2–3 clients editing the same region with
  deterministic ordering and no permanent divergence.
- **Owner / task:** T-101B (sub-step 1B)
- **Result:** *open*. CP-008: memory-backend conformance is green, including
  position-sensitive hashes (density and material rearrangements), exact multi-chunk
  reporting and point-query freshness. This is same-process reference evidence only;
  the production adapter, network convergence and cross-platform replay remain untested
  (DEF-5). No adoption or multiplayer claim follows from these four headless tests.
- **Decision:** *open*

## R-002 — Join-in-progress modified-chunk transfer

- **Severity:** High — a friends server is joined mid-session constantly.
- **Probability:** High — Legacy has no documented JIP state-transfer path.
- **Mitigation experiment:** Server holds heavily edited terrain; a fresh client joins
  and reconstructs modified chunks from snapshot + ops since, without replaying full
  server history and without visible divergence.
- **Owner / task:** T-101B (sub-step 1E)
- **Result — CP-019, mitigated for the common case.** P-008 (D-044) is built.
  - Edited chunks are sent as Zlib snapshots, nearest first, with backpressure. A dug chunk is
    about 0.3–5 KB on the wire.
  - Multi-round `MP.Convergence`, where every round after a map change is a join onto a saved,
    edited world, passes 3 rounds with an observer.
  - `-DropOp` proves a lost op is detected and repaired.
  - Snapshot install costs the client 16–25 ms per chunk after the bulk `WriteRegion` fix.
- **E-6 measured at CP-020 (P-011).**
  - A joiner arriving mid-edit in a 100 m region dug by 5,000 edits caught up in **6.1 s** (243
    snapshots, 1.6 MB, 1.74 MB over the connection), while edits continued at about 75/s.
  - Installs cost about 23 ms per chunk and are applied one per frame by an ordered inbox, so they
    no longer burst.
  - All 254 edited chunks verified identical in density and material.
- **Decision:** D-044, D-047. **Closed for the design workload.** The residual is Linux, and a
  region much larger than the interest radius.

## R-003 — Terrain save growth and compaction

- **Severity:** Medium — a server that cannot be saved cheaply cannot run for months.
- **Probability:** Medium
- **Mitigation experiment:** Hundreds to thousands of edits, then measure save-file
  growth, snapshot size after compaction, and bytes per edit. Establish whether the
  operation journal can be compacted into a compact chunk snapshot at revision R.
- **Owner / task:** T-101B (sub-steps 1D, 1F)
- **Result:** *open*
- **CP-010 evidence:** in-memory revision monotonicity and atomic overflow rejection
  pass in Revision.Monotonic. This does not cover payload deletion or revision
  recovery after restart; AR-4 defers those to build step 4/DEF-9.
- **CP-013 evidence — one symptom of this risk is gone, and the risk itself is not.**
  The T-101A *hill* was never persistent because it was sculpted by script into a running
  editor session (finding 2e). T-108 replaced it with a generated world that is a pure
  function of position and seed, so the world's SHAPE now costs zero bytes and survives
  every restart with no save file at all. **Player EDITS still do not survive a restart**;
  that is build step 4 and is what this risk is actually about. If anything the generated
  world helps it: a pristine chunk regenerates and never needs a payload, so only edited
  chunks can grow the save.
- **CP-014 evidence — the format now exists, and the numbers are computed rather than
  guessed.** P-004 fixes schema 2 and its codecs are built and tested. The withdrawn
  ~122 B/edit estimate is replaced by **computed** figures: a radius-4 dig over 8 chunks with
  3 materials is **350 B** of journal; a Dense chunk object is **131,200 B**; the worst legal
  journal record is **84,768 B**; SparseDiff beats Dense up to **21,844** of 32,768 changed
  samples. Compaction's cost argument is now structural rather than hoped for: the
  path-copied index rewrites at most 12·D pages for D changed keys regardless of how much
  cold history exists, measured at 147 pages for 64 keys and exactly 12 for one later change.
  **None of this is a measured save file.** Nothing writes to disk yet, so "hundreds to
  thousands of edits, then measure growth" — the mitigation experiment this risk actually
  names — has still not been run. Bytes/edit under the §7.1 workload remains open.
- **CP-020, measured (P-011).** Over 7,659 edits at the design load:
  - the journal grows about **235 B/edit**;
  - the world directory held **55.8 MB**, mostly two retained checkpoint generations of about 250
    Dense chunks (131 KB each), plus a 0.35 MB ledger with a 4 MB WAL;
  - retention reclaimed about 18 MB per cycle, 29 times.
  Save growth is bounded by the dug area, not by edit count. The journal still grows without limit
  until trimming exists.
- **Decision:** *open*, until journal trimming exists.

## R-004 — Material-yield accuracy

- **Severity:** High — mining as measured removal is the mechanic that makes the game
  (D-011, D-016). If yield cannot be derived from removed volume, mining degenerates
  into "raycast node, add ore."
- **Probability:** Medium — the sphere tools return `ModifiedValues`, so the hook
  exists; whether it is accurate and deterministic is unproven.
- **Mitigation experiment:** T-101B Gate-Critical — dig a known volume of a known
  material and confirm the server computes soil / stone / ore quantities
  deterministically from the edit, not from the rendered mesh.
- **Owner / task:** T-101B (sub-step 1C)
- **Result:** *open*. CP-008: reference-backend tests verify positive Remove and negative
  Add volumes with the appropriate material, and no physical volume for Paint. The
  reference linear occupancy map is documented, not calibrated against the production
  kernel. E-1 and DEF-6 remain open; these tests do not validate economic conservation.
- **Result — CP-019, the mechanic works on the production plugin (P-009, P-010).**
  - Every edit measures signed per-material volume from the plugin's own per-voxel old and new
    values. **E-1:** within 2.4% of the rendered hole at the 2 m player dig, and within 0.5% from
    radius 6 up.
  - A dig across a stone/ore boundary splits the same total to the microlitre.
  - Credits are settled into a crash-safe ledger; the kill test's audit matches the journal after
    every hard kill.
  - **Known leak, not a mint:** placing and then re-digging the same sphere recovers about 79%,
    because Add and Remove are not exact inverses at the boundary. Players place Fill, which never
    pays, so there is no mint.
- **Decision:** D-045 and D-046 (technical). Closed for measurement accuracy; the economic tuning
  of placement is a later GAME decision.

## R-005 — PCG / foliage invalidation after edits

- **Severity:** Low — cosmetic in Year 1.
- **Probability:** High — trees left floating over an excavation is the default
  behaviour of most systems.
- **Mitigation experiment:** Gate-Observe — remove terrain under foliage, document what
  happens, define a response model. Not solved at the gate.
- **Owner / task:** T-101B (sub-step 1F, Gate-Observe)
- **Result:** *open*
- **Decision:** *open*

## R-006 — Dynamic navigation after edits

- **Severity:** Medium — gates creature AI (D-005 Year 1 wildlife).
- **Probability:** Medium
- **Mitigation experiment:** Gate-Observe — measure nav dirtying and rebuild behaviour
  after terrain edits. Does not need to be production-ready; it needs to be known.
- **Owner / task:** T-101B (sub-step 1F, Gate-Observe)
- **Result:** *open*
- **Decision:** *open*

## R-007 — Dedicated-server / plugin compatibility, and platform divergence generally

- **Severity:** High — D-002 makes a Linux dedicated server the shipping target.
- **Probability:** Medium — the plugin declares Win64/Linux/Mac module support, but a
  server build has never been attempted and the shipped binaries are editor DLLs.
- **CP-015 — this risk is broader than "will it build", and there is now a confirmed
  instance.** T-119 found that `IFileHandle::Flush(bool bFullFlush = false)` means different
  things on the two target platforms: Windows ignores the parameter and always calls
  `FlushFileBuffers`, while Unix maps `false` to `fdatasync` and `true` to `fsync`.
  `fdatasync` makes no promise about **metadata**, and a file's length is metadata — which
  every object write and every journal append changes. The defaulted call would have been
  **correct on the development machine and silently wrong on the shipping one**, and because
  the two calls are literally the same instruction on Windows, **no test runnable on this
  machine could have found it**. It was caught by reading both platform implementations.
  Fixed by D-034 §1 and normative in P-004 §12.
  **What that changes about this risk:** the exposure is not only "the server build has never
  been attempted". It is that an entire class of defect — correct here, wrong there — is
  invisible from the only platform this project has ever run on, and the project has no
  mechanism that would surface the next one.
- **Mitigation experiment:** confirm the architecture never requires client-only plugin
  behaviour; attempt a server target build at Phase 4 (T-302 successor). **Add:** run the
  headless `TerrainCore` suite on Linux as soon as a server target builds at all. It needs no
  renderer, no plugin and no editor, so it is the cheapest possible cross-platform signal and
  it would have caught nothing this time — the Flush bug is a durability bug, not a
  correctness one — which is itself worth knowing when deciding what evidence to trust.
- **CP-019 — two more things only Linux can check.**
  - P-007's `flock` lease, with its inode check, has never been compiled.
  - UE's SQLite runs on UE's own file layer (`SQLITE_OS_OTHER`, no shared memory). It silently
    refused plain WAL on Windows, and the ledger now requires EXCLUSIVE + WAL + FULL and reads
    each back. Whether Linux behaves identically is unverified.
  - The first Linux build must run `WriterLease`, `Settlement.*` and `Storage.PlatformDevice`.
- **CP-021 — a known Linux durability gap (Codex review F6, source analysis).** Bootstrap syncs
  the new world directory and its subdirectories, but never the parent `Saved/Worlds`. On Linux,
  the world directory's own name is therefore not proved durable, so P-005 §6's "window closed"
  overstates the Unix case. It must be fixed and covered before any Linux claim.
- **Owner / task:** T-101B (architecture check) → Phase 4 (build proof)
- **Result:** *open, and broadened at CP-015 by a confirmed instance.*
- **Decision:** *open*

## R-008 — Plugin licensing and version risk

- **Severity:** High — this is the backend the whole gate runs against.
- **Probability:** High.
  - Voxel Plugin **Free Legacy** is maintenance-mode. Installed build: **v434 /
    `159fd19a0` / EngineVersion 5.8.0** (was v432 / `e9648b302` / 5.7.0 through
    CP-010; repointed at T-112.5, CP-011). The 5.7 install is retained locally at
    `Tools\downloads\VoxelFree.bak-*-engine5.7.0` as the rollback.
  - **Voxel Plugin 2 is paid** and engine-version-gated: distributed via the Fab "Voxel
    Plugin Installer," which requires owning Voxel Plugin Pro Legacy. Documented engine
    targets at last docs snapshot were 5.5/5.6 — re-verify before relying on it.
- **CP-013 — the Pro gate's worst consequence is now closed.** The gate meant the only
  runnable generators were Flat and Empty, which is why the test world was a plane and why
  the hill had to be sculpted by script. **T-108 makes the generator ours**: game-owned C++
  in `TerrainCore` with a thin adapter forwarder, so the 106 Pro-gated graph assets are no
  longer on any critical path. What remains of this risk is the plugin's maintenance status
  and version gating, not its generator tier. The Mesh Terrain watch item is still
  un-evaluated.
- **Mitigation experiment:** The D-011 adapter keeps the backend replaceable, and the
  T-101B gate decides whether Free Legacy is adopted at all. No gameplay code takes a
  dependency on plugin types.
- **Owner / task:** D-010, D-011, T-101B
- **Result — FIRST HIT, 2026-09-05 (T-101A):** **Voxel Graphs are Pro-gated at runtime.**
  `Voxel: Running Voxel Graphs require Voxel Plugin Pro`. All 106 `VoxelGraphGenerator`
  example assets produce an **empty world with no error** — the asset loads,
  `SetGeneratorObject` succeeds, `IsCreated()` returns true, the world reports a
  generation time, and the density field is empty. Free ships exactly two runnable
  generators, both C++: `UVoxelFlatGenerator` and `UVoxelEmptyGenerator`.
  → Procedural generation must be a C++ `UVoxelGenerator` subclass (**T-108**), promoted
  from "later" to a Phase 1 requirement. Full write-up: `T-101A_FINDINGS.md` section 2b.
- **What this changes about the risk:** the exposure is worse than "the free tier has
  fewer features." Gates are **silent at runtime** and are not documented in the headers,
  so each one is found by something quietly not working. Assume more exist; budget for
  discovery in every sub-step of T-101B rather than treating each as a surprise.
- **Watch item, logged 2026-09-06 (CP-007, per D-025):** UE 5.8 ships **Mesh Terrain**, a
  mesh-based terrain system supporting overlapping geometry. Unknown whether it is
  runtime-deformable or replicable — the two properties Pillar 1 actually needs. It is a §10
  conformance candidate or it is nothing: if it can be driven through the eleven
  `ITerrainBackend` methods and passes `Backend.Conformance`, it is a second backend and this
  risk gets cheaper; if it cannot, it is irrelevant. **No fork opens until there is evidence.**
  First look was scheduled for T-112.5. **Not evaluated at CP-011:** T-112.5 was
  bounded to the engine/plugin bump and MCP adoption, and the Director's split kept it
  there. Mesh Terrain remains a watch item with **no evidence either way**; the engine
  is now on 5.8, so the look is cheap whenever it is scheduled.
- **Result — version bump survived, 2026-09-06 (T-112.5, CP-011):** the plugin moved
  **432 -> 434** with the engine. The full CP-006 verification set was re-run green,
  including the Director's by-hand `AddSphere`/`RemoveSphere` dig through the unchanged
  `BP_ThirdPersonCharacter` wiring. No API break, no silent behaviour change observed at
  this surface. This measures one bump at one call site; it does not make Free Legacy
  maintained, and it says nothing about the Pro-gated surfaces already found.
- **New exposure, 2026-09-06 (T-112.5, CP-011): Unreal MCP is Epic-Experimental.**
  `ModelContextProtocol` and `AllToolsets` both carry `IsExperimentalVersion: true` and
  `EnabledByDefault: false`, and Epic's own documentation warns the APIs and formats may
  change. This is accepted deliberately: MCP is a **dev-time editor tool** on the D-025
  guard, never shipped, never referenced from `VoxelWorld` or `TerrainCore`. The cost of
  it breaking is a broken tool, not a broken game. What makes that true in practice is
  the `TargetAllowList` of `["Editor"]` in `VoxelWorld.uproject` — **necessary, because
  the plugin ships Runtime modules** (`ModelContextProtocol`,
  `ModelContextProtocolEngine`) and is not inherently editor-only. Proven from the build
  receipts at CP-011: the game target's `BuildPlugins` contains none of them and zero
  matching build products. AGENTS §9 carries the guard.
- **Decision:** *open* — resolved by the T-101B exit (PASS / CONDITIONAL / FAIL /
  VISION CHANGE). This finding does not by itself argue for VP2: the C++ generator is
  work we owed D-011 regardless.

## R-009 — Director availability and project stall

- **Severity:** High — the project's largest observed failure mode. The repo sat at
  CP-001 for nearly three months: four commits, ~5,000 lines of planning against 0 lines
  of gameplay.
- **Probability:** High — the Director's time is genuinely constrained.
- **Mitigation experiment:** The **7-day rule** — if the first hole is not dug within
  seven days of CP-002, the plan is wrong, not the Director: cut the step smaller. Every
  task is scoped to one evening. Agents prefer config, C++, and Python-scripted editor
  actions over menu instructions (AGENTS.md section 11).
- **Owner / task:** T-101A, and every task definition thereafter
- **Result:** ✅ **First test passed.** The deadline was 2026-09-12; the first hole *and*
  the tunnel landed **2026-09-06**, six days early, in one working session. The rule has
  now been exercised once and held. It stays in force for every subsequent task — one
  success does not retire the largest observed failure mode of this project.
- **Decision:** *open* — keep the rule; re-evaluate after T-101B, which is a much larger
  task and the real test of whether one-evening scoping survives contact with the gate.

## R-010 — Voxel terrain is a hostile multiplayer movement base

- **Severity:** High — it sits directly on Pillar 1 and on the T-101B pass criteria.
  Players stand on terrain constantly; if standing on it is unsound under replication,
  no amount of correct edit-sync saves the feel.
- **Probability:** Observed, not hypothetical — reproduced on the first PIE run at
  T-101A (2026-09-06), which was accidentally still on the CP-001 3-player settings.
- **Evidence:** `VoxelProceduralMeshComponent` is reported `NOT Supported` by
  `FNetGUIDCache::SupportsObject`, so when the server sets it as a character's relative
  movement base the client cannot resolve it and **every `ClientAdjustPosition`
  correction is discarded**. Separately, `AVoxelWorld` refuses to use the player camera
  as its LOD invoker outside standalone net mode, so multiplayer terrain does not render
  at all without a `VoxelInvokerComponent` on the character. Full write-up:
  `T-101A_FINDINGS.md` section 2d.
- **What this changes:** two items the T-101B gate did not have. The invoker is a hard
  requirement, not polish. The movement-base failure needs a deliberate test — a player
  standing on terrain while another player edits it — because that is the exact case the
  game is built around and the exact case this breaks in.
- **Second observation, 2026-09-06 (T-112.5, CP-011): a slow frame is enough to lose the
  player through the floor.** On the first UE 5.8 standalone launch, on a cold DDC, the
  Director spawned and fell into an endless void. The log explains it: 150 PSO creation
  hitches and six Path Tracing RTPSO compiles of **18-55 seconds each**, at spawn. The
  character spawns at Z+150 above the generated plane, so any stall before the voxel
  collision mesh exists is an unopposed fall with nothing underneath. A warm-cache
  relaunch generated the world in **0.130s** instead of 2.804s and played correctly.
- **What this changes:** the T-101A note that adding a sphere under yourself drops you
  through the ground is now the *second* route into the same unrecoverable state, and
  the first one needs no player action at all — a hitch will do. The two share one
  mitigation. **A KillZ or respawn volume is a prerequisite for anyone playing, not
  polish**, because today the only recovery from a fall is to quit the process. Not an
  engine-upgrade defect: it reproduces from cold caches on any version.
- **Third observation, 2026-09-07 (T-113, CP-012): the invoker requirement is now met in
  code, and standalone runs two invokers at once.** `UTerrainStreamingComponent` registers
  interest with `UTerrainService`, which forwards it to the backend, which creates a
  `UVoxelSimpleInvokerComponent` on the voxel world actor — so the hard requirement above is
  satisfied without any plugin type touching a character or an asset (§7.4, DEF-10). The log
  confirms it: `Voxel Invoker enabled; Name: VoxelSimpleInvokerComponent_0`.
  **But the plugin still logs `No Voxel Invoker found, using camera as invoker` a
  millisecond earlier**, because subsystem `OnWorldBeginPlay` runs after actor `BeginPlay`,
  so our first interest is acquired on the component's next tick. Both invokers then stay
  live. Harmless in standalone and it does not affect digging — but it means the camera
  invoker is still doing part of the LOD work, and the camera invoker is precisely what the
  plugin **refuses** outside standalone. **On a dedicated server there is no camera to fall
  back to**, so whatever the camera is currently covering will simply be missing. Look at
  this at build step 3, when the multiplayer route is exercised for the first time.
- **Unchanged and still open: the KillZ.** Nothing in T-113 addressed it, and T-113 did not
  make it worse. It remains a prerequisite for anyone actually playing.
- **Owner / task:** T-101B; the invoker also blocks any multiplayer terrain test at all
- **Result:** *open*. The invoker half is now implemented and behind the game-owned
  boundary; the movement-base failure, the collision-readiness window and the KillZ are all
  untouched. No multiplayer claim follows from a standalone run.
- **Decision:** *open* — feeds D-017 (terrain architecture v1)

## R-011 — Implementation deferral

**Ruled:** 2026-09-06 (CP-005)

An implementation increment may not be returned unstarted. If the implementer is
blocked, it returns one specific blocking question naming the ambiguous line of
`ARCHITECTURE.md`, not a general request for a ruling. A packet returned without a
named ambiguity counts as a failed increment under R-009.

- **Result — first exercised 2026-09-06 (CP-007):** ✅ **held.** The build-step-1 packet named
  `ETerrainRole` in its file list; `ARCHITECTURE.md` names the concept once, at **line 325**,
  and never enumerates its values. The increment was delivered complete — codec, quantiser and
  two green tests — with the ambiguity named to the line and nothing else in the increment made
  to depend on the answer. The Architect ruled it in the same session as **AR-2**
  (`ETerrainRole = { Server, Client }`), alongside **AR-1**, **AR-3** and **AR-4**. The rule
  produced the behaviour it was written for: work shipped, the open question visible rather
  than absorbed, and answered inside one session.
- **What to watch:** the failure mode this rule *cannot* catch is an ambiguity the implementer
  does not notice and resolves silently. AR-2 was surfaced because the packet happened to name
  the type; a determination made inside a function body would not have surfaced the same way.
  **AR-3** — `DequantiseVoxel` returning the voxel centre — is exactly that shape, and was
  caught only because a test forced it. Assume there are others.
- **Result — CP-008:** the first-session packet was initially stopped at one named
  conflict: §4.6 lines 536–538 declared Density/Material/Version while the packet
  required Sample(FIntVector). The Director authorised a determination (D-027);
  T-112.2 then completed with a green build and four green headless tests in the same
  session. API, residency and reference-kernel determinations were exposed in headers
  and reconciled into ARCHITECTURE at checkpoint. The initial stop is recorded, not
  counted as implementation; the completed increment is the result. Risk stays open.
- **Result — CP-010:** T-112.3's missing index/service API was exposed in a bounded
  R2 plan; Director approved with "go" (D-029). Implementation, UE 5.7 build and
  all five headless tests completed. Future edit integration must reject revision
  exhaustion before terrain mutation; the metadata helper alone does not resolve
  DEF-7. No continuing architectural delegation or terrain-risk closure follows.
- **Result — CP-012:** T-113 found a real gap and did not stop on it. §4.3's
  `FTerrainBackendInit` never carried a world, which a backend owning an actor cannot do
  without; the Director had already given blanket execution authority (D-031 part 1), so the
  determination was made, recorded as **AR-5** in the file header *and* in ARCHITECTURE at
  checkpoint, and routed for cheap overrule. This is the shape the rule wants: work shipped,
  the question visible rather than absorbed.
  **The CP-007 warning above still applies and is worth repeating**: AR-5 surfaced only
  because it changed a struct that other code reads. The step-2 scope limits — Removed left
  empty, density-only region transfer, unsupported ops refused — are determinations of the
  same weight that live *inside* function bodies, and they were surfaced by deliberately
  writing them into headers rather than by any mechanism. Assume the ones nobody wrote down
  are the dangerous ones.
- **Decision:** *open* — keep the rule. Re-evaluate after T-101B, alongside R-009.

## R-012 — Process weight

**Ruled:** 2026-09-06 (CP-005)

Governance exists to protect the game, and stops being useful when it costs more
Director attention than the game does. Warning signs: a session that produces no
playable change; a document whose only reader is another document; a decision the
Director cannot restate in one sentence.

Checked at every checkpoint alongside the VISION drift checks. If flagged, the next
session is spent deleting process, not adding features.

**CP-008 check:** bounded step passed: onboarding and implementation happened together,
and the outcome is executable conformance evidence. No playable change yet; T-113
remains the next playable milestone, after T-112.3 and the D-025 upgrade. Checkpoint
documents were held until explicitly requested. Keep watching the cost of successive
headless-only steps; no new process task was inserted ahead of the playable milestone.

**CP-009 check:** the Director requested a docs skim, README refresh and an alternating
agent handoff. One rolling HANDOFF file and OPERATIONS §5.1 cover both agents; no
per-agent log tree, repeated full-doc skim or new startup gate is required. This aims
to reduce usage-limit interruption cost; effectiveness will be tested by the next
pickup of T-112.3. No terrain risks closed by this documentation-only update.

**CP-010 check:** bounded step passed. The shared handoff enabled pickup; one R2
plan approval preceded code, then T-112 completed with five green tests. No new
process task was inserted. No playable change yet; T-113 remains next for gameplay,
after the scheduled T-112.5 upgrade. No risks closed by this headless increment.

**CP-012 check: PASS, and the first playable change since T-101A.** T-113 ended in
something that runs: digging works through the service, and the two drift checks that have
been flagged since T-101A are cleared for standalone. The run of headless-only steps that
CP-008 and CP-010 both flagged as worth watching has ended, as those checks predicted it
would. Cost: one authorisation sentence from the Director and no new process task, document
or gate. Checkpoint text was held until "checkpoint" was typed.

**One process cost is worth recording honestly:** the session hit the usage limit mid-task
and had to be resumed. The handoff breadcrumb written before the risky editor work is what
made the pickup cost roughly one message instead of a re-derivation, which is the outcome
D-028 was written for. Keep writing the breadcrumb *before* the risky half, not after.

## R-013 — The production adapter has not passed the conformance suite

**Opened:** CP-012 (2026-09-07)

- **Severity:** Medium — it does not threaten a pillar today, but it is the difference
  between "replaceable" as a claim and "replaceable" as a fact, and D-010/D-011 rest on it.
- **Probability:** Certain, by construction — this is a known gap, not a suspicion.
- **What it is.** ARCHITECTURE §10 defines replaceability operationally: a backend passes the
  `Backend.Conformance` suite "or the backend is not a candidate. This suite is the definition
  of the contract; there is no other one." `FMemoryTerrainBackend` passes it.
  **`FVPLegacyBackend` does not yet**, and T-113 makes no claim that it does. The gaps are
  deliberate and each is bound to the step that decides it: materials are zero everywhere
  because K9 (game-id ↔ plugin-index) lands at step 6; `Removed` is empty because DEF-6 and
  the material read land at step 6; `ReadRegion`/`WriteRegion` move density only because the
  snapshot format is K3/DEF-9 at step 4; Flatten, Smooth, Paint and box ops are refused
  because DEF-5 leaves their semantics unruled.
- **Why it is worth a register line rather than a comment.** The suite is also headless by
  §6.1 — no engine world, no plugin — and `FVPLegacyBackend` needs both. So running it against
  the adapter is **§6.2 in-engine work that does not exist yet**, and `Adapter.ApplyOp.Matches`
  is listed in §6.2 with no owning step. Left unwritten, the most likely outcome is that the
  suite is quietly never run against the only backend the game actually ships, and D-011 goes
  from compiler-enforced to merely believed.
- **Mitigation experiment:** stand up the §6.2 harness — an in-engine automation test that
  creates a world, spawns the adapter and runs `RunTerrainBackendConformance` against it —
  and close the material and region gaps as their steps land. `Terrain.SelfTest` is the
  interim stand-in and is explicitly not a substitute: it proves the chain is connected, not
  that the contract is met.
- **Interim evidence, CP-012:** the adapter drives a real dig end to end — 438 voxels across
  8 chunks, revision advanced, density inverted, OpSeq monotonic, three rejection paths
  correct. The eleven methods are all implemented; four of them are honest partials.
- **Owner / task:** build step 4 for regions, step 6 for materials; the §6.2 harness itself
  is unassigned and should be given an owner at the next checkpoint.
- **Result:** *open*
- **Decision:** *open*

## R-014 — Cross-platform and cross-build kernel determinism

- **Severity:** High if it bites — a client that computes a different density from the same
  op sees a wall where the server sees air (FM-1), and the divergence is inside a chunk
  whose revision matches, which is the case revision tracking cannot detect.
- **Probability:** Low to Medium, and genuinely unmeasured.
- **What it is.** §4.10.4(b) **requires** that one backend, given the same op sequence, seed
  and generator version, produce identical output across builds and platforms — and states
  plainly that this is a requirement with a residual risk, not a proof. The game side
  contributes no float to the geometry: the op is integers, `r = RadiusVoxQ16/65536` is exact
  on any IEEE platform, and the sphere test is an exact comparison. **The entire residual is
  in the kernel's own floating-point arithmetic** — a compiler contracting a multiply-add, a
  different vectorisation, a fast-math flag, a different CPU.
- **Why it is only opened now.** It was inside DEF-5, which was open. Closing DEF-5 resolved
  the *specification* and left this measurable question standing, so it belongs in RISKS
  rather than in a closed defect where nobody would look for it.
- **What already guards it.** `Op.Semantics.Golden` (§6.1) — committed hashes that fail
  loudly if a toolchain change moves a value, with the test's own error text saying the fix
  is never to update the number. `bMultiThreaded = false` on **both** server and client until
  E-2 reports, because client results supply collision and are not cosmetic.
- **Mitigation experiment:** `Adapter.Determinism` (§6.2) across 20 runs and both threading
  modes; then the same fixtures on a second toolchain, and on Linux when R-012's
  cross-platform work happens.
- **Owner / task:** build step 3 (E-2), then §12.
- **CP-014 note — the blast radius shrank; the risk did not close.** P-004 rule 1.2 persists
  **no floating point at all**: voxel size and world origin cross as exact micrometres, and
  every stored sample is an integer. A save file therefore cannot decode differently on
  another toolchain. The residual is unchanged and is still the whole risk: the kernel's own
  arithmetic when it *computes* those samples. `Adapter.Determinism` has still not been run.
- **Result:** *open — specified, guarded by fixtures, not yet measured across platforms.*
- **Decision:** *open*


## R-015 — Durable name publication on Windows is unproved

- **Severity:** High if it bites — the failure mode is a world that boots to a checkpoint
  older than the one the server acknowledged, or, if the containment argument is wrong, a
  root that references an object the filesystem has forgotten.
- **Probability:** Low to Medium, and genuinely unmeasured. No in-process test can settle it.
- **What it is.** UE 5.8 routes `IFileHandle::Flush` to `FlushFileBuffers`
  (`WindowsPlatformFile.cpp:933`), which proves that a file's **contents** reach the device.
  It proves nothing about the durability of a newly created **directory entry**, and Win32
  exposes no directory-flush primitive at all. P-004 §12 states this rather than assuming it.
- **What already guards it.** The four things that decide which state is current — two root
  slots and two anchor slots — are **pre-created, fixed-size and overwritten in place**, so
  they create no new names at runtime and do not depend on namespace durability. New names
  are created only for immutable content-addressed objects, which are referenced only after a
  slot naming them is published; if a crash loses such an entry, the slot's closure validation
  fails on boot and the **previous** generation's root is used. That is a lost checkpoint, not
  a corrupt world. **This is a containment argument, not a durability proof.**
- **Why it costs little to be wrong about the mode.** No schema-2 field contains a path, so
  adopting P-003's preallocated-container fallback changes the object addressing map and not
  one stored byte.
- **CP-015 — the mechanism that will test this now exists.** `FTerrainFaultDevice` can fail
  **or tear** any single storage operation, and `FTerrainSlotPair` is already tested against a
  torn publication: the damaged slot is rejected with `BodyChecksumMismatch`, the other slot
  still holds the last acknowledged generation, and the retry repairs the damaged slot rather
  than touching the good one. That is the *containment* half of P-004 §12's argument,
  demonstrated rather than asserted. It is **not** the durability half: a fault injected in
  process is not a power loss, and nothing here tests whether a directory entry survives one.
- **CP-016 — containment is now demonstrated exhaustively, and this risk became the gate on
  two separate things.** `Persistence.CrashMatrix` (**D-040**) fails **every** mutating write
  of a scripted session in turn, hard and torn, and every recovery matched the reference world
  at its own OpSeq; all 40 injections are accounted for, and no established world was made
  unopenable. That is the containment argument proved across the whole write sequence rather
  than at chosen points. **It is still not power loss.** A fault injected in process models a
  *lost write*; nothing in it can say whether a directory entry survives a power cut.
- **And retention raised the stakes (D-039).** Pack compaction removes the original file after
  writing a replacement, and a replacement can hold objects shared by **both** roots — so
  losing that one new name defeats both retained generations at once. Every other failure in
  this system leaves a fallback; this is the only one that would not. `Terrain.Reclaim` is
  therefore gated behind `-TerrainRetentionExperiment` until this risk is settled.
- **Mitigation experiment:** power-loss-class testing of name creation (an external harness, a
  VM with host-level power control, or an equivalent), or an explicit decision to adopt
  P-003's preallocated-container mode instead. The latter costs no stored bytes, which is why
  it stays the cheap escape.
- **Owner / task:** build step 4. **This is now the highest-value open item in the persistence
  stack**: it blocks retention running for real, and it is what would extend the crash
  matrix's claims from lost writes to power loss.
- **CP-017 — CLOSED BY CONSTRUCTION (D-041, P-005).** The question was not answered; it was
  made irrelevant. Objects are frames appended to four pre-created containers, compaction
  truncates instead of deleting, and an open world creates and removes no names — asserted as a
  count (0 and 0 across three captures and a full compaction) and checked on a real disk (the
  file-name set is identical before and after reclamation). After bootstrap, the crash matrix's
  lost-write model is the power-loss model under the per-file flush contract.
- **Residuals, kept visible:** (1) the per-file flush contract itself, which every acknowledged
  edit already relied on; (2) on Windows, the seconds after world creation — bootstrap names are
  synced with `FlushFileBuffers` on directory handles, accepted by NTFS here but undocumented;
  a loss there makes the world refuse to open, never open wrong; (3) the Unix `fsync(dir)` branch
  is not yet compiled on Linux; (4) journal `Rotate` still creates a name and is barred from
  production until trimming uses a pre-created ring.
- **Result:** *closed at CP-017 by construction; residuals above.*
- **Decision:** **D-041**

## R-016 — R3 work reviewed by its own author

- **Severity:** Medium — it does not break anything by itself; it weakens the evidence that
  nothing is broken, on a subsystem that contains a permanent save format.
- **Probability:** N/A — this is a known exposure, not a hazard that may or may not occur.
- **What it is.** `AGENTS.md` §2 requires that the writer is not the reviewer for R3 work.
  The Director lifted that for T-117 (**D-033** §5), so P-004 and its codecs were written and
  reviewed by the same agent. The self-review was real and found six issues, two of which
  mattered — a torn-tail rule that would have discarded acknowledged history, and a cap
  enforced by `checkf`, which compiles out of a shipping build. Finding two defects of that
  size in one's own work is also evidence that a second reader would find more.
- **The specific thing that is unproved.** The 16 golden vectors were produced by the same
  implementation they now pin. They lock the format against future drift, which is their job.
  They do **not** prove the code matches P-004's byte tables, because one author wrote both.
- **Mitigation experiment:** a pass over P-004 and `893a029` that **reimplements the
  objects from the document alone** and compares them against the pinned hashes.
- **RESULT, 2026-09-20 — the experiment was run, and it passed.** An independent encoder was
  written in Python from P-004's byte tables, in a disposable scratchpad venv with real
  BLAKE3 and XXH3 (both verified against their published test vectors first; nothing was
  installed into the machine's Python). It reproduced **16 of 16 pinned golden vectors
  exactly** — every object type, both slot files, both record frames, the intent digest, the
  record digest and the WorldTag. **P-004's tables and the C++ encoders agree.** That is the
  specific thing this risk said was unproved, and it is now proved.
- **Two findings came out of it, both fixed:**
  - **F-1, the real one — the document understated the format.** The decoders enforce
    reference-validity rules that P-004 never stated: nonzero `SegmentId` / `FirstOpSeq`,
    nonzero anchor segment IDs, `PredecessorLastOpSeq == 0` when there is no predecessor,
    nonzero root-slot descriptor references with an upper bound, an upper bound on
    `RootPageLength`, and nonzero index child digests and lengths. **A decoder written from
    P-004 alone would have accepted objects this one rejects** — and the more permissive
    implementation is the one that accepts a corrupt world. The rules are now stated in
    §§6.2, 7, 8, 9.1 and 9.5, and P-004 §1 gained rule 11: a field rule a conforming decoder
    enforces is written down in the section that defines the field, and anything not stated
    there is not a requirement a decoder may invent.
  - **F-2 — one condition, two error codes.** An over-cap SparseDiff sample count was
    `FieldOutOfRange` on encode and `CapExceeded` on decode. The taxonomy only earns its keep
    if the same condition reports the same way. Both are `CapExceeded` now.
  Both findings have their own test cases; the suite is **24 of 24** after them, and the
  golden vectors are unchanged because neither finding moved a byte.
- **What is still NOT proved, stated precisely.** The reimplementation covered **encoders**.
  It did not independently implement a **decoder**, the **path-copy index algorithm**, or the
  **segment scanner's torn-tail logic** — the three places where behaviour is more than a
  byte layout. And no reimplementation can review a **design**: whether schema 2 is the right
  format is still a judgement only a second reader can second.
- **CP-016 — the residual was attacked by the other agent, and it was right to be.** T-124's
  retention pass was written by Claude and reviewed by Codex (**D-039**), which is the first
  R3 increment this project has put through a genuine second reader. It found **six defects**,
  and the two that mattered have a shape worth recording because neither would ever have been
  caught by its author:
  - the mark **added** payload digests to the live set instead of **loading** them, so a
    checkpoint with a missing payload marked clean and reclamation proceeded;
  - the fallback test **computed** its comparison hashes and never **compared** them.

  Both *looked* like checks. Their author already believed the thing they were supposed to
  prove, which is exactly why he wrote them that way and exactly why he would have read past
  them again. A seventh finding invalidated a result already reported to the Director: a
  workload repeating `Add` on solid terrain is idempotent, so two "production" runs changed
  zero voxels and measured nothing.
- **What that does to this risk.** It does not close it — P-004 and its codecs are still
  single-author, and that is what this risk names. But it converts the argument from a
  plausible worry into a measured hit rate: **one increment, six real defects, two of them
  recovery-critical.** The case for alternating authorship (**D-028**) is no longer a process
  preference; it is the only reason those two defects are not in the save format today.
- **Owner / task:** residual is a design read by the vendor that did not write T-117.
- **Result:** *substantially mitigated on the byte-level claim, and newly evidenced on the
  general one: cross-agent review of R3 work found six defects in a single increment.
  P-004's decoder behaviour, the index algorithm and the design judgement remain
  single-author work.*
- **Decision:** *open, and now cheap to close either way. The Director may accept the residual
  and move on; that is his call and not a thing to re-litigate.*

- **CP-019 — four more R3 increments by a single author.** T-128 through T-131 (P-007 to P-010)
  were written and self-reviewed by Claude under the Director's standing instruction.
  - The self-reviews found real defects before they counted as evidence: a multiplayer material
    check that passed a deliberately broken build; E-1 first compared against the wrong truth; a
    database close with a live statement, which crashed; and SQLite's silent WAL fallback.
  - Each headline claim was mutation-tested: the lease, the join/resync path, snapshot materials,
    and the boot settlement pass.
  - None of P-005 to P-010 has had a cross-vendor read. That is the cheapest remaining way to
    find what one author reads past.
- **CP-021 — the cross-vendor read happened, and it found what the self-reviews missed.**
  Codex reviewed CP-016 to CP-020 and found **two P1 and four P2 defects**. All six were in
  integration and failure paths that the author's isolated tests had passed.
  - The P1s: the G ≤ W guard was never wired in normal play, and a copy-before-write failure was
    never seen by the service.
  - The stress gate accepted a failed ledger audit.
  - F1 and F2 are fixed (D-048). The fix's own self-review found a third defect of the same
    kind.
  - **The lesson for this project:** "the component test passes" has repeatedly not meant "the
    owner uses the component correctly". Service-level tests with mutations are now the standard
    for persistence fixes (`Capture.Service`).
- **CP-022 — F3 to F5 closed (D-049).** Five of the six findings are now fixed and
  mutation-checked. The fix for F5 confirmed that the reviewer was right twice over: the old crash
  oracle lacked a lower bound, **and** its upper bound would have rejected a legitimate recovery
  after a lost acknowledgement. Only F6 remains, which is Linux-only (R-007).

## R-017 — Player identity is not yet durable

- **Severity:** Medium — inventories are keyed by owner, and an owner that changes between
  sessions is a player who loses everything they dug.
- **Probability:** High until a real login exists. The owner is the BLAKE3 of the player's unique
  net id, and the project runs on Unreal's Null online subsystem, whose ids are per machine at
  best.
- **Evidence so far (CP-019):**
  - Three clients on one machine got distinct ids.
  - The ids stayed the same across server travel.
  - Survival across a game restart, or on another machine, is unmeasured.
- **Mitigation:** adopt a real identity (Steam or EOS) before inventories matter to players. That
  choice costs nothing but is platform-facing, so it is a Director decision when the time comes.
  Until then the ledger's rows are correct but their owner key may not follow the person.
- **Owner / task:** Phase 4 (multiplayer vertical slice)
- **Result:** *open*
- **Decision:** *open*

## R-018 — The server cannot yet sustain the 96 edits/s design point

- **Severity:** Medium — at 32 players digging continuously, edits would queue for seconds and
  some would be refused; server frames would run 2–3× long, which players feel as lag.
- **Probability:** High on the development machine (measured). Unknown on the Linux server.
- **Evidence (CP-020, P-011):**
  - 71–78 edits/s sustained, with server frames p95 65–80 ms.
  - One journal flush per edit takes 3.5 ms on the game thread; checkpoint re-reads cost about
    3 ms per chunk.
  - Settlement is not the bottleneck: the window never filled.
- **Mitigation experiment:** journal group commit (T-133), then re-run
  `Tools/Test-TerrainStress.ps1`. Target: 96/s sustained with frames near 33 ms.
- **CP-021/CP-022 re-runs:** 76.7/s and 68.3/s, with correctness unchanged. Run-to-run spread on
  this machine is about ±5/s, so T-133's result needs several runs, not one.
- **Owner / task:** T-133
- **Result:** *open*
- **Decision:** D-047 (technical)


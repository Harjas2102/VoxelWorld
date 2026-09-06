# CP-002 — Copy-paste prompt for Claude Code

> **Director: before pasting.** On the gaming PC:
> 1. Download from the planning chat: `OPERATIONS.md`, `DUAL_AGENT_SETUP.md`,
>    `REVIEW_2026-09-05_Claude.md`, and this file. From ChatGPT: the two GPT
>    handoff `.md` files.
> 2. Put all six in a new folder `C:\Dev\VoxelWorld\_incoming\`.
> 3. Open PowerShell → `cd C:\Dev\VoxelWorld` → `git pull` → `claude` →
>    `/model` → Opus → press **Shift+Tab** until it says **plan mode**.
> 4. Paste everything below the line. Approve the plan, then approve each
>    action as it asks. Expect 3–4 commits.

---

CP-002 — ARCHITECTURE REVIEW ADOPTED. Director-authorized 2026-09-05. Execute
in plan mode: show me the plan first, then perform each part, committing at
the marked points. Stop and ask on any ambiguity; do not invent designs or
API names. Do not modify any `.uasset`/`.umap` file in this session.

## PART 0 — Preflight (report a table before changing anything)

1. Confirm working directory contains `VoxelWorld.uproject`.
2. `git status`, `git remote -v`, `git fetch origin`, `git pull`, then
   `git push --dry-run origin main` to prove push credentials still work.
   If auth fails, stop and give me the exact Git Credential Manager
   re-authentication steps.
3. `git lfs version` and `git lfs ls-files | measure` (LFS healthy).
4. Locate the engine: find `UnrealEditor.exe` under `C:\Program Files\Epic
   Games\UE_5.7\Engine\Binaries\Win64\` (search if not there). Record path.
5. Check Visual Studio 2022 + C++ game workload: run `vswhere` (under
   `C:\Program Files (x86)\Microsoft Visual Studio\Installer\`) or check for
   `cl.exe`. Record present/absent — do not install yet.
6. Confirm `_incoming\` contains six files (list them). If any are missing,
   tell me which and continue with what exists.
7. Read fully: `CLAUDE.md`, every file in `Docs\`, and the three review files
   in `_incoming\`. Confirm the repo is at CP-001.
8. Report the preflight table. Wait for my "go".

## PART 1 — Context you must hold for this session

On 2026-09-05 the project underwent an external architecture review by
GPT-5.6 (two documents) and a response review by Claude Fable 5.1. The
Director accepted all findings and recommendations of the Claude review
(which adopts most of GPT's, scopes some down). Summary of what was decided:

- The game's identity: a persistent multiplayer world that **permanently
  records what players did to it**; technology progression = increasing scale
  of environmental control. Voxels are infrastructure, not art direction.
- The June plan's errors, now corrected: D-001 bundled requirement + backend;
  terrain multiplayer was deferred despite D-002; the "non-Nanite voxel"
  assumption was stale; 1 km² island was too big a first target; CLAUDE.md as
  constitution was vendor lock; repo visibility/token housekeeping.
- **Plugin licensing discovery:** Voxel Plugin 2 is NOT free (Fab "Voxel
  Plugin Installer" requires owning Voxel Plugin Pro Legacy; docs last
  targeted UE 5.5/5.6). **Voxel Plugin Free Legacy** (GitHub
  `VoxelPlugin/VoxelPluginFreeLegacy`) compiles on 5.6/5.7/5.8 and provides
  prebuilt 5.7 binaries. It is the initial provisional backend.
- Roadmap reordered around a **terrain feasibility gate** ("one hill is
  trustworthy") before any ordinary survival content.
- Governance becomes vendor-neutral (`AGENTS.md`), with Claude Code as the
  current implementer on Opus under Claude Pro, and dual-vendor adversarial
  review reserved for R3 milestones.
- A **7-day rule**: if the first hole isn't dug within 7 days of CP-002, the
  step gets cut smaller.

## PART 2 — Move the incoming files

- `_incoming\OPERATIONS.md` → `Docs\OPERATIONS.md`
- `_incoming\DUAL_AGENT_SETUP.md` → `Docs\DUAL_AGENT_SETUP.md`
- `_incoming\REVIEW_2026-09-05_Claude.md` → `Docs\reviews\2026-09-05_Claude_Review.md`
- `_incoming\VoxelWorld_Master_Project_Architecture_and_AI_Authoring_Handoff.md`
  → `Docs\reviews\2026-09-05_GPT56_Master_Handoff.md`
- `_incoming\VoxelWorld_Optional_GLM53_Flash_Worker_Strategy.md`
  → `Docs\reviews\2026-09-05_GPT56_GLM_Worker_Strategy.md`
- `_incoming\CP002_CLAUDE_CODE_PROMPT.md` → `Docs\prompts\CP002_prompt.md`
- Delete `_incoming\` when empty.

## PART 3 — Governance patch (create/rewrite these files)

### 3a. `AGENTS.md` (repo root) — the vendor-neutral constitution
Write it from these requirements (concise, imperative, no prose padding):
- **Session start:** read `Docs/STATE.md` and `Docs/VISION.md` (and
  `Docs/ARCHITECTURE.md`, `Docs/RISKS.md` when they exist); confirm current
  task; never rely on chat memory; if docs and conversation conflict, ask.
- **Roles:** Director (human; all rulings, pillars, scope, acceptance, UE
  testing) · Architect (proposals only) · Implementer (bounded increments; no
  architectural authority; stops on ambiguity) · Independent reviewer (assumes
  the author is wrong; finds flaws; no implementation). Current holders are
  operational choices recorded in STATE, not in this file.
- **Risk classes:** R0 mechanical · R1 bounded implementation · R2 localized
  design · R3 subsystem architecture (terrain, replication, persistence, JIP,
  power graph, threading, save migration) · R4 vision/irreversible (Director
  only). R3 requires a proposal file, an independent review, and a ruling
  before implementation.
- **Engineering rules:** server-authoritative always; C++ owns simulation/
  networking/data, Blueprint owns feel/UI/tuning/asset hookup; no per-machine
  ticking replicated actors (graph simulation, replicate deltas); terrain
  edits replicate as operations, never meshes; distance relevancy on all
  replication; **gameplay never calls the terrain plugin directly — only
  through the game-owned terrain service/adapter (D-011)**; persistence
  formats versioned, flat, inspectable; every persistent format has a
  schema version and migration path; small increments; compile + multiplayer
  PIE test before "done"; no new architectural dependency without a decision.
- **Output format for implementation tasks:** goal → complete code/files →
  exact placement → how to run/compile → expected output. Blueprint work =
  numbered node instructions, verified by Director screenshot.
- **Action words** (execute exactly): `resume`, `status`, `next`, `go`,
  `checkpoint`, `push`, `pull`, `recite the vision`, `stuck`, `explain`,
  `undo`, `review this`, `packet`, `rule D-0XX:`, `park` — with the
  definitions in `Docs/OPERATIONS.md` §4 (copy them in).
- **Checkpoint protocol:** on `checkpoint` or session end: update STATE.md
  (new CP number/date/state/current task), DECISIONS.md entries for rulings,
  BACKLOG.md moves (done → Done log), RISKS.md updates; commit
  `type(scope): summary`; push; confirm `main -> main`.
- **Git rules:** one scoped task per commit; never mix architecture with
  unrelated changes; never rewrite history; never commit secrets/tokens;
  `.uasset`/`.umap` only when the task explicitly requires it.
- **Drift guard:** anything contradicting VISION.md (blocky terrain,
  client-authoritative shortcuts, space, 100+ players, commercial features,
  direct plugin calls from gameplay) is flagged and needs a decision.
- **Escalation rule:** if a specification does not uniquely determine an
  architectural choice, stop and report; do not improvise.
- **Director profile:** intermediate technical builder; strong at debugging,
  Linux, networking; Git novice (exact commands); struggles with UE editor
  navigation — prefer config/C++/Python-scripted editor actions over menu
  instructions; when menus are unavoidable, give exact click paths.

### 3b. `CLAUDE.md` → thin adapter
Replace contents with: "Read `AGENTS.md`; it is authoritative. Claude-specific
notes:" + 3–5 lines (use plan mode for R2+; `/model` Opus; `/effort` high for
R3; run `git pull` before starting; this project has no API budget — respect
Pro usage windows).

### 3c. `Docs/RISKS.md`
Seed with fields (id, severity, probability, mitigation experiment, owner/
task, result, decision):
R-001 terrain backend multiplayer synchronization · R-002 JIP modified-chunk
transfer · R-003 terrain save growth/compaction · R-004 material-yield
accuracy · R-005 PCG/foliage invalidation after edits · R-006 dynamic
navigation after edits · R-007 dedicated-server/plugin compatibility ·
**R-008 plugin licensing/version risk** (Free Legacy is maintenance-mode; VP2
is paid and engine-version-gated; mitigation = adapter D-011 + gate) ·
R-009 Director availability/stall (mitigation = 7-day rule, one-evening
tasks).

### 3d. `Docs/ARCHITECTURE.md` (stub, v0)
Contents: the layer diagram — Gameplay/Tools/Machines → Authoritative Terrain
Service (permissions, validation, materials, op semantics, revisions, yield)
→ Persistent World State (deterministic base, chunk snapshots, edit journal)
→ Terrain Backend Adapter (Voxel Plugin Free Legacy initially; replaceable)
→ Rendered terrain/collision/queries. State that names like
`ITerrainBackend`, `FTerrainEditOp`, `UTerrainAuthoritySubsystem` are
placeholders until the T-101A findings and the blind benchmark
(DUAL_AGENT_SETUP §6) produce v1. Include: server is sequencing authority
(monotonic op IDs + per-chunk revisions); multi-chunk ops sequenced once
globally; prediction deferred; relevancy spatial; validation of reach,
radius, frequency, permissions even on a friends server.

**COMMIT 1:** `docs(governance): AGENTS.md constitution, CLAUDE.md adapter, RISKS + ARCHITECTURE stubs, reviews archived (CP-002 part 1)`

## PART 4 — Decisions (append to DECISIONS.md verbatim; date 2026-09-05; STATUS ACCEPTED)

Also annotate D-001: add a line "**Superseded in part by D-010** (backend
provisional; requirement retained)."

**D-010 — Terrain requirement vs backend.** Context: D-001 fused gameplay
requirement, representation, and vendor. Decision: The game requires a
persistent, server-authoritative, deformable world supporting arbitrary
volumetric excavation/addition (tunnels, mining, earthworks, later
industrial-scale terraforming). This requirement is durable. The terrain
backend is provisional: initial candidate **Voxel Plugin Free Legacy** (free,
5.7 binaries); **Voxel Plugin 2** is the upgrade candidate when budget and
engine compatibility allow. No backend is permanent until it passes the
T-101B feasibility gate. Consequences: backend code isolated behind D-011
adapter; R-008 tracks licensing/version risk.

**D-011 — Authoritative terrain service.** Decision: gameplay owns edit
operations, material semantics, permissions, revisions, and resource yield;
the plugin sits behind a game-owned adapter and is never economic authority.
Gameplay code never includes plugin headers directly. Consequences:
replaceable backend; testable without renderer; plugin-specific types stay
in the adapter module.

**D-012 — Persistence model (provisional).** Decision: deterministic base
world (seed + generator version + authored stamps) never saved redundantly;
per-modified-chunk snapshot at revision R plus append-only operation journal
after R; compaction on thresholds; monotonic revisions; SQLite for entities
(players, inventories, structures, machines, grids); chunk data in versioned
files. Consequences: JIP = snapshot + ops since; every format versioned with
migration path; old-save fixtures kept under `Tests/Saves/`.

**D-013 — Terrain multiplayer moves into the gate.** Decision: no terrain
backend is accepted on solo sculpting. Concurrent edits, save/restart, and
join-in-progress are pass criteria of T-101B. Supersedes the June BACKLOG
phasing that deferred sync to Phase 3. Consequences: Phase 1 = terrain
feasibility; ordinary survival content waits.

**D-014 — Vendor-neutral AI governance.** Decision: `/Docs` + `AGENTS.md`
are the constitution; roles (Architect/Implementer/Reviewer) are functions
held by replaceable models; current implementer = Claude Code on Opus under
Claude Pro; primary implementer for Phase 1B+ chosen by the blind benchmark
(DUAL_AGENT_SETUP §6); writer ≠ reviewer for R3; no API spending until
programmatic orchestration has real value; GLM worker strategy parked until
≥10 bounded tasks exist. Consequences: CLAUDE.md is an adapter; any vendor
can be swapped without changing workflow.

**D-015 — VISION amendment: voxels are infrastructure, not art direction.**
Decision: add to VISION.md the principle that only terrain/geology is
volumetric; buildings, machines, props, trees, rocks, foliage are
conventional meshes/Nanite/PCG; the voxel grid should be as invisible to the
player as the physics broadphase. Add the identity statement: the world
permanently records what the players did to it. Add drift checks: "terrain
backend remains replaceable" and "voxels are invisible to the player."
Consequences: codename stays codename; final title should not mention voxels.

**D-016 — Progression identity.** Decision: technology progression is framed
as increasing scale of environmental control — Stage 0 human labor → Stage 1
organized workshop → Stage 2 powered control → Stage 3 industrial logistics
→ Stage 4 landscape-scale infrastructure; "the map physically records the
factory's growth." Survival pressure comes from environment mastery
(temperature, exposure, darkness, hazards), not default bar-maintenance
chores; food exists without becoming a tax unless testing proves it fun.
Consequences: GDD progression and survival sections rewritten; survival
meters remain an open, test-resolved question.

## PART 5 — Doc corrections

- **VISION.md:** apply D-015 exactly (new principle section, identity
  statement, two drift checks). Nothing else changes.
- **GDD.md:** correct the Nanite statement (VP2 supports runtime Nanite;
  Free Legacy does not — terrain material quality carries the look until a
  VP2 upgrade); replace the tech-tier table with D-016 stages; rewrite the
  survival paragraph per D-016; add "World authoring: procedural foundation +
  authored geography + player-created history"; add "First test world:
  256–512 m hill with strata, an ore body, forest, water/lowland, cliff";
  note backend = Free Legacy provisional (D-010). Bump to v0.2 (CP-002).
- **STATE.md → CP-002 (2026-09-05):** Phase 1 — Terrain Feasibility. What
  exists (unchanged from CP-001 in-engine; governance/docs updated). Repo
  visibility: **public** (correct the record; see Part 6). Current task:
  **T-100** (VS 2022 + C++ workload, parallel) and **T-101A** (Free Legacy
  install + first hole; one evening; 7-day rule). Toolchain table updated.
  Open decisions: D-008 title, survival meters (test-resolved), transport
  method, integrity model, D-017/D-018 (benchmark outcomes).
- **BACKLOG.md v2** — replace the phase structure:
  - **Phase 1 — Terrain Feasibility (NOW).** T-100 VS 2022 Community + "Game
    development with C++" workload (Claude Code attempts `winget install
    Microsoft.VisualStudio.2022.Community` with the NativeGame workload;
    Director approves UAC; fallback manual). T-101A smoke test (Free Legacy
    installed, hill, dig + add in PIE, tunnel/overhang, screenshot; solo;
    one evening). T-101B tiered gate — Gate-Critical: smooth non-blocky
    terrain, material queries (soil/stone/ore), authoritative edit path
    (client request → server validate → apply → replicate), concurrent
    2–3-client edits with deterministic ordering, save/restart exactness,
    join-in-progress reconstruction, material yield measured from removed
    volume, edit stress (hundreds–thousands of ops; frame time, rebuild
    latency, save growth, bandwidth, memory, collision cost); Gate-Observe:
    collision edge cases, foliage/PCG response, nav dirtying, unsupported/
    floating terrain behavior, streaming. Exit: PASS / CONDITIONAL / FAIL /
    VISION CHANGE. Sub-steps 1C material field + yield, 1D journal +
    snapshot + restart, 1E JIP + relevancy, 1F stress + decide. Blind
    benchmark (DUAL_AGENT_SETUP §6) sits between 1A and 1B and yields
    ARCHITECTURE v1, D-017, D-018. **Milestone: one hill is trustworthy.**
    Also here: T-107 first-person camera rig (unchanged, cheap, can slot in
    any time after 1A).
  - **Phase 2 — First complete physical loop:** interact/tool framework,
    tree harvest, terrain mining, authoritative inventory (C++ base + BP UI),
    one craftable tool, campfire or workbench, save/restart. Milestone: join,
    gather, dig an ore vein, craft, log out, server restart, everything
    persists.
  - **Phase 3 — Building:** foundation/wall/ceiling/doorframe/door, placement
    validation, persistence, terrain-under-structure policy (first version:
    block edits under critical bounds or allow floating — decide, don't
    simulate).
  - **Phase 4 — Multiplayer vertical slice:** engine-from-source, Linux
    dedicated server, remote friends 2–4, reconnect, telemetry. Milestone:
    harvest → mine → craft → build → persist with friends.
  - **Phase 5 — Environment/survival:** day/night, one hazard, one
    mitigation, basic wildlife.
  - **Phase 6 — First electricity:** generator, fuel, power graph, cables,
    light, powered furnace, persistence, delta replication.
  - **Phase 7 — First automation:** one material path (ore → powered miner →
    transport → processor → storage).
  - **Later/parked:** biomes, weather, continent, industrial excavation,
    roads, logistics, creatures/taming, PvP zones, integrity, 16–32 tuning,
    MCP editor control (evaluate after T-101B), GLM worker tier.
  - Map old T-102…T-106 into the new phases; keep the Done log.
- **SETUP.md:** add a one-line banner at top: "Superseded for daily
  operation by OPERATIONS.md; kept as the environment build record."
- **README.md** (repo root, new, short): codename note, one-paragraph
  description, "start at Docs/OPERATIONS.md", link to VISION.

**COMMIT 2:** `docs: checkpoint CP-002 — architecture review adopted (D-010…D-016), BACKLOG v2, VISION/GDD/STATE updated`

## PART 6 — Security and housekeeping

1. Open `Config/DefaultEngine.ini`; locate the `[/Script/AndroidFileServerEditor.AndroidFileServerRuntimeSettings]`
   section (or any `SecurityToken=` line). Remove the token line/section —
   this is a desktop project; the Android file server is irrelevant. Scan all
   `Config/*.ini` and the repo for anything resembling keys/tokens; report.
2. Repo visibility: it is currently **public** (STATE said private). Record
   "public" in STATE. Recommendation to Director: keep public for now — it
   lets any vendor read the docs by URL for reviews, and the repo contains no
   secrets after step 1 — but ask me to confirm; do not change visibility
   yourself.
3. `.gitignore` additions: `Plugins/VoxelFree/`, `_incoming/`, `Tools/downloads/`,
   `*.env`, `**/secrets*`.

**COMMIT 3:** `chore(security): remove generated Android file-server token, harden gitignore`

Then `git push origin main` and show `git log origin/main -1`.

## PART 7 — T-101A execution: do as much as possible yourself

Goal: the Director digs the first hole this week with minimal editor
navigation. Prefer config edits, scripts, and CLI over menu instructions.

1. **Install Voxel Plugin Free Legacy (automated).** Write
   `Tools/Install-VoxelFreeLegacy.ps1` that: creates `Tools/downloads/`;
   downloads the 5.7 binaries zip linked in the README of
   `https://github.com/VoxelPlugin/VoxelPluginFreeLegacy` (at time of
   writing:
   `https://api.voxelplugin.com/external/7f2800eb480ae2e0289fecb6994aac5e/VoxelFree-432-e9648b302-5.7-Binaries.zip`
   — fetch the README first and use whatever 5.7 link it shows now);
   extracts it; normalizes the folder so `Plugins\VoxelFree\VoxelFree.uplugin`
   exists at the project root (inspect the zip's internal layout — do not
   assume); prints the resulting tree. Run it (ask me before executing
   downloads). If the download fails, tell me the exact URL to download in a
   browser and where to drop the zip, then continue.
2. **Enable plugins by editing `VoxelWorld.uproject`:** add `VoxelFree`,
   `PythonScriptPlugin`, and `EditorScriptingUtilities` to the `Plugins`
   array (Enabled: true). Keep JSON valid.
3. **Launch the editor from the CLI** using the path from preflight, wait for
   it to open, then read `Saved/Logs/VoxelWorld.log` and confirm the VoxelFree
   plugin loaded without errors. If shader compilation starts, tell me to
   wait it out.
4. **Read the plugin's own headers** in `Plugins/VoxelFree/Source/` to learn
   the exact class/function names for: the voxel world actor, world
   generators, and sphere add/remove tools. **Do not guess names.**
5. **Editor scripting:** write `Tools/Editor/place_voxel_world.py` that
   spawns the voxel world actor at the origin in the current level with a
   simple noise generator and a reasonable size (values from the headers/
   examples), so I don't have to find it in menus. Tell me exactly how to run
   it (editor Output Log: `py C:\Dev\VoxelWorld\Tools\Editor\place_voxel_world.py`).
6. **Dig input — fastest path first (Blueprint, no compiler needed):** give
   me numbered node-by-node instructions to add to the template character
   Blueprint: Left Mouse → line trace from camera (≈500 cm) → plugin's
   "remove sphere" tool at the hit location (radius ≈100 cm); Right Mouse →
   "add sphere". I will screenshot the graph; verify it. If T-100 (VS 2022)
   is already done and you judge C++ faster, offer the C++ component
   alternative — but the hole gets dug this week either way.
7. **Definition of done for T-101A:** screenshot of a dug hole and an added
   mound in PIE, a tunnel or overhang, no editor errors in the log, script +
   .uproject changes committed and pushed, findings (plugin API notes, what
   the plugin does/doesn't provide for materials, saves, multiplayer) written
   to `Docs/T-101A_FINDINGS.md`.
8. Save all of Part 7 as `Docs/T-101A_RUNBOOK.md` so it can be re-followed.

**COMMIT 4:** `feat(terrain): T-101A tooling — Free Legacy install script, plugin enablement, editor placement script, runbook`

## PART 8 — Required shape of your final message

1. Preflight table (from Part 0).
2. Files created/changed, grouped by commit, with commit hashes.
3. Push confirmation (`git log origin/main -1`).
4. **START HERE NEXT** — a numbered list of exactly what I do next, in order,
   with exact clicks/keys/commands, beginning with T-100 (or telling me VS is
   already present) and running through T-101A step by step until the first
   hole is dug.
5. Open questions needing a ruling (repo visibility; anything you escalated).

Ambiguity rule for this whole session: if something is unclear or a name
can't be verified from files on disk, stop and ask — do not improvise.

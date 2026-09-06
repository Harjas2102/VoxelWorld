# DECISIONS.md — Decision Log

> Every design or architecture fork gets a numbered entry. The Director rules;
> Claude records. Entries are never deleted — superseded decisions get status
> SUPERSEDED with a pointer to the replacement.

**Template:**
```
## D-0XX — Title (YYYY-MM-DD) — STATUS
Context · Decision · Consequences
```

---

## D-001 — Smooth voxel terrain via Voxel Plugin (2026-06-12) — ACCEPTED
**Context:** Minecraft-grade world manipulation with Rust-grade realism. Stock
UE Landscape cannot deform at runtime; blocky voxels violate Pillar 2.
**Decision:** Marching-cubes smooth voxel terrain using Voxel Plugin (free
tier to prototype, Pro when justified). Nanite reserved for static meshes.
**Consequences:** Terrain look depends on materials, not Nanite. A custom
replication + persistence layer for voxel edits is our engineering burden.
**Superseded in part by D-010 (backend provisional; requirement retained).**

## D-002 — Server-authoritative from day one (2026-06-12) — ACCEPTED
**Context:** Retrofitting multiplayer into a single-player UE project is
effectively a rewrite.
**Decision:** Every system is written and tested against a dedicated-server
model from its first version. Multiplayer PIE testing is the daily loop.
Dedicated Linux server is the shipping target.
**Consequences:** Slightly slower feature development; zero MP-retrofit risk.
Requires engine-from-source build for the Linux server target (~Phase 3).

## D-003 — Scale target: 16–32 players, self-hosted (2026-06-12) — ACCEPTED
**Context:** Rust-scale (50–100+) demands custom netcode; co-op (2–8) wastes
the concept's social potential.
**Decision:** 16–32 players on one self-hosted dedicated server. Stock UE
replication with aggressive distance relevancy. Direct IP connect.
**Consequences:** No platform services, no matchmaking, no anti-cheat arms
race. Replication discipline (D-006 rules) is mandatory, not optional.

## D-004 — PvE core, zoned PvP later (2026-06-12) — ACCEPTED
**Context:** Full-loot PvP forces raid protection, balance, and anti-cheat
work from day one and risks friend-group burnout.
**Decision:** Ship PvE-only first. Add server-side PvP zone volumes (~M12+)
that flip damage/build rules. Contested zones hold the richest resources.
**Consequences:** Year 1 stays buildable. Structural integrity/decay must be
decided before PvP ships (GDD open question #3).

## D-005 — Creatures deferred (2026-06-12) — ACCEPTED
**Context:** Ark-style taming/breeding/command AI is the single largest
content sink in the inspiration set.
**Decision:** Year 1: passive huntable wildlife only (simple behavior trees).
Year 2: hostiles, taming, breeding.
**Consequences:** Survival loop gets meat early; the Ark fantasy waits.

## D-006 — Code strategy: C++-forward hybrid (2026-06-12) — ACCEPTED
**Context:** Claude (primary programmer) writes complete C++ files fluently
but cannot draw Blueprint graphs — only describe them and verify screenshots.
**Decision:** C++ owns simulation, networking, and data (voxel sync, power
graph, persistence, inventory core). Blueprints stay thin children for feel,
UI, tuning, and asset hookup, built from Claude's numbered instructions and
verified by screenshot. Pattern: C++ base (`ABaseHarvestable`) → BP child
(`BP_Harvestable_Tree`).
**Consequences:** Director needs editor fluency + compile workflow, not C++
authorship. Core engineering rules: no per-machine ticking replicated actors
(power/automation = server-side graph sim, replicate state deltas); voxel
edits replicate as operations; distance relevancy everywhere.

## D-007 — Collaboration & continuity system (2026-06-12) — ACCEPTED
**Context:** LLM context decays in long chats; new chats start near-blank.
The founder's original vision must be un-forgettable.
**Decision:** Repo `/Docs` is the single source of truth (VISION, GDD,
DECISIONS, STATE, BACKLOG). Checkpoint protocol: on "checkpoint" or session
end, Claude emits updated STATE.md + decision entries + commit message.
Chats are short, task-scoped, and disposable; every session opens by loading
STATE.md + VISION.md. Toolchain: chat = design office; Claude Code in-repo =
primary programmer (reads CLAUDE.md); UE 5.7 in-editor AI = concierge; MCP
editor control = evaluate ~M3+.
**Consequences:** Continuity survives any context loss, any model change,
any gap in development.

## D-008 — Working title (—) — OPEN
**Context:** The project needs a name for the repo, server, and docs.
**Decision:** Pending Director. Claude to present candidates on request.

## D-009 — Perspective: true first person + 3P toggle (2026-06-12) — ACCEPTED
**Context:** Director ruled the game is first-person. In multiplayer, every
player needs a full-body animated mesh regardless of camera — others always
see whole bodies — so camera style was the real fork. References: Rust and
Tarkov (true FP, visible body), Ark (perspective toggle).
**Decision:** True first person — camera at the head socket of the full-body
character, own body visible looking down — plus an Ark-style toggle to a
third-person boom camera. Still built on the Third Person template; SETUP.md
unchanged.
**Consequences:** Phase 1 gains T-107 (camera rig: head-socket camera, FOV
tuning, near-clip fixes, owner-only head hiding, toggle). Camera is purely
client-side — zero networking cost. Held tools attach to hand sockets once
and work in both views (no duplicate viewmodel meshes, a hidden tax of the
floating-arms approach we avoided). **Known flag:** third-person corner-
peeking is a PvP balance problem — decide whether PvP zones force first
person when D-004 ships.

## D-010 — Terrain requirement vs. backend (2026-09-05) — ACCEPTED
**Context:** D-001 fused three separate decisions into one: the gameplay requirement,
the world representation, and the vendor. Only the first is durable.
**Decision:** The game requires a persistent, server-authoritative, deformable world
supporting arbitrary volumetric excavation and addition — tunnels, mining, earthworks,
and later industrial-scale terraforming. **This requirement is durable.** The terrain
backend is **provisional**: the initial candidate is **Voxel Plugin Free Legacy** (free,
ships 5.7 binaries); **Voxel Plugin 2** is the upgrade candidate when budget and engine
compatibility allow. No backend is permanent until it passes the **T-101B feasibility
gate**.
**Consequences:** Backend code is isolated behind the D-011 adapter. R-008 tracks the
licensing and version risk. A successful solo dig proves almost nothing — the gate
decides.

## D-011 — Authoritative terrain service (2026-09-05) — ACCEPTED
**Context:** If gameplay calls the terrain plugin directly, the plugin API embeds itself
across the whole game: resource logic depends on the renderer, network messages and save
formats become plugin-specific, and replacing the backend becomes a rewrite.
**Decision:** Gameplay owns edit operations, material semantics, permissions, revisions,
and resource yield. The plugin sits behind a game-owned adapter and is **never the
economic authority**. Gameplay code never includes plugin headers directly.
**Consequences:** The backend is replaceable and the service is testable without a
renderer. Plugin-specific types stay inside the adapter module. Mining yield is computed
by the simulation from material actually removed, never inferred from the rendered mesh.

## D-012 — Persistence model (2026-09-05) — ACCEPTED (provisional)
**Context:** Persistence is the pillar. "We changed the struct and the server world is
gone" must never become normal — for a friends server the emotional value of the world
eventually exceeds that of the code.
**Decision:** The deterministic base world (seed + generator version + authored stamps)
is never saved redundantly. Each modified chunk stores a snapshot at revision R plus an
append-only operation journal after R, compacted on thresholds. Revisions are monotonic.
SQLite holds entities — players, inventories, structures, machines, grids; chunk data
lives in versioned files.
**Consequences:** Join-in-progress = snapshot + operations since. Every format carries a
schema version and a migration path. Old-save fixtures are kept under `Tests/Saves/`.

## D-013 — Terrain multiplayer moves into the gate (2026-09-05) — ACCEPTED
**Context:** D-002 requires server authority from day one, yet the June BACKLOG scoped
T-101 as solo and deferred terrain edit sync to Phase 3 — exempting the riskiest system
from the project's own principle.
**Decision:** **No terrain backend is accepted on solo sculpting.** Concurrent edits,
save/restart, and join-in-progress are pass criteria of **T-101B**. This supersedes the
June BACKLOG phasing that deferred sync to Phase 3.
**Consequences:** Phase 1 is terrain feasibility. Ordinary survival content waits until
one hill is trustworthy.

## D-014 — Vendor-neutral AI governance (2026-09-05) — ACCEPTED
**Context:** `CLAUDE.md` as the project constitution made one vendor structurally
load-bearing. The 3-month gap and a cross-vendor review proved the repo-brain works — and
that it should not be branded.
**Decision:** `/Docs` plus `AGENTS.md` are the constitution. Roles — Architect,
Implementer, Reviewer — are functions held by replaceable models. Current implementer:
**Claude Code on Opus under Claude Pro**. The primary implementer for Phase 1B onward is
chosen by the blind benchmark (`Docs/DUAL_AGENT_SETUP.md` section 6). **Writer is not
reviewer for R3 work.** No API spending until programmatic orchestration has real value.
The GLM worker strategy is **parked** until at least 10 bounded tasks exist.
**Consequences:** `CLAUDE.md` becomes a thin adapter. Any vendor can be swapped without
changing the workflow.

## D-015 — VISION amendment: voxels are infrastructure, not art direction (2026-09-05) — ACCEPTED
**Context:** The codename made an implementation detail sound like the identity. The June
GDD already built this way in practice; the principle was simply never stated.
**Decision:** Add to VISION.md the principle that **only terrain and geology are
volumetric** — buildings, machines, props, trees, rocks, and foliage are conventional
meshes / Nanite / PCG. The voxel grid should be as invisible to the player as the physics
broadphase. Add the identity statement: **the world permanently records what the players
did to it.** Add two drift checks: "terrain backend remains replaceable" and "voxels are
invisible to the player."
**Consequences:** The codename stays a codename; the final title should not mention
voxels. Every asset decision is tested against the invisibility rule.

## D-016 — Progression identity (2026-09-05) — ACCEPTED
**Context:** "Primitive → workbench → electricity → automation" is a generic tech tree. It
describes any survival game and distinguishes none.
**Decision:** Technology progression is framed as **increasing scale of environmental
control** — Stage 0 human labour, Stage 1 organized workshop, Stage 2 powered control,
Stage 3 industrial logistics, Stage 4 landscape-scale infrastructure. **"The map
physically records the factory's growth."** Survival pressure comes from environment
mastery — temperature, exposure, darkness, hazards — not default bar-maintenance chores.
Food exists without becoming a tax unless testing proves that loop is fun.
**Consequences:** The GDD progression and survival sections are rewritten. Survival meters
remain an open question resolved by testing, not by genre convention.

## D-017 — Terrain architecture v1: adopt-and-amend (2026-09-06) — ACCEPTED

**Ruled:** 2026-09-06 (CP-005) · **Class:** R3

`P-001-terrain-claude.md` is adopted as the basis for `Docs/ARCHITECTURE.md` v1.
`P-001-terrain-astra.md` is not adopted as an architecture; its evidence table and
experiment discipline are carried into v1.

Astra's review blockers B1–B10 are adopted as a numbered defect list
(`ARCHITECTURE.md` §14), each bound to the earliest build step that depends on it,
rather than as a precondition for all implementation. A build step may not start
while an unresolved defect is bound to it.

Rationale: build steps 0–2 discharge STATE.md's standing obligation (adapter first,
T-101A Blueprint rewired or deleted) and clear both flagged drift checks. Ruling the
full revision first buys correctness that is not yet load-bearing, at the cost of the
project's largest observed failure mode (R-009).

## D-018 — Primary implementer for Phase 1B+ (2026-09-06) — ACCEPTED

**Ruled:** 2026-09-06 (CP-005) · **Amended:** 2026-09-06 (CP-005) · **Class:** R2

Astra (OpenAI, via Codex CLI) is primary implementer for Phase 1B onward.
Claude Code becomes independent reviewer for R3 increments, per D-014.

**Amendment, same session:** the handover happens at **build step 3**, not
immediately. Build steps 0–2 are implemented by Claude Code, which is already
installed and verified in-repo. Installing and validating a second agent before any
C++ module exists adds a session and delays the first compile. Astra is onboarded
against a repo that already builds.

## D-019 — Repository stays public (2026-09-05) — ACCEPTED
**Context:** The repo was created private (CP-001) and the Director made it public
manually so an external ChatGPT audit could read it — the audit that became the CP-002
architecture review. STATE still said "private" until CP-002 corrected the record. With
dual-vendor review now part of the workflow (D-014), the question is whether public is
the right steady state or just an artifact of that one audit.
**Decision:** **The repository stays public.** Any vendor can then read the constitution
and state from a raw URL with no connector, no auth, and no upload — which is exactly
what the blind benchmark (`Docs/DUAL_AGENT_SETUP.md` section 6) needs, since both
vendors must demonstrably see identical context. Raw URLs are listed in
`Docs/CHAT_OPENER.md`.
Note this is a convenience, not a requirement: Codex and similar tools can authenticate
against a private repo. Public is chosen because the friction saved is real and the cost
is currently near zero.
**Consequences:**
- The repo must stay free of secrets. The CP-002 scan is clean and `.gitignore` now
  blocks `*.env` and `**/secrets*`.
- **The generated Android File Server token in history at `4549474` is left in place**,
  per Director ruling. It only ever authorized the Android File Server (USB/network file
  push to an Android device running this project); that plugin is now disabled for the
  project, so nothing regenerates or honours it. Rewriting history to purge it would
  violate the git rules for no security gain.
- One concrete cost to watch: **public repos serve Git LFS objects to anyone who clones,
  and LFS bandwidth is billed to the repo owner** against GitHub's free monthly quota.
  238 LFS files today. Not a problem at this obscurity, but it is the thing that would
  make private worth revisiting — not the source code.
- Revisit if the project ever holds server credentials, private playtest builds, or
  anything with commercial value. Flipping to private later is one click; un-publishing
  what was already cloned is not.

## D-020 — `Lvl_ThirdPerson` is the T-101A test map; `VoxelSandbox` is abandoned (2026-09-06) — ACCEPTED
**Context:** `Content/Maps/VoxelSandbox.umap` was created at CP-002 as the dedicated
terrain sandbox. At CP-003 it ended up holding the failed Voxel-Graph state (an empty
world) and was deliberately left uncommitted. When `place_voxel_world.py` was re-run on
2026-09-06 the editor happened to be on the ThirdPerson template map, so the voxel world
was spawned into `Lvl_ThirdPerson` instead — and that turned out to be the better host,
because it already carries the PlayerStart, GameMode and `BP_ThirdPersonCharacter` that
the dig test needs. `VoxelSandbox` has none of them.
**Decision:** **`Content/ThirdPerson/Lvl_ThirdPerson` is the T-101A map of record.**
`Content/Maps/VoxelSandbox.umap` was reverted to its committed state and is abandoned for
this task, not deleted.
**Consequences:**
- The voxel world actor, the moved PlayerStart and the dig wiring all live in
  `Lvl_ThirdPerson` and its `__ExternalActors__` packages. That level uses One File Per
  Actor, so **each actor is its own package and saving the level does not save them** —
  a trap that cost three failed test launches at T-101A. Scripts must save
  `actor.get_package()`, never `actor.get_outer().get_outermost()`.
- Editing a template map means template content is mixed with test content. Acceptable
  for a smoke test; T-101B should get a purpose-built map with its own PlayerStart and
  GameMode rather than inheriting this one.
- `VoxelSandbox.umap` stays in the repo at its CP-002 state. If a clean sandbox is wanted
  later it needs a PlayerStart and a GameMode added before it is usable.

## D-021 — Solo terrain work runs standalone, not PIE (2026-09-06) — ACCEPTED
**Context:** PIE is configured for the CP-001 three-player replication test
(`PlayNetMode=PIE_ListenServer`, `PlayNumberOfClients=3`). In any non-standalone net mode
Voxel Plugin Free Legacy refuses to use the player camera as its LOD invoker, never
subdivides the render octree, and shows the world as one coarse blob that line traces
miss — while spamming `ClientAdjustPosition` failures, because
`VoxelProceduralMeshComponent` cannot serve as a replicated movement base
(`T-101A_FINDINGS.md` 2d, **R-010**).
**Decision:** **PIE stays on the three-player settings** — that is what T-101B needs.
Solo terrain testing launches standalone via `Tools\Play-Solo.ps1`, which runs as a
separate process so the editor can stay open beside it.
**Consequences:**
- Any solo terrain result obtained through PIE is invalid and should be re-run standalone.
- Multiplayer terrain testing is **blocked on adding a `VoxelInvokerComponent`** to the
  character. That is a hard requirement, not polish, and it is now a T-101B entry cost.
- Standalone writes to `Saved\Logs\Standalone_T101A.log`, separate from the editor's
  `VoxelWorld.log`, so "check the log" stays unambiguous while both run.

## D-022 — ARCHITECTURE.md v1 adopted (2026-09-06) — ACCEPTED

**Ruled:** 2026-09-06 (CP-005) · **Class:** R3

> **Numbering note.** CP-005 drafted this ruling and the two below as D-019, D-020 and
> D-021. Those numbers were already taken (D-019 repository stays public, D-020
> `Lvl_ThirdPerson` is the T-101A test map, D-021 solo work runs standalone), and
> `ARCHITECTURE.md` v1 §2.1 and §6.3 cite D-020 and D-021 with their existing meanings.
> Per "never deleted, only superseded", the existing rulings keep their numbers and the
> CP-005 rulings were renumbered to D-022, D-023 and D-024. D-017 and D-018 are
> unaffected.

`Docs/ARCHITECTURE.md` v1 is the implementation spec for terrain authority,
persistence and replication. It supersedes ARCHITECTURE.md v0 §2 where they differ.

§3 records forks K1–K10. §14 records defects DEF-1…DEF-10, each bound to a build
step. §11 records unknowns E-1…E-9 with their experiments. A build step may not start
while an unresolved defect is bound to it.

**Consequence:** ARCHITECTURE v0 is archived at `Docs/archive/ARCHITECTURE_v0.md`. v1 is
deliberately narrower than v0, so v0's header now records the four things v0 covers that
v1 does not — the adapter-boundary rationale, surface queries as a backend consumer, tool
ownership / cooldown / fuel as validation inputs, and power grids in the entity store.
Those remain live requirements until a later document covers them.

## D-023 — Decision classes: technical rulings move to the Architect (2026-09-06) — ACCEPTED

**Ruled:** 2026-09-06 (CP-005) · **Class:** R2 · **Amends D-014**

Decisions are split into two classes, and only one of them reaches the Director.

**GAME decisions — Director rules.** What the player does, sees, feels and can build.
Scope. Pace of progression. What is fun. What goes in the world. What the game is
called. Anything a player would notice.

**TECHNICAL decisions — Architect rules and logs.** Code structure, module names,
data formats, wire protocols, threading, persistence layout, class shapes, defect
sequencing, tooling. The Director is notified in one line and takes no action.

A technical decision is escalated to the Director only if it (a) changes something a
player would notice, (b) changes project scope, or (c) costs money. AGENTS §10's
"stop at ambiguity" still applies between agents — it no longer routes to the
Director by default.

Rationale: presenting the Director with technical options he cannot evaluate, and
receiving the recommended option back, is not direction. It is ceremony that consumes
the Director's attention and produces no signal. The Director's judgement is the
scarce resource on this project and it is spent on the game.

## D-024 — Forks K1–K10 ruled (2026-09-06) — ACCEPTED

**Ruled:** 2026-09-06 (CP-005) · **Class:** technical (per D-023) · **Architect ruling**

| Fork | Ruling |
|---|---|
| K1 | Revision model: **both** — global `OpSeq` and per-chunk `Rev` |
| K2 | Authority chunk size: **32³ voxels**, revisited only if E-3 fails |
| K3 | Snapshot content: **sparse diff, dense fallback**, chosen per chunk by byte size |
| K4 | Edit execution: **game thread**, bounded by `MaxVoxelsPerOp`. A dedicated terrain thread only if §7.1 measurement demands it and E-5 supports it. Plugin thread-safety is undocumented; do not assume it |
| K5 | Durability: **journal record durable before inventory is credited.** Slower and correct. Revisit only with measurement |
| K6 | Client delivery: **generator + ops**; snapshots only on subscribe (already required by AGENTS §4) |
| K7 | Service shape: **`UWorldSubsystem`.** The service is authority, not a thing in the world |
| K8 | Module names: **`TerrainCore`, `TerrainBackendVPLegacy`** — no "Voxel" in game-owned names, per D-015 |
| K9 | Material config: **`SingleIndex`**, game-owned id↔index table in the adapter, 255 terrain materials max. Nothing visible changes while terrain is on the placeholder grid material; if it later affects how terrain looks, that becomes a GAME decision at that time |
| K10 | Field ownership: **adapter-held density, journal as the durable record.** Split ownership is reconsidered at T-108, when the C++ density field exists and the comparison is concrete |

`ARCHITECTURE.md` §3 now reads **Ruled (D-024)** for all ten, and §15 "Open forks" is
replaced with a line pointing at this ruling. §4.5 is reconciled with the K4 ruling;
DEF-4 remains open, because the ruling picks the thread and does not discharge the defect.

## D-025 — Engine version and agentic editor tooling (2026-09-06)

> **Numbering note.** An earlier CP-007 commit (`bf1b432`) used D-025 for a pending
> Implementer routing entry covering `ETerrainRole` and `DequantiseVoxel`. The Architect
> ruled both in the same session as **AR-2** and **AR-3** in `ARCHITECTURE.md`'s header
> ruling block — technical, so they carry no `DECISIONS.md` entry of their own (D-023).
> That entry has been **renumbered to D-026 and preserved here in full**, status
> SUPERSEDED: this log is append-only without exception and a used number is never reused,
> so that a later reader of `bf1b432` is not sent to a D-025 that says something else.
> D-025 is the ruling below. The original text is also in git history at `bf1b432`.

**Ruling.** Stay on UE 5.7 for T-112. Upgrade to UE 5.8 and adopt Epic's
first-party Unreal MCP plugin as **T-112.5**, scheduled between T-112 and T-113.

**Basis.**
- Unreal MCP (`ModelContextProtocol` + `AllToolsets`) shipped with **UE 5.8**. It does
  not exist in 5.7. This is an engine upgrade, not a plugin toggle.
- UE 5.8 is Epic's **last planned major UE5 release**. The upgrade is inevitable;
  only its date is a choice.
- **The expected blocker does not exist:** VoxelPluginFreeLegacy publishes prebuilt
  binaries for both 5.7 and 5.8. `Tools/Install-VoxelFreeLegacy.ps1` repoints to a
  different release asset.
- T-112 is the most engine-agnostic task on the roadmap (`TerrainCore` against
  Core/CoreUObject/Engine, headless, no plugin, no world), so deferring the upgrade
  past it costs ~nothing.
- T-113 is the first task that both *needs* MCP (Blueprint rewiring) and *must not*
  be written twice (`FVPLegacyBackend` binds to plugin headers). The adapter is
  authored once, against the plugin build we keep.

**Guard.** Unreal MCP is a **dev-time editor tool**. It is never referenced from
`VoxelWorld` or `TerrainCore`, and `IModelContextProtocolModule::StartServer()` is
never called from any game target. T-112.5 adds this to the AGENTS §9 drift guard.

**Watch item, not a ruling.** UE 5.8 ships **Mesh Terrain**, a mesh-based terrain
system supporting overlapping geometry. Unknown whether it is runtime-deformable or
replicable. Logged against R-008. It is a §10 conformance candidate or it is nothing;
no fork opens until there is evidence.

## D-026 — Two determinations made under R-011 (2026-09-06) — SUPERSEDED

**Status:** SUPERSEDED by `ARCHITECTURE.md` AR-2 and AR-3 (Architect ruling,
2026-09-06, technical per D-023).

**Original number:** raised as D-025 at commit `bf1b432` by the Implementer as a
pending routing entry, not a ruling. Renumbered to D-026 and preserved here so the
log stays append-only and no number is reused.

**Preserved because** this is the first recorded exercise of R-011 — an ambiguity
named to a specific line of `ARCHITECTURE.md` and answered inside one session rather
than returning the increment unstarted.

---

*Original entry, reproduced verbatim from `bf1b432:Docs/DECISIONS.md`:*

**Raised:** 2026-09-06 (CP-007) · **Class:** technical (per D-023) · **Raised by:** Implementer

**This entry is a record and a routing, not a ruling.** The Implementer has no architectural
authority (`AGENTS.md` §2). R-011 forbids returning an increment unstarted, so the two
questions below were decided the only way the increment could proceed, and are written down
here so the Architect can overrule either one cheaply — before either becomes permanent by
having been in the tree for a month.

### 1. `ETerrainRole` — the values are not in the spec

**Context:** `ARCHITECTURE.md` mentions `role` exactly once, at **line 325**, in the §4.3
`ITerrainBackend::Initialize` comment — `// seed, gen version, voxel size, bounds, density
field, role`. The enum's values are never enumerated anywhere in the document. This is the
named ambiguous line R-011 requires.

**Taken:** `enum class ETerrainRole : uint8 { Server, Client };` — the only pair the
document's own language supports (§4.3 *"Server: full result. Client: result ignored."*; §4.4
*"exists on both server and client; `HasAuthority` gates the authoritative half"*). No
dedicated/listen/standalone split, because the dedicated-server case is already carried by
`FTerrainStreamingInterest::bRender` (§4.2).

**Consequence if overruled:** cheap. Nothing in build step 1 depends on the value set —
`ETerrainRole` is declared and not yet consumed. It is first read by `FTerrainBackendInit` at
step 1's second half and by the adapter at step 2.

### 2. `DequantiseVoxel` returns the voxel centre, not its minimum corner

**Context:** the packet specifies `FVector DequantiseVoxel(const FIntVector&, const
FTransform&, float)` and requires `QuantiseEdit(DequantiseVoxel(Q)) == Q` for every `Q`.
`ARCHITECTURE.md` does not say which point in the voxel the function returns.

**Taken:** the centre. This is **forced, not chosen.** A transform is not exactly invertible
in floating point, so a minimum corner that lands one ULP below its own face floors to `Q−1`
and the required identity fails. The centre sits half a voxel from either face — orders of
magnitude more slack than the transform's error.

**Consequence if overruled:** the identity in §6.1 has to be given up, or the fixed §4.3
rounding rule has to change. Both are larger changes than this one.

**Neither determination touches the wire format, the 58 bytes, or the rounding rule.** Those
are fixed by §4.2 and §4.3 and were copied, not decided.

---

**Resolution.** Both questions were ruled identically by the Architect in the same
session, independently: `ETerrainRole` is `{ Server, Client }` (AR-2);
`DequantiseVoxel` returns the voxel centre (AR-3). Both live in `ARCHITECTURE.md`'s
header ruling block and carry no ruling of their own here, per D-023.

## D-027 — Earlier Astra onboarding and bounded T-112.2 delegation (2026-09-06) — ACCEPTED

**Recorded:** CP-008 · **Authority:** Director · **Amends:** D-018 handover timing only.

The Director assigned Astra as Implementer and scheduled **T-110 onboarding together
with T-112.2**, bringing it forward from the after-T-113 handover in D-018/STATE.
Onboarding completed against the existing T-112.1 code and installed UE 5.7 toolchain.

When the Implementer reported the packet's single `Sample(FIntVector)` interface
conflicting with `ARCHITECTURE.md` §4.6's `Density`/`Material`/`Version` declarations
(then lines 536–538), the Director replied: **"I authorize you to decide what you think
is best"**. This delegates resolution of the current increment's technical ambiguities;
it does not give the Implementer continuing architectural authority under AGENTS §2.

The resulting technical determinations were recorded in the new interface/backend
headers before implementation completed and are now reconciled in `ARCHITECTURE.md`'s
CP-008 header block and §4.6. They select the packet's single-Sample API and clarify
null-field residency, reference kernels/accounting, transfer and lifecycle behaviour.
No GAME pillar changes; no new module dependency; DEF-5 and DEF-6 remain open.

**Evidence:** T-112.2 build succeeded; four TerrainCore tests passed headless with
zero failures and process exit 0. The Director then requested this checkpoint.

## D-028 — Alternate Implementers with a shared handoff (2026-09-06) — ACCEPTED

**Recorded:** CP-009 · **Authority:** Director · **Supersedes:** D-018's fixed primary
Implementer assignment. D-027's earlier onboarding remains completed history.

The Director will alternate Claude and Codex to use available usage limits and time.
Claude is expected for T-112.3; the next worker is not guaranteed to be a particular
vendor or even a different agent. One active Implementer works in the shared workspace.

Every agent leaves brief summaries of choices, reasons, evidence and accomplishments
during meaningful work, then a formal handoff for whoever continues. `Docs/HANDOFF.md`
is the rolling artifact; `OPERATIONS.md` §5.1 is the single stepwise procedure, linked
from AGENTS. STATE remains checkpoint truth; approved architecture/rulings remain in
their existing files. Breadcrumbs are concise conclusions, not reasoning transcripts.

This is scheduling and continuity, not extra architectural authority: R2 approval,
R3 independent review, allowed-file boundaries and required checks still apply.
The Director explicitly authorised the README/workflow updates, automatic commit/push
and session wrap-up. No additional onboarding or review gate is introduced for R0/R1.

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

## D-029 — Bounded T-112.3 revision/service plan approval (2026-09-06) — ACCEPTED

**Recorded:** CP-010 · **Authority:** Director · **Class:** R2 · **Scope:** T-112.3 only.

The Director requested "Start 112.3", reviewed the concrete revision-index/service
plan and approved it with **"go"** before implementation. This approves a local API
within AR-4 and K7; it does not extend D-027's architectural delegation.

The index reads unseen chunks as zero, increments each distinct affected chunk
once per call and rejects the entire update if any revision would overflow.
The service privately owns the index, exposes read queries and gates its internal
metadata update on initialized lifetime/world, game-thread execution and server
authority. Initialization/teardown govern ownership. Exact APIs and limits are
reconciled in ARCHITECTURE's CP-010 block.

**Evidence:** UE 5.7 build succeeded; all five TerrainCore tests passed with zero
failures and exit 0. The revision test is worldless per §6.1; live client/server
authority and persistence are not established. No gameplay, dependency or format
change. DEF-7 and DEF-9 remain open. Director then requested checkpoint.

## D-030 — T-112.5 execution rulings and one Implementer determination (2026-09-06)

**Recorded:** CP-011 · **Authority:** Director (parts 1–2) · **Class:** R2 ·
**Scope:** T-112.5 only.

### 1. Director rulings

Asked to begin T-112.5, the Director ruled two bounded points before implementation:

- **UE 5.8 installs side-by-side, keeping UE 5.7.** 5.7.4 stays on disk as the rollback
  path rather than being uninstalled after the upgrade. Both engines are registered in
  `LauncherInstalled.dat`; the project's `EngineAssociation` is `5.8`.
- **T-112.5 splits in two, with a commit each.** **T-112.5a** = engine and plugin bump
  plus the CP-006 verification set re-run green. **T-112.5b** = Unreal MCP adoption.
  This buys a clean bisect if 5.8 ever proves to have broken something, at the cost of
  one extra commit — the Director accepted that trade explicitly.

D-025's premise was re-verified against the live source before any change and **holds**:
VoxelPluginFreeLegacy advertises 5.8 binaries, so the installer only repoints. The
plugin version moves 432 → 434, which the register now tracks under R-008.

### 2. Blocking prerequisite, recorded because it cost a session gap

UE 5.8 was **not installed** when T-112.5 was called. Only 5.7.4 existed. The engine
install is a human action in the Epic Games Launcher, so the increment stopped with
nothing implemented until the Director completed it (5.8.2). Worth recording once:
the next task with a launcher-level prerequisite should verify it at task selection,
not at task start.

### 3. An Implementer determination — a record and a routing, not a ruling

**This follows the D-026 precedent.** The Implementer has no architectural authority
(AGENTS §2), and R-011 forbids returning an increment unstarted, so the determination
below was made the only way the increment could proceed and is written down here so the
Architect can overrule it cheaply.

**Both `Target.cs` files move from `BuildSettingsVersion.V6` / `Unreal5_7` to
`BuildSettingsVersion.V7` / `EngineIncludeOrderVersion.Unreal5_8`.**

UE 5.8 refuses the old pinning outright — not a warning, a hard build failure:

> `VoxelWorldEditor modifies the values of properties: [ UnreachableCodeWarningLevel:`
> `Off != Error, ReturnTypeWarningLevel: Off != Error, DanglingWarningLevel: Off != Error ].`
> `This is not allowed, as VoxelWorldEditor has build products in common with UnrealEditor.`

Three options existed. `bOverrideBuildEnvironment = true` forces the mismatch through
and is a lie to the build system. `TargetBuildEnvironment.Unique` gives the target its
own environment but requires compiling the engine from source, which this project does
not do and should not start doing for a warning-level pin. Bumping to the 5.8 defaults
is the engine's own suggested fix and the only option that leaves the shared environment
honest, so that is what was done.

**What it changes:** V7 turns on `FPSemantics = Precise` for editor/program targets and
promotes return-type, dangling-reference and unreachable-code warnings to errors;
`Unreal5_8` adopts the new include order. **Nothing in `Source/**` needed changing** —
`TerrainCore` and `VoxelWorld` compiled unmodified under both, which is itself evidence
for the §4.1 claim that `TerrainCore` is engine-agnostic. The stricter warnings are
a net gain and are cheap to keep.

**Cheap to overrule** while it is one line in each of two files.

### 4. Guard strengthened, not merely restated

D-025 required T-112.5 to add Unreal MCP to the AGENTS §9 drift guard. It does, and it
says *why* rather than only *what*: `ModelContextProtocol.uplugin` declares **Runtime**
modules beside its Editor ones, so the plugin is **not inherently editor-only** and the
`"TargetAllowList": ["Editor"]` in `VoxelWorld.uproject` is the sole mechanism keeping
it out of a shipped game. Proven from the build receipts, not asserted. A future agent
tidying that allow-list away now has to read what it is for first.

**Evidence:** the full CP-006 verification set re-run green under 5.8.2, plus the
Director's by-hand dig; five TerrainCore tests green with MCP enabled, exit 0; a live
MCP `initialize` handshake returning HTTP 200. Detail is in `HANDOFF.md` and the two
commit messages. No gameplay, terrain architecture, dependency or save format changed.

---

## D-031 — T-113 execution authority, one determination, and a tooling finding (2026-09-07)

**Recorded:** CP-012 · **Authority:** Director (part 1) · **Class:** R2 within an approved
R3 architecture · **Scope:** T-113 / build step 2 only.

### 1. The authorisation

Asked to begin T-113, the Director said: *"I trust you on all accounts to execute anything
as needed for the implementation. Begin everything necessary."* That is in-session authority
of the same shape as D-027, and it is what the whole increment was executed under, including
its one architectural determination and the Blueprint asset edit.

**No numbered decision was changed.** T-113 is build step 2 of the architecture adopted at
D-017; §14 binds **no** defect to step 2 (DEF-10, the only one that was, is Resolved), so
the step was clear to start under §14's own rule.

### 2. AR-5 — an Implementer determination, routed for cheap overrule

**This follows the D-026 and D-030 precedent.** The Implementer has no architectural
authority (AGENTS §2), and R-011 forbids returning an increment unstarted, so the
determination below is written down where the Architect can overrule it cheaply.

**`FTerrainBackendInit` gains `UWorld* World` and `FTransform OriginTransform`.**

§4.3 lists `Initialize`'s inputs and stops at "role". That is complete for a backend that
owns only memory. `FVPLegacyBackend` has to find or spawn an `AVoxelWorld`, attach invoker
components to it and destroy them at teardown, and every one of those needs a `UWorld`. The
alternative was for the adapter to reach for `GWorld` and guess, which is worse in the
specific way that matters: it would be invisible.

`OriginTransform` is there for a different reason. §8.1 gives coordinate policy to the
**game** and §4.3 requires the server to quantise exactly once, so the service has to state
where the grid starts and the backend has to conform its actor to it. A backend that instead
read the origin off its own actor would move every existing edit by however far someone had
dragged that actor, and nothing would report it.

**Why this is cheap to accept:** both are engine types, not plugin types, so it widens what
the game tells a backend without widening what a backend may tell the game.
`FMemoryTerrainBackend` ignores both, still runs with no engine world, and
`Backend.Conformance` leaves both defaulted and still passes. Full reasoning is
ARCHITECTURE.md's CP-012 block, item 9.

### 3. What step 2 deliberately did not build

Recorded because the gap between "digging works through the service" and "terrain is
server-authoritative" is exactly the kind of thing that gets misremembered as done.

`FVPLegacyBackend` refuses Flatten, Smooth, Paint and box ops rather than approximating
them; `FTerrainEditResult::Removed` is empty because yield needs the K9 table and a separate
material read, both step 6; region transfer moves density only and is **not** the snapshot
format. **The production adapter therefore does not yet pass `Backend.Conformance`, which
§10 makes the operational meaning of "replaceable".** New risk **R-013** tracks that.
`RequestEdit` has no replication, journal, yield, reach or permission validation — all bound
to steps 3+ behind DEF-4, DEF-5 and DEF-7, all open.

### 4. A tooling finding that changes what the next agent should reach for

**Unreal MCP was not used for the Blueprint rewire, and could not be.** The server binds
loopback on demand and Auto Start Server is off — by D-025's own reasoning — so it was not
listening when the session started and its tools were unavailable for the whole session.

It turned out not to matter. **UE 5.8 exposes a full Blueprint graph API to plain Python**
(`unreal.BlueprintGraphEditor`, `unreal.BlueprintGraphPinLibrary`,
`unreal.BlueprintEditorLibrary`): enumerate nodes, delete them, create call-function nodes,
connect pins, set pin defaults, remove member variables, attach components through
`SubobjectDataSubsystem`, compile and save. That is AGENTS §11's **third** rung, and it beats
the fourth. The rewire is committed as `Tools/Editor/rewire_dig_through_service.py`,
idempotent and re-runnable.

**This does not overturn D-025.** Unreal MCP stays adopted, editor-only, and its §9 guard
stands untouched. It does mean an agent facing editor work should try scripted Python first
and start the MCP server only when Python cannot do the job.

### 5. Status

**Implementation complete and verified automatically; the Director's by-hand LMB/RMB check
is outstanding** and is the last item before step 2 is finished. Evidence is in `HANDOFF.md`
and the commit message for `0eabf48`.

## D-032 — CP-013 rulings: the generated world, AR-6, and three defects closed (2026-09-07)

**Recorded:** CP-013 · **Class:** technical (per **D-023**) · **Architect rulings, logged
not asked** · **Scope:** T-108 and T-114.

### 0. The instruction that shaped how this session was run

Asked to resume, the increment opened by handing the Director AR-5 and a DEF-4/5/7 proposal
to rule on. He refused it, sharply and correctly:

> *"Remember I instructed you to stop handing me proposals that i have no idea what they
> mean? Lets just continue working. And if you need me to decide something, it sure as hell
> better not be nuanced programming you already know I don't have a clue about."*

**This is already project law and the mistake was mine, not a new rule.** **D-023** splits
decisions into GAME (the Director rules) and TECHNICAL (the Architect rules and logs, the
Director is notified in one line and takes no action), and lists threading, data formats,
wire protocols and defect sequencing among the technical ones. D-023's own rationale is the
point: *"presenting the Director with technical options he cannot evaluate, and receiving
the recommended option back, is not direction. It is ceremony."*

**Every ruling below is therefore recorded, not requested.** A technical decision escalates
only if it (a) changes something a player would notice, (b) changes scope, or (c) costs
money. His subsequent instructions for both increments were *"Lets just continue working"*
and *"Ok proceed."*

**Consequence for AGENTS §3.** R3's "proposal file + independent review + Director ruling"
is amended in practice by D-023: for a *technical* R3, the ruling is the Architect's and the
independent review is the **cross-vendor** step (D-028), not a Director tutorial. Do not
open a proposal file at `Docs/proposals/` expecting the Director to adjudicate its contents.

### 1. T-108 was taken out of build order, and that was legal

§9 lists steps 0–8 but §14's rule is about **defects**, not sequence: *"a step may not start
while an unresolved defect or unruled fork is bound to it."* Step 8 was bound only to
**R-008 — a risk**, which is neither. Steps 3–7 were blocked by DEF-4, DEF-5 and DEF-7.
Step 8 was therefore the only remaining step that was both legal to start and able to move
the Phase 1 milestone, which BACKLOG states as *"one hill is trustworthy."* §9 now records
this explicitly so the next agent does not read the table as a queue.

### 2. The world's shape is game-owned C++, and it exists

`FTerrainWorldField` in `TerrainCore` implements `ITerrainDensityField`;
`UVPLegacyDensityGenerator` in the adapter forwards to it and decides nothing. This is §4.6
as written, built.

**What it closes.** Voxel Graphs are Pro-gated and fail silently (T-101A finding 2b, R-008),
so the T-101A hill was *sculpted by script into a running editor session* and did not
survive a map load (finding 2e, R-003). Every standalone process regenerated a flat plane —
which is why the Director had never seen a hill. The world is now a pure function of
position and seed and needs no save file. **This landed at step 8 rather than waiting for
persistence at step 4, and that ordering is deliberate: the world's SHAPE and the durability
of a player's EDITS are different questions, and only the second one needs a journal.**

`GeneratorVersion` **0 → 1**. Version 0 was the flat plane; §4.2/K3 put it in every snapshot
header so a chunk saved under one world is never silently reinterpreted by another.

**The strata colour palette in the adapter is COSMETIC and is not the K9 mapping.** It
exists so the bands are visible in the cliff face. Nothing reads a colour back, nothing
persists one, and no yield is computed from one. K9 and DEF-6 are untouched.

### 3. AR-6 — `ITerrainDensityField::SampleRange`

`Sample` alone is enough to FILL a chunk and not enough to SKIP one. A backend octree that
cannot ask *"is this whole region certainly solid, or certainly empty?"* must sample every
voxel of every region at every LOD, which across a 512 m world of 50 cm voxels is a
measurable cost rather than a theoretical one.

**The default implementation returns the full `[-1, 1]`**, which is always correct and
merely forfeits the skip — so no existing implementer breaks and no method becomes required.
That is what makes this cheap to overrule: deleting it costs performance and no correctness.

**AR-5 is confirmed as written at T-113.** It has now carried two increments, and T-108
depends on it: conforming the actor to the game's origin and voxel size is exactly what
makes plugin voxel coordinates identical to game voxel coordinates, which is why the
generator needs no coordinate conversion at all.

### 4. DEF-4 resolved — §4.5.1

Affinity and ownership table; five rules; a four-state shutdown machine with a fixed
eight-step teardown; cancellation.

The two rulings worth reading twice: **no lock of ours is ever held across a call into the
plugin** — the defect's "an external bounds lock may conflict with one the wrapper takes
internally" is answered by holding no lock at all, since one thread already establishes
mutual exclusion and a second mechanism could only deadlock against the plugin's own. And
**cancellation is a queue operation, not a plugin operation**: `ApplyOp` is synchronous on
the game thread and cannot be pre-empted by `EndPlay`, travel or PIE exit, so no operation is
ever partially applied at teardown. The pending queue is **discarded, not drained**.

**Not closed by this, and said so in the section:** the plugin's own internal thread safety
(E-2/E-5), collision readiness (DEF-8), durability ordering (DEF-1).

### 5. DEF-5 resolved — §4.10

- **The operation set is CLOSED at Remove, Add, Paint.** `Flatten` and `Smooth` are
  **removed from it** and permanently refused until a numbered decision specifies plane,
  strength, iteration and falloff. The defect's complaint was that they were named without
  semantics; the answer is to stop naming them. Their wire enumerators stay, because the
  58-byte encoding is permanent.
- **Canonical geometry**: write set `{v : |v-C|² <= r²}` in double with `<=` and **no
  epsilon**; read bounds `[C-floor(r), C+floor(r)+1)`; rounding unchanged from
  `TerrainQuantise.h`. **Monotonicity and idempotence are required properties** — the second
  is what makes DEF-3's duplicate JIP application survivable rather than corrupting.
- **Determinism split into three claims, and only two are made.** Cross-**backend** value
  identity is **explicitly out of scope**, because §8.1 gives "sphere/box edit kernels" to
  the plugin while giving "what Remove/Add/Paint mean" to the game — consistent only if the
  game specifies *properties* and the backend supplies *values*.

**This has a consequence and it is recorded rather than hidden: a backend swap is a resample
migration for every EDITED chunk, not a format-compatible reload.** FM-9 previously flagged
only differing voxel size or grid alignment. Pristine chunks regenerate and are unaffected.
It also fixes what `Backend.Conformance` means — the contract, never density equality
between two backends — which is a **weaker** claim than §10 could be read as making, and the
true one.

**New risk R-014** carries the residual named in §4.10.4(b): the kernel's own floating-point
arithmetic across builds and platforms.

### 6. DEF-7 resolved — §4.11

Trusted-input table; validation on the **quantised** footprint rather than the float request
(one voxel of disagreement between "permitted" and "changed" is a permission bypass at the
edge of every protected zone); `(SourceId, RequestId)` identity with a 64-entry
per-connection dedup ring; two-phase reserve-then-revalidate; bounded queue, round-robin
across sources, **no priority classes**.

Two changes to what the document previously allowed:

- **`bTruncated` is removed as a success signal.** A backend that would truncate must fail
  the whole op and mutate nothing. The field stays for struct stability and is false on
  every successful result.
- **`ApplyOp` returning false means nothing changed** — not "something may have changed".
  The adapter reaches that by pre-validating the entire footprint, which rests on an
  assumption about the plugin kernel; that assumption is **stated in §4.11.6 rather than
  buried**, and `Backend.Conformance` probes it.

**Only `Box` ops split.** An over-cap `Sphere` is rejected `TooLarge`, never split: a sphere
has no exact partition and the permanent 58-byte wire has nowhere to put a clip box, so an
approximate split would make the same request produce different terrain depending on whether
it crossed a cap. A transaction is **not atomic across sub-ops**, stated rather than assumed
because the alternative needs a durability protocol DEF-1 has not defined.

### 7. Status

**Build step 3 is unblocked** and is the next task. T-114 wrote the specification and its
headless evidence; it implemented none of step 3. Evidence for both increments is in
`HANDOFF.md` and in the commit messages for `db4cb72` and `c6d9ad6`.

---

## D-033 — CP-014 rulings: step 3 shipped, persistence adopted, schema 2 fixed (2026-09-20)

**Recorded:** CP-014 · **Class:** technical (per **D-023**) · **Architect rulings, logged
not asked** · **Scope:** T-115, T-116, T-117 · **Status:** ACCEPTED

Everything below is a technical ruling under D-023 — data formats, wire protocols, threading
and defect sequencing are explicitly on its technical side. The Director was notified, not
consulted, except for §5, which is his own instruction.

### 1. P-002 — box splitting is exact, and spheres do not split

**Context:** §7.1 caps an op at 65,536 written voxels, and §4.11.7 required over-cap requests
to split into sub-ops sharing a `TransactionId`. "Split" was never defined.

**Decision:** split along the **longest eligible axis** (X, then Y, then Z on ties) into the
**nearest balanced even widths** (lower coordinate takes the smaller part on ties). Every
child is reserved before any child is admitted. A cap below eight is impossible.

**An over-cap `Sphere` is rejected `TooLarge` and is never split.** A sphere has no exact
integer partition, and the permanent 58-byte wire has nowhere to put a clip box, so an
approximate split would make the same request produce different terrain depending on whether
it happened to cross a cap. Rejecting is the only answer that keeps §4.10's determinism
claims true.

**Consequences:** `Split.Equivalence` asserts that a box applied whole and applied as its
split produce identical region hashes, on and off chunk boundaries. A transaction is **not**
atomic across sub-ops; restart abandons uncommitted children and never replays the parent
intent to "finish" it.

### 2. AR-7 — shared immutable field lifetime

**Context:** the service owned the density field and destroyed it on shutdown, while the
plugin's asynchronous generator instances could still be holding it.

**Decision:** the field is shared and immutable, and **outlives the service** until the last
worker consumer releases it. `FTerrainBackendInit` carries an optional `DensityFieldOwner`
alongside the borrowed raw pointer; the raw field must match when both are supplied.

**Consequences:** no game-thread wait for meshing or collision, and no plugin type leaks
across the boundary. Teardown ordering is tested by `Service.Lifecycle`.

### 3. The density-kernel correction is compliance, not a fixture update

**Context:** a radius-four solid dig historically touched 895 samples. Under the adopted
§4.10 canonical write set it should touch far fewer.

**Decision:** the stock plugin writes beyond the canonical W (radius + 2), and clipping alone
still leaves wholly-contained cells partially filled. The adapter now clips W **and** enforces
full empty/solid for those cells, retaining the plugin's ramp on boundary cells only. The
same dig now touches **257** samples.

**Consequences:** this is intentional compliance with a decision already made, and it is
recorded here specifically so that nobody later reads the changed number as a golden fixture
that was quietly updated to make a test pass. The four pinned production hashes were
regenerated once, deliberately, for this reason and no other.

### 4. P-003 adopted, and P-004 fixes its bytes

**P-003 §§1–7 are adopted at the architectural level** after three revisions and three
independent cross-vendor reviews, the last of which found all six of its blocking items
closed. `ARCHITECTURE.md` §4.7 references it and the unimplemented schema-1 sketches and the
~122 bytes/edit estimate are **withdrawn**.

**P-004 is its exact-format packet**, and the determinations it makes are permanent format:

- **BLAKE3-256 for content digests; XXH3-64 for framing checksums.** Both are specified
  algorithms vendored in `Core`. **Not `FCrc::MemCrc32`** — its value is an Unreal
  implementation detail, and writing one into a file that must outlive an engine upgrade
  would make every saved world hostage to a header Epic is free to change.
- **No floating point is persisted anywhere in schema 2.** Voxel size and world origin are
  config floats and cross as exact **micrometres** (`int64`). A stored `float VoxelSizeCm`
  would make save compatibility depend on `50.0f` decoding identically on every toolchain,
  which is R-014's exact shape. This does not close R-014 — the kernel's own arithmetic is
  untouched — but it takes the save format out of its blast radius.
- **A 32-byte `GeneratorParamsDigest` is added to the base descriptor.** P-003 §6 requires
  binding "generator identity/version", and `GeneratorVersion` is a config integer a human
  remembers to bump. The digest means a changed `FTerrainWorldFieldParams` with a forgotten
  bump **fails the exact-base check** instead of silently reinterpreting every Empty chunk.
  This strengthens an adopted requirement; it does not alter one.
- **Empty has no payload object.** Its revision and last-change metadata live in the index
  leaf entry. Two on-disk spellings for one logical state would force every validator to rule
  on which wins when they disagree.
- **Sealing appends a record rather than rewriting a segment header.** Consequently no
  durable object in schema 2 is ever modified in place **except** the four fixed slots — and
  those are the only places where a torn write has a surviving redundant copy.
- **Journal records inherit identity from their segment header**, plus an 8-byte `WorldTag`
  per record as the splice check that inheritance would otherwise lose.
- **Mode A (named content-addressed objects) is the default storage mode**, and **no format
  field contains a path**, so P-003's preallocated-container fallback remains adoptable later
  without changing one stored byte. Whether Windows durably publishes a new directory entry
  across power loss is **not proved** — see **R-015**.
- **A torn tail is bounded by position, not by symptom.** A checksum failure counts as a tear
  only when its frame reaches exactly end of file; a damaged record in the middle of a
  segment fails closed. The looser rule would have silently discarded acknowledged history.

**Consequences:** two new permanent formats exist — schema-2 persistence objects and
schema-2 journal records — each governed by `AGENTS.md` §4. Sixteen pinned golden BLAKE3
vectors guard them. **Failing one of those vectors is a save-format change requiring a
numbered decision and a migration path, never a constant to update.**

### 5. The Director merged the writer and reviewer roles

**Context:** AGENTS §2 requires that the writer is not the reviewer for R3 work, and §3
requires proposal + independent review + ruling. The Director's instruction, verbatim:

> *"Read handoff.md, and move forward with game development picking up at whatever T-XXX is
> not completed. You will fluidly be an independent reviewer and a code writer. There are no
> longer any constraints to your job description, and i trust you to make all decisions.
> Begin at once."*

**Decision, his:** for T-117 the acting agent both wrote and reviewed the work, and ruled its
technical questions without asking. This is consistent with D-023, which already gives the
Architect technical rulings, and it extends that to the review step as well.

**Consequences, recorded honestly rather than celebrated.** The self-review was real and
found six issues, two of which mattered: the torn-tail rule in §4 above, and an object
encoder enforcing its cap with `checkf`, which compiles out of a shipping build — the same
class of defect the step-3 review had already caught once. But a review of one's own work is
weaker evidence than a cross-vendor review, and this is an R3 subsystem with a permanent
format in it. **R-016** carries that exposure, and `STATE.md`'s current task makes the owed
review the first thing the next agent does. This entry does not amend `AGENTS.md`; the
Director's instruction stands above it and can be withdrawn the same way it was given.

### 6. Status

**Build step 3 is complete** and its drift check is discharged against real clients. **Build
step 4 is specified and part-built**: architecture, byte format and codecs exist; the storage
owner, commit path, capture pump, settlement, recovery and retention do not. **DEF-1, DEF-2
and DEF-9 remain open** and none of P-003 §8's named evidence tests exists. Evidence for all
three increments is in `HANDOFF.md` and in the commit messages for `21e3a2c`, `b9104c0` and
`893a029`.

---

## D-034 — CP-015 rulings: durable-write protocol and a specification convention (2026-09-20)

**Recorded:** CP-015 · **Class:** technical (per **D-023**) · **Architect rulings, logged
not asked** · **Scope:** T-118, T-119 · **Status:** ACCEPTED

### 1. Every durable write ends with `Flush(true)`

**Context:** `IFileHandle::Flush(bool bFullFlush = false)` documents `false` as giving the
operating system "more leeway about when the data actually gets written to disk". Reading both
platform implementations shows they do not treat the parameter the same way:

| Platform | `Flush(false)` | `Flush(true)` |
|---|---|---|
| Windows (`WindowsPlatformFile.cpp:933`) | `FlushFileBuffers` — the parameter is **ignored** | `FlushFileBuffers` |
| Unix (`UnixPlatformFile.cpp:323`) | `fdatasync` | `fsync` |

`fdatasync` synchronises data and makes no promise about **metadata**. A file's length is
metadata, and **every object write and every journal append extends a file**.

**Decision:** every durable write in the terrain store ends with `Flush(true)`, and a failed
flush is an I/O error and never a success. `ITerrainStorageDevice` is the only place in the
project that calls it.

**Consequences:** the defaulted call would have been correct on the development machine and
silently wrong on the Linux dedicated server that D-002 makes the shipping target — and no
amount of testing on Windows would have found it, because on Windows the two calls are the
same instruction. This is recorded as a decision rather than a code comment because the
tempting simplification (`Flush()`, which reads fine and compiles fine) is wrong, and the next
person to touch this file deserves to find out why before they make it.

**It broadens R-007.** That risk was about whether a Linux server *builds*. It is also about
behaviour that is correct here and wrong there, and this is the first confirmed instance.

### 2. A slot is published by in-place overwrite that never truncates

**Context:** `OpenWrite(bAppend = false)` truncates the file it opens.

**Decision:** `OverwriteInPlace` opens in append mode and seeks to zero, and refuses a length
mismatch rather than truncating to fit.

**Consequences:** truncating would make a slot briefly zero-length, and the two-slot
publication protocol rests on a slot being either old or new and **never absent**. A
zero-length window converts a survivable torn write into a lost root.

### 3. A field rule a decoder enforces is written down where the field is defined

**Context:** T-118's independent reimplementation reproduced all sixteen golden vectors, which
proved the byte tables were right and said nothing about validity. Reading the decoders against
the document then showed they enforced a set of reference-validity rules P-004 never stated.

**Decision:** P-004 §1 gains rule 11 — a field rule a conforming decoder enforces is written
down in the section that defines the field, and anything not stated there is **not** a
requirement a decoder may invent. The missing rules are now stated in §§6.2, 7, 8, 9.1 and 9.5.

**Consequences:** a byte table alone is not a specification. Two implementations can agree on
every offset and still disagree about which files are valid, and **the more permissive one is
the one that accepts a corrupt world**. This is the concrete reason the format packet exists
at all, and it was found by the review rather than by a test, because no test of a single
implementation can find it.

### 4. Status

**Build step 4 remains incomplete.** The architecture, the byte format, the codecs and the
storage device exist; the journal writer, segment rotation, anchor and checkpoint publication,
world create/open, recovery and retention do not, and **nothing is written to disk**. DEF-1,
DEF-2 and DEF-9 remain open. Evidence for both increments is in `HANDOFF.md` and in the commit
messages for `af67b6f` and `30105b5`.

---

## D-035 — The checkpoint stall is index write amplification, not reading (2026-09-20)

**Recorded:** CP-016 · **Class:** technical (per **D-023**) · **Architect ruling, logged
not asked** · **Scope:** T-120 and the increment after it · **Status:** ACCEPTED

### 1. Context

P-003 §4 named the cause of the capture stall before anyone measured it: *"the current
32,768-per-voxel-call adapter path must gain a measured bulk-read implementation before
production integration."* That prediction was reasonable and it was wrong.

The bulk `ReadRegion` is now built. `FVPLegacyBackend::ReadRegion` takes one
`FVoxelReadScopeLock` and one `FVoxelConstDataAccelerator` per chunk instead of 32,768 lock
acquisitions each carrying a full octree traversal, and writes the payload in place instead of
65,536 `TArray::Add` calls. `Adapter.DensityContract` passes 20/20 with **unchanged fixture
hashes**, so the fast path reads exactly what the slow path read.

Capture time barely moved: ~42 ms/chunk before, ~25 ms/chunk after. So the phases were
instrumented, and the answer is not ambiguous. Warm solo capture, 8 chunks, 1,049,600 bytes:

| Phase | Time | Share |
|---|---|---|
| `ReadRegion` × 8 | 0.003 s | 1.5% |
| encode + BLAKE3 digest | 0.000 s | ~0% |
| store 8 payload objects (1 MB) | 0.015 s | 8% |
| **index path-copy, 49 pages** | **0.168 s** | **85%** |
| descriptor + root slot | 0.005 s | 2.5% |

Under three-client load the shape holds: 4 chunks cost 0.14–0.25 s, which is *the same
wall-clock as 8 chunks solo*. Doubling the chunk count did not change the time, because the
chunks were never the cost.

### 2. Ruling

**The incremental capture pump is not the next increment, and P-003 §4's description of it is
no longer sufficient.** The pump as specified spreads chunk payload work — copy-before-write
fences, dirty banks, background chunk reads. That is the 9.5% of capture that reading and
storing payloads account for. Building it as written would spread a tenth of the stall, leave
85% of it synchronous on the game thread, and let us report a fix that a player would still
feel.

The index write path is what has to change. Nine changed keys cost 49 durably written pages —
a 6:1 write amplification, and each page is an object write ending in `Flush(true)` per
**D-034**, which is where the time goes. The candidates are batching the pages of one capture
into a single durable write, deferring the fsync to one barrier before the descriptor (the
publication order in P-004 §12 already makes every page unreferenced garbage until the root
slot lands, so a crash mid-batch is already safe), and reducing the page count itself.

**`bCheckpointCapture` stays `false`.** The measured stall is smaller but the projection to the
256-chunk trigger is still multi-second, so P-003 §4's gate is still failed.

### 3. Consequences

The order of work changes: index write amplification precedes the pump, and the pump's scope
shrinks to whatever is still expensive once the fsync barrier is fixed — possibly to nothing,
which would be the better outcome.

The general point is worth keeping. **A prediction in an adopted proposal is not evidence.**
P-003 §4 named a real inefficiency, the fix was worth building on its own merits, and the
document's account of why capture was slow was still wrong by a factor of nine. The phase
breakdown is now permanent in `FTerrainCheckpointStats` and is logged on every capture, so the
next claim about where the time goes is checkable rather than inherited.

---

## D-036 — Objects are written in packs; the cost was the number of files (2026-09-20)

**Recorded:** CP-016 · **Class:** technical (per **D-023**) · **Architect ruling, logged
not asked** · **Scope:** T-121 · **Status:** ACCEPTED

### 1. The candidate I ruled for in D-035 was wrong, and measuring said so first

D-035 named three candidates for the index write path and put this one first: *"deferring the
fsync to one barrier before the descriptor."* Before building it, the three strategies were
measured over 49 small files on the development disk:

| Strategy | Time |
|---|---|
| write each file and flush it (what existed) | 151.6 ms |
| **write all files, hold the handles, flush at the end** | **155.8 ms** |
| write and close each, then reopen each and flush | 55.6 ms |
| **all of it in one file, one flush** | **2.3 ms** |

The deferred barrier is *no cheaper*, because `FlushFileBuffers` costs the same whenever it is
called — it is per file, and there is no cheap cross-file barrier to defer to. One `fsync` costs
~3 ms **regardless of size**: a 160-byte index page costs what a megabyte costs.

So the cost was never *when* the store syncs. It is **how many files it syncs**, and a
checkpoint synced 59.

### 2. Ruling

**Objects are written in packs — P-004 §13.** A capture buffers every object it writes (chunk
payloads, index pages, the checkpoint descriptor) and the pack is written and flushed **once**,
immediately before the root slot. Two `fsync`s per capture instead of 59.

Content addressing does not change, which is what makes this a storage-layer change rather than
a format rewrite. Objects are still named by, and verified against, their BLAKE3 digest on the
way in and on the way out; the index still path-copies; no other section's byte tables move.
Only where the bytes live changes, and a reference does not record whether its target is loose
or packed.

The crash argument is the one §12 already made. A pack is flushed strictly before the root slot
that names anything inside it, so a pack failing its trailer checks belongs to a capture that
never published — **ignored in full, and explicitly not an error**, because refusing to open the
world over it would turn collectable garbage into a dead world. An abandoned capture now writes
nothing at all, which is better than the loose-object path, where partial work survived.

### 3. Measured result

| | before | after |
|---|---|---|
| warm solo capture, 8 chunks | 0.197 s | **0.010 s** |
| under three-client load, 4 chunks | 0.14–0.25 s | **0.026–0.035 s** |
| stall warnings in a 30 s MP round | 15, ~0.34 s each | **0** |

Restore still works across process restarts: the four-launch harness restores 8 chunks from a
pack written by a previous process, replays zero edits, and all eight chunk hashes are
identical. 34/34 automation tests pass, including a new `Persistence.Storage.Pack` covering
torn packs, corrupt bodies, id reuse, abandoned batches and loose/packed coexistence.

### 4. `bCheckpointCapture` stays false, for a better reason

It stays off, but the reason has changed from *"the measurement failed the gate"* to **"the
measurement has not been taken."** Reading is now dominant again — about three quarters of a
capture under load — and extrapolating 4 chunks to 256 gives roughly 1.5 s, which is not
multi-second but is not evidence.

**Flipping the default requires measuring a capture at its real trigger.** This checkpoint has
now twice acted on a plausible projection and been wrong: P-003 §4 predicted the read was the
cost, and D-035 predicted the fsync barrier was the fix. The harnesses dirty 4 and 8 chunks; the
trigger is 256. Building that measurement is the gate, and it comes before the pump.

### 5. Consequences

Retention (DEF-9) inherits a new problem: reclamation can delete a loose object individually but
**cannot delete one object out of a pack**. A pack is reclaimable only when nothing live refers
to anything in it; reclaiming partially dead packs needs a compaction pass that does not exist.
Recorded in P-004 §13.6.

The general lesson is the same one as D-035, and it is now cheap to restate because it has
happened twice in one checkpoint: **the fix that looks obvious deserves a measurement before it
is built, not after.** Holding the handles open and flushing at the end is an entirely
reasonable idea that would have cost a day and bought 4 ms in the wrong direction. It took three
minutes to price it.

---

## D-037 — Checkpoint capture is on by default, measured at its real trigger (2026-09-20)

**Recorded:** CP-016 · **Class:** technical, but with a player-visible consequence · **Architect
ruling, logged not asked** · **Scope:** T-122 · **Status:** ACCEPTED

### 1. The measurement that had not been taken

D-036 left `bCheckpointCapture` false for a stated reason: *"the measurement has not been
taken."* The trigger is 256 dirty chunks and every harness dirtied four or eight. So the
harness was built.

`Terrain.StressCapture` dirties exactly `CheckpointDirtyChunkTrigger` chunks through the real
`RequestEdit` path and lets the ordinary pump fire the capture. Two things about it are worth
recording, because both were discovered by it failing first:

- **It runs over ~80 seconds, not one frame.** The queue rate limits each source to three
  intents a second, and a first attempt to issue 256 edits in one frame was refused 253 times
  with `RateLimited`. That limit is anti-griefing and working correctly. Spreading the edits is
  also the truthful shape: on a real server 256 dirty chunks accumulate from many players over
  minutes, never from one source in a frame, and capture cost depends on how many chunks are
  dirty rather than on how they got that way.
- **It adds rather than removes.** The first working run dirtied nothing, because a `Remove` in
  empty air modifies no voxels, so the backend reports no affected chunks and nothing is marked
  dirty. The capture never fired and the run looked like a silent failure.

### 2. The result, and how wrong the projection was

**256 chunks, 33,587,200 bytes, 1,155 index pages, in 0.162 s** — 0.6 ms per chunk.

| Phase | Time |
|---|---|
| read 256 chunks | 0.089 s (55%) |
| encode + BLAKE3 | 0.005 s |
| store payloads | 0.026 s |
| **1,155 index pages** | **0.016 s** |
| pack write + root slot | 0.025 s |

Reproduced at 0.165 s on a second run with no settings overrides at all, which also confirms
the new default takes effect.

D-036 extrapolated this at *"roughly 1.5 s"*. The real number is **nine times better**. That is
the third projection in this checkpoint to be wrong — P-003 §4 said reading was the cost (it was
1.5%), D-035 said a deferred fsync barrier was the fix (it was 4 ms slower), and D-036 said the
trigger would cost 1.5 s. **Each was reasonable, and each was wrong, and each took far less time
to measure than to argue about.**

Note the index line: 1,155 durably written pages in 0.016 s, because they share one pack. Before
D-036 that alone would have been about 3.5 seconds of `fsync`.

### 3. Ruling

**`bCheckpointCapture` defaults to true.**

0.162 s is an order of magnitude inside P-003 §4's multi-second gate, and the trade it was
guarding has now inverted. Leaving capture off buys a world whose startup replay grows without
bound and therefore slows down forever; turning it on costs an occasional stall of about a
sixth of a second. The unbounded cost is worse than the bounded one, and the bounded one is now
small.

The stall warning's threshold moves from 0.1 s to 0.5 s. At 0.1 s it fired on every healthy
capture at the trigger while announcing a gate failure that had not happened, and a warning that
cries wolf on the normal case teaches people to ignore it.

`RejectionName` also gained the six enum values it was missing — `OutOfReach`,
`ToolUnavailable`, `PermissionDenied`, `NotResident`, `RateLimited`, `UnsafePlacement` — all of
which previously printed as `Unknown`. That gap is what initially hid the rate limiter.

### 4. The tail this does not fix, stated plainly

Capture runs only when the queue is empty, and admission closes at
`TerrainCheckpointDirtyHardBound` (4,096) dirty chunks. A server busy enough that the queue
never drains would accumulate toward that bound and then take a capture roughly sixteen times
this one — about 2.6 s, back inside the territory P-003 §4 fails — while refusing edits until it
drained.

Nothing observed so far goes near it: a 30-second three-client round reaches 243 ops and 4 dirty
chunks, and takes no checkpoint at all. But it is a real shape and it is exactly what the
incremental copy-before-write pump exists to prevent (DEF-2). **The pump is now the next
increment**, and for the first time it is aimed at the phase that actually dominates: reading,
at 55% of capture.

---

## D-038 — The incremental capture pump, and copy-before-write without the copy (2026-09-20)

**Recorded:** CP-016 · **Class:** technical (per **D-023**) · **Architect ruling, logged
not asked** · **Scope:** T-123, DEF-2 · **Status:** ACCEPTED

### 1. What changed

Capture was one synchronous call: every dirty chunk read, encoded and buffered while the game
thread waited. Measured at the real trigger that is 0.162 s (**D-037**), and it scales with the
dirty set — admission closes at 4,096 chunks, so the worst case a server can reach is sixteen
times that, back inside the multi-second stall P-003 §4 fails.

The pump takes the cut once, at G, then reads and encodes a few chunks per frame under a time
budget (`CheckpointPumpMillisPerFrame`, default 2 ms), and publishes when the last one is done.
Edits keep being admitted, committed and broadcast throughout.

### 2. Copy-before-write, without the copy

P-003 §4 specifies stashing a copy of a chunk before an edit touches it, so the capture can
encode the copy later — dirty banks, a fence, and a reconciliation step.

**None of that is needed.** When an edit is about to modify a chunk the capture still owes, the
pump captures *that chunk immediately, out of order*, and drops it from the pending set. The
pre-edit state is encoded before the backend is allowed to change it, which is the property the
banks existed to provide, with no second copy of a 131 KB payload and nothing to reconcile. The
cost is one chunk read inside that edit, bounded by the edit's own validated footprint.

The hook is `UTerrainService::Cb.Apply`, which already computed the footprint and pinned it; the
pump is told immediately before `Backend->ApplyOp`.

### 3. Why the cut is still consistent

Every chunk in the checkpoint is its state at G, and nothing else can be:

- a chunk the pump reaches on its own has not been edited since G, because any edit would have
  gone through `NoticeWrite` first and taken it;
- a chunk an edit touches is encoded by `NoticeWrite` before `ApplyOp`, so what is encoded is
  its pre-edit state;
- revisions agree, because `NoticeWrite` runs before the revision index advances, so the
  revision encoded is the one the chunk had at G.

Edits after G accumulate into a **fresh** dirty set. A chunk edited during a capture is in both:
this checkpoint records its state at G, the next records what the edit made of it.

### 4. Two rulings inside the implementation

**The synchronous entry point is now the pump run with an unlimited budget.** One code path, so
the two cannot drift; everything that tested synchronous capture now tests the pump. That
refactor landed green at 34/34 before any new behaviour was added, which is what made it safe.

**`Advance` always captures at least one chunk, whatever the budget.** A pump that can make zero
progress is a pump that can never finish: a budget smaller than one chunk's cost would starve
the capture forever. Forward progress is not negotiable; the budget only decides how much *more*
than one chunk a frame does. This was found by the test asserting that a zero budget still
progresses — the first implementation checked the deadline before doing any work.

### 5. Measured

| | before the pump | with the pump |
|---|---|---|
| 256-chunk capture, game thread | **0.162 s in one frame** | 0.135 s spread over 0.444 s wall |
| longest unbroken step | 0.162 s | **0.035 s** (index 0.014 + publish 0.021) |
| stall warnings | fired | none |

The visible hitch at the trigger therefore drops from **0.162 s to 0.035 s**, and the part that
scales with the dirty set — reading, encoding, buffering — no longer lands in one frame at all.

Copy-before-write is exercised in production, not only in tests: a 30-second three-client round
with captures every 16 ops took 15 checkpoints and **2 chunks by copy-before-write**, with no
stalls. The unit test `Persistence.Capture.Pump` drives it deliberately, interleaves edits with
a part-finished capture, and asserts that the restored checkpoint matches the world **at G**
rather than as it is now — plus the converse, that the live world really has moved on, so the
match cannot pass vacuously.

### 6. What is still synchronous, and the tail

Publication — the index path-copy, the descriptor, the single pack write and the root slot — is
one step at the end and is not spread. At the trigger that is 0.035 s. Spreading it would mean
interleaving a path-copy with edits changing the very set being indexed, which is a much harder
problem than the one this solves, and it is not worth it until the number says otherwise. The
stall warning now measures exactly that unspread part, against a 0.5 s threshold.

The 4,096-chunk tail is **reduced but not gone**: chunk work is spread, so what remains is
publication at that size, which is roughly sixteen times 0.035 s. That is still worth watching
and is now the only part that scales badly. Admission also now counts the capture's outstanding
set toward the hard bound — previously only today's dirty set was counted, which would have let
a world hold up to twice the bound it advertises.

---

## D-039 — Object retention exists, is gated off, and DEF-9 stays open (2026-09-21)

**Recorded:** CP-016 · **Class:** technical (per **D-023**) · **Architect ruling, logged
not asked** · **Scope:** T-124 · **Status:** ACCEPTED
**Implementation:** Claude (first pass) and Codex (review and repair), per **D-028**.

### 1. What was built

A mark-and-sweep over the object store, reachable through `Terrain.Reclaim`. It marks from both
root slots, deletes unreachable loose objects, and **compacts partly dead packs** by rewriting
them without their garbage.

Compaction is not optional, and the number says why. A pack is removable only when nothing in it
is live, but index path-copying **shares** pages between generations by design, so an old pack
keeps at least one live page essentially forever. Measured on a three-generation world: **0 of 3
packs removable, 176 bytes reclaimed, 757 KB stranded** out of 2.68 MB. With compaction the same
world reclaimed **666 KB — 25% of the store — and stranded nothing.**

### 2. The review found six defects in the first pass, and they were real

The first implementation was mine; the review was Codex's, which is what **R-016** asks for and
what the author of a piece of work cannot do for it. Recorded in
`Docs/reviews/P-004-review-codex-retention.md`. The findings that mattered:

1. **The mark named payloads without reading them.** A hand-rolled page walker added each leaf's
   payload digest to the live set without loading it, so a checkpoint with a *missing* payload
   marked clean and reclamation proceeded. Marking now uses `TerrainIndexEnumerate` — the same
   traversal restore uses — through a tracking store that records what it actually loaded, so
   the live set is by construction **what a restore would need**. This is the better design and
   it is not a detail: a mark that can disagree with restore is a mark that can delete a world.
2. **Root validity was cached.** I refreshed the slot pair after publication, which does not
   cover damage or failed publication afterwards. Every pass now re-reads both on-disk slots.
3. **Compaction trusted the bytes it copied.** Survivors are now BLAKE3-verified before writing
   and read back through the ordinary content-addressed path before the original is removed.
4. **A torn replacement poisoned the next pack id until restart.** A failed write that still left
   a file made every later capture retry the occupied name. A failed write that leaves a file now
   consumes its id.
5. **The fallback assertion was theatre.** My test computed `HashesAtG8` and never compared it.
   The replacement damages the newer slot on a device copy, restores through the ordinary path,
   and compares the older generation's terrain hashes.
6. **Failure reporting was false after partial work.** The console said "Nothing was deleted" for
   every failure, including I/O failures that stop mid-sweep. It now reports what completed.

A seventh finding was about my measurement rather than my code, and it invalidated a result I had
already reported: **repeating `Add` on solid terrain is idempotent.** My two production runs
changed `voxels=0` and therefore never created the extra generations the measurement needed. The
harness now alternates Add/Remove/Add and refuses to measure unless voxels actually changed.

### 3. Ruling: the pass is gated off pending R-015

`Terrain.Reclaim` does nothing unless the process carries `-TerrainRetentionExperiment`.

I had written that "crash safety comes free from content addressing". That is true for a process
crash and false for power loss, and the difference is the whole ruling. Compaction removes the
original pack **after** writing a replacement, and a replacement can hold objects shared by
*both* roots. If the replacement's directory entry is not durable — which **P-004 §12 leaves
unproven on Windows (R-015)** — that one loss defeats both retained generations simultaneously.

Every other failure path in this system preserves a fallback: a failed capture leaves the
previous root, a torn pack is unreferenced garbage, a failed publication leaves the other slot.
This is the only operation that can take both. It does not get to run on a player's world on the
strength of a readback in the same process.

### 4. DEF-9 is **not** closed

What exists is a synchronous diagnostic on the game thread, scheduled by hand. P-003 §5 specifies
an incremental, off-thread, bounded-buffer collector with retention pins and an epoch protocol,
and none of that is built. Journal trimming is untouched — worth about 49 KB per checkpoint
interval against 33 MB of payloads, so it was correctly deprioritised, not done. Generator
migration and the broader crash matrix remain separate work.

Backup, migration and sync consumers must not be enabled alongside this pass until the pin/epoch
and exclusive-writer protocols exist.

### 5. Measured on a real world

`Tools/Test-TerrainRetention.py` builds three genuinely distinct generations (Add / Remove /
Add; 539,904 / 538,368 / 538,368 voxels changed; cuts at G=256, G=512, G=768) and verifies 256
chunk hashes across a restart after each.

| | |
|---|---|
| store before / after | **101,658,408 → 67,823,838 bytes** |
| reclaimed | **33,834,570 bytes** — one full dead generation |
| terrain | **all 256 hashes survive reclamation and restart** |
| second sweep | 0 bytes — correctly idempotent |

Two costs fall out of this that were not previously known, and both argue the same way:

- **The pass costs ~16.8 s on the game thread whether or not it reclaims anything** — the
  idempotent second sweep cost as much as the first. The expense is the **mark**, not the
  sweep, so production retention means making the *walk* incremental, not just the deletion.
- **Restore is 13.1 s for 256 chunks.** Checkpointing drove replay to zero; the restore that
  replaced it is not free and has never been looked at.

Note also that the production run deleted a whole pack and compacted none, while the unit test
compacts and deletes none. Both are correct and both are needed: the harness re-edits the *same*
chunks each generation so the oldest pack dies entirely, while the unit test touches *different*
chunks so path-copying shares subtrees and nothing ever dies whole.

### 6. Consequence worth keeping

Two of the six findings were places where my code *looked* like it checked something and did not
— a payload digest added to a set instead of loaded, a hash computed instead of compared. Both
would have passed review by their author, because their author already believed the thing they
were supposed to prove. That is precisely the shape R-016 names, and it is the argument for
alternating implementation between agents (**D-028**) rather than treating it as scheduling.

---

## D-040 — The crash matrix, taken literally (2026-09-21)

**Recorded:** CP-016 · **Class:** technical (per **D-023**) · **Architect ruling, logged
not asked** · **Scope:** T-125 · **Status:** ACCEPTED

### 1. What "crash matrix" had to mean

P-003 §8 asks for injection *"before/after apply, append/flush, every payload/page/descriptor
write/flush, root overwrite/flush, segment discovery/rotation and every deletion"*, plus repeat
recovery. The tests that existed each broke **one case somebody had thought of**:
`Storage.SlotPair` tears a root slot, `Storage.Pack` tears a pack, `Persistence.Retention`
interrupts a compaction. A handful of chosen cases cannot be that list, because the write nobody
thought to break is the one that breaks.

So the matrix runs one scripted session — nine edits, two checkpoints — and then runs it again
**once per mutating write**, failing that write and only that write, walking the sequence index
by index, in two modes: a hard refusal, and a torn write that lands half on disk and then
reports failure. The fault device gained `FailAtMutation`, which counts `WriteNew`,
`OverwriteInPlace`, `Append` and `Delete` as one ordered sequence, because a crash is a *moment*
and the per-operation-type faults could only express a *kind*.

### 2. The assertion is the point, not the coverage

"Recovery works" is not worth proving. A world that comes back holding a state it was never in
is **worse** than one that refuses to come back, because nobody finds out.

So the reference run records the exact terrain after every committed operation, and each
recovered world must equal the reference **at its own OpSeq** — an exact chunk-hash comparison.
Recovery may lose the tail (a crash before a record was durable means the edit did not happen,
which is exactly what the commit ordering promises). It may lose nothing. It may refuse. It may
**not** land between two operations, ahead of the journal, or on terrain that never existed.

### 3. Result

**40 injections over 20 mutating writes, in both modes.**

| Outcome | Count |
|---|---|
| recovered to an exact point in real history | **28** (20 of them losing the tail) |
| refused to open | 12 — **all of them crashes during world creation** |
| landed on a state that never existed | **0** |

The refusal count is bounded rather than merely reported, and that is the stronger claim:
**once a world has been created, no single crash made it unopenable.** A crash partway through
creation leaves nothing to open, which costs nothing because nothing was there. Every crash
after that point was survived by the two-slot root, the torn-tail rule, or
unreferenced-garbage containment. Recovering twice reaches the same head and the same terrain,
which is P-003 §8's repeat-recovery requirement.

### 4. An observation worth recording about packs

The session has only **20** mutating writes, and that is because of **D-036**: a capture writes
one pack instead of one file per payload, page and descriptor. Before packs this same session
would have had well over a hundred crash points.

That cuts both ways and both directions are good. The matrix is far cheaper to walk exhaustively
— which is why walking it exhaustively was affordable at all. And each surviving write carries
much more, so the crash-safety argument now rests on fewer, larger, better-understood steps: a
pack is durable before the root slot that names it, or it is unreferenced garbage. Fewer places
to be wrong is the same property that made §11's six-operation device surface worth having.

### 5. What this does and does not close

`Restart.CrashMatrix`'s **terrain half is satisfied**. Its other half — *"no duplicated or
missing payout, no durable ore without durable removal"* — needs settlement and SQLite, which
are build step 6 and do not exist, so **DEF-1 stays open** and the criterion is not met.

The matrix runs against `FTerrainMemoryStorageDevice` behind the fault decorator. That is an
honest model of *lost writes*, and it is **not** power loss: whether a file's name survives a
power cut is **R-015**, unproven, and no amount of in-process injection can speak to it. The
matrix proves the protocol handles the failures it is given; R-015 is about whether the platform
gives it the failures we assume.

## D-041 — Namespace durability: container mode, not a power-loss rig (2026-09-21)

**Recorded:** CP-017 · **Class:** technical (per **D-023**) · **Architect ruling, logged
not asked** · **Scope:** T-126, R-015 · **Status:** ACCEPTED · **Spec:** P-005

### 1. Ruling

R-015 is settled by **removing the dependency**, not by measuring it. The object store adopts
P-003 §4's container mode: four pre-created files `containers/c.0`–`c.3`; objects are written
as frames (a 40-byte header naming its own offset, then an unchanged P-004 §13.3 pack image);
an append cuts a torn tail before writing; retention copies live objects into the active
container, flushes, verifies, then truncates. After bootstrap, an open world creates and removes
no names.

### 2. Why not the alternatives

A power-loss rig (no Hyper-V on Windows 11 Home) could only ever show a failure on one disk,
never prove its absence. Relying on NTFS log ordering is common practice and undocumented, and
says nothing about Linux, where `fsync` on a file explicitly does not cover its name. Container
mode needs only the per-file flush contract every acknowledged edit already relies on. It removes
a risk rather than accepting one, so under D-023 it is logged, not escalated.

### 3. Deviations from P-003's wording, stated

The container **count** is fixed; each grows by append rather than being zero-preallocated —
append is the journal's primitive, not an unproven one. A frame whose header validates but whose
body fails is skipped rather than ending the scan, so bit rot cannot hide later frames.

### 4. Consequences

- R-015 closed by construction. Residuals: the per-file flush contract; on Windows, the bootstrap
  window after world creation (directory sync is best effort). Unix directory sync is `fsync`,
  not yet compiled on Linux.
- `ITerrainStorageDevice` gains `ReadRange`, `Truncate` (shrink-only, mutating, part of the crash
  matrix's sequence) and `SyncDirectory` (bootstrap only).
- Retention's gate changes reason: DEF-9, not R-015. It stays behind `-TerrainRetentionExperiment`.
- Journal `Rotate` must not be used from production as written; trimming must rotate within a
  pre-created ring.
- No schema-2 byte table, golden vector or record changed.

## D-042 — Background retention: the epoch rule, abandon rather than reconcile (2026-09-21)

**Recorded:** CP-018 · **Class:** technical (per **D-023**) · **Architect ruling, logged
not asked** · **Scope:** T-127, DEF-9 · **Status:** ACCEPTED · **Spec:** P-006

### 1. Ruling

Retention runs in the background by default (`bBackgroundRetention = true`), after every
successful checkpoint.
- **Worker:** marks, builds frames of at most 4 MB, and verifies, reading only a snapshot of the
  location map.
- **Game thread:** does every mutation, one bounded step at a time.
- **The experiment flag is removed.** `Terrain.Reclaim` remains, as an inline diagnostic.

### 2. The epoch rule

`FTerrainFileObjectStore::ReferenceEpoch` moves on every `StoreObject` — including a dedup hit,
which is P-003 §5's "resurrection by content hash" — and on every root publication. Planning,
every append and every cut or delete require an unchanged epoch; a cut or delete also requires
no open capture batch. A moved epoch **abandons** the cycle rather than merging new references
into the mark: captures are minutes apart and a cycle takes about 0.55 s, so abandonment is rare
(0 of 30 cycles in the multiplayer run), and a rule that only ever stops is easier to prove than
one that reconciles.

### 3. Policy

A container is compacted only when at least 25% of its object bytes are dead. Pre-P-005 files
are always migrated.

### 4. Deferred

- **Explicit retention pins:** no consumer exists yet. When one does, its pin must move the
  epoch and join the mark.
- **Journal trimming.**
- **The exclusive-writer lease** (T-128).


## D-043 — The exclusive-writer lease: an OS lock, taken before existence is checked (2026-09-21)

**Recorded:** CP-019 · **Class:** technical (per **D-023**) · **Architect ruling, logged
not asked** · **Scope:** T-128, P-003 §5 · **Status:** ACCEPTED · **Spec:** P-007

- The lease is an OS lock on `writer.lock` at the world root: `LockFileEx` on Windows (a handle
  that shares read and write but not delete), and on Unix `flock` plus an inode check. Its
  contents are never used. A crashed holder never leaves a stale lease.
- It lives in the device seam (`ITerrainStorageDevice::AcquireExclusiveLease`). The world store
  holds it for its whole life and releases it last. The service takes it **before** checking
  whether the world exists, so two servers cannot both create the world.
- It never blocks: a held lease gives `StoreBusy` at once. The second server closes terrain
  access and changes no byte.
- `Open`/`Create` do not take the lease themselves: headless tests model restarts with two store
  objects. Taking the lease is not a mutation, so crash-matrix indices are unchanged.

## D-044 — Join-in-progress by ordered snapshots; DEF-3 resolved (2026-09-21)

**Recorded:** CP-019 · **Class:** technical (per **D-023**) · **Architect ruling, logged
not asked** · **Scope:** T-129, build step 5, DEF-3 · **Status:** ACCEPTED · **Spec:** P-008

- **DEF-3 is resolved without a buffering protocol.** Commits are serialized on the server, and
  each client's terrain stream is one reliable, ordered channel, so a snapshot read between two
  commits arrives before every later op.
- `FTerrainReplica`: revision checks and bumps apply **only to chunks the replica holds in sync**.
  An op is still written over its whole footprint, which is safe because the kernel is
  pointwise. A gap demotes the one chunk it affects, not the whole op.
- Snapshots use the Dense layout, Zlib-compressed, in fragments of at most 16 KiB, sent nearest
  first. Each connection may have at most 64 KiB unacknowledged, and the server sends at most 2
  per tick; acknowledgements are generation-checked. Resync now repairs chunks.
- `DeliveredRevisions` records only chunks the client holds in sync.
- **Adapter determination:** `FVPLegacyBackend::WriteRegion` is a bulk write (one lock, one
  accelerator, one remesh). Snapshot install went from 65–165 ms to 16–25 ms per chunk, and boot
  restore of 256 chunks from 4.4 s to 0.8–1.0 s, with every restart hash unchanged.

## D-045 — Materials and physical yield; DEF-6 measurement half; K9's config switch deferred (2026-09-21)

**Recorded:** CP-019 · **Class:** technical (per **D-023**) · **Architect ruling, logged
not asked** · **Scope:** T-130, gate 1C (physical half) · **Status:** ACCEPTED · **Spec:** P-009

- **Measurement:** signed microlitres per game material. Removal is attributed to the pre-edit
  material, placement to `Op.MaterialId`. There is one shared `TerrainOccupancy`.
  `ITerrainBackend::MeasuresPhysicalYield()` drives the journal's `Measured` flag.
- **E-1 is answered** against the rendered hole: +2.4% at the 2 m player dig, and within 0.5% from
  radius 6 up.
- **Material ids without a visible change:** the generator colours each voxel by game id, one
  distinct colour per catalog entry, and the adapter inverts that table exactly. If two colours
  ever collide, yield is reported as unavailable.
- **This amends the timing of K9 (D-024):** the SingleIndex config switch changes how terrain
  looks, so it waits for the Director. Saves and the wire carry ids only, so the switch is
  adapter-internal whenever it happens.
- Old saves (material 0) keep their generated materials. `HashRegion` on the plugin stays
  density-only on purpose; materials are verified directly.

## D-046 — The settlement ledger in SQLite; DEF-1 resolved for terrain settlement; economy policy v1 (2026-09-21)

**Recorded:** CP-019 · **Class:** technical (per **D-023**), **includes a dependency entry**
(AGENTS §4: no new architectural dependency without a decision entry) · **Scope:** T-131,
gate 1C, DEF-1, DEF-6 · **Status:** ACCEPTED · **Spec:** P-010

### 1. Dependency

- UE's **`SQLiteCore`** engine plugin (SQLite 3.47.1) is enabled in `VoxelWorld.uproject` for all
  targets. This implements D-012 ("SQLite holds entities"); it is not a new vendor.
- New module **`EntityStore`** is the only module that links it. It registers its ledger with
  `FTerrainSettlementRegistry`, and the service loads it by name
  (`UTerrainSettings::SettlementModule`).
- TerrainCore's `Build.cs` is unchanged. No MCP plugin is involved (D-025 untouched).

### 2. Protocol

This is P-003 §2 and §3 as written:
- The intent is decided before the journal write and recorded in the record.
- A serialized worker settles `(OpSeq, digest, deltas, W)` in all-or-nothing transactions of up
  to 16 records. Settlement never gates terrain.
- 32 unsettled records pause execution, and checkpoint cuts wait for W = H.
- Boot settles (W, H] from the journal and never recomputes. It refuses a missing ledger once
  the journal has ever paid, and refuses W < G or W > H. Worlds from before T-131 get a new
  ledger and settle their NoEconomy history.

### 3. SQLite configuration

UE's SQLite runs on its own file layer with no shared memory, so plain WAL silently stays in
DELETE mode. The ledger therefore sets `locking_mode=EXCLUSIVE` + `journal_mode=WAL` +
`synchronous=FULL`, reads each one back, and refuses to run on any mismatch. The service then
syncs the world directory after opening. No ledger name is created or removed while the world
runs.

### 4. Economy policy version 1 (DEF-6, economic half)

- Balances are exact microlitres per material, in the owner's personal stock. There is no residue.
- Topsoil, Dirt, Stone, Deep Stone, Bedrock and Iron Ore pay; Air, Unknown and Fill do not.
- Tool 0 pays 100%.
- **Players place `Fill`** (new catalog material 8). It looks like dirt and never pays, which
  closes the dig/place/dig mint. Placement stays free, exactly as before.
- **The owner** is the BLAKE3 of the player's unique net id, captured at registration. No
  identity means NoEconomy. Owner 1 is server diagnostics.
- **Base compatibility:** a saved material catalog that is smaller is accepted, because ids are
  append-only.

### 5. Still open

- Inventory-based placement (a debit) is a GAME decision.
- Spending, crafting and transfers.
- Backups and ledger GC.
- Durable player identity, which needs a real login.

## D-047 — The stress profile's fixes; checkpoint gating moved to publication; group commit next (2026-09-21)

**Recorded:** CP-020 · **Class:** technical (per **D-023**) · **Architect ruling, logged
not asked** · **Scope:** T-132, gate 8, E-6 · **Status:** ACCEPTED · **Report:** P-011

### 1. Rulings

- **The service ticks once per world frame** (`FWorldDelegates::OnWorldTickStart`). The old 10 ms
  looping timer fired once per elapsed interval, which is about 4 times per 30 Hz frame, and so
  quadrupled every per-frame budget.
- **A checkpoint cut needs only no half-executed transaction** (`IsBetweenTransactions`), not an
  empty queue. **P-003 §3's G ≤ W is enforced at publication:** the pump encodes every chunk, and
  the root waits until the settlement watermark reaches G. The old start gates starved checkpoints
  completely under sustained load.
- Residency pins are **residency only**, with no collision or navmesh.
- The console pump respects the 32-record settlement window.
- Clients apply ops and snapshots from an **ordered inbox**, under an 8 ms per-frame budget with at
  least one item per frame. Channel order is preserved, so P-008 is unchanged.

### 2. Measured, on this machine

Correctness passes under the full design load. **Performance fails at 96 edits/s:**
- sustained 71–78 edits/s, and server frames p95 65–80 ms;
- the named causes are one journal flush per edit (3.5 ms, on the game thread) and checkpoint
  re-reads (about 3 ms per chunk).

E-6: a joiner caught up in 6.1 s mid-edit (243 snapshots, 1.6 MB).

### 3. Next

**T-133: journal group commit** takes up P-003 §2's anticipated batching, now that the evidence
exists. All commits in one pump call are appended, one flush covers them, and only then are they
broadcast or settled. A checkpoint trigger priced in chunks comes with it. It gets a short spec,
and the crash matrix must cover a torn multi-record append.

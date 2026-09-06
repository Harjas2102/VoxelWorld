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

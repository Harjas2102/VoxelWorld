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

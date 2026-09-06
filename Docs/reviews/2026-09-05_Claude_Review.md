# Architecture Review — Claude (Fable 5.1) — 2026-09-05

**Status:** Director-accepted in full on 2026-09-05 → adopted at **CP-002** (D-010 … D-016).
**Companions:** `Docs/reviews/2026-09-05_GPT56_Master_Handoff.md`, `Docs/reviews/2026-09-05_GPT56_GLM_Worker_Strategy.md`.
**Inputs reviewed:** both GPT-5.6 documents in full, the live repo (STATE/DECISIONS/BACKLOG at CP-001), the June 2026 genesis planning chat, current Voxel Plugin documentation.

---

## 1. Verdict

The June design was sound in its bones and wrong in three specific places. The
GPT-5.6 review found all three. Its central reframing — *the world permanently
records what the players did to it* — is a sharper statement of the founder's
idea than anything in the June docs. Most of the handoff is adopted. Parts are
scoped down because they prescribe a three-model engineering organization and
~3,000 lines of architecture for a repository containing zero gameplay code.

## 2. Most important finding

The repo sat at **CP-001 (2026-06-12)** for nearly three months: four commits,
T-101 never started. Not a fault (graduation, boards, job search, deliberate
pause) — but it means ~5,000 lines of planning now exist against 0 lines of
gameplay. The next evolution optimizes for **contact with the engine**, not more
documents. Hence the 7-day rule (§9).

## 3. What held up from June

- Ranked pillars, especially P1 (malleable/persistent) > P2 (realistic).
- Server authority, 16–32 scale, PvE-first + zoned PvP, creatures deferred,
  C++/Blueprint split, true first person + toggle.
- **D-007 repo-brain governance — validated by this event.** A different vendor's
  model read the docs cold, understood the project, produced a substantive
  review; the project survived a 3-month gap and a model-lineage change with
  nothing lost.

## 4. Where GPT was right and the June plan was wrong

1. **D-001 bundled three decisions** (gameplay requirement / representation /
   vendor). Only the requirement is durable; the backend must pass a gate.
2. **Internal contradiction:** D-002 says server-authoritative from day one, but
   the June BACKLOG deferred terrain edit sync to Phase 3 and scoped T-101 solo.
   Terrain — the riskiest system — was exempted from the principle. Official
   Voxel Plugin docs confirm the risk: runtime-edit replication is not provided
   out of the box, and join-in-progress state transfer has no easy path.
3. **Stale Nanite assumption.** "Voxel terrain is non-Nanite" was Voxel Plugin 1
   knowledge; Voxel Plugin 2 supports runtime Nanite and Lumen. GDD corrected.
4. **1 km² island was the wrong first target.** A 256–512 m abusive test hill
   that survives concurrent edits, restart, join-in-progress, and profiling is
   the right Phase 1. Milestone: *"one hill is trustworthy."*
5. **Material yield must be simulation-owned** — mining = measured removal of
   material from the world, not "raycast node → add ore." This is the mechanic
   that makes the game itself.
6. **The codename framed the project.** "VoxelWorld" made an implementation
   detail sound like the identity. Codename only; final title should not
   mention voxels.
7. **CLAUDE.md as constitution = vendor lock.** `AGENTS.md` becomes the
   vendor-neutral constitution; `CLAUDE.md` a thin adapter.
8. **Housekeeping:** repo is public while STATE said private; a UE-generated
   Android File Server token was committed in `Config/DefaultEngine.ini`.

## 5. Where GPT is scoped down

1. **Interface design before evidence.** The terrain-service/adapter/journal
   architecture is the right eventual shape, but design the adapter *after*
   touching the plugin's real API (T-101A first), not before.
2. **Tiered gate.** Fifteen flat tests will never be "passed." Gate-Critical
   (smooth terrain, material queries, authoritative edit path, concurrent edits,
   save/restart, join-in-progress, material yield, edit stress) must pass;
   Gate-Observe (foliage, nav, streaming, collapse, collision edges) is
   documented, not solved.
3. **Dual-vendor adversarial review at R3 milestones only** (terrain
   architecture, persistence, power graph) — not routinely.
4. **GLM worker strategy parked** until a queue of 10+ bounded tasks exists
   (Phase 2 at earliest). Its own rule applies: free is not automatically
   efficient.
5. **What GPT couldn't know:** the Director's time and the stall pattern. The
   next step must be small enough to happen this week with a defined minimum.

## 6. Plugin licensing discovery (post-review, 2026-09-05)

- **Voxel Plugin 2 is not free.** It is distributed via the Fab "Voxel Plugin
  Installer," which requires owning Voxel Plugin Pro Legacy (paid). Documented
  engine targets at last docs snapshot: 5.5/5.6 (re-verify).
- **Voxel Plugin Free Legacy** (GitHub: `VoxelPlugin/VoxelPluginFreeLegacy`)
  compiles on 5.6/5.7/5.8 and ships prebuilt **5.7 binaries** (direct zip in
  its README). Legacy 1.2 also documents a TCP-based multiplayer edit-sync
  example (voxel-level, supports JIP; requires open ports — fine for a
  self-hosted server) — a useful reference, though D-011 still requires our
  own authoritative layer.
- **Consequence:** initial provisional backend = **Voxel Plugin Free Legacy**.
  VP2 = upgrade candidate when budget and engine compatibility allow. The
  adapter (D-011) makes that swap a bounded task. Recorded in D-010 and R-008.

## 7. The "voxels as infrastructure" excerpt

Adopted as **D-015**, VISION-level. The June GDD already built this way in
practice (mesh buildings, Fab/Megascans assets, PCG foliage, only terrain
volumetric); GPT named the unstated principle. Test for every asset decision:
*the voxel grid should be as invisible to the player as the physics
broadphase.*

## 8. AI authoring position

- Do not wait for API budget; subscription coding agents are correct.
- Codex vs Claude Code is close; the repo is wired for Claude Code; decide by
  GPT's Section-39 blind benchmark (terrain authority architecture from
  identical context, cross-critiqued), which also produces the architecture
  proposal needed anyway.
- All Claude roles run on **Opus under Claude Pro** going forward.
- Astra (when verified available) = architecture forks; Opus = independent
  reviewer, or reversed. Writer ≠ reviewer for R3 work. See
  `Docs/DUAL_AGENT_SETUP.md` (gated on verified Astra access).

## 9. Decisions accepted (full text in DECISIONS.md)

- **D-010** Split D-001: volumetric requirement durable; backend provisional;
  Free Legacy first, VP2 candidate; feasibility gate required.
- **D-011** Authoritative terrain service; plugin behind adapter; never
  economic authority.
- **D-012** Persistence: deterministic base + chunk snapshots + append-only op
  journal + compaction + revision sequencing (provisional).
- **D-013** Terrain multiplayer inside the gate: JIP/restart/concurrency are
  pass criteria.
- **D-014** Vendor-neutral AI governance; implementer chosen by benchmark.
- **D-015** VISION amendment: voxels are infrastructure, not art direction.
- **D-016** Progression identity: increasing scale of environmental control —
  "the map physically records the factory's growth."

**Roadmap direction:** Phase 1 = terrain feasibility (1A smoke test → 1B tiered
gate → 1C material yield → 1D persistence → 1E JIP → 1F stress/decide).
Milestone: one hill is trustworthy. Only then the first complete physical loop.

**7-day rule:** if the first hole isn't dug within seven days of CP-002, the
plan is wrong, not the Director — cut the step smaller.

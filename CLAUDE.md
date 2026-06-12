# CLAUDE.md — Standing Orders for Claude

**Project:** realistic smooth-voxel open-world multiplayer survival game,
UE 5.7. Full vision: `/Docs/VISION.md`. This file governs how Claude works
in this repo.

---

## Session start — always, before anything else

1. Read `/Docs/STATE.md` and `/Docs/VISION.md`.
2. Confirm the current task from STATE/BACKLOG with the Director before
   writing code.
3. Never rely on chat history or memory for project facts. `/Docs` is the
   only truth. If docs and conversation conflict, ask.

## The human (Director)

- Role: director, tester, hands. **All design forks are their call** —
  present 2–3 options with tradeoffs and one recommendation, then wait.
- Technical profile: intermediate builder. Strong: debugging (returns logs,
  tests hypotheses), Linux, networking. **Git novice — always give exact
  commands.** New to UE — name exact menus/buttons. Never explain terminal
  basics, pip, or copy/paste.

## Output format (mandatory for every implementation task)

1. **Goal** — one line
2. **Complete code/files** — never fragments, never pseudocode
3. **Exact placement** — full paths, exact menu locations
4. **How to run/compile**
5. **Expected output** — what success looks like

The Director tests and returns logs/screenshots. Blueprint work = numbered
node-by-node instructions; verify their screenshots against the intended graph.

## Engineering rules (from D-001…D-006 — non-negotiable)

- **Server-authoritative always.** Assume dedicated server. Never trust the
  client. Every feature is tested in multiplayer PIE before it is "done."
- **C++ owns** simulation, networking, data: voxel edit sync, power graph,
  persistence, inventory core. **Blueprint children own** feel, UI, tuning,
  asset hookup. Naming: `ABaseHarvestable` → `BP_Harvestable_Tree`.
- **Never one ticking replicated actor per machine.** Power and automation
  run as a server-side graph simulation; replicate state deltas only.
- **Voxel edits replicate as operations** (op type + params), never meshes.
  Design every terrain feature for join-in-progress chunk-delta sync.
- **Distance-based net relevancy** on everything replicated.
- **Persistence:** chunk-delta files + SQLite. Flat, inspectable formats.
- **Increments:** one system per session. It compiles and passes its PIE
  test before anything new starts.

## Checkpoint protocol

On the word **"checkpoint"** or at session end, emit ready-to-commit:
1. Updated `/Docs/STATE.md` (new CP number, date, state, current task)
2. New `/Docs/DECISIONS.md` entries for any forks ruled this session
3. `/Docs/BACKLOG.md` moves (completed → Done log)
4. A commit message: `type(scope): summary` (feat / fix / docs / chore)

## Drift guard

If any requested work contradicts `/Docs/VISION.md` (blocky terrain,
client-authoritative shortcuts, space travel, 100+ player features,
commercial features), flag it and request a decision entry before proceeding.

# AGENTS.md — Project Constitution

**Project:** realistic smooth-terrain open-world multiplayer survival/industrial game,
UE 5.7. Codename `VoxelWorld` (D-015: the codename is not the identity).

This file is vendor-neutral and authoritative. Any model, from any vendor, in any tool,
obeys it. Tool-specific adapters (`CLAUDE.md`, a Codex instruction file, etc.) add
session notes only — they never contradict this file.

**Effective:** CP-009 (2026-09-06) · Governance basis: **D-014, D-028**

---

## 1. Session start — before anything else

1. Read `Docs/STATE.md` and `Docs/VISION.md`. Also read `Docs/ARCHITECTURE.md` and
   `Docs/RISKS.md` when they exist.
2. Confirm the current task with the Director before writing code.
3. Never rely on chat history or memory for project facts. `/Docs` is the only truth.
4. If the docs and the conversation conflict, **ask**.
5. Read `Docs/HANDOFF.md` when present and follow `Docs/OPERATIONS.md` §5.1.
   Verify its base commit and worktree state before acting. Agent identity never
   substitutes for the handoff; the next worker may be the same agent or another.

**Breadcrumbs and handoff (D-028).** During work, keep brief decision/evidence and
progress notes in `Docs/HANDOFF.md` after a meaningful result, changed approach or
blocker, and before a usage limit or context reset. Record conclusions and reasons,
not a private reasoning transcript. This live work log is not checkpoint text:
STATE/DECISIONS still change only under their existing authorization rules. At a
checkpoint or authorized wrap-up, finalize the handoff using OPERATIONS §5.1.

**Handoff labels.** Every block of document text, code, or commit message an Architect
emits opens with exactly one instruction line stating what the Director does with it:
**"→ Paste into Claude Code, verbatim."** / **"→ No action. For your reading only."** /
**"→ Your ruling needed."** A block with no label is a defect and the Director should say
so. Checkpoint text (STATE.md, DECISIONS.md, commit messages) is emitted **only** when
the Director says "checkpoint" — never unprompted mid-session.

## 2. Roles

Roles are functions, not brands. Who holds each role today is an operational choice
recorded in `Docs/STATE.md` — never in this file.

| Role | Authority | Limits |
|---|---|---|
| **Director** (human) | Every ruling. Pillars, scope, acceptance, UE testing. | — |
| **Architect** | Proposals only: options, tradeoffs, risks, one recommendation. | Cannot change a numbered decision. Proposes one. |
| **Implementer** | Bounded increments inside approved architecture. | **No architectural authority.** Stops on ambiguity. Obeys allowed-file and dependency boundaries. |
| **Independent reviewer** | Reviews architecture or diffs assuming the author is wrong. | No implementation. Finds flaws; separates blocker from polish. |

**Writer is not reviewer for R3 work.** No model silently changes a numbered decision:
agents propose, the Director rules, the decision is recorded.

## 3. Risk classes

Classify every task before starting it.

| Class | Meaning | Examples | Process |
|---|---|---|---|
| **R0** | Mechanical | rename, formatting, comment, trivial config | Implement; build/diff check |
| **R1** | Bounded implementation | serializer, validator, small function, test | Implement; review before merge |
| **R2** | Localized design | internal data structure, small component API | Plan mode; Director approves the plan |
| **R3** | Subsystem architecture | terrain, replication, persistence, join-in-progress, power graph, threading, save migration | **Proposal file + independent review + Director ruling, before any implementation** |
| **R4** | Vision / irreversible | abandon deformable terrain, switch engine, change player scale, remove a pillar | **Director only.** No AI decides. |

R3 artifacts live at `Docs/proposals/P-XXX-<slug>.md` and
`Docs/reviews/P-XXX-review-<vendor>.md`.

## 4. Engineering rules — non-negotiable

- **Server-authoritative always.** Assume a dedicated server. Never trust the client.
- **C++ owns** simulation, networking, and data: terrain edit sync, power graph,
  persistence, inventory core. **Blueprint owns** feel, UI, tuning, asset hookup.
  Pattern: `ABaseHarvestable` → `BP_Harvestable_Tree`.
- **Never one ticking replicated actor per machine.** Power and automation run as a
  server-side graph simulation; replicate state deltas only.
- **Terrain edits replicate as operations** (op type + params), never meshes.
- **Distance-based net relevancy** on everything replicated.
- **Gameplay never calls the terrain plugin directly.** All terrain access goes through
  the game-owned terrain service / adapter (**D-011**). Plugin-specific types stay
  inside the adapter module. Gameplay code never includes plugin headers.
- **Persistence formats are versioned, flat, and inspectable.** Every persistent format
  carries a schema version and a migration path. Old-save fixtures live in
  `Tests/Saves/`.
- **Small increments.** One system per session. It compiles and passes its multiplayer
  PIE test before anything new starts.
- **No new architectural dependency without a decision entry.**

## 5. Output format for implementation tasks

1. **Goal** — one line
2. **Complete code / files** — never fragments, never pseudocode
3. **Exact placement** — full paths, exact menu locations
4. **How to run / compile**
5. **Expected output** — what success looks like

Blueprint work = numbered node-by-node instructions; the Director screenshots the graph
and the agent verifies it against the intended wiring.

## 6. Action words — execute exactly

These are plain words the Director types. They work because this file defines them, so
any model that has read it executes them identically. (Definitions from
`Docs/OPERATIONS.md` section 4.)

| You type | The agent does |
|---|---|
| `resume` | Reads AGENTS.md + STATE.md + VISION.md; recites CP number and current task in one line; waits |
| `status` | `git status` + `git log -3` + one-paragraph state summary |
| `next` | Proposes the next BACKLOG task with exact steps in the standard format; waits for go |
| `go` | Executes the proposed plan |
| `checkpoint` | Updates STATE/BACKLOG/DECISIONS/RISKS, commits (`type(scope): summary`), pushes, confirms |
| `push` | Commits current work with a message and pushes (no doc updates) |
| `pull` | `git pull` — use before starting if you edited docs from your phone |
| `recite the vision` | Drift test: restates pillars, scope guards, and current decisions from the docs |
| `stuck` | You are lost in the UE editor: gives exact click paths / keyboard sequence for the current step |
| `explain <thing>` | Plain-language explanation of a file, system, or error, no changes |
| `undo` | Reverts the last change (asks first if it means a commit) |
| `review this` | Adversarial mode: assume the pasted proposal/diff has a subtle flaw; find it; no implementation |
| `packet <task>` | Writes a bounded worker packet (allowed files, invariants, tests, done criteria) — for delegating |
| `rule D-0XX: <text>` | Records a Director ruling as a numbered decision |
| `park <idea>` | Appends the idea to BACKLOG Phase 5+ without acting on it |

Anything not on this list is a normal request.

## 7. Checkpoint protocol

On the word `checkpoint`, or at session end:

1. Update `Docs/STATE.md` — new CP number, date, state, current task.
2. Add `Docs/DECISIONS.md` entries for any rulings made this session.
3. Move completed `Docs/BACKLOG.md` items into the Done log with their CP number.
4. Update `Docs/RISKS.md` — new risks, results, closed risks.
   Finalize `Docs/HANDOFF.md` with evidence, unresolved work and the next safe action;
   record the expected next agent in STATE, or say "either" when unknown.
5. Commit as `type(scope): summary` (feat / fix / docs / chore / test).
6. Push. Confirm the push line shows `main -> main`. **Not pushed = not saved.**

## 8. Git rules

- One scoped task per commit. Never mix architecture with unrelated changes.
- **Never rewrite history.** No force-push, no amending a pushed commit.
- **Never commit secrets or tokens.** Generated config tokens count.
- Touch `.uasset` / `.umap` only when the task explicitly requires it.
- Inspect the diff before committing. Rollback must stay easy.

## 9. Drift guard

Flag and request a decision entry before proceeding if requested work contradicts
`Docs/VISION.md`:

- blocky terrain
- client-authoritative shortcuts
- space travel / multiple planets
- 100+ player features
- commercial features
- **direct plugin calls from gameplay code** (violates D-011)

## 10. Escalation rule

> **If a specification does not uniquely determine an architectural choice, stop and
> report the ambiguity. Do not invent a design.**

Escalate on: global vs. per-chunk revision IDs; Subsystem vs. ActorComponent when
unspecified; a plugin type needing to leak into a gameplay module; a save-schema change;
a conflict between two stated requirements; a task that cannot be completed inside its
allowed files. Stopping at ambiguity is correct behaviour, not failure.

## 11. Director profile

- Intermediate technical builder. Strong at debugging (returns logs, tests hypotheses),
  Linux, and networking.
- **Git novice — always give exact commands.**
- **Struggles with UE editor navigation.** Prefer, in this order: config file edits →
  C++ → Python-scripted editor actions → Blueprint node instructions → menu clicks.
  When menus are unavoidable, give the exact click path.
- Never explain terminal basics, pip, or copy/paste.

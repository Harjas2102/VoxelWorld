# OPERATIONS.md — How to Run Every Session

> The operating manual. When you forget how this project works (you will —
> that is expected and fine), open this file. Lives in `/Docs`. Supersedes
> "The Ongoing Rhythm" table in SETUP.md.

**Updated:** CP-009 (2026-09-06) · **Implementation:** Claude or Codex, alternating by availability (D-028)

**Current entry point:** read STATE and HANDOFF using AGENTS §1. The Claude-specific
commands below are conveniences for Claude sessions, not gates for Codex.
The shared breadcrumb/handoff workflow is §5.1, including returns to the same agent.

---

## 0. The three direct answers

**Should the genesis chat (June 2026 planning chat) stay open?**
Retire it after CP-002 is committed. Keep it — never delete it — as a read-only
archive; it's searchable. But stop working in it. It's months old and carries
two 100 KB documents in context, which makes every reply slower, costlier, and
more prone to the exact drift you worried about in June. Everything in it that
matters now lives in the repo. Design work happens in **fresh, short chats
inside the "VoxelWorld" Claude Project**, opened with `Docs/CHAT_OPENER.md`.

**Is it okay to switch model and effort mid-chat?**
Yes — the app allows it and the repo-brain makes it safe. Hygiene rule: pick
model and effort **per session type**, not per message. High effort for design
forks and architecture; default for tactical questions and doc edits. Don't
flip back and forth inside one conversation; it wastes context and money.

**The Opus transition.**
From CP-002 forward, **every Claude role runs on Opus** (the Claude app for
design; Claude Code for building) under the Pro plan. Fable/Mythos-class
models are not required for this project. If you ever want to pay credits
for one, spend it on a single R3 milestone review (terrain architecture,
persistence, power graph) — never on routine work. Pro usage resets on a
5-hour window; §7 is the playbook for that.

---

## 1. Session types — pick one before you start

| Session type | Where | Model / effort | Opens with | Ends with |
|---|---|---|---|---|
| **Design** (decisions, architecture, planning, reviews) | Claude app → VoxelWorld Project → new chat | Opus, high effort | Paste `STATE.md` + `VISION.md` (opener auto-applies) | `checkpoint` → paste result into Claude Code |
| **Build** (code, docs, tooling, plugin setup) | Claude Code or Codex in `C:\Dev\VoxelWorld` | Available agent under the same risk rules | `resume` + HANDOFF | Formal handoff + authorized checkpoint/push |
| **Test** (UE editor, PIE, friends playtest) | Unreal editor | none | The runbook for the task | Screenshots + logs → next Build session |
| **Cross-review** (R3) | Independent reviewer, separate from author | per DUAL_AGENT_SETUP.md | proposal/diff file | review file saved to repo |

One task per session. If a second task appears, note it in BACKLOG and stop.

---

## 2. Claude Code on the gaming PC — re-setup and daily start

**If it has been a while:**
```
cd C:\Dev\VoxelWorld
git pull
claude --version
```
- `claude` not recognized → PATH lost or install gone. Fix PATH
  (`C:\Users\harja\.local\bin`) or reinstall in PowerShell:
  `irm https://claude.ai/install.ps1 | iex` — then reopen the terminal.
- Version prints → run `claude update` occasionally to stay current.
- Auth expired → inside the session type `/login` and complete the browser flow.

**Every Build session starts exactly like this:**
First check for existing work using §5.1; the following assumes a clean worktree.
```
cd C:\Dev\VoxelWorld
git pull --ff-only
claude
```
Then in Claude Code:
1. `/model` → choose **Opus**. (`/effort` → medium; high only for architecture.)
2. Type `resume` — it reads AGENTS.md, STATE.md, VISION.md and recites the
   checkpoint number and current task. **If the recital is wrong, stop and fix
   the docs before doing anything.**
3. Follow AGENTS risk classes: R0/R1 bounded implementation; R2 approved plan;
   R3 proposal + independent review + ruling. File count does not determine risk.

**Every Build session ends exactly like this:**
1. `/diff` — read what changed.
2. Type `checkpoint` — it updates STATE/BACKLOG/DECISIONS/RISKS, commits, pushes.
3. Confirm the push line shows `main -> main`. Not pushed = not saved.
4. `/exit`.

---

## 3. Claude Code built-in commands you will actually use

| Command | What it does | When |
|---|---|---|
| `/help` | Lists current commands (authoritative — the tool updates often) | When unsure |
| `/model` | Pick the model (choose Opus) | Session start |
| `/effort` | Reasoning depth slider (low → max) | Architecture work: high; boilerplate: low |
| `/context` | Shows what is filling the context window | Mid-session, before it gets slow |
| `/compact` | Summarizes the conversation to free context (can add focus instructions) | At ~80% context or after finishing a chunk |
| `/clear` | Wipes conversation, keeps files on disk | Switching to an unrelated task |
| `/plan` or Shift+Tab | Plan mode — proposes before editing | Before any big change |
| `/diff` | Shows all uncommitted changes | Before every commit |
| `/review` | Code-quality pass on the diff | Before committing anything non-trivial |
| `/rewind` (or Esc Esc) | Roll back code and/or conversation to a prior point | Something went wrong — faster than arguing |
| `/resume` (`/continue`) | Pick up an earlier Claude Code session | Continuing yesterday's task |
| `/stats` | Usage on Pro/Max | When planning around limits |
| `/rename` | Name the session | Start of a multi-day task |
| `!command` | Run a shell command inline (`!git status`) | Quick checks |
| `@path` | Attach a file to the message | Point at a log or header |
| `Esc` | Interrupt a running response | It's going the wrong way |

CLI flags: `claude -c` (continue last session), `claude -r` (pick a session),
`claude update`.

---

## 4. Action words — our protocol (defined in AGENTS.md, so every agent obeys them)

These are plain words you type. They work because AGENTS.md defines them —
any model that read AGENTS.md will execute them the same way.

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
| `stuck` | You're lost in the UE editor: gives exact click paths / keyboard sequence for the current step |
| `explain <thing>` | Plain-language explanation of a file, system, or error, no changes |
| `undo` | Reverts the last change (asks first if it means a commit) |
| `review this` | Adversarial mode: assume the pasted proposal/diff has a subtle flaw; find it; no implementation |
| `packet <task>` | Writes a bounded worker packet (allowed files, invariants, tests, done criteria) — for delegating |
| `rule D-0XX: <text>` | Records a Director ruling as a numbered decision |
| `park <idea>` | Appends the idea to BACKLOG Phase 5+ without acting on it |

Anything not on this list is a normal request — just describe what you want
in the standard format (goal → what you observed → what you need).

---

## 5. The standard Build session, as a checklist

```
[ ] Check status and HANDOFF (§5.1); sync a clean tree with git pull --ff-only
[ ] Open available agent; resume → recital correct
[ ] next → read plan → go   (risk process from AGENTS §3)
[ ] Work. Return logs / screenshots / "expected vs actual" when asked.
[ ] /diff → /review (if code)
[ ] checkpoint → confirm push
[ ] /exit
```
Target length: 60–120 minutes. Longer sessions drift; checkpoint and restart.

### 5.1 Breadcrumbs and formal handoff

**One rolling file: `Docs/HANDOFF.md`.** No extra per-agent logs or copied transcripts.
STATE remains checkpoint truth; the handoff is a working note, never a ruling.

1. **Receive:** check `git status --short`, branch and `git log -3 --oneline`; on a
   clean tree use `git pull --ff-only`. Preserve dirty work, identify its owner/task,
   and reconcile it before edits. Read AGENTS' required docs plus HANDOFF. Verify its
   base commit and unfinished-work claims. Do not depend on access to the old chat.
2. **Set scope:** record task, risk class, agent/role, approved boundaries and base
   commit in HANDOFF. Confirm the task as AGENTS requires. Address the eventual
   handoff to whoever continues; an unknown incoming agent is normal.
3. **Leave breadcrumbs:** after a meaningful result, failed approach, changed decision
   or blocker, add **what changed/was learned → why → evidence/file → next action**.
   Capture useful conclusions and concise rationale, not hidden deliberation or a
   reasoning transcript. A few lines suffice. Label pending ideas; link approved
   choices to their ruling.
4. **Before limits or context reset:** save the exact safe restart point, changed or
   staged files, incomplete tests, failing commands and any running process or cleanup.
   Do this before spending the last response. Never label unfinished/untested code done
   or commit it as a finished increment. Prefer an authorized checkpoint; `push` alone
   retains its AGENTS meaning.
5. **Finalize:** replace the rolling handoff with task/outgoing role; completed and
   remaining work; decision summaries and sources; exact verification commands/results
   and limits; failures/blockers; base commit/worktree state; and numbered next safe
   actions with approval boundaries. Use HANDOFF's sections as the template. STATE
   records the expected next agent if known, but either agent can receive the artifact.
6. **Save and transfer:** at checkpoint or an explicitly authorized session wrap-up,
   update checkpoint docs and HANDOFF, review the diff, commit and push. Verify
   `main -> main` and clean status; report the commit in the final response. The
   handoff can refer to its enclosing commit; do not amend merely to embed its own SHA.
   On an interrupted/dirty transfer, identify uncommitted/unpushed work for the receiver
   to reconcile. Git history preserves older handoffs.

One active Implementer per workspace. Alternation does not require duplicate work or
another proposal for already approved work. R3 independence and R2 approval remain.

---

## 6. Phone workflow (between shifts)

- **Design chat:** GitHub app → VoxelWorld → Docs → STATE.md → copy raw →
  Claude app → VoxelWorld Project → new chat → paste (+ VISION.md for design
  forks). The Project's custom instructions carry the opener automatically.
- **Commit a checkpoint from the phone:** GitHub app → file → pencil → paste
  the exact text the chat emitted → Commit changes. Then `pull` at the next
  Build session.
- **Reading reviews/proposals:** they live in `Docs/reviews/` and
  `Docs/proposals/`; readable in the GitHub app.

---

## 7. Usage-limit playbook (either agent)

Hitting the limit is a scheduling event, not a crisis.
1. Finalize HANDOFF before the limit (§5.1); checkpoint/push when authorized. If
   interrupted with dirty work, record exact files, test state and the safe restart.
2. Use the pause for work that needs no AI: compile, open the editor, run the
   PIE test, take screenshots, read the runbook, install a tool.
3. Prepare the report for the next session (logs, screenshots, what happened).
4. Switch implementation to Claude or Codex according to availability. Both consume
   the same handoff; neither is limited to review/doc work (D-028).
5. Spend Opus on: architecture, subtle bugs, review. Don't spend it on
   renames, comments, or reformatting — batch those.

---

## 8. Hygiene rules (the ten that matter)

1. One task per session. Second task → BACKLOG, not the same session.
2. Every session ends with `checkpoint` and a confirmed push. No exceptions.
3. Chats are disposable; the repo is permanent. Retire long chats without guilt.
4. `resume` first, always. A wrong recital means fix docs before work.
5. Follow AGENTS risk classes: R2 approved plan; R3 independent review/ruling before implementation.
6. Small diffs, small commits (`feat(terrain): …`, `docs: …`, `fix(net): …`).
7. Run the approved task's checks: headless tests for TerrainCore increments, PIE
   for gameplay/multiplayer, diff/link checks for documentation-only changes.
8. UE editor problems → screenshot + `Saved/Logs/VoxelWorld.log`, then `stuck`.
9. Docs beat memory. If an agent "remembers" something not in `/Docs`, it's
   wrong until verified.
10. No model changes a numbered decision. Agents propose; you rule; it's recorded.

---

## 9. Getting the agent to do the Unreal work for you

You find the editor hard to navigate. Push as much as possible to the agent:
- **Config and plugins:** it edits `VoxelWorld.uproject` and `Config/*.ini`
  directly (enable/disable plugins, settings) — no menus needed.
- **Launching:** it can start the editor from the command line and read
  `Saved/Logs/VoxelWorld.log` afterward to see what happened.
- **Editor scripting:** with the **Python Editor Script Plugin** enabled
  (CP-002 enables it), the agent writes Python scripts that place actors, set
  properties, and create assets — you run them from the editor's Output Log
  (`py path\to\script.py`) or it launches the editor with the script.
- **C++ over Blueprint** wherever possible (D-006) — it writes the whole file;
  you press nothing but Compile.
- **Blueprint when unavoidable:** it gives numbered node instructions; you
  screenshot; it verifies.
- **Scheduled:** the D-025 engine/tooling upgrade is T-112.5, before T-113.

When the instruction says "click X," and you can't find X: type `stuck` and
paste a screenshot. That's the intended path, not a failure.

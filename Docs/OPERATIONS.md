# OPERATIONS.md — How to Run Every Session

> The operating manual. When you forget how this project works (you will —
> that is expected and fine), open this file. Lives in `/Docs`. Supersedes
> "The Ongoing Rhythm" table in SETUP.md.

**Effective:** CP-002 (2026-09-05) · **Claude tier:** Opus under Claude Pro

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
| **Build** (code, docs, tooling, plugin setup) | Claude Code in `C:\Dev\VoxelWorld` | Opus via `/model`; `/effort` medium (high for R3 work) | `resume` | `checkpoint` (commits + pushes) |
| **Test** (UE editor, PIE, friends playtest) | Unreal editor | none | The runbook for the task | Screenshots + logs → next Build session |
| **Cross-review** (only when Astra is verified — see DUAL_AGENT_SETUP.md) | ChatGPT + Claude | per that doc | proposal/diff file | review file saved to repo |

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
```
cd C:\Dev\VoxelWorld
git pull
claude
```
Then in Claude Code:
1. `/model` → choose **Opus**. (`/effort` → medium; high only for architecture.)
2. Type `resume` — it reads AGENTS.md, STATE.md, VISION.md and recites the
   checkpoint number and current task. **If the recital is wrong, stop and fix
   the docs before doing anything.**
3. Press **Shift+Tab** until the status line says **plan mode** for anything
   larger than a one-file edit. Read the plan. Approve. Then it executes.

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
[ ] cd C:\Dev\VoxelWorld && git pull && claude
[ ] /model → Opus     [ ] resume → recital correct
[ ] next → read plan → go   (plan mode for anything big)
[ ] Work. Return logs / screenshots / "expected vs actual" when asked.
[ ] /diff → /review (if code)
[ ] checkpoint → confirm push
[ ] /exit
```
Target length: 60–120 minutes. Longer sessions drift; checkpoint and restart.

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

## 7. Usage-limit playbook (Pro, 5-hour window)

Hitting the limit is a scheduling event, not a crisis.
1. Don't end the session dirty — if you can, `push` first (one short message).
2. Use the pause for work that needs no AI: compile, open the editor, run the
   PIE test, take screenshots, read the runbook, install a tool.
3. Prepare the report for the next session (logs, screenshots, what happened).
4. If Astra is verified, design and review work can continue there
   (`DUAL_AGENT_SETUP.md`).
5. Spend Opus on: architecture, subtle bugs, review. Don't spend it on
   renames, comments, or reformatting — batch those.

---

## 8. Hygiene rules (the ten that matter)

1. One task per session. Second task → BACKLOG, not the same session.
2. Every session ends with `checkpoint` and a confirmed push. No exceptions.
3. Chats are disposable; the repo is permanent. Retire long chats without guilt.
4. `resume` first, always. A wrong recital means fix docs before work.
5. Plan mode before anything bigger than one file.
6. Small diffs, small commits (`feat(terrain): …`, `docs: …`, `fix(net): …`).
7. Nothing is "done" until it compiles and passes its PIE test.
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
- **Later (R2 evaluation, not now):** MCP editor control lets the agent drive
  the editor directly. Evaluate once T-101B is done.

When the instruction says "click X," and you can't find X: type `stuck` and
paste a screenshot. That's the intended path, not a failure.

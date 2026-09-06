# CLAUDE.md — Claude adapter

Read `AGENTS.md`; it is authoritative. This file adds Claude-specific session notes
only and never overrides it.

Claude-specific notes:

- Check `git status --short` first; on a clean worktree, run `git pull --ff-only`
  before starting. Preserve and reconcile existing work instead of overwriting it.
- Read `Docs/HANDOFF.md` after the required project docs. Follow the shared
  breadcrumb and handoff protocol in `Docs/OPERATIONS.md` §5.1; Claude and Codex
  alternate implementation according to availability (D-028), not fixed ownership.
- `/model` → **Opus**. `/effort` medium for routine work, **high for R3** (terrain,
  replication, persistence, join-in-progress, power graph, threading, save migration).
- Use **plan mode** (Shift+Tab) for anything R2 or larger — show the plan, wait for
  approval, then execute.
- This project has **no API budget**. Respect the Pro 5-hour usage window; see
  `Docs/OPERATIONS.md` section 7 for the limit playbook.
- End every session with `checkpoint` and a confirmed push (`main -> main`).

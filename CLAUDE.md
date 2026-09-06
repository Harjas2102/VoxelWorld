# CLAUDE.md — Claude adapter

Read `AGENTS.md`; it is authoritative. This file adds Claude-specific session notes
only and never overrides it.

Claude-specific notes:

- Run `git pull` before starting; the Director edits docs from their phone.
- `/model` → **Opus**. `/effort` medium for routine work, **high for R3** (terrain,
  replication, persistence, join-in-progress, power graph, threading, save migration).
- Use **plan mode** (Shift+Tab) for anything R2 or larger — show the plan, wait for
  approval, then execute.
- This project has **no API budget**. Respect the Pro 5-hour usage window; see
  `Docs/OPERATIONS.md` section 7 for the limit playbook.
- End every session with `checkpoint` and a confirmed push (`main -> main`).

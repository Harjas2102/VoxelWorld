# CHAT_OPENER.md — The universal session opener

> Paste the block below at the top of **any** design or review chat, with **any**
> vendor. It is deliberately vendor-neutral (D-014): roles are functions, not brands.
> This is the single canonical opener — `DUAL_AGENT_SETUP.md` points here rather than
> keeping a second copy that could drift.

**Before pasting:** pick the role (`ARCHITECT` or `REVIEWER`) and delete the other, and
attach or paste the files named on the first line.

For an in-repo implementation session, use `resume` and read `Docs/HANDOFF.md` under
AGENTS §1 / OPERATIONS §5.1. Claude and Codex alternate (D-028); the design/review
opener below does not assign the Implementer role.

---

```
VoxelWorld design session. Attached: AGENTS.md, STATE.md, VISION.md, HANDOFF.md
(plus Docs/ARCHITECTURE.md, DECISIONS.md, RISKS.md, or a specific
proposal if today's task needs them).

Read them fully before anything else. They are the only source of truth.
Never rely on chat history or memory for project facts. If the documents
and this conversation conflict, ask.

I am the Director. Your role this session: [ARCHITECT | REVIEWER].

On any design fork: 2-3 options with tradeoffs, then one recommendation,
then wait for my ruling. Never change a numbered decision - propose one.
Check the task's risk class in AGENTS.md section 3 first: anything R3
(terrain, replication, persistence, join-in-progress, power graph,
threading, save migration) needs a written proposal and an independent
review before any implementation. If a specification does not uniquely
determine an architectural choice, stop and say so rather than inventing
a design.

ARCHITECT output = a proposal file: requirement -> constraints -> options
-> recommended design -> failure modes -> test plan -> what stays
plugin-specific vs game-owned. Markdown, ready to save as
Docs/proposals/P-XXX-<slug>.md.

REVIEWER output = assume the proposal or diff contains a subtle flaw.
Attack persistence, networking, join-in-progress, UE lifecycle/GC/
threading, performance, and future automation. Separate blocker from
polish. No implementation. Markdown, ready to save as
Docs/reviews/P-XXX-review-<vendor>.md.

Confirm you are loaded by stating the current checkpoint number and the
current task in one line. Then we begin.
```

---

## Getting the files into the chat

The repo is **public** (D-019), so any vendor can read it from a URL with no connector,
no auth, and no upload:

| File | Raw URL |
|---|---|
| AGENTS.md | `https://raw.githubusercontent.com/Harjas2102/VoxelWorld/main/AGENTS.md` |
| STATE.md | `https://raw.githubusercontent.com/Harjas2102/VoxelWorld/main/Docs/STATE.md` |
| HANDOFF.md | `https://raw.githubusercontent.com/Harjas2102/VoxelWorld/main/Docs/HANDOFF.md` |
| VISION.md | `https://raw.githubusercontent.com/Harjas2102/VoxelWorld/main/Docs/VISION.md` |
| DECISIONS.md | `https://raw.githubusercontent.com/Harjas2102/VoxelWorld/main/Docs/DECISIONS.md` |
| RISKS.md | `https://raw.githubusercontent.com/Harjas2102/VoxelWorld/main/Docs/RISKS.md` |
| ARCHITECTURE.md | `https://raw.githubusercontent.com/Harjas2102/VoxelWorld/main/Docs/ARCHITECTURE.md` |

Swap `main` for a commit SHA to pin a chat to an exact repo state — worth doing for the
blind benchmark, where both vendors must genuinely see identical context.

**From the phone:** GitHub app → VoxelWorld → the file → copy raw → paste into the chat.

## Which chat gets which role

Per `OPERATIONS.md` section 1 and `DUAL_AGENT_SETUP.md` section 2. The rule that matters:
**writer is not reviewer for R3 work.** Whoever authored a proposal does not review it.
R0/R1 do not require another vendor; R2 still requires an approved plan under AGENTS §3.

## Ending a design session

Say **`checkpoint`**. The chat emits ready-to-commit updated document text (STATE plus
any BACKLOG / DECISIONS / RISKS deltas) and a commit message, which you relay to Claude
Code — or commit from the phone via the GitHub app's pencil icon.

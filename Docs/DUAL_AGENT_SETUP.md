# DUAL_AGENT_SETUP.md — Claude + GPT-6 Astra in Unison

> **GATE: do not follow anything below §1 until §1 is fully verified.**
> Until then, the single-vendor flow in `OPERATIONS.md` is the whole system.
> This document adds a second vendor for *independent review and
> architecture*; it never replaces the repo as the source of truth.

Governance basis: **D-014** (vendor-neutral). Roles are functions, not brands.
Any model can hold any role; assignments below are today's operational choice.

---

## 1. The gate — verify before enabling any of this

Check all four in the actual product UI, not from rumor:
- [ ] In ChatGPT's model picker, **GPT-6 Astra** is selectable on your plan
      and a test message ("reply with the word ready") completes.
- [ ] You know your plan's **Astra usage allowance** (Settings / Help docs) —
      Plus-type seats have a limited allowance; it is not infinite.
- [ ] You know whether **Codex** is available to your account (optional; the
      workflow works without it).
- [ ] `AGENTS.md` exists at the repo root (created at CP-002).

If any box is unchecked, stop here. Nothing in the project depends on Astra.

---

## 2. Roles (recorded in AGENTS.md)

| Role | Holder today | Holds authority over |
|---|---|---|
| **Director** | Harjas | Every ruling. Pillars. Scope. Acceptance. UE testing. |
| **Architect** | Astra (R3 work) — Opus if Astra unavailable | Proposals only: options, risks, recommendation |
| **Independent reviewer** | Whichever vendor did *not* author | Finding flaws. No implementation. |
| **Implementer** | Claude Code (Opus) — Codex as alternate if available | Bounded increments inside approved architecture |
| **Constitution** | `/Docs` | Everything. No model edits a numbered decision. |

Rule: **writer ≠ reviewer for R3 work** (terrain architecture, persistence,
join-in-progress, power graph, threading, save migration). Routine R0/R1 work
needs no second vendor.

---

## 3. The universal opener

Moved to **`Docs/CHAT_OPENER.md`** — that is now the single canonical opener for every
vendor and every chat. Keeping a second copy here would guarantee the two drift apart.

Set the role line to `ARCHITECT` or `REVIEWER` before pasting. `CHAT_OPENER.md` also
carries the raw GitHub URLs, so any vendor can read the documents without a connector.

## 4. The relay — how a consequential (R3) subsystem gets built

Every handoff is a **file in the repo**, never a chat memory. This is what
makes two vendors safe.

1. **Sync.** `git pull`. Read STATE. Confirm the task is R3 (see AGENTS.md
   risk classes). If it's R0–R2, skip to step 6 — Claude Code alone.
2. **Proposal (Architect = Astra).** Opener + docs + task → proposal →
   save as `Docs/proposals/P-XXX-<slug>.md`
   (from the PC: tell Claude Code `save this as a proposal` and paste it;
   from the phone: GitHub → Add file).
3. **Review (Reviewer = Opus, fresh Claude chat).** Opener in REVIEWER role +
   proposal → review → save as `Docs/reviews/P-XXX-review-claude.md`.
   *(Optional second round: Astra responds to the review; revised P-XXX v2.)*
4. **Ruling (Director).** Read both. Pick/revise. In Claude Code:
   `rule D-0XX: <decision>` → recorded, committed.
5. **Architecture doc.** Claude Code updates `Docs/ARCHITECTURE.md` with the
   ruled design (one increment, reviewed diff).
6. **Implement (Claude Code).** One small increment. Compile. PIE test.
7. **Cross-review the diff (Reviewer = Astra).** `git diff main~1` (or the
   task's diff) pasted into Astra with the REVIEWER opener:
   "Review this diff against ARCHITECTURE.md and D-0XX. Blockers first."
   Fix real defects; ignore style nits unless cheap.
8. **Checkpoint.** `checkpoint` in Claude Code → STATE/BACKLOG/RISKS
   updated, commit, push.

Per feature this is slower. Per *project* it is much faster than recovering
from a foundational mistake — that is the entire argument.

---

## 5. Daily operating rhythm with two vendors

- **Morning / before a session:** decide the session type (OPERATIONS §1) and
  which role each vendor holds today. Write it in the first message.
- **Alternate on limits.** Claude window exhausted → Astra does review and doc
  work; Astra allowance exhausted → Opus reviews its own vendor's proposal in
  a *fresh* chat with the REVIEWER opener (weaker independence, still useful).
- **Never run both on the same task simultaneously** without a file between
  them. Proposal → file → review. Diff → file → review.
- **Astra is for leverage, not volume.** Terrain authority, persistence
  protocol, JIP, power graph, save migration, dangerous refactors,
  postmortems. Not comments, renames, or STATE edits.
- **Codex (if available) = alternate implementer,** and diff reviewer for
  Claude Code's work when you want a second implementation opinion. Same
  rules: reads AGENTS.md, small increments, compile before done.

---

## 6. First joint task — the blind benchmark (from the GPT handoff, §39)

This both chooses the primary implementer *and* produces the architecture the
project needs next. Run it right after T-101A (once the plugin's real API has
been touched).

1. Identical context to both vendors: AGENTS, VISION, GDD, DECISIONS, STATE,
   RISKS, the two review files, and T-101A findings. **Do not show either the
   other's answer.**
2. Identical challenge to both:
   > Design a server-authoritative, persistent terrain-editing architecture
   > for the current UE 5.7 project using Voxel Plugin Free Legacy as the
   > provisional backend behind a replaceable adapter, supporting 16–32
   > players, join-in-progress, mining yield from removed material,
   > snapshot + operation persistence, chunk relevancy, concurrent edits,
   > future powered excavation, and backend replacement. Require: class/
   > subsystem boundaries, data structures, server/client sequence,
   > persistence schema, chunk revision model, concurrency, JIP, failure
   > recovery, tests, performance risks, what stays plugin-specific.
3. Save as `Docs/proposals/P-001-terrain-astra.md` and
   `Docs/proposals/P-001-terrain-claude.md`.
4. Swap: each reviews the other. Save both reviews in `Docs/reviews/`.
5. Score (GPT §39 criteria): correct reading of vision, real risks found,
   avoids unnecessary complexity, UE correctness, persistence/networking
   correctness, testability, replaceability, willingness to say "unknown —
   prototype this," survives critique.
6. Director rules: `rule D-017: terrain architecture = …` and
   `rule D-018: primary implementer = …`. ARCHITECTURE.md v1 written from the
   winning proposal (with the loser's valid critiques folded in).

---

## 7. What never changes, regardless of vendor

- `/Docs` is the constitution. Models are staff.
- No proposal becomes a decision without a `D-0XX` entry ruled by the Director.
- No diff is "done" without compile + PIE test on the actual machine.
- If a vendor disappears, changes pricing, or is replaced, the workflow is
  unchanged: same files, same roles, different holder.

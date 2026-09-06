# DUAL_AGENT_SETUP.md — Alternating Claude and Codex

**Updated:** CP-009 (2026-09-06) · **Basis:** D-014, D-023, D-028.

Both agents have worked in this repository. Claude completed T-112.1; Astra/Codex
completed T-112.2 at CP-008. No model-picker or subscription gate is required to
continue with an already working tool. The Director chooses according to available
usage and time; actual allowances are checked in the chosen product, not assumed here.

## 1. Receive the work

Read AGENTS and its required project docs, then `HANDOFF.md`. Follow the single
stepwise protocol in **OPERATIONS §5.1**. It applies to either vendor and to a fresh
session of the same agent. No copying an entire old chat is required.

## 2. Roles

Current assignments live in **STATE**, never in this file or AGENTS.

- **Director:** scope, acceptance and GAME rulings.
- **Implementer:** Claude or Codex, one at a time, bounded by approved architecture.
- **Architect:** technical rulings under D-023; implementation authority does not
  silently become architectural authority. D-027's delegation was T-112.2 only.
- **Independent reviewer:** distinct from the author for R3; another vendor is the
  default. A fresh chat of the same author does not satisfy independence.

## 3. Openers

In-repo: `resume`, verify the recital/handoff, then task confirmation and the appropriate
risk process. Design/review chats: `CHAT_OPENER.md`. Switching agents changes neither
the approved task nor the verification requirements.

## 4. R3 relay

1. Architect writes a proposal under `Docs/proposals/`.
2. Independent reviewer records blockers/polish under `Docs/reviews/`.
3. Obtain the applicable ruling under AGENTS and D-023; update the implementation spec.
4. Available Implementer executes one bounded increment and runs its required checks.
5. Review the diff under the task's risk process; finish the shared handoff and checkpoint.

R0/R1 do not acquire this ceremony by using two agents. R2 still needs an approved plan.

## 5. Alternating on usage limits

Leave decision/evidence breadcrumbs during work, then a formal handoff before stopping
(OPERATIONS §5.1). The expected next agent is a scheduling note, not a dependency.
Do not run two Implementers against the same working tree simultaneously. If no agent
can continue, the same handoff remains sufficient when one becomes available.

## 6. Blind benchmark — completed historical task

The original benchmark compared identical context and independent proposals, then
cross-reviewed them. Its input is preserved in `benchmark/P-001-CONTEXT.md`, with
proposals and reviews under `reviews/`. D-017/D-022 adopted architecture v1; D-018's
fixed primary-implementer assignment is superseded by D-028. Do not rerun the benchmark
as an onboarding gate or follow the historical packet as current implementation scope.

## 7. What remains invariant

AGENTS governs all tools; `/Docs` holds project facts. Same architecture, same code,
same tests, same handoff, whichever agent is available. A successful headless check
does not replace required gameplay/PIE evidence, and neither replaces an R3 review.

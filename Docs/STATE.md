# STATE.md — Current Project State

> **Paste this file + VISION.md at the start of every Claude session**
> (or let Claude Code read them from the repo). Updated at every checkpoint.

---

**Checkpoint:** CP-000 · **Date:** 2026-06-12
**Phase:** 0 — Environment Setup (pre-engine)

## What exists right now

Nothing in-engine. Plan and documentation only. This doc pack (VISION, GDD,
DECISIONS, STATE, BACKLOG, CLAUDE) is the entire project artifact set.

## Validated

- **Dev hardware green-lit:** Ryzen 7 9800X3D / RX 9070 XT 16GB / 64GB
  DDR5-6000. Fully capable for UE 5.7 + Lumen development and multi-client
  PIE testing. FSR (not DLSS) is the upscaler. Engine-from-source compile
  (Phase 3, Linux server target) = overnight job on 8 cores, acceptable.
- **To verify before install:** 250GB+ free NVMe space.

## Current task

Week-1 setup: **T-001 → T-004** (install UE 5.7, template project + shader
compile, 3-player multiplayer PIE test, Git/LFS init + commit this doc pack).
Then T-005 (Claude Code install).

## Definition of done for this phase

Director reports: shader compile finished, three PIE windows replicating
movement, `git status` clean after initial commit, Claude Code answering
from inside the repo.

## Blockers

None.

## Open decisions

D-008 (working title) · survival meter set · T3 transport method ·
structural integrity model

## Toolchain status

| Tool | Status |
|---|---|
| UE 5.7 | NOT installed |
| Git + LFS | NOT installed |
| Claude Code | NOT installed |
| Voxel Plugin (free) | Not yet acquired (Phase 1, T-101) |

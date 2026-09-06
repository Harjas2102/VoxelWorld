# RISKS.md — Architectural Risk Register

> The top architectural unknowns, each with an experiment that resolves it. A risk is
> closed only by a **result** and a **decision**, never by an opinion. Updated at every
> checkpoint (AGENTS.md section 7).

**Opened:** CP-002 (2026-09-05)

Severity / probability scale: **High · Medium · Low**.

---

## R-001 — Terrain backend multiplayer synchronization

- **Severity:** High — Pillar 1 depends on it.
- **Probability:** High — Voxel Plugin Free Legacy provides no server-authoritative
  edit replication out of the box.
- **Mitigation experiment:** T-101B Gate-Critical — client requests an edit, server
  validates and applies, all clients converge. 2–3 clients editing the same region with
  deterministic ordering and no permanent divergence.
- **Owner / task:** T-101B (sub-step 1B)
- **Result:** *open*
- **Decision:** *open*

## R-002 — Join-in-progress modified-chunk transfer

- **Severity:** High — a friends server is joined mid-session constantly.
- **Probability:** High — Legacy has no documented JIP state-transfer path.
- **Mitigation experiment:** Server holds heavily edited terrain; a fresh client joins
  and reconstructs modified chunks from snapshot + ops since, without replaying full
  server history and without visible divergence.
- **Owner / task:** T-101B (sub-step 1E)
- **Result:** *open*
- **Decision:** *open*

## R-003 — Terrain save growth and compaction

- **Severity:** Medium — a server that cannot be saved cheaply cannot run for months.
- **Probability:** Medium
- **Mitigation experiment:** Hundreds to thousands of edits, then measure save-file
  growth, snapshot size after compaction, and bytes per edit. Establish whether the
  operation journal can be compacted into a compact chunk snapshot at revision R.
- **Owner / task:** T-101B (sub-steps 1D, 1F)
- **Result:** *open*
- **Decision:** *open*

## R-004 — Material-yield accuracy

- **Severity:** High — mining as measured removal is the mechanic that makes the game
  (D-011, D-016). If yield cannot be derived from removed volume, mining degenerates
  into "raycast node, add ore."
- **Probability:** Medium — the sphere tools return `ModifiedValues`, so the hook
  exists; whether it is accurate and deterministic is unproven.
- **Mitigation experiment:** T-101B Gate-Critical — dig a known volume of a known
  material and confirm the server computes soil / stone / ore quantities
  deterministically from the edit, not from the rendered mesh.
- **Owner / task:** T-101B (sub-step 1C)
- **Result:** *open*
- **Decision:** *open*

## R-005 — PCG / foliage invalidation after edits

- **Severity:** Low — cosmetic in Year 1.
- **Probability:** High — trees left floating over an excavation is the default
  behaviour of most systems.
- **Mitigation experiment:** Gate-Observe — remove terrain under foliage, document what
  happens, define a response model. Not solved at the gate.
- **Owner / task:** T-101B (sub-step 1F, Gate-Observe)
- **Result:** *open*
- **Decision:** *open*

## R-006 — Dynamic navigation after edits

- **Severity:** Medium — gates creature AI (D-005 Year 1 wildlife).
- **Probability:** Medium
- **Mitigation experiment:** Gate-Observe — measure nav dirtying and rebuild behaviour
  after terrain edits. Does not need to be production-ready; it needs to be known.
- **Owner / task:** T-101B (sub-step 1F, Gate-Observe)
- **Result:** *open*
- **Decision:** *open*

## R-007 — Dedicated-server / plugin compatibility

- **Severity:** High — D-002 makes a Linux dedicated server the shipping target.
- **Probability:** Medium — the plugin declares Win64/Linux/Mac module support, but a
  server build has never been attempted and the shipped binaries are editor DLLs.
- **Mitigation experiment:** Confirm the architecture never requires client-only plugin
  behaviour; attempt a server target build at Phase 4 (T-302 successor).
- **Owner / task:** T-101B (architecture check) → Phase 4 (build proof)
- **Result:** *open*
- **Decision:** *open*

## R-008 — Plugin licensing and version risk

- **Severity:** High — this is the backend the whole gate runs against.
- **Probability:** High.
  - Voxel Plugin **Free Legacy** is maintenance-mode. Installed build: **v432 /
    `e9648b302` / EngineVersion 5.7.0**.
  - **Voxel Plugin 2 is paid** and engine-version-gated: distributed via the Fab "Voxel
    Plugin Installer," which requires owning Voxel Plugin Pro Legacy. Documented engine
    targets at last docs snapshot were 5.5/5.6 — re-verify before relying on it.
- **Mitigation experiment:** The D-011 adapter keeps the backend replaceable, and the
  T-101B gate decides whether Free Legacy is adopted at all. No gameplay code takes a
  dependency on plugin types.
- **Owner / task:** D-010, D-011, T-101B
- **Result — FIRST HIT, 2026-09-05 (T-101A):** **Voxel Graphs are Pro-gated at runtime.**
  `Voxel: Running Voxel Graphs require Voxel Plugin Pro`. All 106 `VoxelGraphGenerator`
  example assets produce an **empty world with no error** — the asset loads,
  `SetGeneratorObject` succeeds, `IsCreated()` returns true, the world reports a
  generation time, and the density field is empty. Free ships exactly two runnable
  generators, both C++: `UVoxelFlatGenerator` and `UVoxelEmptyGenerator`.
  → Procedural generation must be a C++ `UVoxelGenerator` subclass (**T-108**), promoted
  from "later" to a Phase 1 requirement. Full write-up: `T-101A_FINDINGS.md` section 2b.
- **What this changes about the risk:** the exposure is worse than "the free tier has
  fewer features." Gates are **silent at runtime** and are not documented in the headers,
  so each one is found by something quietly not working. Assume more exist; budget for
  discovery in every sub-step of T-101B rather than treating each as a surprise.
- **Decision:** *open* — resolved by the T-101B exit (PASS / CONDITIONAL / FAIL /
  VISION CHANGE). This finding does not by itself argue for VP2: the C++ generator is
  work we owed D-011 regardless.

## R-009 — Director availability and project stall

- **Severity:** High — the project's largest observed failure mode. The repo sat at
  CP-001 for nearly three months: four commits, ~5,000 lines of planning against 0 lines
  of gameplay.
- **Probability:** High — the Director's time is genuinely constrained.
- **Mitigation experiment:** The **7-day rule** — if the first hole is not dug within
  seven days of CP-002, the plan is wrong, not the Director: cut the step smaller. Every
  task is scoped to one evening. Agents prefer config, C++, and Python-scripted editor
  actions over menu instructions (AGENTS.md section 11).
- **Owner / task:** T-101A, and every task definition thereafter
- **Result:** *open* — clock starts 2026-09-05.
- **Decision:** *open*

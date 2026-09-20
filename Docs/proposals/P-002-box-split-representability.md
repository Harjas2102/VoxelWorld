→ No action. For your reading only.

# P-002 — Exact box partitions in the existing operation format

**Status:** Accepted by Codex under explicit Director delegation, 2026-09-17:
"Make the best decision for P-002 yourself, and incorporate the proposed correction and complete T-101B."
The recommendation below is the ruling; the Director's instruction supersedes the earlier
request to route this decision to Claude. **Risk:** R3 amendment to the
adopted terrain specification. **Author:** Codex, 2026-09-17. **Base:** `e94767a` (CP-013).
Implementation is authorized by that instruction. No independent review is claimed.
This is a technical correction under D-023, not a new player feature or a wire migration.

## Blocker

ARCHITECTURE §4.10.2 (line 1064) defines a box as `[C-E, C+E)` with integer centre and
positive integer extents. Every representable side length is therefore even.
§4.11.7 rule 1 (line 1324) requires recursive halving along the longest axis, while
requiring disjoint children whose union is exactly the parent. These requirements do
not determine a legal split when equal halves have odd lengths.

Concrete default-cap case: `CentreVox=(0,0,0)`, `ExtentVox=(21,21,21)` has side lengths
42 and volume **74,088**, above `MaxVoxelsPerOp=65,536`. Every axis is longest. Equal
halves along any of them have one side of length 21, requiring extent 10.5 and
non-integer centres. The permanent 58-byte `FTerrainOp` cannot encode either child.

Changing rounding silently would choose an unspecified partition rule. Rounding both
halves independently could leave a gap or overlap. Refusing every such box would also
contradict the specified exact splitting behavior. AGENTS §10 requires escalation.

## Recommendation

Amend rule 1 to require **the most balanced representable partition**, preserving the
existing wire format and the exact requested voxel set:

- Choose the longest axis with length at least four; ties use X, then Y, then Z.
- Cut at the nearest interior position making both child lengths even. If two cuts
  are equally near the midpoint, use the lower coordinate.
- Recurse in lower-child, upper-child order until every child's volume is at most the cap.
- Reject a split request before allocation/reservation if the cap is below eight:
  the smallest legal box is 2 × 2 × 2. Bound the number of children by admission capacity;
  constructing an unbounded subdivision tree must not precede a queue-cap check.

For the example, cut X at -1. The two child operations have:

| Child | Centre | Extent | Voxel count |
|---|---|---|---|
| Lower | (-11, 0, 0) | (10, 21, 21) | 35,280 |
| Upper | (10, 0, 0) | (11, 21, 21) | 38,808 |

Their X intervals are `[-21,-1)` and `[-1,21)`. They do not overlap, leave no gap,
and exactly preserve the original 74,088 voxels. The wire layout, operation meaning,
transaction accounting, and requested excavation shape remain unchanged.

The alternative of adding minimum/maximum corners or fractional centres would alter
the permanent format and entail migration. It is unnecessary for exact partitions.

## Required review and evidence

The independent reviewer must check representability, exact union/disjointness,
termination, tie-breaking, negative coordinates, integer overflow, and admission
capacity before accepting the recommendation. No existing numbered decision is changed
by this proposal alone.

After the ruling, `TerrainCore.Split.Equivalence` must compare whole versus split
region hashes for Remove, Add and Paint on the reference backend. Include the 42³
counterexample, negative coordinates, chunk-aligned and unaligned cuts, recursive
splits, minimum-size boxes and caps below eight. Keep the codec and golden fixtures
unchanged. This proposal does not settle queue/transaction scheduling or implement
the still-deferred persistence, yield or join-in-progress protocols.

## Implementation and evidence

Implemented in `TerrainOpGeometry.cpp` and exercised by `TerrainCore.Split.Equivalence`:
42^3 counterexample, representable balanced children, exact nonoverlap/union, recursive
splits, negative/chunk-aligned coordinates, cap below eight, bounded parts and overflow.
Whole-versus-split Remove/Add/Paint hashes agree on the reference backend. Codec and
existing reference golden values remain unchanged. `TerrainCore.Admission.Contract`
checks reservation limits, contiguous child sequences across pumps and source fairness.
HANDOFF carries current build/network evidence and later-gate limitations. Independent
cross-vendor review is not claimed; the Director explicitly authorized incorporation.

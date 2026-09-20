→ No action. For your reading only.

# P-003 revision 3 independent review — Claude

**Reviewed:** `Docs/proposals/P-003-persistence-commit-and-recovery.md`, working tree, revision 3.
**Reviewed proposal SHA256: `acdeca2fd4f176f1ace53b244072d2809f495ef136bced38e4ed26cb75e31981`** (as supplied; read-only tooling, so recorded not recomputed).
**Read:** AGENTS.md; revision 3; `Docs/reviews/P-003-review-claude-r2.md`. Targeted source only where a counterexample turned on it: `VPLegacyBackend.cpp:381-429`, `TerrainChunk.h:59`, `TerrainServiceReplication.cpp:88,134,148-154,189`, `TerrainCore.Private.Tests` footprint assertions. The R2 source audit is not repeated.
**Method:** read-only (Read/Grep). No command, no edit, no test, no agent. Codex authored revisions 1–3; I did not author the proposal. Scope: closure of R2-B1…R2-B6 and defects introduced by their fixes. Exact byte layout, OS-primitive proof and runtime numbers are the next packet's, per assignment.

---

## Verdict

**Adoptable at the architecture level. All six R2 blockers are closed, and I found no structural contradiction introduced by the fixes.** No blocker remains; four conditions below are text-level and should be folded in when the amendment lands, not argued first.

This is adoption of §§1–7 as the architectural resolution of DEF-1/2/9 **only**. It is explicitly **not** permission to implement: the exact-format/storage packet (increment 1) must be written and independently reviewed before codecs, and the performance/fidelity gates (capture-fence delay, sustained commits/s vs 96 ops/s, boot time vs save size, peak RSS, bulk `ReadRegion`, material fidelity) must pass before service integration or any DEF closure. §8's own increment ordering already says this; adoption does not shorten it.

---

## Per-blocker disposition

**R2-B1 — boot validation undefined. Closed.** §4 now rules eagerly and verifying, prices it honestly (O(index + unique payload bytes), ~131 GB for 1M dense chunks), and justifies it as matching a restore model that already materializes every edited chunk before login. I asked for the *opposite* reading in R2; the ambiguity, not the direction, was the blocker, and this resolves it. It is also the more self-consistent choice: the exemption "publication must not require rereading every shared cold object" is only sound because boot populated a fully verified object catalog, and §4 now says exactly that. Refusing to bolt on hydration/quarantine as an unreviewed subsystem is correct, and "a production boot-time failure at the intended scale requires revisiting hydration before release" keeps the risk named rather than hidden.

**R2-B2 — pause scaled with the dirty set. Closed, mechanism named.** Copy-before-write at G with a per-transaction preflight replaces the global multi-frame pause. The induction holds as written: every post-G mutation of a frozen key is fenced and forces that key's G copy first, so a chunk copied late in wall time still carries G data, and there is still one global cut. I checked the load-bearing implementability question — whether the fenced key set is knowable *before* apply — and it is: `TerrainOpBounds` → `TerrainChunkKeysForBox` computes the geometric footprint pre-apply in both the backend (`VPLegacyBackend.cpp:386-392`) and admission (`TerrainServiceReplication.cpp:88`), and `:152` already asserts `AffectedChunks ⊆ Keys`. So the preflight set is a sound superset of what the op can change; no mutation can escape the fence. All server mutation reaches the backend through the queue callback (`:134`), so there is no bypass path.

**R2-B3 — dirty keys unpinned vs `ReadRegion` refusing non-resident chunks. Closed.** Service-owned residency pin before first mutation, held until successful capture, with the memory consequence stated rather than buried: up to 1 GiB across two disjoint full banks, called a prototype reservation and not a claim about plugin RSS. "Loss of a player's interest cannot evict authoritative dirty data" is the exact invariant the counterexample needed. The "no false/nonresident Empty published as pristine; only comparison against the exact base chooses Empty" rule closes the silent-data-loss variant.

**R2-B4 — throughput ceiling. Closed.** N=32 is now a named parameter with the arithmetic for rejecting N=1 (≈30/s at 30 Hz vs the 96 ops/s design point) written down, ≤16 records per SQLite transaction, and W lagging H by at most N. That was the ask: choosing the window deliberately rather than silently.

**R2-B5 — `W < G` disagreement. Closed, and strengthened past what I asked for.** §3/§5 now require `max(G of structurally valid retained roots) ≤ W ≤ H` **in this epoch**, including when falling back to an older payload set. Using *structural* validity as the floor is the right refinement: a newer root whose payloads fail eager verification still pins W, so an hour-old `entities.sqlite` from the same world/epoch cannot enter through older-root fallback. The epoch qualifier is necessary and present — after a coherent restore, output roots at B with W=H=B pass. The G=140/W=100 negative fixture survives.

**R2-B6 — journal head anchor and namespace fallback. Closed.** Dual pre-created anchor slots separate from the roots, published-and-flushed *before* the first record of a new segment, highest valid anchor wins, missing named segment is corruption rather than "no more ops", no silent head regression, and preallocated containers with `(container,offset,length)` IDs named as the defined fallback. I traced the torn-anchor case: anchor-before-records ordering means a torn rotation leaves a nonempty unanchored segment, which the text classifies as a protocol violation — fail-closed with the data preserved, not lost. Acceptable; see condition 4.

---

## Defects introduced by the fixes — checked, none structural

Cut-start drain to `G=W=H` composes with the N=32 window (bounded by ≤2 SQLite batches, not by history). Bank/publication interlock does not deadlock: capture and publication never depend on execution, so the "stop execution until capacity is freed" state always has a way out. Retention keyed to *the older of the two valid cuts* is exactly what makes payload-corrupt-newer-root fallback replayable, and "if either root fails, disable reclamation" covers the single-valid-root case. Recovery against a fresh backend, GC's no-resurrection-by-content-hash rule, nonblocking lease with `StoreBusy`, token *digest* rather than live token, and the `sync` pin definition all landed. The X‖Y‖Z key ordering is now a recorded ruling with a reason, which is all R2 asked.

---

## Conditions to record with the amendment (none blocking)

1. **State the capture-throughput inequality.** The per-frame constant (8 chunks / 1 MiB / 2 ms) is now a *throughput* parameter, not only a latency one: 240 chunks/s at 30 Hz against a worst-case unique-dirty rate near 96 ops/s × 8 chunks. Overlap plausibly closes that gap, but if sustained unique-dirty rate exceeds copy rate, backpressure becomes steady state. Add one sentence: "Capture copy throughput must meet or exceed the sustained unique-dirty-key rate at the §7.1 load; if it does not, the constant or the mechanism changes with evidence." I considered this for blocker status and rejected it — unlike R2-B2, raising the constant now works, and the escape hatch is already stated.
2. **Deferred transactions must keep their turn.** "Let other sources' eligible transactions run under round-robin fairness" needs: a transaction deferred for a pending fence retains its queue position and is not repeatedly demoted by sources that stay eligible. This touches the fairness rules reconciled at `9a8dd7b`; leaving it implicit invites a starvation implementation.
3. **Say "read/write footprint" (or "affected key set").** §4's fence is written against "read footprints"; the property required is that every key the transaction may *change* is copied. These coincide on today's read-modify-write ops, which is why it is wording, not a defect.
4. **Name the offline adoption path for a nonempty unanchored segment** whose header predecessor identity matches the anchored segment's seal. Today a single torn 4 KiB anchor write blocks boot on a world whose data is entirely intact. Fail-closed is right; a documented operator path costs nothing.
5. Minor: add per-checkpoint object count to the packet's first-class numbers, and add cut-start drain time to the measured list in §2.

---

## What decides the next step

The architecture is coherent, its costs are stated in the places they actually fall, and the three remaining risks (capture throughput, game-thread journal fsync inside 8 ms, boot time at scale) are named with evidence gates and escape hatches rather than assumed away. Per D-023/D-032 the Director's ruling adopts §§1–7 as the resolution of DEF-1/2/9's architecture; DEF-1/2/9 stay **open** until their named evidence exists, and increment 1 (exact format/storage specification) remains a prerequisite to any runtime code.

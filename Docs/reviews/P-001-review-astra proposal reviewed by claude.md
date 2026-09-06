# P-001-review-astra.md — Independent review of `P-001-terrain-astra.md`

**Reviewer role:** Independent reviewer (AGENTS.md §2). Writer is not reviewer for R3 work.
**Reviewed document:** `Docs/proposals/P-001-terrain-astra.md`, 145 lines, status "Incomplete proposal; stopped at the first unresolved architectural fork."
**Checkpoint:** CP-004 · 2026-09-06 · Risk class **R3**
**Evidence used:** `P-001-CONTEXT.md` (packet, commit `b7d0375`) and the proposal. The other vendor's proposal was not consulted.
**Scope:** Review only. No implementation, no code, no changes to any numbered decision.

---

## 0. Verdict

**Cannot be adopted as ARCHITECTURE.md v1 in its current form**, and the reason is not that it is short — it is that the dependency it claims justifies its shortness is not established.

The proposal delivers 2 of the 12 required elements (plugin-specific/game-owned boundary; a test plan), states a genuine fork, recommends a side of it, and defers the remaining ten pending a ruling. The evidence reading is unusually careful and I found **no factual misreading of the packet** — that is real and it is rare. The problem is structural: **most of the ten withheld deliverables do not depend on the fork that was used to withhold them.** AGENTS.md §10 licenses stopping on *the underdetermined choice*. It does not license suspending the fork-independent remainder of an R3 deliverable, and reading it that way converts the project's own anti-stall rule (R-009) into a stall mechanism.

Separately, one hazard class in the proposal is not merely deferred but absent, and it will bite whichever option is ruled: **the UE lifetime of an in-flight authoritative edit** (B-1 below).

Recommended disposition is in §6.

---

## 1. Blockers

Blockers are items that, left as they are, would make the resulting architecture wrong or the ruling unsound. Each is stated as a defect, not as a design.

### B-1 — In-flight edit completion is anchored to a per-player UObject lifetime; nothing in the proposal notices

`UVoxelSphereTools::*Async` (packet §10.9) takes `UObject* WorldContextObject` and `FLatentActionInfo`. UE latent actions are owned by that UObject's lifetime. If the authoritative edit is issued from anything player-scoped — a `PlayerController`, a character, a tool component — and the player disconnects, times out, or is destroyed between issue and completion, the latent action is torn down with it. The plugin's data mutation may already have happened; the completion callback that would have advanced the chunk revision, journaled the operation, and settled inventory does not fire.

That produces exactly the failure the proposal's own §5 table says must not happen ("Process failure occurs between terrain durability and inventory credit"), but by a mechanism the table does not describe and does not cover: not a crash, not a retry, just a disconnect during a normal 200 uu dig. On a friends server with players joining and dropping constantly (the stated JIP premise, R-002), this is a routine event, not an edge case.

The sync overloads return directly and avoid it, but `bMultiThreaded` defaults to `true` in the sync form (packet §10.9) — so the naive escape is a blocking multi-threaded mutation on the server tick, which is B-4.

**What the proposal must state (not solve here):** the authoritative edit path's *owning object* and its lifetime relative to a player session, and what a completion means if the issuer is gone. This is fork-independent — it is true under Option A and Option B alike, because under B the adapter still applies the resulting field to the plugin through the same tools.

### B-2 — The claimed dependency between the fork and the ten withheld deliverables is not established

§8 asserts that concrete lifecycle, wire/save structures, transaction algorithm and JIP sequence "depend on this ruling." Check that claim item by item against the required twelve:

| Deliverable | Actually gated by Option A vs B? |
|---|---|
| 1. Class/subsystem boundaries | Partially — the *field owner* class differs; the service, journal, replication and settlement boundaries do not |
| 3. Server/client sequence | No — request → validate → sequence → commit → replicate is identical |
| 4. Persistence schema | No — the schema is game-owned under **both** options (D-012 requires it; the proposal's own §7 assigns "durable schemas, journal lifecycle, compaction, migration" to game-owned in *either* case) |
| 5. Chunk revision model | No — ARCHITECTURE v0 §2 already fixes global op IDs + per-chunk revisions |
| 6. Concurrency | No — server-side serialization policy is above the field |
| 7. JIP | Partially — the *content* of a snapshot differs; the protocol does not |
| 8. Failure recovery | No — the commit ordering problem is identical |
| 10. Performance risks | No — see B-4; several are knowable now |
| 11. Plugin-specific boundary | Delivered |
| 12. Backend replacement | No — the swap procedure is a property of the adapter contract, which the proposal declines to write |

So the fork gates roughly two-and-a-half of ten. The rest was withheld without a stated reason that survives inspection. The proposal is entitled to stop on the fork; it is not entitled to treat the fork as a global blocker without demonstrating the dependency, and it does not demonstrate it.

**Consequence for the Director:** if this is ruled and returned for revision as-is, the next revision faces the same pattern at the next fork (transaction protocol, service lifetime, material config, commit ordering — at least four more, all named or implied in the document itself). At one fork per review cycle under the 7-day rule, D-017 does not land this quarter. **R-009 is the project's largest observed failure mode and this process shape feeds it.**

### B-3 — `MaterialConfig` is a persistence-schema-affecting decision and it is not in the proposal

The proposal assigns "material identity, ore grade, measured-removal interpretation and yield" to game-owned (§7) and correctly observes that `FModifiedVoxelValue` carries no material (§2). It never reaches the consequence: the world is currently on `EVoxelMaterialConfig::RGB` (packet §10.4, STATE.md), which encodes material as colour channels, not as a discrete index. Discrete strata and ore grades want `SingleIndex` or `MultiIndex` (packet §10.15).

That is not a rendering preference. It changes:

- what a "material" *is* in every chunk snapshot the game writes — i.e. the persistence schema, which is the deliverable D-012 cares most about;
- the material channel layout, which the plugin's own save version history tracks (`StoreMaterialChannelsIndividuallyAndRemoveFoliage`, packet §10.12) and which `VOXEL_MATERIAL_ENABLE_*` in `VoxelDefinitions.h` governs at compile time (§10.2);
- what the T-108 C++ generator must emit;
- whether `RGBHardness` / `MaterialsHardness` (§10.4) is a usable tool-efficiency input or dead weight.

A terrain architecture whose central mechanic is yield-from-removed-material cannot leave the representation of material unstated. Changing it later is a save migration on the pillar format — precisely what D-012 exists to prevent becoming normal.

### B-4 — Server-side terrain cost is treated as entirely unknown when parts of it are knowable from the packet today

§6 experiment 7 defers all budgets to measurement, and §5's last row defers load and platform. Deferring *budgets* is correct. Deferring the *risk inventory* is not — "performance risks" is deliverable #10 and several are legible from the packet without running anything:

- **Invoker-driven server cost.** R-010 makes a `VoxelInvokerComponent` per character mandatory in any net mode (findings §2d). At 16–32 dispersed players that is 16–32 invokers driving octree LOD, collision cooking and navmesh work, against `NumberOfThreads` default `2` and `MeshUpdatesBudget` default `1000` (§10.4). Dispersed players are the worst case and are the normal case on a build-anywhere server. The knobs — `bRenderWorld`, `bComputeVisibleChunksCollisions`, `VisibleChunksCollisionsMaxLOD`, `bDoNotMergeCollisionsAndNavmesh`, `bStaticWorld` — are all `AVoxelWorld` properties, i.e. adapter-owned, i.e. in scope for this document.
- **Edit cost scales with radius³, on the server, on the game thread.** The sync overload defaults `bMultiThreaded=true`; the async overload defaults it to `false` (§10.9). Stage 4 landscape-scale excavation (D-016, and a named constraint of the challenge) means brush volumes orders of magnitude above the 200 uu hand tool. The proposal mentions powered excavation once, in §7, as "a subsequent design question." It is a stated constraint of the task and it is the axis on which the edit path's cost model fails.
- **Restore primitive cost.** See M-1 — the only bounded write primitives are per-voxel `UVoxelDataTools::SetValue`/`SetMaterial` or the C++ `FVoxelData::Set`/`ParallelSet` lambda via `GetData()`. A 16³ leaf is 4096 samples. That bears directly on JIP and startup and belongs in experiment 1's design, not just its measurements.

---

## 2. Major findings

### M-1 — The A/B framing omits the two options that the packet actually makes cheapest

The fork is presented as: adapter holds the live field (A) vs. the game holds a second canonical field (B). Two alternatives are not considered, and both change the recommendation's cost basis:

- **Journal-as-truth.** The game owns base identity + the append-only operation journal as the canonical durable record; the adapter's field is a *derived cache*, reconstructible by replay. This is neither A nor B — no second resident field, and the durable format is already what D-012 mandates. Its viability turns entirely on replay determinism, which is exactly what experiment 2 tests. The proposal tests the precondition for an option it never lists.
- **Split ownership.** The game already owns the material field *analytically*, because T-108 forces the strata/ore generator to be game-written C++ (R-008, findings §2b). So "game-owned material semantics" is nearly free — yield can be evaluated against the game's own analytic strata plus a swept volume, without a second voxel store — while density stays in the adapter. Option B's headline cost ("requires an additional terrain representation") is overstated for the half of the field that matters economically.

Presenting a two-way fork when the packet supports at least four is a real defect in an R3 options document, and it is the kind of defect that survives into the ruling.

### M-2 — Op-replay on clients is named as a failure mode, but the requirement conflict it implies is not escalated

§5 states that clients "cannot replay a full native brush against missing or stale neighboring state and assume convergence." Correct. But AGENTS.md §4 rules that **terrain edits replicate as operations, never meshes**, and AGENTS.md §10 lists "a conflict between two stated requirements" as an escalation trigger. If native brush replay is not sound on a client with partial neighbour state, then the accepted rule and the observed backend behaviour are in tension, and the fallback — shipping bounded per-voxel value/material deltas — is neither a mesh nor an operation.

The proposal escalated the fork it chose to escalate and left this one inside a table. Whether delta shipping satisfies §4 is a Director call, and it should have been asked. It also decides the wire format, which is deliverable #2.

### M-3 — R-010 is measured but not answered

R-010 is one of three risks the packet flags as reshaping the gate, and `VoxelProceduralMeshComponent` being `NOT Supported` by `FNetGUIDCache` is an *observed* defect, not a hypothesis. Experiment 4 measures it competently. But the document contains no architectural response, and no fork for one — even though its own §7 assigns "backend rendering/collision integration" to the adapter, which is where any response lives.

The reviewable gap: the proposal does not state what changes in the design if the movement base cannot be made resolvable. Standing on terrain while someone else digs it is the game's defining moment (VISION success definition), and "we will measure it" is not an architecture for it.

### M-4 — The bulk load path at world creation is not identified, and it constrains Option A's own experiment

`FVoxelWorldCreateInfo` (packet §10.4) offers `bOverrideSave` (whole-world `FVoxelUncompressedWorldSave`) and `bOverrideData` (an existing `FVoxelData`), and they are **mutually exclusive** — the plugin errors if both are set. Experiment 1 says "restore into a fresh base" without naming which path it uses. That matters: `bOverrideSave` is the whole-world blob D-012 rejects, and `bOverrideData` requires constructing an `FVoxelData` outside the actor, which is a C++-only, non-UFUNCTION path through `GetData()`. The experiment as written could pass through a mechanism that fails the option it is meant to validate.

### M-5 — Chunk alignment is unconstrained

The proposal correctly separates game logical chunk identity from plugin chunk sizes (§7). It never notes the constants: `DATA_CHUNK_SIZE = 16`, `RENDER_CHUNK_SIZE = 32` (§10.2), and `FVoxelChunkSave` is keyed per data-octree leaf (§10.12). Any bounded export/import is realistically leaf-aligned; a logical chunk that is not a multiple of 16 (probably 32) voxels buys partial-leaf reads on every snapshot and every JIP transfer. Experiment 1 tests a "region boundary" without specifying alignment, so it can pass on an aligned case and hide the cost of the unaligned one.

Credit where due: testing **negative coordinates** is exactly right, given the `MakeMultipleOfSmaller`/`Bigger` rounding family in `FVoxelIntBox`.

### M-6 — Backend-version identity in the snapshot header is asserted as a requirement without contents

§5 requires that "a backend upgrade changes brush results or density conversion → version mismatch is detected." Agreed, but the row is unactionable as written. The packet makes the set enumerable: `VoxelSize`, `EVoxelValueConfigFlag` / `GVoxelValueConfigFlag` (which flips if anyone ever uncomments `EIGHT_BITS_VOXEL_VALUE` in `VoxelUserDefinitions.h`), `MaterialConfig`, the `VOXEL_MATERIAL_ENABLE_*` set, `DATA_CHUNK_SIZE`, plugin version/commit, and the game's own generator version. A migration requirement that does not say what it keys on is not yet a requirement.

Note the sharp edge the proposal did catch and should be credited for: those macros are **source-level edits to a gitignored plugin** (STATE.md), so the value that a save was written under is not recoverable from the repo.

### M-7 — Chunk relevancy is a named constraint of the challenge and is absent

Relevancy appears once, inside a §5 failure row. It is a required constraint (packet §1.3), it is network-layer work, and it is fork-independent — Option A vs B does not change who subscribes to which chunks. No subscription model, no bound on retained history per client, no statement of what happens when a chunk leaves and re-enters relevance.

---

## 3. Polish

- **P-1.** §2 header row "Free Legacy v432 / e9648b302 rendered and edited a smooth tunnel in standalone" — accurate, but the run was on `VoxelFlatGenerator` plus sculpted spheres, so it is also evidence that the *test world itself* is not reproducible from seed. The proposal states this at the end of §2; it belongs in the evidence table, where a reader looking for the base-world problem will find it.
- **P-2.** §6 experiment 3 says "counting changed samples or summing raw density differences is not an accepted volume oracle" — correct and well argued — but does not name an independent reference. Without naming one, "compare against an independent reference integration" is unexecutable as a packet.
- **P-3.** §7's last paragraph handles future powered excavation in one sentence. Given that D-016 makes Stage 4 the identity of the progression, and given B-4, one sentence under-weights it.
- **P-4.** The document does not state which of R-001…R-010 each experiment is intended to move, even though it correctly notes none are closed by a proposal. A mapping would make the T-101B sub-step wiring obvious.
- **P-5.** Service lifetime is deferred (§4 last paragraph). That deferral is explicitly sanctioned — AGENTS.md §10 lists "Subsystem vs. ActorComponent when unspecified" as an escalation trigger — so this is correct behaviour, but it is another fork that should have been *enumerated* alongside the first rather than mentioned in passing. The GC/lifetime hazards attached to it (a subsystem holding a raw `AVoxelWorld*`, or a `TVoxelSharedRef<FVoxelData>` outliving `EndPlay`/`DestroyWorld` — which already has a known shutdown `ensure`) are unmentioned anywhere.

---

## 4. What the proposal gets right

Stated because a review that only attacks is not usable input for D-018.

- **The evidence table is correct.** I checked every row against the packet: `Clone()`, save equality by GUID, no region argument on save/load, `FModifiedVoxelValue` carrying no material, the §10.12 correction of the findings' multiplayer reading, the append behaviour of the C++ tool overload. No misreading found. Several rows are the kind of thing a confident answer glides over.
- **It refuses the two most tempting shortcuts:** treating changed-sample count as volume, and treating a callable header as a capability. Both are real traps in this packet.
- **It correctly separates "a passing experiment closes no risk"** from RISKS.md's result-plus-decision requirement.
- **It catches that ARCHITECTURE v0 §2 already fixes both ordering scopes** (global op IDs, per-chunk revisions) and that only the transaction protocol is open — which is a more precise reading than AGENTS.md §10's own escalation list.
- **It flags that the T-101A hill is not preserved by the map package**, and therefore that the deterministic base must specify its authored-stamp inputs rather than inherit them.
- **It changed nothing.** No numbered decision touched, no self-certification, explicit non-adoption of Free Legacy.

---

## 5. Questions for the Director

These are the rulings this document should have asked for, and did not:

1. **Does shipping bounded per-voxel value/material deltas satisfy AGENTS.md §4 ("operations, never meshes")** when native brush replay cannot be shown to converge on a client with partial neighbour state? (M-2. This decides the wire format.)
2. **What is `MaterialConfig`?** RGB (current) or SingleIndex/MultiIndex. (B-3. This decides the snapshot schema and the T-108 generator's output.)
3. **Is the terrain authority permitted to be player-scoped at any point in the edit path?** (B-1.)
4. **Are experiments 1–3 authorized as bounded backend spikes with no architectural commitment,** independent of the D-017 ruling? They touch only the plugin and need no adapter. (See §6.)

---

## 6. Recommended disposition

Not a ruling, and not a design.

1. **Do not adopt as ARCHITECTURE v1.** Two of twelve deliverables is not a v1, and B-2 shows the deferral of the other ten is not justified by the fork it cites.
2. **Rule the fork anyway, and rule it as narrow.** A ruling on field ownership costs one line and unblocks the parts of the revision that genuinely depend on it. Ruling it does not endorse the process shape.
3. **Reject the one-fork-per-cycle pattern explicitly.** Require the revision to enumerate *every* fork it can see — transaction protocol, service lifetime, `MaterialConfig`, commit ordering between chunk files and SQLite, relevancy subscription — with options and recommendations, in one document, so they can be ruled in one pass. AGENTS.md §10 requires stopping on an underdetermined choice; it does not require stopping the document.
4. **Authorize experiments 1–3 now, as R1/R2 spikes against the plugin only.** They require no adapter, no architecture, and no D-017. They are also the experiments whose results most change the fork's answer — including for the journal-as-truth option the proposal never lists (M-1). This is the move that keeps R-009 satisfied while the R3 process runs.
5. **Carry B-1, B-3, M-2, M-3 and M-6 into ARCHITECTURE v1 as required content** regardless of which proposal wins the benchmark.

---

## 7. Scoring notes (GPT §39 criteria, packet §1.1 step 5)

Offered as input to D-018, not as a score.

| Criterion | Assessment |
|---|---|
| Correct reading of vision | Strong. Identity, D-011 ownership and D-013's early-gate consequence all read correctly. |
| Real risks found | Mixed. The risks it names are real and precisely stated; it misses B-1, B-3, B-4 and M-7 entirely. |
| Avoids unnecessary complexity | Strong on content, **weak on process** — the deferral pattern imports schedule complexity the project can least afford (R-009). |
| UE correctness | Under-demonstrated. Latent-action lifetime, GC/ownership, server-side collision cost and the `bOverrideData`/`bOverrideSave` exclusivity are all untouched. This is the weakest axis. |
| Persistence / networking correctness | Directionally right, materially incomplete: no schema, no revision mechanics, no relevancy, no commit ordering. |
| Testability | **Strongest section by a distance.** Experiments 1–3 and 5–6 are well-formed, falsifiable, and specify stop conditions and non-oracles. |
| Replaceability | Asserted and well-bounded in §7; the actual swap procedure (deliverable 12) is absent. |
| "Unknown — prototype this" | Used correctly and unusually well — genuinely earned, and the document's best quality. |
| Survives critique | The evidence base survives. The completeness claim and the fork-dependency argument do not (B-2). |

---

*Review complete. No implementation performed, no repository file modified, no numbered decision changed. D-017 and D-018 remain open.*

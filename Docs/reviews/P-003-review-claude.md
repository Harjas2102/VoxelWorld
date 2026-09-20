→ No action. For your reading only.

# P-003 independent review — Claude (independent reviewer)

**Scope reviewed:** `Docs/proposals/P-003-persistence-commit-and-recovery.md` @ `b1b93e4`, against ARCHITECTURE §4.2/§4.4/§4.5.1/§4.7/§4.9/§4.10.4/§4.11/§14 (DEF-1/2/9), DECISIONS D-012/D-023/D-024(K3,K4,K5)/D-032, VISION pillar 1. Read-only; no files written, no commands run, no tests claimed. I am not the author (Codex authored), satisfying DUAL_AGENT_SETUP §4.2.

**Verdict: not ready for adoption. Blocked on B1–B6.** The route forward is a revised *specification* packet, not the step-1 format/storage implementation packet in §6. Several parts are genuinely sound and I say so at the end.

One author claim I re-verified: `WindowsPlatformFile.cpp:933` does implement `Flush` as `FlushFileBuffers(FileHandle)`. Correct as cited (and it ignores `bFullFlush`). This supports content flush only, which the proposal already concedes.

---

## Blockers

**B1 — Retried requests are deduplicated in RAM only, so restart re-mines and re-credits.**
§4.11.3 keeps `(SourceId, RequestId)` receipts in a 64-entry ring and states reliable RPCs *are* re-sent across reconnects. P-003's durable identity is `(WorldId, OpSeq)` — assigned at commit, unrelated to `RequestId`. The 58-byte op (§4.2) carries `SourceId` but **no `RequestId`**, and P-003's journal envelope (§5) does not add one.
*Counterexample:* player sends `RequestId=91`; server commits `OpSeq=5000`, flushes, settles, then dies before the receipt reaches the client. Client reconnects, the reliable RPC replays `RequestId=91`. The ring is empty; the journal cannot answer "was 91 committed?"; `OpSeq=5001` is assigned and the ore is credited twice. P-003's ledger makes *recovery* exactly-once and then says "an absent acknowledgement means unknown" — it never closes the re-admission path. It also does not address whether `SourceId`, derived from the connection (§4.11.1), is even stable across reconnect. Needs: `RequestId` (and the stable owner id) in the durable envelope with a lookup, **or** a server-incarnation token in receipts that makes cross-restart retries explicitly refusable. Either is a specification decision, not an implementation detail.

**B2 — Recovery has two cursors and P-003 states only one, inviting double terrain apply.**
§1 says "process journal records after the entity watermark in sequence." §2 says "replay every committed op with `OpSeq > G`." These are different floors and P-003 never says they are separate passes.
*Counterexample:* entity watermark W=100, checkpoint G=140 (legal: §1 step 4 advances the terrain committed sequence before the entity commit, and the storage-fault paragraph re-enables capture after "journal commit and metadata advancement", *not* after settlement). Records 101–140 must be **settled but not replayed**; they are already baked into the manifest payloads. The obvious single-pass implementation applies them twice and mines the rock twice. State the rule explicitly: terrain cursor = `G`, settlement cursor = `W`, independent, with `W ≤ terrain HWM` as the only cross-check.

**B3 — Journal retention is pinned to the *live* entity watermark, so entity-store restore is unrecoverable.**
§3's deletion test includes "processed durably by the entity settlement consumer" — the current watermark. The entity store has no generation, no second slot, and no fallback, while terrain has two of each. The asymmetry is fatal in one direction.
*Counterexample:* W=2500, older checkpoint covers 2000; segment 1000–2000 is deleted as permitted. `entities.sqlite` is then corrupted and restored from a nightly backup at W=800. Terrain is intact at 2500. Settlements 1000–2500 can never be reconstructed: the ops are baked into payloads and the records are gone. "Retain settlement identities" retains identities, not amounts. P-003's §1 integrity rule only fails closed on `W > terrain HWM` — the *opposite* direction. Needs: an entity-store epoch/floor that pins journal retention, or an explicit ruling that entity-store rollback requires whole-world restore to the matched generation — which then contradicts §2's "select the highest valid compatible generation," since "compatible" is nowhere defined across stores.

**B4 — Checkpoint cost is O(all edited chunks ever), not O(changed), and this is a pillar-1 design choice, not a measurement.**
A complete manifest enumerates "every nonzero-history chunk." On a server whose identity is "the world permanently records what the players did to it," that set only grows. Every checkpoint rewrites the whole manifest. Compounding it: §4 forbids sparse/pristine compaction on VPLegacy until material transfer and baseline access exist, so captured payloads are **dense** — 32³ × (int16+uint16) = 131,072 B per chunk (K2/§4.2). One 8-chunk dig ⇒ ~1 MB of checkpoint payload. P-003 files both under "must be measured." Adopting an unbounded per-checkpoint scan against D-012's explicitly incremental model needs either a bound (chained/delta manifests with a capped chain length) or a stated accepted limit with a world-size ceiling.

**B5 — §5 collides with the already-written §4.7 schema and never says which wins.**
§4.7 fixes a journal record (`TJOP`, schemaVersion, 58-byte op, `serverUtcMillis`, `{matId u16, microLitres i64}` yield list, `{key, newRev}` list, crc32) and a `.chunk` header, and `world.json` already declares `"schema": 1`. P-003 adds "a versioned economic-intent envelope and before/after revisions," replaces physical µL yield with "stable entity/inventory IDs and exact signed deltas," demotes `world.json`, and calls the result "initial persistence schema 1." Two different things now claim to be schema 1, and no field is stated as kept, widened, or dropped. Also missing: `WorldId` appears in no record or segment header, yet `(WorldId, OpSeq)` is the settlement primary key — copy or rename a world directory and the keys collide. AGENTS §4 and §10 both make a save-schema change an escalation, so this must be an ARCHITECTURE §4.7 amendment adopted *before* the format packet, not deferred into it.

**B6 — Two durability barriers in front of the client broadcast exceed K5 and threaten K4/§7.1.**
K5 ruled the journal record durable *before inventory is credited*. P-003 §1 step 5 additionally blocks the **terrain broadcast** on a `synchronous=FULL` SQLite commit, on the game thread, per sub-op (§4.11.7: splits commit individually). That is two fsyncs per sub-op inside the 8 ms §7.1 budget with 16–32 players. The stated justification — "transfer, crafting and spending can see only settled inventory state" — is satisfied by gating inventory *reads*, not the terrain broadcast; the ledger plus retained journal already make recovery exact. Either justify the extra barrier against §7.1 or move the broadcast to after journal durability.

---

## Ambiguities that must be resolved before an implementation packet (AGENTS §10)

- **"Captures synchronously"** — copy-into-owned-buffers only (game-thread-bound per §4.5.1), or also write+flush? §2 forbids a terrain thread in one sentence and permits I/O workers in another.
- **Chunks pristine at G but touched by a post-G op** are absent from the manifest; §2 never says replay must materialize them from the manifest's base descriptor rather than the running generator. §4's "mismatched pieces cannot be assembled silently" implies a gate but never defines "compatible" for §2's root selection.
- **Settlement that cannot be applied** — destination inventory full, or entity/inventory id deleted. P-003 says "apply all credits/debits"; capacity overflow is explicitly DEF-6/step-6 and unspecified.

## Polish

- Two root slots must be specified as **pre-created fixed-size files overwritten in place**; if a slot is published by create+rename, the namespace gap P-003 concedes could leave both slots unreachable at once.
- Order deletion after "new root published **and** both roots validated," as a rule rather than an implication.
- Bound manifest entry count and byte size; §5's caps cover binary framing only.

## Implementation experiments (not blockers)

Checkpoint pause and save growth (B4's measurements), `E-1` occupancy calibration feeding the economic envelope, and power-loss/namespace behaviour on the target filesystem.

## What is right

The complete-manifest argument genuinely does dissolve the multi-chunk replay hazard without touching the permanent 58-byte wire, and the reasoning ("an older reused payload is valid at G because that chunk did not change") is correct. Fail-closed on interior CRC failure and missing segments, truncation permitted only at the physical tail, storage-fault teardown suppressing shutdown compaction, `bIsGeneratorValue` rejected as an equality test, and refusing `.bak` as a substitute for a decoder are all correct and directly answer DEF-1/2/9 language.

**Consequence for §14:** even with B1–B6 closed, §4's gating of sparse/pristine compaction on VPLegacy means the DEF-9 Empty/pristine path cannot produce evidence on the production backend at step 4. DEF-9 cannot be marked Resolved by this packet regardless of the review outcome.

→ No action. For your reading only.

# P-010 — The settlement ledger: digging pays, and a crash can't change the payout

**Status: specification and Architect ruling, 2026-09-21. Implemented as T-131.**
**Author:** Claude Opus. **Risk:** R3 (persistence, settlement, a new engine plugin).
**Base:** `7ce51a2` (T-130).
**Authority:** P-003 §2 steps 4–5, §3's settlement pass, §5; D-012 ("SQLite holds entities");
DEF-1; DEF-6 (economic half); D-023 (technical ruling, logged).

Writer and reviewer are the same agent. §7 is the self-review.

---

## 1. What exists now

A player who digs is credited, per material, with exactly what the ground gave up. The credit
is durable in a SQLite ledger. **No crash can lose a credit or pay one twice.** The whole path is
P-003's, built as written:

1. At commit, before the journal write, the service computes the op's **economic intent** from
   its measured yield (P-009) and the owner, and writes it **into the journal record**
   (`EconomyKind = ExactDeltas`, policy version, deltas).
2. Once the record is durable, the edit is broadcast exactly as before. **The ledger never gates
   terrain** (P-003 §2 step 3).
3. The record's settlement input (OpSeq, the digest of the exact bytes appended, the deltas) goes
   to one serialized worker. Batches of up to 16 records each settle in one SQLite transaction:
   insert the `(OpSeq, digest)` rows, apply the deltas, advance W. It is all or nothing.
4. The service's mirror of balances, and the owner's `ClientInventory` message, change only
   after that transaction commits. A player never sees a promised credit.
5. More than 32 unsettled records stops execution of new edits until the ledger catches up. A
   new checkpoint cut waits until W = H. A failed settlement is an uncertain storage fault and
   closes admission. The records are journaled, and the next boot settles them.

**Boot**, before anything is admitted:
- A ledger exists: require G ≤ W ≤ H, then settle (W, H] from the journal. That reads what
  was recorded and never recomputes it.
- No ledger, and the journal has never recorded an economy: create one at W = 0 and settle the
  NoEconomy records to W = H. This covers new worlds and every world from before T-131.
- No ledger, but the journal has recorded an economy: **refuse**. Those inventories existed and
  are gone, and a blank ledger would silently erase them (P-003 §5).

## 2. Rulings: the module boundary

- **A new module, `EntityStore`**, is the only module that links SQLite. It registers itself with
  `FTerrainSettlementRegistry` at startup, and the service loads it by name
  (`UTerrainSettings::SettlementModule`). This is the same arrangement as the plugin adapter.
  TerrainCore's `Build.cs` is unchanged: Core, CoreUObject and Engine only.
- **SQLite comes from UE's own `SQLiteCore` plugin (3.47.1), now enabled in the `.uproject`.**
  D-012 already chose SQLite for entities, so this is that decision's implementation, not a new
  vendor. It is recorded as a decision entry at the checkpoint.
- The interface (`ITerrainSettlementLedger`), the journal reader, the policy and the worker live
  in TerrainCore, so settlement logic tests without SQLite, and SQLite could be replaced.

## 3. Rulings: DEF-6's economic half (policy version 1)

- **The ledger holds exact microlitres per material**, in each owner's personal stock (container
  0). With no integer items there is no residue to lose or round. Turning volume into item counts
  is a presentation and tuning decision for when an inventory UI exists.
- **Materials that pay:** Topsoil, Dirt, Stone, Deep Stone, Bedrock, Iron Ore. Air, Unknown and
  **Fill** pay nothing.
- **Placement: players place Fill.** Fill is a new catalog material (id 8). It looks like dirt,
  one 8-bit blue step away, so the id stays exact under P-009 §3. It never yields. That closes
  the mint that T-130's measurement made possible: dig stone and be paid, place it back (which
  kept the old "Stone" material), dig again and be paid again. Placement still costs nothing,
  exactly as today. When building with inventory arrives, which is a game decision, placement
  will debit real materials under a new policy version.
- **Tool 0 pays 100%.** Any other tool id pays nothing; admission already refuses them.
- **Owner.** BLAKE3 of the player's unique net id, captured when the connection registers and
  kept after disconnect, so settlement never depends on a live requester (P-003 §2). No identity
  means no payment and a NoEconomy record. Console and diagnostic edits pay owner 1, the server.
- **Catalog compatibility.** A saved base with a smaller material catalog now opens, because ids
  are append-only. Before this change, adding Fill would have locked out every existing world.

## 4. Rulings: SQLite, and the finding that shaped them

**UE compiles SQLite on its own file layer (`SQLITE_OS_OTHER`), which has no shared memory, so
ordinary WAL is unavailable.** Asked for WAL, SQLite silently stays in DELETE mode. That mode
creates and deletes a `-journal` file on every transaction, which is the file-name churn P-005
removed from the terrain store because a power cut can lose the name. The first boot caught it
only because the ledger reads every pragma back and refuses to run if one didn't take.

Ruling:
- `PRAGMA locking_mode=EXCLUSIVE` **before** `journal_mode=WAL`. SQLite then keeps the WAL index in
  heap memory and needs no shared memory. One connection is all there ever is, since the writer
  lease (T-128) guarantees one server per world.
- `synchronous=FULL`, **read back**, and so are the other two pragmas. Any mismatch refuses boot.
- The WAL file is created when the ledger opens, which is before anything is admitted. The
  service then **syncs the world directory**, as P-005 does for the terrain store's bootstrap
  names. While the world runs, no ledger file is created or removed.
- The engine's sync maps to `FlushFileBuffers` on Windows and `fdatasync` on Linux. On Linux,
  `fdatasync` does flush a file-size change, which is the property a growing WAL needs.

**Why a lost ledger tail is not a lost credit.** The journal is written first and is the recovery
authority. If a power cut drops the ledger's last transactions, W simply comes back lower, and
boot settles those records again from the journal. That holds as long as the ledger is
internally consistent, which is what WAL mode's commit protocol exists to guarantee.

## 5. Evidence

| Check | Result |
|---|---|
| TerrainCore automation | **42/42**. New: `Persistence.Settlement.Ledger`, `.Policy`, `.CommitCrash` |
| `Settlement.Ledger` | settle, W, balances; replaying settled records pays nothing twice; a settled OpSeq with another digest is refused as corruption; a gap is refused; **a failure injected at each of 11 statements of a two-record batch rolled back whole every time** (no row, no balance, no W), then the batch settled; an overflowing credit fails closed; state survives reopen; creating over a ledger and opening another lineage's ledger are both refused |
| `Settlement.CommitCrash` | 24 real measured digs on the reference backend, recorded by the real commit path with the real policy. The journal's settlement inputs are byte-identical to the live ones, digest included. **A crash after K settled records, for every K from 0 to 24, recovers to the exact balances** (186,625 L), and replaying the whole journal a second time changes nothing |
| `Settlement.Policy` | stone and ore pay exactly; fill, air, unknown and placements pay nothing; no owner or an unknown tool means NoEconomy |
| **`Tools/Test-TerrainSettlement.py` (real processes, hard kills)** | 8 iterations of a server digging continuously and being killed with TerminateProcess at a random moment. On every restart, **`Terrain.LedgerAudit` recomputed every balance from the journal alone and matched the ledger, with W = H**. Balances only rose (to 1,358,721.6 L over 121 paid records). Kills during slowed settlement landed in the journaled-but-unpaid gap 5 times, and **the boot pass paid those records exactly once**. Refusals: a deleted ledger is refused with every file unchanged; an old ledger copy (W=23 < G=144) is refused with every file unchanged |
| **Mutation test** | with the boot settlement pass disabled, the kill test **fails** at the first kill that lands in the gap |
| Pre-T-131 world (no ledger, NoEconomy history) | the ledger is created; 10 records settle at boot; audit PASS |
| `MP.Convergence -Rounds 3 -IncludeObserver`, 60 s | PASS in all rounds, with **the ledger audit passing after every round** (H=426, 853, 1338). Owners are distinct per client and stable across travel |
| `-DropOp 20`, `Test-TerrainCheckpoint.py`, `Test-TerrainLease.py`, `Terrain.AdapterChecks` | PASS |
| Both targets | build |

## 6. What this closes, and what it doesn't

- **DEF-1 is closed for terrain settlement.** The commit point, acknowledgement semantics, the
  ordering of terrain versus entity effects, restart reconciliation and disk-error behaviour are
  all built. P-003's named evidence is `CommitCrash` and `SettlementReplay` (both in
  `Settlement.CommitCrash` and the kill test). `RetryFence`, a retransmitted request not paying
  twice, is covered by the queue's request dedup plus the OpSeq key.
- **Not built:** spending, crafting and transfers, the non-terrain entity transactions. P-003's
  "entity-only restore is prohibited" rule matters once they exist. Also not built: backups
  (P-003 §5's backup descriptor), ledger identity GC (explicitly not in the first
  implementation), and an inventory UI.

## 7. Self-review: what could still be wrong

- **Owner identity is only as stable as the online subsystem's id.** On this machine the Null
  subsystem gave each process a distinct id that stayed the same across travel. Whether it
  survives a game restart, or an account on another machine, depends on the eventual login
  (Steam or EOS). Until then, "your inventory" is "this machine's identity".
- **The multiplayer harness pays mostly one client,** because all three dig the same spots in
  the same order. The audit is owner-agnostic, so it proves conservation, not fairness between
  players.
- **The harness paints iron ore on placements in round 1**, which is a deliberate harness-only
  mint. Real players place Fill.
- **Durability rests on SQLite's WAL commit protocol running over UE's file layer**, which is
  not SQLite's own OS layer. The kill test proves process crashes. Power loss is argued (§4), not
  tested, like everything else on this machine (no Hyper-V). On Linux the directory sync after
  opening closes the name window; on Windows it is best effort, as P-005 §6 already records.
- **Not measured: the throughput limit.** 96 ops/s against the 32-record window. Settlement
  latency with FULL sync is unmeasured too. The multiplayer runs never filled the window.
- **Checkpoint cuts now wait for W = H.** Under continuous editing that could delay captures; it
  wasn't observed, since captures kept publishing in every run.

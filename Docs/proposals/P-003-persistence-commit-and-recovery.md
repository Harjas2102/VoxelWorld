→ No action. For your reading only.

# P-003 — Persistence commit, publication and recovery

**Status: revision 3 architecturally adopted, 2026-09-20 (D-023/D-032); exact-format/storage packet required before implementation.**
**Author:** Codex, revised 2026-09-20. **Risk:** R3. **Runtime base:** `21e3a2c`.
**Revision base:** `b72a663`. Director: Start the next step/"T-XXX" phase.
D-023/D-032 govern technical rulings. This prerequisite for T-101B build step 4
supplies the reviewed architectural resolution for DEF-1/2/9; it does not close
their evidence gates or authorize runtime code before the exact-format/storage packet.
The [first review](../reviews/P-003-review-claude.md) remains an immutable record.

## Recommendation and alternatives

Keep D-012's deterministic base, game-owned journal, per-chunk snapshots and SQLite
entity store. The journal commits terrain; an idempotent entity ledger settles its
economic effects. Publish a consistent global cut through two roots and an immutable,
path-copied chunk index. A complete *logical* checkpoint need not rewrite a flat list
of every chunk ever edited.

Alternatives considered: SQLite for terrain would change the adopted storage split;
independent per-chunk cuts need clipped multi-chunk replay absent from the fixed
58-byte op; flat complete manifests rewrite cold history on every checkpoint.
A fixed-depth index preserves the global-cut argument with work proportional to
changed keys. It adds index validation/garbage collection, explicitly tested below.

## 1. Identity, retry policy and acknowledgements — B1

Distinguish three identities:

- `WorldId`: immutable random 128-bit world identity, recorded in every persistence
  object's header and in SQLite metadata, never inferred from directory name.
- `(WorldId, OpSeq)`: permanent settlement key. Record digest must match on duplicates.
- `(AdmissionToken, RequestId)`: transient request key. The server creates a fresh
  unpredictable 128-bit token for **every stream registration**, including reconnect,
  restart and replacement after travel. Bind it to the actual owning connection;
  server-originated producers receive service-owned tokens. RequestId is positive,
  monotonic within that token. SourceId is connection-local, not an inventory owner.

Proposed transport protocol 2 adds the token to session handshake, ready acknowledgement,
requests and receipts; reject mismatches **before queue admission** as `StaleSession`.
Do not reinterpret a tokenless protocol-1 request. The existing 58-byte committed op
is unchanged. Journal envelopes retain a token digest (never the live token), request ID, child ordinal/count and a
canonical intent digest for diagnostics; these are not a new cross-session retry API.

The current code has no reconnect resend queue (`TerrainStreamComponent.cpp:SubmitEdit`
allocates IDs locally). Reliable transport does not establish application-level durable
retry semantics. Replace §4.11.3's unsupported blanket claim about resends across
reconnects with the policy here; do not adopt the review's transport premise as fact.

Within a live token, keep the 64-receipt ring and high-water rejection. An evicted
request is **unknown to the client**, not permission to re-key the same action.
On connection loss or token rotation, clear pending retransmission and held-input
continuations. Never automatically assign a new ID/token to an unresolved old intent.
A later fresh player input is a new intent. Repeating an unchanged Remove/Add/Paint
against unchanged terrain is idempotent under §4.10.3 and must not create new yield.
Intervening edits can make a new action effective; this is not durable retry dedup.
Any future non-idempotent operation must revisit this boundary. StaleSession/StaleRequest are not proof
that an earlier edit did not commit; do not show them as such. This is a fail-closed
retry boundary, not an exactly-once reconnect/status service. An offline machine
producer likewise must not re-submit a persisted outstanding intent as a new action.
Durable machine job/status semantics require their own integration before enabled.

A split keeps its existing partial-commit semantics. Restart abandons uncommitted
children; never replay the parent intent to "finish" it. The journal recovers only
committed children. One token's stale request cannot collide with a new token's ID 1.
An explicit retransmit inside a live token must preserve all intent fields; a digest
mismatch against queued/cached identity rejects rather than silently changing intent.

Two receipt states are distinct: `TerrainCommitted` means the journal is durable;
`Settled` means entity effects are durable too. Queued means neither. NoEconomy can
report terrain commitment without suggesting any reward exists. No acknowledgement
means unknown; restart may find a complete commit that the client never heard about.

## 2. Commit and settlement protocol — B6 / DEF-1

One backend mutation at a time on the game thread (K4). Before mutation, revalidate
all existing admission checks, sequence/revision capacity, bounded envelope sizes,
and the resources needed for storage/capture. Reserve charges and, for future economy,
exact debit resources and required destination capacity/lifetime through settlement.
No external inventory operation may consume or delete those reservations.

1. Apply the bounded backend op synchronously. A false result means no change and no
   OpSeq. A successful zero-change op may commit and consumes a sequence as today.
2. Create the complete immutable journal record with provisional next OpSeq, before/
   after revisions, physical result and exact economic intent. Append and durably
   flush it. A complete valid record is recovery authority even if its flush result
   or acknowledgement was lost. The running process relies on it only after success.
3. Advance committed sequence/revisions and broadcast the committed op. Return
   `TerrainCommitted`. **SQLite durability does not gate terrain broadcast.**
4. Submit immutable settlement input to one serialized entity-store worker, which
   owns its SQLite connection, uses WAL/FULL and checks every result. In one database
   transaction verify/insert the unique settlement key plus digest, apply all exact
   deltas, advance contiguous watermark W. Duplicates with another digest are corruption.
5. Consume worker success on the game thread, release economic reservations, expose
   inventory effects and return `Settled`. Up to **N=32** contiguous committed records
   may await settlement; stop executing new mutations when that window fills. Preserve
   each record's reservations until settlement, including across requester disconnect.
   No UObject/backend access on the worker. Inventory reads/spends expose only committed
   database state, never promised credits. Settlement cannot depend on a live requester.

The window is a bounded throughput mechanism, not a new queue without limits. The
worker may settle up to 16 already-journaled contiguous records in one SQLite transaction,
with all-or-nothing ledger/delta/watermark updates; no artificial wait to fill a batch.
The game-thread pump can commit multiple bounded ops per frame within its time budget.
A window of one would cap asynchronous completions near the pump/tick rate (30/s on a
30 Hz server), below §7.1's 96 ops/s design point, so it is not the adopted default.
N=32 bounds pending reservations and maximum live terrain/settlement cursor separation;
W may lag H by at most N in normal operation. Durable replay remains unbounded by N if
loading a supported older checkpoint, but settlement backlog in a healthy store does not.

Measure journal flush, backend apply, game-thread commit time, settlement latency,
cut-start settlement drain time,
queue age and **sustained commits/s versus 96 ops/s**, plus window saturation. Both
barriers remain in settlement latency; only the journal barrier precedes terrain
broadcast and runs on the game thread. The 8 ms budget is not relaxed or claimed passed.
If either game-thread latency or sustained throughput fails, return with evidence
before adopting journal batching, a different execution model, or a lower gameplay load.

Step 4's prototype permits only explicit `NoEconomy` intent, physical result marked
unavailable where the adapter cannot measure it. It must not encode "unknown" as a
measured zero. Its durable test consumer still advances W for every committed record.
No real inventory dependency is added until the module/storage packet is reviewed.
Real economy integration at step 6 is disabled until DEF-6 defines conversion, overflow,
residue and ownership. Full inventory or a missing/deleted destination must be rejected
before mutation or covered by an approved durable reservation policy. Encountering
such a condition during committed settlement fails closed; never drop/redirect a credit,
refund a debit alone, or advance W. This proposal invents no overflow gameplay policy.

### Storage faults and lifetime

Before mutation, a storage/resource failure is a no-change rejection. After mutation,
append/flush/settlement failure is an uncertain storage fault: close admission, enter
Draining, cancel unexecuted queue entries, disconnect the world and recover from disk.
A broadcast after step 3 already names durable terrain; do not send a rejection or
roll it back if settlement fails. Unbroadcast provisional RAM is discarded.

Starting a new checkpoint cut is forbidden until every in-flight record is settled.
Once its cut is frozen, capture may continue under the write-fence rules in §4; a
provisional post-cut mutation must never be read into that checkpoint.
On storage-fault teardown never snapshot/compact that RAM. Ordinary teardown stops
mutation immediately and needs no final checkpoint: committed journal plus prior roots
are sufficient. A process-owned storage session owns the worker/immutable input until
I/O completes; callbacks use weak service identity plus a generation fence. Draining
never waits on a worker that needs the game thread, and no worker dereferences the
service. Preserve the store's exclusive writer lease until worker release; travel
cannot reopen the same store concurrently. Lease acquisition is nonblocking: refuse
with a diagnosable StoreBusy error; never wait indefinitely for stalled I/O. These are proposed §4.5.1 additions,
not a claim that synchronous backend ApplyOp now runs asynchronously.

## 3. Recovery uses separate cursors — B2

Let G be the chosen terrain checkpoint cut, W the entity settlement watermark, H the
validated durable journal/checkpoint head. All carry the same WorldId and StoreEpoch
(§5). Validate compatibility, checksums and required ranges before mutating stores.

- **Terrain pass:** restore the complete logical checkpoint at G, materialize absent
  chunks touched by replay from its **exact recorded base**, and apply whole ops
  `G < OpSeq <= H` once in global order. Validate before/after revisions and results;
  no yield settlement in this pass. Never replay an op separately per chunk.
- **Settlement pass:** process only `W < OpSeq <= H`, reading recorded economic intent,
  without calling ApplyOp. Verify/skip existing matching ledger identities and advance
  the contiguous watermark transactionally. Never recompute rewards from current tuning.
- W > H or W < G is corruption. Also require W to be at least the highest structurally
valid retained root cut in this epoch, even when falling back to an older payload set.
W below the available journal coverage is also corruption:
  require the complete contiguous range `(W,H]`, even when some lies at or below G.
  Do not infer unavailable settlements from a terrain snapshot.

Example G=100, W=140, H=150: terrain replays 101–150; settlement reads 141–150.
G=140, W=100 is a **negative fixture that must refuse boot**, even if all records
101–150 remain: healthy captures require G=W, so this detects an old database copy.
Independent cursors do not authorize entity rollback or missing non-terrain transactions.

Recovery always initializes a fresh backend from the recorded base, never overlays a
live/dirty backend. Decode SparseDiff to full absolute Dense for WriteRegion; Empty
keeps the freshly generated base plus persistent revision metadata. No player login,
transfer or spending before both passes finish. Replay uses temporary
service-owned residency interests for the entire read footprint, released afterwards;
missing payload does not mean air. Exact generator/kernel/config mismatch refuses boot
and invokes the offline migration procedure, never the currently configured generator.

H is max(G, last complete contiguous journal sequence). All retained segment headers
carry first sequence and predecessor identity; a single final active segment is allowed.
Missing interior segments, conflicting duplicate sequences or framed bad checksums fail
closed. Only an incomplete physical tail of that active segment can be torn append;
preserve it diagnostically before repair. Compute next sequence H+1 with overflow refusal.
No reset on Empty chunks or segment reclamation.

**Journal discovery has its own durable anchor:** two pre-created fixed-size slots,
separate from checkpoint roots, contain world/epoch, rotation generation, active
segment ID and its first sequence, predecessor identity and checksum. Before putting
any record in a newly active segment: create/flush its header and namespace, publish
and flush the inactive anchor slot, then append records. Anchor rotation and root
publication use the same serialized storage owner. On boot, the highest valid anchor
names the active segment; a missing named segment is corruption, never "no more ops".
A newer unanchored empty segment is an orphan; a new nonempty unanchored segment is a
protocol violation. Seal the preceding segment before rotating; retained chain ranges
and predecessor identity must agree with the anchor. The previously anchored segment
can serve fallback only with proof it reaches all durable consumers' high-water marks;
no silent head regression. Rotation requires no anchor update per individual op.
Detailed framing/torn-slot tie-breaking remain in the packet; the anchor is mandatory.

A nonempty unanchored segment is never adopted by ordinary boot. An explicit offline
repair command may preserve the original store, work in a separate copy, and consider
a unique valid extension whose predecessor matches the anchored segment's verified
seal. It must validate every header/record/sequence, root/epoch/base and settlement
floor, reconstruct terrain and settlement coherently, then publish new anchors in a
new-epoch output store. Ambiguous branches, gaps, bad checksums or missing dependencies
refuse repair and require a coherent backup. Preserve diagnostics; never repair by
silently dropping acknowledged history. Exact repair procedure and interrupted-repair
fixtures belong in the storage packet; no repair tool exists yet.

## 4. Consistent incremental checkpoints — B4 / DEF-2

A checkpoint is logically complete at one global G. Replace the flat complete JSON
manifest with a small descriptor plus a persistent **96-bit chunk-key radix index**.
Transform each signed coordinate by XORing its sign bit, concatenate X/Y/Z as big-endian
key bytes. Twelve levels consume one key byte each. An internal immutable page has
at most 256 sorted unique child references; the depth-11 page has at most 256 leaf
entries. Each entry stores revision, last-changing sequence and payload identity/length/
digest (or Empty), including chunks returned to base. The descriptor identifies the
root page, G, world/base identity and generation. All changed paths are copied; cold
subtrees and payloads are shared. No delta chain depends on historical root descriptors.

Bounds for the packet: descriptor <=16 KiB, page <=32 KiB, maximum depth 12, key
traversal exactly 12 bytes. Chunk payload Dense is 131,072 bytes before bounded header;
SparseDiff and Empty follow §6. Reference paths are derived from opaque object IDs,
never untrusted arbitrary paths. Validate depth, child slots, key prefixes, counts,
lengths, digest and world/base identity; cycles or cross-prefix references reject.
A checkpoint changing D keys rewrites at most 12D index pages before shared-prefix
coalescing, regardless of the number of cold edited chunks. Disk history grows with
edited chunks, as Pillar 1 requires; per-checkpoint rewriting does not.

### Capture by copying before the first later write — R2-B2/R2-B3

Track dirty keys incrementally with a hard bound of 4,096 per bank. A key reserves a
**service-owned residency pin** before its first mutation, retained until a successful
full capture (and retained longer if dirtied again for a later cut). Loss of a player's
interest cannot evict authoritative dirty data. Reserve capacity for the complete
candidate footprint before mutation. Current geometry admits at most 2,052 chunks in
one box child, or 27 for the sphere read cap; both fit. At a soft trigger of 256 dirty
keys or the existing age/byte trigger request a checkpoint. Hard-bound pressure pauses
admission/execution before mutation; it never loses a dirty key.

Start a cut only **between transactions**, after the current bounded settlement window
has drained to G=W=H. Freeze the dirty key set and its revision metadata at G, then
resume edits. Do not walk all of its payloads before resuming. A bounded background
capture pump on the game thread reads pending keys into immutable buffers, at most
eight chunks/1 MiB per frame and yielding after a 2 ms target between indivisible reads.
All ReadRegion calls require residency and a successful full sample payload. A false
return or nonresident Empty sentinel cannot be published as pristine. Only a comparison
of those samples against the exact base can choose Empty.

**Write fence:** before executing the first child of an unstarted transaction, ensure
every frozen, not-yet-copied key in that transaction's read/write footprints has been copied
at G. Preflight the whole bounded transaction so its children can later retain contiguous
OpSeq without being interrupted by a capture fence. Schedule required copies through the
same bounded capture pump; defer that transaction while its copies are pending and let
other sources' eligible transactions run under round-robin fairness. A deferred
transaction retains its FIFO position and accumulated turn; once its fence is ready,
continually eligible sources cannot repeatedly demote or starve it. No mutation crosses
a pending fence. The transaction is revalidated normally when it becomes eligible.
Copy each frozen key once; later post-G mutations of it only dirty the next bank.

Thus a chunk copied later in wall time still contains G's data: every earlier mutation
of it was fenced and forced its G copy first. Cold keys use prior immutable payloads.
There is one global cut, not independently chosen chunk cuts. An individual edit that
needs many captures can wait; the system does **not globally stop all terrain execution
for D/8 frames**. No new cut starts halfway through a selected split transaction.
New hooks in queue selection, residency and adapter bulk read are explicit packet work.

The capture worker receives only immutable buffers and writes payload/index objects;
no backend/UObject calls. Backpressure caps buffers at 8 MiB. Allow one frozen bank and
one current bank, each <=4,096 keys, and one publication at a time. Reserve the whole
next candidate footprint before mutation; if it would overflow while the frozen bank
publishes, stop execution until capacity is freed. On successful publication retire the
frozen bank; on failure retain prior roots/journal and enter storage-fault handling.
Pins release only after no bank or mutation reservation needs them.

The deliberate bound is **up to 1 GiB of raw dense samples** across two disjoint full
banks (512 MiB each), plus measured backend/index/meshing overhead, not "just 8 MiB".
This is a prototype memory reservation, not a promise that plugin RSS equals sample
bytes. Memory pressure must refuse admission before mutation. Measure actual peak RSS,
residency-pin expansion, copy cost and storage buffers before production acceptance.

One ReadRegion is indivisible, so its maximum cost is an acceptance gate; the current
32,768-per-voxel-call adapter path must gain a measured bulk-read implementation before
production integration. Measure per-op capture-fence delay separately from apply time,
at 30/60 Hz and 96 ops/s, including worst legal thin boxes, split transactions and slow
I/O. The constants are ceilings, not measured rates. A visible multi-second stall under
the supported gameplay workload fails; there is no automatic relaxation of the budget.
Capture throughput must meet the sustained unique-dirty-key rate at §7.1 load; if it
does not, change the copy constant or mechanism with evidence before integration.
This mechanism removes the deterministic D/8 global pause in revision 2, not all I/O
backpressure. If performance fails, change the mechanism with evidence before adoption
of a playable implementation.

### Publication

Write and durably publish payload/index dependencies before the descriptor. Two root
slots are **pre-created fixed-size 4 KiB files**, overwritten in place, never renamed
as the publication primitive. Each contains generation, WorldId, StoreEpoch, G,
descriptor ID/length/digest, format, and whole-slot checksum; reserved bytes are zero.
Only overwrite the inactive slot and flush it. Initial world creation durably creates
both slots and an empty G=0 checkpoint before any edit admission.

**Boot validation is eager and verifying**, not structural-only: validate root slots,
all referenced index pages **and every referenced payload's actual content digest**,
with bounded streaming buffers, before choosing the highest valid compatible generation
under §5. This deliberately costs O(index bytes + unique stored payload bytes), e.g.
131 GB of reads for one million all-Dense chunks, shared objects read once. It matches
the initial recovery model, which already restores every edited chunk before login.
The B4 fix concerns incremental runtime checkpoint work; it does not claim sublinear
startup. Record boot time/memory versus save size as a separate acceptance metric.
Lazy payload validation would require a reviewed server-side hydration/quarantine
protocol so corrupted chunks never become gameplay-ready; it is not silently added by
this proposal or confused with step-5 client JIP. This explicit eager choice resolves
R2-B1's ambiguity while preserving complete-closure fallback. A production boot-time
failure at the intended saved-world scale requires revisiting hydration before release. Torn inactive publication leaves the
other root. An orphan descriptor is never promoted. Validate new objects during write
and validate the new root plus referenced closure against the already validated immutable
object catalog before deletion; unknown catalog entries require verification. Fresh boot
always revalidates from disk. If either root fails, disable reclamation until redundancy
is repaired. Successful checkpoint publication must not require rereading every shared
cold object; arbitrary later media corruption is diagnosed on read/boot, not promised
impossible by the cache. Keep both roots' required replay tails.

Fixed roots/anchors do not prove namespace durability for new payloads or segments.
The storage packet must establish that ordering on the tested filesystem. The defined
fallback, if durable new-name publication cannot be established, is **pre-created,
preallocated container files**, with immutable objects addressed by opaque `(container,
offset,length)` IDs. Pre-create the bounded container pool and roots/anchors before
admission; no new live namespace entries in this mode. Exhaustion backpressures before
mutation, never grows the pool using an unproven primitive. Segment and object headers
still carry identity/checksums; roots/anchors publish offsets. Pool sizing, extent reuse
and bootstrap durability tests are packet choices, not a third storage architecture.
Select and prove one mode before service integration; no claim that rename or a single
FlushFileBuffers call proves a multi-file protocol.

## 5. Compatibility, retention and coherent restore — B3 / DEF-9

SQLite metadata and every file header carry `(WorldId, StoreEpoch)`. StoreEpoch is a
random 128-bit lineage ID created with a world store; a coherent restore or migration
creates a new epoch for the entire output. It does not change settlement WorldId/OpSeq.
Normal checkpoint publication does not change epoch. Exclusive writer ownership is
required; running two copies of the same world as independent branches is unsupported.
A deliberate world clone must remap WorldId across all stores offline before use.

A compatible boot root has matching world/epoch, supported format and exact base/kernel
identity, valid dependency closure and a complete terrain tail to H. The attached SQLite
store must have matching world/epoch, pass integrity validation, have
max(G of structurally valid retained roots)<=W<=H and possess
all records `(W,H]`. Older-root fallback is only a different terrain cut in the **same
live store**, with the live database and retained tail; it is not an entity rollback.

**Entity-only restore is prohibited.** Terrain journal entries cannot reconstruct
unrelated crafting, transfer, spending, machine or structure transactions. Thus even
having every terrain settlement record does not make an old entity backup compatible.
Never automatically substitute a backup database or open a blank one when live SQLite
is missing/corrupt. Operator restore must use a validated whole-world backup, even if
newer terrain roots remain readable. Merely comparing watermarks cannot detect every
manual file replacement; unsupported manual mixing is not claimed detectable or safe.

A backup is self-contained at B: pause terrain and all entity writers, await settlement
W=H=B, produce a terrain checkpoint at B, use the SQLite backup API (or a clean closed
DB), and copy all dependencies into a separate temporary backup directory. A final
backup descriptor binds world/epoch, B, exact DB-file digest and terrain closure digests;
validate and durably publish it before declaring the backup usable or releasing pins.
Restore that complete backup into a new directory, verify it, assign a new StoreEpoch
to all output metadata, and start a new journal at B+1. Do not overlay it onto the live
directory or select newer roots outside that backup. Restoring an older backup is an
explicit whole-world rollback, never silent recovery of acknowledged newer history.

A sealed journal segment may be deleted only when its end is covered by the older of
the two valid retained terrain cuts, by live W, and by all active backup/migration/sync
pins. Future step-5 sync readers are not enabled by this proposal; before integration
they must acquire the same explicit retention handle for their exact object/segment
ranges and release it on completion/cancellation. Its successor's header must preserve continuity evidence. Cold chunk last-change
sequences do not pin newer history. Self-contained completed backups retain their own
data, not the live journal. Keep ledger identities until a separately proven durable
floor permits their removal; no ledger GC in the first implementation.

Payload/index GC traces the union of both root closures and active pins, with bounded
I/O buffers off the game thread. Publication/pin acquisition and GC use one storage
owner: capture a root/pin epoch, stop deletion if it changes, and never delete an object
created after the captured epoch. Deletion is permitted only after publication success
and validation of both roots. Never delete from another world/store or by age alone.
Reference selection/pin acquisition and deletion are serialized owner tasks. A new
publication may reuse only objects reachable from an already validated root or protected
by its own creation pin; it cannot resurrect an unpinned orphan by content hash.
Failure leaves extra files. Full mark work may grow with history but runs incrementally
and is not required for each checkpoint to succeed. Disk exhaustion closes admission
before mutation where possible; it never justifies dropping a recovery dependency.

## 6. Base identity, encoding and migration

The exact base descriptor contains seed, generator identity/version, origin/grid,
voxel/chunk sizes, bounds, backend/kernel compatibility, value/material configuration,
material-catalog version and ordered authored-stamp digest (currently an explicit empty
list). All referenced objects bind its digest. A pristine chunk touched after G is
materialized using this descriptor, not the installed defaults.

Compare every density **and material** against the canonical encoded base; provenance
is not equality. SparseDiff contains absolute replacement samples at changed indices,
Dense contains all samples, Empty retains revision/last-change metadata. Choose smaller
SparseDiff/Dense including encoding overhead, Dense on ties. A zero-difference chunk
uses Empty. Encoding choice and exact base sampling must have material-only fixtures.
The current production adapter has density-only transfer and no exact material baseline:
its prototype cannot claim full-state persistence or enable sparse/pristine compaction.
Full production DEF-9 closure requires that fidelity, not a successful memory fixture.

Migration is offline: validate/recover source to H with its old generator/kernel,
settle to H, retain a verified coherent `.bak`, materialize **all history-bearing**
chunks (including Empty and SparseDiff) into absolute samples, then convert in a separate
directory and publish a complete target at H. Preserve WorldId, sequence, revisions and
settlement identities; assign a new StoreEpoch. Never replay old ops through a new
kernel. Changing base does not reinterpret formerly edited Empty chunks as the new
base. Truly never-edited chunks may regenerate per §4.10.6. A backend change uses its
explicit reviewed resample migration; no automatic lossy conversion is authorized.
Unknown schema or missing old implementation/catalog stops without altering source.

## 7. Adopted architecture amendment — B5

The 2026-09-20 architectural adoption replaces **all of §4.7** with this referenced
contract; its exact byte-format packet remains a separate prerequisite. Amend §4.4 sequencing, §4.5.1 storage lifetime, §4.11.3/8 retry advice and §4.10.6
migration precedence. ARCHITECTURE now references §§1–7 as the adopted architectural contract; it does not
claim the missing byte-format packet or runtime implementation exists.
Do not leave two contradictory definitions available to an implementer. Re-home the
existing **transfer** Dense layout in §4.2: LE int16[32^3] followed by uint16[32^3],
local index x+32*y+1024*z; disk schema 2 does not change that replication contract.
Update cross-references for FTerrainRegionData::Payload, backend/config versions,
power-grid entities, migration and DEF-9. The packet must assign one recovered sequence
owner (the queue currently owns committed assignment); remove the unused service counter
and provide a validated H+1 seeding path before admitting work.

| Former §4.7 item | Adopted replacement |
|---|---|
| Unimplemented schema 1 sketches | Reserve version 1 as legacy design, never emit it. New persistence envelopes use **schema 2**; unrecognized/version-1 files refuse without a registered decoder. The op's 58-byte codec is unchanged |
| `world.json` sequence fields | Inspectable advisory status, schema 2, world/epoch/base and timestamps; roots+journal own sequence authority |
| `chunks/X_Y_Z.chunk` overwritten path | Immutable uniquely identified payloads referenced through path-copied pages and checkpoint descriptor |
| TCHK header, payload-only CRC | Keep key/rev/lastOpSeq and encoding metadata; add world/epoch/base identity, complete bounded framing and header+payload checksum |
| TJOP + op + serverUtcMillis | Keep op and UTC diagnostic time; add framed schema-2 envelope, world/epoch, request token digest/ID/child identity, before/after revisions |
| Physical `{matId u16, microLitres i64}` list | Retain signed physical list with explicit availability. **Add**, do not replace it with, versioned economic intent: NoEconomy or exact stable-owner/inventory/item deltas plus policy version |
| `{key,newRev}` list | Sorted unique changed keys with before/after revisions; zero-change list empty; retain complete replay preconditions required by the packet |
| ~122 bytes/edit estimate | Withdraw. Measure schema-2 bytes/edit, including identity/economic envelope |
| Per-chunk compaction publication | Consistent global cut, incremental chunk/index write, two roots and consumer-based retention |
| Implicit migration at load | Offline coherent whole-store conversion; source and `.bak` retained |

WorldId belongs inside headers, not filenames; copying a file across worlds is rejected.
Field widths, byte offsets, digest algorithms, segment discovery/rotation, allocation caps
and optional-field rules must be fixed in a separate reviewed **specification** packet
before codecs. This architecture revision is not itself a binary-format implementation
packet. UE/plugin types never become persisted or cross the adapter boundary.

## 8. Evidence, independent review and next bounded increment

Revision 2's [independent review](../reviews/P-003-review-claude-r2.md) confirmed all
original B1-B6 addressed and identified R2-B1–R2-B6. Revision 3 resolves them with:
explicit eager boot verification; copy-before-write capture; dirty-key residency pins
and honest memory reservation; a 32-record settlement window; W>=retained G checks;
and dual journal anchors plus a preallocated-container fallback. X||Y||Z key ordering
is retained for simple canonical implementation and ordered-key diagnostics: the 12D
bound does not rely on spatial locality; Morton packing is not a correctness requirement.
The [focused revision-3 review](../reviews/P-003-review-claude-r3.md) finds all six
closed and the architecture adoptable. Technical ruling under D-023/D-032, 2026-09-20:
adopt §§1–7 at the architectural level, incorporating its nonblocking throughput,
fairness, footprint, offline repair and measurement clarifications here. The reviewed
SHA identifies the pre-adoption text; changes after that review are this status/ruling
and those requested clarifications. Runtime and defect-evidence status remain unchanged.

Author checks on revision 2: actual request/session/queue code inspected; B1's missing
token confirmed, automatic reconnect retransmission not established. B2–B6 accepted
as specification gaps; their resolutions are §§2–7, with B3 clarified to require a
whole-world restore even if terrain journal coverage happens to remain available.
No runtime tests or crash/power-loss experiment is claimed by this document.

The completed independent review examined stale same-session/rotated-token IDs, partial split,
lost ack, terrain broadcast before settlement, W<G and W>H, missing `(W,H]`, entity-only
rollback, cold index subtrees, multi-frame capture, GC racing pins/publication, torn
root/segment and migration of edited Empty chunks. Distinguish an architectural blocker
from exact-format items explicitly assigned to the next specification packet.

Next increments under the accepted review and technical ruling:

1. **Exact format/storage specification**, including protocol 2 field table, schema-2
   offsets/caps/golden fixtures, per-checkpoint object counts, Windows namespace/rotation
   guarantees, storage-owner lifetime and module dependency boundaries. No runtime implementation while ambiguous.
2. **Bounded codecs and storage seam**, against the adopted packet; corrupt/truncated
   fixture coverage and inspect/dump command. No service/economy integration yet.
3. **Commit/recovery integration**, NoEconomy consumer, crash matrix and MP regression.
4. **Checkpoint/retention/migration**, memory evidence then production transfer fidelity,
   restart hashes, save growth and maximum pause/flush/queue measurements. DEF-1/2/9
   remain open until their architecture resolution **and named evidence** exist.

Named evidence for DEF-1: `TerrainCore.Persistence.CommitCrash`,
`TerrainCore.Persistence.SettlementReplay`, `TerrainCore.Persistence.RetryFence`.
DEF-2: `TerrainCore.Persistence.CaptureFence`, `TerrainCore.Persistence.RootPublication`,
`TerrainCore.Persistence.JournalAnchor`. DEF-9: `TerrainCore.Persistence.RetentionPins`,
`TerrainCore.Persistence.CoherentRestore`, `TerrainCore.Persistence.Migration`, plus
existing `Snapshot.*`, `Revision.Monotonic` and production `Restart.Identity` /
`Restart.CrashMatrix`. These are required tests, **not executed results**.

Mandatory injected failures: before/after apply, append/flush, SQLite begin/write/commit,
broadcast/receipt, every payload/page/descriptor write/flush, root overwrite/flush,
segment discovery/rotation and every deletion. Repeat recovery; include missing/corrupt
SQLite, backup interruption, pinned GC, dirty-set overflow and slow I/O. Graceful exit
is not crash evidence. Real yield/capacity policy stays DEF-6/step 6, JIP stays step 5,
and collision/movement readiness stays step 7.

Source verification: installed UE 5.8 `WindowsPlatformFile.cpp:933` routes file-handle
Flush to FlushFileBuffers. This proves that call path only. [Windows file caching](https://learn.microsoft.com/en-us/windows/win32/fileio/file-caching)
and [FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers)
explain the primitive; namespace/power-loss behavior still needs evidence.
[SQLite WAL](https://www.sqlite.org/wal.html) documents FULL's per-transaction sync;
it does not make terrain files and SQLite a shared transaction.
[Epic RPC documentation](https://dev.epicgames.com/documentation/unreal-engine/remote-procedure-calls-in-unreal-engine)
describes reliable resending until acknowledged; this proposal does not infer durable
application retry across a new connection from that transport guarantee.

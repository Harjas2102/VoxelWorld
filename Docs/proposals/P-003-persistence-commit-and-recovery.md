→ No action. For your reading only.

# P-003 — Persistence commit, publication and recovery

**Status: proposed; independent review required before implementation.**
**Author:** Codex, 2026-09-19. **Risk:** R3. **Base:** `21e3a2c`.
**Authority:** Director: "Commit and move to the next step"; technical rulings follow
D-023/D-032. This is the next prerequisite increment for T-101B build step 4.
It proposes resolutions to DEF-1, DEF-2 and the remaining DEF-9 questions. It does
not mark those defects resolved, change numbered decisions, or introduce save code.

## Goal and recommendation

After a restart, recover every acknowledged terrain edit exactly once, keep terrain
and earned/spent inventory consistent, and reclaim old journal segments only when
every recovery consumer can do without them.

Keep D-012's game-owned operation journal, per-chunk snapshots, deterministic base
and SQLite entity store. Use the terrain journal as the commit authority, an
idempotent settlement ledger in the entity database, and immutable chunk files
published through complete world checkpoint manifests. A checkpoint represents
one global commit boundary, even when unchanged chunk payloads are reused.

Alternatives: making SQLite the terrain store violates the adopted storage split;
independent per-chunk publication needs clipped multi-chunk replay that the fixed
58-byte operation cannot express. Complete manifests avoid that replay ambiguity
without changing the operation format. They cost a manifest scan and capture of
all chunks changed since the preceding checkpoint, which must be measured.

## 1. Durable identity and the commit sequence — DEF-1

The permanent settlement identity is `(WorldId, OpSeq)`. Connection SourceId and
RequestId remain transient admission identities; they are never inventory owners.
A future economic intent names stable entity/inventory IDs and exact signed deltas.
Do not recompute rewards from current tuning, current ownership or current geology.

All terrain mutation, sequence assignment and snapshot capture stay on the service's
game-thread execution path. Before a mutation, reserve queue/rate/tool costs, resolve
the stable owner and revalidate as today. No external entity transaction may consume
the same reserved placement items before that edit resolves.

For each admitted sub-operation:

1. Validate every precondition, including sequence/revision capacity. Choose the next
   provisional sequence, without publishing it or sending a successful receipt.
2. Apply the bounded backend operation synchronously. A backend refusal changes
   nothing and consumes no sequence. Capture exact changed keys and physical result.
3. Form a complete immutable journal commit record: encoded op with sequence,
   expected before/after revisions, physical result and exact economic intent.
   Append and flush that record. **A complete valid record is the commit authority.**
   Successful durable flush is the point at which the running process may rely on it.
4. Advance in-memory revisions and the committed sequence. Apply settlement through
   one entity transaction: verify/insert the unique settlement identity and record
   digest, apply all credits/debits, and advance the contiguous processed watermark.
   Zero-effect records also advance that watermark. Duplicate identity with different
   contents is corruption, never an ignored insert.
5. Only after the entity commit is durable, publish the terrain op and inventory
   effects and return success. A split still commits each child independently.
   Transfer, crafting and spending can see only settled inventory state.

Step 4's current nonconsuming prototype records an explicit `NoEconomy` intent and
does not manufacture inventory/yield. Its test settlement consumer exercises the
protocol independently. Real player settlement/consumption is a step-6 integration
gate; economic operations remain disabled until that implementation passes the same
crash matrix. This sequencing does not claim a working economy at step 4.

The entity store remains one SQLite database. Require WAL and `synchronous=FULL`,
check every transaction result, and serialize settlement with all other inventory
transactions. This does not make the terrain file and SQLite atomic together: the
unique settlement ledger and retained journal supply that missing reconciliation.

### Disk errors and uncertain outcomes

An error before backend mutation is an ordinary no-change rejection. An append,
flush, or entity-commit error **after mutation** is not an ordinary rejection:
the durable result may be unknown. Enter Draining with a storage-fault cause,
close edit/economy admission, cancel work that has not executed and disconnect the
affected world. Never broadcast the provisional edit or continue from its RAM state.
Recover from disk before reopening. Do not retry the economic effect under a new ID.
In particular, storage-fault teardown must not run normal shutdown snapshot/compaction:
the backend can contain a mutation that the last committed sequence does not describe.
Capture remains disabled from mutation start until journal commit and metadata advancement
have both completed. A storage fault discards that RAM instance without publishing it.

If an unacknowledged record nevertheless survived fully, recovery commits it. If it
did not, recovery discards the uncommitted RAM change. Thus an absent acknowledgement
means "unknown", not "definitely rejected". A future reconnect-status API must use
stable durable identity; the 64-entry connection ring alone makes no such promise.

On startup, validate the store first, reconstruct terrain, then process journal
records after the entity watermark in sequence. A settlement already in the ledger
is verified and skipped; a missing settlement is applied exactly once. Complete
reconciliation before player login, transfer or spending. An entity watermark beyond
the validated terrain high-water mark is a hard integrity error, not permission to
discard player inventory or invent terrain records.

## 2. Checkpoint publication and boot replay — DEF-2

Add immutable `checkpoints/<generation>.json` manifests and generation-qualified
chunk payload paths. A manifest contains schema, WorldId, generation, global
`checkpointOpSeq=G`, the complete base descriptor, and every nonzero-history chunk's
key/revision/last-changing-sequence, payload path, byte length and content digest.
Unchanged chunk payloads may be referenced from an older generation. Empty records
retain revision metadata even when no terrain payload is necessary.

Capture occurs **between complete commits**, with mutation and settlement paused.
Choose G as the current committed sequence. Read every dirty chunk into immutable
owned buffers and copy revision metadata from the same boundary. No backend/UObject
access from an I/O worker. Initial implementation captures synchronously; do not
introduce a terrain thread to conceal a failed performance budget.

Write new chunk files under unique names, flush their content and required filesystem
metadata, then write and flush the complete manifest. Publish its filename/digest in
the inactive one of two versioned root slots. Each slot contains generation, manifest
identity, journal floor, length and checksum. Flush the inactive slot before treating
publication as durable. Never overwrite the active slot or any reachable payload.
On startup, validate both roots and their complete dependency closures; select the
highest valid compatible generation. A torn new slot leaves the previous one valid.
No directory scan may promote an orphan manifest into the authority implicitly.

`world.json` is inspectable configuration/status, not an independent sequence oracle.
Recover the next sequence from `max(G, highest complete contiguous journal OpSeq)+1`;
reject overflow, duplicates with different contents and interior gaps. Never reset it
because a chunk returned to its base or a journal segment was reclaimed.

Restore all chunk payloads from the selected complete manifest, then replay every
committed op with `OpSeq > G` **once in global order across the entire operation**.
Do not replay independently per chunk. An older reused payload is valid at G because
that chunk did not change between its capture and G. This is the precise property
that makes full-op replay safe with a multi-chunk operation and the unchanged codec.

Only an incomplete final record at the physical end of the active segment may be
treated as a torn append. Preserve a diagnostic copy before truncation. A CRC failure
in a fully framed record, a malformed interior record or an absent middle segment
fails closed; it is not permission to truncate acknowledged history silently.

## 3. Retention and fallback — DEF-9

Retain the two published checkpoint generations and every dependency each references.
Never delete a payload simply because the newer checkpoint stopped referencing it.
Both roots must be valid before reclaiming anything needed by the older generation.

A sealed journal segment can be deleted only if every record in it is:

- covered by the **older retained complete checkpoint** (not just by the newest one);
- processed durably by the entity settlement consumer, including zero-effect records;
- no longer pinned by a backup, migration, or future step-5 sync reader.

This is a per-segment dependency test. It does not take the minimum of the last edit
of every cold chunk. A cold chunk covered by G does not retain all history after its
last edit. Conversely, the settlement consumer can retain a record after terrain
has snapshotted it. Deletion failure leaves extra files, never changes commit status.

Fallback to the older checkpoint also requires the complete journal tail needed to
reach the latest durable high-water mark. Retain that tail before deleting anything.
If neither retained checkpoint has a valid closure, stop and request a coherent backup;
do not regenerate edited terrain and call it recovered. This is resilience against
interrupted publication, not a promise to survive arbitrary loss of both generations.

Ledger garbage collection is separate from journal deletion. Until a proven durable
floor excludes all possible reprocessing, retain settlement identities. Backup and
restore capture terrain roots/journal and entity database as one world generation;
copying a live SQLite main file without its WAL is not a coherent backup.

## 4. Base identity, pristine equality and migration — DEF-9

The base descriptor records seed, generator identity/version, origin, voxel size,
chunk size, backend identity/kernel compatibility, value/material configuration,
material catalog version and authored-stamp digest. Current stamps are explicitly
an empty ordered list, not an omitted field. Snapshot headers/manifests identify the
same descriptor; mismatched pieces cannot be assembled into one world silently.

Determine pristine state by comparing **every stored density and material sample**
against the exact canonical base for that descriptor. `bIsGeneratorValue` only reports
provenance and is not an equality test. SparseDiff stores absolute replacement samples
at changed indices against that base; Dense stores the whole chunk. Choose the smaller
supported encoding as K3 requires. A zero-difference result writes Empty metadata;
revision and last-changing-sequence survive. Until production material transfer and
exact baseline access exist, do not enable sparse/pristine compaction on that backend.

Migration is offline with admission closed. Validate the complete source generation
and retain a verified `.bak` copy of the whole coherent world (including SQLite WAL
through the SQLite backup mechanism or a clean closed database). With the old base
implementation available, materialize all edited chunks to absolute dense samples,
replay the source journal to its head, reconcile settlement, then publish a complete
target generation in a separate directory. Preserve WorldId, revisions, committed
sequence and settlement identities. Apply the registered schema conversion chain;
never replay old ops using a different kernel or generator.

For a backend change, perform the explicit resample migration already required by
§4.10.4(c), compare expected loss/error and retain the old generation. No automatic
lossy conversion is authorized here. Missing old generator/catalog support, unknown
schema or an unavailable migration edge fails without altering the source. A `.bak`
cannot substitute for an implementation capable of decoding its data.

## 5. Format and storage boundaries

The sketch in §4.7 has no shipped saves yet. Define initial persistence schema 1
before writing any real save: explicit little-endian framing, record length, maximum
length/counts, whole-header-and-body checksum, and reserved bits that must be zero.
Keep the nested `FTerrainOp` at 58 bytes. Add a versioned economic-intent envelope
and before/after revisions; unknown mandatory fields refuse loading.

Exact byte tables and resource caps belong in the first reviewed implementation
packet. They must be fixed before its codec is written, with golden fixtures in
`Tests/Saves/`, malformed/truncated input coverage and an inspect/dump command.
No production save format may be inferred from this prose alone.

The storage abstraction must distinguish buffered append, durable flush, publication
and deletion, and allow failure injection before/after each. UE archive Flush is not
assumed to be a durable OS flush. On Windows verify the chosen platform file handle
actually reaches FlushFileBuffers and preserves namespace metadata ordering; no
claim that rename by itself supplies power-loss durability. Initial support is local
storage on the tested filesystem; network filesystems require separate validation.

Local source verification, 2026-09-19: installed UE 5.8
`Engine/Source/Runtime/Core/Private/Windows/WindowsPlatformFile.cpp:933` implements
`FFileHandleWindows::Flush` with `FlushFileBuffers`; `OpenWrite` at line 1638 returns
that handle. This establishes the content-flush path, not namespace durability or
successful power-loss recovery. Those remain storage-packet acceptance conditions.

Reference facts: [Windows file caching](https://learn.microsoft.com/en-us/windows/win32/fileio/file-caching)
and [FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers)
describe cache flushing and its costs. [SQLite WAL](https://www.sqlite.org/wal.html)
documents per-transaction sync with FULL. These support the primitives; they do not
prove this proposed multi-file protocol or a particular drive's power-loss behavior.

## 6. Bounded implementation sequence and acceptance

1. **Format/storage packet:** finalize byte tables and caps; pure codecs, fixture loader,
   storage fault seam, durable-file primitive tests. No service integration or SQLite
   dependency added until its exact module boundary is reviewed.
2. **Commit/recovery packet:** serialized journal integration, storage-fault shutdown,
   ordered restart and idempotent test settlement consumer. Re-run multiplayer convergence
   and restart full-region hashes; no real economy claim.
3. **Checkpoint/retention packet:** atomic capture, complete manifests, two roots,
   sparse/dense/Empty where backend support is real, pinned segment reclamation,
   fallback recovery and offline migration fixtures.
4. **Production gate:** adapter transfer/material fidelity, real entity settlement at
   step 6, restart/crash matrix, save growth and checkpoint pause measurements. Do not
   label the whole persistence/economy gate passed after only the memory fixture does.

Mandatory crash cases: before/after apply, partial journal append, record flush,
entity begin/write/commit, broadcast/ack, payload write/flush, manifest write/flush,
root-slot write/flush, and each payload/segment deletion. Include lost ack, transfer
and spending after mining, multi-chunk edits, split transactions, repeated recovery,
old-root fallback, corrupted newest payload, missing segment, a cold unchanged chunk,
reversion to base and migration interruption. Every durable boundary needs injected
failure evidence, not a test that merely calls graceful shutdown.

## Independent review questions

Find any route to durable inventory without removal, lost or duplicated payout,
acknowledged history loss, double multi-chunk replay, reused sequence/revision,
unsafe segment deletion, impossible fallback or migration from unavailable data.
Check the provisional-versus-durable sequence distinction and root publication on
Windows. Separate blocking specification gaps from implementation measurements.
No runtime implementation may start on an unresolved review blocker.

### Review attempt and current disposition

On 2026-09-19 the installed Claude CLI was invoked with Opus and only Read/Glob/Grep
available, with MCP disabled, for the required independent review. It returned
**401: API key is invalid**. No review was produced and no approval is inferred.
The proposal remains unadopted; DEF-1/2/9 remain open. The next safe action is to run
the independent review through an authenticated reviewer, then resolve its findings
and fix the exact format/storage packet before implementing persistence.

→ No action. For your reading only.

# Retention continuation review — Codex

Base: `ebdf8ae73980d8108734632d4e4fa57f57aa23d8`, CP-015/T-123.
Reviewed the nine inherited, uncommitted retention files against P-003 §5 and
P-004 §§6–8, 12–13. The supplied `claudepaste.txt` identifies the unfinished
increment as object/pack reclamation, followed by production measurement.

The initial review is independent of Claude's implementation. Codex then repaired
the findings below; validation of those repairs is self-review, not a second
independent review. No schema or digest fixtures changed.

## Findings addressed in this continuation

1. **Recovery closure was not validated before deletion.** The custom mark walker
   checked page bodies but not traversal depth/prefix/declared length, and merely
   added payload digests to the live set. A missing payload therefore did not stop
   reclamation. Marking now uses `TerrainIndexEnumerate` with a read-only tracking
   store and validates payload digest, type, identity, length, chunk/revision/op
   metadata and cut bounds. Tests publish a missing payload reference and an
   incorrect root-page length, and require every device byte to remain unchanged.
2. **Root validity was cached.** Refreshing after successful publication did not
   cover subsequent slot damage or failed publication. Every retention pass now
   reads both on-device slots, validates each descriptor against its root and
   requires the open store's current descriptor in the marked set. A test damages
   a slot after Open and proves the stale healthy cache cannot authorize deletion.
3. **Compaction trusted copied bytes.** Survivors now undergo BLAKE3 verification
   before writing and normal content-addressed readback before original deletion.
   This is not namespace durability evidence; see the outstanding limit below.
4. **A torn replacement poisoned the next pack ID until restart.** A failed pack
   write that leaves a file now consumes that ID. Tests inject failed creation,
   torn creation and failed original deletion, verify both generations through
   actual backend restore, then retry reclamation in the same open store.
5. **Fallback evidence overstated what was checked.** The inherited test computed
   `HashesAtG8` but never compared it. The replacement damages the newer slot on
   a device copy, uses ordinary Open/restore, and compares G=8 terrain hashes.
6. **Failure reporting was false after partial work.** The console no longer says
   "Nothing was deleted" for all failures; it reports completed sweep operations.
7. **The production workload did not make multiple generations.** Repeating Add
   is idempotent: `Reclaim-b.log` and `Reclaim-c.log` show `voxels=0`. The new harness
   alternates Add/Remove/Add and requires changed voxels and distinct consecutive
   terrain hashes before measuring reclamation.

## Limits that remain open

- The manual console pass is a diagnostic prototype, synchronous on the game
  thread. It is **not** P-003 §5's incremental, off-thread, bounded-buffer GC.
  No automatic scheduling or production GC completion is claimed.
- There are no backup/migration/sync retention handles yet. Those consumers must
  not be enabled concurrently with this pass before P-003's pins/epoch protocol
  and exclusive-writer ownership are implemented.
- **R-015 matters more for compaction:** a replacement pack can contain objects
  shared by both roots. Losing its new directory entry after deleting the original
  could invalidate both generations. Readback and an in-process crash test do not
  prove power-loss durability. Production compaction remains behind P-004 §12's
  namespace-durability acceptance gate (or its container alternative).
- Journal trimming, generator migration, material transfer fidelity and the broader
  crash matrix remain separate work. DEF-9 is not closed by this increment.

Exact verification results and the safe restart point are in `Docs/HANDOFF.md`.

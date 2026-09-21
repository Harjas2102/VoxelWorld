→ No action. For your reading only.

# P-005 — Namespace durability: the store stops creating names at runtime (R-015)

**Status: specification and Architect ruling, 2026-09-21. Implemented as T-126.**
**Author:** Claude Opus. **Risk:** R3. **Base:** `25b6670` (CP-016).
**Authority:** P-003 §4 (*"the defined fallback, if durable new-name publication cannot be
established, is pre-created, preallocated container files"*), P-004 §12 (Mode B named as a
discharge of the acceptance gate), D-023 (technical ruling, logged), D-039 §3 (the gate on
retention).

Writer and reviewer are the same agent for this packet (Director's standing instruction,
2026-09-20). §9 is the adversarial self-review, written down rather than skipped.

---

## 1. The question, and why it will not be answered by measurement

R-015 asks whether a **newly created directory entry** survives a power cut on Windows, given
that the file's contents were flushed with `FlushFileBuffers`. Win32 documents a content flush
and nothing about the directory entry. Three ways to settle it were considered:

1. **A power-loss rig.** A VM whose host cuts its power while it creates and flushes files. The
   development machine runs Windows 11 Home, which has no Hyper-V. And such a rig can only show
   a failure; a thousand clean runs prove nothing about the thousand-and-first on different
   hardware. It is evidence about one disk, not a guarantee.
2. **Rely on NTFS behaviour.** NTFS journals metadata, and in practice a full flush of any file
   forces the log past every earlier create. This is widely relied on (Git's batch fsync mode
   is built on it), and it is **not a documented contract**. It also says nothing about the
   Linux dedicated server this project ships on (D-002), where POSIX is explicit that `fsync` on
   a file does *not* make its directory entry durable.
3. **Stop depending on it.** P-003's container mode. No name is created or needed at runtime,
   so the question does not arise on either platform.

**Ruling: (3).** It is the only option that turns an unproved assumption into a property of the
code, it is the fallback P-003 already adopted, and P-004 §1 rule 10 made it cheap in advance:
no schema-2 field contains a path, so **no object, record, slot or golden vector changes.**
This removes a risk rather than accepting one, so under D-023 it is logged, not escalated.

## 2. What the store did at runtime before this packet

Audited from the source, not from the documents:

| Operation | Creates or removes a name | When |
|---|---|---|
| Checkpoint capture | **creates** `packs/pack-N.tpk` | every capture |
| Unbatched `StoreObject` | **creates** `objects/hh/<digest>.tobj` | world creation (the G=0 descriptor) |
| Retention: pack compaction | **creates** a replacement, then **removes** the original | `Terrain.Reclaim` |
| Retention: sweep | **removes** dead packs and loose objects | `Terrain.Reclaim` |
| Journal rotation | **creates** `journal/seg-N.tjs` | **tests only** — production never rotates |
| Root / anchor publication | none — overwritten in place | every capture / rotation |
| Journal append | none — extends an existing file | every edit |

Capture's name creation was already *contained* (a lost pack name fails the new root's closure
and the older root is used, P-004 §12). Compaction's was not: the replacement can hold objects
both roots share, so losing its name after removing the original defeats both generations.

## 3. The primitive this packet does rely on

**Per-file flush covers that file's contents and its length.** `Flush(true)` is
`FlushFileBuffers` on Windows and `fsync` on Unix; `fsync` is documented to include the file's
metadata, and a Windows file flush writes the file's own record. This is not a new assumption:
**every acknowledged edit already depends on it**, because journal append extends an existing
file and an edit is acknowledged when that flush returns. Container mode asks for exactly as
much of the platform as the journal already does, and no more.

Two operations are added to `ITerrainStorageDevice`, both on existing files:

- **`Truncate(path, size)`** — shrink only (growing is refused as `WrongSize`), then
  `Flush(true)`. UE implements it with `SetEndOfFile` / `ftruncate`. It changes one existing
  file's length, the same kind of metadata an append changes.
- **`ReadRange(path, offset, length)`** — a positioned read, so resolving one object does not
  read a whole container. Read-only.

A third, **`SyncDirectory(path)`**, exists for bootstrap only (§6).

## 4. Containers

`containers/c.0` … `containers/c.3` — **four** files, pre-created empty when the world is
created. The pool size is fixed; a container grows by append and shrinks by truncation, and is
never created or removed after bootstrap.

**Deviation from P-003's wording, stated.** P-003 says *preallocated* and *"exhaustion
backpressures … never grows the pool using an unproven primitive"*. Here the **number** of
containers is fixed, but each grows by append. Append is not an unproven primitive — it is the
journal's (§3) — so growing a pre-created file satisfies the rule's intent. Zero-filled
preallocation would add a second format state (written vs. merely reserved) and prove nothing
extra.

### 4.1 Frame

A container is a sequence of frames, back to back from offset 0, with no file header. An empty
container is a zero-length file.

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | magic `0x314D415246535254` — `"TRSFRAM1"` little-endian |
| 8 | 8 | **frame offset**: this header's own position in the container, u64 LE |
| 16 | 8 | body length, u64 LE |
| 24 | 4 | version, u32 LE, currently 1 |
| 28 | 4 | reserved, zero |
| 32 | 8 | XXH3-64 of bytes `[0, 32)` of this header |
| 40 | *body length* | **a pack image, exactly P-004 §13.3**, its offsets relative to the body |

The body is a pack, byte for byte. §13.3's format and §13.4's validity rules are reused
unchanged, with "file length" read as "body length"; this packet adds a frame and amends no
existing byte table.

**The frame offset field is what makes stale bytes harmless.** A frame is valid only at the
position it names, so the remains of an older, longer frame left beyond a truncation point can
never be mistaken for a frame at a new position.

### 4.2 Scan

At open, each container is walked from offset 0. A frame's **header** is accepted if its
checksum, magic, version and self-offset hold and its body fits in the file. **The first header
that fails ends the scan**, and its offset is the container's **valid end**. Everything past it
is a torn tail: written by an append whose flush never returned, therefore never named by a root.

A frame whose header holds but whose body fails P-004 §13.4 rules 1–5 is **skipped, not a
stop** — its length is trustworthy, so the frames after it still are — and nothing in it is
believed (rule 6 still skips single entries). That covers bit rot in flushed data and a flush
that extended the file without writing its contents; without it, one flipped bit in the middle
of a container would hide every later frame.

The scan mutates nothing. Repair is lazy (§4.3).

### 4.3 Append

Before appending, the writer checks the container's size against its valid end. If they
differ — a torn tail from a previous process, or a failed append earlier in this one — it
**truncates to the valid end first**, and refuses the append if that fails. Without this rule a
frame appended after garbage would be durable, named by a root, and unreachable by the scan: the
one way this design could publish a root that boot cannot follow. The container is then
appended with one `Append` + `Flush(true)`: a capture is still one durable write (D-036).

### 4.4 Location and precedence

An object's location is `(container, absolute offset, length)`, in memory only. Where the same
digest appears more than once, the first found in `(container index, offset)` order wins, as
earlier packs won before. Every copy of a digest is the same bytes, and every load verifies the
digest, so the choice is arbitrary and fixed only for determinism.

### 4.5 Which container is written

The **active** container receives every append. At open it is the non-empty container with the
largest valid end, or `c.0` when all are empty. Which one is active is policy, not correctness:
any container is a valid place for any frame.

## 5. Retention, rewritten over containers

The mark is unchanged: both on-disk root slots, re-read, full closures loaded and decoded.
Nothing is removed until the mark completes.

1. **Legacy migration.** Live objects found in pre-P-005 files (`objects/`, `packs/`) are
   copied into one frame in the active container and flushed. **Then** every legacy file is
   deleted. Removing a name is safe in a way creating one is not: if the removal is lost to a
   power cut the file comes back, holding a byte-identical duplicate.
2. **Rotation.** If the active container holds dead bytes and an empty container exists, the
   empty one becomes active, so the old one can be compacted in step 3.
3. **Compaction.** For every non-active container holding dead bytes: its live objects are
   copied into one new frame in the active container and flushed, read back through the
   verifying load path, and **only then** is the container truncated to zero.

**Why step 3 is safe across a power cut, and the old compaction was not.** The copy is durable
before the truncation is issued, by the per-file flush contract (§3). No name is created, so
nothing can be forgotten. If the truncation is lost, both copies exist and §4.4 picks one. The
only thing a power cut can undo is the space saving.

**Gate.** `-TerrainRetentionExperiment` stays. What gated it was R-015; what remains is DEF-9 —
the pass is synchronous, on the game thread, scheduled by hand, and has no pins or epoch
protocol. That is a performance and ownership gap, not a durability one, and it is a separate
decision (§8).

## 6. Bootstrap — the one place names are still created

World creation, and opening a pre-P-005 world, create names: `base.tobj`, both root slots, both
anchor slots, the first journal segment, and the four containers. All of it happens **before
the store is opened for admission**, so no acknowledged edit can depend on a name created in
the same instant.

It is not free, and is not claimed to be: an edit acknowledged seconds after world creation
depends on those entries surviving. To make them as durable as the platform allows, creation
ends with **`SyncDirectory`** on the world directory, `roots/`, `journal/` and `containers/`:

- **Unix: `fsync` on the directory.** POSIX's documented primitive, so on the shipping server
  the bootstrap window is closed, not narrowed. A failure is an I/O error.
- **Windows: `FlushFileBuffers` on a directory handle** opened with backup semantics. The best
  Win32 offers and not a documented guarantee; a failure is logged and not fatal, because
  refusing to create worlds on a filesystem that rejects the call would trade a theoretical loss
  for a certain one.

**The residual, precisely:** on Windows only, a power cut in the seconds after a world is
created may lose its bootstrap names. The world then refuses to open rather than opening wrong,
and it holds at most the edits of those seconds. After bootstrap, no runtime operation depends
on a name being durable.

## 7. What this changes for the crash matrix

`Persistence.CrashMatrix` injects **lost and torn writes**. Beyond those, what a power cut can
additionally do is reorder or forget operations whose durability was never promised. Every
runtime mutation is now an append, an in-place overwrite, or a truncation of an existing file,
each flushed before the next is issued, so there is nothing left unpromised for a power cut to
reorder. **After bootstrap, the lost-write model is the power-loss model** — under the per-file
flush contract the journal already relies on. That is the extension of the matrix's claim R-015
was blocking. It is an argument, not a power-loss measurement, and is labelled as one.

## 8. Not decided here

- **Journal rotation** still creates a segment name. Production never rotates today. Trimming,
  when built, must rotate within a **pre-created ring of segment files**, reset by truncation —
  the same pattern as containers. Until then `Rotate` is correct but must not be called from a
  production path, and is commented as such.
- **Production retention (DEF-9)** — incremental, off-thread, pins and epochs. Not built.
- **Firmware that lies about flushes.** Out of scope for every storage design; noted.

## 9. Self-review — assume it is wrong

1. **Append after a torn tail.** Found while designing §4.3: an append after garbage publishes a
   root that boot cannot follow. Fixed by construction — the writer truncates to the valid end
   before every append, and a failure blocks the capture, not the world.
2. **Stale frames beyond a truncation point.** A later shorter frame over a torn tail leaves
   older bytes beyond it. They are not a frame at the position they would be read from, because
   the self-offset check fails; and the scan stops at the first failure anyway.
3. **Compaction of the active container.** It would copy a container into itself and then
   truncate it. Forbidden: only non-active containers are compacted, and rotation happens first.
4. **The in-memory map after compaction.** It must repoint every copied digest *before* the
   truncation, or a load in the same process resolves into a zero-length file. Tested.
5. **A torn truncation.** Truncation either happened or did not; a power cut cannot leave half
   a file length. If it is lost, both copies exist and §4.4 decides.
6. **Unbatched stores.** Previously wrote a loose object — a runtime name. Now an unbatched
   store is a one-object frame. No code path writes `objects/` or `packs/` any more; a test
   asserts that an opened world performs **zero** `WriteNew` and zero `Delete` operations across
   captures and a full retention pass.
7. **What is still trusted and not proved**, restated so no later document inflates it: the
   per-file flush contract (§3), and on Windows the bootstrap window (§6).

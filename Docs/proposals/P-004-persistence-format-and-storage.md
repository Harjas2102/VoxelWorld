→ No action. For your reading only.

# P-004 — Persistence exact format and storage specification (schema 2)

**Status: specification, 2026-09-20. Implemented by the codec increment in the same session.**
**Author:** Claude Opus. **Risk:** R3. **Runtime base:** `21e3a2c`. **Doc base:** `b9104c0`.
**Authority:** P-003 §§1–7, adopted 2026-09-20 under D-023/D-032, and `ARCHITECTURE.md` §4.7.
This packet is the prerequisite P-003 §8 item 1 names: *"exact format/storage specification,
including protocol 2 field table, schema-2 offsets/caps/golden fixtures, per-checkpoint
object counts, Windows namespace/rotation guarantees, storage-owner lifetime and module
dependency boundaries."* It fixes bytes. It does not re-open the architecture, and where a
question is architectural it defers to P-003 rather than answering it again.

This document specifies **only the durable byte formats and the rules for validating them**.
The commit path, capture pump, settlement worker, recovery driver and retention/GC are the
increments after it, and nothing here claims they exist.

---

## 0. What this packet decides, and what it deliberately does not

**Decided here.** Every byte offset, width, cap, ordering rule, checksum, digest and
validation step for schema 2; the canonical intent and record digests; journal framing,
sealing and anchor contents; the chunk-key index transform and page layout; the file naming
rule; the module boundary; and the error taxonomy a decoder returns.

**Not decided here, by design.** Whether Windows guarantees durable new-name publication
(§12); the SQLite entity-store schema (it is the settlement increment's, and the terrain
formats bind to it only through `(WorldId, StoreEpoch, OpSeq)`); the economic delta *meaning*
(DEF-6/step 6 — schema 2 reserves the field and the prototype writes `NoEconomy`); and any
performance claim. §7.1's 8 ms budget and P-003's measurement gates are untouched.

**One thing this packet adds to the adopted architecture, and says so plainly.** P-003 §6
requires the base descriptor to bind "generator identity/version". Version numbers are
maintained by hand and can be forgotten; `UTerrainSettings::GeneratorVersion` is a config
integer a human bumps. Schema 2 therefore also binds a **32-byte `GeneratorParamsDigest`**
over the generator's canonical parameter encoding, so a changed `FTerrainWorldFieldParams`
with a forgotten version bump fails the exact-base check instead of silently reinterpreting
every Empty chunk. This strengthens an adopted requirement; it does not change one.

---

## 1. Conventions

1. **Little-endian, field by field, in the order of each table below.** No `memcpy` of a
   struct, no `sizeof` of a struct, no compiler layout dependency, ever. This is the rule
   `TerrainOp.cpp` already follows and the reason the 58-byte op survived a 5.7→5.8 engine
   change unremarked.
2. **No floating point is persisted anywhere in schema 2.** Values that are floats in
   config cross as exact integers: voxel size and world origin are **micrometres**
   (`int64`). A format that stored `float VoxelSizeCm` would make save compatibility depend
   on `50.0f` decoding identically on every toolchain, which is exactly the class of risk
   R-014 is open about.
3. **Signed integers** round-trip through their unsigned counterpart; `static_cast` back is
   well-defined in C++20, which is what the project compiles as.
4. **Enums** cross as `uint8`/`uint16` and are range-checked on the way in. A value this
   build does not define is a decode failure, never a silent cast to the zero enumerator.
5. **Reserved bytes are zero on write and MUST be zero on read.** A nonzero reserved byte is
   `ReservedNotZero`, not a forward-compatibility escape hatch. Schema 2 has no optional
   fields and no TLV: every object's length is a function of its declared counts. If a field
   is needed later, it takes a new schema version and a registered decoder.
6. **Content digest = BLAKE3-256** (`FBlake3`, 32 bytes, full width). **Framing checksum =
   XXH3-64** (`FXxHash64`, 8 bytes LE). Both are vendored in `Core`, both are specified
   algorithms rather than engine-internal hashes, and neither is `FCrc::MemCrc32` — whose
   value is an Unreal implementation detail and an unacceptable thing to write into a file
   that must outlive an engine upgrade.
7. **Digests identify content; checksums detect damage.** An index entry references a payload
   by digest. A file on disk is protected by checksums so a torn or bit-rotted object is
   diagnosed at the frame level before anyone hashes 128 KiB of it.
8. **Validation is fail-closed and ordered.** Length → magic → schema → header size → header
   checksum → object type → identity (`WorldId`, `StoreEpoch`, base digest) → body length
   against caps → body checksum → field ranges → ordering/uniqueness → exact-consumption.
   A decoder never partially writes its output: the caller's struct is assigned only after
   the whole decode succeeds.
9. **Exact consumption.** Every object and record declares its length, and a decode that
   does not consume exactly that many bytes is `TrailingBytes`. There is no "ignore the
   rest" path, because that is how two builds come to disagree about what a save means.
10. **The format contains no file paths.** Objects are named by their digest and nothing
    else (§11). This is what lets §12's storage-mode question stay open without touching a
    single byte table.
11. **A field rule that a conforming decoder enforces is written down in the section that
    defines the field.** A byte table alone is not a specification: two implementations can
    agree on every offset and still disagree about which files are valid, and the more
    permissive one is the one that accepts a corrupt world. Each section below therefore
    carries its own "field rules" paragraph, and anything not stated there is **not** a
    requirement a decoder may invent.

### 1.1 Error taxonomy

`ETerrainPersistError`, returned by every decoder. It exists so that a corrupt-fixture test
can assert *which* defence fired, not merely that something was rejected:

`None`, `ShortBuffer`, `BadMagic`, `UnsupportedSchema`, `BadHeaderSize`, `UnknownObjectType`,
`HeaderChecksumMismatch`, `BodyChecksumMismatch`, `BodyLengthOutOfRange`, `WorldMismatch`,
`EpochMismatch`, `BaseMismatch`, `TrailingBytes`, `FieldOutOfRange`, `ReservedNotZero`,
`OrderViolation`, `DuplicateKey`, `EncodingNotPermitted`, `CapExceeded`, `TornTail`.

`TornTail` is reserved for the one place P-003 §3 permits an incomplete physical tail: the
end of the active journal segment. Everywhere else an incomplete object is `ShortBuffer`.

---

## 2. Identity types

| Type | Width | Meaning |
|---|---|---|
| `FTerrainWorldId` | 16 B | Immutable random 128-bit world identity (P-003 §1). Never inferred from a directory name. |
| `FTerrainStoreEpoch` | 16 B | Random 128-bit lineage ID; a coherent restore or migration mints a new one for the whole output store (P-003 §5). |
| `FTerrainDigest` | 32 B | BLAKE3-256. A zero digest means "absent" only where a table says so. |

Both 128-bit identities are opaque byte arrays, compared with `Memcmp`, written raw. They are
**not** `FGuid`: `FGuid`'s four-`uint32` layout and its string forms are an engine convention,
and a 16-byte opaque identity has no byte-order question to get wrong.

`WorldTag` (8 B) = `XXH3-64(WorldId || StoreEpoch)`. It appears in each journal record so that
a record spliced out of another world's segment is rejected even though records themselves
carry no full header (§9.2).

---

## 3. Common object header — 96 bytes, every standalone object

| Off | Size | Field | Notes |
|---:|---:|---|---|
| 0 | 4 | `Magic` | ASCII `VXTP` = `0x56 0x58 0x54 0x50` |
| 4 | 2 | `SchemaVersion` | `uint16` = **2**. Version 1 is reserved as the withdrawn legacy sketch and is **never emitted**; reading it is `UnsupportedSchema` |
| 6 | 1 | `ObjectType` | §3.1 |
| 7 | 1 | `HeaderSize` | = **96**. Any other value is `BadHeaderSize` |
| 8 | 16 | `WorldId` | |
| 24 | 16 | `StoreEpoch` | |
| 40 | 32 | `BaseDigest` | BLAKE3 of the base-descriptor **body** (§4). Binds every object to the exact world shape |
| 72 | 8 | `BodyLength` | `uint64` LE, bytes following the header |
| 80 | 8 | `BodyChecksum` | XXH3-64 of body bytes `[0, BodyLength)` |
| 88 | 8 | `HeaderChecksum` | XXH3-64 of header bytes `[0, 88)` |
| | **96** | | |

The header checksum covers the header only, so a decoder can trust `BodyLength` *before*
allocating or reading `BodyLength` bytes. That ordering is the whole reason the two checksums
are separate: a corrupted length field must not be able to make a reader allocate 2^63 bytes.

### 3.1 `ETerrainPersistObjectType`

| Value | Type | Body |
|---:|---|---|
| 1 | `BaseDescriptor` | §4 |
| 2 | `ChunkPayload` | §5 |
| 3 | `IndexPage` | §6 |
| 4 | `CheckpointDescriptor` | §7 |
| 5 | `RootSlot` | §8, fixed 4000-byte body |
| 6 | `JournalSegmentHeader` | §9.1 |
| 7 | `JournalAnchorSlot` | §9.5, fixed 4000-byte body |

Value 0 and values above 7 are `UnknownObjectType`. A decoder for a specific type also
rejects a well-formed header of the wrong type with `UnknownObjectType`, so a root slot can
never be read as a checkpoint descriptor.

**The base descriptor is the one object whose header carries its own `BaseDigest`**, equal to
the BLAKE3 of its own body. That is self-consistent, checkable in one step, and removes a
bootstrap special case from every other validator.

---

## 4. Base descriptor (type 1) — the exact world shape

Fixed prefix 122 bytes, then variable-length names and stamps.

| Off | Size | Field | Notes |
|---:|---:|---|---|
| 0 | 8 | `Seed` | `int64`, sign-extended from `UTerrainSettings::Seed` |
| 8 | 4 | `GeneratorVersion` | `uint32` |
| 12 | 4 | `BackendKernelVersion` | `uint32` — the adapter's density-kernel compatibility number (§4.10) |
| 16 | 32 | `GeneratorParamsDigest` | BLAKE3 over the generator's canonical parameter encoding (§0, §4.1) |
| 48 | 24 | `OriginWorldMicrometres` | `int64[3]` — voxel (0,0,0) in world space, exact |
| 72 | 8 | `VoxelSizeMicrometres` | `int64`, > 0. 50 cm = `500000` |
| 80 | 4 | `ChunkSizeVox` | `int32` = 32 (K2/D-024). A different value is a different world |
| 84 | 24 | `WorldBoundsVox` | `int32[6]`: `Min.X, Min.Y, Min.Z, Max.X, Max.Y, Max.Z`, half-open |
| 108 | 1 | `ValueConfig` | `uint8`, mirrors the backend value-config flag |
| 109 | 1 | `EncodingRulesVersion` | `uint8` = 1 — the §5.3 encoding-choice rule |
| 110 | 2 | `MaterialCatalogVersion` | `uint16` |
| 112 | 2 | `MaterialCatalogCount` | `uint16` — `ETerrainMaterial::Count`, currently 8 |
| 114 | 2 | `GeneratorNameLength` | `uint16`, ≤ 128 |
| 116 | 2 | `BackendNameLength` | `uint16`, ≤ 128 |
| 118 | 2 | `AuthoredStampCount` | `uint16`, ≤ 1024 (currently 0 — P-003 §6's "explicit empty list") |
| 120 | 2 | `Reserved` | zero |
| 122 | *n* | `GeneratorName` | UTF-8, no NUL, exactly `GeneratorNameLength` bytes |
| … | *m* | `BackendName` | UTF-8, no NUL |
| … | 32·k | `AuthoredStamps` | ordered BLAKE3 digests |

Whole body capped at 8192 bytes. `WorldBoundsVox` must be non-empty on every axis.
`VoxelSizeMicrometres` and `ChunkSizeVox` must be positive.

### 4.1 `GeneratorParamsDigest`

The generator owns its own canonical encoding and hands the digest to the persistence layer;
`TerrainCore`'s format code never reaches into `FTerrainWorldFieldParams`. The rule the
generator must follow is the same as everywhere else in this packet — little-endian, field by
field, declaration order — and because the field's parameters are `double`, they are digested
as **IEEE-754 binary64 bit patterns in LE order**, which is exact and reproducible. Digesting
a double's bit pattern is safe in a way *storing a float as a world coordinate* is not: the
digest is compared for equality, never used for arithmetic.

---

## 5. Chunk payload (type 2)

| Off | Size | Field | Notes |
|---:|---:|---|---|
| 0 | 12 | `Key` | `int32 X, Y, Z` |
| 12 | 4 | `Rev` | `uint32` |
| 16 | 8 | `LastOpSeq` | `uint64` |
| 24 | 1 | `Encoding` | 0 = `Dense`, 1 = `SparseDiff`. **2 (`Empty`) is `EncodingNotPermitted`** — see §5.2 |
| 25 | 4 | `GeneratorVersion` | `uint32`, must equal the base descriptor's |
| 29 | 1 | `ValueConfig` | `uint8`, must equal the base descriptor's |
| 30 | 2 | `Reserved` | zero |
| 32 | … | encoding body | §5.1 |

### 5.1 Encoding bodies

**Dense — exactly 131,072 bytes.** `int16[32768]` densities, then `uint16[32768]` game
material IDs, local index `x + 32*y + 1024*z`. **This is byte-for-byte the §4.2 Dense region
transfer layout**, re-homed here as P-003 §7 requires. One layout serves replication and
disk; there is no conversion step between them and therefore no way for them to drift apart.

**SparseDiff — 4 + 6·N bytes.**

| Off | Size | Field |
|---:|---:|---|
| 0 | 4 | `SampleCount` `uint32`, 1 ≤ N ≤ 32768 |
| 4 | 6·N | entries: `LocalIndex uint16`, `Density int16`, `MaterialId uint16` |

Entries are sorted **strictly ascending by `LocalIndex`**; equal or descending is
`OrderViolation`, which also makes duplicates impossible. `LocalIndex` ≥ 32768 is
`FieldOutOfRange`. Samples are **absolute replacement values**, not deltas (P-003 §6) — the
name is historical and the meaning is "the samples that differ from the base".
`SampleCount == 0` is `FieldOutOfRange`: a chunk with nothing changed is Empty, not an empty
SparseDiff, and permitting both would give one state two encodings.

Total body: Dense 131,104 B; SparseDiff 36 + 6N B.

### 5.2 Why `Empty` has no payload object

P-003 requires Empty to retain revision and last-change metadata. Schema 2 carries that
metadata in the **index leaf entry** (§6.2), which every Empty chunk already needs. Giving
Empty a payload object as well would mean one logical state had two on-disk spellings, and
every validator would then have to rule on which wins when they disagree. So the encoder
refuses `Empty` with `EncodingNotPermitted` and the index says it instead: leaf
`Encoding = 2`, `PayloadDigest` zero, `PayloadLength` zero.

### 5.3 Encoding choice (`EncodingRulesVersion` 1)

Compare **density and material** of every sample against the canonically encoded base
(P-003 §6; provenance is not equality). Then:

- zero differing samples → **Empty**;
- otherwise choose the **smaller total encoded body**, Dense on ties.

With the sizes above, SparseDiff wins iff `36 + 6N < 131104`, i.e. **N ≤ 21844**. A tie is
arithmetically impossible (`36 + 6N ≡ 0 mod 6`, `131104 ≡ 4 mod 6`); the tie rule is stated
anyway so a future width change cannot make the choice ambiguous by accident.

A capture may only choose Empty from **successful full samples of a resident chunk** compared
against the exact base. P-003 records the sentinel trap this closes: `FMemoryTerrainBackend`'s
`ReadRegion` returns success with a default Empty for a nonresident chunk while
`FVPLegacyBackend` returns false, and **neither is evidence of pristine equality**.

---

## 6. Chunk-key index (type 3) — 96-bit radix, 12 levels

### 6.1 Key transform

For each signed coordinate, `u = uint32(v) XOR 0x80000000` (flip the sign bit), written
**big-endian**, 4 bytes. The key is `X || Y || Z`, exactly 12 bytes. Sign-flip plus big-endian
makes unsigned byte-wise comparison of keys agree exactly with signed component-wise
comparison, which is what lets a radix page hold a sorted, uniquely-prefixed child set.

`X || Y || Z` rather than Morton: P-003 §8 already ruled this. The 12·D page bound does not
depend on spatial locality, and ordered-key diagnostics are easier to read.

Level *d* (0-based) consumes key byte *d*. Depth 0..10 are internal pages; **depth 11 is the
leaf page**. Traversal is exactly 12 bytes and maximum depth is 12 levels of page.

### 6.2 Page body

| Off | Size | Field | Notes |
|---:|---:|---|---|
| 0 | 1 | `Depth` | 0..11 |
| 1 | 1 | `PageKind` | 0 = Internal (Depth ≤ 10), 1 = Leaf (Depth == 11). A mismatch with `Depth` is `FieldOutOfRange` |
| 2 | 2 | `EntryCount` | 1..256. Zero is `FieldOutOfRange` — an empty page is never published |
| 4 | 12 | `KeyPrefix` | the key bytes above this page; **bytes at index ≥ `Depth` MUST be zero** (`ReservedNotZero`) |
| 16 | … | entries | |

**Internal entry — 37 bytes:** `ByteValue uint8`, `ChildDigest[32]`, `ChildLength uint32`.
**Leaf entry — 50 bytes:** `ByteValue uint8`, `Encoding uint8` (0/1/2), `Rev uint32`,
`LastOpSeq uint64`, `PayloadLength uint32`, `PayloadDigest[32]`.

Entries are sorted **strictly ascending by `ByteValue`** (`OrderViolation` otherwise, which
subsumes duplicate detection). A leaf entry with `Encoding == 2` (Empty) must have a zero
`PayloadDigest` and `PayloadLength == 0`; a non-Empty entry must have a nonzero
`PayloadLength` within the §5 cap.

An internal entry must have a **nonzero `ChildDigest`** and a **nonzero `ChildLength`** no
greater than `96 + 32,768`, the largest a page object can be. A zero digest is the format's
"absent" marker and an entry that is present cannot also be absent; a zero length names an
object that cannot exist. Both are `FieldOutOfRange`.

Maximum page body: internal 16 + 256·37 = 9,488 B; leaf 16 + 256·50 = 12,816 B. The
validation cap is **32,768 B**, as P-003 §4 states, with the measured maxima well inside it.

### 6.3 Path copying, sharing and validation

A checkpoint that changes D keys rewrites at most **12·D** pages before shared-prefix
coalescing. Changed pages are written as **new immutable objects**; unchanged subtrees are
referenced by their existing digests and are not rewritten, read or re-hashed.

Validation on boot walks the closure from the descriptor and checks, at every page:
declared depth against traversal depth, `KeyPrefix` against the bytes actually traversed,
entry ordering, counts, child lengths, and **every referenced payload's actual content
digest** (P-003 §4: boot validation is eager and verifying, and the cost is
O(index bytes + unique payload bytes) by explicit choice). A child digest reachable at two
different prefixes is a cross-prefix reference and is rejected; because a page's identity is
its content digest and its content includes its own `KeyPrefix` and `Depth`, legitimate
sharing of a subtree between two prefixes is impossible by construction, and a cycle would
require a digest to contain itself.

---

## 7. Checkpoint descriptor (type 4) — 80-byte body

| Off | Size | Field | Notes |
|---:|---:|---|---|
| 0 | 8 | `G` | `uint64` — the global cut |
| 8 | 8 | `Generation` | `uint64`, strictly increasing per publication |
| 16 | 8 | `CreatedUtcMillis` | `int64`, diagnostic |
| 24 | 4 | `Flags` | bit0 `HasRootPage`; other bits zero |
| 28 | 4 | `RootPageLength` | `uint32`; 0 iff `!HasRootPage` |
| 32 | 32 | `RootPageDigest` | zero iff `!HasRootPage` |
| 64 | 8 | `LeafKeyCount` | `uint64`, advisory accounting |
| 72 | 8 | `TotalPayloadBytes` | `uint64`, advisory accounting |

`HasRootPage` clear is the legal, required state of the **empty G=0 checkpoint that world
creation publishes before any edit is admitted** (P-003 §4). Validation cap 16,384 B.

When `HasRootPage` is set, `RootPageLength` must additionally be no greater than
`96 + 32,768` — the largest a page object can be — and `RootPageDigest` must be nonzero.
Violations are `FieldOutOfRange`. A bit outside bit0 of `Flags` is `ReservedNotZero`.

Advisory fields are advisory: a mismatch between `LeafKeyCount` and the walked closure is a
logged diagnostic, not a boot refusal, because the closure is the authority and a second
authority for the same fact is how inconsistencies become unbootable worlds.

---

## 8. Root slot (type 5) — fixed 4096-byte file, 4000-byte body

Two pre-created fixed-size files, overwritten in place, never renamed (P-003 §4).

| Off | Size | Field |
|---:|---:|---|
| 0 | 8 | `Generation` `uint64` |
| 8 | 8 | `G` `uint64` |
| 16 | 32 | `DescriptorDigest` |
| 48 | 4 | `DescriptorLength` `uint32` |
| 52 | 2 | `StoreFormatVersion` `uint16` = 2 |
| 54 | 2 | `Reserved` zero |
| 56 | 8 | `PublishedUtcMillis` `int64` |
| 64 | 3936 | `Reserved`, all zero |
| | **4000** | |

**Field rules.** `DescriptorDigest` must be nonzero and `DescriptorLength` must be nonzero
and no greater than `96 + 16,384`, the largest a checkpoint-descriptor object can be
(`FieldOutOfRange`) — a root that names nothing, or names something that cannot exist, is not
a root. A `StoreFormatVersion` other than 2 is `UnsupportedSchema`, not `FieldOutOfRange`:
it means a build that does not know this layout wrote the slot, which is the same condition
the header's own schema field reports. Every byte of the reserved tail must be zero
(`ReservedNotZero`).

`BodyLength` is 4000, so the body checksum covers the **whole slot** including its reserved
tail: P-003's "whole-slot checksum", obtained without a second checksum field. A torn
overwrite therefore fails the body checksum and the other slot is used. Boot selects the
**highest `Generation` among slots that fully validate** — including their referenced
closure — and if either slot fails, reclamation is disabled until redundancy is repaired.

---

## 9. Journal

### 9.1 Segment header (type 6) — 72-byte body

Written and flushed, with its namespace, **before any record is appended** (P-003 §3).

| Off | Size | Field |
|---:|---:|---|
| 0 | 8 | `SegmentId` `uint64` |
| 8 | 8 | `FirstOpSeq` `uint64` |
| 16 | 8 | `PredecessorSegmentId` `uint64` |
| 24 | 32 | `PredecessorSealDigest` |
| 56 | 8 | `CreatedUtcMillis` `int64` |
| 64 | 4 | `Flags` — bit0 `HasPredecessor`; other bits zero |
| 68 | 4 | `Reserved` zero |

**Field rules.** `SegmentId` and `FirstOpSeq` are both **nonzero** (`FieldOutOfRange`).
Sequence numbering is 1-based, so a segment claiming to start at `OpSeq` 0 has no first
record; and segment 0 is reserved because §9.5's anchor uses `PredecessorSegmentId == 0` to
mean "none", and one value cannot also name a real segment.

`HasPredecessor` clear requires `PredecessorSegmentId == 0` and a zero seal digest, and is
legal only for the world's first segment, whose `FirstOpSeq` is 1. Set, it requires a nonzero
`PredecessorSegmentId` and a nonzero seal digest. A bit outside bit0 of `Flags` is
`ReservedNotZero`.

**The header is never rewritten.** Sealing appends a record (§9.4) instead of flipping a bit
in a header that has already been flushed, so no durable object in schema 2 is ever modified
in place except the four fixed slots — and those are the only places a torn write has a
surviving redundant copy.

### 9.2 Record frame — 20 bytes of framing

| Off | Size | Field |
|---:|---:|---|
| 0 | 4 | `RecordMagic` ASCII `VXJR` |
| 4 | 4 | `RecordLength` `uint32` — **total** framed bytes including magic and checksum |
| 8 | 1 | `RecordType` — 0 `Commit`, 1 `Seal` |
| 9 | 1 | `RecordVersion` = 2 |
| 10 | 2 | `Reserved` zero |
| 12 | … | body |
| `RecordLength-8` | 8 | `Checksum` XXH3-64 over `[0, RecordLength-8)` |

`RecordLength` is capped at **131,072 B** and must be at least 20 + the type's minimum body.
Records carry no 96-byte object header: they inherit `WorldId`/`StoreEpoch`/`BaseDigest` from
their segment header, which is validated first. The 8-byte `WorldTag` at the head of each
body is the splice check that identity inheritance would otherwise lose — cheap, and it means
a record lifted from another world's segment fails even though its own checksum is intact.

**Torn tail.** Only the final record of the **active** segment may be incomplete, and only in
these four forms:

1. fewer than 12 bytes remain;
2. `RecordLength` exceeds the bytes remaining;
3. the checksum fails on a frame that reaches **exactly** end of file;
4. the remaining bytes are **all zero** — a torn append that extended the file's valid data
   length without writing the record.

Any of those is `TornTail`: the file is truncated to the last good record **after the torn
bytes are preserved diagnostically**, and H is the last complete contiguous sequence.

Form 3 is stated as "reaches exactly end of file" rather than "fails its checksum", because
the two are not the same and treating them as the same is a bug: a damaged record in the
*middle* of a segment also fails its checksum, and silently truncating there would discard
acknowledged history that follows it. A frame whose length does not reach end of file is
`BodyChecksumMismatch` and fails closed, as does any non-zero garbage that is not a valid
frame header. On a sealed or otherwise inactive segment, none of the four forms is tolerated.

### 9.3 Commit record body — 140-byte prefix, then three arrays

| Off | Size | Field | Notes |
|---:|---:|---|---|
| 0 | 8 | `WorldTag` | §2 |
| 8 | 8 | `ServerUtcMillis` | `int64`, **diagnostic only** — never an ordering authority |
| 16 | 58 | `Op` | the existing 58-byte codec, unchanged (§4.2) |
| 74 | 16 | `TokenDigest` | BLAKE3 of the live admission token, **truncated to 16 B**. The live token is never persisted (P-003 §1) |
| 90 | 4 | `RequestId` | `uint32`, positive |
| 94 | 2 | `ChildOrdinal` | `uint16`, 0-based |
| 96 | 2 | `ChildCount` | `uint16`, ≥ 1; `ChildOrdinal < ChildCount` |
| 98 | 32 | `IntentDigest` | §9.6 |
| 130 | 1 | `EconomyKind` | 0 `NoEconomy`, 1 `ExactDeltas` |
| 131 | 1 | `PhysicalAvailability` | 0 `Unavailable`, 1 `Measured` |
| 132 | 2 | `PhysicalCount` | ≤ 64 |
| 134 | 2 | `ChangedKeyCount` | ≤ 4096 |
| 136 | 2 | `EconomyDeltaCount` | ≤ 64; **must be 0 when `EconomyKind == NoEconomy`** |
| 138 | 2 | `EconomyPolicyVersion` | must be 0 when `NoEconomy` |
| 140 | 10·p | `Physical[]` | `MaterialId uint16`, `MicroLitres int64` — signed, + removed / − placed |
| … | 20·c | `ChangedKeys[]` | `X int32, Y int32, Z int32, BeforeRev uint32, AfterRev uint32` |
| … | 32·e | `EconomyDeltas[]` | `OwnerId uint64, ContainerId uint64, ItemId uint32, Count int64, Flags uint32` |

`PhysicalAvailability == Unavailable` requires `PhysicalCount == 0`. This is the field that
keeps P-003 §2's rule enforceable in bytes: the step-4 prototype **must not encode "unknown"
as a measured zero**, and a decoder can tell the difference.

`ChangedKeys` are sorted **strictly ascending by the §6.1 12-byte key** and unique
(`OrderViolation`). A zero-change commit has `ChangedKeyCount == 0` and is legal — P-003 §2
step 1 keeps "a successful zero-change op may commit and consumes a sequence". `AfterRev`
must be > `BeforeRev` for every entry of a changed-key list.

The 4096 cap matches `TerrainChunkKeysForBox`'s `MaxKeys` default, which is the admission
path's own ceiling, and comfortably covers the audited worst-aligned box-child coverage of
2,052 chunks. Worst-case record: 20 + 140 + 640 + 81,920 + 2,048 = **84,768 B**, inside the
131,072 B cap with 35% headroom.

**`RecordDigest`** = BLAKE3 over the body bytes `[12, RecordLength-8)`. This is the digest
P-003 §1 means by *"record digest must match on duplicates"* for the `(WorldId, OpSeq)`
settlement key. It covers `ServerUtcMillis`, deliberately: two records claiming the same
`OpSeq` with different timestamps are two different records, and that is corruption worth
detecting rather than tolerating.

### 9.4 Seal record body — 64 bytes

| Off | Size | Field |
|---:|---:|---|
| 0 | 8 | `WorldTag` |
| 8 | 8 | `LastOpSeq` `uint64` |
| 16 | 8 | `CommitRecordCount` `uint64` |
| 24 | 32 | `RecordsDigest` |
| 56 | 8 | `SealedUtcMillis` `int64` |

`RecordsDigest` = BLAKE3 over the concatenation of every **commit** record frame in the
segment, in append order — computable incrementally as records are written, so sealing costs
no re-read. A seal record is the last record in its segment; anything after it is
`OrderViolation`. The next segment's `PredecessorSealDigest` is the BLAKE3 of the **seal
record frame** itself, and the anchor repeats it, giving the "continuity evidence" P-003 §5
requires at both ends of the join.

### 9.5 Anchor slot (type 7) — fixed 4096-byte file, 4000-byte body

| Off | Size | Field |
|---:|---:|---|
| 0 | 8 | `AnchorGeneration` `uint64` |
| 8 | 8 | `ActiveSegmentId` `uint64` |
| 16 | 8 | `ActiveSegmentFirstOpSeq` `uint64` |
| 24 | 32 | `PredecessorSealDigest` |
| 56 | 8 | `PredecessorSegmentId` `uint64` |
| 64 | 8 | `PredecessorLastOpSeq` `uint64` |
| 72 | 8 | `PublishedUtcMillis` `int64` |
| 80 | 4 | `Flags` — bit0 `HasPredecessor` |
| 84 | 4 | `Reserved` zero |
| 88 | 3912 | `Reserved`, all zero |
| | **4000** | |

**Field rules.** `ActiveSegmentId` and `ActiveSegmentFirstOpSeq` are both **nonzero**, for
the reasons §9.1 gives. `HasPredecessor` clear requires `PredecessorSegmentId == 0`, a zero
seal digest **and** `PredecessorLastOpSeq == 0`; set, it requires a nonzero segment ID and a
nonzero seal digest. Violations are `FieldOutOfRange`, a stray `Flags` bit is
`ReservedNotZero`, and every byte of the reserved tail must be zero.

Two pre-created slots, same publication discipline and same whole-slot checksum argument as
§8. Rotation order, normative: create and flush the new segment's header and namespace →
publish and flush the **inactive** anchor slot → append records. The preceding segment is
sealed before rotation. Boot takes the highest-`AnchorGeneration` slot that validates; a
named-but-missing segment is corruption, **never "no more ops"**; a newer unanchored empty
segment is an orphan to be ignored; a newer unanchored **nonempty** segment is a protocol
violation that refuses ordinary boot and is only ever considered by the explicit offline
repair path (P-003 §3).

### 9.6 Canonical intent digest

`IntentDigest` = BLAKE3 over exactly, in order:

1. the 58-byte `Op` encoding **with the `OpSeq` field's 8 bytes set to zero** — the intent
   exists before a sequence is assigned, so including the assigned sequence would make the
   digest useless for the one job it has;
2. `TokenDigest` (16 B);
3. `RequestId` (4 B LE);
4. `ChildOrdinal` (2 B LE);
5. `ChildCount` (2 B LE).

82 bytes in, 32 out. This is what P-003 §1 means by *"a digest mismatch against queued/cached
identity rejects rather than silently changing intent"*, and it is diagnostic: it is **not** a
cross-session retry key, and `StaleSession`/`StaleRequest` remain non-proof that an earlier
edit did not commit.

---

## 10. Caps, collected

| Constant | Value | Source |
|---|---:|---|
| `SchemaVersion` | 2 | P-003 §7 |
| `ObjectHeaderSize` | 96 | §3 |
| `SlotSize` | 4096 | P-003 §4 |
| `MaxBaseDescriptorBody` | 8,192 | this packet |
| `MaxChunkPayloadBody` | 131,104 | §5.1 |
| `MaxIndexPageBody` | 32,768 | P-003 §4 |
| `MaxCheckpointDescriptorBody` | 16,384 | P-003 §4 |
| `MaxIndexEntriesPerPage` | 256 | P-003 §4 |
| `IndexKeyBytes` / `MaxDepth` | 12 / 12 | P-003 §4 |
| `MaxJournalRecordBytes` | 131,072 | this packet (§9.3 worst case 84,768) |
| `MaxChangedKeysPerRecord` | 4,096 | `TerrainChunkKeysForBox` default |
| `MaxPhysicalEntriesPerRecord` | 64 | this packet |
| `MaxEconomyDeltasPerRecord` | 64 | this packet |
| `MaxSparseSamples` | 32,768 | one chunk |

### 10.1 Bytes per edit, computed (not measured)

P-003 §7 withdraws the old ~122 B/edit estimate and requires schema-2 bytes/edit to be
measured. Schema 2's **computed** figures, which the codec test prints so the number is in a
log rather than in a comment: a radius-4 solid dig touching 8 chunks with 3 physical material
entries and `NoEconomy` is `20 + 140 + 30 + 160 = 350 B` of journal. Checkpoint cost is
dominated by payloads: a single Dense chunk object is 131,200 B, and 12·D index pages at
≤ 12,816 B each. Real bytes/edit under the §7.1 workload remains a measurement gate.

---

## 11. Files, naming and module ownership

### 11.1 Layout

```
<WorldDir>/
  base.tobj                      base descriptor (type 1)
  roots/root.0  roots/root.1     4096 B slots (type 5), pre-created
  journal/anchor.0  anchor.1     4096 B slots (type 7), pre-created
  journal/seg-%016llx.tjs        segment header object, then framed records
  objects/<hh>/<64-hex>.tobj     content-addressed payloads, index pages, descriptors
  entities.db                    SQLite (settlement increment)
  world.json                     advisory status only; never a sequence authority
```

`<hh>` is the first byte of the digest in lowercase hex — a 256-way fan-out so one directory
never holds millions of entries.

**An object's name is derived from its digest by a fixed rule and from nothing else.** The
digest is 32 bytes, rendered as exactly 64 lowercase hex characters. A reader that is handed
an object ID validates it as 64 hex characters *before* it becomes part of a path, and after
loading verifies that the content actually hashes to that digest. There is no code path in
which a byte from a file becomes a path component without that check. This is P-003 §4's
*"reference paths are derived from opaque object IDs, never untrusted arbitrary paths"*,
stated as something a reviewer can check by grep.

### 11.2 Module ownership and dependency boundary

Everything in this packet is implemented in **`TerrainCore`**, whose `Build.cs` depends on
`Core`, `CoreUObject` and `Engine` and on nothing else. The codecs use only `Core`
(`FBlake3`, `FXxHash64`, `TArray`, `TArrayView`) and compile and test headless with no engine
world, no plugin and no file system.

File handles live behind **`ITerrainStorageDevice`** in `TerrainStorage.h`, which the codec
headers do not include — the format stays testable with no file system, and the device stays
replaceable by an in-memory or fault-injecting implementation. Its surface is six mutating
operations plus two reads, deliberately small: every mutating one is a place a crash can
happen, and a smaller surface is a smaller crash matrix.

Above it: **`FTerrainJournalWriter`** owns the active segment, the anchor pair and the
rotation ordering below, and **`FTerrainWorldStore`** owns a world directory — create, open
and checkpoint publication. SQLite and the commit path arrive later, behind the same
discipline.

**A rotation consumes its segment ID even when it fails.** If the new segment's header is
written and the anchor publication then fails, the file exists as an orphan and rewriting it
is refused, because an immutable object is never overwritten. A retry therefore uses the next
ID. That is the safe failure — the alternative is a writer that overwrites a file it cannot
prove is its own — and reclaiming an orphan's ID belongs to retention. No plugin type, no `UObject` and
no engine-asset type is ever persisted or crosses the adapter boundary (D-011, §4.1.0).

---

## 12. Durable publication on Windows — stated, not assumed

P-003 §4 requires this packet to establish namespace-durability ordering on the tested
filesystem, and defines a preallocated-container fallback if it cannot be established.

**What is verified.** UE 5.8's `FWindowsPlatformFile` routes `IFileHandle::Flush` to
`FlushFileBuffers` (`WindowsPlatformFile.cpp:933`). That proves file **contents** reach the
device for an open handle. It proves nothing about **directory-entry** durability for a newly
created name, and Win32 exposes no directory-flush primitive at all.

**A flush rule this packet fixes, found by reading both platform implementations.**
`IFileHandle::Flush(bool bFullFlush = false)` documents `false` as letting "the operating/file
system have more leeway about when the data actually gets written to disk". The two platforms
this project targets do **not** treat the parameter the same way:

| Platform | `Flush(false)` | `Flush(true)` |
|---|---|---|
| Windows (`WindowsPlatformFile.cpp:933`) | `FlushFileBuffers` — the parameter is **ignored** | `FlushFileBuffers` |
| Unix (`UnixPlatformFile.cpp:323`) | `fdatasync` | `fsync` |

`fdatasync` synchronises data but makes no promise about **metadata**, and a file's length is
metadata. **Every object write and every journal append extends a file**, and D-002 makes a
Linux dedicated server the shipping target. A defaulted `Flush()` would therefore be correct
on the machine this is developed on and silently wrong on the machine it ships to — the worst
shape a durability bug can have, because no amount of testing on Windows would find it.

**Normative: every durable write in this store ends with `Flush(true)`**, and a failed flush
is an I/O error, never a success. `ITerrainStorageDevice` is the only place that calls it.

**A second rule, for the same reason.** A slot is published with an **in-place overwrite that
does not truncate**: the handle is opened in append mode and seeked to zero, because opening
for write with truncation makes the file briefly zero-length, and a slot that can be *absent*
breaks the one property the two-slot protocol rests on. A length mismatch is refused rather
than truncating to fit.

**Ruling for this packet.** Schema 2 is specified so the question is *isolable and
deferrable*, not so it is answered by assertion. The containment half of the argument below
is now **demonstrated rather than asserted**: a torn slot publication is rejected by the
whole-slot checksum, the other slot still carries the last acknowledged generation, and the
retry repairs the damaged slot instead of touching the good one. What remains unproved is the
durability half — a fault injected in process is not a power loss.

- The four durable-publication primitives — two root slots and two anchor slots — are
  **pre-created, fixed-size, overwritten in place**. They create no new names at runtime, so
  they do not depend on namespace durability at all. Everything that decides *which* state is
  current passes through them.
- New names are created only for **immutable, content-addressed objects and segments**, which
  are referenced only *after* a slot naming them is published. If a crash loses a directory
  entry for an object written before the slot that references it, the slot's closure
  validation fails on boot and the **previous** generation's root is used. That is a lost
  checkpoint, not a corrupt world.
- Therefore **Mode A (named objects) is the specified default**, and the byte tables above are
  **mode-independent**: no format field contains a path, so adopting the
  `(container, offset, length)` fallback later changes the object *addressing map* and not one
  byte of any record. That property is the reason §1 rule 10 exists.

**What remains open, and stays open.** Whether an ordinary power-loss can lose a flushed
object's directory entry while retaining a later slot overwrite is a hardware/filesystem
question that no in-process test can settle. It is a **named acceptance gate before service
integration**, discharged by either crash-matrix evidence or by selecting Mode B. This packet
does not claim to have proved it, and the argument above is a containment argument — the
worst case is falling back one checkpoint generation — not a durability proof.

---

## 13. Packs — many objects, one durable write

### 13.1 Why this exists

Every object in §§4–7 is immutable and named by its BLAKE3 digest, and the obvious way to store
one is as a file. That was how it was built, and it is the reason a checkpoint was slow.

The cost of a durable write is not the cost of its bytes. §12 requires every write to end in
`Flush(true)`, and on the development machine that call costs about **3 ms regardless of size**:
the same for a 160-byte index page as for a megabyte. A checkpoint over 8 changed chunks writes
59 objects, of which 49 are index pages holding about **7 KB between them** — and writing that
7 KB took **0.168 s**, 85% of the whole capture.

Three alternatives were measured over 49 small files on that machine:

| Strategy | Time |
|---|---|
| write each file and flush it (what was built) | 151.6 ms |
| write all files, hold the handles, flush them all at the end | 155.8 ms |
| write and close each, then reopen each and flush | 55.6 ms |
| **all of it in one file, one flush** | **2.3 ms** |

The second row is the important one, because it is the fix that looks obvious and does not
work: a flush barrier is per *file* whenever it is called, so deferring the flushes buys
nothing. **The number of files that must be synced is the cost.** Packs reduce that number to
one per capture.

### 13.2 What a pack is, and what does not change

A pack is one file holding many object images back to back, followed by a manifest naming
each one, followed by a fixed trailer.

**Content addressing is unchanged.** An object is still named by, and verified against, its
BLAKE3 digest, on the way in and on the way out. The index still path-copies, the descriptor
still names a root page by digest, and a reference does not record whether its target is loose
or packed. Only *where the bytes live* changes, which is why this section adds a file shape and
amends no other section's byte tables.

An object may be stored loose or in a pack. A reader resolves a digest by looking in the open
batch, then the pack map, then `objects/`.

### 13.3 File shape

`packs/pack-%016llx.tpk`, the id in lowercase hex, allocated strictly upward.

| Region | Bytes | Contents |
|---|---|---|
| Body | variable | object images concatenated, in store order, no padding |
| Manifest | 44 × *count* | one entry per object |
| Trailer | 32 | fixed, at the very end of the file |

Manifest entry, 44 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 32 | object digest (BLAKE3-256) |
| 32 | 8 | offset of the image from the start of the file, u64 LE |
| 40 | 4 | image length, u32 LE |

Trailer, 32 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | magic `0x314B4341504E5254` — `"TRNPACK1"` little-endian |
| 8 | 4 | version, u32 LE, currently 1 |
| 12 | 4 | object count, u32 LE, at most 2²⁰ |
| 16 | 8 | manifest offset, u64 LE |
| 24 | 8 | XXH3-64 of bytes `[0, file length − 32)` |

The trailer is last because it is written last. A pack torn by a crash has no valid trailer, so
"is this pack complete" and "is this pack usable" are the same question, and one read at a known
offset from the end answers it.

### 13.4 Validity rules a conforming reader enforces

Per §1 rule 11, these are stated here because they are enforced here.

1. The file is at least 32 bytes.
2. The magic and version match exactly.
3. The count is at most 2²⁰.
4. `manifest offset + 44 × count + 32` equals the file length exactly.
5. The XXH3-64 over `[0, file length − 32)` equals the stored checksum.
6. Every manifest entry's `offset + length` is within `[0, manifest offset]`.

A pack failing **any** of these is **ignored in full — it is not an error, and it must not
prevent the world from opening.** A pack is flushed strictly before the root slot that names
anything inside it (§12), so a pack that fails these checks belongs to a capture that never
published, and nothing reachable from a published root refers to it. Refusing to open the world
over it would turn collectable garbage into a dead world.

An entry failing rule 6 is skipped individually; rules 1–5 condemn the whole file.

Pack ids are allocated past the highest id **present on disk**, whether or not that pack was
readable. A torn pack keeps its id until retention removes it, and reusing the id would mean
writing over a file that is still there.

Where the same digest appears in more than one pack, the earlier pack wins. A digest names one
byte string, so the later copy is the same bytes and the choice is arbitrary; fixing it makes
the map deterministic.

### 13.5 Ordering and crash containment

Capture buffers every object it writes — chunk payloads, index pages and the checkpoint
descriptor — and the pack is written and flushed **once**, immediately before the root slot is
published. This is the same ordering §12 already requires, with a smaller number of files:

1. buffer payloads, index pages, descriptor — nothing is durable
2. write the pack, one `Flush(true)` — everything is durable, nothing is referenced
3. publish the root slot — everything becomes reachable at once

A crash before (2) leaves nothing at all. A crash between (2) and (3) leaves a complete pack
that no root names: unreferenced garbage, exactly as §12 describes for loose objects. A crash
during (2) leaves a pack failing §13.4, which is the same thing. **No crash point leaves a root
naming an object that is not durable**, which is the property the whole ordering exists for.

An abandoned capture discards its buffer and writes nothing, so a failed capture no longer
leaves loose objects behind — an improvement on the loose-object path, where partial work
survived.

### 13.6 What this leaves for retention

Reclamation (§8) can delete a loose object individually. **It cannot delete one object out of a
pack.** A pack is reclaimable only when nothing reachable from a live root refers to *any*
object in it, and reclaiming partially used packs requires rewriting a pack without its dead
objects and republishing — a compaction pass that does not exist. Retention is not built yet
(DEF-9), and this is now part of what it has to handle.

## 14. Validation and test plan

New headless cases, `TerrainCore` automation, no engine world and no plugin (§6.1):

| Test | Asserts |
|---|---|
| `TerrainCore.Persistence.Format.Sizes` | Every fixed size in §§3–9 measured from the encoder and logged as numbers: 96, 4096, 4000, 122, 32, 131104, 16, 37, 50, 80, 72, 20, 140, 64 |
| `TerrainCore.Persistence.Format.RoundTrip` | Every object and record type round-trips field-for-field **and** byte-for-byte, including negative chunk coordinates, maximum counts, and zero-change commits |
| `TerrainCore.Persistence.Format.Golden` | Canonical fixtures hash to pinned BLAKE3 hex constants. A reordered, widened or re-endianed field changes a hex string in the diff |
| `TerrainCore.Persistence.Format.Corrupt` | Every defence fires with its **own** error code: short buffer, bad magic, schema 1, bad header size, header/body checksum, wrong object type, world/epoch/base mismatch, nonzero reserved, out-of-range enum, descending/duplicate ordering, over-cap counts, trailing bytes, Empty-as-payload |
| `TerrainCore.Persistence.Format.TornTail` | An incomplete final record is `TornTail`; a corrupt interior record is not |
| `TerrainCore.Persistence.Index.Keys` | The §6.1 transform is order-preserving over signed coordinates including `INT32_MIN`/`INT32_MAX` boundaries |
| `TerrainCore.Persistence.Index.PathCopy` | Rewriting D keys produces ≤ 12·D new pages, shares every untouched subtree by digest, and the rebuilt tree validates and resolves every key |

These are the format packet's own tests. The DEF-1/2/9 evidence tests P-003 §8 names
(`CommitCrash`, `SettlementReplay`, `RetryFence`, `CaptureFence`, `RootPublication`,
`JournalAnchor`, `RetentionPins`, `CoherentRestore`, `Migration`) belong to the later
increments and **remain unwritten**. Nothing in this packet closes DEF-1, DEF-2 or DEF-9.

---

## 15. Status

DEF-1, DEF-2 and DEF-9 remain **open**. This packet is a specification and its codecs; it
adds no commit path, no checkpoint pump, no recovery driver, no settlement, no SQLite, no
file I/O on the authoritative path, and no service integration. Build step 4 is not complete
and no persistence performance, crash-safety or save-compatibility claim follows from it.

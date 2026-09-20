→ No action. For your reading only.

# HANDOFF.md — replay works; next is attaching a journal to a running world

## Identity, authority and Git state

- Updated **2026-09-20**. Outgoing **Claude Opus**, who both wrote and reviewed this
  increment. Incoming **either** (D-028). One active Implementer per workspace.
- Director instruction this session: *"Read handoff.md, and move forward with game
  development picking up at whatever T-XXX is not completed. You will fluidly be an
  independent reviewer and a code writer. There are no longer any constraints to your job
  description, and i trust you to make all decisions. Begin at once."* That lifts the
  writer-is-not-reviewer separation of AGENTS §2 **for this increment only**, by the
  Director's explicit word. It is not a standing change to AGENTS.md, and the honest cost
  of it is recorded under "Limits" below.
- Received at **`b9104c0`**, branch `main`, clean tree; `git pull --ff-only` already up to
  date. Runtime base for T-117 was `893a029`.
- **CP-014** covered three increments that had run without a checkpoint: **T-115** (build
  step 3, `21e3a2c`), **T-116** (P-003 adopted, `b9104c0`) and **T-117** (P-004 and its
  codecs, `893a029`). Rulings in **D-033**; risks **R-015** and **R-016** opened.
- **CP-015 is this one**, at `30105b5`, covering **T-118** (the R-016 review, `af67b6f`) and
  **T-119** (the storage device seam, `30105b5`). Rulings in **D-034**; **R-007 broadened**
  by a confirmed platform-divergence instance; **R-015** gains its containment evidence;
  **R-016** reduced to a design read. STATE, BACKLOG, DECISIONS and RISKS are current.
- Changed by T-117: P-004 (new), ARCHITECTURE §4.7 and §6.1, seven new TerrainCore source
  files, three new test files and one test fixture header, plus two one-line renames in
  existing files (see "Two pre-existing defects", below). Changed by the checkpoint: STATE,
  BACKLOG, DECISIONS, RISKS and this handoff.
- No `.Build.cs`, `.uproject`, config, `.uasset` or `.umap` changed. No dependency added.
  The 58-byte operation wire and every existing golden value are untouched.

## The R-016 review — run after CP-014, findings fixed

`STATE.md` made this the first task of the next session and it was done immediately.

**An independent encoder was written in Python from P-004's byte tables alone** — not
translated from the C++ — in a disposable scratchpad venv with real BLAKE3 and XXH3, both
checked against their published test vectors first. Nothing was installed into the machine's
Python. It reproduced **16 of 16 pinned golden vectors exactly**. The document and the code
agree at the byte level, which is exactly what R-016 said was unproved.

Two findings, both fixed, both now tested, neither moving a byte (the golden vectors are
unchanged):

- **F-1 — P-004 understated the format.** The decoders enforced reference-validity rules the
  document never stated, so a decoder written from P-004 alone would have accepted objects
  this one rejects. Now stated in §§6.2, 7, 8, 9.1 and 9.5, with a new §1 rule 11 making it
  a standing convention.
- **F-2 — an over-cap SparseDiff count reported `FieldOutOfRange` on encode and
  `CapExceeded` on decode.** Both are `CapExceeded` now.

**What the review did not cover**, and what therefore remains single-author: the reimplementation
was of **encoders**. A decoder, the path-copy index algorithm and the scanner's torn-tail
logic were not independently implemented, and no reimplementation reviews a *design*. R-016
is reduced to that residual rather than closed.

The reimplementation lives in the session scratchpad and is **not committed** — it depends on
two pip packages and is evidence, not project code. Re-running it means recreating the venv
and re-writing it from P-004, which is the point: if it needed to be kept, it would not be
independent.

## Replay — a world can now be rebuilt from its journal

**This is the increment that demonstrates Pillar 1 rather than building towards it.**

`TerrainReplayJournal` is P-003 §3's terrain pass: walk the segment chain in ascending order,
check contiguity *across* segments (no single segment's scan can), and re-apply each operation
**whole and exactly once** onto a freshly initialised backend, validating the recorded
before/after revisions as it goes. A record whose changed-chunk set does not match what the
backend actually did is `BaseMismatch` — a record describing a different world.

`Persistence.Replay.Equivalence` is the proof: six deliberately **overlapping** edits are
played into one backend and recorded; a fresh store object and a fresh backend then replay,
and **every edited chunk comes back with an identical `HashRegion`**. That hash is
position-sensitive and includes materials, so it compares two worlds rather than counting
operations that happened to succeed. An untouched chunk is identical to the pristine base, so
replay writes only where it was asked to.

**Two honest findings, both now written into the code rather than left to be discovered.**

1. **Replay must NOT release its residency interest.** P-003 §3 says replay's interests are
   "released afterwards", which is right in the world P-003 describes — one where a checkpoint
   holds the restored state, so an evicted chunk reloads from its payload. *That world does
   not exist yet.* With no capture pump, G is 0 and everything replay rebuilds lives only in
   backend RAM, so releasing the interest on a backend that evicts would silently discard the
   entire restored world. The interest is retained and handed to the caller. This is one of
   the concrete reasons the capture pump is required rather than an optimisation.
2. **A damaged FINAL journal record is indistinguishable from a torn append.** Both are the
   same bytes, so the world returns *without that edit* and no reader can tell which happened.
   That is a property of an append-only journal, not a defect here, and the test asserts it
   explicitly rather than leaving it to be found in a saved world. A damaged **interior**
   record, by contrast, refuses the world outright. My first version of that test corrupted
   the only record in a one-record journal and expected a refusal; it got a torn tail, which
   was the format being right and the test being wrong.

**Verified:** both targets build; **32 of 32** tests pass, exit 0 (`Saved/Logs/Replay-Pass.log`).

**The gap that remains, and it is the important one.** Nothing attaches a journal to a running
game. `UTerrainService::CommitJournal` is null unless something sets it, and no bootstrap
creates a world store for a real session, so **no edit survives a restart of the actual
game**. Every piece needed is now built and tested; what is missing is the wiring and a
decision about where a world directory lives. That is the next increment and it is small.

## The commit path — durable before published

P-003 §2's ordering, wired into the live edit path for the first time.

`UTerrainService::CommitOp` (was `BroadcastCommit`) now, in this order: validates geometry,
captures before-revisions, advances the revision index, **durably records the operation**, and
only then advances the committed sequence and broadcasts. If the record cannot be made
durable it returns false, the service marks itself **storage-faulted**, and admission closes
with `ShuttingDown` — the rejection that already meant "the world is going away, do not
retry". The backend has already mutated at that point, so the honest state is RAM holding a
change that is neither durable nor published, and P-003 §2 resolves that by restarting from
disk rather than by pretending the edit happened.

**The queue now treats the sequence as provisional.** `Cb.Commit` returns bool, and `OpSeq` is
only consumed once the record is durable — otherwise a refused commit would leave a gap in the
journal for an operation that never became part of the world's history.

**One deviation from P-003 §2's literal step order, stated rather than hidden.** The revision
index advances *before* the append, not after, because the record must carry the true
after-revisions and the index is the only authority for them. That is safe for one reason: the
index is in-memory, and P-003 §2 discards unbroadcast provisional RAM on a storage fault. What
must not happen — publishing before the flush — cannot happen.

**Two honest defaults in the record.** `EconomyKind = NoEconomy`, because DEF-6 is open. And
`PhysicalAvailability = Unavailable` with an **empty** list even when the backend reports
volumes, because the production adapter's materials are zero and P-003 §2 forbids encoding
unknown as a measured zero — `FTerrainWorldStoreJournal::bPhysicalMeasured` flips it when a
backend genuinely measures. The token digest is zero because protocol 2 does not exist; that
is an honest "none", not a stand-in.

**Verified:** both targets build; **31 of 31** tests pass, exit 0; and because this touched
the live service, the real-network harness was rerun — **`MP.Convergence: PASS clients=3
chunks=4 committed=243`** (`Saved/Logs/T101B-MP-20260920-152624`).

**A gap this increment did not close, and did not pretend to.** `CommitOp` cannot be driven
from a headless test: `TryAdvanceRevisions` needs `HasAuthority()`, which needs a real game
`UWorld`, which would make the case §6.2 rather than §6.1. Its internal ordering is read, not
tested. The first attempt at this test did build a service and drive `CommitOp`, and it
crashed on exactly that — recorded because the crash is the evidence for the claim.

**Nothing attaches a journal to a running game yet.** `CommitJournal` is null unless something
sets it and no bootstrap creates a world store for a real session, so **no edit survives a
restart**. That is the next increment.

## The journal writer and the world store — a world directory now exists

P-003 §8 item 3's remaining half, on top of the device.

**`FTerrainJournalWriter`** owns the active segment, the anchor pair and the ordering P-004
§9.5 makes normative — create and flush the segment header and its namespace, publish and
flush the **inactive** anchor, then append. Sealing and rotating are one operation rather than
two a caller could get out of order, because the new segment's continuity evidence is the
digest of the seal frame that rotation just wrote.

**`FTerrainWorldStore`** owns a world directory: create (base descriptor, the empty G=0
checkpoint, both root slots, the first segment, both anchors), open, and checkpoint
publication. **A fresh world is 7 files.**

**Every claim about what the writer wrote is checked with the scanner**, never with the
writer's own state. The two were built as halves of one contract in T-117 and T-119 precisely
so neither marks its own work.

What the tests actually demonstrate, beyond round trips:

- A **torn append** closes the writer over an uncertain tail, and a reopen finds the tear,
  keeps the last complete record, and still refuses to append. It does **not** truncate:
  P-003 §3 requires the torn bytes preserved before repair, and repair is the recovery
  increment's job with its own policy.
- An **interrupted rotation** — the new segment header written, the anchor publication failed
  — leaves a world that boots to the sealed segment with no acknowledged record lost, and
  reports the new segment as an ignorable orphan. That is exactly the window P-004 §9.5's
  ordering exists to make survivable.
- A newer unanchored **non-empty** segment refuses boot; a named-but-**missing** segment
  refuses boot rather than reporting an empty journal.
- A **torn root publication** leaves the previous checkpoint current, reports root redundancy
  as broken, and is repaired by republishing. P-004 §12's *containment* argument is now
  demonstrated rather than asserted — the durability half is still unproved.
- A checkpoint beyond the journal head refuses to open, and a cross-wired world (this world's
  base descriptor in front of another world's roots) is refused with `WorldMismatch`.

**One wart, documented rather than hidden.** A rotation that fails after writing its segment
header consumes that segment ID: the orphan file exists and rewriting it is refused, because
an immutable object is never overwritten. A retry must use the next ID. That is the safe
failure, and there is a test that pins it.

**Verified:** both targets build; **30 of 30** tests pass, exit 0
(`Saved/Logs/Journal-Final.log`). Two new cases: `Journal.Writer`, `WorldStore.Lifecycle`.
Both declare their expected error logs, so "the writer shouts when it closes itself" is a
checked expectation rather than noise.

**Still missing:** the commit path, the capture pump, settlement, SQLite, replay/recovery and
retention. Nothing calls any of this from the game yet, and **no edit survives a restart**.

## The storage device seam — built after the review

P-003 §8 item 3's first half. `TerrainStorage.h/.cpp` adds `ITerrainStorageDevice` (six
mutating operations, because every one is a place a crash can happen and a smaller surface is
a smaller crash matrix), a real `FTerrainPlatformStorageDevice`, an in-memory one, a
fault-injecting decorator that can **fail or tear** any chosen operation, the
content-addressed `FTerrainFileObjectStore`, and `FTerrainSlotPair` — the publication
primitive the whole store rests on.

**Two findings came out of reading the engine rather than assuming it, and the second one
would have shipped a bug.**

1. **`OverwriteInPlace` must not truncate.** `OpenWrite(bAppend=false)` truncates, which would
   make a slot briefly zero-length — and a slot that can be *absent* breaks the one property
   the two-slot protocol rests on. The device opens in append mode and seeks to zero.
2. **`Flush()`'s default argument is wrong for this project.** Windows ignores `bFullFlush`
   and always calls `FlushFileBuffers`. **Unix does not**: `Flush(false)` is `fdatasync`,
   `Flush(true)` is `fsync`, and `fdatasync` makes no promise about metadata — including a
   file's length. Every object write and every journal append extends a file, and D-002 makes
   a Linux dedicated server the shipping target. A defaulted `Flush()` would have been correct
   on this machine and silently wrong on the shipping one, and no amount of Windows testing
   would have found it. Both rules are now normative in P-004 §12 with the platform sources
   cited.

**A test-quality finding, recorded because it nearly passed for the wrong reason.** The first
torn-slot fixture tore at byte 1000. A root slot body is 64 bytes of fields and 3,936 zeroes,
so both the old and new images are identical past byte 160 and the "tear" reproduced a
perfectly valid slot — the test passed nothing. It now tears at 104, leaving a slot whose
**generation field says new while its content is old**, which is the exact state that would
fool a reader that trusted the generation without validating the checksum.

**Verified:** both targets build; **28 of 28** TerrainCore tests pass, exit 0
(`Saved/Logs/Storage-2.log`). Four new cases: `Storage.Paths`, `Storage.ObjectStore`,
`Storage.SlotPair`, `Storage.PlatformDevice`. The last runs against the real file system in
`Saved/Automation/TerrainStorageTest` and cleans up after itself, because the two properties
that matter most are properties of `IPlatformFile` and a fake device would agree with itself
and prove nothing.

**What is still missing above this seam:** the journal writer and segment rotation, anchor
publication ordering, checkpoint publication, world create/open, recovery and retention. The
device is ready for all of them and none of them exists.

## What this increment is

P-003 §8 item 1 required an **exact format/storage specification** before any codec, and
item 2 required **bounded codecs and a storage seam** against it. Both are done.

[**P-004**](proposals/P-004-persistence-format-and-storage.md) fixes schema 2: every byte
offset, width, cap, ordering rule, checksum and digest; the canonical intent and record
digests; journal framing, sealing and anchors; the chunk-key index transform and page
layout; the file-naming rule; the module boundary; and the error taxonomy a decoder
returns. ARCHITECTURE §4.7 now points at it and §6.1 lists the tests it required.

**Read P-004 before touching any of the code below.** The code is its implementation and
the section numbers in the comments are P-004's.

### Decisions this packet made, that P-003 left to it

Each is a byte-level determination inside the adopted architecture, not a change to it.

- **BLAKE3-256 for content digests, XXH3-64 for framing checksums.** Both are specified
  algorithms vendored in `Core`. Explicitly **not** `FCrc::MemCrc32`, whose value is an
  Unreal implementation detail and would make every saved world hostage to a header Epic
  is free to change.
- **No floating point is persisted anywhere in schema 2.** Voxel size and world origin are
  config floats and cross as exact **micrometres** (`int64`). A stored `float VoxelSizeCm`
  would make save compatibility depend on `50.0f` decoding identically on every toolchain,
  which is R-014's exact shape.
- **A 32-byte `GeneratorParamsDigest` is added to the base descriptor.** P-003 §6 requires
  binding "generator identity/version", and `GeneratorVersion` is a config integer a human
  remembers to bump. The digest means a changed `FTerrainWorldFieldParams` with a forgotten
  version bump fails the exact-base check instead of silently reinterpreting every Empty
  chunk. This strengthens an adopted requirement; it does not alter one.
- **Empty has no payload object.** Its revision and last-change metadata live in the index
  leaf entry, which every Empty chunk needs anyway. Two on-disk spellings for one logical
  state would force every validator to rule on which wins when they disagree.
- **Sealing appends a record; it never rewrites a segment header.** Consequently no durable
  object in schema 2 is ever modified in place **except** the four fixed slots — and those
  are the only places where a torn write has a surviving redundant copy.
- **Records inherit identity from their segment header** rather than each carrying a
  96-byte header, plus an 8-byte `WorldTag` per record as the splice check that inheritance
  would otherwise lose.
- **Mode A (named content-addressed objects) is the default storage mode**, and the byte
  tables are mode-independent because **no format field contains a path**. P-003's
  preallocated-container fallback can therefore still be adopted without changing a stored
  byte. See "Limits" for what is *not* proved about that.

## Files

All placements are beneath **`C:/Dev/VoxelWorld/`**.

| Files | Behaviour |
|---|---|
| `Docs/proposals/P-004-persistence-format-and-storage.md` | The specification. 14 sections, every byte table, caps collected, storage mode ruling, test plan |
| `Source/TerrainCore/{Public,Private}/TerrainPersistenceFormat.*` | Identity types, BLAKE3/XXH3 choices, the 64-hex object-naming rule, bounds-checked byte reader/writer, the 20-value error taxonomy, the 96-byte object header, slot framing |
| `Source/TerrainCore/{Public,Private}/TerrainPersistenceRecords.*` | Base descriptor, chunk payload (Dense/SparseDiff) and the encoding-choice rule, checkpoint descriptor, root slot, journal segment header, commit and seal records, anchor slot, intent and record digests, and a whole-segment scanner |
| `Source/TerrainCore/{Public,Private}/TerrainPersistenceIndex.*` | The 96-bit key transform, page codec, the `ITerrainObjectStore` seam with its in-memory implementation, and path-copying apply / lookup / structural validate |
| `Source/TerrainCore/Private/TerrainPersistenceDump.cpp` | `Terrain.PersistDump <file>` — read-only inspection of any schema-2 object or journal segment. AGENTS §4 requires persistence formats to be inspectable |
| `Source/TerrainCore/Private/Tests/TerrainPersistenceFixtures.h` | Shared fixed fixtures. Nothing here reads a clock, a random or any config, which is what makes the golden vectors meaningful |
| `Source/TerrainCore/Private/Tests/TerrainPersistence{Format,Golden,Index}Test.cpp` | The seven new cases in §6.1 |

## Verification — executed

UE **5.8.2**, Legacy **434**, Win64. Logs are local and gitignored.

| Check | Result / evidence |
|---|---|
| Editor Development build | `Result: Succeeded` |
| Game Development build | `Result: Succeeded` |
| TerrainCore automation | **24 tests completed, 24 Success, zero failures**, process exit 0. `Saved/Logs/P004-Validation-Final.log`. 17 pre-existing plus the 7 new |
| `Terrain.PersistDump` | Registered and exercised in a real `-game` process, exit 0: a non-object file, a missing file and a no-argument call each report and do not crash. `Saved/Logs/P004-Dump-Smoke.log`. **Its success path is not exercised end to end** — see Limits |
| Golden vectors | 16 pinned, produced and re-verified. `Saved/Logs/P004-Golden-1.log` holds the first emission |
| D-011 boundary | Source scan: no plugin include in `TerrainCore` or gameplay. The new files include only `Core` and `TerrainCore` headers. Module dependencies unchanged |
| D-025 guard | Source scan: no `ModelContextProtocol` / `AllToolsets` / `StartServer` reference in `Source/`. `.uproject` untouched |
| Diff | `git diff --check` clean; no generated asset or config change |

### Exact commands

Run from `C:/Dev/VoxelWorld`:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' VoxelWorldEditor Win64 Development '-Project=C:\Dev\VoxelWorld\VoxelWorld.uproject' -WaitMutex
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' VoxelWorld Win64 Development '-Project=C:\Dev\VoxelWorld\VoxelWorld.uproject' -WaitMutex
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'C:\Dev\VoxelWorld\VoxelWorld.uproject' '-ExecCmds=Automation RunTests TerrainCore; Quit' -unattended -nopause -nosplash -nullrhi '-abslog=C:\Dev\VoxelWorld\Saved\Logs\P004-Validation-Final.log'
```

Expected: two successful builds, then 24 `Result={Success}` lines and exit 0.

## Measured numbers worth carrying forward

These are **computed or measured from the encoders**, not projections.

- Object header **96 B**; slot file **4096 B**; base descriptor prefix **122 B**; chunk
  payload prefix **32 B**; Dense body **131,104 B**; checkpoint descriptor **80 B**;
  segment header **72 B**; record frame overhead **20 B**; commit prefix **140 B**;
  seal body **64 B**.
- Worst-case commit record **84,768 B** against the 131,072 B cap — 35% headroom.
- A radius-4 dig over 8 chunks with 3 materials and `NoEconomy` is **350 B** of journal.
  This replaces the withdrawn ~122 B/edit estimate as a *computed* figure; real bytes/edit
  under the §7.1 workload is still a measurement gate.
- SparseDiff beats Dense up to **21,844** changed samples of 32,768.
- Index: 64 keys wrote **147 pages** against the 12·D bound of 768; one subsequently
  changed key rewrote **exactly 12** and shared every other page.

## Two pre-existing defects found and fixed

Adding four translation units changed the unity-build grouping and exposed two latent
collisions that had nothing to do with persistence. Both are file-local renames with no
behaviour change, and both would have broken the next person to add a file here.

- `MemoryTerrainBackend.cpp`'s anonymous-namespace `MaxWrites` shadowed the `MaxWrites`
  **parameter** of `TerrainOpCounts`/`SplitTerrainOp` once they shared a translation unit
  (C4459, warnings-as-errors). Renamed to `MemoryMaxWrites`.
- `Store16` was defined in the anonymous namespace of **both** `BackendConformance.cpp`
  and `TerrainOpSemanticsTest.cpp` (C2084 when grouped). The latter already prefixes its
  helpers `Sem`, so it became `SemStore16`.

## Self-review findings, and what was done about them

The Director merged the writer and reviewer roles for this increment, so this is a review
of my own code. Six findings, all fixed before the final run; the first two are the ones
that mattered.

1. **Torn-tail detection was too loose, and would have eaten history.** The segment scanner
   treated *any* checksum failure on an active segment as a legal torn tail. A damaged
   record in the **middle** of a segment also fails its checksum, so the scanner would have
   reported truncation at that point and silently discarded every acknowledged record after
   it. Fixed in both the code and P-004 §9.2: a checksum failure is a tear only when its
   frame reaches **exactly** end of file. Found while writing the fixture that is now
   `Persistence.Format.TornTail`'s interior-damage case.
2. **`TerrainPersistEncodeObject` enforced its cap with `checkf`**, which compiles out of a
   shipping build — the same class of defect the step-3 review already caught once, where
   geometry and revision advancement sat inside `check(...)`. An over-cap object written by
   a shipping server is one no decoder will ever accept again. It now returns
   `CapExceeded`, appends nothing, and has its own test. The Dense sample accessors had the
   same shape and now refuse out of range instead of reading past the buffer.
3. A zero-filled tail — a torn append that extended the file's valid data length without
   writing bytes — was not covered. Added as P-004 §9.2 form 4, with a test, and with
   non-zero garbage still failing closed.
4. A segment header could claim `SegmentId` 0, which the anchor already uses to mean "no
   predecessor". One value, two meanings. Now `FieldOutOfRange`.
5. The segment-header body size was written down in two headers. One now derives from the
   other, which is the rule P-004 §10 states and I had just broken.
6. `ExpectedWorldTag == 0` silently skips the splice check. Kept — the sentinel is needed
   by the dump command, which has no identity to check against — but now documented, with
   the 1-in-2^64 consequence stated rather than left implicit.

## Limits — what this increment does NOT establish

1. **DEF-1, DEF-2 and DEF-9 remain open.** This is a specification and its codecs. There is
   no storage owner, no file I/O on any authoritative path, no commit path, no capture
   pump, no settlement, no SQLite, no recovery driver, no retention or GC, and no service
   integration. **Build step 4 is not complete.** None of P-003 §8's named evidence tests
   (`CommitCrash`, `SettlementReplay`, `RetryFence`, `CaptureFence`, `RootPublication`,
   `JournalAnchor`, `RetentionPins`, `CoherentRestore`, `Migration`) exists.
2. **Windows durable name publication is still unproved.** P-004 §12 rules Mode A as the
   default and argues *containment* — the four fixed slots create no new names, so the
   worst case of a lost directory entry is falling back one checkpoint generation. That is
   an argument, not a durability proof, and no in-process test can settle it. It stays a
   named acceptance gate before service integration.
3. **`Terrain.PersistDump`'s success path is not exercised end to end.** Its failure paths
   are, in a real game process. Nothing yet writes a schema-2 file to disk, so there is
   nothing for it to read successfully; the decoders it calls are unit-tested, which is not
   the same claim. Close this when the storage owner lands.
4. **The golden vectors were produced by the same implementation they now pin.** They lock
   the format against future drift, which is their job. They do **not** prove the code
   matches P-004's tables — one author wrote both. An independent implementation of a few
   objects from the document alone, checked against these hashes, would prove that and is
   the cheapest remaining review of this work. It was not done here: it needs BLAKE3 and
   XXH3 outside the engine, and neither is installed on this machine.
5. **Writer and reviewer were the same agent, by the Director's explicit instruction.** The
   six findings above are real and were fixed, but a review of one's own work is weaker
   evidence than a cross-vendor review, and this is an R3 subsystem. If the Director wants
   the AGENTS §2 guarantee back for this increment, the packet plus the diff is exactly the
   shape Codex reviewed P-003 revisions in.
6. **No performance claim.** Nothing here was profiled. §7.1's 8 ms budget is untouched and
   the step-3 queue-age and apply-time figures still stand as the last measurements.
7. `Tests/Saves/` does not exist yet. Schema 2 is the first schema ever written, so there is
   nothing to be backward-compatible *with*; the first migration is what creates it.

## Next safe actions

1. Read P-004, then this handoff's "Decisions" and "Limits". Verify the enclosing commit,
   branch and worktree; `git pull --ff-only` on a clean tree.
2. **The byte-level review is done** (T-118, 16/16 vectors reproduced from the document
   alone). What remains owed under R-016 is a **design** read of P-004 by the vendor that did
   not write it — a judgement no reimplementation can make. It is not a blocker for the work
   below; it is a thing the Director may want before this format carries a real world.
3. Then P-003 §8 item 3, in this order: the **storage owner** (pre-created slots, segment
   files, content-addressed object store behind `ITerrainObjectStore`, and the ordering
   rules in P-004 §9.5 and §12), then the commit path with a `NoEconomy` consumer, then the
   crash matrix. Do not start settlement or SQLite before the storage owner has its own
   fault-injection coverage.
4. Keep DEF-1/2/9 open until their named evidence exists. Materials, real economy (DEF-6),
   client JIP (step 5) and collision readiness (step 7) remain separately gated.
5. **CP-015 is done.** Do not re-record T-115 through T-119; they are in the Done log. The
   next checkpoint is CP-016 and it should cover the journal writer and the world store.

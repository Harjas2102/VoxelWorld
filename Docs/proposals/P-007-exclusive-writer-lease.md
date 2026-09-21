→ No action. For your reading only.

# P-007 — The exclusive-writer lease

**Status: specification and Architect ruling, 2026-09-21. Implemented as T-128.**
**Author:** Claude Opus. **Risk:** R3. **Base:** `614201d` (CP-018).
**Authority:** P-003 §2 ("preserve the store's exclusive writer lease until worker release…
lease acquisition is nonblocking: refuse with a diagnosable StoreBusy error"), P-003 §5
("exclusive writer ownership is required"), P-005 (no runtime names), D-023 (technical ruling,
logged).

Writer and reviewer are the same agent (Director's standing instruction). §5 is the self-review.

---

## 1. The problem

Before this change, nothing stopped two servers from opening the same `WorldStoreName` and
writing side by side. Both would append to the same journal segment and containers, and both
would publish root slots. The result would be a world that neither server's recovery path can
make sense of. P-003 §5 has always required a single writer. This change is where that rule gets
enforced.

## 2. Rulings

1. **Use an OS lock, not a marker file.** A marker file outlives a crash, and then a later
   server can't open the world without someone deleting a file by hand. An OS lock ends when its
   process ends, however that happens.
2. **The lock is on one file, `writer.lock`, at the root of the world.** Its contents are never
   read or written, and it stays zero bytes. The lease creates it before the world is opened,
   as a bootstrap step, just like the container pool P-005 adds to an older world. So "an open
   world creates no names" still holds.
3. **The lease sits in the device seam.** `ITerrainStorageDevice::AcquireExclusiveLease` returns
   an `ITerrainStorageLease`, and releasing the object releases the lock.
   - **Windows:** `LockFileEx(EXCLUSIVE | FAIL_IMMEDIATELY)` on byte 0, through a handle that
     shares read and write but not delete. The HANDOFF had suggested an open with no sharing
     at all. I ruled against that because a virus scanner or indexer briefly opening the file
     would then make a server refuse its own world. Withholding delete sharing means nobody can
     remove or rename the lock file while it is held.
   - **Unix:** `flock(LOCK_EX | LOCK_NB)`, which conflicts between two separate opens even inside
     one process. After locking, it checks that the locked inode is still the one the name
     points to, because a lock on an unlinked file protects nothing.
   - **Memory device:** keeps a set of held paths. A fault device passes the lease through to
     the device it wraps, so two decorators over one memory device compete for it the same way
     two processes compete for one disk.
4. **The lease never blocks.** If someone else holds it, the call returns
   `ETerrainStorageResult::Busy` at once (logged as `StoreBusy`). It never waits.
5. **The service takes the lease before it checks whether the world exists.** Otherwise two
   servers could both see "no world" and both create one. A refused server has read nothing
   and written nothing. It closes terrain access, following the same policy as a wrong-base
   boot, and its log line tells the operator what to do.
6. **The store holds the lease for its whole life.** `FTerrainWorldStore::Lease` is declared
   before every member that writes, so it is the last thing released. `CloseWorldStore` already
   waits for the retention worker and the capture pump before it destroys the store, which
   satisfies P-003 §2's "until worker release".
7. **`Open` and `Create` do not take the lease themselves.** Headless tests simulate a restart by
   opening a second store on the same device while the first one is still in scope. That is a
   test convenience, not a second writer, and making it fail would mean rewriting about fifteen
   tests to show nothing new. The only production caller is the service, and it takes the lease.
8. **Taking the lease is not a mutation.** It is not counted in `FailAtMutation`'s sequence, so
   every crash-matrix index still means what it meant at CP-018.

## 3. Evidence

| Check | Result |
|---|---|
| TerrainCore automation | **38/38**, including the new `Persistence.Storage.WriterLease` |
| `WriterLease`, memory half | the second holder gets Busy, and so does a third through a fault device; no byte changes; the first holder still publishes; after release the next holder opens the world at the published generation; a device fault gives IoError, holds nothing, and counts as no mutation |
| `WriterLease`, real disk | the lease creates the directory and lock file before the world exists; a second device on the same directory gets Busy, and so does a second store on the same device; no byte changes; the held lock file cannot be deleted; after release the other device takes the lease and opens the world |
| **Mutation test** | with `LockFileEx` removed, `WriterLease` fails at exactly the two disk-contention assertions |
| `Tools/Test-TerrainLease.py` (real processes) | Server A holds the world. Server B, on the same world, logs `StoreBusy`, closes access, has its dig refused (`reason=ShuttingDown`), never opens or creates the store, and changes no save byte. A is then **killed hard** (TerminateProcess), and Server C opens the same world and passes `Terrain.SelfTest`, so no stale lease is left |
| `Test-TerrainCheckpoint.py` | PASS, and the wrong-base boot still leaves every byte unchanged |
| `Test-TerrainMultiplayer.ps1 -CheckpointCapture` | PASS: 485 commits, 30 checkpoints, no terrain warnings |
| Server travel (2 and 3 rounds) | the store closes, releases its lease and reopens after travel (`World store opened: G=419`). See §4 for what fails after that |
| Both targets | build |

## 4. Found along the way, not caused by this change

**Clients stop receiving terrain after a server travel on a persisted world.** In a multi-round
`MP.Convergence`, round 1 passes. After the travel, the server reopens the store and commits
edits, but every client applies **zero** of them and the round fails. I reproduced it on
`614201d` with the T-128 work stashed (`-Rounds 2 -DurationSeconds 20 -CheckpointCapture`:
PASS for round 1, then FAIL with `committed=324`). So it predates this change. The last
multi-round pass on record is CP-014, which came before persistence existed. This is its own
task (T-129).

## 5. Self-review: what could still be wrong

- **Unix is written but has never been compiled.** It carries the same risk as P-005's
  `fsync(dir)` (R-007). The first Linux build must run `WriterLease`.
- **How fast Windows releases the lock after a hard kill.** The docs say the release "depends
  on available system resources". Server C opened immediately after a kill in two runs. A
  server restarted within milliseconds of a crash could, in principle, see a brief
  `StoreBusy`. The failure is safe: it refuses and never writes beside anyone.
- **Spurious Busy.** On Windows, a process that opens `writer.lock` with *no* sharing would make
  us refuse. No scanner we know of does that, and the answer would still be safe.
- **A hung holder blocks the world forever.** This is intended (P-003: never wait on stalled
  I/O). The operator kills the hung server.
- **Network file systems.** `flock` over NFS is not reliable. A dedicated server's save is local
  disk, and nothing here claims more than that.
- **Nothing makes `Open` refuse to run without the lease** (ruling 7). A future offline tool
  that opens a live world would need to take the lease itself. The comment on `AcquireWriterLease`
  says so.

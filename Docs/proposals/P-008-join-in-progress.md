→ No action. For your reading only.

# P-008 — Join-in-progress: modified-chunk sync and resync (DEF-3, build step 5)

**Status: specification and Architect ruling, 2026-09-21. Implemented as T-129.**
**Author:** Claude Opus. **Risk:** R3 (replication, join-in-progress). **Base:** `1a53645` (T-128).
**Authority:** ARCHITECTURE §4.8 and §7.3, DEF-3, D-023 (technical ruling, logged), and the
Director's standing instruction.

Writer and reviewer are the same agent. §7 is the self-review.

---

## 1. The bug that exposed this

After a server travel on a saved world, clients received **none** of the server's edits, and the
multi-round `MP.Convergence` failed in round 2. The cause was one line in
`RefreshSubscriptions`:

```cpp
if (GetRevision(K)!=0) continue; // Modified-chunk catch-up needs the step-5 protocol.
```

A client could subscribe only to chunks that had never been edited. That was harmless while the
world was forgotten at every restart. Once the server started remembering the world (CP-016),
every edited chunk in a reloaded world became a chunk no client could ever subscribe to. The
player would have seen the original hill in multiplayer, with the saved digging missing, and
none of the other players' edits in those chunks. The bug was not a small defect. It was the
missing build step 5.

## 2. Why DEF-3 is closed without a buffering protocol

DEF-3 says a multi-chunk op can be replayed into a chunk whose snapshot already contains it, and
that the sync/live handoff has no atomic transition. Two facts about this codebase remove both
problems:

1. **The server serializes every commit on the game thread.** A snapshot read between two
   commits is an exact cut: every op before it is inside the snapshot and every op after it is
   not.
2. **All terrain state for one client travels over one reliable, ordered channel.** Ops, pristine
   notices and snapshot fragments are all RPCs on that client's `UTerrainStreamComponent`, so they
   arrive in the order they were sent.

So a snapshot sent at cut S arrives before every op committed after S. The client installs each
message as it arrives and never has to decide whether an op is "already in" a chunk. The
snapshot's position in the stream decides that. The handoff is atomic because the channel is
ordered.

**One property makes the rest safe: the kernel is pointwise.** The new value of voxel v depends
only on v's old value and the op (§4.10.3: monotone, idempotent, nothing outside W changes). An op
applied across a footprint that contains a chunk the client has not synced yet writes something
meaningless into that chunk, but it cannot affect a neighbouring chunk that *is* synced.

## 3. Rulings: the replica (`FTerrainReplica`)

1. A client keeps a set of **synced** chunks: chunks whose data and revision are known to equal
   the server's. A chunk gets into the set in two ways: a pristine notice it accepted, or a
   snapshot it installed.
2. **An authoritative op is applied to the backend over its whole footprint.** Revision checks
   and revision bumps apply **only to synced chunks**. An unsynced chunk is written but never
   checked or bumped, and it stays unsynced until a snapshot replaces it.
3. **A synced chunk that doesn't match is demoted, not the whole op.** A chunk "doesn't match"
   when its revision differs from the op's before-revision (a missed op), or when "the server
   changed it" disagrees with "we changed it". That chunk alone is demoted and reported for
   resync. The rest of the op still applies. The old rule blocked every op that touched a
   flagged chunk, forever.
4. A snapshot is installed with `WriteRegion`, and its revision is **assigned**
   (`AssignReplicaRevision`, replica only). A replica's revision is a copy of the server's, not
   history of its own.
5. The whole rule lives in `FTerrainReplica`, which has no UObject, world or transport. It tests
   headless against the reference backend.

## 4. Rulings: the server

1. **Subscription.** A chunk inside the radius is handled one of three ways:
   - if the client already has its current revision, it is subscribed directly;
   - if it was never edited, it gets the 12-byte pristine notice, as before;
   - otherwise it goes on a per-connection **snapshot queue**, sorted nearest first.

   Queued chunks that leave 1.5× the radius are dropped from the queue. The listen host is
   subscribed directly, since its world is the authoritative one.
2. **A snapshot is read when it is sent, not when it is queued.** That read is the cut. All of a
   snapshot's fragments go out in one call, so no commit can land between them.
3. **Wire format.** A snapshot is the §4.2 Dense layout, Zlib-compressed (`FCompression`, which
   is Core), and cut into fragments of at most 16 KiB (up to 10). That keeps each one far below
   UE's 64 KiB partial-bunch limit. A real dug chunk compressed to **272 bytes to 5 KB**;
   incompressible noise, the worst case, is 131,118 bytes in 9 fragments.
4. **Backpressure (§7.3).** Each connection has at most 64 KiB of unacknowledged snapshot bytes,
   because UE closes a connection whose reliable buffer overflows. One snapshot is always
   allowed in flight, so an oversized chunk still goes out, alone. Across all connections the
   server sends at most 2 snapshots per tick.
5. **Relevance.** An op goes to a client if any chunk it changes is subscribed, pending pristine,
   **or has a snapshot in flight**. The client will be synced to the state before the op by the
   time the op arrives.
6. **`DeliveredRevisions` records only chunks the client holds in sync** (subscribed or
   syncing). Before this change it recorded every chunk in an op's footprint. Under the new
   rules that would have let a resubscription skip a snapshot the client needed.
7. **Acknowledgement.** The client acknowledges each snapshot with its generation. A stale
   acknowledgement, one that a resync has already superseded, is ignored. A refused snapshot
   clears `DeliveredRevisions`, so the next refresh sends it again.
8. **Resync is the repair primitive (§4.8).** `ServerRequestResync` clears every trace of the
   chunk, including a queued or in-flight snapshot. The next refresh then re-sends it: a snapshot
   if it has been edited, the pristine notice if not. Until this change, resync logged "needs
   step 5" and repaired nothing.

## 5. DEF-3's requirement list, one line each

| Requirement | Answer |
|---|---|
| Application scope | Per chunk, through the synced set (§3) |
| Read halos / coordinated baseline | Not needed: the kernel is pointwise (§2) |
| Duplicate handling | Stream order decides what a snapshot contains; revision checks on synced chunks catch any gap |
| Finite sync cut | The snapshot is read at send time, between commits (§4.2) |
| Atomic sync/live handoff | One ordered channel (§2) |
| Stale-fragment discrimination | Generation plus a strict assembler: any fragment out of sequence drops the partial snapshot and requests the chunk again |
| Cancellation | Leaving the radius drops a queued snapshot; one in flight completes on its acknowledgement |
| Backpressure | 64 KiB unacknowledged per connection, 2 snapshots per tick (§4.4) |

## 6. Evidence

| Check | Result |
|---|---|
| TerrainCore automation | **39/39**. New: `Replication.JoinInProgress`. Rewritten to the new rule: `Replay.Validation` |
| `JoinInProgress`: codec | a dug chunk round-trips byte-exact at 272 bytes; a truncated snapshot never decodes; worst-case noise splits into 9 fragments; the assembler rejects a start mid-way, a skipped fragment, another generation, or a short middle fragment, and names the chunk it lost |
| `JoinInProgress`: protocol | history made before the client existed; snapshots at **different cuts**, with multi-chunk ops in between; an op **dropped**, the gap detected, the chunks repaired while edits continue. **Every watched chunk ends equal to the server in data and revision, and every one is synced** |
| `JoinInProgress`: controls | a client that skips one snapshot **diverges**, and knows that chunk is unsynced. Replaying an op into a snapshot that already contains it, after a non-commuting op, **diverges**. So the test exercises the hazard it claims to close |
| `MP.Convergence -Rounds 2 -CheckpointCapture` | **PASS both rounds** (round 2 failed this morning). Round 2: each client installed 4 snapshots, then 54 live ops; all hashes matched |
| `MP.Convergence -Rounds 3 -IncludeObserver -CheckpointCapture`, 60 s | **PASS all three rounds**, 4 clients, 1,280 commits |
| `-DropOp 20` (new, MP.Resync) | **PASS both rounds**: client 0 discards op 20; the gap is detected in 4 chunks; resync is requested; 4 snapshots arrive; hashes match. The pass rule *requires* failures, a repair and matching hashes when a drop was injected |
| Snapshot install on a client | **16–25 ms per chunk** after the adapter fix (§6a), down from 65–165 ms |
| `Test-TerrainCheckpoint.py` | PASS (restore now goes through the bulk write) |
| `Test-TerrainRetention.py` | PASS: all 256 hashes survive reclamation and restart |
| `Test-TerrainLease.py` | PASS |
| Both targets | build |

### 6a. An adapter fix this work forced

`FVPLegacyBackend::WriteRegion` called `UVoxelDataTools::SetValue` once per voxel. That meant a
lock, an octree walk from the root, and a remesh request, 32,768 times per chunk. It now uses one
write lock, one `FVoxelMutableDataAccelerator`, and one `UpdateWorld` for the whole chunk: the
write twin of T-120's read fix. The stored values are the same, since the same `FVoxelValue` is
built from the same float, and every restart hash test agrees.

| Write path | Before | After |
|---|---|---|
| Client snapshot install, per chunk | 65–165 ms | 16–25 ms |
| Server restore, 4 chunks | 0.103 s | 0.026 s |
| **Server restore, 256 chunks (boot)** | **4.4 s** | **0.8–1.0 s** |

## 7. Self-review: what could still be wrong

- **The pointwise property is proved only for the reference backend.** For the plugin it is
  inferred from §4.10.3 and the adapter's clipping. The multiplayer hash matches are strong
  evidence, since interleaved snapshots and multi-chunk ops would diverge otherwise, but they
  are not a proof. A future kernel that reads neighbours (smoothing) would break §2 and must
  revisit this document.
- **A client can still hitch.** 16–25 ms per snapshot, and several can arrive in one frame. A
  100-chunk dug region costs roughly 2 s of cumulative stalls. E-6, a heavy region with 5,000
  edits and a joiner arriving mid-edit, has **not** been measured. About 3 ms of each install is
  a probe `ReadRegion` done only to learn the backend's header fields; that is cheap to remove.
- **Server snapshot cost is not measured.** Each one is a bulk read plus Zlib, budgeted at 2 per
  tick on an estimate of a few ms each.
- **Resync has no rate limit.** A malicious client could request 4,096 chunks and make the server
  re-read and re-send them. Bandwidth is bounded by backpressure; server read cost is bounded
  only by the 2-per-tick budget. Rate-limiting belongs with the rest of admission hardening.
- **Unsubscribing does not notify the client.** A chunk the client left can later fail a check
  when an op also touches a subscribed chunk. That costs one spurious resync snapshot, never a
  wrong world.
- **Unsynced chunks can look briefly wrong.** An op writes into an unsynced chunk's data, so
  until its snapshot arrives the player may see a partly edited chunk instead of a pristine
  one. It was already wrong before this change, just differently. The window is sub-second.
- **The listen-server path is written but untested.** Every test here uses a dedicated server.
- **Materials are still not replicated or read back** (K9, gate 1C). Snapshots carry zero
  materials and the plugin backend ignores them, on both sides.

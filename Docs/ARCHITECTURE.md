# ARCHITECTURE.md — v0 (stub)

> **Status: v0, deliberately incomplete.** This file records the *shape* the Director
> ruled at CP-002 (D-011, D-012, D-013). It is not yet a design.
>
> **Every type name below is a placeholder.** `ITerrainBackend`, `FTerrainEditOp`,
> `UTerrainAuthoritySubsystem` and friends are naming sketches from the review
> documents, not approved API. They become real only when **T-101A findings** (what the
> plugin's actual API provides) and the **blind benchmark**
> (`Docs/DUAL_AGENT_SETUP.md` section 6) produce **ARCHITECTURE v1** under **D-017**.
>
> Do not implement against these names. Interface design comes *after* contact with the
> real API — that is the whole point of the review's scope-down.

**Version:** v0 · **Opened:** CP-002 (2026-09-05) · **Basis:** D-011, D-012, D-013

---

## 1. Layers

```text
Gameplay / Tools / Machines
        |
        v
Authoritative Terrain Service          <-- game-owned, C++, server truth
  - permissions
  - validation
  - materials
  - operation semantics
  - revisions
  - resource yield
        |
        v
Persistent World State
  - deterministic base (seed + generator version + authored stamps)
  - chunk snapshots
  - edit journal
        |
        v
Terrain Backend Adapter                <-- the only code that knows the plugin
  - Voxel Plugin Free Legacy initially
  - replaceable (D-010)
        |
        v
Rendered terrain / collision / surface queries
```

**The plugin does not own the game's truth.** The game owns terrain semantics; the
backend implements and renders them. Plugin-specific types never cross out of the
adapter module, and gameplay code never includes plugin headers (D-011, AGENTS.md
section 4).

Why this matters: if gameplay called the plugin directly, the plugin API would embed
itself across the whole game, resource logic would depend on the renderer, network
messages and save files would become plugin-specific, replacing or upgrading the backend
would become a rewrite, and terrain could not be tested without a renderer.

## 2. Authority and sequencing

- **The server is the sequencing authority.** It assigns monotonically increasing
  operation IDs and per-chunk revisions. Clients apply the authoritative order; they
  never decide it.
- **Multi-chunk operations are sequenced once, globally.** One edit may touch several
  chunks. The affected chunk set is computed, the operation is sequenced once, and
  clients must never observe a half-applied operation indefinitely. The transaction
  mechanism may start simple, but it must be defined rather than implied.
- **Prediction is deferred.** The first version accepts round-trip latency: client
  requests, server confirms, client sees the change. Prediction is not part of the
  T-101B feasibility gate unless latency proves intolerable.
- **Relevancy is spatial.** Players receive terrain state for nearby and relevant chunks
  only, never every operation across the world. The server keeps global truth and
  replicates spatially.

## 3. Validation

The server validates every edit request even on a friends-only server — this prevents
accidental corruption and produces sane architecture before PvP ever ships:

- player reach to the target position
- operation radius within limits
- operation frequency (rate limiting)
- tool ownership, cooldown, durability, fuel/power
- zone permissions
- target chunk loaded and available
- resulting resource payout

## 4. Persistence shape (D-012, provisional)

- The deterministic base world is never saved redundantly — it is regenerated from seed
  + generator version + authored stamps.
- Each modified chunk carries a snapshot at revision R plus an append-only journal of
  operations after R.
- Compaction folds base + operations into a new snapshot on thresholds (operation count,
  bytes, idle time, shutdown).
- Join-in-progress = snapshot + operations since.
- SQLite holds structured entities: players, inventories, structures, machines, grids.
  Chunk data lives in versioned files.
- Every format carries a schema version and a migration path; old-save fixtures live in
  `Tests/Saves/`.

## 5. What v1 must add

Deferred to **D-017** / ARCHITECTURE v1, informed by T-101A findings and the blind
benchmark: concrete class and subsystem boundaries · data structures and wire format ·
server/client sequence diagrams · persistence schema · the chunk revision model ·
concurrency semantics · join-in-progress protocol · failure recovery · test plan ·
performance risks and budgets · the exact line between plugin-specific and game-owned.

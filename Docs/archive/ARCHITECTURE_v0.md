# ARCHITECTURE_v0.md — v0 (stub, archived)

> **ARCHIVED 2026-09-06 at CP-005.** Superseded as the project's architecture document by
> `Docs/ARCHITECTURE.md` v1 (D-017 / D-022). Moved here from `Docs/ARCHITECTURE.md`; the
> body below is unchanged from its CP-002 state. Kept because v1 is deliberately narrower
> than v0 — v1 covers **terrain authority, persistence and replication only**.
>
> **Still live in this file.** The following are *not* carried by v1 and remain in force
> until some later document covers them. They are requirements, not history:
>
> 1. **§1 — why the adapter boundary exists.** v1 states the boundary and its enforcement
>    (v1 §4.1 `Build.cs` separation, §8.1 the ownership table) but not the reasoning: if
>    gameplay called the plugin directly, the plugin API would embed itself across the whole
>    game, resource logic would depend on the renderer, network messages and save files would
>    become plugin-specific, replacing the backend would become a rewrite, and terrain could
>    not be tested without a renderer. This is the argument to reach for when the boundary is
>    questioned.
> 2. **§1 — surface queries as a consumer of the backend.** v0's bottom layer is "rendered
>    terrain / collision / **surface queries**". v1 §8.1 assigns meshing, LOD and collision
>    cooking to the plugin but defines no surface- or trace-query path through
>    `ITerrainBackend`. Gameplay already needs one — the T-101A dig aims by camera line trace,
>    and placement will need the same. Unowned in v1.
> 3. **§3 — tool ownership, cooldown and fuel as validation inputs.** v1 §4.4 validates
>    request identity, reach, radius against tool max, rate limit per source, tool/power/
>    durability, zone permission, self-clearance and chunk residency. It does **not** validate
>    that the requester owns or has equipped the tool, nor tool cooldown, nor fuel. Live.
> 4. **§4 — power grids in the entity store.** v0 lists SQLite entities as "players,
>    inventories, structures, machines, **grids**". v1 §4.7's `entities.sqlite` comment drops
>    `grids`. The electricity path is a VISION pillar and a standing drift check; the entity
>    store still has to hold power grids. Live.
>
> **Discharged, not live.** v0 §5 ("What v1 must add") is a checklist, and v1 delivers every
> item on it: class and subsystem boundaries (§4.1), data structures and wire format (§4.2),
> server/client sequence (§4.4), persistence schema (§4.7), the chunk revision model (K1),
> concurrency semantics (§4.5), join-in-progress (§4.8), failure recovery (§5), test plan
> (§6), performance budgets (§7), and the plugin/game line (§8). v0 §2 is explicitly
> superseded by v1 where they differ. v0 §3's remaining validation inputs and v0 §4's
> persistence shape are otherwise carried by v1 §4.4 and §4.7.
>
> Every type name in the body below is still a placeholder and was never approved API. The
> real names are in `Docs/ARCHITECTURE.md` v1 §4. Do not implement against this file.


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

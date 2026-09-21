→ No action. For your reading only.

# P-009 — Materials and physical yield (gate 1C, first half; DEF-6 measurement half)

**Status: specification and Architect ruling, 2026-09-21. Implemented as T-130.**
**Author:** Claude Opus. **Risk:** R3 (the yield half of build step 6). **Base:** `152e6f4` (T-129).
**Authority:** ARCHITECTURE §4.9, §4.10.3, K9 (D-024), DEF-6, P-003 §2, D-023 (technical
ruling, logged).

Writer and reviewer are the same agent. §6 is the self-review.

---

## 1. Scope: where build step 6 was cut, and why

Build step 6 has four parts: the material config (K9), measuring removed volume (§4.9), the
yield and placement policy (DEF-6), and crediting inventory durably (DEF-1). No inventory
exists, and crediting one safely needs P-003's settlement ledger. That is its own R3 increment
(**T-131**). This increment does everything below the economy:

- **Materials become real.** The server knows the game material of every voxel, stores it in
  checkpoints, sends it in join-in-progress snapshots, and answers `QueryPoint` with it.
- **Every edit measures what it physically moved**, per material, on the real plugin, and that
  measurement is recorded in the journal as `Measured`.
- **DEF-6's measurement half is ruled** (§2). Its economic half (tool efficiency, placement debit,
  capacity, residue) stays with T-131.

**No save-format change.** P-004 already reserved both the material half of every chunk payload
and the journal's `Physical` list. Until now they were always zero or `Unavailable`.

## 2. Ruling: what a physical measurement is (DEF-6, measurement half)

1. **Occupancy** is `TerrainOccupancy(v) = clamp((1 − v/32767)/2, 0, 1)`: 1 for fully solid, 0 for
   fully empty, linear in between. There is one definition, in `TerrainOpGeometry.h`, shared by
   every backend.
2. **`Removed` is signed, per game material, in microlitres.** For each changed voxel,
   `Δ = occ(old) − occ(new)`:
   - if Δ > 0, the volume is removed and credited to the voxel's **pre-edit** material;
   - if Δ < 0, the volume is placed and debited to **`Op.MaterialId`**.
   The per-material sums are rounded to whole µL.
3. **One op never mixes the two signs.** The op set is closed at Remove / Add / Paint, and each is
   monotone (§4.10.3), so a Remove only removes, an Add only places, and a Paint moves no volume.
   "Separate removal and placement accounting" therefore holds without separate lists.
4. **Conservation.** A voxel's occupancy can fall only as far as it previously rose. So the total
   removal ever credited from a region can never exceed what the world held plus what was placed
   there. That is true whatever the occupancy map is, and it is why the E-1 discrepancies below
   are accuracy findings, not mints.
5. **Placement material is server state** (`FTerrainSourceState::PlacementMaterial`, DEF-7), never
   client input. It stays Unknown (0) for players until inventory decides what they place. An
   Add with an Unknown material places volume, debits it to Unknown, and repaints nothing.
6. **`ITerrainBackend::MeasuresPhysicalYield()`** is a capability query with a `false` default. The
   service sets the journal's `bPhysicalMeasured` from it, so a backend that cannot measure
   records `Unavailable`, never a measured zero (P-003 §2). The reference backend and the plugin
   adapter both return true.

## 3. Ruling: the material id channel, without a visible change

K9 (D-024) ruled a switch to the plugin's SingleIndex material config, and made any visible change
a Director decision. Switching would change how terrain renders, which is a decision for the
Director, not a technical one. It also isn't needed:

- The world renders in the RGB config. The generator colours each voxel by its game material id
  through `TerrainMaterialDebugColor`, and every catalog entry has its own colour.
- **So the stored colour is the id, exactly.** The adapter builds the inverse table from the same
  call the generator makes. A colour outside the table reads as Unknown, never as the nearest
  match. If two catalog colours ever collide, the backend logs an error and
  `MeasuresPhysicalYield()` turns false.
- **Saves and the wire carry game ids, never colours.** Moving to SingleIndex later is therefore
  adapter-internal and touches no stored byte.
- **Old saves.** Chunks written before this change store material 0. `WriteRegion` treats 0 as
  "no material recorded" and leaves the voxel's generated material alone. It never paints an
  unmapped colour.

`ReadRegion`, `WriteRegion`, `QueryPoint` and Add painting all carry ids. `HashRegion` stays
**density-only on purpose**: `Adapter.DensityContract`'s four pinned fixtures are density hashes,
and their staying identical is the proof that this change moved no density sample. Materials are
verified by direct comparison instead (§5).

## 4. E-1, answered

§4.9 asked whether linear occupancy over int16 density measures volume accurately. The first
comparison was against the ideal sphere 4/3·π·R³. That was the wrong reference: the canonical
write set (§4.10.2) clips a dig, so the hole really is smaller than the ideal sphere. The truth
is **the hole a player sees**: the region where the trilinearly interpolated post-edit density
is empty, which is what an isosurface mesher draws. The check computes that independently, at
4×4×4 subsamples per cell.

| Radius (voxels) | Measured | Rendered hole | Ratio | Ideal sphere ratio |
|---|---|---|---|---|
| 3 | 11,913.7 L | 11,093.8 L | 1.074 | 0.843 |
| **4 (the 2 m player dig)** | 26,599.1 L | 25,966.8 L | **1.024** | 0.794 |
| 6 | 101,059.7 L | 100,593.8 L | 1.005 | 0.894 |
| 8 | 239,219.4 L | 239,968.8 L | 0.997 | 0.892 |

**Yield is within 2.4% of the visible hole at the gameplay radius, and within 0.5% from radius 6
up.** Tiny digs over-report by up to 7%. By §2.4 that cannot be farmed; it only affects how
closely the number matches the picture.

## 5. Evidence

| Check | Result |
|---|---|
| TerrainCore automation | **39/39** |
| `Terrain.AdapterChecks` (real plugin) | PASS, 20 runs. **The pinned density fixtures are unchanged.** New: every generated sample reads a catalog material; QueryPoint agrees with ReadRegion; WriteRegion/ReadRegion round-trip density **and** material byte-exact; E-1 within 10% of the rendered hole (see §4); a homogeneous dig yields exactly one material; a repeated dig yields nothing; a dig across a stone/iron-ore boundary yields both, and **the split total equals the single-material total to the µL** (101,059.658 L both ways); an Add with a material is measured as negative volume of that material and paints it; mining the placement back **never yields more than was placed**; an Add with no material repaints nothing |
| `Terrain.SelfTest` | PASS, now also checking that the probe has a real material (Dirt), the dig measured removal only (24.4 m³ dirt + 2.2 m³ stone), and the placement measured placement only. The yield reaches the requester in `FTerrainEditReceipt::Yield` |
| **Old save** (written by T-129, materials zero) | boots on the new build; all 8 chunk hashes equal the save's last state; the probe reads Dirt; SelfTest PASS |
| `MP.Convergence -Rounds 2`, **material hashes added** | PASS. The harness places iron ore in round 1 only (826 samples); round 2's clients can only get it by snapshot |
| **Mutation test** of that check | with clients ignoring snapshot materials, round 2 **FAILS**. (The first version of the check passed that mutant, because round 2 repainted the same voxels; that was fixed before it counted as evidence) |
| `MP.Convergence -Rounds 3 -IncludeObserver`, 60 s | PASS, all rounds |
| `-DropOp 20` | PASS both rounds |
| `Test-TerrainCheckpoint.py`, `Test-TerrainRetention.py` (256 hashes) | PASS. The save is 7,680 bytes larger over 512 edits: 15 bytes per journal record, now `Measured` |
| Both targets | build |

## 6. Self-review: what could still be wrong

- **Placing and re-digging leaks about 21%.** Adding a radius-4 sphere of dirt and removing the
  same sphere recovers 21,073 of 26,599 litres. The rest stays in the world as a partial shell,
  because Add and Remove are not exact inverses at the boundary. It is not a mint (§2.4), but
  once resources are real a player will feel it. It is T-131's policy question; for example, a
  mining radius slightly larger than the placing radius, or a placement debit equal to what can
  be recovered.
- **The id channel depends on the debug palette.** Two catalog materials must never share a
  colour. The guard makes that fail loudly rather than silently. It is also the first thing to
  revisit if terrain gets real materials, which is a visible change and so the Director's call.
- **The plugin's `HashRegion` is still density-only**, against §4.3's "values + materials". That
  is deliberate (§3). Materials are compared directly in the adapter checks and the multiplayer
  harness instead of through the convergence oracle.
- **Clients compute yield too,** which is wasted work: one material read per changed voxel. Cheap
  at the current radius cap, and removable later.
- **Player placements carry no material** until inventory exists, so in real play every Add
  debits Unknown. That is honest, but it means nothing in play exercises painting yet; only the
  adapter checks and the harness do.
- **Nobody is paid yet.** `EconomyKind` stays `NoEconomy` and nothing is credited. That is T-131:
  the inventory, the settlement ledger (DEF-1), and DEF-6's economic half.

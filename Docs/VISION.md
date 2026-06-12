# VISION.md — [Working Title: TBD, see D-008]

> **The founding document.** Immutable except by numbered entry in DECISIONS.md.
> Loaded at the start of every working session, alongside STATE.md.
> If current work contradicts this file, the work is wrong or a decision is missing.

**Founded:** 2026-06-12 · **Founder/Director:** Harjas · **Architect/Programmer:** Claude

---

## The Original Idea (founder's words, preserved)

An open-world multiplayer game where other people join. You can live off of the
world and manipulate it — harvest resources, terraform, create permanent
structures. More than just mining nodes in Rust and chopping trees.

World manipulation in the style of Minecraft, but the game looks and feels
realistic, like Rust or Satisfactory. **No blocks.**

Technology matters — not just primitive tools. Electricity and machines are a
thing.

A combination of Rust, Minecraft (modded with tech modpacks), Satisfactory,
No Man's Sky / Star Citizen, and Ark Survival Ascended. Interplanetary flight
is too big a scope — **one planet for the foreseeable future**, with different
regions and biomes.

Harvest everything Unreal Engine 5.7 has to offer. The game should look
super, super real.

Built the way Minecraft was built: vanilla before modded — few ores, few mobs,
simple terrain generation first, then grow.

---

## Inspiration Matrix — what we take, what we leave

| Source | We take | We leave |
|---|---|---|
| **Rust** | Survival loop, socket building, server community feel, realistic art bar | Full-loot 100+ player brutality, offline raiding misery |
| **Minecraft (tech modpacks)** | Free-form world manipulation, deep tech trees, automation joy, alpha-first dev philosophy | Blocky aesthetic |
| **Satisfactory** | Machines, power networks, logistics, "the factory grows" satisfaction | Pure factory focus — we keep survival at the core |
| **Ark: Survival Ascended** | Creature ecosystem, taming fantasy, biome danger | Grind-heavy balance (creatures = Year 2, per D-005) |
| **No Man's Sky / Star Citizen** | Sense of planetary scale and wonder | Space flight, multiple planets — **out of scope indefinitely** |

---

## Pillars (rank-ordered — when pillars conflict, lower number wins)

1. **The world is malleable and persistent.** Dig, terraform, tunnel, build
   anywhere. The server remembers everything at next login.
2. **Realistic, never blocky.** Smooth voxel terrain, Lumen lighting, scanned
   assets. Rust-quality visuals are the bar.
3. **Technology progression.** Primitive tools → crafting stations →
   electricity → machines → automation.
4. **Built for friends.** 16–32 players on a self-hosted dedicated server.
   Fun > polish > profit. No commercial pressure, ever.
5. **PvE heart, PvP edges.** Safe home regions to build; contested rich
   zones worth fighting over.

---

## What this game is NOT (scope guards)

- **Not commercial.** No Steam release, no monetization, no marketing.
- **Not space.** One planet. No ships, no orbits, no other worlds.
- **Not blocks.** If terrain ever reads as cubes, we have failed Pillar 2.
- **Not Rust-scale.** 16–32 players, not 100+. No custom netcode arms race.
- **Not pretty before playable.** Placeholder art is acceptable for all of
  Year 1. Systems first.

---

## Drift Checks

Run at every checkpoint. Any unchecked box halts feature work until resolved.

- [ ] Terrain is smooth-voxel and player-deformable
- [ ] Every gameplay system is server-authoritative
- [ ] The tech path still leads to electricity and machines
- [ ] Scope is still one planet, 16–32 players
- [ ] Development is still incremental, Minecraft-alpha style
- [ ] The five inspiration games above are still the reference set

---

## Success Definition

A weekend night, six-plus friends on the server: someone is terraforming a
base perimeter, someone is wiring a generator into a new machine, someone is
hunting in the forest, someone is pushing into a contested zone for rare ore.
Nobody is asking when the game will be "done." The world remembers all of it
tomorrow.

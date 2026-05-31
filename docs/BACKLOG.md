# ThumbyRogue — feedback backlog & plan

Running list of feedback/requests with the considered approach for each, so every
item gets full due consideration and nothing is dropped. Updated as work lands.

## ✅ Done (recent polish passes)
- Inventory shows what each item does (3-line detail: name, stats+affixes, action).
- Off-hand "Focus" → **Shield / Tome / Charm** (clearer names + variety).
- Dynamic light **softened**: graded line-of-sight shadows (per-sample, not
  all-or-nothing) + a dim ambient floor (74) so you can always see in shadow.
- Lights are now **Minecraft torches** (BLK_TORCH light source + drawn torch
  model) instead of out-of-place lamp blocks.
- Moving platforms: **only over lava chasms** (no random ones clipping through
  scenery), recoloured **grey stone** (no more purple).
- Enclosed dungeon: low **green** grass lip (was a camera-blocking grey massif).
- Flagstone floor with **4 tessellating slab variants**; brick walls restored.
- **Organic** lava chasms (noise-edged lakes) sunk below a bridge; more chests.

## 🔜 Planned (ordered)

### P1 — Wide enemy roster + item drops  *(next)*
- Add many cuboid creatures beyond rat/slime/skeleton/spider: **skeleton archer**
  (ranged), **kobold**, **goblin**, **fire sprite** (floating, fire), **demon**
  (big, tough), **bat** (erratic flyer), **zombie** (slow, tanky).
- Per-type: distinct multi-cuboid model, palette, size, hp/dmg/speed, and an AI
  flavour (ranged kiting for archer/sprite, swarming for bat/kobold, charging for
  demon).
- **Loot**: a per-type drop table — most still drop gold, but several have a
  chance to drop an actual gear item / gem / potion (e.g., goblins drop gear,
  fire sprites drop gems, demons drop rare+). Tie rosters into depth bands so
  each band feels different.
- Engine: extend `rogue_enemy` MODEL/DEFS tables + a `drop` field; the death
  event already carries the type, so the game's drop code keys off it.

### P2 — Chests + dropped-loot clarity
- **Chest model**: a proper treasure chest (wood body, banded iron straps, gold
  lock, hinged lid) with a clear **closed vs open** state + a glow when openable.
- **Show contents**: on open, a "Found: …" toast/list of what was inside; items
  spill as visible drops, then vacuum to the backpack.
- **Dropped loot beams**: a rarity-coloured vertical glow above every ground item
  so drops are obvious and you can read rarity from across the room; a floating
  name label when you're close.
- Separate enemy drops (lie on the ground) from chest hauls (announced).

### P3 — Moving platform = the *only* way
- Platforms shouldn't be a redundant alt-crossing. Make each chasm platform the
  sole route to a **bonus chest on a lava island** (chest surrounded by lava,
  reachable only by riding the platform out to it). Record island pos in level
  info; place a (better-loot) bonus chest there; platform ferries bridge→island.

### P4 — Map / minimap
- "Hard to track where you've been." Add a **fog-of-war minimap**: a visited grid
  (mark cells near the player each tick), drawn as a compact corner overlay
  showing explored rooms/corridors, the player dot, and up/down stairs + the
  merchant. Reset per floor. Possibly a full-screen map page too.

### P5 — Water levels
- Some floors get **water** for variety: shallow pools (cosmetic + slow), a
  band/biome theme (e.g., "Flooded Vault") with water-filled rooms, maybe a
  swim/wade mechanic. Engine already renders animated water (BLK_WATER).

### P6 — Combat & spell polish  *(the big one)*
- **Clearer attack animations**: bigger, readable melee swing arcs (a visible
  sweep/trail), weapon-class-specific tells.
- **Spells**: animated projectile sprites + **particle effects** (impact bursts,
  trails, fire/ice/arcane themed by weapon), screen-shake/flash on big hits.
- A lightweight **particle system** (pooled cuboid/point particles with gravity,
  fade, colour ramps) reused for hits, deaths, lava embers, torch sparks, pickups.
- General juice: hit-stop, damage numbers polish, death poofs.

### Smaller / ongoing
- Keep refining level design variety (room shapes, set-pieces, special rooms:
  vault/shrine already planned in PLAN.md but not built).
- Device perf pass once content is in (the LOS shadow samples + many enemies).

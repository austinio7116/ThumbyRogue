# ThumbyRogue — Design & Plan

An endless, real-time, isometric hack-n-slash roguelike for the Thumby Color
(RP2350, 128×128 RGB565), built on a **vendored copy of the ThumbyCraft voxel
engine**. Each dungeon floor is a self-contained 3D world generated into
ThumbyCraft's static **64×64×64** voxel buffer; you descend an endless
staircase and depth is your score.

## 1. Design pillars

- **Real-time hack-n-slash** combat (Diablo/Spelunky/Dead-Cells feel).
- **Pure permadeath** — one life, no meta-progression carryover. Score = depth.
- **Fixed isometric camera with 90° snap-rotation** (LB/RB) to peek around walls.
- **Single gear-defined hero** — "your weapon is your class": a found staff
  makes you a caster, a bow a ranger, dual daggers a fast bruiser. Build
  identity emerges from this run's loot.

**Why it fits:** A chunky-voxel 3D iso roguelike is rare on any handheld.
ThumbyCraft already gives us the exact primitives the genre needs — a DDA voxel
raycaster with a z-buffer, a cuboid model renderer (mobs/torches/tools are all
multi-cuboid models), a lightmap, chests, an inventory, and a damage/HP/respawn
system. We reskin and re-purpose rather than build a renderer.

## 2. Controls

| Input | Action |
|-------|--------|
| D-pad | Move (screen-relative; rotated by current iso yaw) |
| A | Primary attack (swing equipped weapon — arc/range/speed from the item) |
| B | Secondary (ranged/spell/offhand; charge- or ammo-gated; reuses bow auto-aim) |
| LB / RB | Snap-rotate the iso view ±90° (~150 ms tween) |
| RB-hold / double-tap | Dodge-roll with i-frames (primary defensive tool) |
| MENU | Inventory / equip + pause |

## 3. What we reuse vs. build

**Reuse from ThumbyCraft (vendored):** voxel raycaster + z-buffer
(`craft_render.c`); cuboid model renderer (`CuboidPart[]` path — `craft_mobs.c`
`MAX_PARTS=17`, `craft_torches.c`, `craft_tool_models.h`) — this draws every 3D
enemy, weapon, chest, pickup and stair; existing mobs as a starting bestiary
(`MOB_SLIME/SKELETON/SPIDER/CREEPER/BOSS_SPIDER`); lightmap + day/night
brightness repurposed as dungeon darkness + torchlight; chests (`craft_chests.c`,
4×16 slots); HP/damage/knockback/hit-flash/respawn (`craft_player.c`); save layer
(`craft_save.c`); HUD primitives (`craft_hud.c`).

**Build new (`rogue_*` in `src/`):** iso follow-camera w/ 90° snap-rotate;
bounded static-level model (no sliding window); dungeon generator; per-instance
item + affix system; real-time combat layer; depth-band content system;
run/score flow.

**Dropped (SRAM discipline — a roguelike doesn't need them):** redstone,
furnace/smelting. `craft_world`'s three `craft_redstone_*` hooks are no-op'd
(will become a vendor patch). The 48 KB sliding-window mod-hash is also
reclaimable since a bounded floor has no player block-edits.

## 4. World & level model

- **One floor = the static 64³ buffer.** `craft_world_origin_x/z = 0`; do NOT
  call `craft_world_maybe_shift`. Replace `craft_world_load_around` with
  `rogue_level_generate(seed, depth, band)` writing blocks directly into
  `craft_world`, then rebuild the lightmap.
- **2.5D footprint, 3D presentation.** Layout is a 64×64 plan in X/Z; the Y axis
  (~12–20 cells) carries walls, ceilings, elevation, pits and ramps for tactical
  depth — not a full 3D maze (unreadable at 128px iso).
- **Descent loop:** spawn on up-stairs → fight/loot → reach down-stairs →
  regenerate for `depth+1`. Endless.

### Generation (hybrid; per band)
- **Structured floors:** BSP partition → rooms → corridor carve.
- **Cavern floors:** cellular-automata + drunkard's walk for organic caves.
- **Solution-path guarantee (Spelunky-style):** carve a guaranteed
  start→down-stairs path first, then decorate off-path rooms + optional
  branches. Every floor is provably completable — critical under permadeath.
- **Special rooms** stamped as whole units (reuse `craft_gen_stamp_features`
  region pattern): treasure vault (locked), shop, shrine/altar, boss arena.
- **Reachability validator** (flood-fill start→stairs; crib ThumbyLunky's
  validator) re-rolls or bores a fallback tunnel on failure.

## 5. Camera & controls

`rogue_camera`: fixed **pitch ≈ −0.62 rad** (~35° 3/4 tilt), positioned at a
fixed offset behind/above the hero and lerped to follow. Perspective FOV kept
narrow for a near-orthographic look (true ortho deferred — perspective-iso reads
fine at this scale). LB/RB snap-rotate yaw ±90°. Movement is screen-relative.

## 6. Combat (real-time, readable, fair)

Rule: **every death has a clear cause.**
- **Player attacks:** weapon defines arc, range, speed, damage type. Melee =
  short cone/AABB sweep; ranged/caster = projectile/hitscan w/ auto-aim. Crits +
  on-hit elemental from affixes.
- **Enemy telegraphs:** each hostile has a wind-up state (part-tint flash + brief
  freeze + optional ground marker via HUD projection) before a strike — extends
  `hurt_flash`/AI-timer in `craft_mobs.c`. No un-telegraphed damage.
- **Dodge-roll** w/ i-frames (intentionally strong).
- **Juice:** hit-flash, knockback, screen-shake, rumble, damage popups.
- **Depth scaling:** enemy hp/damage/speed scale with depth + band.
- **Budget:** stay within `CRAFT_MAX_MOBS=16`; cuboid render + AI is the hot path.

## 7. Depth bands (endless escalation)

Themed bands every ~4–5 floors reskin blocks (block variety + biome tint), swap
palette + enemy roster + music, raise difficulty; each ends in a boss arena.
After the deepest authored band, bands loop with escalating modifiers.

v1 bands: **Crypt** (brick/stone, skeletons & rats) → **Caverns** (rock/lava CA
caves, slimes & bats) → **Fungal Depths** (spore mobs, poison) → **Frostvault**
(ice, slow/freeze) → **Inferno** (fire) → loop+modifiers. Bosses start with a
reskinned `BOSS_SPIDER`, adding new cuboid bosses over phases.

## 8. Items, gear & affixes (gear-defined build)

ThumbyCraft's `inventory[BLK_COUNT]` is count-per-type and can't carry rolled
stats, so add a small **item-instance system** (`rogue_items.c`):
- **Instance:** base_type (weapon class / armor slot / relic / consumable),
  rarity (common/magic/rare/unique), up to N rolled affixes (Diablo
  prefix+suffix model: same affix, value range scales by tier/depth), derived
  stats.
- **Equip slots:** weapon (defines playstyle), armor, 1–2 relics, consumables.
  No class select — picking up a staff *makes you* a caster for the run.
- **Loot:** enemy drops + chests (reuse `craft_chests` containers + chest menu
  UI, backed by item instances), rarity-weighted by depth. Gold for shops.
- **Unidentified-lite:** affixes revealed on pickup (keeps it snappy). Deep
  identify/curse out of scope for v1.

## 9. Tension & roguelike systems

- **Light as a resource (signature mechanic):** dungeons are dark; the hero
  carries a torch with limited fuel / a shrinking light radius. Low light →
  enemies hit harder / see you first. Torches/oil lootable. Leans on the
  existing lightmap + entity lighting.
- **Traps** (spikes, dart, collapsing floor) — reuse pressure-pad/dispenser
  sprite-block machinery.
- **Shrines/altars** — risk/reward (sacrifice HP/gold for gear/buffs).
- **Permadeath flow:** death → run-summary (depth, gold, kills, best gear) →
  high-score table. A suspend-save resumes an in-progress run after quitting;
  it is wiped on death.

## 10. Performance & SRAM targets

- 30 FPS target; cap concurrent hostiles + bbox-cull cuboid renders.
- Floor regen during a brief stairs transition.
- Keep item-instance pool small + fixed.
- Static RAM: 64³ world (256 KB) + lightmap (64 KB) + framebuffer (32 KB) is the
  floor; gameplay modules linked only as phases use them. Reclaim the 48 KB
  mod-hash once bounded levels land.

## 11. Phased delivery

- **Phase 0 — Scaffold. ✅ DONE.** ThumbyRogue created; ThumbyCraft engine
  vendored to `engine/src` (+ `VENDOR_NOTES.md`); host + device CMake stood up;
  unmodified engine renders a textured world at the iso tilt on both targets
  with a lean module set (no redstone/furnace/gameplay).
- **Phase 1 — Iso camera + bounded level.** `rogue_camera` (fixed pitch, follow,
  90° snap-rotate), screen-relative movement, one hand-built static 64³ test
  room, sliding-window disabled.
- **Phase 2 — Dungeon generator.** `rogue_gen` (BSP rooms+corridors), solution-
  path guarantee + reachability validator, up/down stairs, descend-to-regenerate,
  depth counter.
- **Phase 3 — Real-time combat.** Melee arc + dodge-roll i-frames, enemy
  telegraph state, hit juice, depth-scaled stats. Reskin existing mobs as the
  Crypt roster.
- **Phase 4 — Loot & gear.** `rogue_items` instances + affixes + rarities,
  equip/compare UI, weapon-defines-playstyle, chests as loot, gold + drops,
  combat HUD.
- **Phase 5 — Depth bands & content.** Band reskins + rosters + palettes, special
  rooms (vault/shop/shrine/boss arena), per-band bosses, new cuboid monsters.
- **Phase 6 — Tension & run flow.** Light/torch-fuel resource, traps, shrines,
  permadeath summary + high-score table + suspend-save.
- **Phase 7 — Audio, polish, balance, device perf pass, ThumbyOne slot.**

## 12. Out of scope for v1

Multiplayer; deep identify/curse system; hunger; class-select; meta-progression
(pure permadeath by design); hand-authored levels; true orthographic projection.

## 13. References

- Procgen (BSP / cellular automata / drunkard's walk): Cogmind dev blog,
  RoguelikeDev resources.
- Spelunky template-chunk + solution-path generation: Spelunky Wiki.
- Diablo affix / prefix-suffix / tier system: Diablo Wiki.
- Action-roguelike combat readability (telegraphs, dodge): Dead Cells / Hades.
- Engine internals: `engine/VENDOR_NOTES.md`; project memories
  `feedback_thumbycraft_rendering`, `feedback_thumbycraft_biomes`,
  `project_thumbygolf` (vendoring pattern), `feedback_pico_heap_default`,
  `feedback_no_usb_cdc`, `feedback_thumbyone_default_build`.

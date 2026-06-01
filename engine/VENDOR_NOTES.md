# Vendored engine snapshot

This directory contains a **frozen copy of the ThumbyCraft voxel engine**
sources, taken from `../../ThumbyCraft/` on **2026-05-31**.

ThumbyRogue is a real-time isometric roguelike built on ThumbyCraft's DDA
voxel raycaster, z-buffer, cuboid model renderer (mobs/torches/tools),
lightmap, chests and HP/damage systems. The roguelike-specific engine
changes (fixed iso camera, bounded static levels, real-time combat hooks)
make no sense upstream, so we vendor rather than fork in place.

## Layout

- `src/` — every `.c` / `.h` from `ThumbyCraft/src/` (48 files)
- `tools/` — texture baker (`bake_textures.c` + cmake) and helper scripts
- `host/` — minimal platform stubs (chunk-store no-op)

## ThumbyRogue-specific patches on the vendored copy

| File | Change | Phase |
|------|--------|-------|
| `src/craft_render.{c,h}` | `ROGUE_FULLFRAME_RENDER` guard, three changes: (1) raycaster no longer skips the bottom hotbar-plate rows → world renders edge-to-edge (removes the toolbar gap); (2) `trace_ray` advances rays from an OUT-OF-WORLD origin to the world-AABB entry point and seeds the DDA there, so the iso camera can sit back beyond the world edge and the hero stays centred (no camera clamping); (3) `craft_render_set_light_pos(x,y,z)` sets the player-light bubble origin (the hero's head) so the torch tracks the player not the pulled-back camera; (4) `craft_render_set_light_intensity()`/`_radius()` + a smooth quadratic torch falloff (replaces the 3 hard rings with a continuous gradient that dims/shrinks as torch fuel drops). All no-ops when the macro is off → identical to upstream. Defined in both CMakeLists. | 1, 2, 6 |

| `src/craft_world.c` | SRAM reclaim for the 520KB device: `MOD_TABLE_SIZE` 2048→64 (the block-edit mod hash — ThumbyRogue writes the level via `craft_world_set_byte`, which bypasses mods, so the big table is dead weight; ~23KB) and `LIGHTSRC_MAX` 1024→512 (a bounded floor never has 1024 simultaneous light sources; ~6KB). | reclaim |
| `src/craft_audio.c` | `DELAY_SIZE` 2048→1024 — shorter reverb tail, saves 2KB. | reclaim |
| `src/craft_blocks.{c,h}` | `bake_water_frame` recoloured from clear blue to **dank murky green** (`rgb565(22, 64+band, 40)`) — dungeon/crypt water should read as stagnant, not a swimming pool. Added ThumbyRogue band blocks (BLK_CAVE_ROCK / MYCELIUM / FUNGAL_WALL / MUSHROOM) and **room-scenery blocks** (BLK_BOOKCASE / BARREL / CRATE / SARCOPHAGUS / CRYSTAL cubes + the cross-sprite set BLK_BONES / RUBBLE / SHARDS / FUNGI / COBWEB) with baked textures. Cubes are solid (extended the `BLK_RFLOOR..BLK_CRYSTAL` solid range); the cross-sprites are non-opaque + non-solid (`ROGUE_IS_SPRITE_SCENERY`). | polish |
| `src/craft_render.c` | `s_block_class[]` marks the five ThumbyRogue cross-sprite scenery ids as `BCLASS_CROSS` so they render as two perpendicular cutout quads (like tall grass). No other render change — non-grass CROSS blocks already sample their own untinted texture. | polish |
| `src/craft_world.c` | `is_light_source()` also returns true for `BLK_CRYSTAL` so crystal clusters emit light into the lightmap. | polish |
| `src/craft_render.{c,h}` | **X-ray walls that cover the hero** (ROGUE_FULLFRAME_RENDER): `craft_render_set_xray(x,feet,z,radius)` defines a thin cylinder along the camera→hero sightline; wall cells (by>=feet) inside it and nearer than the hero are passed through by the DDA (TraceHit.passed_xray) and veiled in render_strip, so only the blocks actually occluding the character turn translucent. Floor cells and walls beyond the hero stay solid. | polish |

Planned patch points (filled in as phases land):
- `craft_render.{c,h}` — fixed-pitch iso camera helper, 90° snap-yaw.
- `craft_world.{c,h}` — bounded static level (no sliding window).
- `craft_mobs.{c,h}` — enemy attack-telegraph state, depth-scaled stats.
- `craft_player.c` — dodge-roll i-frames, weapon-defined attack arcs.

## Re-vendoring

The user develops ThumbyCraft in parallel — **never edit `../../ThumbyCraft/`
from ThumbyRogue work.** To pull useful upstream improvements, manually diff
`../../ThumbyCraft/src/*` against `engine/src/*` and copy only the pieces that
don't conflict with the patches listed above. Don't blanket-copy.

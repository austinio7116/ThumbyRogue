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
| _(none yet — Phase 0 boots the engine unmodified)_ | | |

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

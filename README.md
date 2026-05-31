# ThumbyRogue

An **endless, real-time, isometric hack-n-slash roguelike** for the
[Thumby Color](https://color.thumby.us/) (RP2350, 128×128 RGB565), built on a
**vendored copy of the ThumbyCraft voxel engine**.

Descend an endless staircase of procedurally-generated dungeon floors, each a
self-contained 3D world rendered in chunky voxels at a fixed isometric tilt.
One life — **pure permadeath** — and **your weapon is your class**: the gear
you find this run *is* your build. How deep you get is your score.

> Design pillars: real-time hack-n-slash · pure permadeath · fixed-iso camera
> with 90° snap-rotation · single gear-defined hero. Inspirations: Diablo's
> loot, Spelunky's guaranteed-solvable procedural floors, Angband's one-life
> depth obsession, Dead-Cells/Hades combat readability.

See **[PLAN.md](PLAN.md)** for the full design and phased roadmap.

## Status

**Phase 0 — Scaffold: done.** The vendored engine builds and renders a
textured voxel world at the isometric tilt on both host (SDL2) and device
(RP2350). No game loop yet — that's Phase 1+.

## Layout

```
ThumbyRogue/
├── PLAN.md            design + phased roadmap
├── src/               rogue_* gameplay (added per phase)
├── engine/            FROZEN vendored ThumbyCraft engine (see engine/VENDOR_NOTES.md)
│   ├── src/           48 craft_* sources
│   ├── tools/         texture baker
│   └── host/          chunk-store no-op stub
├── host/              SDL2 host build (host_main.c + CMakeLists.txt)
├── device/            RP2350 firmware (rogue_device_main.c + platform + CMakeLists.txt)
└── docs/              design dossiers
```

**The engine under `engine/` is a frozen snapshot — never edited in
`../ThumbyCraft/`.** All roguelike-specific engine changes live only in the
vendored copy, tracked in `engine/VENDOR_NOTES.md`.

## Build & run

### Host (Linux/SDL2) — primary dev loop
```bash
cmake -B build_host -S host
cmake --build build_host -j8
./build_host/thumbyrogue_host [seed]
```
Keys: `WASD` pan · `Q`/`E` rotate · `Z`/`X` raise/lower · `R` regen · `F` fog ·
`ESC` quit. Headless render: `ROGUE_SHOT=out.ppm ./build_host/thumbyrogue_host`.

### Device (RP2350)
```bash
cmake -S device -B build_device -DPICO_SDK_PATH=$HOME/mp-thumby/lib/pico-sdk
cmake --build build_device -j8
cp build_device/thumbyrogue.uf2 ../firmware_thumbyrogue.uf2
```
Flash: enter BOOTSEL (off → hold DOWN → on), copy the `.uf2` to the RP2350
drive.

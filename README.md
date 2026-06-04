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

See **[PLAN.md](PLAN.md)** for the full design and phased roadmap, and the
**[illustrated user guide](https://austinio7116.github.io/ThumbyRogue/)** (loot,
level-gen, every band & icon).

## Status

**Playable on host + device.** Full run loop: title → descend → permadeath
summary. Implemented: iso camera (90° rotate), BSP dungeons with guaranteed
paths, real-time combat (melee/ranged/caster + dodge→jump), jump/platforming
(lava pits & bridges, pedestals, moving platforms), 5 depth bands + champions,
the **Diablo-4-lite loot system** (6 equip slots, rarity + affixes incl. 8
weapon elements, legendary aspects, sockets/gems, salvage, a merchant stall
with a shopkeeper who fights back) with a paperdoll inventory
screen, light/torch tension, traps, and procedural audio. See the
**[illustrated user guide](https://austinio7116.github.io/ThumbyRogue/)** for
the full loot & level-gen breakdown.

## Changelog

### 1.1

The post-release playtest round plus three feature drops. Suspend saves from
1.0 use an older layout and are cleanly ignored (fresh title on first boot).

**Shops are real now**
* A built **merchant stall** on every floor — gold-trimmed counter, a robed
  shopkeeper on his gold-tiled alcove, and a wares-packed shelf wall (two new
  block textures). Walk up to the counter to trade; placement provably avoids
  the guaranteed solution path.
* **Provoke him at your peril** — any hit turns the shopkeeper into a
  blink-casting battle wizard (fans of three arcane bolts, teleports when
  cornered). Kill him and his unsold stock spills as free pickups, but every
  later shopkeeper this run attacks on sight and trading is over.

**Elements & explosive magic**
* **Magic detonates**: every wand/scepter/staff impact fires a circular
  shockwave and splashes half damage around the hit; piercing staves detonate
  on every enemy they pass through.
* **Eight weapon elements** — fire (burn), frost (45% chill), poison (DoT),
  lightning (arcs to a second enemy), holy (double vs the undead), shadow
  (life drain), void (implosion pull) and arcane force (1.2-cell launch) —
  rolled as weapon affixes or socketed via **eight new elemental gems**
  (Fire Opal → Aetherite). The classic four gems keep their stat identity.
  Elements tint projectiles, trails, impacts, shockwaves and melee sparks.
* **Lava burns enemies** — anything knocked into (or out over) a lava lake
  cooks fast; fire sprites and demons are immune, flyers stay above it.
  Force weapons turn chasm rooms into environmental kill zones.

**Real staircases**
* The up-stairs is a walled stone stairwell; the way down is a **2-wide
  staircase cut into the floor** — three treads descending under a stone
  hood, lit from inside, flanked by glowing crystal shards with teal motes
  rising over the steps. Beacons removed. A carve that would sever a level's
  only route rolls back and retries (verified over 24,000 generated floors).

**Playtest round (visibility, combat feel, fairness)**
* X-ray occlusion fade is dark and **per-pixel exact** — only pixels that
  actually hide the hero fade, never neighbours or blocks behind.
* Hero and enemies render larger with brighter palettes; per-type locomotion
  patterns (rat darts, slime hops, bat weaves, spider circles, kobold zigzag,
  zombie lurch, demon charge) make every foe readable and predictable.
* Enemies hit ~50% harder from depth 1 and keep scaling; shop weapon upgrade
  is once per shop and gentler.
* Weapon swings are proper **slash crescents** with per-weapon impact FX and
  a hit-stop heartbeat; blunt weapons slam dust shock-rings.
* Enemies spawn only on open flat ground, never path or shoot through
  objects (per-cell movement + line-of-sight checks).
* Teal-blue animated water over a pebbled riverbed; loot beams now mean
  equipment rarity only (gold is a coin, torches and trinkets are models).

### 1.0

Initial release (shipped inside ThumbyOne 1.18): the full game — iso camera,
BSP dungeons with a guaranteed path, real-time combat, Diablo-4-lite loot,
five depth bands, light/torch tension, suspend save, procedural audio.

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

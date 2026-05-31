#include "rogue_gen.h"
#include "rogue_level.h"      /* ROGUE_FLOOR_Y */
#include "rogue_band.h"
#include "craft_world.h"
#include "craft_blocks.h"
#include <math.h>

#define GW   CRAFT_WORLD_X
#define GD   CRAFT_WORLD_Z
#define WALL_H     5
#define WALL_BLK   BLK_COBBLE
#define FLOOR_BLK  BLK_STONE
#define MIN_LEAF   12         /* smallest BSP region edge */
#define MAX_ROOMS  48
#define CORR_W     2          /* corridor width (cells) */

/* --- seeded RNG ------------------------------------------------- */
static uint32_t s_rng;
static uint32_t rng(void) {
    s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
    return s_rng;
}
/* inclusive [lo,hi] */
static int rr(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (int)(rng() % (uint32_t)(hi - lo + 1));
}

/* --- walkable grid --------------------------------------------- */
static uint8_t s_walk[GW * GD];   /* 1 = floor, 0 = wall */

typedef struct { int cx, cz; } RoomCenter;
static RoomCenter s_rooms[MAX_ROOMS];
static int s_n_rooms;

static void carve_rect(int x0, int z0, int x1, int z1) {
    if (x0 < 1) x0 = 1; if (z0 < 1) z0 = 1;
    if (x1 > GW - 2) x1 = GW - 2; if (z1 > GD - 2) z1 = GD - 2;
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++)
            s_walk[z * GW + x] = 1;
}

static void carve_h(int xa, int xb, int z) {
    if (xa > xb) { int t = xa; xa = xb; xb = t; }
    carve_rect(xa, z, xb, z + CORR_W - 1);
}
static void carve_v(int za, int zb, int x) {
    if (za > zb) { int t = za; za = zb; zb = t; }
    carve_rect(x, za, x + CORR_W - 1, zb);
}
static void carve_corridor(int x1, int z1, int x2, int z2) {
    if (rng() & 1) { carve_h(x1, x2, z1); carve_v(z1, z2, x2); }
    else           { carve_v(z1, z2, x1); carve_h(x1, x2, z2); }
}

/* Recursive BSP. Fills (*cx,*cz) with a representative room centre in this
 * region so the caller can wire a corridor between sibling regions. */
static void bsp(int x, int z, int w, int h, int depth, int *cx, int *cz) {
    bool can_split = depth < 5 &&
        (w >= 2 * MIN_LEAF || h >= 2 * MIN_LEAF) &&
        (depth < 2 || (rng() % 4) != 0);

    if (can_split && s_n_rooms < MAX_ROOMS - 2) {
        int c1x, c1z, c2x, c2z;
        bool split_w = (w >= h) ? true : false;
        if (w >= 2 * MIN_LEAF && (!(h >= 2 * MIN_LEAF) || split_w)) {
            int sp = rr(x + MIN_LEAF, x + w - MIN_LEAF);
            bsp(x, z, sp - x, h, depth + 1, &c1x, &c1z);
            bsp(sp, z, x + w - sp, h, depth + 1, &c2x, &c2z);
        } else {
            int sp = rr(z + MIN_LEAF, z + h - MIN_LEAF);
            bsp(x, z, w, sp - z, depth + 1, &c1x, &c1z);
            bsp(x, sp, w, z + h - sp, depth + 1, &c2x, &c2z);
        }
        carve_corridor(c1x, c1z, c2x, c2z);
        *cx = c1x; *cz = c1z;
        return;
    }

    /* Leaf: place a room with 1-cell padding inside the region. */
    int rw = rr(5, w - 2);
    int rh = rr(5, h - 2);
    if (rw > w - 2) rw = w - 2;
    if (rh > h - 2) rh = h - 2;
    if (rw < 4) rw = 4;
    if (rh < 4) rh = 4;
    int rx = rr(x + 1, x + w - 1 - rw);
    int rz = rr(z + 1, z + h - 1 - rh);
    carve_rect(rx, rz, rx + rw - 1, rz + rh - 1);
    int ccx = rx + rw / 2, ccz = rz + rh / 2;
    if (s_n_rooms < MAX_ROOMS) {
        s_rooms[s_n_rooms].cx = ccx;
        s_rooms[s_n_rooms].cz = ccz;
        s_n_rooms++;
    }
    *cx = ccx; *cz = ccz;
}

/* (A flood-fill reachability validator used to live here. Dropped to reclaim
 * 12KB SRAM: the BSP recursion connects every sibling region with a corridor,
 * so the room graph is always fully connected and the down-stairs reachable.) */

/* Pick the room centre farthest (Manhattan) from the up-stairs. */
static int farthest_room(int from) {
    int best = from, bestd = -1;
    for (int i = 0; i < s_n_rooms; i++) {
        int d = (s_rooms[i].cx - s_rooms[from].cx);
        if (d < 0) d = -d;
        int dz = (s_rooms[i].cz - s_rooms[from].cz);
        if (dz < 0) dz = -dz;
        d += dz;
        if (d > bestd) { bestd = d; best = i; }
    }
    return best;
}

/* --- background value noise (rolling Minecraft-ish terrain) ------ */
static uint32_t hash2(int x, int z, uint32_t seed) {
    uint32_t h = (uint32_t)(x * 374761393) ^ (uint32_t)(z * 668265263) ^ (seed * 2246822519u);
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static float vnoise(float fx, float fz, uint32_t seed) {
    int x0 = (int)floorf(fx), z0 = (int)floorf(fz);
    float tx = fx - x0, tz = fz - z0;
    float a = hash2(x0,   z0,   seed) / 4294967295.0f;
    float b = hash2(x0+1, z0,   seed) / 4294967295.0f;
    float c = hash2(x0,   z0+1, seed) / 4294967295.0f;
    float d = hash2(x0+1, z0+1, seed) / 4294967295.0f;
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);
    return a + (b - a) * tx + (c - a) * tz + (a - b - c + d) * tx * tz;
}
static int bg_height(int x, int z, uint32_t seed) {
    float n = vnoise(x / 9.0f, z / 9.0f, seed)
            + 0.4f * vnoise(x / 4.0f, z / 4.0f, seed ^ 0x55u);
    n /= 1.4f;
    /* A LOW enclosing rock lip just high enough that you can't walk/jump out a
     * ruined gap (top = FLOOR_Y+2, above the ~1.6-block jump), but not so tall
     * it walls off the iso camera's view. An occasional +1 for a little
     * craggy variation. */
    int h = ROGUE_FLOOR_Y + 1;
    if (n > 0.65f) h += 1;
    if (h >= CRAFT_WORLD_Y) h = CRAFT_WORLD_Y - 1;
    return h;
}

static bool is_walk(int x, int z) {
    if ((unsigned)x >= GW || (unsigned)z >= GD) return false;
    return s_walk[z * GW + x] != 0;
}

/* Build the world: open natural terrain everywhere, rooms as flat clearings,
 * thin (1-cell) ruined low walls outlining them. */
static void apply_to_world(uint32_t seed, int depth) {
    const RogueBand *band = rogue_band_get(depth);
    const uint8_t FLOOR = band->floor, WALL = band->wall;

    craft_world_clear();

    for (int z = 0; z < GD; z++) {
        for (int x = 0; x < GW; x++) {
            if (is_walk(x, z)) {
                /* Room/corridor: flat clearing — solid floor, open above. */
                for (int y = 0; y < ROGUE_FLOOR_Y; y++)
                    craft_world_set_byte(x, y, z, FLOOR);
                /* Flagstone bands scatter 4 slab variants across the surface
                 * so the floor tessellates with variety instead of a repeated
                 * tile. */
                if (FLOOR == BLK_RFLOOR) {
                    uint32_t hv = hash2(x, z, seed ^ 0xF100u);
                    craft_world_set_byte(x, ROGUE_FLOOR_Y - 1, z,
                                         (uint8_t)(BLK_RFLOOR + (hv % 4u)));
                }
                continue;
            }
            /* Border cell = touches a walkable neighbour → thin low wall. */
            bool border = is_walk(x+1,z) || is_walk(x-1,z) ||
                          is_walk(x,z+1) || is_walk(x,z-1);
            if (border) {
                uint32_t hh = hash2(x, z, seed ^ 0xBEEFu);
                if ((hh % 100u) < 18u) {
                    /* ruined gap — leave the floor exposed (a breach) */
                    for (int y = 0; y < ROGUE_FLOOR_Y; y++)
                        craft_world_set_byte(x, y, z, FLOOR);
                    continue;
                }
                int wh = 2 + (int)((hh >> 8) % 2u);     /* 2 or 3 tall */
                for (int y = 0; y < ROGUE_FLOOR_Y; y++)
                    craft_world_set_byte(x, y, z, FLOOR);
                for (int y = ROGUE_FLOOR_Y; y < ROGUE_FLOOR_Y + wh; y++)
                    craft_world_set_byte(x, y, z, WALL);
                continue;
            }
            /* Surrounding terrain: a LOW grass-topped earth lip on all sides —
             * green for contrast against the grey stone dungeon, but only ~2
             * tall so it encloses (you can't walk/jump out) without walling
             * off the iso camera. */
            int th = bg_height(x, z, seed);
            for (int y = 0; y <= th; y++) {
                uint8_t blk = BLK_STONE;
                if (y == th)          blk = BLK_GRASS;
                else if (y == th - 1) blk = BLK_DIRT;
                craft_world_set_byte(x, y, z, blk);
            }
        }
    }
}

void rogue_gen_dungeon(uint32_t seed, int depth, RogueLevelInfo *out) {
    s_rng = (seed ^ (0x9E3779B9u * (uint32_t)(depth + 1)));
    if (s_rng == 0) s_rng = 0xDEADBEEFu;

    for (int i = 0; i < GW * GD; i++) s_walk[i] = 0;
    s_n_rooms = 0;

    int rcx, rcz;
    bsp(2, 2, GW - 4, GD - 4, 0, &rcx, &rcz);
    if (s_n_rooms == 0) {           /* degenerate guard */
        carve_rect(8, 8, GW - 9, GD - 9);
        s_rooms[0].cx = GW / 2; s_rooms[0].cz = GD / 2; s_n_rooms = 1;
    }

    int up = 0;
    int down = farthest_room(up);

    apply_to_world(seed, depth);

    /* Lava chasms: turn a couple of rooms into an ORGANIC lava lake sunk a
     * couple of levels below the floor, spanned by a narrow 2-wide BRIDGE you
     * cross on foot (and, via rogue_platform, a moving platform). The blobby
     * lake edge comes from value noise, not square quadrants. Lava is
     * non-solid + erupts you out, so the bridge is the safe route but a miss
     * never soft-locks. Chasm centres are recorded for platform placement. */
    out->n_chasm = 0;
    for (int i = 0; i < s_n_rooms && out->n_chasm < 3; i++) {
        if (i == up || i == down) continue;
        bool force = (out->n_chasm == 0 && i == s_n_rooms - 1);  /* guarantee >=1 */
        if (!force && (hash2(s_rooms[i].cx, s_rooms[i].cz, seed ^ 0x1A7Au) % 3u) != 0u)
            continue;
        int cx = s_rooms[i].cx, cz = s_rooms[i].cz;
        int isx = cx - 4, isz = cz - 4;                      /* bonus island (marooned) */
        for (int dz = -7; dz <= 7; dz++) {
            for (int dx = -7; dx <= 7; dx++) {
                /* CROSS bridge (both axes) so the room is always safely
                 * crossable from any corridor — lava is instant death, so the
                 * critical path must never require touching it. */
                if (dz == 0 || dz == 1 || dx == 0 || dx == 1) continue;
                /* keep the bonus island solid (marooned in a lava quadrant) */
                if (dx >= -5 && dx <= -3 && dz >= -5 && dz <= -3) continue;
                float d = (float)(dx*dx + dz*dz);
                float rn = 4.6f + 2.4f * (vnoise((cx+dx) * 0.45f, (cz+dz) * 0.45f,
                                                 seed ^ 0x9F1u) - 0.5f) * 2.0f;
                if (d > rn * rn) continue;                   /* blobby lake edge */
                int x = cx + dx, z = cz + dz;
                if (!is_walk(x, z)) continue;
                craft_world_set_byte(x, ROGUE_FLOOR_Y - 1, z, BLK_AIR);
                craft_world_set_byte(x, ROGUE_FLOOR_Y - 2, z, BLK_LAVA);
                craft_world_set_byte(x, ROGUE_FLOOR_Y - 3, z, BLK_LAVA);
            }
        }
        out->chasm_x[out->n_chasm] = (int16_t)cx;
        out->chasm_z[out->n_chasm] = (int16_t)cz;
        out->island_x[out->n_chasm] = (int16_t)isx;
        out->island_z[out->n_chasm] = (int16_t)isz;
        out->n_chasm++;
    }

    /* Shallow water pools: organic 1-deep wadeable pools in ~1/4 of rooms (not
     * chasm or stairs rooms) for variety. Water is non-solid, so you step down
     * a block and wade through (slowed); the engine animates the surface. */
    for (int i = 0; i < s_n_rooms; i++) {
        if (i == up || i == down) continue;
        if ((hash2(s_rooms[i].cx, s_rooms[i].cz, seed ^ 0x2233u) & 3u) != 0u) continue;
        if ((hash2(s_rooms[i].cx, s_rooms[i].cz, seed ^ 0x1A7Au) % 3u) == 0u) continue; /* not lava rooms */
        int cx = s_rooms[i].cx, cz = s_rooms[i].cz;
        for (int dz = -5; dz <= 5; dz++)
            for (int dx = -5; dx <= 5; dx++) {
                float d = (float)(dx*dx + dz*dz);
                float rn = 3.2f + 2.0f * (vnoise((cx+dx)*0.5f, (cz+dz)*0.5f, seed ^ 0x77u) - 0.5f) * 2.0f;
                if (d > rn*rn) continue;
                int x = cx + dx, z = cz + dz;
                if (is_walk(x, z)) craft_world_set_byte(x, ROGUE_FLOOR_Y - 1, z, BLK_WATER);
            }
    }

    /* Verticality: raised plateaus + stepping-stone pillars in ~40% of rooms.
     * All 1 block high, so they're always jumpable from the ground and never
     * wall off the validated path — they just add high ground + hop routes. */
    {
        const RogueBand *vb = rogue_band_get(depth);
        for (int i = 0; i < s_n_rooms; i++) {
            if (i == up || i == down) continue;
            uint32_t h = hash2(s_rooms[i].cx, s_rooms[i].cz, seed ^ 0x7E12u);
            if (h % 5u >= 2u) continue;                              /* ~40% */
            if ((hash2(s_rooms[i].cx, s_rooms[i].cz, seed ^ 0x1A7Au) % 3u) == 0u)
                continue;                                           /* skip lava rooms */
            int cx = s_rooms[i].cx, cz = s_rooms[i].cz;
            if ((h >> 8) & 1) {
                /* raised plateau offset to one side (centre stays clear) */
                int ox = (h & 1) ? 2 : -6;
                for (int dz = -3; dz <= 3; dz++)
                    for (int dx = 0; dx <= 4; dx++) {
                        int x = cx + ox + dx, z = cz + dz;
                        if (is_walk(x, z))
                            craft_world_set_byte(x, ROGUE_FLOOR_Y, z, vb->floor);
                    }
            } else {
                /* scattered stepping-stone pillars to hop between */
                for (int s = 0; s < 5; s++) {
                    int sx = cx + ((int)((h >> (s * 3 + 2)) % 7u) - 3);
                    int sz = cz + ((int)((h >> (s * 3 + 14)) % 7u) - 3);
                    if (is_walk(sx, sz))
                        craft_world_set_byte(sx, ROGUE_FLOOR_Y, sz, vb->floor);
                }
            }
        }
    }

    /* Minecraft-style torches light ~2/3 of the rooms. BLK_TORCH is a proper
     * light source (lightmap propagates its glow) and the raycaster passes
     * through it, so the cell is an invisible light; rogue_game draws the
     * actual torch model (stick + flame) at each recorded position. */
    out->n_torch = 0;
    for (int i = 0; i < s_n_rooms && out->n_torch < 16; i++) {
        if (i == up || i == down) continue;
        if ((hash2(s_rooms[i].cx, s_rooms[i].cz, seed ^ 0x10Cu) % 3u) == 0u) continue;
        int bx = s_rooms[i].cx + 2, bz = s_rooms[i].cz + 2;
        if (!is_walk(bx, bz)) { bx = s_rooms[i].cx - 2; bz = s_rooms[i].cz - 2; }
        if (!is_walk(bx, bz)) continue;
        craft_world_set_byte(bx, ROGUE_FLOOR_Y, bz, BLK_TORCH);
        out->torch_x[out->n_torch] = (int16_t)bx;
        out->torch_z[out->n_torch] = (int16_t)bz;
        out->n_torch++;
    }

    craft_world_rebuild_lightmap();

    out->floor_y = ROGUE_FLOOR_Y;
    out->up_x = s_rooms[up].cx;   out->up_z = s_rooms[up].cz;
    out->down_x = s_rooms[down].cx; out->down_z = s_rooms[down].cz;
    out->n_rooms = s_n_rooms;
    for (int i = 0; i < s_n_rooms && i < ROGUE_MAX_LEVEL_ROOMS; i++) {
        out->room_cx[i] = (int16_t)s_rooms[i].cx;
        out->room_cz[i] = (int16_t)s_rooms[i].cz;
    }
    out->spawn = v3(out->up_x + 0.5f, (float)ROGUE_FLOOR_Y, out->up_z + 0.5f);
}

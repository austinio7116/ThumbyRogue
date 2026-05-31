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

/* Flood-fill reachable floor from (sx,sz); returns true if (tx,tz) reached. */
static bool reachable(int sx, int sz, int tx, int tz) {
    static uint8_t seen[GW * GD];
    static uint16_t stack[GW * GD];   /* indices < 4096 fit in 16 bits */
    for (int i = 0; i < GW * GD; i++) seen[i] = 0;
    int sp = 0;
    stack[sp++] = (uint16_t)(sz * GW + sx);
    seen[sz * GW + sx] = 1;
    const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
    while (sp > 0) {
        int idx = stack[--sp];
        int x = idx % GW, z = idx / GW;
        if (x == tx && z == tz) return true;
        for (int d = 0; d < 4; d++) {
            int nx = x + dx[d], nz = z + dz[d];
            if ((unsigned)nx >= GW || (unsigned)nz >= GD) continue;
            int ni = nz * GW + nx;
            if (seen[ni] || !s_walk[ni]) continue;
            seen[ni] = 1;
            stack[sp++] = (uint16_t)ni;
        }
    }
    return false;
}

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
    int h = (int)(ROGUE_FLOOR_Y - 3 + n * 6.0f);
    if (h < 1) h = 1;
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
            /* Open background: rolling natural terrain for life. */
            int th = bg_height(x, z, seed);
            for (int y = 0; y <= th; y++) {
                uint8_t blk = BLK_STONE;
                if (y == th)        blk = BLK_GRASS;
                else if (y > th - 3) blk = BLK_DIRT;
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
    /* Safety: if the (always-connected) graph somehow leaves down
     * unreachable, bore a direct corridor. */
    if (!reachable(s_rooms[up].cx, s_rooms[up].cz,
                   s_rooms[down].cx, s_rooms[down].cz)) {
        carve_corridor(s_rooms[up].cx, s_rooms[up].cz,
                       s_rooms[down].cx, s_rooms[down].cz);
    }

    apply_to_world(seed, depth);

    /* Scatter glowing braziers (lit-lamp blocks) through ~2/3 of the rooms
     * so the dungeon has fixed light sources beyond the hero's torch. The
     * lightmap rebuild below propagates their glow. */
    for (int i = 0; i < s_n_rooms; i++) {
        if (i == up || i == down) continue;     /* keep stairs cells clear */
        if ((hash2(s_rooms[i].cx, s_rooms[i].cz, seed ^ 0x10Cu) % 3u) == 0u) continue;
        /* Offset to a corner so the brazier never blocks the room centre. */
        int bx = s_rooms[i].cx + 2, bz = s_rooms[i].cz + 2;
        if (!is_walk(bx, bz)) { bx = s_rooms[i].cx - 2; bz = s_rooms[i].cz - 2; }
        if (!is_walk(bx, bz)) continue;
        for (int y = 0; y < ROGUE_FLOOR_Y; y++)
            craft_world_set_byte(bx, y, bz, FLOOR_BLK);
        craft_world_set_byte(bx, ROGUE_FLOOR_Y, bz, BLK_LAMP_ON);
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

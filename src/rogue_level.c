#include "rogue_level.h"
#include "craft_world.h"
#include "craft_blocks.h"

#define WALL_H   5
#define FLOOR_BLK  BLK_STONE
#define WALL_BLK   BLK_COBBLE
#define PILLAR_BLK BLK_PLANK

/* Fill an inclusive XZ rectangle at a single Y with a block. */
static void fill_rect_y(int x0, int x1, int z0, int z1, int y, uint8_t blk) {
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++)
            craft_world_set_byte(x, y, z, blk);
}

Vec3 rogue_level_build_test_room(uint32_t seed) {
    (void)seed;
    const int W = CRAFT_WORLD_X, D = CRAFT_WORLD_Z;

    /* Clear to air, then lay the floor substrate up to ROGUE_FLOOR_Y-1. */
    craft_world_clear();
    for (int y = 0; y < ROGUE_FLOOR_Y; y++)
        fill_rect_y(0, W - 1, 0, D - 1, y, FLOOR_BLK);

    /* Perimeter walls. */
    for (int y = ROGUE_FLOOR_Y; y < ROGUE_FLOOR_Y + WALL_H; y++) {
        fill_rect_y(0, W - 1, 0, 0,       y, WALL_BLK);
        fill_rect_y(0, W - 1, D - 1, D - 1, y, WALL_BLK);
        fill_rect_y(0, 0,       0, D - 1, y, WALL_BLK);
        fill_rect_y(W - 1, W - 1, 0, D - 1, y, WALL_BLK);
    }

    /* A few interior pillars to test collision + 90° rotation depth. */
    const int px[] = { 20, 44, 20, 44, 32 };
    const int pz[] = { 20, 20, 44, 44, 32 };
    for (int i = 0; i < 5; i++)
        for (int y = ROGUE_FLOOR_Y; y < ROGUE_FLOOR_Y + WALL_H; y++)
            fill_rect_y(px[i] - 1, px[i] + 1, pz[i] - 1, pz[i] + 1, y, PILLAR_BLK);

    craft_world_rebuild_lightmap();

    /* Spawn in the centre, feet on the floor surface. */
    Vec3 spawn = v3(W * 0.5f + 0.5f, (float)ROGUE_FLOOR_Y, D * 0.5f - 8.5f);
    return spawn;
}

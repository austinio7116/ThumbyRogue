#ifndef ROGUE_GEN_H
#define ROGUE_GEN_H
/*
 * ThumbyRogue dungeon generator. Carves a BSP rooms+corridors dungeon into
 * the static 64^3 craft_world buffer (origin 0,0). BSP recursion connects
 * every sibling region, so the room graph is fully connected — the down-
 * stairs are always reachable from spawn (verified by a flood-fill).
 */
#include <stdint.h>
#include "craft_types.h"

#define ROGUE_MAX_LEVEL_ROOMS 48

typedef struct {
    Vec3 spawn;            /* hero feet, on the up-stairs */
    int  up_x, up_z;       /* up-stairs cell (XZ) */
    int  down_x, down_z;   /* down-stairs cell (XZ) */
    int  floor_y;          /* walkable surface Y */
    int  n_rooms;
    int16_t room_cx[ROGUE_MAX_LEVEL_ROOMS];
    int16_t room_cz[ROGUE_MAX_LEVEL_ROOMS];
    int  n_chasm;                  /* lava-chasm rooms (bridge + platform) */
    int16_t chasm_x[3], chasm_z[3];
} RogueLevelInfo;

void rogue_gen_dungeon(uint32_t seed, int depth, RogueLevelInfo *out);

#endif /* ROGUE_GEN_H */

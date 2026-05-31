#ifndef ROGUE_PLAYER_H
#define ROGUE_PLAYER_H
/*
 * ThumbyRogue hero — position, screen-relative movement with block
 * collision, and a multi-cuboid render. The hero walks on a flat dungeon
 * floor (no gravity yet in Phase 1); Y is the floor surface.
 */
#include <stdint.h>
#include "craft_types.h"
#include "craft_render.h"
#include "craft_buttons.h"

typedef struct {
    Vec3  pos;        /* feet position */
    float yaw;        /* facing, radians (0 = +Z) */
    float move_phase; /* walk-cycle accumulator for limb swing */
    int   hp, max_hp;
} RoguePlayer;

void rogue_player_init(RoguePlayer *p, Vec3 spawn);

/* Advance the hero. `cam_yaw` is the camera's snapped yaw so D-pad input is
 * screen-relative (UP = away from camera). Collides against solid world
 * blocks at body height `floor_y`. */
void rogue_player_update(RoguePlayer *p, const CraftRawButtons *btn,
                         float dt, float cam_yaw, int floor_y);

void rogue_player_draw(const RoguePlayer *p, const CraftCamera *cam,
                       uint16_t *fb, int tint_q8);

#endif /* ROGUE_PLAYER_H */

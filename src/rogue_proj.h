#ifndef ROGUE_PROJ_H
#define ROGUE_PROJ_H
/*
 * ThumbyRogue projectiles — arrows (ranged) and magic bolts (caster). A
 * small pool; each flies straight, damages the first enemy it touches, and
 * dies on walls or range. Rendered as a little cuboid streak.
 */
#include <stdint.h>
#include "craft_types.h"
#include "craft_render.h"

void rogue_proj_clear(void);
void rogue_proj_fire(Vec3 pos, float yaw, float speed, int dmg,
                     int caster, float max_range, int pierce);
void rogue_proj_update(float dt, int floor_y);
void rogue_proj_draw(const CraftCamera *cam, uint16_t *fb);

#endif /* ROGUE_PROJ_H */

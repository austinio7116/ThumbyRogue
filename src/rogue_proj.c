#include "rogue_proj.h"
#include "rogue_render.h"
#include "rogue_enemy.h"
#include "craft_world.h"
#include "craft_blocks.h"
#include <math.h>

#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))
#define MAX_PROJ 10

typedef struct {
    bool  alive;
    Vec3  pos;
    float vx, vz;
    float travelled, max_range;
    int   dmg;
    int   caster;     /* 0 = arrow, 1 = bolt (visual) */
} Proj;

static Proj s_proj[MAX_PROJ];

void rogue_proj_clear(void) {
    for (int i = 0; i < MAX_PROJ; i++) s_proj[i].alive = false;
}

void rogue_proj_fire(Vec3 pos, float yaw, float speed, int dmg,
                     int caster, float max_range) {
    for (int i = 0; i < MAX_PROJ; i++) {
        if (s_proj[i].alive) continue;
        Proj *p = &s_proj[i];
        p->alive = true;
        p->pos = pos; p->pos.y += 0.55f;
        p->vx = sinf(yaw) * speed;
        p->vz = cosf(yaw) * speed;
        p->travelled = 0;
        p->max_range = max_range;
        p->dmg = dmg;
        p->caster = caster;
        return;
    }
}

static bool cell_solid(int wx, int wy, int wz) {
    return craft_block_solid(craft_world_get(wx, wy, wz));
}

void rogue_proj_update(float dt, int floor_y) {
    for (int i = 0; i < MAX_PROJ; i++) {
        Proj *p = &s_proj[i];
        if (!p->alive) continue;
        float dx = p->vx * dt, dz = p->vz * dt;
        p->pos.x += dx; p->pos.z += dz;
        p->travelled += sqrtf(dx*dx + dz*dz);
        if (p->travelled > p->max_range) { p->alive = false; continue; }
        if (cell_solid((int)floorf(p->pos.x), floor_y, (int)floorf(p->pos.z))) {
            p->alive = false; continue;
        }
        if (rogue_enemies_hit_point(p->pos.x, p->pos.z, 0.35f, p->dmg))
            p->alive = false;
    }
}

void rogue_proj_draw(const CraftCamera *cam, uint16_t *fb) {
    for (int i = 0; i < MAX_PROJ; i++) {
        Proj *p = &s_proj[i];
        if (!p->alive) continue;
        uint16_t c = p->caster ? RGB(120, 220, 255) : RGB(230, 210, 120);
        RogueCuboid body[1] = {
            { 0.0f, 0.0f, 0.0f, 0.08f, 0.06f, 0.14f, c }
        };
        float yaw = atan2f(p->vx, p->vz);
        rogue_render_model(cam, fb, p->pos, yaw, body, 1, 0.15f, 0.12f,
                           p->caster ? 0.6f : 0.2f, 256);
    }
}

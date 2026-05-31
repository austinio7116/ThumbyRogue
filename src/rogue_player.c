#include "rogue_player.h"
#include "rogue_render.h"
#include "craft_world.h"
#include "craft_blocks.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))

#define PLAYER_SPEED   5.0f    /* world cells / sec */
#define PLAYER_RADIUS  0.30f

/* Hero palette */
#define C_TUNIC   RGB(40, 90, 200)
#define C_TUNIC_D RGB(28, 64, 150)
#define C_SKIN    RGB(225, 175, 140)
#define C_HAIR    RGB(90, 55, 30)
#define C_BOOT    RGB(70, 50, 35)
#define C_BELT    RGB(120, 90, 40)
#define C_BLADE   RGB(210, 215, 225)

/* Model-local: feet at y=0, faces +Z, centred on x=z=0. ~1.25 tall. */
static const RogueCuboid hero_parts[] = {
    /* boots / legs */
    { -0.11f, 0.14f, 0.0f,  0.07f, 0.14f, 0.08f, C_BOOT  },
    {  0.11f, 0.14f, 0.0f,  0.07f, 0.14f, 0.08f, C_BOOT  },
    /* torso (tunic) */
    {  0.00f, 0.55f, 0.0f,  0.17f, 0.22f, 0.11f, C_TUNIC },
    {  0.00f, 0.36f, 0.0f,  0.18f, 0.05f, 0.12f, C_BELT  },
    {  0.00f, 0.55f, 0.10f, 0.10f, 0.18f, 0.02f, C_TUNIC_D },
    /* arms */
    { -0.21f, 0.55f, 0.0f,  0.05f, 0.18f, 0.06f, C_TUNIC_D },
    {  0.21f, 0.55f, 0.0f,  0.05f, 0.18f, 0.06f, C_TUNIC_D },
    /* hands */
    { -0.21f, 0.36f, 0.04f, 0.05f, 0.05f, 0.05f, C_SKIN },
    {  0.21f, 0.36f, 0.04f, 0.05f, 0.05f, 0.05f, C_SKIN },
    /* head + hair */
    {  0.00f, 0.92f, 0.0f,  0.12f, 0.13f, 0.12f, C_SKIN },
    {  0.00f, 1.02f, 0.0f,  0.13f, 0.06f, 0.13f, C_HAIR },
    /* sword held in right hand */
    {  0.27f, 0.55f, 0.06f, 0.02f, 0.30f, 0.02f, C_BLADE },
};
#define HERO_NPARTS ((int)(sizeof(hero_parts)/sizeof(hero_parts[0])))

void rogue_player_init(RoguePlayer *p, Vec3 spawn) {
    p->pos = spawn;
    p->yaw = 0.0f;
    p->move_phase = 0.0f;
    p->max_hp = 100;
    p->hp = 100;
}

/* True if the cell at world (wx,wy,wz) blocks movement. */
static bool cell_solid(int wx, int wy, int wz) {
    return craft_block_solid(craft_world_get(wx, wy, wz));
}

/* Can the hero's centre stand at (x,z)? Samples the body column at floor_y
 * with a radius margin in the direction of travel. */
static bool can_stand(float x, float z, int floor_y) {
    /* sample the 4 corners of the player's footprint box */
    float r = PLAYER_RADIUS;
    int x0 = (int)floorf(x - r), x1 = (int)floorf(x + r);
    int z0 = (int)floorf(z - r), z1 = (int)floorf(z + r);
    for (int cz = z0; cz <= z1; cz++)
        for (int cx = x0; cx <= x1; cx++)
            if (cell_solid(cx, floor_y, cz)) return false;
    return true;
}

void rogue_player_update(RoguePlayer *p, const CraftRawButtons *btn,
                         float dt, float cam_yaw, int floor_y) {
    /* Screen-relative movement basis from the (snapped) camera yaw. */
    float fx = sinf(cam_yaw), fz = cosf(cam_yaw);
    float rx = cosf(cam_yaw), rz = -sinf(cam_yaw);

    float mx = 0.0f, mz = 0.0f;
    if (btn->up)    { mx += fx; mz += fz; }
    if (btn->down)  { mx -= fx; mz -= fz; }
    if (btn->right) { mx += rx; mz += rz; }
    if (btn->left)  { mx -= rx; mz -= rz; }

    float len = sqrtf(mx*mx + mz*mz);
    if (len > 0.0001f) {
        mx /= len; mz /= len;
        p->yaw = atan2f(mx, mz);
        p->move_phase += dt * 8.0f;

        float step = PLAYER_SPEED * dt;
        /* Resolve axes independently so we slide along walls. */
        float nx = p->pos.x + mx * step;
        if (can_stand(nx, p->pos.z, floor_y)) p->pos.x = nx;
        float nz = p->pos.z + mz * step;
        if (can_stand(p->pos.x, nz, floor_y)) p->pos.z = nz;
    }
}

void rogue_player_draw(const RoguePlayer *p, const CraftCamera *cam,
                       uint16_t *fb, int tint_q8) {
    rogue_render_model(cam, fb, p->pos, p->yaw,
                       hero_parts, HERO_NPARTS,
                       0.32f, 1.15f, 0.0f, tint_q8);
}

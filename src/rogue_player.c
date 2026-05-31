#include "rogue_player.h"
#include "rogue_render.h"
#include "craft_world.h"
#include "craft_blocks.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))

#define PLAYER_SPEED   5.2f
#define PLAYER_RADIUS  0.30f
#define ATK_HITFRAME   0.11f   /* time into the swing the blow lands */
#define ATK_RECOVER    0.16f
#define DODGE_DUR      0.26f
#define DODGE_SPEED    12.0f
#define DODGE_CD       0.50f

/* Hero palette */
#define C_TUNIC   RGB(40, 90, 200)
#define C_TUNIC_D RGB(28, 64, 150)
#define C_SKIN    RGB(225, 175, 140)
#define C_HAIR    RGB(90, 55, 30)
#define C_BOOT    RGB(70, 50, 35)
#define C_BELT    RGB(120, 90, 40)
#define C_BLADE   RGB(210, 215, 225)

/* Sword is the LAST part so we can pose it during a swing. */
enum { P_SWORD = 11 };
static const RogueCuboid hero_parts[] = {
    { -0.11f, 0.14f, 0.0f,  0.07f, 0.14f, 0.08f, C_BOOT  },
    {  0.11f, 0.14f, 0.0f,  0.07f, 0.14f, 0.08f, C_BOOT  },
    {  0.00f, 0.55f, 0.0f,  0.17f, 0.22f, 0.11f, C_TUNIC },
    {  0.00f, 0.36f, 0.0f,  0.18f, 0.05f, 0.12f, C_BELT  },
    {  0.00f, 0.55f, 0.10f, 0.10f, 0.18f, 0.02f, C_TUNIC_D },
    { -0.21f, 0.55f, 0.0f,  0.05f, 0.18f, 0.06f, C_TUNIC_D },
    {  0.21f, 0.55f, 0.0f,  0.05f, 0.18f, 0.06f, C_TUNIC_D },
    { -0.21f, 0.36f, 0.04f, 0.05f, 0.05f, 0.05f, C_SKIN },
    {  0.21f, 0.36f, 0.04f, 0.05f, 0.05f, 0.05f, C_SKIN },
    {  0.00f, 0.92f, 0.0f,  0.12f, 0.13f, 0.12f, C_SKIN },
    {  0.00f, 1.02f, 0.0f,  0.13f, 0.06f, 0.13f, C_HAIR },
    {  0.27f, 0.55f, 0.06f, 0.02f, 0.30f, 0.02f, C_BLADE },  /* sword */
};
#define HERO_NPARTS ((int)(sizeof(hero_parts)/sizeof(hero_parts[0])))

void rogue_player_init(RoguePlayer *p, Vec3 spawn) {
    p->pos = spawn;
    p->knock = v3(0, 0, 0);
    p->yaw = 0.0f;
    p->move_phase = 0.0f;
    p->max_hp = 100;
    p->hp = 100;
    p->alive = true;
    p->atk_t = p->atk_cd = 0.0f;
    p->atk_hit_done = false;
    p->atk_hit_pending = false;
    p->dodge_t = p->dodge_cd = 0.0f;
    p->dodge_dx = p->dodge_dz = 0.0f;
    p->invuln_t = 0.0f;
    p->hurt_flash = 0.0f;
    /* Default melee weapon (Phase 4 makes this gear-driven). */
    p->wpn_range = 1.8f;
    p->wpn_arc_cos = 0.40f;   /* cos(~66°) half-arc */
    p->wpn_dur = 0.30f;
    p->wpn_dmg = 26;
}

static bool cell_solid(int wx, int wy, int wz) {
    return craft_block_solid(craft_world_get(wx, wy, wz));
}
static bool can_stand(float x, float z, int floor_y) {
    float r = PLAYER_RADIUS;
    int x0 = (int)floorf(x - r), x1 = (int)floorf(x + r);
    int z0 = (int)floorf(z - r), z1 = (int)floorf(z + r);
    for (int cz = z0; cz <= z1; cz++)
        for (int cx = x0; cx <= x1; cx++)
            if (cell_solid(cx, floor_y, cz)) return false;
    return true;
}
static void move_xz(RoguePlayer *p, float mx, float mz, float step, int floor_y) {
    float nx = p->pos.x + mx * step;
    if (can_stand(nx, p->pos.z, floor_y)) p->pos.x = nx;
    float nz = p->pos.z + mz * step;
    if (can_stand(p->pos.x, nz, floor_y)) p->pos.z = nz;
}

void rogue_player_update(RoguePlayer *p, const CraftRawButtons *btn,
                         bool atk_edge, bool dodge_edge,
                         float dt, float cam_yaw, int floor_y) {
    if (!p->alive) return;

    if (p->atk_cd   > 0) p->atk_cd   -= dt;
    if (p->dodge_cd > 0) p->dodge_cd -= dt;
    if (p->invuln_t > 0) p->invuln_t -= dt;
    if (p->hurt_flash > 0) p->hurt_flash -= dt;

    /* Knockback impulse (decays fast), collision-checked. */
    if (p->knock.x != 0 || p->knock.z != 0) {
        move_xz(p, p->knock.x, p->knock.z, dt, floor_y);
        float decay = 1.0f - 9.0f * dt;
        if (decay < 0) decay = 0;
        p->knock.x *= decay; p->knock.z *= decay;
        if (fabsf(p->knock.x) < 0.05f && fabsf(p->knock.z) < 0.05f)
            p->knock = v3(0,0,0);
    }

    float fx = sinf(cam_yaw), fz = cosf(cam_yaw);
    float rx = cosf(cam_yaw), rz = -sinf(cam_yaw);
    float mx = 0, mz = 0;
    if (btn->up)    { mx += fx; mz += fz; }
    if (btn->down)  { mx -= fx; mz -= fz; }
    if (btn->right) { mx += rx; mz += rz; }
    if (btn->left)  { mx -= rx; mz -= rz; }
    float len = sqrtf(mx*mx + mz*mz);
    if (len > 0.0001f) { mx /= len; mz /= len; }

    if (p->dodge_t > 0) {
        /* Rolling: locked direction, fast, invulnerable. */
        p->dodge_t -= dt;
        if (p->invuln_t < p->dodge_t) p->invuln_t = p->dodge_t;
        move_xz(p, p->dodge_dx, p->dodge_dz, DODGE_SPEED * dt, floor_y);
    } else {
        /* Start a dodge? */
        if (dodge_edge && p->dodge_cd <= 0 && len > 0.0001f) {
            p->dodge_t = DODGE_DUR;
            p->dodge_cd = DODGE_CD;
            p->dodge_dx = mx; p->dodge_dz = mz;
            p->yaw = atan2f(mx, mz);
        } else {
            /* Walk (slowed while mid-swing for weight). */
            float sp = PLAYER_SPEED * (p->atk_t > 0 ? 0.35f : 1.0f);
            if (len > 0.0001f) {
                if (p->atk_t <= 0) p->yaw = atan2f(mx, mz);
                p->move_phase += dt * 8.0f;
                move_xz(p, mx, mz, sp * dt, floor_y);
            }
            /* Start an attack? */
            if (atk_edge && p->atk_cd <= 0 && p->atk_t <= 0) {
                p->atk_t = p->wpn_dur;
                p->atk_hit_done = false;
            }
        }
    }

    /* Advance swing; raise the hit flag at the strike frame. */
    if (p->atk_t > 0) {
        p->atk_t -= dt;
        float elapsed = p->wpn_dur - p->atk_t;
        if (!p->atk_hit_done && elapsed >= ATK_HITFRAME) {
            p->atk_hit_done = true;
            p->atk_hit_pending = true;
        }
        if (p->atk_t <= 0) p->atk_cd = ATK_RECOVER;
    }
}

bool rogue_player_damage(RoguePlayer *p, int dmg, Vec3 from) {
    if (!p->alive || p->invuln_t > 0) return false;
    p->hp -= dmg;
    p->hurt_flash = 0.30f;
    p->invuln_t = 0.45f;   /* brief mercy i-frames after a hit */
    float dx = p->pos.x - from.x, dz = p->pos.z - from.z;
    float l = sqrtf(dx*dx + dz*dz);
    if (l > 0.001f) { p->knock.x = dx / l * 6.0f; p->knock.z = dz / l * 6.0f; }
    if (p->hp <= 0) { p->hp = 0; p->alive = false; }
    return true;
}

void rogue_player_draw(const RoguePlayer *p, const CraftCamera *cam,
                       uint16_t *fb, int tint_q8) {
    RogueCuboid parts[HERO_NPARTS];
    for (int i = 0; i < HERO_NPARTS; i++) parts[i] = hero_parts[i];

    /* Pose the sword: swing forward+down through the strike. */
    if (p->atk_t > 0) {
        float ph = 1.0f - (p->atk_t / p->wpn_dur);   /* 0..1 */
        float s = sinf(ph * (float)M_PI);             /* 0..1..0 */
        parts[P_SWORD].cz = 0.06f + 0.42f * s;        /* thrust forward */
        parts[P_SWORD].cy = 0.55f - 0.18f * s;        /* chop down */
        parts[P_SWORD].hz = 0.02f + 0.18f * s;        /* lengthen toward target */
    }

    float flash = (p->hurt_flash > 0) ? (p->hurt_flash / 0.30f) * 0.7f : 0.0f;
    /* Blink during i-frames (after a hit / while rolling). */
    if (p->invuln_t > 0 && ((int)(p->invuln_t * 30.0f) & 1)) return;

    rogue_render_model(cam, fb, p->pos, p->yaw, parts, HERO_NPARTS,
                       0.32f, 1.15f, flash, tint_q8);
}

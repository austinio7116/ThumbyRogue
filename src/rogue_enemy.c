#include "rogue_enemy.h"
#include "rogue_render.h"
#include "craft_world.h"
#include "craft_blocks.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif
#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))

typedef enum { AI_WANDER, AI_CHASE, AI_WINDUP, AI_STRIKE } AIState;

typedef struct {
    bool      alive;
    EnemyType type;
    Vec3      pos;
    float     yaw;
    int       hp;
    AIState   state;
    float     state_t;     /* time in current state */
    float     wander_dx, wander_dz;
    float     hurt_flash;
    float     atk_cd;
} Enemy;

static Enemy s_en[ROGUE_MAX_ENEMIES];

/* --- per-type stats ---------------------------------------------- */
typedef struct {
    float speed, aggro, atk_range, windup, dmg_mul;
    int   base_hp, base_dmg;
    float radius, height;
} EnemyDef;

static const EnemyDef DEFS[EN_TYPE_COUNT] = {
    /* speed aggro range windup dmgmul hp dmg  radius height */
    [EN_RAT]      = { 3.6f, 7.0f,  0.9f, 0.30f, 1.0f,  14, 5,  0.30f, 0.45f },
    [EN_SLIME]    = { 2.4f, 8.0f,  1.0f, 0.45f, 1.0f,  22, 7,  0.42f, 0.55f },
    [EN_SKELETON] = { 3.0f, 11.0f, 1.3f, 0.55f, 1.2f,  30, 10, 0.34f, 1.55f },
    [EN_SPIDER]   = { 4.2f, 10.0f, 1.1f, 0.35f, 1.0f,  20, 8,  0.50f, 0.45f },
};

/* --- cuboid models (feet at y=0, face +Z) ------------------------ */
static const RogueCuboid M_RAT[] = {
    { 0.0f, 0.16f, 0.0f, 0.18f, 0.12f, 0.26f, RGB(90,80,70) },     /* body */
    { 0.0f, 0.20f, 0.28f, 0.10f, 0.09f, 0.10f, RGB(110,98,86) },   /* head */
    { 0.0f, 0.14f,-0.30f, 0.02f, 0.02f, 0.12f, RGB(150,130,120) }, /* tail */
    {-0.13f,0.22f,0.30f, 0.04f,0.05f,0.02f, RGB(60,50,45) },       /* ears */
    { 0.13f,0.22f,0.30f, 0.04f,0.05f,0.02f, RGB(60,50,45) },
};
static const RogueCuboid M_SLIME[] = {
    { 0.0f, 0.22f, 0.0f, 0.38f, 0.22f, 0.38f, RGB(70,200,90) },    /* blob */
    {-0.14f,0.30f,0.32f, 0.05f,0.05f,0.03f, RGB(20,40,20) },       /* eye */
    { 0.14f,0.30f,0.32f, 0.05f,0.05f,0.03f, RGB(20,40,20) },
    { 0.0f, 0.12f,0.34f, 0.16f,0.02f,0.02f, RGB(20,40,20) },       /* mouth */
};
static const RogueCuboid M_SKELETON[] = {
    { 0.0f, 1.40f, 0.0f, 0.14f, 0.13f, 0.13f, RGB(220,220,205) },  /* skull */
    {-0.07f,1.44f,0.11f, 0.04f,0.04f,0.03f, RGB(20,20,20) },       /* eyes */
    { 0.07f,1.44f,0.11f, 0.04f,0.04f,0.03f, RGB(20,20,20) },
    { 0.0f, 1.05f, 0.0f, 0.15f, 0.22f, 0.08f, RGB(200,200,185) },  /* ribcage */
    { 0.0f, 1.02f, 0.10f,0.10f, 0.16f, 0.02f, RGB(160,160,148) },  /* sternum */
    {-0.20f,0.95f, 0.0f, 0.04f, 0.26f, 0.04f, RGB(210,210,195) },  /* arms */
    { 0.20f,0.95f, 0.0f, 0.04f, 0.26f, 0.04f, RGB(210,210,195) },
    {-0.08f,0.32f, 0.0f, 0.05f, 0.32f, 0.05f, RGB(210,210,195) },  /* legs */
    { 0.08f,0.32f, 0.0f, 0.05f, 0.32f, 0.05f, RGB(210,210,195) },
};
static const RogueCuboid M_SPIDER[] = {
    { 0.0f, 0.20f, -0.05f,0.26f, 0.16f, 0.30f, RGB(40,30,45) },    /* abdomen */
    { 0.0f, 0.20f, 0.32f, 0.16f, 0.13f, 0.16f, RGB(55,42,60) },    /* head */
    {-0.10f,0.30f,0.42f, 0.04f,0.04f,0.03f, RGB(220,40,40) },      /* eyes */
    { 0.10f,0.30f,0.42f, 0.04f,0.04f,0.03f, RGB(220,40,40) },
    {-0.34f,0.16f,0.10f, 0.18f,0.03f,0.03f, RGB(30,22,34) },       /* legs */
    { 0.34f,0.16f,0.10f, 0.18f,0.03f,0.03f, RGB(30,22,34) },
    {-0.34f,0.16f,-0.20f,0.18f,0.03f,0.03f, RGB(30,22,34) },
    { 0.34f,0.16f,-0.20f,0.18f,0.03f,0.03f, RGB(30,22,34) },
};
static const RogueCuboid *MODEL[EN_TYPE_COUNT] = { M_RAT, M_SLIME, M_SKELETON, M_SPIDER };
static const int MODEL_N[EN_TYPE_COUNT] = {
    (int)(sizeof(M_RAT)/sizeof(RogueCuboid)),
    (int)(sizeof(M_SLIME)/sizeof(RogueCuboid)),
    (int)(sizeof(M_SKELETON)/sizeof(RogueCuboid)),
    (int)(sizeof(M_SPIDER)/sizeof(RogueCuboid)),
};

/* --- RNG (local, for spawn jitter / wander) ---------------------- */
static uint32_t s_rng = 0x1234567u;
static uint32_t rng(void){ s_rng^=s_rng<<13; s_rng^=s_rng>>17; s_rng^=s_rng<<5; return s_rng; }
static float frand(void){ return (float)(rng() & 0xFFFF) / 65536.0f; }

void rogue_enemies_clear(void) {
    for (int i = 0; i < ROGUE_MAX_ENEMIES; i++) s_en[i].alive = false;
}

void rogue_enemies_spawn(const int16_t *room_cx, const int16_t *room_cz,
                         int n_rooms, int up_x, int up_z,
                         int floor_y, int depth, uint32_t seed) {
    rogue_enemies_clear();
    s_rng = seed ^ (0xABCD1234u * (uint32_t)(depth + 1));
    if (!s_rng) s_rng = 1;

    int want = 4 + depth;                 /* scale population with depth */
    if (want > ROGUE_MAX_ENEMIES) want = ROGUE_MAX_ENEMIES;

    int placed = 0;
    for (int attempt = 0; attempt < want * 6 && placed < want; attempt++) {
        int r = (int)(frand() * n_rooms);
        if (r >= n_rooms) r = n_rooms - 1;
        if (room_cx[r] == up_x && room_cz[r] == up_z) continue;  /* never spawn on start */
        Enemy *e = &s_en[placed];
        e->alive = true;
        /* Depth-weighted type: more skeletons/spiders deeper. */
        int roll = (int)(frand() * 100);
        EnemyType t;
        if (roll < 30) t = EN_RAT;
        else if (roll < 55) t = EN_SLIME;
        else if (roll < 80) t = EN_SKELETON;
        else t = EN_SPIDER;
        e->type = t;
        e->pos = v3(room_cx[r] + 0.5f + (frand()-0.5f)*2.0f, (float)floor_y,
                    room_cz[r] + 0.5f + (frand()-0.5f)*2.0f);
        e->yaw = frand() * 6.28f;
        float scale = 1.0f + 0.18f * depth;
        e->hp = (int)(DEFS[t].base_hp * scale);
        e->state = AI_WANDER;
        e->state_t = frand() * 1.5f;
        e->wander_dx = e->wander_dz = 0.0f;
        e->hurt_flash = 0.0f;
        e->atk_cd = 0.0f;
        placed++;
    }
}

static bool cell_solid(int wx, int wy, int wz) {
    return craft_block_solid(craft_world_get(wx, wy, wz));
}
static void en_move(Enemy *e, float dx, float dz, float step, int floor_y) {
    float nx = e->pos.x + dx * step;
    if (!cell_solid((int)floorf(nx), floor_y, (int)floorf(e->pos.z))) e->pos.x = nx;
    float nz = e->pos.z + dz * step;
    if (!cell_solid((int)floorf(e->pos.x), floor_y, (int)floorf(nz))) e->pos.z = nz;
}

void rogue_enemies_update(RoguePlayer *p, float dt, int floor_y) {
    for (int i = 0; i < ROGUE_MAX_ENEMIES; i++) {
        Enemy *e = &s_en[i];
        if (!e->alive) continue;
        const EnemyDef *d = &DEFS[e->type];
        if (e->hurt_flash > 0) e->hurt_flash -= dt;
        if (e->atk_cd > 0) e->atk_cd -= dt;
        e->state_t += dt;

        float dx = p->pos.x - e->pos.x, dz = p->pos.z - e->pos.z;
        float dist = sqrtf(dx*dx + dz*dz);
        float nx = dist > 0.001f ? dx/dist : 0, nz = dist > 0.001f ? dz/dist : 0;

        switch (e->state) {
        case AI_WANDER:
            if (p->alive && dist < d->aggro) { e->state = AI_CHASE; e->state_t = 0; break; }
            if (e->state_t > 1.2f) {
                e->state_t = 0;
                float a = frand() * 6.28f;
                e->wander_dx = sinf(a); e->wander_dz = cosf(a);
                if (frand() < 0.4f) e->wander_dx = e->wander_dz = 0;  /* pause */
            }
            if (e->wander_dx || e->wander_dz) {
                e->yaw = atan2f(e->wander_dx, e->wander_dz);
                en_move(e, e->wander_dx, e->wander_dz, d->speed * 0.4f * dt, floor_y);
            }
            break;
        case AI_CHASE:
            if (!p->alive || dist > d->aggro * 1.4f) { e->state = AI_WANDER; e->state_t = 0; break; }
            e->yaw = atan2f(nx, nz);
            if (dist <= d->atk_range && e->atk_cd <= 0) {
                e->state = AI_WINDUP; e->state_t = 0;
            } else {
                en_move(e, nx, nz, d->speed * dt, floor_y);
            }
            break;
        case AI_WINDUP:
            e->yaw = atan2f(nx, nz);     /* track during the tell */
            if (e->state_t >= d->windup) { e->state = AI_STRIKE; e->state_t = 0; }
            break;
        case AI_STRIKE:
            /* The blow lands once, if the player is still in reach. */
            if (e->state_t == 0.0f || e->state_t < dt + 0.0001f) {
                if (p->alive && dist <= d->atk_range + 0.4f) {
                    int dmg = (int)(d->base_dmg * d->dmg_mul);
                    rogue_player_damage(p, dmg, e->pos);
                }
            }
            if (e->state_t >= 0.18f) {
                e->state = AI_CHASE;
                e->atk_cd = 0.8f;
            }
            break;
        }
    }
}

int rogue_enemies_hit_arc(Vec3 origin, float yaw, float range,
                          float arc_cos, int dmg) {
    float fx = sinf(yaw), fz = cosf(yaw);
    int hits = 0;
    for (int i = 0; i < ROGUE_MAX_ENEMIES; i++) {
        Enemy *e = &s_en[i];
        if (!e->alive) continue;
        float dx = e->pos.x - origin.x, dz = e->pos.z - origin.z;
        float dist = sqrtf(dx*dx + dz*dz);
        if (dist > range + DEFS[e->type].radius) continue;
        if (dist > 0.001f) {
            float dot = (dx/dist) * fx + (dz/dist) * fz;
            if (dot < arc_cos) continue;     /* outside the swing arc */
        }
        e->hp -= dmg;
        e->hurt_flash = 0.22f;
        /* knockback */
        if (dist > 0.001f) { e->pos.x += dx/dist * 0.35f; e->pos.z += dz/dist * 0.35f; }
        if (e->hp <= 0) e->alive = false;
        else if (e->state == AI_WANDER) { e->state = AI_CHASE; e->state_t = 0; }
        hits++;
    }
    return hits;
}

void rogue_enemies_draw(const CraftCamera *cam, uint16_t *fb) {
    for (int i = 0; i < ROGUE_MAX_ENEMIES; i++) {
        Enemy *e = &s_en[i];
        if (!e->alive) continue;
        const EnemyDef *d = &DEFS[e->type];
        float flash = (e->hurt_flash > 0) ? 0.8f : 0.0f;
        /* Telegraph: flash bright white-hot during the wind-up. */
        if (e->state == AI_WINDUP) {
            float k = e->state_t / d->windup;
            flash = 0.4f + 0.5f * k;
        }
        rogue_render_model(cam, fb, e->pos, e->yaw,
                           MODEL[e->type], MODEL_N[e->type],
                           d->radius + 0.05f, d->height, flash, 256);
    }
}

int rogue_enemies_alive_count(void) {
    int n = 0;
    for (int i = 0; i < ROGUE_MAX_ENEMIES; i++) if (s_en[i].alive) n++;
    return n;
}

bool rogue_enemies_nearest(float x, float z, float *ex, float *ez) {
    float best = 1e30f; bool found = false;
    for (int i = 0; i < ROGUE_MAX_ENEMIES; i++) {
        if (!s_en[i].alive) continue;
        float dx = s_en[i].pos.x - x, dz = s_en[i].pos.z - z;
        float d = dx*dx + dz*dz;
        if (d < best) { best = d; *ex = s_en[i].pos.x; *ez = s_en[i].pos.z; found = true; }
    }
    return found;
}

#include "rogue_platform.h"
#include "rogue_render.h"
#include <math.h>

#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))
#define MAX_PLAT 6
#define PLAT_HX  0.95f
#define PLAT_HZ  0.95f
#define PLAT_TH  0.30f   /* slab half-thickness */
#define PLAYER_R 0.30f

typedef struct {
    bool  used;
    Vec3  a, b;          /* endpoints (top-surface position) */
    float t, dir, speed;
    Vec3  pos, prev;     /* current / previous top-surface position */
} Plat;

static Plat s_p[MAX_PLAT];

void rogue_platform_clear(void) {
    for (int i = 0; i < MAX_PLAT; i++) s_p[i].used = false;
}

static uint32_t hh(uint32_t x){ x^=x<<13; x^=x>>17; x^=x<<5; return x; }

void rogue_platform_place(const int16_t *room_cx, const int16_t *room_cz,
                          int n_rooms, int up_x, int up_z,
                          int floor_y, int depth, uint32_t seed,
                          const int16_t *chasm_x, const int16_t *chasm_z,
                          int n_chasm) {
    rogue_platform_clear();
    uint32_t r = seed ^ 0x9143u ^ (uint32_t)(depth * 2654435761u);
    float y = (float)floor_y;          /* top surface at normal walk height */
    int placed = 0;

    /* A platform ferrying across each lava lake (parallel to the bridge, but
     * over the lava) — guarantees a visible moving platform over a chasm. */
    for (int c = 0; c < n_chasm && placed < MAX_PLAT; c++) {
        int cx = chasm_x[c], cz = chasm_z[c];
        Plat *p = &s_p[placed++];
        p->used = true;
        p->a = v3(cx - 5.0f, y, cz + 4.0f);
        p->b = v3(cx + 5.0f, y, cz + 4.0f);
        r = hh(r);
        p->t = (float)(r & 0xFF) / 255.0f;
        p->dir = 1.0f;
        p->speed = 0.40f + 0.04f * depth;
        if (p->speed > 0.7f) p->speed = 0.7f;
        p->pos = p->prev = p->a;
    }

    /* A few more scattered through other rooms. */
    int want = placed + 1 + depth / 4;
    if (want > MAX_PLAT) want = MAX_PLAT;
    for (int a = 0; a < want * 5 && placed < want; a++) {
        r = hh(r);
        int idx = (int)(r % (uint32_t)(n_rooms > 0 ? n_rooms : 1));
        int cx = room_cx[idx], cz = room_cz[idx];
        if (cx == up_x && cz == up_z) continue;
        Plat *p = &s_p[placed];
        p->used = true;
        if (hh(r) & 1) { p->a = v3(cx - 4.0f, y, cz + 3.0f); p->b = v3(cx + 4.0f, y, cz + 3.0f); }
        else           { p->a = v3(cx + 3.0f, y, cz - 4.0f); p->b = v3(cx + 3.0f, y, cz + 4.0f); }
        p->t = (float)(hh(r) & 0xFF) / 255.0f;
        p->dir = 1.0f;
        p->speed = 0.35f + 0.05f * depth;
        if (p->speed > 0.7f) p->speed = 0.7f;
        p->pos = p->prev = p->a;
        placed++;
    }
}

void rogue_platform_update(float dt) {
    for (int i = 0; i < MAX_PLAT; i++) {
        Plat *p = &s_p[i];
        if (!p->used) continue;
        p->t += p->dir * p->speed * dt;
        if (p->t >= 1.0f) { p->t = 1.0f; p->dir = -1.0f; }
        if (p->t <= 0.0f) { p->t = 0.0f; p->dir =  1.0f; }
        p->prev = p->pos;
        p->pos.x = p->a.x + (p->b.x - p->a.x) * p->t;
        p->pos.y = p->a.y + (p->b.y - p->a.y) * p->t;
        p->pos.z = p->a.z + (p->b.z - p->a.z) * p->t;
    }
}

bool rogue_platform_support(float x, float z, float feet,
                            float *top, float *dx, float *dy, float *dz) {
    float best = -1e30f; int bi = -1;
    for (int i = 0; i < MAX_PLAT; i++) {
        Plat *p = &s_p[i];
        if (!p->used) continue;
        if (fabsf(x - p->pos.x) > PLAT_HX + PLAYER_R) continue;
        if (fabsf(z - p->pos.z) > PLAT_HZ + PLAYER_R) continue;
        float t = p->pos.y;                 /* top surface */
        if (t > feet + 0.35f) continue;     /* above the player's feet */
        if (t < feet - 0.7f) continue;      /* too far below to land on */
        if (t > best) { best = t; bi = i; }
    }
    if (bi < 0) return false;
    *top = best;
    *dx = s_p[bi].pos.x - s_p[bi].prev.x;
    *dy = s_p[bi].pos.y - s_p[bi].prev.y;
    *dz = s_p[bi].pos.z - s_p[bi].prev.z;
    return true;
}

void rogue_platform_draw(const CraftCamera *cam, uint16_t *fb) {
    for (int i = 0; i < MAX_PLAT; i++) {
        Plat *p = &s_p[i];
        if (!p->used) continue;
        /* Slab sits just under its top surface; runic edge for visibility. */
        RogueCuboid m[2] = {
            { 0.0f, -PLAT_TH, 0.0f, PLAT_HX, PLAT_TH, PLAT_HZ, RGB(70, 60, 80) },
            { 0.0f, -0.02f,   0.0f, PLAT_HX, 0.03f,   PLAT_HZ, RGB(150, 120, 200) },
        };
        rogue_render_model(cam, fb, p->pos, 0.0f, m, 2, PLAT_HX + 0.1f, 0.4f, 0.15f, 256);
    }
}

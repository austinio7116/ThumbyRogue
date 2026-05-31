#include "rogue_loot.h"
#include "rogue_render.h"
#include "rogue_inventory.h"
#include "craft_world.h"
#include "craft_blocks.h"
#include <math.h>

#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))
#define MAX_GROUND 18
#define MAX_CHEST  6
#define PICKUP_R   1.4f    /* gold/potion vacuum radius */
#define INTERACT_R 1.3f

typedef struct { bool alive; RogueItem item; Vec3 pos; float spin; } Ground;
typedef struct { bool used, opened; Vec3 pos; } Chest;

static Ground s_g[MAX_GROUND];
static Chest  s_c[MAX_CHEST];
static uint32_t s_rng = 0x2468ace0u;
static uint32_t xs(void){ s_rng^=s_rng<<13; s_rng^=s_rng>>17; s_rng^=s_rng<<5; return s_rng; }
static float frand(void){ return (float)(xs() & 0xFFFF) / 65536.0f; }

void rogue_loot_clear(void) {
    for (int i = 0; i < MAX_GROUND; i++) s_g[i].alive = false;
    for (int i = 0; i < MAX_CHEST; i++)  s_c[i].used = false;
}

void rogue_loot_drop(const RogueItem *it, Vec3 pos) {
    for (int i = 0; i < MAX_GROUND; i++) {
        if (s_g[i].alive) continue;
        s_g[i].alive = true;
        s_g[i].item = *it;
        s_g[i].pos = pos;
        s_g[i].spin = frand() * 6.28f;
        return;
    }
}

void rogue_loot_place_chests(const int16_t *room_cx, const int16_t *room_cz,
                             int n_rooms, int up_x, int up_z,
                             int floor_y, int depth, uint32_t seed) {
    for (int i = 0; i < MAX_CHEST; i++) s_c[i].used = false;
    s_rng = seed ^ (0x51ED2700u * (uint32_t)(depth + 1));
    if (!s_rng) s_rng = 1;
    int want = 1 + depth / 3;
    if (want > MAX_CHEST) want = MAX_CHEST;
    int placed = 0;
    for (int a = 0; a < want * 5 && placed < want; a++) {
        int r = (int)(frand() * n_rooms);
        if (r >= n_rooms) r = n_rooms - 1;
        if (room_cx[r] == up_x && room_cz[r] == up_z) continue;
        s_c[placed].used = true;
        s_c[placed].opened = false;
        int cx = room_cx[r], cz = room_cz[r];
        float cy = (float)floor_y;
        /* ~45% of chests sit on a pedestal you must JUMP onto to reach. */
        if ((xs() % 100u) < 45u) {
            craft_world_set_byte(cx, floor_y, cz, BLK_COBBLE);  /* 1-high pedestal */
            cy = (float)(floor_y + 1);
        }
        s_c[placed].pos = v3(cx + 0.5f, cy, cz + 0.5f);
        placed++;
    }
}

void rogue_loot_update(RoguePlayer *p, float dt) {
    for (int i = 0; i < MAX_GROUND; i++) {
        Ground *g = &s_g[i];
        if (!g->alive) continue;
        g->spin += dt * 2.4f;
        float dx = g->pos.x - p->pos.x, dz = g->pos.z - p->pos.z;
        if (dx*dx + dz*dz > PICKUP_R*PICKUP_R) continue;
        if (g->item.kind == ITEM_GOLD) {
            p->gold += g->item.amount;
            g->alive = false;
        } else if (g->item.kind == ITEM_POTION) {
            p->hp += g->item.amount;
            if (p->hp > p->max_hp) p->hp = p->max_hp;
            g->alive = false;
        } else if (g->item.kind == ITEM_TORCH) {
            p->torch_fuel += g->item.amount;
            if (p->torch_fuel > 90.0f) p->torch_fuel = 90.0f;
            g->alive = false;
        } else if (rogue_item_is_equip(&g->item) || g->item.kind == ITEM_GEM) {
            /* gear + gems auto-collect into the backpack (managed via MENU) */
            if (rogue_inventory_add(&g->item)) g->alive = false;
        }
    }
}

bool rogue_loot_weapon_near(float x, float z, RogueItem *out, int *out_index) {
    float best = INTERACT_R * INTERACT_R; int bi = -1;
    for (int i = 0; i < MAX_GROUND; i++) {
        if (!s_g[i].alive || !rogue_item_is_equip(&s_g[i].item)) continue;
        float dx = s_g[i].pos.x - x, dz = s_g[i].pos.z - z;
        float d = dx*dx + dz*dz;
        if (d < best) { best = d; bi = i; }
    }
    if (bi < 0) return false;
    *out = s_g[bi].item; *out_index = bi;
    return true;
}

bool rogue_loot_take(int index, RogueItem *out) {
    if (index < 0 || index >= MAX_GROUND || !s_g[index].alive) return false;
    *out = s_g[index].item;
    s_g[index].alive = false;
    return true;
}

bool rogue_loot_chest_near(float x, float y, float z, int *out_index) {
    for (int i = 0; i < MAX_CHEST; i++) {
        if (!s_c[i].used || s_c[i].opened) continue;
        float dx = s_c[i].pos.x - x, dz = s_c[i].pos.z - z;
        if (fabsf(s_c[i].pos.y - y) > 0.8f) continue;  /* must stand at its level */
        if (dx*dx + dz*dz <= INTERACT_R*INTERACT_R) { *out_index = i; return true; }
    }
    return false;
}

void rogue_loot_open_chest(int index, int depth, uint32_t seed) {
    if (index < 0 || index >= MAX_CHEST || !s_c[index].used || s_c[index].opened) return;
    s_c[index].opened = true;
    s_rng = seed ^ (0xC4E57u * (uint32_t)(index + 1) * (uint32_t)(depth + 2));
    if (!s_rng) s_rng = 1;
    Vec3 base = s_c[index].pos;
    /* Always a weapon + some gold; sometimes a potion. */
    RogueItem it;
    rogue_item_roll_drop(&it, depth + 1, xs());   /* chest gear skews better */
    Vec3 a = base; a.x += 0.6f; rogue_loot_drop(&it, a);
    rogue_item_make_gold(&it, 8 + (int)(frand() * (12 + depth * 6)));
    Vec3 b = base; b.x -= 0.6f; rogue_loot_drop(&it, b);
    if (frand() < 0.5f) {
        rogue_item_make_potion(&it, 35);
        Vec3 c = base; c.z += 0.6f; rogue_loot_drop(&it, c);
    }
}

void rogue_loot_draw(const CraftCamera *cam, uint16_t *fb) {
    /* chests */
    for (int i = 0; i < MAX_CHEST; i++) {
        if (!s_c[i].used) continue;
        if (!s_c[i].opened) {
            RogueCuboid m[3] = {
                { 0.0f, 0.16f, 0.0f, 0.30f, 0.16f, 0.22f, RGB(150, 100, 50) },
                { 0.0f, 0.34f, 0.0f, 0.31f, 0.07f, 0.23f, RGB(120, 78, 38) },
                { 0.0f, 0.30f, 0.23f, 0.06f, 0.05f, 0.03f, RGB(230, 200, 90) },
            };
            rogue_render_model(cam, fb, s_c[i].pos, 0.0f, m, 3, 0.4f, 0.6f, 0.0f, 256);
        } else {
            RogueCuboid m[2] = {
                { 0.0f, 0.14f, 0.0f, 0.30f, 0.14f, 0.22f, RGB(95, 62, 30) },
                { 0.0f, 0.28f, 0.0f, 0.26f, 0.04f, 0.18f, RGB(20, 14, 8) },
            };
            rogue_render_model(cam, fb, s_c[i].pos, 0.0f, m, 2, 0.4f, 0.5f, 0.0f, 256);
        }
    }
    /* ground items: small spinning cuboid, rarity-tinted, bobbing. */
    for (int i = 0; i < MAX_GROUND; i++) {
        Ground *g = &s_g[i];
        if (!g->alive) continue;
        bool eq = rogue_item_is_equip(&g->item);
        uint16_t c = eq ? rogue_rarity_color(g->item.rarity) : g->item.color;
        float bob = 0.12f + 0.05f * sinf(g->spin * 1.7f);
        Vec3 pos = g->pos; pos.y += bob;
        RogueCuboid m[1] = { { 0.0f, 0.10f, 0.0f, 0.10f, 0.10f, 0.10f, c } };
        float flash = (eq && g->item.rarity >= RAR_RARE) ? 0.3f : 0.0f;
        rogue_render_model(cam, fb, pos, g->spin, m, 1, 0.16f, 0.30f, flash, 256);
    }
}

#include "rogue_game.h"
#include "rogue_camera.h"
#include "rogue_player.h"
#include "rogue_enemy.h"
#include "rogue_gen.h"
#include "rogue_render.h"
#include "rogue_hud.h"
#include "rogue_items.h"
#include "rogue_loot.h"
#include "rogue_proj.h"
#include "rogue_band.h"
#include "rogue_sfx.h"
#include "rogue_platform.h"
#include "rogue_inventory.h"
#include "rogue_shop.h"
#include "rogue_particle.h"
#include "rogue_dmgnum.h"
#include "craft_world.h"
#include "craft_blocks.h"
#include "craft_render.h"
#include "craft_font.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))

static RoguePlayer    s_player;
static CraftCamera    s_cam;
static CraftRawButtons s_prev;
static RogueLevelInfo s_level;
static uint32_t s_seed;
static int   s_depth;
static float s_dead_t;     /* >0 while the death banner shows */
static float s_band_banner_t;  /* >0 while the band-name banner shows */
static int   s_last_band = -1;
static int   s_kills;
static int   s_best_depth;
static bool  s_title = true;

/* Cheat: hold LB+RB together for ~5s to open a level-skip menu. */
static bool  s_skip;
static int   s_skip_target;
static float s_lbrb_t;

static float s_anim_t;     /* animated-tile clock (water/lava/portal) */
static char  s_toast[48];  /* transient pickup/chest message */
static float s_toast_t;
static uint32_t s_loot_rng = 0x13572468u;

void rogue_game_toast(const char *msg) {
    snprintf(s_toast, sizeof s_toast, "%s", msg);
    s_toast_t = 2.6f;
}

/* Per-weapon-type melee impact FX so each weapon's hit reads differently:
 * light/quick for daggers, heavy blue for greatswords, fiery for axes, and a
 * grey dust shock-ring for the blunt mace/warhammer. */
static void melee_hit_fx(Vec3 hp, uint8_t wt) {
    switch (wt) {
    case WT_DAGGER:     rogue_particle_burst(hp, 5,  5.5f, 0.25f, RGB(230,235,255), 0.05f); break;
    case WT_GREATSWORD: rogue_particle_burst(hp, 16, 5.5f, 0.45f, RGB(170,210,255), 0.10f); break;
    case WT_AXE:        rogue_particle_burst(hp, 12, 5.5f, 0.40f, RGB(255,120,50),  0.09f); break;
    case WT_SPEAR:      rogue_particle_burst(hp, 6,  6.0f, 0.30f, RGB(225,235,250), 0.06f); break;
    case WT_MACE:
    case WT_WARHAMMER: {
        int big = (wt == WT_WARHAMMER);
        rogue_particle_burst(hp, big ? 18 : 12, 3.8f, 0.50f, RGB(205,197,178), big ? 0.12f : 0.10f);
        /* a low ground shock-ring of dust motes — the blunt-impact signature */
        int ring = big ? 12 : 8;
        float spd = big ? 6.5f : 5.0f;
        Vec3 g = hp; g.y -= 0.45f;
        for (int k = 0; k < ring; k++) {
            float a = (float)k / ring * 6.2831853f;
            rogue_particle_spawn(g, cosf(a) * spd, 0.4f, sinf(a) * spd,
                                 0.40f, RGB(190,182,165), 0.08f, 5.0f);
        }
        break; }
    default:            rogue_particle_burst(hp, 9,  5.0f, 0.35f, RGB(255,245,190), 0.07f); break;  /* sword */
    }
}

/* Fog-of-war minimap: cells the hero has been near. */
static uint8_t s_visited[CRAFT_WORLD_X * CRAFT_WORLD_Z];

/* Spike traps — always visible (fair), damage on contact with a cooldown. */
#define MAX_TRAPS 8
static Vec3  s_trap[MAX_TRAPS];
static int   s_n_trap;
static float s_trap_cd[MAX_TRAPS];
static uint32_t loot_rng(void){ s_loot_rng^=s_loot_rng<<13; s_loot_rng^=s_loot_rng>>17; s_loot_rng^=s_loot_rng<<5; return s_loot_rng; }

/* Stairs are drawn as four offset steps (descending into a dark pit for the
 * down-stairs, rising for the up-stairs) plus the tall locator beacon — teal
 * for down, amber for up (colours unchanged). */
static const RogueCuboid down_stair[] = {
    { 0.0f, 0.20f, -0.20f, 0.40f, 0.04f, 0.09f, RGB(74, 74, 86)  },  /* rim step (front, high) */
    { 0.0f, 0.14f, -0.04f, 0.40f, 0.04f, 0.09f, RGB(52, 52, 62)  },
    { 0.0f, 0.08f,  0.12f, 0.40f, 0.04f, 0.09f, RGB(36, 36, 44)  },
    { 0.0f, 0.02f,  0.28f, 0.40f, 0.04f, 0.09f, RGB(22, 22, 30)  },  /* deepest (back, low) */
    { 0.0f, 1.25f,  0.0f,  0.05f, 1.20f, 0.05f, RGB(40, 230, 210) }, /* teal beacon */
};
static const RogueCuboid up_stair[] = {
    { 0.0f, 0.06f, -0.20f, 0.40f, 0.06f, 0.09f, RGB(140,140,150) }, /* low step (front) */
    { 0.0f, 0.16f, -0.04f, 0.40f, 0.06f, 0.09f, RGB(152,152,162) },
    { 0.0f, 0.26f,  0.12f, 0.40f, 0.06f, 0.09f, RGB(164,164,174) },
    { 0.0f, 0.36f,  0.28f, 0.40f, 0.06f, 0.09f, RGB(176,176,186) }, /* high step (back) */
    { 0.0f, 1.25f,  0.0f,  0.05f, 1.20f, 0.05f, RGB(245,180, 60) }, /* amber beacon */
};
/* Minecraft-style floor torch: a thin wooden stick topped with a flame. */
static const RogueCuboid torch_model[] = {
    { 0.0f, 0.24f, 0.0f, 0.035f, 0.24f, 0.035f, RGB(110, 75, 40)  },  /* stick */
    { 0.0f, 0.50f, 0.0f, 0.075f, 0.06f, 0.075f, RGB(255, 150, 30) },  /* ember */
    { 0.0f, 0.58f, 0.0f, 0.055f, 0.06f, 0.055f, RGB(255, 225, 120) }, /* flame */
};

/* Carry the full paperdoll + gold across floors; only rebuild on death. */
static RogueItem s_keep_equip[SLOT_COUNT];
static int s_keep_gold;
static bool s_have_keep;

/* ---- save / load ---- */
int rogue_plat_save(const uint8_t *data, int len);   /* platform-provided */
int rogue_plat_load(uint8_t *data, int max);

#define ROGUE_SAVE_MAGIC 0x52475633u   /* 'RGV3' — bumped: backpack grew to 21 slots */
typedef struct {
    uint32_t magic, version;
    uint32_t seed;
    int32_t  depth;        /* >0 = a live run to resume; <=0 = none */
    int32_t  kills, best, gold, hp;
    float    torch;
    RogueItem equip[SLOT_COUNT];
    int32_t  bag_n;
    RogueItem bag[ROGUE_BAG_N];
} RogueSave;

void rogue_game_save(int run_active) {
    RogueSave s;
    memset(&s, 0, sizeof s);
    s.magic = ROGUE_SAVE_MAGIC; s.version = 3;
    s.seed = s_seed;
    s.depth = run_active ? s_depth : -1;
    s.kills = s_kills;
    s.best = (s_depth > s_best_depth) ? s_depth : s_best_depth;
    s.gold = s_player.gold;
    s.hp = s_player.hp;
    s.torch = s_player.torch_fuel;
    for (int i = 0; i < SLOT_COUNT; i++) s.equip[i] = s_player.equip[i];
    s.bag_n = rogue_inventory_export(s.bag, ROGUE_BAG_N);
    rogue_plat_save((const uint8_t *)&s, (int)sizeof s);
}

static void load_level(void);
/* Try to resume a saved run. Returns true if a live run was restored. */
static bool try_resume(void) {
    RogueSave s;
    int n = rogue_plat_load((uint8_t *)&s, (int)sizeof s);
    if (n < (int)sizeof s || s.magic != ROGUE_SAVE_MAGIC) return false;
    if (s.best > s_best_depth) s_best_depth = s.best;
    if (s.depth <= 0) return false;        /* save exists but run is over */
    s_seed = s.seed;
    s_depth = s.depth;
    s_kills = s.kills;
    load_level();                          /* regenerate the saved floor */
    for (int i = 0; i < SLOT_COUNT; i++) s_player.equip[i] = s.equip[i];
    rogue_player_recompute(&s_player);
    s_player.hp = s.hp > 0 ? s.hp : 1;
    if (s_player.hp > s_player.max_hp) s_player.hp = s_player.max_hp;
    s_player.gold = s.gold;
    s_player.torch_fuel = s.torch;
    rogue_inventory_import(s.bag, s.bag_n);
    s_have_keep = true;
    for (int i = 0; i < SLOT_COUNT; i++) s_keep_equip[i] = s.equip[i];
    s_keep_gold = s.gold;
    return true;
}

static void load_level(void) {
    memset(s_visited, 0, sizeof s_visited);
    rogue_particle_clear();
    rogue_dmgnum_clear();
    rogue_gen_dungeon(s_seed, s_depth, &s_level);
    rogue_player_init(&s_player, s_level.spawn);
    if (s_have_keep) {
        for (int i = 0; i < SLOT_COUNT; i++) s_player.equip[i] = s_keep_equip[i];
        rogue_player_recompute(&s_player);
        s_player.hp = s_player.max_hp;
        s_player.gold = s_keep_gold;
    }
    rogue_camera_init(s_player.pos);
    rogue_enemies_spawn(s_level.room_cx, s_level.room_cz, s_level.n_rooms,
                        s_level.up_x, s_level.up_z, s_level.floor_y,
                        s_depth, s_seed);
    rogue_loot_clear();
    rogue_proj_clear();
    rogue_loot_place_chests(s_level.room_cx, s_level.room_cz, s_level.n_rooms,
                            s_level.up_x, s_level.up_z, s_level.floor_y,
                            s_depth, s_seed);
    rogue_platform_place(s_level.room_cx, s_level.room_cz, s_level.n_rooms,
                         s_level.up_x, s_level.up_z, s_level.floor_y,
                         s_depth, s_seed,
                         s_level.chasm_x, s_level.chasm_z, s_level.n_chasm);
    rogue_shop_place(s_level.room_cx, s_level.room_cz, s_level.n_rooms,
                     s_level.up_x, s_level.up_z, s_level.down_x, s_level.down_z,
                     s_level.floor_y, s_depth, s_seed);
    /* Bonus chest on each lava island — only reachable by riding the platform. */
    for (int c = 0; c < s_level.n_chasm; c++)
        rogue_loot_add_chest_at(s_level.island_x[c] + 0.5f, (float)s_level.floor_y,
                                s_level.island_z[c] + 0.5f);
    /* Spike traps in some rooms (not the up-stairs). */
    s_n_trap = 0;
    int twant = 1 + s_depth / 2;
    if (twant > MAX_TRAPS) twant = MAX_TRAPS;
    for (int a = 0; a < twant * 4 && s_n_trap < twant; a++) {
        int r = (int)(loot_rng() % (uint32_t)(s_level.n_rooms > 0 ? s_level.n_rooms : 1));
        if (s_level.room_cx[r] == s_level.up_x && s_level.room_cz[r] == s_level.up_z) continue;
        s_trap[s_n_trap] = v3(s_level.room_cx[r] + 0.5f + ((int)(loot_rng()%3)-1),
                              (float)s_level.floor_y,
                              s_level.room_cz[r] + 0.5f + ((int)(loot_rng()%3)-1));
        s_trap_cd[s_n_trap] = 0.0f;
        s_n_trap++;
    }

    /* Announce a new band the first time we enter it. */
    int band = (s_depth - 1) / ROGUE_BAND_FLOORS;
    if (band != s_last_band) { s_last_band = band; s_band_banner_t = 2.2f; }
}

void rogue_game_init(uint32_t seed) {
    s_seed = seed;
    s_depth = 1;
    s_dead_t = 0.0f;
    s_have_keep = false;
    s_last_band = -1;
    s_band_banner_t = 0.0f;
    s_loot_rng = seed | 1u;

    craft_render_set_fog(false);
    craft_render_set_clouds(false);
    craft_render_set_far_lod(false);
    craft_render_set_groundcover(false);
    craft_render_set_interlace(false);
    craft_render_set_lowres(false);
    craft_render_set_coarse_skip(false);
    craft_render_set_torch_light(false);
    craft_render_set_player_light(true);   /* the hero's torch lights the scene */
    craft_render_set_time(240.0f);         /* deep-night ambient → dark dungeon */

    s_kills = 0;
    s_best_depth = 0;
    rogue_inventory_clear();
    rogue_sfx_init();
    /* Resume a saved run if there is one; otherwise start fresh at the title. */
    if (try_resume()) {
        s_title = false;
    } else {
        s_title = true;
        load_level();
    }
    s_prev = (CraftRawButtons){0};
}

static bool edge(bool now, bool prev) { return now && !prev; }

void rogue_game_tick(const CraftRawButtons *btn, float dt) {
    /* Advance the animated-tile clock every frame (water 4Hz, lava 2Hz,
     * portal 3Hz). The renderer already blends water see-through; this is
     * what makes the dank-green surface actually ripple. Runs in every
     * state so the world keeps animating behind menus too. */
    s_anim_t += dt;
    craft_blocks_animate_water(s_anim_t);

    /* Title screen — slowly orbit the camera until A starts the run. */
    if (s_title) {
        if (edge(btn->a, s_prev.a)) s_title = false;
        rogue_camera_follow(s_player.pos, dt);
        rogue_camera_update(dt);
        rogue_camera_get(&s_cam);
        s_prev = *btn;
        return;
    }

    /* Death → run-summary screen; A starts a fresh run (permadeath: gear +
     * gold lost, back to the starter dagger). */
    if (!s_player.alive) {
        if (s_depth > s_best_depth) s_best_depth = s_depth;
        if (s_dead_t == 0.0f) rogue_game_save(0);   /* permadeath: wipe run, keep best */
        s_dead_t += dt;
        /* require a brief beat before accepting input, then wait for A */
        if (s_dead_t > 0.6f && edge(btn->a, s_prev.a)) {
            s_dead_t = 0.0f;
            s_seed = s_seed * 1664525u + 1013904223u;
            s_depth = 1;
            s_have_keep = false;
            s_kills = 0;
            rogue_inventory_clear();
            load_level();
            rogue_game_save(1);
        }
        rogue_camera_update(dt);
        rogue_camera_get(&s_cam);
        s_prev = *btn;
        return;
    }

    /* Inventory screen — freezes gameplay; MENU toggles it. */
    if (rogue_inventory_is_open()) {
        rogue_inventory_input(&s_player, btn, &s_prev);
        if (edge(btn->menu, s_prev.menu)) rogue_inventory_close();
        rogue_camera_get(&s_cam);
        s_prev = *btn;
        return;
    }
    /* Shop screen — opened by the merchant pad; MENU leaves. */
    if (rogue_shop_is_open()) {
        rogue_shop_input(&s_player, btn, &s_prev);
        if (edge(btn->menu, s_prev.menu)) rogue_shop_close();
        rogue_camera_get(&s_cam);
        s_prev = *btn;
        return;
    }
    /* Level-skip cheat menu (opened by the LB+RB hold below). */
    if (s_skip) {
        if (edge(btn->up, s_prev.up) || edge(btn->right, s_prev.right)) s_skip_target++;
        if (edge(btn->down, s_prev.down) || edge(btn->left, s_prev.left)) s_skip_target--;
        if (s_skip_target < 1) s_skip_target = 1;
        if (edge(btn->a, s_prev.a)) {
            s_depth = s_skip_target; s_last_band = -1;
            load_level(); rogue_game_save(1);
            s_skip = false;
        }
        if (edge(btn->b, s_prev.b) || edge(btn->menu, s_prev.menu)) s_skip = false;
        rogue_camera_get(&s_cam);
        s_prev = *btn;
        return;
    }

    if (edge(btn->menu, s_prev.menu)) {
        rogue_inventory_open();
        s_prev = *btn;
        return;
    }

    /* LB/RB rotate the view — unless BOTH are held, which arms the level-skip
     * cheat (hold ~5s). Holding both suppresses rotation so the camera doesn't
     * spin while you wait. */
    bool both_lr = btn->lb && btn->rb;
    if (!both_lr) {
        if (edge(btn->lb, s_prev.lb)) rogue_camera_rotate(-1);
        if (edge(btn->rb, s_prev.rb)) rogue_camera_rotate(+1);
        s_lbrb_t = 0.0f;
    } else {
        s_lbrb_t += dt;
        if (s_lbrb_t >= 5.0f) { s_skip = true; s_skip_target = s_depth; s_lbrb_t = 0.0f; }
    }

    bool atk_edge   = edge(btn->a, s_prev.a);
    bool jump_edge  = edge(btn->b, s_prev.b);
    int  hp0 = s_player.hp, gold0 = s_player.gold;
    if (atk_edge && s_player.atk_cd <= 0 && s_player.atk_t <= 0) rogue_sfx_swing();

    /* Torch burns down slowly; relight it FAST by standing near a lit brazier
     * or lava (a clear, discoverable recharge). Torch pickups also refill it. */
    if (s_player.torch_fuel > 0) s_player.torch_fuel -= dt * 0.7f;
    {
        int pcx = (int)floorf(s_player.pos.x);
        int pcz = (int)floorf(s_player.pos.z);
        int pcy = s_level.floor_y;
        bool near_fire = false;
        for (int dz = -2; dz <= 2 && !near_fire; dz++)
            for (int dx = -2; dx <= 2; dx++) {
                int b  = craft_world_get(pcx + dx, pcy, pcz + dz);
                int b2 = craft_world_get(pcx + dx, pcy - 1, pcz + dz);
                if (b == BLK_LAMP_ON || craft_is_lava_id((uint8_t)b) ||
                    craft_is_lava_id((uint8_t)b2)) { near_fire = true; break; }
            }
        if (near_fire && s_player.torch_fuel < 90.0f)
            s_player.torch_fuel += dt * 18.0f;   /* relight */
    }
    if (s_player.torch_fuel < 0) s_player.torch_fuel = 0;
    if (s_player.torch_fuel > 90.0f) s_player.torch_fuel = 90.0f;
    craft_render_set_player_light(s_player.torch_fuel > 0);
    /* Torch dims + shrinks as fuel runs low — brightness is a resource you
     * watch drain. Full above 14s, fading to a dim ember by 0. */
    {
        float f = s_player.torch_fuel;
        float lvl = f >= 14.0f ? 1.0f : 0.35f + 0.65f * (f / 14.0f);
        craft_render_set_light_intensity(lvl);
        craft_render_set_light_radius(5.0f + 4.0f * lvl);   /* ~9 blocks → ~6.4 */
    }
    /* Light the bubble around the HERO (head height), not the camera. */
    craft_render_set_light_pos(s_player.pos.x, s_player.pos.y + 0.9f, s_player.pos.z);
    /* X-ray near walls so the hero is never lost behind them (camera-side only). */
    craft_render_set_xray(s_player.pos.x, s_player.pos.y, s_player.pos.z, 3.5f);
    rogue_enemies_set_dark(s_player.torch_fuel <= 0);

    rogue_platform_update(dt);   /* before player: sets platform delta to ride */

    bool starting_attack = atk_edge && s_player.atk_cd <= 0 && s_player.atk_t <= 0;
    rogue_player_update(&s_player, btn, atk_edge, jump_edge, dt,
                        rogue_camera_snapped_yaw(), s_level.floor_y);
    if (s_player.jumped) rogue_sfx_dodge();   /* jump sound */

    /* Reveal the minimap around the hero (fog of war). */
    {
        int pcx = (int)floorf(s_player.pos.x), pcz = (int)floorf(s_player.pos.z);
        for (int dz = -4; dz <= 4; dz++)
            for (int dx = -4; dx <= 4; dx++) {
                if (dx*dx + dz*dz > 20) continue;
                int x = pcx + dx, z = pcz + dz;
                if ((unsigned)x < CRAFT_WORLD_X && (unsigned)z < CRAFT_WORLD_Z)
                    s_visited[z * CRAFT_WORLD_X + x] = 1;
            }
    }

    /* Melee auto-face: snap the swing toward the nearest enemy in lunge
     * range so daggers/swords land where you mean them to. */
    if (starting_attack && s_player.wpn_class == WCLASS_MELEE) {
        float ex, ez;
        if (rogue_enemies_nearest(s_player.pos.x, s_player.pos.z, &ex, &ez)) {
            float dx = ex - s_player.pos.x, dz = ez - s_player.pos.z;
            float reach = s_player.wpn_range * 1.8f;
            if (dx*dx + dz*dz <= reach*reach) s_player.yaw = atan2f(dx, dz);
        }
    }

    /* Lava contact: standing in a lava cell burns over time. */
    {
        int lx = (int)floorf(s_player.pos.x);
        int ly = (int)floorf(s_player.pos.y + 0.05f);
        int lz = (int)floorf(s_player.pos.z);
        if (craft_is_lava_id((uint8_t)craft_world_get(lx, ly, lz)) ||
            craft_is_lava_id((uint8_t)craft_world_get(lx, ly - 1, lz))) {
            /* Lava is INSTANT DEATH — bridges and the platform are the only
             * safe routes (the critical path always has a cross-bridge). */
            s_player.hp = 0;
            s_player.alive = false;
        }
    }

    /* Spike traps. */
    for (int i = 0; i < s_n_trap; i++) {
        if (s_trap_cd[i] > 0) s_trap_cd[i] -= dt;
        float dx = s_player.pos.x - s_trap[i].x, dz = s_player.pos.z - s_trap[i].z;
        if (dx*dx + dz*dz < 0.45f*0.45f && s_trap_cd[i] <= 0) {
            rogue_player_damage(&s_player, 12 + s_depth * 2, s_trap[i]);
            s_trap_cd[i] = 1.1f;
        }
    }

    /* Effective per-hit damage with a crit roll (from aggregated stats). */
    uint32_t asp = s_player.stats.aspects;
    int outdmg = s_player.wpn_dmg;
    if ((int)(loot_rng() % 100) < s_player.stats.crit)
        outdmg = outdmg * s_player.stats.crit_dmg / 100;
    /* Nightstalker aspect: extra damage while the torch is out. */
    if ((asp & (1u << ASP_DARK)) && s_player.torch_fuel <= 0) outdmg = outdmg * 7 / 5;

    /* Melee strike frame → damage every enemy in the swing arc. */
    if (s_player.atk_hit_pending) {
        s_player.atk_hit_pending = false;
        int hits = rogue_enemies_hit_arc(s_player.pos, s_player.yaw,
                              s_player.wpn_range, s_player.wpn_arc_cos, outdmg);
        /* Chaining aspect: a cleave around the hero on top of the arc. */
        if ((asp & (1u << ASP_CHAIN)) && hits > 0)
            rogue_enemies_hit_radius(s_player.pos.x, s_player.pos.z, 2.2f, outdmg / 2);
        if (hits > 0) {
            rogue_sfx_hit();
            /* spark burst at the swing point */
            Vec3 hp = v3(s_player.pos.x + sinf(s_player.yaw) * s_player.wpn_range * 0.7f,
                         s_player.pos.y + 0.6f,
                         s_player.pos.z + cosf(s_player.yaw) * s_player.wpn_range * 0.7f);
            melee_hit_fx(hp, s_player.wpn_type);
            int heal = s_player.stats.life_on_hit * hits;
            if (asp & (1u << ASP_LIFESTEAL)) heal += outdmg / 8;  /* Vampiric */
            if (heal) {
                s_player.hp += heal;
                if (s_player.hp > s_player.max_hp) s_player.hp = s_player.max_hp;
            }
        }
    }
    /* Ranged/caster strike frame → fire a projectile (auto-aim a nearby foe). */
    if (s_player.fire_pending) {
        s_player.fire_pending = false;
        float aim = s_player.yaw, ex, ez;
        if (rogue_enemies_nearest(s_player.pos.x, s_player.pos.z, &ex, &ez)) {
            float dx = ex - s_player.pos.x, dz = ez - s_player.pos.z;
            if (dx*dx + dz*dz < s_player.wpn_range * s_player.wpn_range)
                aim = atan2f(dx, dz);
        }
        int pk = (s_player.wpn_type == WT_CROSSBOW) ? PROJ_BOLT
               : (s_player.wpn_type == WT_WAND)     ? PROJ_WAND
               : (s_player.wpn_type == WT_SCEPTER)  ? PROJ_SCEPTER
               : (s_player.wpn_type == WT_STAFF)    ? PROJ_STAFF
               :                                      PROJ_ARROW;
        rogue_proj_fire(s_player.pos, aim, s_player.wpn_proj_speed,
                        outdmg, pk,
                        s_player.wpn_range, (asp & (1u << ASP_PIERCE)) ? 1 : 0);
    }

    rogue_enemies_update(&s_player, dt, s_level.floor_y);
    rogue_proj_update(dt, s_level.floor_y);
    rogue_loot_update(&s_player, dt);
    rogue_particle_update(dt);
    rogue_dmgnum_update(dt);

    /* Drop loot from anything that died this frame — weighted by the slain
     * creature's loot class (goblins/kobolds → gear, fire sprites → gems,
     * slimes/zombies → potions, demons → rare gear, the rest → mostly gold). */
    Vec3 dpos; int dtype;
    while (rogue_enemies_pop_death(&dpos, &dtype)) {
        s_kills++;
        rogue_sfx_enemy_die();
        /* death poof — fire sprites burst in flame, others in dust */
        Vec3 pp = dpos; pp.y += 0.4f;
        uint16_t pc = (dtype == EN_FIRESPRITE) ? RGB(255,140,30)
                    : (dtype == EN_DEMON) ? RGB(200,40,40) : RGB(170,160,150);
        rogue_particle_burst(pp, 12, 4.5f, 0.55f, pc, 0.09f);
        RogueItem it;
        EnemyLoot lk = rogue_enemy_loot(dtype);
        int goldmul = (lk == LOOT_RARE) ? 4 : (lk == LOOT_GOLD ? 2 : 1);
        rogue_item_make_gold(&it, 2 + (int)(loot_rng() % (5 + s_depth * 2)) * goldmul);
        rogue_loot_drop(&it, dpos);
        int r = loot_rng() % 100;
        switch (lk) {
        case LOOT_GEAR:   if (r < 45) { rogue_item_roll_drop(&it, s_depth, loot_rng()); rogue_loot_drop(&it, dpos); } break;
        case LOOT_GEM:    if (r < 50) { rogue_item_make_gem(&it, (GemType)(1 + loot_rng()%4)); rogue_loot_drop(&it, dpos); } break;
        case LOOT_POTION: if (r < 45) { rogue_item_make_potion(&it, 30); rogue_loot_drop(&it, dpos); } break;
        case LOOT_RARE:   /* demons always cough up good gear */
            rogue_item_roll_drop(&it, s_depth + 3, loot_rng()); rogue_loot_drop(&it, dpos);
            if (r < 50) { rogue_item_make_gem(&it, (GemType)(1 + loot_rng()%4)); rogue_loot_drop(&it, dpos); }
            break;
        default:          if (r < 10) { rogue_item_roll_drop(&it, s_depth, loot_rng()); rogue_loot_drop(&it, dpos); } break;
        }
        if (r >= 88) { rogue_item_make_torch(&it, 30); rogue_loot_drop(&it, dpos); }  /* torches universal */
    }

    /* Chests open automatically when you reach them (loot spills, then
     * vacuums into the backpack). */
    {
        int ci;
        if (rogue_loot_chest_near(s_player.pos.x, s_player.pos.y, s_player.pos.z, &ci))
            rogue_loot_open_chest(ci, s_depth, loot_rng());
    }
    /* Step onto the merchant pad → open the shop (rising edge). */
    {
        static bool was_on_pad;
        bool on = rogue_shop_pad_near(s_player.pos.x, s_player.pos.y, s_player.pos.z);
        if (on && !was_on_pad) rogue_shop_open();
        was_on_pad = on;
    }

    /* Event SFX from state deltas this frame. */
    if (s_player.hp < hp0)   rogue_sfx_hurt();
    if (s_player.gold > gold0) rogue_sfx_pickup();

    /* Descend when the hero reaches the down-stairs (keep gear + gold). */
    float ddx = s_player.pos.x - (s_level.down_x + 0.5f);
    float ddz = s_player.pos.z - (s_level.down_z + 0.5f);
    if (ddx*ddx + ddz*ddz < 0.7f*0.7f) {
        rogue_sfx_descend();
        for (int i = 0; i < SLOT_COUNT; i++) s_keep_equip[i] = s_player.equip[i];
        s_keep_gold = s_player.gold;
        s_have_keep = true;
        s_depth++;
        load_level();
        rogue_game_save(1);   /* checkpoint each new floor */
    }

    if (s_band_banner_t > 0) s_band_banner_t -= dt;
    if (s_toast_t > 0) s_toast_t -= dt;

    rogue_camera_follow(s_player.pos, dt);
    rogue_camera_update(dt);
    rogue_camera_get(&s_cam);
    s_prev = *btn;
}

static void gfill(uint16_t *fb, int x, int y, int w, int h, uint16_t c) {
    for (int j = y; j < y + h; j++) {
        if ((unsigned)j >= CRAFT_FB_H) continue;
        for (int i = x; i < x + w; i++)
            if ((unsigned)i < CRAFT_FB_W) fb[j * CRAFT_FB_W + i] = c;
    }
}

/* Level-skip cheat overlay: a fill bar while LB+RB is held, then the menu. */
static void draw_skip(uint16_t *fb) {
    if (s_lbrb_t > 0.0f && !s_skip) {
        craft_font_draw(fb, "LEVEL SKIP...", 30, 50, RGB(240,210,60));
        int w = (int)(s_lbrb_t / 5.0f * 80.0f);
        gfill(fb, 24, 60, 80, 6, RGB(24,22,34));
        gfill(fb, 24, 60, w, 6, RGB(240,210,60));
    }
    if (s_skip) {
        gfill(fb, 18, 40, 92, 46, RGB(20,18,30));
        gfill(fb, 18, 40, 92, 2, RGB(240,210,60)); gfill(fb, 18, 84, 92, 2, RGB(240,210,60));
        gfill(fb, 18, 40, 2, 46, RGB(240,210,60));  gfill(fb, 108, 40, 2, 46, RGB(240,210,60));
        craft_font_draw(fb, "LEVEL SKIP", 24, 46, RGB(240,210,60));
        char b[24]; snprintf(b, sizeof b, "Depth  %d", s_skip_target);
        craft_font_draw(fb, b, 24, 58, RGB(255,255,255));
        craft_font_draw(fb, "up/dn  A go  B x", 24, 72, RGB(160,160,175));
    }
}

/* Fog-of-war minimap. Shown only on the inventory/menu screen (it's too
 * large to leave on during play), tucked into the free area to the right
 * of the paperdoll's stat column. */
/* Map a minimap cell (i,j) to world (wx,wz), oriented to the current camera
 * so the map matches what you see: up = away-from-camera, right = screen-right.
 * The 90deg snap index picks one of four axis permutations/flips. */
static void mm_cell_to_world(int i, int j, int MS, int *wx, int *wz) {
    int u, v;   /* normalised 0..MS-1 along world +x (u) and +z (v) */
    switch (rogue_camera_yaw_index() & 3) {
        default:
        case 0: u = i;          v = MS - 1 - j; break;
        case 1: u = MS - 1 - j; v = MS - 1 - i; break;
        case 2: u = MS - 1 - i; v = j;          break;
        case 3: u = j;          v = i;          break;
    }
    *wx = u * CRAFT_WORLD_X / MS;
    *wz = v * CRAFT_WORLD_Z / MS;
}
/* Inverse: world (wx,wz) -> minimap cell (i,j). */
static void mm_world_to_cell(int wx, int wz, int MS, int *i, int *j) {
    int u = wx * MS / CRAFT_WORLD_X, v = wz * MS / CRAFT_WORLD_Z;
    switch (rogue_camera_yaw_index() & 3) {
        default:
        case 0: *i = u;          *j = MS - 1 - v; break;
        case 1: *i = MS - 1 - v; *j = MS - 1 - u; break;
        case 2: *i = MS - 1 - u; *j = v;          break;
        case 3: *i = v;          *j = u;          break;
    }
}

static void draw_minimap(uint16_t *fb) {
    const int MS = 36, MX = CRAFT_FB_W - MS - 1, MY = 12;
    craft_font_draw(fb, "MAP", MX, MY - 9, RGB(150,150,160));
    for (int j = -1; j <= MS; j++)
        for (int i = -1; i <= MS; i++) {
            int sx = MX + i, sy = MY + j;
            if ((unsigned)sx >= CRAFT_FB_W || (unsigned)sy >= CRAFT_FB_H) continue;
            if (i < 0 || j < 0 || i >= MS || j >= MS) { fb[sy*CRAFT_FB_W+sx] = RGB(40,38,48); continue; } /* border */
            int wx, wz; mm_cell_to_world(i, j, MS, &wx, &wz);
            uint16_t c = RGB(10, 9, 14);                 /* unexplored */
            if (s_visited[wz * CRAFT_WORLD_X + wx]) {
                int b = craft_world_get(wx, s_level.floor_y, wz);
                if (b == BLK_AIR)                       c = RGB(120,116,130); /* floor */
                else if (craft_is_lava_id((uint8_t)b))  c = RGB(230,110,30);  /* lava */
                else                                    c = RGB(48,46,58);    /* wall */
            }
            fb[sy*CRAFT_FB_W+sx] = c;
        }
    /* markers: down-stairs (teal), up (amber), player (white) */
    #define MM_PT(wx,wz,col) do { \
        int _i, _j; mm_world_to_cell((int)(wx), (int)(wz), MS, &_i, &_j); \
        int _x = MX + _i, _y = MY + _j; \
        for (int a=0;a<2;a++) for (int b=0;b<2;b++){ int xx=_x+a, yy=_y+b; \
            if ((unsigned)xx<CRAFT_FB_W && (unsigned)yy<CRAFT_FB_H) fb[yy*CRAFT_FB_W+xx]=(col); } } while(0)
    MM_PT(s_level.down_x, s_level.down_z, RGB(40,230,210));
    MM_PT(s_level.up_x,   s_level.up_z,   RGB(245,180,60));
    MM_PT((int)s_player.pos.x, (int)s_player.pos.z, RGB(255,255,255));
    #undef MM_PT
}

void rogue_game_get_camera(CraftCamera *out) { *out = s_cam; }
int rogue_game_depth(void) { return s_depth; }
int rogue_game_player_hp(void) { return s_player.hp; }
int rogue_game_player_gold(void) { return s_player.gold; }
float rogue_game_player_y(void) { return s_player.pos.y; }
const char *rogue_game_weapon_name(void) { return s_player.equip[SLOT_WEAPON].name; }

/* Test hook: drop a strong weapon at the hero's feet then run the real
 * equip path (weapon_near -> take -> equip -> drop old). Verifies the
 * gear-defined playstyle swap end-to-end. */
void rogue_game_debug_kill(void) { s_player.hp = 0; s_player.alive = false; s_kills = 7; }
void rogue_game_debug_beam(void) {
    /* plant gear of each rarity a few tiles out so the loot beams are visible */
    for (int k = 0; k < 4; k++) {
        RogueItem it; rogue_item_roll_drop(&it, 2 + k * 6, loot_rng());
        it.rarity = (Rarity)k; it.color = rogue_rarity_color((Rarity)k);
        Vec3 p = s_player.pos; p.x += 2.0f + k * 1.2f; p.z += 2.0f;
        rogue_loot_drop(&it, p);
    }
}
void rogue_game_debug_set_torch(float s) { s_player.torch_fuel = s; rogue_game_tick(&s_prev, 0.0f); }

int rogue_game_player_maxhp(void) { return s_player.max_hp; }
int rogue_game_player_armor(void) { return s_player.stats.armor; }
int rogue_game_player_wdmg(void)  { return s_player.wpn_dmg; }

void rogue_game_debug_gear_up(void);   /* fwd */
void rogue_game_debug_open_shop(void){ s_player.gold=999; rogue_shop_open(); }

/* Fill the backpack with rolled drops + open the inventory (UI screenshot). */
void rogue_game_debug_fill_bag(void) {
    rogue_game_debug_gear_up();   /* equip a set so paperdoll shows gear */
    for (int i = 0; i < 9; i++) {
        RogueItem it;
        rogue_item_roll_drop(&it, 9, loot_rng());
        rogue_inventory_add(&it);
    }
    RogueItem pot; rogue_item_make_potion(&pot, 40); rogue_inventory_add(&pot);
    rogue_inventory_open();
}

/* One of every weapon type into the bag + open inventory (icon check). */
void rogue_game_debug_weapon_sheet(void) {
    rogue_inventory_clear();
    for (int wt = 0; wt < WT_COUNT && wt < ROGUE_BAG_N; wt++) {
        RogueItem it;
        /* roll until we land on this weapon type so base stats/name are real */
        for (int tries = 0; tries < 200; tries++) {
            rogue_item_roll_weapon(&it, 6, loot_rng());
            if (it.wtype == wt) break;
        }
        rogue_inventory_add(&it);
    }
    rogue_inventory_open();
}

/* Stand the hero a few tiles in front of the first lava cell (screenshot). */
int rogue_game_debug_goto_lava(void) {
    for (int z = 0; z < CRAFT_WORLD_Z; z++)
        for (int x = 0; x < CRAFT_WORLD_X; x++)
            for (int y = s_level.floor_y - 3; y <= s_level.floor_y; y++)
                if (craft_is_lava_id((uint8_t)craft_world_get(x, y, z))) {
                    s_player.pos = v3(x + 0.5f, (float)s_level.floor_y, z - 4.5f);
                    return 1;
                }
    return 0;
}

/* Move the hero onto the first water pool found (water-render verification). */
int rogue_game_debug_goto_water(void) {
    for (int z = 0; z < CRAFT_WORLD_Z; z++)
        for (int x = 0; x < CRAFT_WORLD_X; x++)
            if (craft_is_water_id((uint8_t)craft_world_get(x, s_level.floor_y - 1, z))) {
                s_player.pos = v3(x + 0.5f, (float)s_level.floor_y, z + 0.5f);
                return 1;
            }
    return 0;
}

/* Spawn a spread of floating damage numbers near the hero (FX verification). */
void rogue_game_debug_dmgnum(void) {
    Vec3 p = s_player.pos;
    rogue_dmgnum_spawn(v3(p.x + 1.2f, p.y, p.z + 0.6f), 12,  false);
    rogue_dmgnum_spawn(v3(p.x - 1.1f, p.y, p.z + 1.2f), 37,  false);
    rogue_dmgnum_spawn(v3(p.x + 0.4f, p.y, p.z - 1.1f), 144, false);
    rogue_dmgnum_spawn(v3(p.x,        p.y, p.z),          8,  true);
    rogue_dmgnum_update(0.18f);   /* let them rise a touch before the shot */
}

/* Set up the item-detail page demo: equip a socketed legendary weapon and
 * drop a couple of gems in the bag, cursor on the weapon. */
void rogue_game_debug_detail_setup(void) {
    rogue_inventory_clear();
    RogueItem w;
    for (int t = 0; t < 200; t++) { rogue_item_roll_weapon(&w, 8, loot_rng()); if (w.wtype == WT_SWORD) break; }
    w.rarity = RAR_LEGENDARY; w.aspect = ASP_CHAIN; w.sockets = 2;
    w.gem[0] = GEM_NONE; w.gem[1] = GEM_NONE;
    w.color = rogue_rarity_color(RAR_LEGENDARY);
    rogue_player_equip(&s_player, &w);
    RogueItem g1, g2; rogue_item_make_gem(&g1, GEM_RUBY); rogue_item_make_gem(&g2, GEM_EMERALD);
    rogue_inventory_add(&g1); rogue_inventory_add(&g2);
    rogue_inventory_open();
}

/* Force-equip a specific weapon type (FX verification). */
void rogue_game_debug_force_weapon(int wt) {
    for (int tries = 0; tries < 400; tries++) {
        RogueItem it; rogue_item_roll_weapon(&it, 6, loot_rng());
        if (it.wtype == wt) { rogue_player_equip(&s_player, &it); return; }
    }
}
/* Force the hero's facing (radians) for FX direction checks. */
void rogue_game_debug_set_yaw(float yaw) { s_player.yaw = yaw; }
/* Freeze a mid-stride walk pose (animation check). */
void rogue_game_debug_walkpose(float ph) { s_player.walk_blend = 1.0f; s_player.move_phase = ph; }
/* Reveal the whole fog-of-war map (minimap verification). */
void rogue_game_debug_reveal_map(void) {
    for (int i = 0; i < CRAFT_WORLD_X * CRAFT_WORLD_Z; i++) s_visited[i] = 1;
}

/* Roll + equip one item into every slot (verify stat aggregation). */
void rogue_game_debug_gear_up(void) {
    for (int s = 0; s < SLOT_COUNT; s++) {
        RogueItem it;
        rogue_item_roll_gear(&it, (EquipSlot)s, 10, loot_rng());
        rogue_player_equip(&s_player, &it);
    }
    s_player.hp = s_player.max_hp;
}

void rogue_game_debug_set_depth(int depth) {
    s_depth = depth < 1 ? 1 : depth;
    s_last_band = -1;
    load_level();
}

void rogue_game_debug_drop_weapon(void) {
    RogueItem it;
    rogue_item_roll_drop(&it, 8, loot_rng());
    rogue_loot_drop(&it, s_player.pos);
    RogueItem w; int idx;
    if (rogue_loot_weapon_near(s_player.pos.x, s_player.pos.z, &w, &idx)) {
        RogueItem taken;
        if (rogue_loot_take(idx, &taken)) {
            RogueItem old = s_player.equip[taken.slot];
            rogue_player_equip(&s_player, &taken);
            if (rogue_item_is_equip(&old)) rogue_loot_drop(&old, s_player.pos);
        }
    }
}

/* Headless autopilot step: steer toward the nearest foe (screen-relative,
 * via the snapped camera yaw) and swing when close. For verification only. */
void rogue_game_demo_step(float dt, int frame) {
    CraftRawButtons b = {0};
    float ex, ez;
    if (rogue_enemies_nearest(s_player.pos.x, s_player.pos.z, &ex, &ez)) {
        float dx = ex - s_player.pos.x, dz = ez - s_player.pos.z;
        float dist = sqrtf(dx*dx + dz*dz);
        float yaw = rogue_camera_snapped_yaw();
        float fwd = dx * sinf(yaw) + dz * cosf(yaw);
        float rgt = dx * cosf(yaw) - dz * sinf(yaw);
        if (dist > 1.2f) {
            if (fwd >  0.4f) b.up = true;
            if (fwd < -0.4f) b.down = true;
            if (rgt >  0.4f) b.right = true;
            if (rgt < -0.4f) b.left = true;
        }
        if (dist < 1.9f) b.a = (frame % 6) < 2;   /* in range → swing */
    }
    if (frame % 40 == 10) b.b = true;      /* periodic jump (verify physics) */
    rogue_game_tick(&b, dt);
}

void rogue_game_draw_overlay(uint16_t *fb) {
    Vec3 dpos = v3(s_level.down_x + 0.5f, (float)s_level.floor_y, s_level.down_z + 0.5f);
    Vec3 upos = v3(s_level.up_x + 0.5f,   (float)s_level.floor_y, s_level.up_z + 0.5f);
    rogue_render_model(&s_cam, fb, upos, 0.0f, up_stair, 5, 0.5f, 2.5f, 0.0f, 256);
    rogue_render_model(&s_cam, fb, dpos, 0.0f, down_stair, 5, 0.5f, 2.5f, 0.0f, 256);

    /* Wall/floor torches (the room light sources). */
    for (int i = 0; i < s_level.n_torch; i++) {
        Vec3 tp = v3(s_level.torch_x[i] + 0.5f, (float)s_level.floor_y, s_level.torch_z[i] + 0.5f);
        rogue_render_model(&s_cam, fb, tp, 0.0f, torch_model, 3, 0.12f, 0.7f, 0.0f, 256);
    }

    /* Spike traps — dark pad + steel spikes (telegraphed; pulses when armed). */
    for (int i = 0; i < s_n_trap; i++) {
        float warn = (s_trap_cd[i] > 0) ? 0.0f : 0.18f;
        RogueCuboid m[5] = {
            { 0.0f, 0.03f, 0.0f, 0.42f, 0.03f, 0.42f, RGB(35, 30, 30) },
            { -0.2f, 0.12f, -0.2f, 0.04f, 0.10f, 0.04f, RGB(190,190,200) },
            {  0.2f, 0.12f, -0.2f, 0.04f, 0.10f, 0.04f, RGB(190,190,200) },
            { -0.2f, 0.12f,  0.2f, 0.04f, 0.10f, 0.04f, RGB(190,190,200) },
            {  0.2f, 0.12f,  0.2f, 0.04f, 0.10f, 0.04f, RGB(190,190,200) },
        };
        rogue_render_model(&s_cam, fb, s_trap[i], 0.0f, m, 5, 0.45f, 0.3f, warn, 256);
    }

    rogue_platform_draw(&s_cam, fb);
    rogue_loot_draw(&s_cam, fb);
    rogue_proj_draw(&s_cam, fb);
    rogue_enemies_draw(&s_cam, fb);
    rogue_player_draw(&s_player, &s_cam, fb, 256);
    rogue_particle_draw(&s_cam, fb);
    rogue_dmgnum_draw(&s_cam, fb);

    if (s_title) { rogue_hud_title(fb, s_best_depth); return; }
    if (rogue_inventory_is_open()) {
        rogue_inventory_draw(fb, &s_player);
        /* map shares the inventory grid only — hide it on the detail sub-pages */
        if (!rogue_inventory_detail_open()) draw_minimap(fb);
        return;
    }
    if (rogue_shop_is_open()) { rogue_shop_draw(fb, &s_player); return; }

    rogue_hud_draw(fb, &s_player, s_depth, rogue_enemies_alive_count());
    if (s_toast_t > 0) rogue_hud_prompt(fb, s_toast);

    if (!s_player.alive) {
        int best = s_depth > s_best_depth ? s_depth : s_best_depth;
        rogue_hud_summary(fb, s_depth, s_player.gold, s_kills, best);
    } else if (s_band_banner_t > 0) {
        const RogueBand *b = rogue_band_get(s_depth);
        rogue_hud_banner(fb, b->name, b->tint);
    }
    draw_skip(fb);   /* cheat: arming bar / level-skip menu on top */
}

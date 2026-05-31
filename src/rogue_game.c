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
#include "craft_world.h"
#include "craft_blocks.h"
#include "craft_render.h"
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
static float s_lava_t;     /* lava-burn tick timer */
static uint32_t s_loot_rng = 0x13572468u;

/* Spike traps — always visible (fair), damage on contact with a cooldown. */
#define MAX_TRAPS 8
static Vec3  s_trap[MAX_TRAPS];
static int   s_n_trap;
static float s_trap_cd[MAX_TRAPS];
static uint32_t loot_rng(void){ s_loot_rng^=s_loot_rng<<13; s_loot_rng^=s_loot_rng>>17; s_loot_rng^=s_loot_rng<<5; return s_loot_rng; }

static const RogueCuboid down_stair[] = {
    { 0.0f, 0.05f, 0.0f, 0.45f, 0.05f, 0.45f, RGB(20, 20, 28)  },
    { 0.0f, 0.30f, 0.0f, 0.30f, 0.22f, 0.30f, RGB(10, 10, 14)  },
    { 0.0f, 1.25f, 0.0f, 0.06f, 1.20f, 0.06f, RGB(40, 230, 210) },
};
static const RogueCuboid up_stair[] = {
    { 0.0f, 0.12f, 0.0f, 0.42f, 0.12f, 0.42f, RGB(150,150,160) },
    { 0.0f, 0.30f, 0.0f, 0.26f, 0.20f, 0.26f, RGB(120,120,128) },
    { 0.0f, 1.25f, 0.0f, 0.06f, 1.20f, 0.06f, RGB(245,180, 60) },
};

/* Carry the equipped weapon + gold across floors; only rebuild on death. */
static RogueItem s_keep_weapon;
static int s_keep_gold;
static bool s_have_keep;

static void load_level(void) {
    rogue_gen_dungeon(s_seed, s_depth, &s_level);
    rogue_player_init(&s_player, s_level.spawn);
    if (s_have_keep) {
        rogue_player_equip(&s_player, &s_keep_weapon);
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
                         s_depth, s_seed);
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
    s_title = true;
    rogue_sfx_init();
    load_level();
    s_prev = (CraftRawButtons){0};
}

static bool edge(bool now, bool prev) { return now && !prev; }

void rogue_game_tick(const CraftRawButtons *btn, float dt) {
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
        s_dead_t += dt;
        /* require a brief beat before accepting input, then wait for A */
        if (s_dead_t > 0.6f && edge(btn->a, s_prev.a)) {
            s_dead_t = 0.0f;
            s_seed = s_seed * 1664525u + 1013904223u;
            s_depth = 1;
            s_have_keep = false;
            s_kills = 0;
            load_level();
        }
        rogue_camera_update(dt);
        rogue_camera_get(&s_cam);
        s_prev = *btn;
        return;
    }

    if (edge(btn->lb, s_prev.lb)) rogue_camera_rotate(-1);
    if (edge(btn->rb, s_prev.rb)) rogue_camera_rotate(+1);

    bool atk_edge   = edge(btn->a, s_prev.a);
    bool jump_edge  = edge(btn->b, s_prev.b);
    int  hp0 = s_player.hp, gold0 = s_player.gold;
    if (atk_edge && s_player.atk_cd <= 0 && s_player.atk_t <= 0) rogue_sfx_swing();

    /* Torch burns down; out of fuel → darkness + bolder, deadlier foes. */
    if (s_player.torch_fuel > 0) s_player.torch_fuel -= dt;
    if (s_player.torch_fuel < 0) s_player.torch_fuel = 0;
    craft_render_set_player_light(s_player.torch_fuel > 0);
    /* Light the bubble around the HERO (head height), not the camera. */
    craft_render_set_light_pos(s_player.pos.x, s_player.pos.y + 0.9f, s_player.pos.z);
    rogue_enemies_set_dark(s_player.torch_fuel <= 0);

    rogue_platform_update(dt);   /* before player: sets platform delta to ride */

    bool starting_attack = atk_edge && s_player.atk_cd <= 0 && s_player.atk_t <= 0;
    rogue_player_update(&s_player, btn, atk_edge, jump_edge, dt,
                        rogue_camera_snapped_yaw(), s_level.floor_y);
    if (s_player.jumped) rogue_sfx_dodge();   /* jump sound */

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
            s_lava_t -= dt;
            if (s_lava_t <= 0) {
                rogue_player_damage(&s_player, 10 + s_depth, s_player.pos);
                s_lava_t = 0.45f;
            }
        } else {
            s_lava_t = 0.0f;
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

    /* Melee strike frame → damage every enemy in the swing arc. */
    if (s_player.atk_hit_pending) {
        s_player.atk_hit_pending = false;
        int hits = rogue_enemies_hit_arc(s_player.pos, s_player.yaw,
                              s_player.wpn_range, s_player.wpn_arc_cos,
                              s_player.wpn_dmg);
        if (hits > 0) rogue_sfx_hit();
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
        rogue_proj_fire(s_player.pos, aim, s_player.wpn_proj_speed,
                        s_player.wpn_dmg, s_player.wpn_class == WCLASS_CASTER,
                        s_player.wpn_range);
    }

    rogue_enemies_update(&s_player, dt, s_level.floor_y);
    rogue_proj_update(dt, s_level.floor_y);
    rogue_loot_update(&s_player, dt);

    /* Drop loot from anything that died this frame. */
    Vec3 dpos; int dtype;
    while (rogue_enemies_pop_death(&dpos, &dtype)) {
        s_kills++;
        rogue_sfx_enemy_die();
        RogueItem it;
        rogue_item_make_gold(&it, 2 + (int)(loot_rng() % (5 + s_depth * 2)));
        rogue_loot_drop(&it, dpos);
        int r = loot_rng() % 100;
        if (r < 12)      { rogue_item_roll_weapon(&it, s_depth, loot_rng()); rogue_loot_drop(&it, dpos); }
        else if (r < 22) { rogue_item_make_potion(&it, 30); rogue_loot_drop(&it, dpos); }
        else if (r < 34) { rogue_item_make_torch(&it, 25); rogue_loot_drop(&it, dpos); }
    }

    /* MENU: interact — equip a ground weapon (swap), or open a chest. */
    if (edge(btn->menu, s_prev.menu)) {
        RogueItem w; int idx;
        if (rogue_loot_weapon_near(s_player.pos.x, s_player.pos.z, &w, &idx)) {
            RogueItem old = s_player.weapon, taken;
            if (rogue_loot_take(idx, &taken)) {
                rogue_player_equip(&s_player, &taken);
                rogue_loot_drop(&old, s_player.pos);
            }
        } else {
            int ci;
            if (rogue_loot_chest_near(s_player.pos.x, s_player.pos.y, s_player.pos.z, &ci))
                rogue_loot_open_chest(ci, s_depth, loot_rng());
        }
    }

    /* Event SFX from state deltas this frame. */
    if (s_player.hp < hp0)   rogue_sfx_hurt();
    if (s_player.gold > gold0) rogue_sfx_pickup();

    /* Descend when the hero reaches the down-stairs (keep gear + gold). */
    float ddx = s_player.pos.x - (s_level.down_x + 0.5f);
    float ddz = s_player.pos.z - (s_level.down_z + 0.5f);
    if (ddx*ddx + ddz*ddz < 0.7f*0.7f) {
        rogue_sfx_descend();
        s_keep_weapon = s_player.weapon;
        s_keep_gold = s_player.gold;
        s_have_keep = true;
        s_depth++;
        load_level();
    }

    if (s_band_banner_t > 0) s_band_banner_t -= dt;

    rogue_camera_follow(s_player.pos, dt);
    rogue_camera_update(dt);
    rogue_camera_get(&s_cam);
    s_prev = *btn;
}

void rogue_game_get_camera(CraftCamera *out) { *out = s_cam; }
int rogue_game_depth(void) { return s_depth; }
int rogue_game_player_hp(void) { return s_player.hp; }
int rogue_game_player_gold(void) { return s_player.gold; }
float rogue_game_player_y(void) { return s_player.pos.y; }
const char *rogue_game_weapon_name(void) { return s_player.weapon.name; }

/* Test hook: drop a strong weapon at the hero's feet then run the real
 * equip path (weapon_near -> take -> equip -> drop old). Verifies the
 * gear-defined playstyle swap end-to-end. */
void rogue_game_debug_kill(void) { s_player.hp = 0; s_player.alive = false; s_kills = 7; }

void rogue_game_debug_set_depth(int depth) {
    s_depth = depth < 1 ? 1 : depth;
    s_last_band = -1;
    load_level();
}

void rogue_game_debug_drop_weapon(void) {
    RogueItem it;
    rogue_item_roll_weapon(&it, 8, loot_rng());
    rogue_loot_drop(&it, s_player.pos);
    RogueItem w; int idx;
    if (rogue_loot_weapon_near(s_player.pos.x, s_player.pos.z, &w, &idx)) {
        RogueItem old = s_player.weapon, taken;
        if (rogue_loot_take(idx, &taken)) {
            rogue_player_equip(&s_player, &taken);
            rogue_loot_drop(&old, s_player.pos);
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
    if (frame % 45 == 20) b.menu = true;   /* periodically grab weapons/chests */
    if (frame % 40 == 10) b.b = true;      /* periodic jump (verify physics) */
    rogue_game_tick(&b, dt);
}

void rogue_game_draw_overlay(uint16_t *fb) {
    Vec3 dpos = v3(s_level.down_x + 0.5f, (float)s_level.floor_y, s_level.down_z + 0.5f);
    Vec3 upos = v3(s_level.up_x + 0.5f,   (float)s_level.floor_y, s_level.up_z + 0.5f);
    rogue_render_model(&s_cam, fb, upos, 0.0f, up_stair, 3, 0.5f, 2.5f, 0.0f, 256);
    rogue_render_model(&s_cam, fb, dpos, 0.0f, down_stair, 3, 0.5f, 2.5f, 0.0f, 256);

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

    if (s_title) { rogue_hud_title(fb, s_best_depth); return; }

    rogue_hud_draw(fb, &s_player, s_depth, rogue_enemies_alive_count());

    /* Interact prompt. */
    if (s_player.alive) {
        RogueItem w; int idx, ci;
        char buf[40];
        if (rogue_loot_weapon_near(s_player.pos.x, s_player.pos.z, &w, &idx)) {
            snprintf(buf, sizeof buf, "MENU: %s (%d)", w.name, w.dmg);
            rogue_hud_prompt(fb, buf);
        } else if (rogue_loot_chest_near(s_player.pos.x, s_player.pos.y, s_player.pos.z, &ci)) {
            rogue_hud_prompt(fb, "MENU: open chest");
        }
    }
    if (!s_player.alive) {
        int best = s_depth > s_best_depth ? s_depth : s_best_depth;
        rogue_hud_summary(fb, s_depth, s_player.gold, s_kills, best);
    } else if (s_band_banner_t > 0) {
        const RogueBand *b = rogue_band_get(s_depth);
        rogue_hud_banner(fb, b->name, b->tint);
    }
}

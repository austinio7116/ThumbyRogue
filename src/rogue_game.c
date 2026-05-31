#include "rogue_game.h"
#include "rogue_camera.h"
#include "rogue_player.h"
#include "rogue_enemy.h"
#include "rogue_gen.h"
#include "rogue_render.h"
#include "rogue_hud.h"
#include "craft_world.h"
#include "craft_render.h"
#include <math.h>

#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))

static RoguePlayer    s_player;
static CraftCamera    s_cam;
static CraftRawButtons s_prev;
static RogueLevelInfo s_level;
static uint32_t s_seed;
static int   s_depth;
static float s_dead_t;     /* >0 while the death banner shows */

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

static void load_level(void) {
    rogue_gen_dungeon(s_seed, s_depth, &s_level);
    rogue_player_init(&s_player, s_level.spawn);
    rogue_camera_init(s_player.pos);
    rogue_enemies_spawn(s_level.room_cx, s_level.room_cz, s_level.n_rooms,
                        s_level.up_x, s_level.up_z, s_level.floor_y,
                        s_depth, s_seed);
}

void rogue_game_init(uint32_t seed) {
    s_seed = seed;
    s_depth = 1;
    s_dead_t = 0.0f;

    craft_render_set_fog(false);
    craft_render_set_clouds(false);
    craft_render_set_far_lod(false);
    craft_render_set_groundcover(false);
    craft_render_set_interlace(false);
    craft_render_set_lowres(false);
    craft_render_set_coarse_skip(false);
    craft_render_set_torch_light(false);
    craft_render_set_player_light(false);
    craft_render_set_time(80.0f);

    load_level();
    s_prev = (CraftRawButtons){0};
}

static bool edge(bool now, bool prev) { return now && !prev; }

void rogue_game_tick(const CraftRawButtons *btn, float dt) {
    /* Death → show banner, then restart the run from depth 1. */
    if (!s_player.alive) {
        s_dead_t += dt;
        if (s_dead_t > 2.2f) {
            s_dead_t = 0.0f;
            s_seed = s_seed * 1664525u + 1013904223u;
            s_depth = 1;
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
    bool dodge_edge = edge(btn->b, s_prev.b);

    rogue_player_update(&s_player, btn, atk_edge, dodge_edge, dt,
                        rogue_camera_snapped_yaw(), s_level.floor_y);

    /* Melee strike frame → damage every enemy in the swing arc. */
    if (s_player.atk_hit_pending) {
        s_player.atk_hit_pending = false;
        rogue_enemies_hit_arc(s_player.pos, s_player.yaw,
                              s_player.wpn_range, s_player.wpn_arc_cos,
                              s_player.wpn_dmg);
    }

    rogue_enemies_update(&s_player, dt, s_level.floor_y);

    /* Descend when the hero reaches the down-stairs. */
    float ddx = s_player.pos.x - (s_level.down_x + 0.5f);
    float ddz = s_player.pos.z - (s_level.down_z + 0.5f);
    if (ddx*ddx + ddz*ddz < 0.7f*0.7f) {
        s_depth++;
        load_level();
    }

    rogue_camera_follow(s_player.pos, dt);
    rogue_camera_update(dt);
    rogue_camera_get(&s_cam);
    s_prev = *btn;
}

void rogue_game_get_camera(CraftCamera *out) { *out = s_cam; }
int rogue_game_depth(void) { return s_depth; }
int rogue_game_player_hp(void) { return s_player.hp; }

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
    rogue_game_tick(&b, dt);
}

void rogue_game_draw_overlay(uint16_t *fb) {
    Vec3 dpos = v3(s_level.down_x + 0.5f, (float)s_level.floor_y, s_level.down_z + 0.5f);
    Vec3 upos = v3(s_level.up_x + 0.5f,   (float)s_level.floor_y, s_level.up_z + 0.5f);
    rogue_render_model(&s_cam, fb, upos, 0.0f, up_stair, 3, 0.5f, 2.5f, 0.0f, 256);
    rogue_render_model(&s_cam, fb, dpos, 0.0f, down_stair, 3, 0.5f, 2.5f, 0.0f, 256);
    rogue_enemies_draw(&s_cam, fb);
    rogue_player_draw(&s_player, &s_cam, fb, 256);

    rogue_hud_draw(fb, &s_player, s_depth, rogue_enemies_alive_count());
    if (!s_player.alive) rogue_hud_banner(fb, "YOU DIED", RGB(220, 40, 40));
}

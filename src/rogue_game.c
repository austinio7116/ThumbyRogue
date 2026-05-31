#include "rogue_game.h"
#include "rogue_camera.h"
#include "rogue_player.h"
#include "rogue_level.h"
#include "rogue_render.h"
#include "craft_world.h"
#include "craft_render.h"

static RoguePlayer s_player;
static CraftCamera s_cam;
static CraftRawButtons s_prev;
static uint32_t s_seed;
static int s_floor_y;

static void load_level(uint32_t seed) {
    Vec3 spawn = rogue_level_build_test_room(seed);
    s_floor_y = ROGUE_FLOOR_Y;
    rogue_player_init(&s_player, spawn);
    rogue_camera_init(s_player.pos);
}

void rogue_game_init(uint32_t seed) {
    s_seed = seed;

    /* Renderer setup: dungeon look — no clouds/far-lod, groundcover off. */
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

    load_level(seed);
    s_prev = (CraftRawButtons){0};
}

static bool edge(bool now, bool prev) { return now && !prev; }

void rogue_game_tick(const CraftRawButtons *btn, float dt) {
    /* Camera rotate (snap 90°): LB ccw, RB cw. */
    if (edge(btn->lb, s_prev.lb)) rogue_camera_rotate(-1);
    if (edge(btn->rb, s_prev.rb)) rogue_camera_rotate(+1);

    /* MENU: regenerate the level (Phase 1 dev convenience). */
    if (edge(btn->menu, s_prev.menu)) {
        s_seed = s_seed * 1664525u + 1013904223u;
        load_level(s_seed);
    }

    rogue_player_update(&s_player, btn, dt,
                        rogue_camera_snapped_yaw(), s_floor_y);

    rogue_camera_follow(s_player.pos, dt);
    rogue_camera_update(dt);
    rogue_camera_get(&s_cam);

    s_prev = *btn;
}

void rogue_game_get_camera(CraftCamera *out) { *out = s_cam; }

void rogue_game_draw_overlay(uint16_t *fb) {
    rogue_player_draw(&s_player, &s_cam, fb, 256);
}

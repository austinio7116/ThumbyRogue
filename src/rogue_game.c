#include "rogue_game.h"
#include "rogue_camera.h"
#include "rogue_player.h"
#include "rogue_gen.h"
#include "rogue_render.h"
#include "craft_world.h"
#include "craft_render.h"
#include <math.h>

#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))

static RoguePlayer    s_player;
static CraftCamera    s_cam;
static CraftRawButtons s_prev;
static RogueLevelInfo s_level;
static uint32_t s_seed;
static int s_depth;

/* Stair models: a base pad + a tall glowing beacon (visible across the
 * room in iso, so the player can navigate toward the exit). */
static const RogueCuboid down_stair[] = {
    { 0.0f, 0.05f, 0.0f, 0.45f, 0.05f, 0.45f, RGB(20, 20, 28)  },   /* dark pit pad */
    { 0.0f, 0.30f, 0.0f, 0.30f, 0.22f, 0.30f, RGB(10, 10, 14)  },   /* recess */
    { 0.0f, 1.25f, 0.0f, 0.06f, 1.20f, 0.06f, RGB(40, 230, 210) },  /* teal beacon */
};
static const RogueCuboid up_stair[] = {
    { 0.0f, 0.12f, 0.0f, 0.42f, 0.12f, 0.42f, RGB(150,150,160) },   /* grey pad */
    { 0.0f, 0.30f, 0.0f, 0.26f, 0.20f, 0.26f, RGB(120,120,128) },
    { 0.0f, 1.25f, 0.0f, 0.06f, 1.20f, 0.06f, RGB(245,180, 60) },   /* amber beacon */
};

static void load_level(void) {
    rogue_gen_dungeon(s_seed, s_depth, &s_level);
    rogue_player_init(&s_player, s_level.spawn);
    rogue_camera_init(s_player.pos);
}

void rogue_game_init(uint32_t seed) {
    s_seed = seed;
    s_depth = 1;

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
    if (edge(btn->lb, s_prev.lb)) rogue_camera_rotate(-1);
    if (edge(btn->rb, s_prev.rb)) rogue_camera_rotate(+1);

    rogue_player_update(&s_player, btn, dt,
                        rogue_camera_snapped_yaw(), s_level.floor_y);

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

void rogue_game_draw_overlay(uint16_t *fb) {
    Vec3 dpos = v3(s_level.down_x + 0.5f, (float)s_level.floor_y, s_level.down_z + 0.5f);
    Vec3 upos = v3(s_level.up_x + 0.5f,   (float)s_level.floor_y, s_level.up_z + 0.5f);
    rogue_render_model(&s_cam, fb, upos, 0.0f, up_stair,
                       (int)(sizeof(up_stair)/sizeof(up_stair[0])), 0.5f, 2.5f, 0.0f, 256);
    rogue_render_model(&s_cam, fb, dpos, 0.0f, down_stair,
                       (int)(sizeof(down_stair)/sizeof(down_stair[0])), 0.5f, 2.5f, 0.0f, 256);
    rogue_player_draw(&s_player, &s_cam, fb, 256);
}

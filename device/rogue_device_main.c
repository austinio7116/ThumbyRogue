/*
 * ThumbyRogue — device entry point (standalone, RP2350). Phase 0.
 *
 * Proves the vendored engine boots on hardware and renders a world at
 * the isometric tilt. Dual-core tile-stealing renderer (core1 helps
 * core0 fill the framebuffer), same pattern as ThumbyGolf/ThumbyCraft.
 *
 *   D-pad   pan the camera over the world (screen-relative)
 *   LB/RB   rotate the iso view (Phase 1 will snap to 90°)
 *   MENU    regenerate the world with a new seed
 *
 * The real game loop (player, combat, dungeon descent) lands in later
 * phases; this is the scaffold the engine renders through.
 */
#include <math.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/rand.h"
#include "hardware/clocks.h"

#include "craft_lcd_gc9107.h"
#include "craft_buttons.h"
#include "craft_render.h"
#include "craft_world.h"
#include "craft_blocks.h"

void craft_tool_models_init(void);
void craft_blocks_build_textures(void);

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static uint16_t g_fb[CRAFT_FB_W * CRAFT_FB_H];

/* Platform/save hooks referenced by the engine but unused in Phase 0. */
bool craft_save_slot_used(int slot)             { (void)slot; return false; }
const uint16_t *craft_save_slot_thumb(int slot) { (void)slot; return NULL; }
uint32_t craft_platform_rand32(void)            { return (uint32_t)get_rand_32(); }

/* ThumbyRogue has no redstone — no-op the hooks craft_world calls so we
 * don't link the redstone module (saves SRAM: mobs/arrows/audio pulls). */
void craft_redstone_note_change(BlockId p, BlockId n) { (void)p; (void)n; }
void craft_redstone_rescan(void)     {}
void craft_redstone_mark_dirty(void) {}

static void fb_fill(uint16_t c) {
    for (int i = 0; i < CRAFT_FB_W * CRAFT_FB_H; i++) g_fb[i] = c;
}

typedef struct { bool prev; } EdgeBtn;
static bool edge_press(EdgeBtn *e, bool now) {
    bool pressed = now && !e->prev;
    e->prev = now;
    return pressed;
}

/* --- Dual-core tile work-stealing renderer ------------------ */
#define TILE_H     8
#define TILE_COUNT (CRAFT_FB_H / TILE_H)
static volatile uint32_t s_next_tile;
static volatile bool     s_core1_go   = false;
static volatile bool     s_core1_done = false;
static CraftCamera       s_render_cam;

static void run_tiles(void) {
    while (true) {
        uint32_t t = __atomic_fetch_add(&s_next_tile, 1, __ATOMIC_RELAXED);
        if (t >= TILE_COUNT) break;
        int y0 = (int)t * TILE_H;
        int y1 = y0 + TILE_H;
        if (y1 > CRAFT_FB_H) y1 = CRAFT_FB_H;
        craft_render_strip(&s_render_cam, g_fb, y0, y1);
    }
}

static void core1_entry(void) {
    while (true) {
        while (!s_core1_go) tight_loop_contents();
        s_core1_go = false;
        run_tiles();
        s_core1_done = true;
    }
}

static float g_yaw = 0.6f;
static float g_cx, g_cz;   /* camera target centre in world XZ */

static void frame_iso(CraftCamera *cam) {
    float dist = 30.0f, h = 48.0f;
    cam->pos.x = g_cx - dist * sinf(g_yaw);
    cam->pos.y = h;
    cam->pos.z = g_cz - dist * cosf(g_yaw);
    cam->yaw   = g_yaw;
    cam->pitch = -0.62f;   /* ~ -35.5° iso 3/4 tilt */
    cam->fov   = 0.95f;
}

int main(void) {
    set_sys_clock_khz(280000, true);

    craft_lcd_init();
    fb_fill(0x0000);
    craft_lcd_present(g_fb);
    craft_buttons_init();

    craft_world_init();
    craft_blocks_build_textures();
    craft_tool_models_init();

    craft_render_set_fog(false);
    craft_render_set_clouds(true);
    craft_render_set_far_lod(false);
    craft_render_set_groundcover(true);
    craft_render_set_interlace(false);
    craft_render_set_lowres(false);
    craft_render_set_coarse_skip(false);
    craft_render_set_torch_light(false);
    craft_render_set_player_light(false);
    craft_render_set_time(80.0f);

    uint32_t seed = get_rand_32();
    craft_world_load_around(CRAFT_WORLD_X / 2, CRAFT_WORLD_Z / 2, seed);
    g_cx = CRAFT_WORLD_X * 0.5f;
    g_cz = CRAFT_WORLD_Z * 0.5f;

    multicore_launch_core1(core1_entry);

    EdgeBtn e_menu = {0};
    uint32_t last_ms = to_ms_since_boot(get_absolute_time());

    while (true) {
        CraftRawButtons btn;
        craft_buttons_read(&btn);
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        float dt = (now_ms - last_ms) * 0.001f;
        if (dt > 0.1f) dt = 0.1f;
        last_ms = now_ms;

        if (edge_press(&e_menu, btn.menu)) {
            fb_fill(0x0007);
            craft_lcd_present(g_fb);
            seed = get_rand_32();
            craft_world_load_around(CRAFT_WORLD_X / 2, CRAFT_WORLD_Z / 2, seed);
        }

        float pan = 18.0f * dt;
        float fx = sinf(g_yaw), fz = cosf(g_yaw);
        float rx = cosf(g_yaw), rz = -sinf(g_yaw);
        if (btn.up)    { g_cx += fx * pan; g_cz += fz * pan; }
        if (btn.down)  { g_cx -= fx * pan; g_cz -= fz * pan; }
        if (btn.right) { g_cx += rx * pan; g_cz += rz * pan; }
        if (btn.left)  { g_cx -= rx * pan; g_cz -= rz * pan; }
        if (btn.lb) g_yaw -= 1.5f * dt;
        if (btn.rb) g_yaw += 1.5f * dt;

        CraftCamera cam;
        frame_iso(&cam);

        s_render_cam = cam;
        craft_render_begin(&cam);
        __atomic_store_n(&s_next_tile, 0, __ATOMIC_RELAXED);
        s_core1_done = false;
        s_core1_go   = true;
        run_tiles();
        while (!s_core1_done) tight_loop_contents();

        craft_lcd_present(g_fb);
    }
    return 0;
}

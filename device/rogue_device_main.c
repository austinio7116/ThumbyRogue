/*
 * ThumbyRogue — device entry point (standalone, RP2350).
 *
 * Thin platform shell over the shared rogue_game loop: read buttons, tick,
 * fetch the camera, render the world strip across both cores (tile work-
 * stealing), draw the entity/HUD overlay, present.
 *
 *   D-pad   move (screen-relative)      LB/RB  rotate the iso view ±90°
 *   MENU    regenerate the level
 */
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/rand.h"
#include "hardware/clocks.h"

#include "craft_lcd_gc9107.h"
#include "craft_buttons.h"
#include "craft_render.h"
#include "craft_world.h"
#include "craft_blocks.h"
#include "craft_audio.h"
#include "craft_audio_pwm.h"
#include "rogue_game.h"

void craft_tool_models_init(void);
void craft_blocks_build_textures(void);

static uint16_t g_fb[CRAFT_FB_W * CRAFT_FB_H];

bool craft_save_slot_used(int slot)             { (void)slot; return false; }
const uint16_t *craft_save_slot_thumb(int slot) { (void)slot; return NULL; }
uint32_t craft_platform_rand32(void)            { return (uint32_t)get_rand_32(); }

/* No redstone in ThumbyRogue — no-op the hooks craft_world calls. */
void craft_redstone_note_change(BlockId p, BlockId n) { (void)p; (void)n; }
void craft_redstone_rescan(void)     {}
void craft_redstone_mark_dirty(void) {}

static void fb_fill(uint16_t c) {
    for (int i = 0; i < CRAFT_FB_W * CRAFT_FB_H; i++) g_fb[i] = c;
}

/* --- Dual-core tile work-stealing world render ------------------ */
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
        int y0 = (int)t * TILE_H, y1 = y0 + TILE_H;
        if (y1 > CRAFT_FB_H) y1 = CRAFT_FB_H;
        craft_render_strip(&s_render_cam, g_fb, y0, y1);
    }
}
static void core1_entry(void) {
    multicore_lockout_victim_init();   /* allow core0 to park us during flash saves */
    while (true) {
        while (!s_core1_go) tight_loop_contents();
        s_core1_go = false;
        run_tiles();
        s_core1_done = true;
    }
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
    craft_audio_pwm_init();
    rogue_game_init(get_rand_32());

    multicore_launch_core1(core1_entry);

    uint32_t last_ms = to_ms_since_boot(get_absolute_time());
    while (true) {
        CraftRawButtons btn;
        craft_buttons_read(&btn);
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        float dt = (now_ms - last_ms) * 0.001f;
        if (dt > 0.1f) dt = 0.1f;
        last_ms = now_ms;

        rogue_game_tick(&btn, dt);

        rogue_game_get_camera(&s_render_cam);
        craft_render_begin(&s_render_cam);
        /* The framebuffer is single-buffered and craft_lcd_present streams it
         * to the panel via async DMA. Wait for the PREVIOUS frame's DMA to
         * finish before we start writing g_fb again, otherwise the render
         * races the transfer and tears a flickering line across the screen. */
        craft_lcd_wait_idle();
        __atomic_store_n(&s_next_tile, 0, __ATOMIC_RELAXED);
        s_core1_done = false;
        s_core1_go   = true;
        run_tiles();
        while (!s_core1_done) tight_loop_contents();

        rogue_game_draw_overlay(g_fb);

        /* Pump procedural audio to the PWM sink. */
        int room = craft_audio_pwm_room();
        while (room > 0) {
            int16_t buf[128];
            int n = room < 128 ? room : 128;
            craft_audio_render(buf, n);
            craft_audio_pwm_push(buf, n);
            room -= n;
        }

        craft_lcd_present(g_fb);
    }
    return 0;
}

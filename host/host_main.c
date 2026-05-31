/*
 * ThumbyRogue — host (Linux/SDL2) shell.
 *
 * Thin platform layer over the shared rogue_game loop: maps keyboard to
 * CraftRawButtons, ticks the game, renders the world strip + entity/HUD
 * overlay, and presents at 3x scale.
 *
 * Keys (match the device's keyboard mapping convention):
 *   W/A/S/D    D-pad (move; screen-relative)
 *   .  ,       A / B
 *   LShift     LB (rotate view CCW)      Space  RB (rotate view CW)
 *   Enter      MENU (regenerate level)
 *   ESC / F12  quit
 *
 * ROGUE_SHOT=path -> render N frames headless to a PPM and exit.
 */
#include "craft_render.h"
#include "craft_world.h"
#include "craft_blocks.h"
#include "craft_types.h"
#include "craft_buttons.h"
#include "craft_audio.h"
#include "rogue_game.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SCALE 5
#define WIN_W (CRAFT_FB_W * SCALE)
#define WIN_H (CRAFT_FB_H * SCALE)

static uint16_t g_fb[CRAFT_FB_W * CRAFT_FB_H];

void craft_tool_models_init(void);
void craft_blocks_build_textures(void);

/* Platform/save hooks referenced by the engine but unused here. */
uint32_t craft_platform_rand32(void) { return (uint32_t)rand(); }
bool craft_save_slot_used(int slot)             { (void)slot; return false; }
const uint16_t *craft_save_slot_thumb(int slot) { (void)slot; return NULL; }

/* ThumbyRogue has no redstone — no-op the hooks craft_world calls. */
void craft_redstone_note_change(BlockId p, BlockId n) { (void)p; (void)n; }
void craft_redstone_rescan(void)     {}
void craft_redstone_mark_dirty(void) {}

static SDL_AudioDeviceID g_audio;
static void audio_cb(void *ud, Uint8 *stream, int len) {
    (void)ud;
    craft_audio_render((int16_t *)stream, len / (int)sizeof(int16_t));
}
static void audio_init(void) {
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = CRAFT_AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = audio_cb;
    g_audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (g_audio) SDL_PauseAudioDevice(g_audio, 0);
}

/* Dismiss the title screen (headless/auto paths). */
static void press_start(void) {
    CraftRawButtons b = {0}; b.a = true;
    rogue_game_tick(&b, 1.0f / 30.0f);
    b.a = false;
    rogue_game_tick(&b, 1.0f / 30.0f);
}

static void render_frame(void) {
    CraftCamera cam;
    rogue_game_get_camera(&cam);
    craft_render_begin(&cam);
    craft_render_strip(&cam, g_fb, 0, CRAFT_FB_H);
    rogue_game_draw_overlay(g_fb);
}

static void dump_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", CRAFT_FB_W, CRAFT_FB_H);
    for (int i = 0; i < CRAFT_FB_W * CRAFT_FB_H; i++) {
        uint16_t c = g_fb[i];
        uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 0x1F) * 255 / 31),
                           (uint8_t)(((c >>  5) & 0x3F) * 255 / 63),
                           (uint8_t)(( c        & 0x1F) * 255 / 31) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("[rogue] wrote %s\n", path);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *shot_path = getenv("ROGUE_SHOT");

    if (SDL_Init(shot_path ? SDL_INIT_EVENTS
                           : (SDL_INIT_VIDEO | SDL_INIT_EVENTS)) != 0)
        fprintf(stderr, "SDL_Init: %s (continuing)\n", SDL_GetError());

    SDL_Window   *win = NULL;
    SDL_Renderer *ren = NULL;
    SDL_Texture  *tex = NULL;
    if (!shot_path) {
        win = SDL_CreateWindow("ThumbyRogue", SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, SDL_WINDOW_SHOWN);
        ren = SDL_CreateRenderer(win, -1,
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
            SDL_TEXTUREACCESS_STREAMING, CRAFT_FB_W, CRAFT_FB_H);
        audio_init();
    }

    srand((unsigned)time(NULL));
    uint32_t seed = (argc > 1) ? (uint32_t)strtoul(argv[1], NULL, 0)
                               : (uint32_t)rand();
    printf("[rogue] seed = %u\n", seed);

    craft_world_init();
    craft_blocks_build_textures();
    craft_tool_models_init();
    rogue_game_init(seed);
    if (getenv("ROGUE_DEPTH")) {
        extern void rogue_game_debug_set_depth(int);
        rogue_game_debug_set_depth(atoi(getenv("ROGUE_DEPTH")));
    }

    /* Headless autopilot: hold forward + mash attack for ~12s, logging
     * HP / depth / foes each second — verifies the combat loop end-to-end. */
    if (getenv("ROGUE_DEMO")) {
        extern int rogue_game_player_hp(void);
        extern int rogue_enemies_alive_count(void);
        extern void rogue_game_demo_step(float dt, int frame);
        extern void rogue_game_debug_drop_weapon(void);
        press_start();
        float t = 0;
        for (int f = 0; f < 25 * 30; f++) {
            if (f == 10) rogue_game_debug_drop_weapon();   /* test equip swap */
            rogue_game_demo_step(1.0f / 30.0f, f);
            t += 1.0f / 30.0f;
            if (f % 30 == 0) {
                extern int rogue_game_player_gold(void);
                extern const char *rogue_game_weapon_name(void);
                printf("[demo] t=%.0fs depth=%d hp=%d foes=%d gold=%d wpn=%s\n",
                       t, rogue_game_depth(), rogue_game_player_hp(),
                       rogue_enemies_alive_count(), rogue_game_player_gold(),
                       rogue_game_weapon_name());
            }
            if (shot_path && f == 90) { render_frame(); dump_ppm(shot_path); }
        }
        SDL_Quit();
        return 0;
    }

    if (shot_path) {
        /* Settle (past the band banner ~2.2s), then dump. */
        CraftRawButtons none = {0};
        if (!getenv("ROGUE_TITLE")) press_start();
        for (int i = 0; i < 80; i++) rogue_game_tick(&none, 1.0f / 30.0f);
        if (getenv("ROGUE_DEAD")) {
            extern void rogue_game_debug_kill(void);
            rogue_game_debug_kill();
            rogue_game_tick(&none, 1.0f / 30.0f);
        }
        render_frame();
        dump_ppm(shot_path);
        SDL_Quit();
        return 0;
    }

    bool running = true;
    Uint32 last_ms = SDL_GetTicks();
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN) {
                SDL_Scancode sc = ev.key.keysym.scancode;
                if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_F12)
                    running = false;
            }
        }
        Uint32 now_ms = SDL_GetTicks();
        float dt = (now_ms - last_ms) * 0.001f;
        if (dt > 0.1f) dt = 0.1f;
        last_ms = now_ms;

        const Uint8 *k = SDL_GetKeyboardState(NULL);
        CraftRawButtons btn = {
            .up    = k[SDL_SCANCODE_W],
            .down  = k[SDL_SCANCODE_S],
            .left  = k[SDL_SCANCODE_A],
            .right = k[SDL_SCANCODE_D],
            .a     = k[SDL_SCANCODE_PERIOD],
            .b     = k[SDL_SCANCODE_COMMA],
            .lb    = k[SDL_SCANCODE_LSHIFT],
            .rb    = k[SDL_SCANCODE_SPACE],
            .menu  = k[SDL_SCANCODE_RETURN],
        };

        rogue_game_tick(&btn, dt);
        render_frame();

        SDL_UpdateTexture(tex, NULL, g_fb, CRAFT_FB_W * sizeof(uint16_t));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

/*
 * ThumbyRogue - FunKey S / RG Nano SDL 1.2 launcher.
 */
#include "craft_audio.h"
#include "craft_blocks.h"
#include "craft_buttons.h"
#include "craft_render.h"
#include "craft_types.h"
#include "craft_world.h"
#include "rogue_game.h"

#include <SDL/SDL.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef SDL_INIT_EVENTS
#define SDL_INIT_EVENTS 0
#endif

#define SCREEN_W 240
#define SCREEN_H 240

static uint16_t g_fb[CRAFT_FB_W * CRAFT_FB_H];
static uint16_t g_scaled[SCREEN_W * SCREEN_H];
static char g_app_dir[256] = "/mnt/FunKey/.thumbyrogue";
static SDL_Surface *g_screen;
static SDL_Joystick *g_joy;
static bool g_running = true;

void craft_tool_models_init(void);
void craft_blocks_build_textures(void);

static void mkdir_p(const char *path) {
    char tmp[320];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

uint32_t craft_platform_rand32(void) {
    uint32_t a = (uint32_t)rand();
    uint32_t b = (uint32_t)SDL_GetTicks();
    return (a << 16) ^ a ^ b;
}

bool craft_save_slot_used(int slot) {
    (void)slot;
    return false;
}

const uint16_t *craft_save_slot_thumb(int slot) {
    (void)slot;
    return NULL;
}

void craft_redstone_note_change(BlockId prev, BlockId next) {
    (void)prev;
    (void)next;
}

void craft_redstone_rescan(void) {}
void craft_redstone_mark_dirty(void) {}

static void save_path(char *out, size_t out_len) {
    snprintf(out, out_len, "%s/thumbyrogue.sav", g_app_dir);
}

int rogue_plat_save(const uint8_t *data, int len) {
    mkdir_p(g_app_dir);
    char path[320];
    save_path(path, sizeof path);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[funkey] open %s: %s\n", path, strerror(errno));
        return 0;
    }
    size_t put = fwrite(data, 1, (size_t)len, f);
    int ok = (put == (size_t)len) && (fclose(f) == 0);
    fprintf(stderr, "[funkey] save: %s (%d bytes)\n", ok ? "ok" : "failed", len);
    return ok;
}

int rogue_plat_load(uint8_t *data, int max) {
    char path[320];
    save_path(path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int n = (int)fread(data, 1, (size_t)max, f);
    fclose(f);
    fprintf(stderr, "[funkey] load: %d bytes\n", n);
    return n;
}

static void audio_cb(void *ud, Uint8 *stream, int len) {
    (void)ud;
    craft_audio_render((int16_t *)stream, len / (int)sizeof(int16_t));
}

static void audio_init(void) {
    SDL_AudioSpec want;
    SDL_AudioSpec have;
    memset(&want, 0, sizeof want);
    want.freq = CRAFT_AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = audio_cb;
    if (SDL_OpenAudio(&want, &have) == 0) {
        SDL_PauseAudio(0);
    } else {
        fprintf(stderr, "[funkey] SDL_OpenAudio: %s\n", SDL_GetError());
    }
}

static bool key_down(SDLKey key) {
    Uint8 *k = SDL_GetKeyState(NULL);
    return k[key] != 0;
}

static bool joy_button(int idx) {
    if (!g_joy || idx < 0 || idx >= SDL_JoystickNumButtons(g_joy)) return false;
    return SDL_JoystickGetButton(g_joy, idx) != 0;
}

static int joy_axis(int idx) {
    if (!g_joy || idx < 0 || idx >= SDL_JoystickNumAxes(g_joy)) return 0;
    return SDL_JoystickGetAxis(g_joy, idx);
}

static Uint8 joy_hat(int idx) {
    if (!g_joy || idx < 0 || idx >= SDL_JoystickNumHats(g_joy)) return SDL_HAT_CENTERED;
    return SDL_JoystickGetHat(g_joy, idx);
}

static void poll_input(CraftRawButtons *btn) {
    SDL_PumpEvents();
    memset(btn, 0, sizeof *btn);

    int ax0 = joy_axis(0);
    int ax1 = joy_axis(1);
    Uint8 hat0 = joy_hat(0);
    btn->up    = key_down(SDLK_UP)    || key_down(SDLK_u) || ax1 < -12000 || (hat0 & SDL_HAT_UP);
    btn->down  = key_down(SDLK_DOWN)  || key_down(SDLK_d) || ax1 >  12000 || (hat0 & SDL_HAT_DOWN);
    btn->left  = key_down(SDLK_LEFT)  || key_down(SDLK_l) || ax0 < -12000 || (hat0 & SDL_HAT_LEFT);
    btn->right = key_down(SDLK_RIGHT) || key_down(SDLK_r) || ax0 >  12000 || (hat0 & SDL_HAT_RIGHT);

    btn->a    = key_down(SDLK_a) || key_down(SDLK_RETURN) || key_down(SDLK_SPACE) || joy_button(0);
    btn->b    = key_down(SDLK_b) || joy_button(1);
    btn->lb   = key_down(SDLK_y) || key_down(SDLK_n) || joy_button(3) || joy_button(4);
    btn->rb   = key_down(SDLK_x) || key_down(SDLK_m) || joy_button(2) || joy_button(5);
    btn->menu = key_down(SDLK_s) || key_down(SDLK_ESCAPE) || joy_button(6) || joy_button(7);
}

static void render_frame(void) {
    CraftCamera cam;
    rogue_game_get_camera(&cam);
    craft_render_begin(&cam);
    craft_render_strip(&cam, g_fb, 0, CRAFT_FB_H);
    rogue_game_draw_overlay(g_fb);
}

static void present_frame(void) {
    for (int y = 0; y < SCREEN_H; y++) {
        int sy = y * CRAFT_FB_H / SCREEN_H;
        for (int x = 0; x < SCREEN_W; x++) {
            int sx = x * CRAFT_FB_W / SCREEN_W;
            g_scaled[y * SCREEN_W + x] = g_fb[sy * CRAFT_FB_W + sx];
        }
    }

    if (SDL_MUSTLOCK(g_screen)) SDL_LockSurface(g_screen);
    if (g_screen->format->BitsPerPixel == 16 &&
        g_screen->format->Rmask == 0xF800 &&
        g_screen->format->Gmask == 0x07E0 &&
        g_screen->format->Bmask == 0x001F) {
        for (int y = 0; y < SCREEN_H; y++) {
            memcpy((uint8_t *)g_screen->pixels + y * g_screen->pitch,
                   &g_scaled[y * SCREEN_W],
                   SCREEN_W * sizeof(uint16_t));
        }
    } else {
        for (int y = 0; y < SCREEN_H; y++) {
            uint8_t *row = (uint8_t *)g_screen->pixels + y * g_screen->pitch;
            for (int x = 0; x < SCREEN_W; x++) {
                uint16_t c = g_scaled[y * SCREEN_W + x];
                uint8_t r = (uint8_t)(((c >> 11) & 0x1F) << 3);
                uint8_t g = (uint8_t)(((c >> 5) & 0x3F) << 2);
                uint8_t b = (uint8_t)((c & 0x1F) << 3);
                uint32_t mapped = SDL_MapRGB(g_screen->format, r, g, b);
                memcpy(row + x * g_screen->format->BytesPerPixel,
                       &mapped, g_screen->format->BytesPerPixel);
            }
        }
    }
    if (SDL_MUSTLOCK(g_screen)) SDL_UnlockSurface(g_screen);
    SDL_Flip(g_screen);
}

static void handle_events(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            g_running = false;
        } else if (ev.type == SDL_KEYDOWN) {
            SDLKey key = ev.key.keysym.sym;
            if (key == SDLK_q || key == SDLK_F12) g_running = false;
        }
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    const char *app_dir = getenv("THUMBYROGUE_HOME");
    if (app_dir && app_dir[0]) snprintf(g_app_dir, sizeof g_app_dir, "%s", app_dir);
    mkdir_p(g_app_dir);

    char log_path[320];
    snprintf(log_path, sizeof log_path, "%s/thumbyrogue.log", g_app_dir);
    freopen(log_path, "a", stderr);
    fprintf(stderr, "\n[funkey] start %ld\n", (long)time(NULL));

    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "[funkey] SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_ShowCursor(SDL_DISABLE);
    if (SDL_NumJoysticks() > 0) {
        g_joy = SDL_JoystickOpen(0);
        SDL_JoystickEventState(SDL_ENABLE);
    }

    g_screen = SDL_SetVideoMode(SCREEN_W, SCREEN_H, 16, SDL_HWSURFACE | SDL_FULLSCREEN);
    if (!g_screen) g_screen = SDL_SetVideoMode(SCREEN_W, SCREEN_H, 16, SDL_SWSURFACE);
    if (!g_screen) {
        fprintf(stderr, "[funkey] SDL_SetVideoMode: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_WM_SetCaption("ThumbyRogue", "ThumbyRogue");
    audio_init();

    uint32_t seed = craft_platform_rand32();
    fprintf(stderr, "[funkey] seed = %u\n", seed);
    craft_world_init();
    craft_blocks_build_textures();
    craft_tool_models_init();
    rogue_game_init(seed);

    uint32_t last_ms = SDL_GetTicks();
    while (g_running) {
        handle_events();

        uint32_t now_ms = SDL_GetTicks();
        float dt = (now_ms - last_ms) * 0.001f;
        if (dt > 0.1f) dt = 0.1f;
        last_ms = now_ms;

        CraftRawButtons btn;
        poll_input(&btn);
        rogue_game_tick(&btn, dt);
        render_frame();
        present_frame();
    }

    rogue_game_save_full();
    SDL_CloseAudio();
    if (g_joy) SDL_JoystickClose(g_joy);
    SDL_Quit();
    return 0;
}

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

/* Save storage — a file next to the binary. */
int rogue_plat_save(const uint8_t *data, int len) {
    FILE *f = fopen("thumbyrogue.sav", "wb");
    if (!f) return 0;
    fwrite(data, 1, (size_t)len, f);
    fclose(f);
    return 1;
}
int rogue_plat_load(uint8_t *data, int max) {
    FILE *f = fopen("thumbyrogue.sav", "rb");
    if (!f) return 0;
    int n = (int)fread(data, 1, (size_t)max, f);
    fclose(f);
    return n;
}

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

    if (getenv("ROGUE_SWEEP")) {   /* reachability sweep: scenery must never block the path */
        extern int rogue_gen_debug_sweep(int);
        int n = atoi(getenv("ROGUE_SWEEP"));
        if (n < 1) n = 200;
        return rogue_gen_debug_sweep(n) == 0 ? 0 : 1;
    }
    if (getenv("ROGUE_DUMP")) {    /* top-down diagnostic for one seed:depth */
        extern void rogue_gen_debug_dump(uint32_t, int);
        int s = atoi(getenv("ROGUE_DUMP"));
        const char *colon = strchr(getenv("ROGUE_DUMP"), ':');
        int depth = colon ? atoi(colon + 1) : 1;
        rogue_gen_debug_dump((uint32_t)(s * 2654435761u + 12345u), depth);
        return 0;
    }

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
    if (getenv("ROGUE_MKSAVE")) {   /* make a save then exit */
        extern void rogue_game_debug_set_depth(int), rogue_game_debug_gear_up(void);
        extern const char *rogue_game_weapon_name(void);
        extern int rogue_game_player_gold(void);
        rogue_game_debug_set_depth(atoi(getenv("ROGUE_MKSAVE")));
        rogue_game_debug_gear_up();
        rogue_game_save(1);
        printf("[save] made save depth=%d gold=%d wpn=%s\n",
               rogue_game_depth(), rogue_game_player_gold(), rogue_game_weapon_name());
        return 0;
    }
    if (getenv("ROGUE_SAVECHK")) {  /* report what init resumed, then exit */
        extern int rogue_game_player_gold(void);
        extern const char *rogue_game_weapon_name(void);
        printf("[save] resumed depth=%d gold=%d wpn=%s\n",
               rogue_game_depth(), rogue_game_player_gold(), rogue_game_weapon_name());
        return 0;
    }

    /* Headless autopilot: hold forward + mash attack for ~12s, logging
     * HP / depth / foes each second — verifies the combat loop end-to-end. */
    if (getenv("ROGUE_DEMO")) {
        extern int rogue_game_player_hp(void);
        extern int rogue_enemies_alive_count(void);
        extern void rogue_game_demo_step(float dt, int frame);
        extern void rogue_game_debug_drop_weapon(void);
        extern void rogue_game_debug_gear_up(void);
        extern int rogue_game_player_maxhp(void), rogue_game_player_armor(void), rogue_game_player_wdmg(void);
        extern const char *rogue_game_weapon_name(void);
        press_start();
        printf("[stats] base: maxhp=%d armor=%d wdmg=%d wpn=%s\n",
               rogue_game_player_maxhp(), rogue_game_player_armor(),
               rogue_game_player_wdmg(), rogue_game_weapon_name());
        rogue_game_debug_gear_up();
        printf("[stats] geared: maxhp=%d armor=%d wdmg=%d wpn=%s\n",
               rogue_game_player_maxhp(), rogue_game_player_armor(),
               rogue_game_player_wdmg(), rogue_game_weapon_name());
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
            { extern float rogue_game_player_y(void);
              if (f >= 8 && f <= 24) printf("[y] f=%d y=%.2f\n", f, rogue_game_player_y()); }
            if (shot_path && f == 80) { render_frame(); dump_ppm(shot_path); }
        }
        SDL_Quit();
        return 0;
    }

    if (shot_path) {
        /* Settle (past the band banner ~2.2s), then dump. */
        CraftRawButtons none = {0};
        if (!getenv("ROGUE_TITLE")) press_start();
        if (getenv("ROGUE_BEAM")) { extern void rogue_game_debug_beam(void); rogue_game_debug_beam(); }
        if (getenv("ROGUE_TORCH")) { extern void rogue_game_debug_set_torch(float); rogue_game_debug_set_torch((float)atof(getenv("ROGUE_TORCH"))); }
        if (getenv("ROGUE_SHOPUI")) { extern void rogue_game_debug_open_shop(void); rogue_game_debug_open_shop(); }
        if (getenv("ROGUE_DETAIL") || getenv("ROGUE_GEMPICK") || getenv("ROGUE_SOCKET")) {
            extern void rogue_game_debug_detail_setup(void);
            rogue_game_debug_detail_setup();
            CraftRawButtons z = {0}, t;
            t = z; t.b = true; rogue_game_tick(&t, 1/30.f); rogue_game_tick(&z, 1/30.f);  /* B -> detail */
            if (getenv("ROGUE_GEMPICK") || getenv("ROGUE_SOCKET")) {
                t = z; t.down = true; rogue_game_tick(&t, 1/30.f); rogue_game_tick(&z, 1/30.f); /* -> Socket */
                t = z; t.a = true;    rogue_game_tick(&t, 1/30.f); rogue_game_tick(&z, 1/30.f); /* open gem pick */
            }
            if (getenv("ROGUE_SOCKET")) {
                extern int rogue_game_player_maxhp(void);
                t = z; t.a = true;    rogue_game_tick(&t, 1/30.f); rogue_game_tick(&z, 1/30.f); /* socket ruby */
                printf("[socket] after socketing ruby: maxhp=%d (expect +20)\n", rogue_game_player_maxhp());
            }
        }
        if (getenv("ROGUE_WEAPONS")) {
            extern void rogue_game_debug_weapon_sheet(void);
            rogue_game_debug_weapon_sheet();
        }
        if (getenv("ROGUE_INV")) {
            extern void rogue_game_debug_fill_bag(void);
            extern int rogue_inventory_count(void), rogue_game_player_maxhp(void), rogue_game_player_gold(void);
            rogue_game_debug_fill_bag();
            printf("[inv] open bag=%d maxhp=%d\n", rogue_inventory_count(), rogue_game_player_maxhp());
            CraftRawButtons b={0}, z={0};
            /* move cursor onto first backpack item, then equip it */
            for (int k=0;k<6;k++){ b=z; b.right=true; rogue_game_tick(&b,1/30.f); rogue_game_tick(&z,1/30.f); }
            b=z; b.a=true; rogue_game_tick(&b,1/30.f); rogue_game_tick(&z,1/30.f);
            printf("[inv] after equip bag=%d maxhp=%d\n", rogue_inventory_count(), rogue_game_player_maxhp());
            /* salvage next item */
            b=z; b.right=true; rogue_game_tick(&b,1/30.f); rogue_game_tick(&z,1/30.f);
            b=z; b.b=true; rogue_game_tick(&b,1/30.f); rogue_game_tick(&z,1/30.f);
            printf("[inv] after salvage bag=%d gold=%d\n", rogue_inventory_count(), rogue_game_player_gold());
        }
        if (getenv("ROGUE_CENSUS")) {
            long lava = 0, lamp = 0;
            for (int y = 0; y < CRAFT_WORLD_Y; y++)
              for (int z = 0; z < CRAFT_WORLD_Z; z++)
                for (int x = 0; x < CRAFT_WORLD_X; x++) {
                    int b = craft_world_get(x,y,z);
                    if (b == 91) lava++; else if (b == 73) lamp++;
                }
            printf("[census] lava=%ld lamp(brazier)=%ld\n", lava, lamp);
        }
        if (getenv("ROGUE_WATER")) {
            extern int rogue_game_debug_goto_water(void);
            printf("[water] found pool: %d\n", rogue_game_debug_goto_water());
        }
        if (getenv("ROGUE_LAVA")) {
            extern int rogue_game_debug_goto_lava(void);
            printf("[lava] found: %d\n", rogue_game_debug_goto_lava());
        }
        if (getenv("ROGUE_DECO")) {
            extern int rogue_game_debug_goto_deco(void);
            printf("[deco] found scenery: %d\n", rogue_game_debug_goto_deco());
        }
        if (getenv("ROGUE_PROP")) {
            extern int rogue_game_debug_goto_prop(int);
            printf("[prop] found: %d\n", rogue_game_debug_goto_prop(atoi(getenv("ROGUE_PROP"))));
        }
        { int settle = getenv("ROGUE_SETTLE") ? atoi(getenv("ROGUE_SETTLE")) : 80;
          for (int i = 0; i < settle; i++) rogue_game_tick(&none, 1.0f / 30.0f); }
        if (getenv("ROGUE_WALK")) {   /* freeze a mid-stride pose (after settle) */
            extern void rogue_game_debug_walkpose(float);
            rogue_game_debug_walkpose((float)atof(getenv("ROGUE_WALK")));
        }
        if (getenv("ROGUE_XRAY")) {   /* walk south into the camera-side wall */
            CraftRawButtons d = {0}; d.down = true;
            for (int i = 0; i < 50; i++) rogue_game_tick(&d, 1.0f / 30.0f);
        }
        if (getenv("ROGUE_FXWPN")) {   /* equip a weapon, swing/fire, catch the FX mid-action */
            extern void rogue_game_debug_force_weapon(int);
            extern void rogue_game_debug_set_yaw(float);
            rogue_game_debug_force_weapon(atoi(getenv("ROGUE_FXWPN")));
            int settle = getenv("ROGUE_FXT") ? atoi(getenv("ROGUE_FXT")) : 3;
            CraftRawButtons a = {0}; a.a = true;
            rogue_game_tick(&a, 1.0f / 30.0f);   /* start the swing (auto-face runs here) */
            if (getenv("ROGUE_FXYAW"))           /* override facing after auto-face; deg: 0=+z away, 90=+x right */
                rogue_game_debug_set_yaw((float)atof(getenv("ROGUE_FXYAW")) * 3.14159265f / 180.0f);
            for (int i = 0; i < settle; i++) rogue_game_tick(&none, 1.0f / 30.0f);
        }
        if (getenv("ROGUE_DMGNUM")) {
            extern void rogue_game_debug_dmgnum(void);
            rogue_game_debug_dmgnum();
        }
        if (getenv("ROGUE_LOOT")) {
            extern void rogue_game_debug_drop_loot(void);
            rogue_game_debug_drop_loot();
        }
        if (getenv("ROGUE_MAP")) {   /* reveal map + open inventory grid to view it */
            extern void rogue_game_debug_fill_bag(void), rogue_game_debug_reveal_map(void);
            rogue_game_debug_fill_bag();
            rogue_game_debug_reveal_map();
        }
        if (getenv("ROGUE_SKIP")) {   /* hold LB+RB ~5.6s to open the skip menu */
            CraftRawButtons h = {0}; h.lb = true; h.rb = true;
            for (int i = 0; i < 170; i++) rogue_game_tick(&h, 1.0f / 30.0f);
        }
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

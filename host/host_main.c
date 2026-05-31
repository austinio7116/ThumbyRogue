/*
 * ThumbyRogue — Phase 0 host driver.
 *
 * Goal of this milestone: prove the vendored ThumbyCraft engine builds
 * and runs under the ThumbyRogue name. We bring up the world arrays,
 * generate a default seeded world, and drive craft_render directly with
 * an isometric-leaning camera you can pan/rotate — a preview of the
 * direction the real game's fixed-iso camera (Phase 1) will take.
 *
 * No game loop yet (no player/mobs/combat) — those land in later phases.
 *
 * Keys:
 *   W/A/S/D          pan camera over the world (screen-relative-ish)
 *   Q / E            rotate yaw left / right (smooth, for now)
 *   Z / X            lower / raise camera
 *   R                regenerate with a new random seed
 *   F                toggle fog
 *   ESC / F12        quit
 *
 * ROGUE_SHOT=path env -> render one frame to a PPM and exit (headless CI).
 */
#include "craft_render.h"
#include "craft_world.h"
#include "craft_blocks.h"
#include "craft_types.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define SCALE   5
#define WIN_W   (CRAFT_FB_W * SCALE)
#define WIN_H   (CRAFT_FB_H * SCALE)

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static uint16_t g_fb[CRAFT_FB_W * CRAFT_FB_H];

/* Renderer shares some state with the held-item / texture paths. */
void craft_tool_models_init(void);
void craft_blocks_build_textures(void);

/* Platform hooks the engine references but Phase 0 doesn't drive. */
uint32_t craft_platform_rand32(void) { return (uint32_t)rand(); }
bool craft_save_slot_used(int slot)             { (void)slot; return false; }
const uint16_t *craft_save_slot_thumb(int slot) { (void)slot; return NULL; }

/* craft_world.c calls these on every block set / world load. ThumbyRogue
 * has no redstone, so we no-op them rather than link the redstone module
 * (which would pull in mobs/arrows/audio + their static SRAM). These will
 * become a documented vendor patch removing the calls from craft_world.c. */
void craft_redstone_note_change(BlockId prev_blk, BlockId new_blk) {
    (void)prev_blk; (void)new_blk;
}
void craft_redstone_rescan(void)     {}
void craft_redstone_mark_dirty(void) {}

static void dump_ppm(const char *path, const uint16_t *fb) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", CRAFT_FB_W, CRAFT_FB_H);
    for (int i = 0; i < CRAFT_FB_W * CRAFT_FB_H; i++) {
        uint16_t c = fb[i];
        int r = ((c >> 11) & 0x1F) * 255 / 31;
        int g = ((c >>  5) & 0x3F) * 255 / 63;
        int b = ( c        & 0x1F) * 255 / 31;
        uint8_t rgb[3] = { (uint8_t)r, (uint8_t)g, (uint8_t)b };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("[rogue] wrote %s\n", path);
}

/* Frame the iso camera on the centre of the resident window, looking
 * down at the ThumbyRogue "3/4" tilt the real camera will lock to. */
static void frame_iso(CraftCamera *cam, float yaw) {
    float cx = CRAFT_WORLD_X * 0.5f;
    float cz = CRAFT_WORLD_Z * 0.5f;
    float dist = 30.0f;
    float h    = 48.0f;   /* well above the ~y30 terrain surface */
    cam->pos.x = cx - dist * sinf(yaw);
    cam->pos.y = h;
    cam->pos.z = cz - dist * cosf(yaw);
    cam->yaw   = yaw;
    cam->pitch = -0.62f;   /* ~ -35.5°, classic iso 3/4 tilt */
    cam->fov   = 0.95f;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *shot_path = getenv("ROGUE_SHOT");

    if (SDL_Init(shot_path ? SDL_INIT_EVENTS
                           : (SDL_INIT_VIDEO | SDL_INIT_EVENTS)) != 0) {
        fprintf(stderr, "SDL_Init: %s (continuing for shot mode)\n",
                SDL_GetError());
    }

    SDL_Window   *win = NULL;
    SDL_Renderer *ren = NULL;
    SDL_Texture  *tex = NULL;
    if (!shot_path) {
        win = SDL_CreateWindow("ThumbyRogue (Phase 0 preview)",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            WIN_W, WIN_H, SDL_WINDOW_SHOWN);
        ren = SDL_CreateRenderer(win, -1,
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
            SDL_TEXTUREACCESS_STREAMING, CRAFT_FB_W, CRAFT_FB_H);
    }

    srand((unsigned)time(NULL));
    uint32_t seed = (argc > 1) ? (uint32_t)strtoul(argv[1], NULL, 0)
                               : (uint32_t)rand();
    printf("[rogue] seed = %u  (world %dx%dx%d)\n",
           seed, CRAFT_WORLD_X, CRAFT_WORLD_Y, CRAFT_WORLD_Z);

    craft_world_init();
    craft_blocks_build_textures();
    craft_tool_models_init();
    craft_world_load_around(CRAFT_WORLD_X / 2, CRAFT_WORLD_Z / 2, seed);

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

    float yaw = 0.6f;
    CraftCamera cam;
    frame_iso(&cam, yaw);

    if (shot_path) {
        craft_render_begin(&cam);
        craft_render_strip(&cam, g_fb, 0, CRAFT_FB_H);
        dump_ppm(shot_path, g_fb);
        SDL_Quit();
        return 0;
    }

    bool fog_on = false, running = true;
    Uint32 last_ms = SDL_GetTicks();
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN) {
                SDL_Scancode sc = ev.key.keysym.scancode;
                if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_F12)
                    running = false;
                if (sc == SDL_SCANCODE_R) {
                    seed = (uint32_t)rand();
                    printf("[rogue] regen, seed = %u\n", seed);
                    craft_world_load_around(CRAFT_WORLD_X / 2,
                                            CRAFT_WORLD_Z / 2, seed);
                }
                if (sc == SDL_SCANCODE_F) {
                    fog_on = !fog_on;
                    craft_render_set_fog(fog_on);
                }
            }
        }

        Uint32 now_ms = SDL_GetTicks();
        float dt = (now_ms - last_ms) * 0.001f;
        if (dt > 0.1f) dt = 0.1f;
        last_ms = now_ms;

        const Uint8 *k = SDL_GetKeyboardState(NULL);
        float pan = 18.0f * dt;
        /* Screen-relative pan: rotate the WASD vector by the camera yaw. */
        float fx = sinf(yaw),  fz = cosf(yaw);
        float rx = cosf(yaw),  rz = -sinf(yaw);
        if (k[SDL_SCANCODE_W]) { cam.pos.x += fx * pan; cam.pos.z += fz * pan; }
        if (k[SDL_SCANCODE_S]) { cam.pos.x -= fx * pan; cam.pos.z -= fz * pan; }
        if (k[SDL_SCANCODE_D]) { cam.pos.x += rx * pan; cam.pos.z += rz * pan; }
        if (k[SDL_SCANCODE_A]) { cam.pos.x -= rx * pan; cam.pos.z -= rz * pan; }
        if (k[SDL_SCANCODE_Z]) cam.pos.y -= pan;
        if (k[SDL_SCANCODE_X]) cam.pos.y += pan;
        if (cam.pos.y < 4.0f)  cam.pos.y = 4.0f;
        if (cam.pos.y > 60.0f) cam.pos.y = 60.0f;
        if (k[SDL_SCANCODE_Q]) yaw -= 1.5f * dt;
        if (k[SDL_SCANCODE_E]) yaw += 1.5f * dt;
        cam.yaw = yaw;

        craft_render_begin(&cam);
        craft_render_strip(&cam, g_fb, 0, CRAFT_FB_H);

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

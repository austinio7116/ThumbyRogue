#ifndef ROGUE_GAME_H
#define ROGUE_GAME_H
/*
 * ThumbyRogue shared game loop — platform-independent. The host and device
 * shells just (1) read buttons into a CraftRawButtons, (2) call tick, (3)
 * fetch the camera and render the world strips (single- or dual-core), then
 * (4) draw the entity/HUD overlay, and (5) present.
 */
#include <stdint.h>
#include "craft_buttons.h"
#include "craft_render.h"

void rogue_game_init(uint32_t seed);
void rogue_game_tick(const CraftRawButtons *btn, float dt);
void rogue_game_get_camera(CraftCamera *out);    /* render the world with this */
void rogue_game_draw_overlay(uint16_t *fb);      /* entities + HUD, after strip */

#endif /* ROGUE_GAME_H */

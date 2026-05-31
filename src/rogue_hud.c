#include "rogue_hud.h"
#include "craft_font.h"
#include "craft_types.h"
#include <stdio.h>
#include <string.h>

#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))

static void fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t c) {
    for (int j = y; j < y + h; j++) {
        if ((unsigned)j >= CRAFT_FB_H) continue;
        for (int i = x; i < x + w; i++) {
            if ((unsigned)i >= CRAFT_FB_W) continue;
            fb[j * CRAFT_FB_W + i] = c;
        }
    }
}

void rogue_hud_draw(uint16_t *fb, const RoguePlayer *p, int depth, int enemies) {
    /* Health bar, top-left. */
    int bw = 60, bh = 6, bx = 3, by = 3;
    fill_rect(fb, bx - 1, by - 1, bw + 2, bh + 2, RGB(10, 10, 10));
    fill_rect(fb, bx, by, bw, bh, RGB(70, 20, 20));
    int hpw = p->max_hp > 0 ? (p->hp * bw / p->max_hp) : 0;
    if (hpw < 0) hpw = 0;
    uint16_t hc = (p->hp * 3 >= p->max_hp) ? RGB(70, 210, 80) : RGB(220, 160, 40);
    if (p->hp * 4 < p->max_hp) hc = RGB(220, 50, 40);
    fill_rect(fb, bx, by, hpw, bh, hc);

    char buf[24];
    snprintf(buf, sizeof buf, "HP %d", p->hp);
    craft_font_draw(fb, buf, bx + 2, by, RGB(240, 240, 240));

    /* Depth, top-right. */
    snprintf(buf, sizeof buf, "DEPTH %d", depth);
    int w = craft_font_width(buf);
    craft_font_draw(fb, buf, CRAFT_FB_W - w - 3, 3, RGB(40, 230, 210));

    /* Enemies remaining, under depth. */
    snprintf(buf, sizeof buf, "FOES %d", enemies);
    w = craft_font_width(buf);
    craft_font_draw(fb, buf, CRAFT_FB_W - w - 3, 11, RGB(230, 120, 120));
}

void rogue_hud_banner(uint16_t *fb, const char *msg, uint16_t color) {
    int w = craft_font_width_2x(msg);
    int x = (CRAFT_FB_W - w) / 2;
    int y = CRAFT_FB_H / 2 - 6;
    fill_rect(fb, 0, y - 4, CRAFT_FB_W, 20, RGB(8, 8, 12));
    craft_font_draw_2x(fb, msg, x, y, color);
}

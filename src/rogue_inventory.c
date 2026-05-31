#include "rogue_inventory.h"
#include "rogue_stats.h"
#include "craft_font.h"
#include "craft_types.h"
#include <stdio.h>
#include <string.h>

#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))

static RogueItem s_bag[ROGUE_BAG_N];
static int  s_bag_n;
static bool s_open;
static int  s_cur;        /* 0..5 = paperdoll, 6.. = backpack item */

/* Paperdoll display order (row-major, 2 cols × 3 rows). */
static const EquipSlot PD[6] = {
    SLOT_WEAPON, SLOT_HELM, SLOT_OFFHAND, SLOT_AMULET, SLOT_ARMOR, SLOT_RING
};

void rogue_inventory_clear(void) { s_bag_n = 0; s_open = false; s_cur = 0; }
int  rogue_inventory_count(void) { return s_bag_n; }
bool rogue_inventory_full(void)  { return s_bag_n >= ROGUE_BAG_N; }
bool rogue_inventory_is_open(void){ return s_open; }
void rogue_inventory_open(void)  { s_open = true; s_cur = 0; }
void rogue_inventory_close(void) { s_open = false; }

bool rogue_inventory_add(const RogueItem *it) {
    if (s_bag_n >= ROGUE_BAG_N) return false;
    s_bag[s_bag_n++] = *it;
    return true;
}
static void bag_remove(int k) {
    for (int i = k; i < s_bag_n - 1; i++) s_bag[i] = s_bag[i + 1];
    s_bag_n--;
}

static int salvage_gold(const RogueItem *it) {
    return 4 + (int)it->rarity * 6 + it->n_affix * 2;
}

static bool edge(bool n, bool p) { return n && !p; }

void rogue_inventory_input(RoguePlayer *p, const CraftRawButtons *btn,
                           const CraftRawButtons *prev) {
    int total = 6 + s_bag_n;
    if (total < 6) total = 6;
    if (edge(btn->left,  prev->left)  || edge(btn->up,   prev->up))   s_cur--;
    if (edge(btn->right, prev->right) || edge(btn->down, prev->down)) s_cur++;
    if (s_cur < 0) s_cur = total - 1;
    if (s_cur >= total) s_cur = 0;

    if (edge(btn->a, prev->a)) {
        if (s_cur < 6) {
            /* unequip paperdoll slot -> backpack */
            EquipSlot sl = PD[s_cur];
            if (rogue_item_is_equip(&p->equip[sl]) && !rogue_inventory_full()) {
                rogue_inventory_add(&p->equip[sl]);
                p->equip[sl].kind = ITEM_NONE;
                rogue_player_recompute(p);
            }
        } else {
            int k = s_cur - 6;
            if (k < s_bag_n && rogue_item_is_equip(&s_bag[k])) {
                RogueItem in = s_bag[k];
                RogueItem old = p->equip[in.slot];
                bag_remove(k);
                rogue_player_equip(p, &in);
                if (rogue_item_is_equip(&old)) rogue_inventory_add(&old);
            } else if (k < s_bag_n && s_bag[k].kind == ITEM_POTION) {
                p->hp += s_bag[k].amount;
                if (p->hp > p->max_hp) p->hp = p->max_hp;
                bag_remove(k);
            } else if (k < s_bag_n && s_bag[k].kind == ITEM_GEM) {
                /* Socket the gem into the first equipped item with a free hole. */
                GemType g = (GemType)s_bag[k].amount;
                for (int sl = 0; sl < SLOT_COUNT; sl++) {
                    RogueItem *e = &p->equip[sl];
                    if (!rogue_item_is_equip(e)) continue;
                    bool done = false;
                    for (int gi = 0; gi < e->sockets && gi < 2; gi++) {
                        if (e->gem[gi] == GEM_NONE) { e->gem[gi] = (uint8_t)g; done = true; break; }
                    }
                    if (done) { bag_remove(k); rogue_player_recompute(p); break; }
                }
            }
        }
    }
    if (edge(btn->b, prev->b) && s_cur >= 6) {
        int k = s_cur - 6;
        if (k < s_bag_n) { p->gold += salvage_gold(&s_bag[k]); bag_remove(k); }
    }
}

/* ---- drawing ---- */
static void fr(uint16_t *fb, int x, int y, int w, int h, uint16_t c) {
    for (int j = y; j < y + h; j++) {
        if ((unsigned)j >= CRAFT_FB_H) continue;
        for (int i = x; i < x + w; i++)
            if ((unsigned)i < CRAFT_FB_W) fb[j * CRAFT_FB_W + i] = c;
    }
}
static void box(uint16_t *fb, int x, int y, int w, int h, uint16_t border, uint16_t fill) {
    fr(fb, x, y, w, h, border);
    fr(fb, x + 1, y + 1, w - 2, h - 2, fill);
}

static const char *slot_abbrev(EquipSlot s) {
    static const char *A[SLOT_COUNT] = { "Wp","Of","Hl","Ar","Am","Rg" };
    return A[s];
}

void rogue_inventory_draw(uint16_t *fb, const RoguePlayer *p) {
    fr(fb, 0, 0, CRAFT_FB_W, CRAFT_FB_H, RGB(12, 10, 18));
    char buf[40];
    craft_font_draw(fb, "INVENTORY", 3, 2, RGB(240, 230, 200));
    snprintf(buf, sizeof buf, "G %d", p->gold);
    craft_font_draw(fb, buf, CRAFT_FB_W - craft_font_width(buf) - 3, 2, RGB(240, 210, 60));

    /* paperdoll 2x3 */
    int px0 = 4, py0 = 12, bw = 18, bh = 14, gap = 2;
    for (int i = 0; i < 6; i++) {
        int col = i % 2, row = i / 2;
        int x = px0 + col * (bw + gap), y = py0 + row * (bh + gap);
        const RogueItem *it = &p->equip[PD[i]];
        bool sel = (s_cur == i);
        uint16_t bdr = sel ? RGB(255,255,255)
                     : rogue_item_is_equip(it) ? rogue_rarity_color(it->rarity) : RGB(70,70,80);
        if (sel) box(fb, x-1, y-1, bw+2, bh+2, RGB(240,210,60), RGB(60,52,18));  /* gold cursor frame */
        box(fb, x, y, bw, bh, bdr, sel ? RGB(48,42,24) : RGB(24,22,30));
        craft_font_draw(fb, slot_abbrev(PD[i]), x + 2, y + 1, RGB(150,150,160));
        if (rogue_item_is_equip(it))
            craft_font_draw(fb, "*", x + bw - 6, y + bh - 7, rogue_rarity_color(it->rarity));
    }

    /* stat column */
    int sx = px0 + 2 * (bw + gap) + 4, sy = 12;
    snprintf(buf, sizeof buf, "HP %d", p->max_hp);   craft_font_draw(fb, buf, sx, sy, RGB(80,220,90));
    snprintf(buf, sizeof buf, "ARM %d", p->stats.armor); craft_font_draw(fb, buf, sx, sy+8, RGB(170,170,200));
    snprintf(buf, sizeof buf, "DMG %d", p->wpn_dmg); craft_font_draw(fb, buf, sx, sy+16, RGB(230,120,80));
    snprintf(buf, sizeof buf, "CRT %d", p->stats.crit); craft_font_draw(fb, buf, sx, sy+24, RGB(240,220,80));
    snprintf(buf, sizeof buf, "RES %d", p->stats.resist); craft_font_draw(fb, buf, sx, sy+32, RGB(120,200,220));

    /* backpack 5xN */
    int gx0 = 4, gy0 = 58, cw = 16, ch = 13, gp = 2, cols = 5;
    for (int k = 0; k < ROGUE_BAG_N; k++) {
        int col = k % cols, row = k / cols;
        int x = gx0 + col * (cw + gp), y = gy0 + row * (ch + gp);
        bool has = k < s_bag_n;
        bool sel = (s_cur == 6 + k);
        uint16_t bdr = sel ? RGB(255,255,255)
                     : has ? (rogue_item_is_equip(&s_bag[k]) ? rogue_rarity_color(s_bag[k].rarity) : s_bag[k].color)
                           : RGB(50,48,58);
        if (sel) box(fb, x-1, y-1, cw+2, ch+2, RGB(240,210,60), RGB(60,52,18));  /* gold cursor frame */
        box(fb, x, y, cw, ch, bdr, sel ? RGB(48,42,24) : RGB(20,18,26));
        if (has) {
            uint16_t ic = rogue_item_is_equip(&s_bag[k]) ? rogue_rarity_color(s_bag[k].rarity) : s_bag[k].color;
            fr(fb, x + 4, y + 3, cw - 8, ch - 6, ic);
        }
    }

    /* detail / compare for the selected cell */
    const RogueItem *sel = NULL; EquipSlot ssl = SLOT_WEAPON;
    if (s_cur < 6) { sel = &p->equip[PD[s_cur]]; ssl = PD[s_cur]; }
    else if (s_cur - 6 < s_bag_n) { sel = &s_bag[s_cur - 6]; ssl = (EquipSlot)sel->slot; }

    int dy = CRAFT_FB_H - 18;
    fr(fb, 0, dy - 1, CRAFT_FB_W, 19, RGB(8, 7, 12));
    if (sel && sel->kind != ITEM_NONE) {
        uint16_t nc = rogue_item_is_equip(sel) ? rogue_rarity_color(sel->rarity) : sel->color;
        craft_font_draw(fb, sel->name, 3, dy, nc);
        /* legendary aspect tag on the name row */
        if (rogue_item_is_equip(sel) && sel->aspect) {
            const char *ad = rogue_aspect_desc((AspectId)sel->aspect);
            craft_font_draw(fb, ad, CRAFT_FB_W - craft_font_width(ad) - 3, dy, RGB(220,130,40));
        }
        if (rogue_item_is_equip(sel)) {
            /* compare vs currently equipped in that slot (for backpack items) */
            const RogueItem *eq = &p->equip[ssl];
            int sd = (sel->kind==ITEM_WEAPON? sel->base_dmg:0) + sel->armor;
            int ed = (eq->kind==ITEM_WEAPON? eq->base_dmg:0) + eq->armor;
            if (s_cur >= 6 && rogue_item_is_equip(eq)) {
                int d = sd - ed;
                snprintf(buf, sizeof buf, "%s%d vs %s  A:equip B:salv",
                         d>=0?"+":"", d, rogue_slot_name(ssl));
            } else if (s_cur >= 6) {
                snprintf(buf, sizeof buf, "%s  A:equip B:salvage", rogue_slot_name(ssl));
            } else {
                snprintf(buf, sizeof buf, "%s  A:unequip", rogue_slot_name(ssl));
            }
            craft_font_draw(fb, buf, 3, dy + 8, RGB(200,200,210));
        } else if (sel->kind == ITEM_POTION) {
            craft_font_draw(fb, "A:drink", 3, dy + 8, RGB(200,200,210));
        } else if (sel->kind == ITEM_GEM) {
            craft_font_draw(fb, "A:socket into gear  B:salvage", 3, dy + 8, RGB(200,200,210));
        }
    } else {
        craft_font_draw(fb, "MENU:close  dpad:move", 3, dy + 4, RGB(150,150,160));
    }
}

#include "rogue_shop.h"
#include "rogue_inventory.h"
#include "craft_world.h"
#include "craft_blocks.h"
#include "craft_font.h"
#include "rogue_level.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))
#define N_STOCK 4

static bool      s_has_pad, s_open;
static Vec3      s_pad;
static RogueItem s_stock[N_STOCK];
static int       s_price[N_STOCK];
static bool      s_sold[N_STOCK];
static int       s_depth;
static uint32_t  s_rng;
static int       s_cur;       /* 0..N_STOCK-1 items, then gamble/reroll/upgrade */

#define OPT_GAMBLE  (N_STOCK + 0)
#define OPT_REROLL  (N_STOCK + 1)
#define OPT_UPGRADE (N_STOCK + 2)
#define N_OPT       (N_STOCK + 3)

static uint32_t xs(void){ s_rng^=s_rng<<13; s_rng^=s_rng>>17; s_rng^=s_rng<<5; return s_rng; }

static int price_of(const RogueItem *it, int depth) {
    int base[RAR_COUNT] = { 15, 35, 75, 150 };
    return base[it->rarity] + depth * (8 + it->rarity * 6);
}

void rogue_shop_place(const int16_t *room_cx, const int16_t *room_cz,
                      int n_rooms, int up_x, int up_z, int down_x, int down_z,
                      int floor_y, int depth, uint32_t seed) {
    s_has_pad = false; s_open = false; s_cur = 0; s_depth = depth;
    s_rng = seed ^ 0x5409u ^ (uint32_t)(depth * 40503u);
    if (!s_rng) s_rng = 1;
    /* pick a room that isn't the stairs */
    for (int a = 0; a < n_rooms * 3; a++) {
        int r = (int)(xs() % (uint32_t)(n_rooms > 0 ? n_rooms : 1));
        if ((room_cx[r]==up_x && room_cz[r]==up_z) ||
            (room_cx[r]==down_x && room_cz[r]==down_z)) continue;
        s_pad = v3(room_cx[r] + 0.5f, (float)floor_y, room_cz[r] + 0.5f);
        craft_world_set_byte(room_cx[r], floor_y - 1, room_cz[r], BLK_GOLD_BLOCK);
        s_has_pad = true;
        break;
    }
    for (int i = 0; i < N_STOCK; i++) {
        rogue_item_roll_drop(&s_stock[i], depth + 1, xs());
        s_price[i] = price_of(&s_stock[i], depth);
        s_sold[i] = false;
    }
}

bool rogue_shop_pad_near(float x, float y, float z) {
    if (!s_has_pad) return false;
    float dx = x - s_pad.x, dz = z - s_pad.z;
    return (dx*dx + dz*dz < 0.8f*0.8f) && (fabsf(y - s_pad.y) < 1.2f);
}
bool rogue_shop_is_open(void){ return s_open; }
void rogue_shop_open(void){ s_open = true; s_cur = 0; }
void rogue_shop_close(void){ s_open = false; }

static bool edge(bool n, bool p){ return n && !p; }

void rogue_shop_input(RoguePlayer *p, const CraftRawButtons *btn,
                      const CraftRawButtons *prev) {
    if (edge(btn->left,prev->left)||edge(btn->up,prev->up))     s_cur--;
    if (edge(btn->right,prev->right)||edge(btn->down,prev->down)) s_cur++;
    if (s_cur < 0) s_cur = N_OPT - 1;
    if (s_cur >= N_OPT) s_cur = 0;

    if (!edge(btn->a, prev->a)) return;
    if (s_cur < N_STOCK) {
        if (!s_sold[s_cur] && p->gold >= s_price[s_cur] && !rogue_inventory_full()) {
            if (rogue_inventory_add(&s_stock[s_cur])) {
                p->gold -= s_price[s_cur]; s_sold[s_cur] = true;
            }
        }
    } else if (s_cur == OPT_GAMBLE) {
        int cost = 30 + s_depth * 8;
        if (p->gold >= cost && !rogue_inventory_full()) {
            p->gold -= cost;
            RogueItem it; rogue_item_roll_drop(&it, s_depth + 1, xs());
            rogue_inventory_add(&it);
        }
    } else if (s_cur == OPT_REROLL) {
        int cost = 25 + s_depth * 5;
        if (p->gold >= cost) {
            p->gold -= cost;
            /* reroll the equipped weapon's affixes at the same rarity */
            RogueItem nw; rogue_item_roll_weapon(&nw, s_depth + 1, xs());
            nw.rarity = p->equip[SLOT_WEAPON].rarity;  /* keep tier feel */
            p->equip[SLOT_WEAPON] = nw;
            rogue_player_recompute(p);
        }
    } else if (s_cur == OPT_UPGRADE) {
        int cost = 40 + s_depth * 8;
        if (p->gold >= cost) {
            p->gold -= cost;
            p->equip[SLOT_WEAPON].base_dmg += p->equip[SLOT_WEAPON].base_dmg / 8 + 2;
            rogue_player_recompute(p);
        }
    }
}

static void fr(uint16_t *fb,int x,int y,int w,int h,uint16_t c){
    for(int j=y;j<y+h;j++){ if((unsigned)j>=CRAFT_FB_H)continue;
        for(int i=x;i<x+w;i++) if((unsigned)i<CRAFT_FB_W) fb[j*CRAFT_FB_W+i]=c; }
}

/* Draw one selectable row with a clear highlight bar + cursor. */
static void shop_row(uint16_t *fb, int y, bool sel, bool dim,
                     const char *label, uint16_t lc, int price) {
    if (sel) {
        fr(fb, 0, y - 1, CRAFT_FB_W, 9, RGB(60, 52, 18));   /* highlight bar */
        fr(fb, 0, y - 1, 2, 9, RGB(240, 210, 60));          /* gold edge */
        craft_font_draw(fb, ">", 4, y, RGB(255, 255, 255));
    }
    uint16_t c = dim ? RGB(95, 90, 80) : (sel ? RGB(255, 255, 255) : lc);
    craft_font_draw(fb, label, 11, y, c);
    if (price >= 0) {
        char pb[12]; snprintf(pb, sizeof pb, "%d", price);
        uint16_t pc = dim ? RGB(120,70,60) : RGB(240, 210, 60);
        craft_font_draw(fb, pb, CRAFT_FB_W - craft_font_width(pb) - 4, y, pc);
    }
}

void rogue_shop_draw(uint16_t *fb, const RoguePlayer *p) {
    fr(fb,0,0,CRAFT_FB_W,CRAFT_FB_H,RGB(14,12,8));
    char buf[40];
    craft_font_draw(fb,"MERCHANT",4,2,RGB(240,220,120));
    snprintf(buf,sizeof buf,"G %d",p->gold);
    craft_font_draw(fb,buf,CRAFT_FB_W-craft_font_width(buf)-4,2,RGB(240,210,60));
    fr(fb,0,11,CRAFT_FB_W,1,RGB(60,52,30));

    int y = 16;
    for (int i = 0; i < N_STOCK; i++) {
        bool sel = (s_cur == i);
        if (s_sold[i]) {
            shop_row(fb, y, sel, true, "- sold -", RGB(90,90,90), -1);
        } else {
            bool dim = p->gold < s_price[i];
            shop_row(fb, y, sel, dim, s_stock[i].name,
                     rogue_rarity_color(s_stock[i].rarity), s_price[i]);
        }
        y += 10;
    }
    y += 4;
    fr(fb,0,y-3,CRAFT_FB_W,1,RGB(60,52,30));
    struct { int id; const char *t; int cost; } opt[3] = {
        { OPT_GAMBLE,  "Gamble random", 30 + s_depth*8 },
        { OPT_REROLL,  "Reroll weapon", 25 + s_depth*5 },
        { OPT_UPGRADE, "Upgrade weapon",40 + s_depth*8 },
    };
    for (int i = 0; i < 3; i++) {
        bool sel = (s_cur == opt[i].id);
        bool dim = p->gold < opt[i].cost;
        shop_row(fb, y, sel, dim, opt[i].t, RGB(200,200,210), opt[i].cost);
        y += 10;
    }

    /* Detail panel for the selected item — stats + affixes as you scroll. */
    int dy = CRAFT_FB_H - 28;
    fr(fb, 0, dy - 1, CRAFT_FB_W, 19, RGB(8, 7, 5));
    if (s_cur < N_STOCK && !s_sold[s_cur]) {
        const RogueItem *it = &s_stock[s_cur];
        if (it->kind == ITEM_WEAPON)
            snprintf(buf, sizeof buf, "DMG %d  %s", it->base_dmg, rogue_slot_name((EquipSlot)it->slot));
        else
            snprintf(buf, sizeof buf, "ARM %d  %s", it->armor, rogue_slot_name((EquipSlot)it->slot));
        craft_font_draw(fb, buf, 4, dy, rogue_rarity_color(it->rarity));
        char line[44]; line[0] = 0; int col = 4;
        for (int a = 0; a < it->n_affix; a++) {
            char ab[24]; rogue_affix_label(ab, sizeof ab, &it->affix[a]);
            craft_font_draw(fb, ab, col, dy + 8, RGB(150,200,150));
            col += craft_font_width(ab) + 6;
        }
        if (it->aspect) {
            const char *ad = rogue_aspect_desc((AspectId)it->aspect);
            craft_font_draw(fb, ad, CRAFT_FB_W - craft_font_width(ad) - 4, dy, RGB(220,130,40));
        }
    } else if (s_cur == OPT_GAMBLE) {
        craft_font_draw(fb, "buy a random item", 4, dy, RGB(180,180,190));
    } else if (s_cur == OPT_REROLL) {
        craft_font_draw(fb, "re-roll equipped weapon affixes", 4, dy, RGB(180,180,190));
    } else if (s_cur == OPT_UPGRADE) {
        craft_font_draw(fb, "raise equipped weapon damage", 4, dy, RGB(180,180,190));
    }
    craft_font_draw(fb,"A buy/use    MENU leave",4,CRAFT_FB_H-9,RGB(150,150,160));
}

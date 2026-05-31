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

void rogue_shop_draw(uint16_t *fb, const RoguePlayer *p) {
    fr(fb,0,0,CRAFT_FB_W,CRAFT_FB_H,RGB(14,12,8));
    char buf[40];
    craft_font_draw(fb,"MERCHANT",3,2,RGB(240,220,120));
    snprintf(buf,sizeof buf,"G %d",p->gold);
    craft_font_draw(fb,buf,CRAFT_FB_W-craft_font_width(buf)-3,2,RGB(240,210,60));

    int y = 14;
    for (int i = 0; i < N_STOCK; i++) {
        uint16_t c = (s_cur==i)?RGB(255,255,255):rogue_rarity_color(s_stock[i].rarity);
        if (s_sold[i]) { snprintf(buf,sizeof buf,"-- sold --"); c=RGB(90,90,90); }
        else snprintf(buf,sizeof buf,"%-16s %d", s_stock[i].name, s_price[i]);
        craft_font_draw(fb,buf,4,y,c);
        y += 9;
    }
    y += 3;
    struct { int id; const char *t; int cost; } opt[3] = {
        { OPT_GAMBLE,  "Gamble (random)", 30 + s_depth*8 },
        { OPT_REROLL,  "Reroll weapon",   25 + s_depth*5 },
        { OPT_UPGRADE, "Upgrade weapon",  40 + s_depth*8 },
    };
    for (int i = 0; i < 3; i++) {
        uint16_t c = (s_cur==opt[i].id)?RGB(255,255,255):RGB(200,200,210);
        snprintf(buf,sizeof buf,"%-16s %d", opt[i].t, opt[i].cost);
        craft_font_draw(fb,buf,4,y,c);
        y += 9;
    }
    craft_font_draw(fb,"A buy/use   MENU leave",4,CRAFT_FB_H-9,RGB(150,150,160));
}

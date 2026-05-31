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

int rogue_inventory_export(RogueItem *out, int max) {
    int n = s_bag_n < max ? s_bag_n : max;
    for (int i = 0; i < n; i++) out[i] = s_bag[i];
    return n;
}
void rogue_inventory_import(const RogueItem *in, int n) {
    if (n > ROGUE_BAG_N) n = ROGUE_BAG_N;
    for (int i = 0; i < n; i++) s_bag[i] = in[i];
    s_bag_n = n;
}

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
static void px(uint16_t *fb, int x, int y, uint16_t c) {
    if ((unsigned)x < CRAFT_FB_W && (unsigned)y < CRAFT_FB_H) fb[y * CRAFT_FB_W + x] = c;
}
static void hrun(uint16_t *fb, int x0, int x1, int y, uint16_t c) {
    for (int x = x0; x <= x1; x++) px(fb, x, y, c);
}

/* Tiny per-kind glyph drawn inside a backpack cell. (ox,oy) is the top-left
 * of a ~12x9 icon box; `tint` is the rarity/item colour used to tone equip
 * gear and consumables so rarity still reads at a glance. */
void rogue_item_draw_icon(uint16_t *fb, int ox, int oy, const RogueItem *it, uint16_t tint) {
    const uint16_t SIL = RGB(205,205,215);   /* steel  */
    const uint16_t WD  = RGB(150,110,60);    /* wood   */
    const uint16_t GLD = RGB(235,200,70);    /* gold   */
    const uint16_t DK  = RGB(28,26,34);      /* shadow */
    #define P(i,j,c) px(fb, ox+(i), oy+(j), (c))
    #define H(a,b,j,c) hrun(fb, ox+(a), ox+(b), oy+(j), (c))
    switch (it->kind) {
    case ITEM_WEAPON:
        switch (it->wtype) {
        case WT_DAGGER:                               /* short blade + small guard */
            for (int j = 2; j < 6; j++) P(5,j,SIL);
            H(4,6,6,GLD);
            P(5,7,WD); P(5,8,WD);
            break;
        case WT_SWORD:                                /* medium blade + crossguard */
            for (int j = 0; j < 6; j++) P(5,j,SIL);
            P(6,1,SIL);
            H(3,7,6,WD); P(5,7,WD); P(5,8,WD);
            break;
        case WT_GREATSWORD:                           /* long 2-wide blade, gold guard */
            for (int j = 0; j < 6; j++) { P(5,j,SIL); P(6,j,SIL); }
            H(3,8,6,GLD);
            P(5,7,WD); P(6,7,WD); P(5,8,GLD); P(6,8,GLD);
            break;
        case WT_AXE:                                  /* haft + tapered beard-axe bit */
            for (int j = 0; j < 9; j++) P(4,j,WD);
            H(5,6,1,SIL); H(5,8,2,SIL); H(5,9,3,SIL); H(5,9,4,SIL);
            H(6,8,5,SIL); P(7,6,SIL);                 /* beard tapers to a point */
            break;
        case WT_MACE:                                 /* haft + round spiked ball */
            for (int j = 4; j < 9; j++) P(5,j,WD);
            H(4,6,0,SIL); H(3,7,1,SIL); H(3,7,2,SIL); H(4,6,3,SIL);  /* ball */
            P(2,1,SIL); P(8,1,SIL);                   /* side spikes */
            break;
        case WT_SPEAR:                                /* long shaft + leaf tip */
            for (int j = 3; j < 9; j++) P(5,j,WD);
            P(5,0,SIL); H(4,6,1,SIL); P(5,2,SIL);
            break;
        case WT_WARHAMMER:                            /* haft + big block head */
            for (int j = 3; j < 9; j++) P(5,j,WD);
            H(3,7,0,SIL); H(2,8,1,SIL); H(3,7,2,SIL);
            break;
        case WT_BOW:                                  /* bow with the arrow drawn THROUGH it */
            P(7,0,WD); P(8,1,WD); P(9,2,WD);          /* upper limb, belly faces right (target) */
            P(9,3,WD); P(9,4,WD); P(9,5,WD); P(9,6,WD);
            P(8,7,WD); P(7,8,WD);                     /* lower limb */
            for (int j = 1; j < 8; j++) P(7,j,SIL);   /* string chord at the tips */
            H(2,10,4,WD);                             /* arrow shaft, pointing right */
            P(2,3,WD); P(2,5,WD);                     /* fletching at the nock (rear) */
            P(10,3,SIL); P(10,5,SIL); P(11,4,SIL);    /* arrowhead, out past the bow */
            break;
        case WT_CROSSBOW:                             /* horizontal limbs + stock + bolt */
            H(1,9,3,WD); P(1,2,WD); P(9,2,WD);        /* bow arms + tips */
            for (int j = 3; j < 8; j++) P(5,j,WD);    /* stock */
            P(5,0,SIL); P(5,1,SIL); P(5,2,SIL);       /* loaded bolt */
            H(3,7,4,SIL);                             /* rail */
            break;
        case WT_WAND:                                 /* short rod + spark */
            for (int i = 0; i < 5; i++) P(3+i, 8-i, WD);
            P(8,3,tint); P(9,2,tint); P(8,2,RGB(255,255,255));
            break;
        case WT_SCEPTER:                              /* ornate gold rod + gem head */
            for (int j = 3; j < 9; j++) P(5,j,GLD);
            P(5,0,tint); H(4,6,1,tint); P(5,2,GLD);
            P(3,1,GLD); P(7,1,GLD);                   /* ornate arms */
            break;
        case WT_STAFF:                                /* diagonal shaft + orb */
        default:
            for (int i = 0; i < 6; i++) P(2+i, 8-i, WD);
            P(8,1,tint); P(9,1,tint); P(8,2,tint); P(9,2,tint);
            P(7,0,RGB(255,255,255));
            break;
        }
        break;
    case ITEM_GEAR:
        if (it->slot == SLOT_OFFHAND) {               /* shield */
            H(3,8,0,SIL); H(2,9,1,tint); H(2,9,2,tint); H(2,9,3,tint);
            H(3,8,4,tint); H(3,8,5,tint); H(4,7,6,tint); H(5,6,7,tint);
            P(5,2,DK); P(6,2,DK); P(5,3,DK); P(6,3,DK);   /* boss */
        } else if (it->slot == SLOT_HELM) {           /* helm */
            H(4,7,0,SIL); H(3,8,1,SIL); H(2,9,2,SIL);
            H(2,9,3,SIL); P(4,3,DK); P(5,3,DK); P(6,3,DK); P(7,3,DK); /* visor */
            H(1,10,4,SIL);                            /* brim */
        } else if (it->slot == SLOT_AMULET) {         /* amulet */
            P(3,0,GLD); P(4,1,GLD); P(5,2,GLD);       /* chain */
            P(8,0,GLD); P(7,1,GLD); P(6,2,GLD);
            P(6,3,tint); P(5,4,tint); P(6,4,tint); P(7,4,tint); P(6,5,tint);
        } else if (it->slot == SLOT_RING) {           /* ring + gem */
            P(5,2,tint); P(6,2,tint);                 /* gem */
            P(4,3,GLD); P(7,3,GLD); P(4,4,GLD); P(7,4,GLD);
            P(5,5,GLD); P(6,5,GLD);                   /* band */
        } else {                                      /* armour / chestplate */
            H(2,3,0,SIL); H(8,9,0,SIL);               /* pauldrons */
            H(1,10,1,SIL); H(2,9,2,tint); H(2,9,3,tint);
            H(3,8,4,tint); H(4,7,5,tint); H(5,6,6,tint);
            P(5,2,DK); P(6,2,DK);                     /* neckline */
        }
        break;
    case ITEM_GEM: {
        uint16_t g = rogue_gem_color((GemType)(it->amount % GEM_COUNT));
        H(5,6,0,g); H(4,7,1,g); H(3,8,2,g); H(4,7,3,g); H(5,6,4,g);
        P(4,1,RGB(255,255,255));                      /* facet glint */
        break; }
    case ITEM_POTION:
        P(5,0,WD); P(6,0,WD);                         /* cork */
        P(5,1,RGB(180,200,210)); P(6,1,RGB(180,200,210));
        H(4,7,2,RGB(180,200,210));
        H(3,8,3,tint); H(3,8,4,tint); H(3,8,5,tint); H(4,7,6,tint);
        P(4,3,RGB(255,255,255));                      /* glass highlight */
        break;
    case ITEM_TORCH:
        for (int j = 4; j <= 8; j++) P(5,j,WD);       /* handle */
        P(5,0,RGB(255,235,120));                      /* flame */
        H(4,6,1,RGB(255,180,40)); H(4,6,2,RGB(255,140,30)); P(5,3,RGB(255,120,20));
        break;
    case ITEM_GOLD:
        H(4,7,1,GLD); H(3,8,2,GLD); H(3,8,3,GLD); H(3,8,4,GLD); H(4,7,5,GLD);
        P(4,2,RGB(255,245,180));                      /* shine */
        break;
    default:
        fr(fb, ox + 2, oy + 1, 8, 7, tint);
        break;
    }
    #undef P
    #undef H
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
            uint16_t tint = rogue_item_is_equip(&s_bag[k]) ? rogue_rarity_color(s_bag[k].rarity) : s_bag[k].color;
            rogue_item_draw_icon(fb, x + 2, y + 2, &s_bag[k], tint);
        }
    }

    /* detail / compare for the selected cell */
    const RogueItem *sel = NULL; EquipSlot ssl = SLOT_WEAPON;
    if (s_cur < 6) { sel = &p->equip[PD[s_cur]]; ssl = PD[s_cur]; }
    else if (s_cur - 6 < s_bag_n) { sel = &s_bag[s_cur - 6]; ssl = (EquipSlot)sel->slot; }

    /* Three-line detail panel: name(+aspect), what-it-does (stats+affixes),
     * then the action/compare line. */
    int dy = CRAFT_FB_H - 22;
    fr(fb, 0, dy - 1, CRAFT_FB_W, 23, RGB(8, 7, 12));
    if (sel && sel->kind != ITEM_NONE) {
        uint16_t nc = rogue_item_is_equip(sel) ? rogue_rarity_color(sel->rarity) : sel->color;
        craft_font_draw(fb, sel->name, 3, dy, nc);
        if (rogue_item_is_equip(sel) && sel->aspect) {
            const char *ad = rogue_aspect_desc((AspectId)sel->aspect);
            craft_font_draw(fb, ad, CRAFT_FB_W - craft_font_width(ad) - 3, dy, RGB(220,130,40));
        }

        /* line 2 — what the item does */
        char info[48]; int n = 0; info[0] = 0;
        if (rogue_item_is_equip(sel)) {
            if (sel->kind == ITEM_WEAPON)
                n += snprintf(info + n, sizeof info - n, "DMG%d ", sel->base_dmg);
            if (sel->armor > 0)
                n += snprintf(info + n, sizeof info - n, "ARM%d ", sel->armor);
            for (int a = 0; a < sel->n_affix && n < (int)sizeof info - 12; a++) {
                char ab[20]; rogue_affix_label(ab, sizeof ab, &sel->affix[a]);
                n += snprintf(info + n, sizeof info - n, "%s ", ab);
            }
            if (sel->sockets)
                n += snprintf(info + n, sizeof info - n, "[%d sock]", sel->sockets);
        } else if (sel->kind == ITEM_POTION) {
            snprintf(info, sizeof info, "Restores %d health", sel->amount);
        } else if (sel->kind == ITEM_GEM) {
            static const char *gd[GEM_COUNT] = { "", "+20 Life", "+8% Resist", "+4% Crit", "+10 Armor" };
            snprintf(info, sizeof info, "Gem: %s (socket into gear)", gd[sel->amount % GEM_COUNT]);
        } else if (sel->kind == ITEM_TORCH) {
            snprintf(info, sizeof info, "Relights torch +%ds", sel->amount);
        }
        craft_font_draw(fb, info, 3, dy + 8, RGB(150, 210, 150));

        /* line 3 — action + compare */
        if (rogue_item_is_equip(sel)) {
            const RogueItem *eq = &p->equip[ssl];
            int sd = (sel->kind==ITEM_WEAPON? sel->base_dmg:0) + sel->armor;
            int ed = (eq->kind==ITEM_WEAPON? eq->base_dmg:0) + eq->armor;
            if (s_cur >= 6 && rogue_item_is_equip(eq)) {
                int d = sd - ed;
                snprintf(buf, sizeof buf, "A equip (%s%d %s)  B salvage",
                         d>=0?"+":"", d, rogue_slot_name(ssl));
            } else if (s_cur >= 6) {
                snprintf(buf, sizeof buf, "A equip %s  B salvage", rogue_slot_name(ssl));
            } else {
                snprintf(buf, sizeof buf, "A unequip to bag");
            }
            craft_font_draw(fb, buf, 3, dy + 16, RGB(200,200,210));
        } else if (sel->kind == ITEM_POTION) {
            craft_font_draw(fb, "A drink", 3, dy + 16, RGB(200,200,210));
        } else if (sel->kind == ITEM_GEM) {
            craft_font_draw(fb, "A socket   B salvage", 3, dy + 16, RGB(200,200,210));
        }
    } else {
        craft_font_draw(fb, "MENU close   dpad move", 3, dy + 8, RGB(150,150,160));
    }
}

#include "rogue_items.h"
#include <stdio.h>
#include <string.h>

#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))

/* --- weapon bases ------------------------------------------------ */
typedef struct {
    const char *name;
    WeaponClass wclass;
    int   dmg;
    float range, arc_cos, cooldown, proj_speed;
    uint16_t color;
} WeaponBase;

static const WeaponBase BASES[] = {
    /* name      class          dmg range arc_cos cd    proj  color */
    { "Dagger",  WCLASS_MELEE,  16, 1.4f, 0.55f, 0.22f, 0,    RGB(200,200,210) },
    { "Sword",   WCLASS_MELEE,  26, 1.8f, 0.40f, 0.32f, 0,    RGB(210,215,225) },
    { "Axe",     WCLASS_MELEE,  38, 1.9f, 0.15f, 0.46f, 0,    RGB(180,150,90)  },
    { "Mace",    WCLASS_MELEE,  32, 1.7f, 0.35f, 0.42f, 0,    RGB(150,150,160) },
    { "Bow",     WCLASS_RANGED, 22, 14.0f,0.0f,  0.40f, 26.0f,RGB(150,110,60)  },
    { "Staff",   WCLASS_CASTER, 30, 13.0f,0.0f,  0.55f, 20.0f,RGB(120,90,200)  },
    { "Wand",    WCLASS_CASTER, 18, 12.0f,0.0f,  0.30f, 24.0f,RGB(90,200,200)  },
};
#define N_BASES ((int)(sizeof(BASES)/sizeof(BASES[0])))

static const char *PREFIX[] = { "Sharp", "Heavy", "Cruel", "Fine", "Brutal", "Ancient" };
static const char *SUFFIX[] = { "of Haste", "of Vigor", "of Wounding", "of the Bear", "of Fury" };
#define N_PREFIX ((int)(sizeof(PREFIX)/sizeof(PREFIX[0])))
#define N_SUFFIX ((int)(sizeof(SUFFIX)/sizeof(SUFFIX[0])))

static uint32_t xs(uint32_t *s){ *s^=*s<<13; *s^=*s>>17; *s^=*s<<5; return *s; }

uint16_t rogue_rarity_color(Rarity r) {
    switch (r) {
        case RAR_MAGIC:  return RGB(90, 140, 255);
        case RAR_RARE:   return RGB(240, 220, 70);
        case RAR_UNIQUE: return RGB(220, 130, 40);
        default:         return RGB(220, 220, 220);
    }
}

void rogue_item_make_gold(RogueItem *it, int amount) {
    memset(it, 0, sizeof *it);
    it->kind = ITEM_GOLD;
    it->amount = amount;
    it->color = RGB(240, 210, 60);
    snprintf(it->name, sizeof it->name, "%d Gold", amount);
}

void rogue_item_make_potion(RogueItem *it, int heal) {
    memset(it, 0, sizeof *it);
    it->kind = ITEM_POTION;
    it->amount = heal;
    it->color = RGB(230, 60, 90);
    snprintf(it->name, sizeof it->name, "Potion (+%d)", heal);
}

void rogue_item_make_torch(RogueItem *it, int seconds) {
    memset(it, 0, sizeof *it);
    it->kind = ITEM_TORCH;
    it->amount = seconds;
    it->color = RGB(255, 170, 40);
    snprintf(it->name, sizeof it->name, "Torch");
}

void rogue_item_starter(RogueItem *it) {
    memset(it, 0, sizeof *it);
    const WeaponBase *b = &BASES[0];   /* Dagger */
    it->kind = ITEM_WEAPON;
    it->wclass = b->wclass;
    it->rarity = RAR_COMMON;
    it->dmg = b->dmg;
    it->range = b->range; it->arc_cos = b->arc_cos;
    it->cooldown = b->cooldown; it->proj_speed = b->proj_speed;
    it->color = b->color;
    snprintf(it->name, sizeof it->name, "%s", b->name);
}

void rogue_item_roll_weapon(RogueItem *it, int depth, uint32_t seed) {
    memset(it, 0, sizeof *it);
    uint32_t s = seed ? seed : 1;
    const WeaponBase *b = &BASES[xs(&s) % N_BASES];

    /* Rarity weighted by depth. */
    int roll = xs(&s) % 100 + depth * 2;
    Rarity rar = RAR_COMMON;
    if (roll > 96) rar = RAR_UNIQUE;
    else if (roll > 82) rar = RAR_RARE;
    else if (roll > 55) rar = RAR_MAGIC;

    float rscale = 1.0f + 0.25f * (int)rar + 0.06f * depth;
    it->kind = ITEM_WEAPON;
    it->wclass = b->wclass;
    it->rarity = rar;
    it->dmg = (int)(b->dmg * rscale);
    it->range = b->range;
    it->arc_cos = b->arc_cos;
    it->cooldown = b->cooldown;
    it->proj_speed = b->proj_speed;
    it->color = b->color;
    it->bonus_life = 0;

    /* Affixes: magic = 1, rare/unique = 2. */
    const char *pfx = NULL, *sfx = NULL;
    int n_affix = (rar == RAR_MAGIC) ? 1 : (rar >= RAR_RARE ? 2 : 0);
    for (int i = 0; i < n_affix; i++) {
        bool prefix = (i == 0) ? (xs(&s) & 1) : (pfx == NULL);
        if (prefix && !pfx) {
            int p = xs(&s) % N_PREFIX; pfx = PREFIX[p];
            if (p == 1) { it->dmg += it->dmg / 4; it->cooldown *= 1.12f; }  /* Heavy */
            else        { it->dmg += it->dmg / 6; }                          /* +dmg */
        } else if (!sfx) {
            int q = xs(&s) % N_SUFFIX; sfx = SUFFIX[q];
            switch (q) {
                case 0: it->cooldown *= 0.78f; break;          /* Haste */
                case 1: it->bonus_life += 15 + depth * 3; break;/* Vigor */
                case 2: it->dmg += it->dmg / 5; break;          /* Wounding */
                case 3: it->bonus_life += 25 + depth * 4; it->cooldown *= 1.08f; break;
                case 4: it->dmg += it->dmg / 3; it->cooldown *= 1.15f; break; /* Fury */
            }
        }
    }

    /* Name: [prefix] base [suffix]. */
    char tmp[30];
    if (pfx && sfx) snprintf(tmp, sizeof tmp, "%s %s %s", pfx, b->name, sfx);
    else if (pfx)   snprintf(tmp, sizeof tmp, "%s %s", pfx, b->name);
    else if (sfx)   snprintf(tmp, sizeof tmp, "%s %s", b->name, sfx);
    else            snprintf(tmp, sizeof tmp, "%s", b->name);
    memcpy(it->name, tmp, sizeof it->name);
    it->name[sizeof it->name - 1] = 0;
}

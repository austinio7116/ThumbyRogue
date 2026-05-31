#ifndef ROGUE_ITEMS_H
#define ROGUE_ITEMS_H
/*
 * ThumbyRogue item instances. The hero has no class — the equipped WEAPON
 * defines the playstyle:
 *   MELEE  (dagger/sword/axe/mace) — A swings an arc
 *   RANGED (bow)                   — A fires an arrow
 *   CASTER (staff/wand)            — A fires a magic bolt
 * Weapons roll a rarity + up to two Diablo-style affixes (prefix/suffix)
 * whose magnitudes scale with depth.
 */
#include <stdint.h>
#include <stdbool.h>

typedef enum { WCLASS_MELEE, WCLASS_RANGED, WCLASS_CASTER } WeaponClass;
typedef enum { ITEM_NONE, ITEM_WEAPON, ITEM_GOLD, ITEM_POTION, ITEM_TORCH } ItemKind;
typedef enum { RAR_COMMON, RAR_MAGIC, RAR_RARE, RAR_UNIQUE, RAR_COUNT } Rarity;

typedef struct {
    ItemKind    kind;
    /* weapon */
    WeaponClass wclass;
    Rarity      rarity;
    int         dmg;
    float       range;       /* melee arc reach / projectile range */
    float       arc_cos;     /* melee half-arc (cos); ignored for projectiles */
    float       cooldown;    /* seconds between attacks */
    float       proj_speed;  /* ranged/caster */
    int         bonus_life;  /* +max HP while equipped (affix) */
    uint16_t    color;       /* ground render + rarity tint */
    char        name[30];
    /* gold / potion */
    int         amount;
} RogueItem;

uint16_t rogue_rarity_color(Rarity r);

void rogue_item_make_gold(RogueItem *it, int amount);
void rogue_item_make_potion(RogueItem *it, int heal);
void rogue_item_make_torch(RogueItem *it, int seconds);

/* Roll a random weapon for the given depth using `seed` (caller varies it). */
void rogue_item_roll_weapon(RogueItem *it, int depth, uint32_t seed);

/* A sensible starting weapon (common dagger). */
void rogue_item_starter(RogueItem *it);

#endif /* ROGUE_ITEMS_H */

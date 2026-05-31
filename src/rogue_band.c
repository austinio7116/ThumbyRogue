#include "rogue_band.h"
#include "rogue_enemy.h"
#include "craft_blocks.h"

#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))

/* Authored bands; the sequence loops after the last (depth keeps scaling).
 * Each band draws enemies from its own roster, so the descent feels varied. */
static const RogueBand BANDS[] = {
    { "THE CRYPT",   BLK_RFLOOR,   BLK_COBBLE,    BLK_PLANK,
      { EN_SKELETON, EN_ARCHER, EN_RAT, EN_ZOMBIE }, 4, RGB(180,180,190) },
    { "THE CAVERNS", BLK_GRAVEL,   BLK_STONE,     BLK_OBSIDIAN,
      { EN_BAT, EN_KOBOLD, EN_SPIDER, EN_SLIME }, 4, RGB(150,140,120) },
    { "FUNGAL DEEP", BLK_DIRT,     BLK_COBBLE,    BLK_SLIME_BLOCK,
      { EN_SLIME, EN_KOBOLD, EN_FIRESPRITE, EN_ZOMBIE, EN_SPIDER }, 5, RGB(110,200,110) },
    { "FROSTVAULT",  BLK_SNOW,     BLK_ICE,       BLK_SNOWY_ROCK,
      { EN_SKELETON, EN_ARCHER, EN_ZOMBIE, EN_BAT }, 4, RGB(170,210,240) },
    { "THE INFERNO", BLK_OBSIDIAN, BLK_COBBLE,    BLK_REDSTONE_BLOCK,
      { EN_DEMON, EN_FIRESPRITE, EN_GOBLIN, EN_ARCHER }, 4, RGB(240,120,60) },
};
#define N_BANDS ((int)(sizeof(BANDS)/sizeof(BANDS[0])))

int rogue_band_count(void) { return N_BANDS; }

const RogueBand *rogue_band_get(int depth) {
    if (depth < 1) depth = 1;
    int b = (depth - 1) / ROGUE_BAND_FLOORS;
    return &BANDS[b % N_BANDS];
}

int rogue_band_is_boss_floor(int depth) {
    return (depth % ROGUE_BAND_FLOORS) == 0;
}

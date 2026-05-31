#include "rogue_band.h"
#include "craft_blocks.h"

#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))

/* Authored bands; the sequence loops after the last (depth keeps scaling). */
static const RogueBand BANDS[] = {
    /* name        floor          wall          pillar         rat sl sk sp  tint */
    { "THE CRYPT",  BLK_RFLOOR,    BLK_COBBLE,   BLK_PLANK,     35,25,30,10, RGB(180,180,190) },
    { "THE CAVERNS",BLK_GRAVEL,   BLK_STONE,    BLK_OBSIDIAN,  20,35,15,30, RGB(150,140,120) },
    { "FUNGAL DEEP",BLK_DIRT,     BLK_COBBLE,   BLK_SLIME_BLOCK,15,45,10,30, RGB(110,200,110) },
    { "FROSTVAULT", BLK_SNOW,     BLK_ICE,      BLK_SNOWY_ROCK,25,20,40,15, RGB(170,210,240) },
    { "THE INFERNO",BLK_OBSIDIAN, BLK_COBBLE,   BLK_REDSTONE_BLOCK,10,20,40,30, RGB(240,120,60) },
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

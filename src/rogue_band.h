#ifndef ROGUE_BAND_H
#define ROGUE_BAND_H
/*
 * ThumbyRogue depth bands. Every ROGUE_BAND_FLOORS floors the dungeon
 * changes theme: floor/wall/pillar blocks, enemy roster weights, and a name
 * shown on entry. After the last authored band the sequence loops — enemy
 * stats keep scaling with depth, so the descent is endlessly escalating.
 */
#include <stdint.h>

#define ROGUE_BAND_FLOORS 4

#define ROGUE_BAND_ROSTER 5

typedef struct {
    const char *name;
    uint8_t floor, wall, pillar;        /* block ids for the reskin */
    uint8_t roster[ROGUE_BAND_ROSTER];  /* EnemyType ids this band spawns */
    uint8_t roster_n;
    uint16_t tint;                      /* banner colour */
} RogueBand;

/* Band for a given depth (1-based). Loops after the last authored band. */
const RogueBand *rogue_band_get(int depth);
int  rogue_band_count(void);
/* True on the last floor of a band (boss/champion floor). */
int  rogue_band_is_boss_floor(int depth);

#endif /* ROGUE_BAND_H */

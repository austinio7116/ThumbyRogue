#ifndef ROGUE_ENEMY_H
#define ROGUE_ENEMY_H
/*
 * ThumbyRogue enemies — a fixed pool of cuboid creatures with simple but
 * readable AI: wander → chase → telegraphed wind-up → strike. Models are
 * adapted from ThumbyCraft's mob cuboids (the "start with the enemies we
 * already have" roster: rat, slime, skeleton, spider). Stats scale with
 * depth. Combat is fair: every hit is preceded by a visible wind-up.
 */
#include <stdint.h>
#include <stdbool.h>
#include "craft_types.h"
#include "craft_render.h"
#include "rogue_player.h"

#define ROGUE_MAX_ENEMIES 20

typedef enum {
    EN_RAT, EN_SLIME, EN_SKELETON, EN_SPIDER,
    EN_BAT, EN_KOBOLD, EN_GOBLIN, EN_ZOMBIE,
    EN_ARCHER, EN_FIRESPRITE, EN_DEMON,
    EN_TYPE_COUNT
} EnemyType;

/* What a slain enemy tends to drop (the game weights its loot roll by this). */
typedef enum { LOOT_GOLD, LOOT_GEAR, LOOT_GEM, LOOT_POTION, LOOT_RARE } EnemyLoot;
EnemyLoot rogue_enemy_loot(int type);
const char *rogue_enemy_name(int type);

void rogue_enemies_clear(void);
void rogue_enemies_set_dark(bool dark);   /* torch-out danger modifier */

/* Populate the level with depth-scaled enemies, avoiding the up-stairs
 * room. `rooms` are candidate room centres; `n` how many. */
void rogue_enemies_spawn(const int16_t *room_cx, const int16_t *room_cz,
                         int n_rooms, int up_x, int up_z,
                         int floor_y, int depth, uint32_t seed);

/* Advance AI + movement; apply telegraphed melee hits to the player. */
void rogue_enemies_update(RoguePlayer *p, float dt, int floor_y);

/* Player melee: damage + knock every live enemy inside the facing arc.
 * Returns the number hit (for SFX/juice). */
int rogue_enemies_hit_arc(Vec3 origin, float yaw, float range,
                          float arc_cos, int dmg);

/* Point/projectile hit: damage the first live enemy within `radius` of
 * (x,z). Returns 1 if it hit something. */
int rogue_enemies_hit_point(float x, float z, float radius, int dmg);
/* AoE: damage ALL live enemies within `radius` (cleave / chain). */
int rogue_enemies_hit_radius(float x, float z, float radius, int dmg);

/* Drain one enemy death event (position + EnemyType). False when empty.
 * The game uses this to drop loot. */
bool rogue_enemies_pop_death(Vec3 *pos, int *type);

void rogue_enemies_draw(const CraftCamera *cam, uint16_t *fb);

int rogue_enemies_alive_count(void);

/* Nearest live enemy to (x,z); false if none. (Debug/autopilot helper.) */
bool rogue_enemies_nearest(float x, float z, float *ex, float *ez);

#endif /* ROGUE_ENEMY_H */

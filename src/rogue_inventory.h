#ifndef ROGUE_INVENTORY_H
#define ROGUE_INVENTORY_H
/*
 * ThumbyRogue backpack + paperdoll inventory screen. Loot flows into the
 * backpack; MENU opens this screen to compare, equip (swap), unequip, and
 * salvage gear. Operates directly on the player's equip[] + gold.
 */
#include <stdint.h>
#include <stdbool.h>
#include "rogue_player.h"
#include "craft_buttons.h"

#define ROGUE_BAG_N 15

void rogue_inventory_clear(void);
bool rogue_inventory_add(const RogueItem *it);   /* false if backpack full */
bool rogue_inventory_full(void);
int  rogue_inventory_count(void);

bool rogue_inventory_is_open(void);
void rogue_inventory_open(void);
void rogue_inventory_close(void);

/* Process one frame of inventory input (edges vs prev). */
void rogue_inventory_input(RoguePlayer *p, const CraftRawButtons *btn,
                           const CraftRawButtons *prev);
void rogue_inventory_draw(uint16_t *fb, const RoguePlayer *p);

#endif /* ROGUE_INVENTORY_H */

#ifndef H_DROPS
#define H_DROPS

#include <stdint.h>

#include "entity.h"

// Items on the ground, as in vanilla: what mobs drop, mined blocks, what a
// player throws and what it carried when it died. They fall and slide,
// merge with the same item nearby, are picked up by walking over them and
// vanish after five minutes.

#define DROP_PICKUP_DELAY 10        // ticks before a fresh drop can be picked up
#define DROP_THROWN_DELAY 40        // when a player throws it
#define DROP_LIFETIME 6000          // ticks before it vanishes
#define DROP_MERGE_RANGE 0.5        // blocks

Entity *drop_item (uint16_t item, uint8_t count, int32_t x, int32_t y, int32_t z,
                   float vx, float vy, float vz, uint16_t pickup_delay);
// A drop popping out of a block or a mob: a little random speed, as vanilla
Entity *drop_pop (uint16_t item, uint8_t count, double x, double y, double z);
void drops_tick (void);                          // physics, merge, pickup, despawn
void drop_loot (const Entity *mob, int32_t killer_id);  // the type's vanilla loot
void drops_spill_inventory (PlayerData *p);      // death: everything it carried
void drops_throw (PlayerData *p, uint8_t whole_stack);  // Q / Ctrl+Q from the hand
void drops_send_meta (const Entity *e, int client_fd);  // which item it is, to a new tracker

#endif

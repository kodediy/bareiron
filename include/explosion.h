#ifndef H_EXPLOSION
#define H_EXPLOSION

#include <stdint.h>

#include "entity.h"

// Explosions as in vanilla: 16 x 16 x 16 rays from the centre lose
// strength through the blocks they cross (their blast resistance) and break
// what they still have strength for; what breaks drops as an item 1 time in
// `power`; entities nearby are hurt and pushed by how close and how exposed
// they are. Creepers and TNT use it.

#define TNT_FUSE 80
#define TNT_POWER 4.0f
#define CREEPER_POWER 3.0f

// (x, y, z) in ENT_UNIT; source_id: who caused it (a creeper, a player who lit TNT), or -1
void   explode (int32_t x, int32_t y, int32_t z, float power, int32_t source_id);
float  block_blast_resistance (uint8_t block);

// Primed TNT
Entity *tnt_prime (int32_t x, int32_t y, int32_t z, uint16_t fuse, int32_t lit_by);
void    tnt_tick (void);
void    tnt_send_extra (const Entity *e, int client_fd);

#endif

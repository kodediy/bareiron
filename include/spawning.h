#ifndef H_SPAWNING
#define H_SPAWNING

#include <stdint.h>

#include "mobs.h"

// Where and when mobs appear, the way vanilla does it, simplified: monsters
// in the dark around each player, up to a cap per player and a ceiling for
// the whole world; animals in the plains when chunks are first sent and now
// and then after. Light is approximated (design §9): the sky by the time of
// day, torches by distance.

#define CAP_MONSTER 24          // per player, counting mobs within CAP_RANGE of it
#define CAP_CREATURE 12
#define CAP_AMBIENT 4
#define CAP_WATER 6
#define CAP_MONSTER_GLOBAL 64
#define CAP_RANGE 128
#define SPAWN_MIN_DISTANCE 24   // never closer than this to a player
#define TORCH_LIGHT 14          // block light 0 needs this many blocks (taxicab) from a torch

int spawn_sky_above (short x, uint8_t y, short z);  // nothing solid above, block changes included
int spawn_is_day (void);                             // world_time < 13000 or > 23460
int spawn_is_dark (short x, uint8_t y, short z);     // no torch near, and covered or night

// The type a spawn of this category picks in this biome (vanilla's weights)
uint16_t spawn_pick_type (MobCategory category, uint8_t biome);

uint8_t spawn_biome_at (short x, short z);
void spawn_tick (void);                              // monster attempts around each player
void spawn_chunk_creatures (short cx, short cz);     // a chunk sent for the first time in a while
uint8_t spawn_creature_room (void);                  // animals never despawn: they may take half the entity table

#endif

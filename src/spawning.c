#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "globals.h"
#include "tools.h"
#include "registries.h"
#include "worldgen.h"
#include "procedures.h"
#include "blockindex.h"
#include "entity.h"
#include "mobs.h"
#include "pathfind.h"
#include "spawning.h"

#define CREATURE_CHUNK_CHANCE 10    // 1 in 10 chunks gets a group when first sent (vanilla: 0.1)
#define CREATURE_TICK_CHANCE 400    // and a top-up group 1 tick in 400
#define GROUP_SIZE 4
#define GROUP_SPREAD 5
#define SPAWN_EVERY 4               // a player's monster attempt every this many ticks

typedef struct { uint16_t type; uint16_t weight; } Weight;

// Vanilla's overworld monster weights; desert and snow swap in husks and strays
static const Weight monsters_plains[] = {
  { E_zombie, 95 }, { E_zombie_villager, 5 }, { E_skeleton, 100 }, { E_spider, 100 }, { E_creeper, 100 }, { 0, 0 }
};
static const Weight monsters_desert[] = {
  { E_zombie, 19 }, { E_zombie_villager, 1 }, { E_husk, 80 }, { E_skeleton, 100 }, { E_spider, 100 }, { E_creeper, 100 }, { 0, 0 }
};
static const Weight monsters_snow[] = {
  { E_zombie, 95 }, { E_zombie_villager, 5 }, { E_skeleton, 20 }, { E_stray, 80 }, { E_spider, 100 }, { E_creeper, 100 }, { 0, 0 }
};
static const Weight creatures_plains[] = {
  { E_sheep, 12 }, { E_pig, 10 }, { E_chicken, 10 }, { E_cow, 8 }, { 0, 0 }
};

static const Weight *weightsFor (MobCategory category, uint8_t biome) {
  if (category == CAT_CREATURE) return biome == W_plains ? creatures_plains : NULL;
  if (category != CAT_MONSTER) return NULL;
  if (biome == W_desert) return monsters_desert;
  if (biome == W_snowy_plains) return monsters_snow;
  return monsters_plains;
}

uint16_t spawn_pick_type (MobCategory category, uint8_t biome) {
  const Weight *w = weightsFor(category, biome);
  if (!w) return 0;
  uint32_t total = 0;
  for (const Weight *k = w; k->type; k ++) total += k->weight;
  uint32_t r = fast_rand() % total;
  for (const Weight *k = w; k->type; k ++) {
    if (r < k->weight) return k->type;
    r -= k->weight;
  }
  return 0;
}

uint8_t spawn_biome_at (short x, short z) {
  return getChunkBiome(div_floor(x, CHUNK_SIZE), div_floor(z, CHUNK_SIZE));
}

int spawn_is_day (void) {
  return world_time < 13000 || world_time > 23460;
}

int spawn_sky_above (short x, uint8_t y, short z) {
  // Under the terrain's own surface counts as covered: climbing the whole
  // column costs too much on the Kode Dot (a shaft dug down to it is the
  // one case this gets wrong)
  if (y <= getHeightAt(x, z)) return 0;
  uint8_t top = chunkTopY(div_floor(x, 16), div_floor(z, 16));
  for (int by = y + 1; by <= top; by ++) {
    if (!isPassableBlock(getBlockAt(x, by, z))) return 0;
  }
  return 1;
}

// A torch within TORCH_LIGHT - 1 blocks (taxicab), from the block changes
static int nearTorch (short x, uint8_t y, short z) {
  int r = TORCH_LIGHT - 1;
  for (int cx = div_floor(x - r, 16); cx <= div_floor(x + r, 16); cx ++) {
    for (int cz = div_floor(z - r, 16); cz <= div_floor(z + r, 16); cz ++) {
      const int *indices;
      int n = bc_column((short)cx, (short)cz, &indices);
      for (int i = 0; i < n; i ++) {
        BlockChange *c = &block_changes[indices[i]];
        if (c->block != B_torch) continue;
        if (abs(c->x - x) + abs(c->y - y) + abs(c->z - z) <= r) return 1;
      }
    }
  }
  return 0;
}

int spawn_is_dark (short x, uint8_t y, short z) {
  if (nearTorch(x, y, z)) return 0;
  if (!spawn_sky_above(x, y, z)) return 1;
  return !spawn_is_day();
}

static double distSq (const PlayerData *p, double x, double z) {
  double dx = p->x + 0.5 - x, dz = p->z + 0.5 - z;
  return dx * dx + dz * dz;
}

static uint8_t activePlayer (const PlayerData *p) {
  return p->client_fd != -1 && !(p->flags & 0x20) && p->health > 0;
}

static int countNear (MobCategory category, double x, double z, int *global) {
  int near = 0;
  if (global) *global = 0;
  for (uint32_t i = 0; i < ent_capacity(); i ++) {
    Entity *e = ent_at(i);
    if (e->type == 0 || e->kind != ENT_KIND_MOB) continue;
    const MobType *t = mob_type(e->type);
    if (!t || t->category != category) continue;
    if (global) (*global) ++;
    double dx = (double)e->x / ENT_UNIT - x, dz = (double)e->z / ENT_UNIT - z;
    if (dx * dx + dz * dz <= CAP_RANGE * CAP_RANGE) near ++;
  }
  return near;
}

static uint8_t farFromPlayers (double x, double z) {
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (player_data[i].client_fd == -1) continue;
    if (distSq(&player_data[i], x, z) < SPAWN_MIN_DISTANCE * SPAWN_MIN_DISTANCE) return 0;
  }
  return 1;
}

// Up to GROUP_SIZE of one type around (x, y, z), each where it can stand and
// the category's light rule allows. On the surface each one stands on its own
// column's top. Returns how many appeared.
static int spawnGroup (MobCategory category, uint16_t type, int x, int y, int z, uint8_t surface, int room) {
  int made = 0;
  for (int k = 0; k < GROUP_SIZE && made < room; k ++) {
    uint32_t r = fast_rand();
    int sx = x + (k ? (int)(r % (2 * GROUP_SPREAD + 1)) - GROUP_SPREAD : 0);
    int sz = z + (k ? (int)((r >> 8) % (2 * GROUP_SPREAD + 1)) - GROUP_SPREAD : 0);
    int sy = surface ? getHeightAt(sx, sz) + 1 : y;
    if (sy < 1 || sy > 254 || !path_standable(sx, sy, sz)) continue;
    if (!farFromPlayers(sx + 0.5, sz + 0.5)) continue;
    uint8_t below = getBlockAt(sx, sy - 1, sz);
    uint8_t feet = getBlockAt(sx, sy, sz);
    if (feet >= B_water && feet < B_water + 8) continue;     // land mobs only, for now
    if (category == CAT_MONSTER) {
      if (!spawn_is_dark(sx, sy, sz)) continue;
    } else {
      if (below != B_grass_block || !spawn_sky_above(sx, sy, sz)) continue;
    }
    if (mob_spawn(type, sx * ENT_UNIT + ENT_UNIT / 2, sy * ENT_UNIT, sz * ENT_UNIT + ENT_UNIT / 2)) made ++;
  }
  return made;
}

// A random spot inside the chunks a player has loaded
static void randomSpotAround (const PlayerData *p, int *x, int *z) {
  int span = (2 * VIEW_DISTANCE + 1) * 16;
  int x0 = div_floor(p->x, 16) * 16 - VIEW_DISTANCE * 16;
  int z0 = div_floor(p->z, 16) * 16 - VIEW_DISTANCE * 16;
  uint32_t r = fast_rand();
  *x = x0 + (int)(r % span);
  *z = z0 + (int)((r >> 12) % span);
}

// Monsters near each player slot and in the whole world, counted once a tick
static int monsters_near[MAX_PLAYERS];
static int monsters_total;

static void countMonsters (void) {
  memset(monsters_near, 0, sizeof(monsters_near));
  monsters_total = 0;
  for (uint32_t i = 0; i < ent_capacity(); i ++) {
    Entity *e = ent_at(i);
    if (e->type == 0 || e->kind != ENT_KIND_MOB) continue;
    const MobType *t = mob_type(e->type);
    if (!t || t->category != CAT_MONSTER) continue;
    monsters_total ++;
    double x = (double)e->x / ENT_UNIT, z = (double)e->z / ENT_UNIT;
    for (int j = 0; j < MAX_PLAYERS; j ++) {
      if (activePlayer(&player_data[j]) && distSq(&player_data[j], x, z) <= CAP_RANGE * CAP_RANGE) monsters_near[j] ++;
    }
  }
}

static void tryMonsters (const PlayerData *p) {
  int global = monsters_total;
  int near = monsters_near[p - player_data];
  if (near >= CAP_MONSTER || global >= CAP_MONSTER_GLOBAL) return;
  int x, z;
  randomSpotAround(p, &x, &z);
  if (!farFromPlayers(x + 0.5, z + 0.5)) return;
  int top = getHeightAt(x, z) + 1;
  // Half the tries on the surface, half at a random height for caves
  uint8_t surface = fast_rand() & 1;
  int y = surface ? top : 1 + (int)(fast_rand() % (unsigned)top);
  uint16_t type = spawn_pick_type(CAT_MONSTER, spawn_biome_at(x, z));
  if (!type || !mob_type(type)) return;
  int room = CAP_MONSTER - near;
  if (CAP_MONSTER_GLOBAL - global < room) room = CAP_MONSTER_GLOBAL - global;
  int made = spawnGroup(CAT_MONSTER, type, x, y, z, surface, room);
  // Keep this tick's counts right for the players after this one
  monsters_total += made;
  for (int j = 0; j < MAX_PLAYERS; j ++) {
    if (activePlayer(&player_data[j]) && distSq(&player_data[j], x + 0.5, z + 0.5) <= CAP_RANGE * CAP_RANGE) monsters_near[j] += made;
  }
}

static void tryCreatures (double cx, double cz, int x, int z) {
  int near = countNear(CAT_CREATURE, cx, cz, NULL);
  if (near >= CAP_CREATURE) return;
  uint16_t type = spawn_pick_type(CAT_CREATURE, spawn_biome_at(x, z));
  if (!type || !mob_type(type)) return;
  spawnGroup(CAT_CREATURE, type, x, 0, z, 1, CAP_CREATURE - near);
}

void spawn_tick (void) {
  // Each player gets a try every SPAWN_EVERY ticks, staggered, so a tick
  // with six players costs what one did
  uint8_t any = 0;
  for (int i = 0; i < MAX_PLAYERS && !any; i ++) {
    any = activePlayer(&player_data[i]) && (server_ticks + i) % SPAWN_EVERY == 0;
  }
  if (!any) return;
  countMonsters();
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    PlayerData *p = &player_data[i];
    if (!activePlayer(p) || (server_ticks + i) % SPAWN_EVERY != 0) continue;
    tryMonsters(p);
    if (spawn_is_day() && fast_rand() % CREATURE_TICK_CHANCE == 0) {
      int x, z;
      randomSpotAround(p, &x, &z);
      tryCreatures(p->x + 0.5, p->z + 0.5, x, z);
    }
  }
}

void spawn_chunk_creatures (short cx, short cz) {
  if (fast_rand() % CREATURE_CHUNK_CHANCE) return;
  uint32_t r = fast_rand();
  int x = cx * 16 + (int)(r & 15), z = cz * 16 + (int)((r >> 4) & 15);
  tryCreatures(x + 0.5, z + 0.5, x, z);
}

// Where and when mobs appear
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "bareiron.h"
#include "globals.h"
#include "registries.h"
#include "blockindex.h"
#include "procedures.h"
#include "worldgen.h"
#include "entity.h"
#include "mobs.h"
#include "spawning.h"

#define FLOOR 200

static void setup (void) {
  static BareironPlatform plat;
  plat = *bareiron_posix_platform();
  platform = &plat;
  world_seed = 0x1234567u;
  free(block_changes);
  block_changes_cap = 20000;
  block_changes = malloc(block_changes_cap * sizeof(BlockChange));
  for (uint32_t i = 0; i < block_changes_cap; i ++) block_changes[i].block = 0xFF;
  block_changes_count = 0;
  bc_index_free();
  CHECK(bc_index_init(block_changes_cap) == 0);
  ent_free();
  CHECK(ent_init(512) == 0);
  for (int i = 0; i < MAX_PLAYERS; i ++) player_data[i].client_fd = -1;
  for (short x = 0; x < 40; x ++) for (short z = 0; z < 40; z ++) makeBlockChange(x, FLOOR, z, B_stone);
  server_ticks = 0;
}

static void test_torch_prevents_spawn (void) {
  setup();
  world_time = 18000;
  makeBlockChange(10, FLOOR + 1, 10, B_torch);
  for (int d = 1; d < 14; d ++) CHECK(!spawn_is_dark(10 + d, FLOOR + 1, 10));
  CHECK(spawn_is_dark(10 + 14, FLOOR + 1, 10));
  CHECK(!spawn_is_dark(10 + 7, FLOOR + 1, 10 + 6));   // taxicab 13
  CHECK(spawn_is_dark(10 + 7, FLOOR + 1, 10 + 7));    // taxicab 14
}

static void test_cave_is_dark_by_day (void) {
  setup();
  world_time = 6000;
  for (short x = 0; x < 10; x ++) for (short z = 0; z < 10; z ++) makeBlockChange(x, FLOOR + 3, z, B_stone);
  CHECK(!spawn_sky_above(5, FLOOR + 1, 5));
  CHECK(spawn_is_dark(5, FLOOR + 1, 5));
}

static void test_surface_not_dark_by_day (void) {
  setup();
  world_time = 6000;
  CHECK(spawn_sky_above(20, FLOOR + 1, 20));
  CHECK(!spawn_is_dark(20, FLOOR + 1, 20));
  world_time = 18000;
  CHECK(spawn_is_dark(20, FLOOR + 1, 20));
}

static PlayerData *player (int i, short x, short z) {
  PlayerData *p = &player_data[i];
  memset(p, 0, sizeof(*p));
  p->client_fd = 1000 + i;
  p->x = x; p->z = z; p->y = getHeightAt(x, z) + 1;
  p->health = 20;
  return p;
}

static int monstersNear (const PlayerData *p) {
  int n = 0;
  for (uint32_t i = 0; i < ent_capacity(); i ++) {
    Entity *e = ent_at(i);
    if (e->type == 0 || e->kind != ENT_KIND_MOB) continue;
    const MobType *t = mob_type(e->type);
    if (!t || t->category != CAT_MONSTER) continue;
    double dx = (double)e->x / ENT_UNIT - p->x, dz = (double)e->z / ENT_UNIT - p->z;
    if (dx * dx + dz * dz <= CAP_RANGE * CAP_RANGE) n ++;
  }
  return n;
}

static void test_cap_per_player (void) {
  setup();
  world_time = 18000;
  PlayerData *a = player(0, 0, 0), *b = player(1, 300, 0), *c = player(2, 0, 300);
  for (int t = 0; t < 6000; t ++) { server_ticks ++; spawn_tick(); }
  int na = monstersNear(a), nb = monstersNear(b), nc = monstersNear(c);
  printf("  monsters: %d %d %d, total %u\n", na, nb, nc, ent_count());
  CHECK(na <= CAP_MONSTER && nb <= CAP_MONSTER && nc <= CAP_MONSTER);
  CHECK(na + nb + nc <= CAP_MONSTER_GLOBAL);
  CHECK(na >= 16 && nb >= 16);   // each player gets a night of its own
  // never within SPAWN_MIN_DISTANCE of a player
  for (uint32_t i = 0; i < ent_capacity(); i ++) {
    Entity *e = ent_at(i);
    if (e->type == 0) continue;
    for (int j = 0; j < 3; j ++) {
      double dx = (double)e->x / ENT_UNIT - player_data[j].x, dz = (double)e->z / ENT_UNIT - player_data[j].z;
      CHECK(dx * dx + dz * dz >= (SPAWN_MIN_DISTANCE - 1) * (SPAWN_MIN_DISTANCE - 1));
    }
  }
}

static void test_desert_spawns_husks (void) {
  setup();
  // Vanilla: in the desert 80 % of the zombie kind are husks; in snow 80 % of skeletons are strays
  int husks = 0, zombies = 0;
  for (int i = 0; i < 4000; i ++) {
    uint16_t t = spawn_pick_type(CAT_MONSTER, W_desert);
    if (t == E_husk) husks ++;
    if (t == E_husk || t == E_zombie || t == E_zombie_villager) zombies ++;
  }
  CHECK(husks * 100 >= zombies * 70 && husks * 100 <= zombies * 90);
  int strays = 0, skeletons = 0;
  for (int i = 0; i < 4000; i ++) {
    uint16_t t = spawn_pick_type(CAT_MONSTER, W_snowy_plains);
    if (t == E_stray) strays ++;
    if (t == E_stray || t == E_skeleton) skeletons ++;
  }
  CHECK(strays * 100 >= skeletons * 70 && strays * 100 <= skeletons * 90);
  CHECK(spawn_pick_type(CAT_CREATURE, W_desert) == 0);   // no wave-1 animals in the desert
}

static void test_lonely_hostile_despawns (void) {
  setup();
  world_time = 18000;
  // On the platform, 36 blocks from the player: beyond 32, within 128, out of chase range
  PlayerData *p = player(0, 2, 2);
  p->y = FLOOR + 1;
  Entity *z = mob_spawn(E_zombie, (int32_t)(2.5 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(38.5 * ENT_UNIT));
  int32_t id = ent_id(z);
  for (int t = 0; t < 6000 && ent_by_id(id); t ++) {
    server_ticks ++;
    mobs_tick();
    if (ent_by_id(id)) { z->x = (int32_t)(2.5 * ENT_UNIT); z->z = (int32_t)(38.5 * ENT_UNIT); z->vx = z->vz = 0; } // keep it there
  }
  CHECK(ent_by_id(id) == NULL);
}

int main (void) {
  RUN(test_torch_prevents_spawn);
  RUN(test_cave_is_dark_by_day);
  RUN(test_surface_not_dark_by_day);
  RUN(test_cap_per_player);
  RUN(test_desert_spawns_husks);
  RUN(test_lonely_hostile_despawns);
  return CHECK_DONE();
}

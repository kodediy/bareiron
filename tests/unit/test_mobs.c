// Mob types and their base goals, on a stone platform up in the sky
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "bareiron.h"
#include "globals.h"
#include "registries.h"
#include "blockindex.h"
#include "procedures.h"
#include "entity.h"
#include "mobs.h"

#define FLOOR 200

static PlayerData *setup (void) {
  static BareironPlatform plat;
  plat = *bareiron_posix_platform();
  platform = &plat;
  world_seed = 0x1234567u;
  world_time = 18000;   // night: nothing burns
  free(block_changes);
  block_changes_cap = 20000;
  block_changes = malloc(block_changes_cap * sizeof(BlockChange));
  for (uint32_t i = 0; i < block_changes_cap; i ++) block_changes[i].block = 0xFF;
  block_changes_count = 0;
  bc_index_free();
  CHECK(bc_index_init(block_changes_cap) == 0);
  ent_free();
  CHECK(ent_init(128) == 0);
  for (short x = 0; x < 40; x ++) for (short z = 0; z < 40; z ++) makeBlockChange(x, FLOOR, z, B_stone);
  for (int i = 0; i < MAX_PLAYERS; i ++) player_data[i].client_fd = -1;
  PlayerData *p = &player_data[0];
  memset(p, 0, sizeof(*p));
  p->client_fd = 1000;        // not a socket: what it is sent is dropped
  p->x = 25; p->y = FLOOR + 1; p->z = 10;
  p->health = 20; p->hunger = 20;
  server_ticks = 0;
  return p;
}

static void tick (void) { server_ticks ++; mobs_tick(); }

static void test_zombie_reaches_player_and_hits (void) {
  PlayerData *p = setup();
  Entity *z = mob_spawn(E_zombie, (int32_t)(13.5 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(10.5 * ENT_UNIT));
  CHECK(z && z->health == 20);
  int t;
  for (t = 0; t < 200 && p->health == 20; t ++) tick();
  CHECK(p->health < 20);
  CHECK(t < 200);
}

static void test_cow_wanders_nearby (void) {
  PlayerData *p = setup();
  p->x = 5; p->z = 35;                     // a player in the world, not next to it
  Entity *c = mob_spawn(E_cow, (int32_t)(20.5 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(20.5 * ENT_UNIT));
  int32_t x0 = c->x, z0 = c->z;
  int moved = 0, far = 0;
  for (int t = 0; t < 2000; t ++) {
    tick();
    if (c->x != x0 || c->z != z0) moved = 1;
    int dx = abs(c->x - x0) / ENT_UNIT, dz = abs(c->z - z0) / ENT_UNIT;
    if (dx > 20 || dz > 20) far = 1;
  }
  CHECK(moved);
  CHECK(!far);
  CHECK(c->y >= FLOOR * ENT_UNIT);        // still on the platform
}

static void test_decisions_are_staggered (void) {
  setup();
  for (int i = 0; i < 40; i ++) mob_spawn(E_zombie, (int32_t)((2 + i % 36) * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)((2 + i / 36) * ENT_UNIT));
  uint32_t worst = 0;
  for (int t = 0; t < 20; t ++) {
    tick();
    if (mob_decisions_last_tick > worst) worst = mob_decisions_last_tick;
  }
  CHECK(worst > 0 && worst <= 11);
}

static void test_creeper_explodes_near_player (void) {
  PlayerData *p = setup();
  Entity *c = mob_spawn(E_creeper, (int32_t)(21.5 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(10.5 * ENT_UNIT));
  CHECK(c != NULL);
  int32_t id = ent_id(c);
  int t;
  for (t = 0; t < 200 && ent_by_id(id); t ++) tick();
  CHECK(ent_by_id(id) == NULL);     // gone in its own blast
  CHECK(p->health < 20);
  CHECK(t >= 30);                   // not before its 1.5 s fuse
}

static void test_creeper_defuses_when_player_leaves (void) {
  PlayerData *p = setup();
  Entity *c = mob_spawn(E_creeper, (int32_t)(23.5 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(10.5 * ENT_UNIT));
  int32_t id = ent_id(c);
  for (int t = 0; t < 20 && c->u.mob.swell == 0; t ++) tick();
  CHECK(c->u.mob.swell > 0);        // lit
  p->x = 3;                         // the player runs off, more than 7 blocks
  for (int t = 0; t < 60; t ++) tick();
  CHECK(ent_by_id(id) != NULL);     // it did not go off
  CHECK(c->u.mob.swell == 0);
  CHECK(p->health == 20);
}

// The player shut in a stone box one block thick, roof included
static void boxIn (const PlayerData *p) {
  for (short x = p->x - 1; x <= p->x + 1; x ++) for (short z = p->z - 1; z <= p->z + 1; z ++)
    for (uint8_t y = FLOOR + 1; y <= FLOOR + 3; y ++)
      if (!(x == p->x && z == p->z && y < FLOOR + 3)) makeBlockChange(x, y, z, B_stone);
}

static void test_creeper_does_not_light_through_a_wall (void) {
  PlayerData *p = setup();
  boxIn(p);
  Entity *c = mob_spawn(E_creeper, (int32_t)(22.5 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(10.5 * ENT_UNIT));
  int32_t id = ent_id(c);
  int lit = 0;
  for (int t = 0; t < 200 && ent_by_id(id); t ++) { tick(); if (c->u.mob.swell) lit = 1; }
  CHECK(ent_by_id(id) != NULL);
  CHECK(!lit);
}

// A spider penned in a roofed cage that shares a wall with the player's box:
// within its reach, but behind stone
static void test_spider_does_not_bite_through_a_wall (void) {
  PlayerData *p = setup();
  boxIn(p);
  for (short x = 21; x <= 24; x ++) for (short z = 9; z <= 12; z ++) for (uint8_t y = FLOOR + 1; y <= FLOOR + 3; y ++)
    if (x == 21 || x == 24 || z == 9 || z == 12 || y == FLOOR + 3) makeBlockChange(x, y, z, B_stone);
  mob_spawn(E_spider, 23 * ENT_UNIT, (FLOOR + 1) * ENT_UNIT, 11 * ENT_UNIT);
  for (int t = 0; t < 200; t ++) tick();
  CHECK(p->health == 20);
}

int main (void) {
  RUN(test_zombie_reaches_player_and_hits);
  RUN(test_cow_wanders_nearby);
  RUN(test_decisions_are_staggered);
  RUN(test_creeper_explodes_near_player);
  RUN(test_creeper_defuses_when_player_leaves);
  RUN(test_creeper_does_not_light_through_a_wall);
  RUN(test_spider_does_not_bite_through_a_wall);
  return CHECK_DONE();
}

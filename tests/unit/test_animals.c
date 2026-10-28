// Wave 1's animals: breeding and its limit, babies, sheep and grass, eggs, seeds
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
#include "drops.h"
#include "worldgen.h"

#define FLOOR 200

static PlayerData *setup (uint8_t ground) {
  static BareironPlatform plat;
  plat = *bareiron_posix_platform();
  platform = &plat;
  world_seed = 0x1234567u;
  world_time = 6000;
  free(block_changes);
  block_changes_cap = 20000;
  block_changes = malloc(block_changes_cap * sizeof(BlockChange));
  for (uint32_t i = 0; i < block_changes_cap; i ++) block_changes[i].block = 0xFF;
  block_changes_count = 0;
  bc_index_free();
  CHECK(bc_index_init(block_changes_cap) == 0);
  ent_free();
  CHECK(ent_init(128) == 0);
  for (short x = 0; x < 40; x ++) for (short z = 0; z < 40; z ++) makeBlockChange(x, FLOOR, z, ground);
  for (int i = 0; i < MAX_PLAYERS; i ++) player_data[i].client_fd = -1;
  PlayerData *p = &player_data[0];
  memset(p, 0, sizeof(*p));
  p->client_fd = 1000;
  p->x = 38; p->y = FLOOR + 1; p->z = 38;
  p->health = 20; p->hunger = 20;
  server_ticks = 0;
  return p;
}

static void tick (void) { server_ticks ++; mobs_tick(); drops_tick(); }

static Entity *cowAt (double x, double z) {
  return mob_spawn(E_cow, (int32_t)(x * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(z * ENT_UNIT));
}

static int babies (uint16_t type) {
  int n = 0;
  for (uint32_t i = 0; i < ent_capacity(); i ++) {
    Entity *e = ent_at(i);
    if (e->type == type && (e->flags & ENT_BABY)) n ++;
  }
  return n;
}

static void hold (PlayerData *p, uint16_t food, uint8_t n) {
  p->inventory_items[0] = food; p->inventory_count[0] = n; p->hotbar = 0;
}

static void feed (Entity *e, PlayerData *p, uint16_t food) {
  if (!p->inventory_count[0]) hold(p, food, 1);
  mob_feed(e, p);
}

static void test_two_fed_cows_have_a_calf (void) {
  PlayerData *p = setup(B_grass_block);
  Entity *a = cowAt(10.5, 10.5), *b = cowAt(14.5, 10.5);
  hold(p, I_wheat, 2);
  feed(a, p, I_wheat);
  feed(b, p, I_wheat);
  CHECK(p->inventory_count[0] == 0);           // one wheat each
  for (int t = 0; t < 600 && !babies(E_cow); t ++) tick();
  CHECK(babies(E_cow) == 1);
  feed(a, p, I_wheat);                          // the parents wait 5 minutes before the next
  feed(b, p, I_wheat);
  for (int t = 0; t < 600; t ++) tick();
  CHECK(babies(E_cow) == 1);
}

static void test_breeding_limit_32 (void) {
  PlayerData *p = setup(B_grass_block);
  for (int i = 0; i < 32; i ++) cowAt(2.5 + (i % 8) * 4, 20.5 + (i / 8) * 4);
  Entity *a = cowAt(10.5, 10.5), *b = cowAt(12.5, 10.5);
  feed(a, p, I_wheat);
  feed(b, p, I_wheat);
  for (int t = 0; t < 600; t ++) tick();
  CHECK(babies(E_cow) == 0);                    // 34 animals within 64 blocks: no more
}

static void test_baby_grows_after_24000 (void) {
  setup(B_grass_block);
  Entity *calf = cowAt(10.5, 10.5);
  calf->flags |= ENT_BABY;
  for (int t = 0; t < 23990; t ++) tick();
  CHECK(calf->flags & ENT_BABY);
  for (int t = 0; t < 20; t ++) tick();
  CHECK(!(calf->flags & ENT_BABY));
}

static void test_sheep_eats_grass_and_regrows (void) {
  setup(B_grass_block);
  Entity *s = mob_spawn(E_sheep, (int32_t)(10.5 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(10.5 * ENT_UNIT));
  s->flags |= ENT_SHEARED;
  for (int t = 0; t < 8000 && (s->flags & ENT_SHEARED); t ++) tick();
  CHECK(!(s->flags & ENT_SHEARED));
  int dirt = 0;
  for (short x = 0; x < 40; x ++) for (short z = 0; z < 40; z ++) if (getBlockAt(x, FLOOR, z) == B_dirt) dirt ++;
  CHECK(dirt >= 1);
}

static void test_sheep_colours (void) {
  setup(B_grass_block);
  int white = 0, other = 0;
  for (int i = 0; i < 100; i ++) {
    Entity *s = mob_spawn(E_sheep, (int32_t)((2.5 + i % 30) * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)((2.5 + i / 30) * ENT_UNIT));
    if (s->u.mob.wool == 0) white ++; else other ++;
    ent_remove(s);
  }
  CHECK(white >= 65 && other >= 5);             // about 82 % white
}

static void test_chicken_lays_eggs (void) {
  setup(B_grass_block);
  mob_spawn(E_chicken, (int32_t)(10.5 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(10.5 * ENT_UNIT));
  int eggs = 0;
  for (int t = 0; t < 12100 && !eggs; t ++) {
    tick();
    for (uint32_t i = 0; i < ent_capacity(); i ++) {
      Entity *e = ent_at(i);
      if (e->type == E_item && e->u.item.item == I_egg) eggs ++;
    }
  }
  CHECK(eggs >= 1);
}

static void test_grass_drops_seeds (void) {
  setup(B_grass_block);
  int seeds = 0;
  for (int i = 0; i < 800; i ++) if (getMiningResult(0, B_short_grass) == I_wheat_seeds) seeds ++;
  CHECK(seeds >= 60 && seeds <= 140);           // 1 in 8
}

int main (void) {
  RUN(test_two_fed_cows_have_a_calf);
  RUN(test_breeding_limit_32);
  RUN(test_baby_grows_after_24000);
  RUN(test_sheep_eats_grass_and_regrows);
  RUN(test_sheep_colours);
  RUN(test_chicken_lays_eggs);
  RUN(test_grass_drops_seeds);
  return CHECK_DONE();
}

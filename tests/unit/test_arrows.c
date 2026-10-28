// Arrows: their arc, what they hit, where they stick
#include <math.h>
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
#include "arrows.h"

#define FLOOR 200

static void setup (void) {
  static BareironPlatform plat;
  plat = *bareiron_posix_platform();
  platform = &plat;
  world_seed = 0x1234567u;
  free(block_changes);
  block_changes_cap = 4000;
  block_changes = malloc(block_changes_cap * sizeof(BlockChange));
  for (uint32_t i = 0; i < block_changes_cap; i ++) block_changes[i].block = 0xFF;
  block_changes_count = 0;
  bc_index_free();
  CHECK(bc_index_init(block_changes_cap) == 0);
  ent_free();
  CHECK(ent_init(32) == 0);
  for (int i = 0; i < MAX_PLAYERS; i ++) player_data[i].client_fd = -1;
  for (short x = 0; x < 40; x ++) for (short z = 0; z < 10; z ++) makeBlockChange(x, FLOOR, z, B_stone);
  // someone watching from off the platform: with nobody near, arrows wait
  PlayerData *p = &player_data[0];
  memset(p, 0, sizeof(*p));
  p->client_fd = 1000; p->x = 20; p->y = FLOOR + 1; p->z = 30; p->health = 20;
  server_ticks = 0;
}

static void ticks (int n) { for (int i = 0; i < n; i ++) { server_ticks ++; arrows_tick(); } }

static void test_arrow_arc_matches_vanilla (void) {
  setup();
  // high in the sky over nothing
  Entity *a = arrow_shoot(-1, 0, 240 * ENT_UNIT, 0, 1, 0, 0, 3.0f, ARROW_BASE_DAMAGE, 1, ARROW_NO_EFFECT);
  CHECK(a != NULL);
  int32_t id = ent_id(a);
  ticks(20);
  a = ent_by_id(id);
  CHECK(a != NULL);
  // vanilla's order each tick: move, then drag, then gravity
  double x = 0, y = 240, vx = 3, vy = 0;
  for (int t = 0; t < 20; t ++) { x += vx; y += vy; vx *= 0.99; vy *= 0.99; vy -= 0.05; }
  CHECK(fabs((double)a->y / ENT_UNIT - y) < 0.05);
  CHECK(fabs((double)a->x / ENT_UNIT - x) < 0.05);
}

static void test_arrow_hits_entity_in_path (void) {
  setup();
  Entity *cow = mob_spawn(E_cow, (int32_t)(12.5 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(5.5 * ENT_UNIT));
  Entity *a = arrow_shoot(-1, (int32_t)(2.5 * ENT_UNIT), (int32_t)((FLOOR + 1.6) * ENT_UNIT), (int32_t)(5.5 * ENT_UNIT),
                          1, 0, 0, 3.0f, ARROW_BASE_DAMAGE, 1, ARROW_NO_EFFECT);
  int32_t id = ent_id(a);
  ticks(10);
  CHECK(cow->health <= 10 - 6);     // 3 blocks a tick x 2, rounded up
  CHECK(ent_by_id(id) == NULL);     // the arrow is spent
}

static void test_arrow_sticks_in_block (void) {
  setup();
  for (short y = FLOOR + 1; y <= FLOOR + 3; y ++) makeBlockChange(20, y, 5, B_stone);
  Entity *a = arrow_shoot(-1, (int32_t)(2.5 * ENT_UNIT), (int32_t)((FLOOR + 2) * ENT_UNIT), (int32_t)(5.5 * ENT_UNIT),
                          1, 0, 0, 3.0f, ARROW_BASE_DAMAGE, 1, ARROW_NO_EFFECT);
  int32_t id = ent_id(a);
  ticks(20);
  a = ent_by_id(id);
  CHECK(a != NULL);
  CHECK(a->u.arrow.in_ground);
  CHECK(a->x <= 20 * ENT_UNIT && a->x > 19 * ENT_UNIT);   // against the wall's face
  int32_t x = a->x;
  ticks(10);
  CHECK(a->x == x);                                       // and stays there
}

// Nobody near: a stuck arrow waits, as in an unloaded chunk
static void test_stuck_arrow_waits_while_nobody_is_near (void) {
  setup();
  for (short y = FLOOR + 1; y <= FLOOR + 3; y ++) makeBlockChange(20, y, 5, B_stone);
  Entity *a = arrow_shoot(-1, (int32_t)(2.5 * ENT_UNIT), (int32_t)((FLOOR + 2) * ENT_UNIT), (int32_t)(5.5 * ENT_UNIT),
                          1, 0, 0, 3.0f, ARROW_BASE_DAMAGE, 1, ARROW_NO_EFFECT);
  int32_t id = ent_id(a);
  ticks(20);
  player_data[0].x = 2000;                                // the player wanders off
  ticks(ARROW_STUCK_LIFETIME + 20);
  CHECK(ent_by_id(id) != NULL);
  player_data[0].x = 20;                                  // and comes back
  ticks(ARROW_STUCK_LIFETIME + 20);
  CHECK(ent_by_id(id) == NULL);
}

static void test_bow_power (void) {
  CHECK(fabsf(bow_power(20) - 1.0f) < 1e-5f);    // a full second: full power
  CHECK(fabsf(bow_power(40) - 1.0f) < 1e-5f);
  CHECK(fabsf(bow_power(10) - (0.25f + 1.0f) / 3) < 1e-5f);
  CHECK(bow_power(1) < 0.1f);
}

int main (void) {
  RUN(test_arrow_arc_matches_vanilla);
  RUN(test_arrow_hits_entity_in_path);
  RUN(test_arrow_sticks_in_block);
  RUN(test_stuck_arrow_waits_while_nobody_is_near);
  RUN(test_bow_power);
  return CHECK_DONE();
}

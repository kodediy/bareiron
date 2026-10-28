// A mob's box moving through the blocks, tick by tick
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
#include "physics.h"

// Everything happens at y = 200 and above, where the generator leaves sky
#define FLOOR 200
static const PhysBody zombie = { 0.6f, 1.95f, 0.6f, 0.08f, 0.98f };

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
  CHECK(ent_init(16) == 0);
  // a floor of stone, x and z 0..29
  for (short x = 0; x < 30; x ++) for (short z = 0; z < 30; z ++) makeBlockChange(x, FLOOR, z, B_stone);
}

static Entity *mob_at (double x, double y, double z) {
  return ent_spawn(E_zombie, (int32_t)(x * ENT_UNIT), (int32_t)(y * ENT_UNIT), (int32_t)(z * ENT_UNIT));
}

static void test_falls_and_lands (void) {
  setup();
  Entity *e = mob_at(10.5, FLOOR + 11, 10.5);
  int t;
  for (t = 0; t < 40 && !(e->flags & ENT_ON_GROUND); t ++) phys_step(e, &zombie);
  CHECK(e->flags & ENT_ON_GROUND);
  CHECK(e->y == (FLOOR + 1) * ENT_UNIT);
  CHECK(t < 40);
}

static void push (Entity *e, int ticks, float vx) {
  for (int t = 0; t < ticks; t ++) { e->vx = vx; phys_step(e, &zombie); }
}

static void test_walks_up_one_block_not_two (void) {
  setup();
  makeBlockChange(15, FLOOR + 1, 10, B_stone);              // a one-block step at x = 15
  Entity *e = mob_at(10.5, FLOOR + 1, 10.5);
  push(e, 80, 0.15f);
  CHECK(e->x > 16 * ENT_UNIT);                                // went over it
  for (int t = 0; t < 40; t ++) phys_step(e, &zombie);
  CHECK(e->y == (FLOOR + 1) * ENT_UNIT);                      // and came back down after it
  CHECK(e->flags & ENT_ON_GROUND);
  setup();
  makeBlockChange(15, FLOOR + 1, 10, B_stone);              // a two-block wall
  makeBlockChange(15, FLOOR + 2, 10, B_stone);
  e = mob_at(10.5, FLOOR + 1, 10.5);
  push(e, 80, 0.15f);
  CHECK(e->x <= (int32_t)((15 - 0.3) * ENT_UNIT));            // stopped at its face
  CHECK(e->y < (FLOOR + 3) * ENT_UNIT);
}

static void test_mob_does_not_jitter_against_wall (void) {
  setup();
  for (short y = FLOOR + 1; y <= FLOOR + 2; y ++) makeBlockChange(15, y, 10, B_stone);
  Entity *e = mob_at(14.3, FLOOR + 1, 10.5);   // touches the wall on the first ticks
  push(e, 3, 0.2f);
  int32_t x3 = e->x;
  int32_t lo = x3, hi = x3;
  for (int t = 3; t < 40; t ++) {
    push(e, 1, 0.2f);
    if (e->x < lo) lo = e->x;
    if (e->x > hi) hi = e->x;
  }
  CHECK(hi - lo <= 1);
  CHECK(e->flags & ENT_ON_GROUND);
}

static void test_floats_in_water (void) {
  setup();
  // a pool 3x3, 6 deep, walled with stone
  for (short y = FLOOR + 1; y <= FLOOR + 6; y ++)
    for (short x = 9; x <= 13; x ++) for (short z = 9; z <= 13; z ++)
      makeBlockChange(x, y, z, (x == 9 || x == 13 || z == 9 || z == 13) ? B_stone : B_water);
  Entity *e = mob_at(11.5, FLOOR + 1, 11.5);
  for (int t = 0; t < 100; t ++) phys_step(e, &zombie);
  CHECK(e->flags & ENT_IN_WATER);
  // head above the surface (top of the water is FLOOR + 7)
  CHECK((double)e->y / ENT_UNIT + zombie.height > FLOOR + 7);
}

static void test_no_tunnelling_at_speed (void) {
  setup();
  for (short y = FLOOR + 1; y <= FLOOR + 2; y ++) makeBlockChange(15, y, 10, B_stone);  // one block thick
  Entity *e = mob_at(10.5, FLOOR + 1, 10.5);
  push(e, 20, 1.0f);
  CHECK(e->x <= (int32_t)((15 - 0.3) * ENT_UNIT));
}

static void test_box_free (void) {
  setup();
  CHECK(phys_box_free((int32_t)(10.5 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(10.5 * ENT_UNIT), &zombie));
  CHECK(!phys_box_free((int32_t)(10.5 * ENT_UNIT), (int32_t)((FLOOR + 0.5) * ENT_UNIT), (int32_t)(10.5 * ENT_UNIT), &zombie));
}

int main (void) {
  RUN(test_falls_and_lands);
  RUN(test_walks_up_one_block_not_two);
  RUN(test_mob_does_not_jitter_against_wall);
  RUN(test_floats_in_water);
  RUN(test_no_tunnelling_at_speed);
  RUN(test_box_free);
  return CHECK_DONE();
}

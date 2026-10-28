// Wave 1's monsters: skeletons and strays, spiders, burning in the sun, husks
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
#include "effects.h"

#define FLOOR 200

static PlayerData *setup (uint16_t time_of_day) {
  static BareironPlatform plat;
  plat = *bareiron_posix_platform();
  platform = &plat;
  world_seed = 0x1234567u;
  world_time = time_of_day;
  free(block_changes);
  block_changes_cap = 20000;
  block_changes = malloc(block_changes_cap * sizeof(BlockChange));
  for (uint32_t i = 0; i < block_changes_cap; i ++) block_changes[i].block = 0xFF;
  block_changes_count = 0;
  bc_index_free();
  CHECK(bc_index_init(block_changes_cap) == 0);
  ent_free();
  CHECK(ent_init(128) == 0);
  for (short x = 0; x < 60; x ++) for (short z = 0; z < 20; z ++) makeBlockChange(x, FLOOR, z, B_stone);
  for (int i = 0; i < MAX_PLAYERS; i ++) player_data[i].client_fd = -1;
  PlayerData *p = &player_data[0];
  memset(p, 0, sizeof(*p));
  p->client_fd = 1000;
  p->x = 25; p->y = FLOOR + 1; p->z = 10;
  p->health = 20; p->hunger = 20;
  eff_clear(p);
  server_ticks = 0;
  return p;
}

static void tick (void) { server_ticks ++; mobs_tick(); arrows_tick(); }

static double distTo (const Entity *e, const PlayerData *p) {
  double dx = (double)e->x / ENT_UNIT - (p->x + 0.5), dz = (double)e->z / ENT_UNIT - (p->z + 0.5);
  return dx * dx + dz * dz;
}

static void test_skeleton_keeps_distance_and_shoots (void) {
  PlayerData *p = setup(18000);
  Entity *s = mob_spawn(E_skeleton, (int32_t)(13.5 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(10.5 * ENT_UNIT));
  CHECK(s != NULL);
  int shot = 0;
  double closest = 1e9;
  for (int t = 0; t < 300 && p->health == 20; t ++) {
    tick();
    for (uint32_t i = 0; i < ent_capacity(); i ++) if (ent_at(i)->type == E_arrow) shot = 1;
    double d = distTo(s, p);
    if (d < closest) closest = d;
  }
  CHECK(shot);
  CHECK(p->health < 20);
  CHECK(closest > 3 * 3);        // it shoots from afar, it does not walk up to the player
}

static void test_stray_arrow_slows (void) {
  PlayerData *p = setup(18000);
  mob_spawn(E_stray, (int32_t)(15.5 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(10.5 * ENT_UNIT));
  for (int t = 0; t < 400 && !eff_ticks_left(p, EFF_SLOWNESS); t ++) { tick(); p->health = 20; }
  CHECK(eff_ticks_left(p, EFF_SLOWNESS) > 0);
}

static void test_spider_climbs_wall (void) {
  PlayerData *p = setup(18000);
  // a wall 4 high across the platform between the spider and the player
  for (short z = 0; z < 20; z ++) for (int y = 1; y <= 4; y ++) makeBlockChange(20, FLOOR + y, z, B_stone);
  Entity *s = mob_spawn(E_spider, (int32_t)(15.5 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(10.5 * ENT_UNIT));
  int top = 0;
  for (int t = 0; t < 100 && !top; t ++) {
    tick();
    if (s->y >= (FLOOR + 4) * ENT_UNIT) top = 1;
  }
  CHECK(top);
  (void)p;
}

static void test_zombie_burns_in_sun_not_in_shade (void) {
  PlayerData *p = setup(6000);
  p->x = 55;                                   // far, so nobody chases anybody
  Entity *open = mob_spawn(E_zombie, (int32_t)(5.5 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(5.5 * ENT_UNIT));
  // a roof over the other one
  for (short x = 9; x <= 13; x ++) for (short z = 9; z <= 13; z ++) makeBlockChange(x, FLOOR + 4, z, B_stone);
  Entity *shade = mob_spawn(E_zombie, (int32_t)(11.5 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(11.5 * ENT_UNIT));
  for (int t = 0; t < 60; t ++) {
    tick();
    shade->x = (int32_t)(11.5 * ENT_UNIT); shade->z = (int32_t)(11.5 * ENT_UNIT);   // keep it under the roof
  }
  CHECK(open->flags & ENT_ON_FIRE);
  CHECK(open->health < 20);
  CHECK(!(shade->flags & ENT_ON_FIRE));
  CHECK(shade->health == 20);
}

static void test_husk_does_not_burn (void) {
  PlayerData *p = setup(6000);
  p->x = 55;
  Entity *h = mob_spawn(E_husk, (int32_t)(5.5 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(5.5 * ENT_UNIT));
  for (int t = 0; t < 60; t ++) tick();
  CHECK(!(h->flags & ENT_ON_FIRE));
  CHECK(h->health == 20);
}

static void test_husk_hit_makes_hungry (void) {
  PlayerData *p = setup(18000);
  mob_spawn(E_husk, (int32_t)(22.5 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(10.5 * ENT_UNIT));
  for (int t = 0; t < 200 && p->health == 20; t ++) tick();
  CHECK(p->health < 20);
  CHECK(eff_ticks_left(p, EFF_HUNGER) > 0);
}

int main (void) {
  RUN(test_skeleton_keeps_distance_and_shoots);
  RUN(test_stray_arrow_slows);
  RUN(test_spider_climbs_wall);
  RUN(test_zombie_burns_in_sun_not_in_shade);
  RUN(test_husk_does_not_burn);
  RUN(test_husk_hit_makes_hungry);
  return CHECK_DONE();
}

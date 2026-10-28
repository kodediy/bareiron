// Beds: two blocks facing the player, sleeping through the night, respawning there
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
#include "beds.h"

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
  for (short x = 0; x < 30; x ++) for (short z = 0; z < 30; z ++) makeBlockChange(x, FLOOR, z, B_stone);
  for (int i = 0; i < MAX_PLAYERS; i ++) { player_data[i].client_fd = -1; beds_forget(&player_data[i]); }
  server_ticks = 0;
}

static PlayerData *player (int i, short x, short z) {
  PlayerData *p = &player_data[i];
  memset(p, 0, sizeof(*p));
  p->client_fd = 1000 + i;
  p->x = x; p->y = FLOOR + 1; p->z = z;
  p->health = 20;
  beds_forget(p);
  return p;
}

static void test_bed_places_two_blocks_facing (void) {
  setup();
  PlayerData *p = player(0, 3, 5);
  p->yaw = -63;                                   // facing east (+x)
  CHECK(bed_place(p, 5, FLOOR + 1, 5));
  CHECK(getBlockAt(5, FLOOR + 1, 5) == B_white_bed_foot_east);
  CHECK(getBlockAt(6, FLOOR + 1, 5) == B_white_bed_head_east);
  p->yaw = 0;                                     // facing south (+z)
  CHECK(bed_place(p, 10, FLOOR + 1, 10));
  CHECK(getBlockAt(10, FLOOR + 1, 11) == B_white_bed_head_south);
  makeBlockChange(20, FLOOR + 1, 21, B_stone);    // no room for the head
  CHECK(!bed_place(p, 20, FLOOR + 1, 20));
  CHECK(getBlockAt(20, FLOOR + 1, 20) == B_air);
  bed_break(5, FLOOR + 1, 5, B_white_bed_foot_east);   // breaking one half takes both
  CHECK(getBlockAt(6, FLOOR + 1, 5) == B_air);
}

static void test_sleep_skips_night_only_when_all_sleep (void) {
  setup();
  world_time = 18000;
  PlayerData *a = player(0, 3, 5), *b = player(1, 3, 15);
  a->yaw = b->yaw = -63;
  bed_place(a, 5, FLOOR + 1, 5);
  bed_place(b, 5, FLOOR + 1, 15);
  CHECK(bed_use(a, 5, FLOOR + 1, 5));
  CHECK(bed_sleeping(a) && !bed_sleeping(b));
  for (int t = 0; t < 2 * BED_SLEEP_TICKS; t ++) beds_tick();
  CHECK(world_time == 18000);                     // one of two asleep: still night
  CHECK(bed_use(b, 6, FLOOR + 1, 15));            // the head works too
  for (int t = 0; t < BED_SLEEP_TICKS + 1; t ++) beds_tick();
  CHECK(world_time == 0);                         // morning
  CHECK(!bed_sleeping(a) && !bed_sleeping(b));    // and everyone is up
}

static void test_no_sleep_by_day_or_with_monsters (void) {
  setup();
  world_time = 6000;
  PlayerData *a = player(0, 3, 5);
  a->yaw = -63;
  bed_place(a, 5, FLOOR + 1, 5);
  bed_use(a, 5, FLOOR + 1, 5);
  CHECK(!bed_sleeping(a));
  world_time = 18000;
  mob_spawn(E_zombie, 9 * ENT_UNIT, (FLOOR + 1) * ENT_UNIT, 5 * ENT_UNIT);
  bed_use(a, 5, FLOOR + 1, 5);
  CHECK(!bed_sleeping(a));
}

static void test_respawn_at_bed (void) {
  setup();
  world_time = 18000;
  PlayerData *a = player(0, 3, 5);
  a->yaw = -63;
  bed_place(a, 5, FLOOR + 1, 5);
  short x, z; uint8_t y;
  CHECK(!bed_spawn(a, &x, &y, &z));               // not until it has slept in it
  bed_use(a, 5, FLOOR + 1, 5);
  CHECK(bed_spawn(a, &x, &y, &z));
  CHECK(abs(x - 5) <= 2 && abs(z - 5) <= 2 && y >= FLOOR + 1);
  bed_break(6, FLOOR + 1, 5, B_white_bed_head_east);
  CHECK(!bed_spawn(a, &x, &y, &z));               // the bed is gone
}

int main (void) {
  RUN(test_bed_places_two_blocks_facing);
  RUN(test_sleep_skips_night_only_when_all_sleep);
  RUN(test_no_sleep_by_day_or_with_monsters);
  RUN(test_respawn_at_bed);
  return CHECK_DONE();
}

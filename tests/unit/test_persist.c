// World format v2: animals, items on the ground and beds survive a restart
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "bareiron.h"
#include "globals.h"
#include "registries.h"
#include "blockindex.h"
#include "procedures.h"
#include "serialize.h"
#include "entity.h"
#include "mobs.h"
#include "drops.h"
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
  CHECK(ent_init(64) == 0);
  memset(player_data, 0, sizeof(player_data));
  for (int i = 0; i < MAX_PLAYERS; i ++) { player_data[i].client_fd = -1; beds_forget(&player_data[i]); }
  for (short x = 0; x < 20; x ++) for (short z = 0; z < 20; z ++) makeBlockChange(x, FLOOR, z, B_stone);
  server_ticks = 0;
}

static Entity *find (uint16_t type) {
  for (uint32_t i = 0; i < ent_capacity(); i ++) if (ent_at(i)->type == type) return ent_at(i);
  return NULL;
}

static void test_v1_world_loads_without_entities (void) {
  setup();
  // A version 1 snapshot: header, block changes, players, nothing after
  size_t size = sizeof(WorldHeader) + block_changes_count * sizeof(BlockChange) + MAX_PLAYERS * sizeof(PlayerData);
  uint8_t *buf = malloc(size);
  WorldHeader h = { WORLD_MAGIC, 1, 0, world_seed, (uint32_t)block_changes_count, MAX_PLAYERS, sizeof(PlayerData), 1234 };
  memcpy(buf, &h, sizeof(h));
  memcpy(buf + sizeof(h), block_changes, block_changes_count * sizeof(BlockChange));
  memcpy(buf + sizeof(h) + block_changes_count * sizeof(BlockChange), player_data, MAX_PLAYERS * sizeof(PlayerData));
  mob_spawn(E_cow, 5 * ENT_UNIT, (FLOOR + 1) * ENT_UNIT, 5 * ENT_UNIT);
  CHECK(worldSnapshotRead(buf, size) == 0);
  CHECK(ent_count() == 0);
  CHECK(world_time == 1234);
  free(buf);
}

static void test_animals_round_trip (void) {
  setup();
  Entity *cow = mob_spawn(E_cow, (int32_t)(5.25 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(6.75 * ENT_UNIT));
  cow->flags |= ENT_BABY;
  cow->u.mob.grow = 1000;
  Entity *sheep = mob_spawn(E_sheep, 8 * ENT_UNIT, (FLOOR + 1) * ENT_UNIT, 8 * ENT_UNIT);
  sheep->u.mob.wool = 7;
  sheep->flags |= ENT_SHEARED;
  sheep->health = 5;
  drop_item(I_dirt, 5, 10 * ENT_UNIT, (FLOOR + 1) * ENT_UNIT, 10 * ENT_UNIT, 0, 0, 0, 0)->flags |= ENT_KEEP;   // a death pile
  mob_spawn(E_zombie, 12 * ENT_UNIT, (FLOOR + 1) * ENT_UNIT, 12 * ENT_UNIT);   // hostiles are not saved
  int32_t cx = cow->x, cz = cow->z;

  size_t size = worldSnapshotSize();
  uint8_t *buf = malloc(size);
  CHECK(worldSnapshotWrite(buf, size) == size);
  ent_clear();
  CHECK(worldSnapshotRead(buf, size) == 0);
  free(buf);

  CHECK(ent_count() == 3);
  cow = find(E_cow); sheep = find(E_sheep);
  Entity *item = find(E_item);
  CHECK(cow && sheep && item && !find(E_zombie));
  CHECK(cow->x == cx && cow->z == cz);
  CHECK((cow->flags & ENT_BABY) && cow->u.mob.grow == 1000);
  CHECK(sheep->u.mob.wool == 7 && (sheep->flags & ENT_SHEARED) && sheep->health == 5);
  CHECK(item->u.item.item == I_dirt && item->u.item.count == 5);
  CHECK(item->flags & ENT_KEEP);
}

static void test_bed_spawn_round_trip (void) {
  setup();
  world_time = 18000;
  PlayerData *p = &player_data[2];
  p->client_fd = 1002; p->uuid[0] = 1; p->x = 3; p->y = FLOOR + 1; p->z = 5; p->health = 20; p->yaw = -63;
  bed_place(p, 5, FLOOR + 1, 5);
  bed_use(p, 5, FLOOR + 1, 5);
  size_t size = worldSnapshotSize();
  uint8_t *buf = malloc(size);
  worldSnapshotWrite(buf, size);
  beds_forget(p);
  CHECK(worldSnapshotRead(buf, size) == 0);
  free(buf);
  short x, z; uint8_t y;
  CHECK(bed_spawn(&player_data[2], &x, &y, &z));
  CHECK(x == 5 && z == 5);
}

static void test_frozen_animal_does_not_tick (void) {
  setup();
  PlayerData *p = &player_data[0];
  p->client_fd = 1000; p->x = 5; p->y = FLOOR + 1; p->z = 5; p->health = 20;
  // far from the player: past the simulation distance, but no animal ever despawns
  Entity *far = mob_spawn(E_cow, 105 * ENT_UNIT, 230 * ENT_UNIT, 5 * ENT_UNIT);       // in the air
  Entity *very = mob_spawn(E_cow, 400 * ENT_UNIT, 230 * ENT_UNIT, 5 * ENT_UNIT);
  int32_t y0 = far->y;
  for (int t = 0; t < 100; t ++) { server_ticks ++; mobs_tick(); }
  CHECK(far->type == E_cow && far->y == y0);   // did not fall: not simulated
  CHECK(very->type == E_cow);                   // still there
}

// A returning player keeps its slot, and with it its bed; only a new player starts without one
static void test_returning_player_keeps_bed (void) {
  setup();
  uint8_t uuid[16] = { 9 };
  char back[16] = "back", fresh[16] = "new";   // names are copied as 16 bytes
  CHECK(reservePlayerData(1001, uuid, back) == 0);
  PlayerData *p;
  CHECK(getPlayerData(1001, &p) == 0);
  int slot = (int)(p - player_data);
  bed_set_spawn_point(slot, 1, 5, FLOOR + 1, 5);
  p->client_fd = -1;                                   // it left
  CHECK(reservePlayerData(1002, uuid, back) == 0);     // and comes back
  CHECK(getPlayerData(1002, &p) == 0);
  p->flags |= 0x20;
  handlePlayerJoin(p);
  short x, z; uint8_t y;
  CHECK(bed_spawn_point(slot, &x, &y, &z) == 1);
  uint8_t other[16] = { 7 };
  CHECK(reservePlayerData(1003, other, fresh) == 0);
  PlayerData *q;
  CHECK(getPlayerData(1003, &q) == 0);
  CHECK(bed_spawn_point((int)(q - player_data), &x, &y, &z) == 0);
}

int main (void) {
  RUN(test_v1_world_loads_without_entities);
  RUN(test_animals_round_trip);
  RUN(test_bed_spawn_round_trip);
  RUN(test_frozen_animal_does_not_tick);
  RUN(test_returning_player_keeps_bed);
  return CHECK_DONE();
}

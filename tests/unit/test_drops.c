// Items on the ground: merging, pickup, lifetime
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "bareiron.h"
#include "globals.h"
#include "registries.h"
#include "blockindex.h"
#include "procedures.h"
#include "entity.h"
#include "drops.h"

#define FLOOR 200

static PlayerData *setup (void) {
  static BareironPlatform plat;
  plat = *bareiron_posix_platform();
  platform = &plat;
  world_seed = 0x1234567u;
  free(block_changes);
  block_changes_cap = 2000;
  block_changes = malloc(block_changes_cap * sizeof(BlockChange));
  for (uint32_t i = 0; i < block_changes_cap; i ++) block_changes[i].block = 0xFF;
  block_changes_count = 0;
  bc_index_free();
  CHECK(bc_index_init(block_changes_cap) == 0);
  ent_free();
  CHECK(ent_init(64) == 0);
  for (short x = 0; x < 20; x ++) for (short z = 0; z < 20; z ++) makeBlockChange(x, FLOOR, z, B_stone);
  for (int i = 0; i < MAX_PLAYERS; i ++) player_data[i].client_fd = -1;
  PlayerData *p = &player_data[0];
  memset(p, 0, sizeof(*p));
  p->client_fd = 1000;
  p->x = 15; p->y = FLOOR + 1; p->z = 15;   // away from the drops unless a test moves it
  p->health = 20;
  server_ticks = 0;
  return p;
}

static void ticks (int n) { for (int i = 0; i < n; i ++) { server_ticks ++; drops_tick(); } }

static int itemsOnGround (void) {
  int n = 0;
  for (uint32_t i = 0; i < ent_capacity(); i ++) if (ent_at(i)->type == E_item) n ++;
  return n;
}

static void test_items_merge (void) {
  setup();
  drop_item(I_dirt, 10, (int32_t)(5.5 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(5.5 * ENT_UNIT), 0, 0, 0, 0);
  drop_item(I_dirt, 10, (int32_t)(5.8 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(5.5 * ENT_UNIT), 0, 0, 0, 0);
  drop_item(I_stone, 1, (int32_t)(5.6 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(5.5 * ENT_UNIT), 0, 0, 0, 0);
  ticks(60);
  CHECK(itemsOnGround() == 2);                 // the dirt merged, the stone did not
  for (uint32_t i = 0; i < ent_capacity(); i ++) {
    Entity *e = ent_at(i);
    if (e->type == E_item && e->u.item.item == I_dirt) CHECK(e->u.item.count == 20);
  }
}

static void test_despawn_after_6000 (void) {
  setup();
  drop_item(I_dirt, 1, (int32_t)(5.5 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(5.5 * ENT_UNIT), 0, 0, 0, 0);
  ticks(DROP_LIFETIME - 1);
  CHECK(itemsOnGround() == 1);
  ticks(2);
  CHECK(itemsOnGround() == 0);
}

static void test_pickup_respects_delay (void) {
  PlayerData *p = setup();
  p->x = 5; p->z = 5;
  drop_item(I_dirt, 3, (int32_t)(5.5 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(5.5 * ENT_UNIT), 0, 0, 0, DROP_THROWN_DELAY);
  ticks(DROP_THROWN_DELAY - 2);
  CHECK(itemsOnGround() == 1);
  CHECK(p->inventory_count[0] == 0);
  ticks(4);
  CHECK(itemsOnGround() == 0);
  CHECK(p->inventory_items[0] == I_dirt && p->inventory_count[0] == 3);
}

static void test_full_inventory_leaves_item (void) {
  PlayerData *p = setup();
  p->x = 5; p->z = 5;
  for (int i = 0; i < 36; i ++) { p->inventory_items[i] = I_stone; p->inventory_count[i] = 64; }
  drop_item(I_dirt, 3, (int32_t)(5.5 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(5.5 * ENT_UNIT), 0, 0, 0, 0);
  ticks(20);
  CHECK(itemsOnGround() == 1);
}

static void test_spill_on_death (void) {
  PlayerData *p = setup();
  p->x = 5; p->z = 5;
  p->inventory_items[0] = I_dirt; p->inventory_count[0] = 5;
  p->inventory_items[38] = I_iron_chestplate; p->inventory_count[38] = 1;
  drops_spill_inventory(p);
  CHECK(p->inventory_count[0] == 0 && p->inventory_count[38] == 0);
  CHECK(itemsOnGround() == 2);
}

// An item at rest is checked less often, but still falls when its floor goes
static void test_resting_item_falls_when_floor_goes (void) {
  setup();
  Entity *e = drop_item(I_dirt, 1, (int32_t)(5.5 * ENT_UNIT), (FLOOR + 1) * ENT_UNIT, (int32_t)(5.5 * ENT_UNIT), 0, 0, 0, 0);
  ticks(40);
  CHECK(e->y == (FLOOR + 1) * ENT_UNIT);
  makeBlockChange(5, FLOOR, 5, B_air);
  ticks(20);
  CHECK(e->y < (FLOOR + 1) * ENT_UNIT);
}

int main (void) {
  RUN(test_items_merge);
  RUN(test_despawn_after_6000);
  RUN(test_pickup_respects_delay);
  RUN(test_full_inventory_leaves_item);
  RUN(test_spill_on_death);
  RUN(test_resting_item_falls_when_floor_goes);
  return CHECK_DONE();
}

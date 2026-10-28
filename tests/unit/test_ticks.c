// Timers measured in game ticks keep their real duration at 20 ticks a second
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "bareiron.h"
#include "globals.h"
#include "registries.h"
#include "blockindex.h"
#include "procedures.h"

static int ticks_until (PlayerData *p, uint8_t flag) {
  for (int t = 1; t <= 200; t ++) {
    handleServerTick(TIME_BETWEEN_TICKS);
    if (!(p->flags & flag)) return t;
  }
  return -1;
}

static PlayerData *setup (void) {
  static BareironPlatform plat;
  plat = *bareiron_posix_platform();
  platform = &plat;
  block_changes_cap = 16;
  block_changes = calloc(block_changes_cap, sizeof(BlockChange));
  bc_index_free();
  CHECK(bc_index_init(block_changes_cap) == 0);
  for (int i = 0; i < MAX_PLAYERS; i ++) player_data[i].client_fd = -1;
  PlayerData *p = &player_data[0];
  memset(p, 0, sizeof(*p));
  p->client_fd = 1000;          // not a socket: packets to it are dropped
  p->health = 20;
  p->hunger = 10;
  return p;
}

static void test_eating_takes_1_6_s (void) {
  PlayerData *p = setup();
  p->inventory_items[0] = I_cooked_beef;
  p->inventory_count[0] = 1;
  p->hotbar = 0;
  p->flags = 0x10;              // eating
  int t = ticks_until(p, 0x10);
  CHECK(TICKS_PER_SECOND == 20);
  CHECK(t >= 31 && t <= 34);    // 1.6 s at 20 ticks a second
}

static void test_attack_cooldown_is_0_6_s (void) {
  PlayerData *p = setup();
  p->flags = 0x01;              // attack cooldown
  int t = ticks_until(p, 0x01);
  CHECK(t >= 11 && t <= 14);    // 0.6 s
}

int main (void) {
  RUN(test_eating_takes_1_6_s);
  RUN(test_attack_cooldown_is_0_6_s);
  return CHECK_DONE();
}

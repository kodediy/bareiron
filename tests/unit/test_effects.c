// Hunger and slowness on a player
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "check.h"
#include "bareiron.h"
#include "globals.h"
#include "registries.h"
#include "net.h"
#include "effects.h"
#include "procedures.h"

static int far_end = -1;

static PlayerData *setup (void) {
  static BareironPlatform plat;
  plat = *bareiron_posix_platform();
  platform = &plat;
  for (int i = 0; i < MAX_PLAYERS; i ++) player_data[i].client_fd = -1;
  if (far_end != -1) close(far_end);
  int sv[2];
  socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
  fcntl(sv[1], F_SETFL, O_NONBLOCK);
  net_open(sv[0]);
  far_end = sv[1];
  PlayerData *p = &player_data[0];
  memset(p, 0, sizeof(*p));
  p->client_fd = sv[0];
  p->health = 20;
  p->hunger = 20;
  eff_clear(p);
  server_ticks = 0;
  return p;
}

// How many packets with this id the player was sent since the last call
static int sent (int packet_id) {
  net_flush(player_data[0].client_fd);
  static uint8_t buf[1 << 16];
  int total = 0, n = 0;
  ssize_t r;
  while ((r = read(far_end, buf + total, sizeof(buf) - total)) > 0) total += r;
  for (int pos = 0; pos < total; ) {
    int len = 0, shift = 0; uint8_t c;
    do { c = buf[pos ++]; len |= (c & 0x7F) << shift; shift += 7; } while (c & 0x80);
    if (buf[pos] == packet_id) n ++;
    pos += len;
  }
  return n;
}

static void test_longer_effect_wins (void) {
  PlayerData *p = setup();
  eff_apply(p, EFF_SLOWNESS, 0, 100);
  eff_apply(p, EFF_SLOWNESS, 0, 50);            // shorter: ignored
  CHECK(eff_ticks_left(p, EFF_SLOWNESS) == 100);
  eff_apply(p, EFF_SLOWNESS, 1, 20);            // stronger: wins, however short
  CHECK(eff_amplifier(p, EFF_SLOWNESS) == 1 && eff_ticks_left(p, EFF_SLOWNESS) == 20);
  CHECK(fabsf(eff_speed_factor(p) - 0.7f) < 1e-5f);
  CHECK(sent(P_update_mob_effect) == 2);        // the first and the stronger one
}

static void test_effect_ends_and_is_removed (void) {
  PlayerData *p = setup();
  eff_apply(p, EFF_HUNGER, 0, 140);
  sent(P_update_mob_effect);
  for (int t = 0; t < 139; t ++) eff_tick(p);
  CHECK(eff_ticks_left(p, EFF_HUNGER) == 1);
  CHECK(sent(P_remove_mob_effect) == 0);
  for (int t = 0; t < 5; t ++) eff_tick(p);
  CHECK(eff_ticks_left(p, EFF_HUNGER) == 0);
  CHECK(sent(P_remove_mob_effect) == 1);
}

static void test_hunger_drains_food (void) {
  PlayerData *p = setup();
  p->saturation = 0;
  eff_apply(p, EFF_HUNGER, 0, 1000);            // 1000 × 0.005 = 5 exhaustion: one point
  for (int t = 0; t < 1000; t ++) eff_tick(p);
  CHECK(p->hunger == 19);
  // with saturation, that goes first
  p = setup();
  p->saturation = 3 * EFF_SATURATION_UNIT;
  eff_apply(p, EFF_HUNGER, 0, 1000);
  for (int t = 0; t < 1000; t ++) eff_tick(p);
  CHECK(p->hunger == 20 && p->saturation == 2 * EFF_SATURATION_UNIT);
}

static void test_rotten_flesh_hungers (void) {
  PlayerData *p = setup();
  int hungry = 0;
  for (int i = 0; i < 20; i ++) {
    p->hunger = 10;
    p->inventory_items[0] = I_rotten_flesh; p->inventory_count[0] = 1; p->hotbar = 0;
    eff_clear(p);
    handlePlayerEating(p, 0);
    if (eff_ticks_left(p, EFF_HUNGER) == 600) hungry ++;
  }
  CHECK(hungry >= 10 && hungry <= 20);   // 4 times in 5
}

int main (void) {
  RUN(test_longer_effect_wins);
  RUN(test_effect_ends_and_is_removed);
  RUN(test_hunger_drains_food);
  RUN(test_rotten_flesh_hungers);
  return CHECK_DONE();
}

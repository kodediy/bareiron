// The entity table: ids, a full table, and what goes over the network
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "check.h"
#include "bareiron.h"
#include "globals.h"
#include "registries.h"
#include "entity.h"
#include "net.h"
#include "arrows.h"

static int far_end = -1;

static PlayerData *setup (uint32_t capacity) {
  static BareironPlatform plat;
  plat = *bareiron_posix_platform();
  platform = &plat;
  ent_free();
  CHECK(ent_init(capacity) == 0);
  for (int i = 0; i < MAX_PLAYERS; i ++) player_data[i].client_fd = -1;
  if (far_end != -1) { net_close(player_data[0].client_fd); close(far_end); }
  int sv[2];
  socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
  fcntl(sv[1], F_SETFL, O_NONBLOCK);
  net_open(sv[0]);
  far_end = sv[1];
  PlayerData *p = &player_data[0];
  memset(p, 0, sizeof(*p));
  p->client_fd = sv[0];
  p->x = 8; p->y = 70; p->z = 8;
  server_ticks = 0;
  return p;
}

// Reads what the player was sent: uncompressed frames, [length][id][payload]
static int read_varint (const uint8_t *b, int *pos) {
  int v = 0, shift = 0;
  while (1) { uint8_t c = b[(*pos) ++]; v |= (c & 0x7F) << shift; if (!(c & 0x80)) break; shift += 7; }
  return v;
}

typedef struct { int id; uint8_t payload[256]; int len; } Packet;

static int received (Packet *out, int max) {
  net_flush(player_data[0].client_fd);
  static uint8_t buf[1 << 16];
  int n = 0, total = 0;
  ssize_t r;
  while ((r = read(far_end, buf + total, sizeof(buf) - total)) > 0) total += r;
  int pos = 0;
  while (pos < total && n < max) {
    int len = read_varint(buf, &pos);
    int start = pos;
    out[n].id = read_varint(buf, &pos);
    out[n].len = len - (pos - start);
    memcpy(out[n].payload, buf + pos, out[n].len < 256 ? out[n].len : 256);
    pos = start + len;
    n ++;
  }
  return n;
}

static void network_turn (void) {
  for (int i = 0; i < ENT_NETWORK_EVERY; i ++) { server_ticks ++; ent_network_tick(); }
}

static void test_ids_never_collide_with_players (void) {
  setup(16);
  Entity *a = ent_spawn(E_zombie, 0, 64 * ENT_UNIT, 0);
  Entity *b = ent_spawn(E_cow, 0, 64 * ENT_UNIT, 0);
  CHECK(a && b);
  CHECK(ent_id(a) < -1 && ent_id(b) < -1 && ent_id(a) != ent_id(b));
  int32_t old = ent_id(a);
  ent_remove(a);
  Entity *c = ent_spawn(E_pig, 0, 64 * ENT_UNIT, 0);
  CHECK(ent_id(c) != old && ent_id(c) != ent_id(b));   // not reused while alive, nor right after
  CHECK(ent_by_id(ent_id(b)) == b);
  CHECK(ent_by_id(old) == NULL);
}

static void test_entity_table_full (void) {
  setup(4);
  for (int i = 0; i < 4; i ++) CHECK(ent_spawn(E_zombie, i * ENT_UNIT, 64 * ENT_UNIT, 0) != NULL);
  CHECK(ent_spawn(E_arrow, 0, 64 * ENT_UNIT, 0) == NULL);   // never takes a mob's place
  CHECK(ent_spawn(E_zombie, 0, 64 * ENT_UNIT, 0) == NULL);  // no item to give way either

  setup(4);
  Entity *items[2];
  items[0] = ent_spawn(E_item, 0, 64 * ENT_UNIT, 0);
  server_ticks ++; items[0]->age = 100;
  items[1] = ent_spawn(E_item, 0, 64 * ENT_UNIT, 0);
  items[1]->age = 10;
  int32_t older = ent_id(items[0]), newer = ent_id(items[1]);
  CHECK(ent_spawn(E_zombie, 0, 64 * ENT_UNIT, 0) != NULL);
  CHECK(ent_spawn(E_zombie, 0, 64 * ENT_UNIT, 0) != NULL);
  CHECK(ent_count() == 4);
  Entity *m = ent_spawn(E_cow, 0, 64 * ENT_UNIT, 0);      // a mob takes the oldest item's place
  CHECK(m != NULL);
  CHECK(ent_by_id(older) == NULL && ent_by_id(newer) != NULL);
  CHECK(ent_spawn(E_arrow, 0, 64 * ENT_UNIT, 0) == NULL);  // an arrow never does
}

static void test_delta_or_teleport (void) {
  PlayerData *p = setup(16);
  Packet pk[64];
  Entity *e = ent_spawn(E_zombie, 8 * ENT_UNIT, 70 * ENT_UNIT, 8 * ENT_UNIT);
  network_turn();
  int n = received(pk, 64), spawned = 0;
  for (int i = 0; i < n; i ++) if (pk[i].id == P_add_entity) spawned ++;
  CHECK(spawned == 1);
  CHECK(e->seen_by & 1);

  e->x += 3 * ENT_UNIT; ent_moved(e);
  network_turn();
  n = received(pk, 64);
  int moves = 0;
  for (int i = 0; i < n; i ++) {
    if (pk[i].id != P_move_entity_pos) continue;
    int pos = 0; read_varint(pk[i].payload, &pos);
    int16_t dx = (int16_t)((pk[i].payload[pos] << 8) | pk[i].payload[pos + 1]);
    CHECK(dx == 3 * ENT_UNIT);
    moves ++;
  }
  CHECK(moves == 1);

  e->x += 9 * ENT_UNIT; ent_moved(e);
  network_turn();
  n = received(pk, 64);
  int teleports = 0, deltas = 0;
  for (int i = 0; i < n; i ++) {
    if (pk[i].id == P_entity_position_sync) teleports ++;
    if (pk[i].id == P_move_entity_pos || pk[i].id == P_move_entity_pos_rot) deltas ++;
  }
  CHECK(teleports == 1 && deltas == 0);
  (void)p;
}

static void test_out_of_range_is_removed (void) {
  PlayerData *p = setup(16);
  Packet pk[64];
  Entity *e = ent_spawn(E_cow, 8 * ENT_UNIT, 70 * ENT_UNIT, 8 * ENT_UNIT);
  network_turn();
  received(pk, 64);
  p->x = 8 + (VIEW_DISTANCE + 2) * 16;   // the player walks away
  network_turn();
  int n = received(pk, 64), removes = 0;
  for (int i = 0; i < n; i ++) if (pk[i].id == P_remove_entities) removes ++;
  CHECK(removes == 1);
  CHECK(!(e->seen_by & 1));
  p->x = 8;                              // and comes back
  network_turn();
  n = received(pk, 64);
  int spawns = 0;
  for (int i = 0; i < n; i ++) if (pk[i].id == P_add_entity) spawns ++;
  CHECK(spawns == 1);
}

// An arrow can hit within a network turn: whoever sees the spot gets it at once
static void test_fast_entity_is_spawned_at_once (void) {
  setup(16);
  Packet pk[64];
  received(pk, 64);
  arrow_shoot(-1, 8 * ENT_UNIT, 71 * ENT_UNIT, 8 * ENT_UNIT, 1, 0, 0, 1.6f, 2.0f, 0, ARROW_NO_EFFECT);
  int n = received(pk, 64), spawned = 0;
  for (int i = 0; i < n; i ++) if (pk[i].id == P_add_entity) spawned ++;
  CHECK(spawned == 1);
}

int main (void) {
  RUN(test_ids_never_collide_with_players);
  RUN(test_entity_table_full);
  RUN(test_delta_or_teleport);
  RUN(test_out_of_range_is_removed);
  RUN(test_fast_entity_is_spawned_at_once);
  return CHECK_DONE();
}

// Click Container: a slot the server has no place for is never written
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "check.h"
#include "bareiron.h"
#include "globals.h"
#include "registries.h"
#include "net.h"
#include "varnum.h"
#include "packets.h"

static int server = -1, peer = -1;

static PlayerData *setup (int index) {
  static BareironPlatform plat;
  plat = *bareiron_posix_platform();
  platform = &plat;
  if (server != -1) { net_close(server); close(server); close(peer); }
  int fds[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
  fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);
  fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL, 0) | O_NONBLOCK);
  server = fds[0]; peer = fds[1];
  CHECK(net_open(server) == 0);
  for (int i = 0; i < MAX_PLAYERS; i ++) player_data[i].client_fd = -1;
  PlayerData *p = &player_data[index];
  memset(p, 0, sizeof(*p));
  p->client_fd = server;
  return p;
}

static int put_varint (uint8_t *b, int v) {
  int n = 0;
  do { uint8_t c = v & 0x7F; v = (int)((unsigned)v >> 7); if (v) c |= 0x80; b[n ++] = c; } while (v);
  return n;
}

// One Click Container (0x11) that puts `count` of `item` in client slot `slot` of `window`
static void click (int window, int16_t slot, uint16_t item, uint8_t count) {
  uint8_t body[64]; int n = 0;
  n += put_varint(body + n, 0x11);
  n += put_varint(body + n, window);
  n += put_varint(body + n, 1);                   // state id
  body[n ++] = (uint8_t)(slot >> 8); body[n ++] = (uint8_t)slot;
  body[n ++] = 0;                                 // button
  n += put_varint(body + n, 0);                   // mode: a plain click
  n += put_varint(body + n, 1);                   // one changed slot
  body[n ++] = (uint8_t)(slot >> 8); body[n ++] = (uint8_t)slot;
  body[n ++] = 1;                                 // it has an item
  n += put_varint(body + n, item);
  n += put_varint(body + n, count);
  n += put_varint(body + n, 0);                   // no components added
  n += put_varint(body + n, 0);                   // none removed
  body[n ++] = 0;                                 // nothing on the cursor
  uint8_t frame[72]; int f = put_varint(frame, n);
  memcpy(frame + f, body, n);
  CHECK(write(peer, frame, f + n) == f + n);
  int length;
  CHECK(net_pump(server) == 0);
  CHECK(net_next_packet(server, &length) == 1);
  CHECK(readVarInt(server) == 0x11);
  cs_clickContainer(server);
  net_finish_packet(server);
}

// Nobody else changed: slot 255 of player 0 is 510 bytes into its items,
// which is inside player 2 (PlayerData is 221 bytes)
static int othersUntouched (void) {
  static const uint8_t zero[sizeof(PlayerData)] = { 0 };
  for (int i = 1; i < 4; i ++) {
    PlayerData copy = player_data[i];
    copy.client_fd = 0;
    if (memcmp(&copy, zero, sizeof(copy))) return 0;
  }
  return 1;
}

static PlayerData *setupWithNeighbours (void) {
  PlayerData *p = setup(0);
  for (int i = 1; i < 4; i ++) { memset(&player_data[i], 0, sizeof(PlayerData)); player_data[i].client_fd = -1; }
  return p;
}

// A window the server does not know maps every slot to 255
static void test_unknown_window_writes_nothing (void) {
  PlayerData *p = setupWithNeighbours();
  click(7, 3, I_dirt, 5);
  CHECK(othersUntouched());
  for (int i = 0; i < 41; i ++) CHECK(p->inventory_items[i] == 0 && p->inventory_count[i] == 0);
}

// The inventory's own result slot (0) has no server slot either
static void test_result_slot_writes_nothing (void) {
  PlayerData *p = setupWithNeighbours();
  click(0, 0, I_oak_planks, 4);
  CHECK(othersUntouched());
  for (int i = 0; i < 41; i ++) CHECK(p->inventory_items[i] == 0);
}

// A real slot still works: hotbar slot 0 is client slot 36
static void test_hotbar_click_lands (void) {
  PlayerData *p = setup(0);
  click(0, 36, I_dirt, 5);
  CHECK(p->inventory_items[0] == I_dirt && p->inventory_count[0] == 5);
}

int main (void) {
  RUN(test_hotbar_click_lands);
  RUN(test_unknown_window_writes_nothing);
  RUN(test_result_slot_writes_nothing);
  return CHECK_DONE();
}

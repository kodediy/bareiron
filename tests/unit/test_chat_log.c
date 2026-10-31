// The chat the host can show: the last lines, oldest first
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "check.h"
#include "bareiron.h"
#include "globals.h"
#include "status.h"
#include "registries.h"
#include "net.h"
#include "varnum.h"
#include "packets.h"
#include "entity.h"
#include "combat.h"

static void setup (void) {
  static BareironPlatform plat;
  plat = *bareiron_posix_platform();
  platform = &plat;
  statusReset();
}

static void test_keeps_the_last_lines_in_order (void) {
  setup();
  char line[32];
  for (int i = 1; i <= 10; i ++) {
    snprintf(line, sizeof(line), "<Kody> hola %d", i);
    statusChat(line);
  }
  char out[BAREIRON_CHAT_LINES][BAREIRON_CHAT_LEN];
  uint32_t seq = 0;
  int n = bareiron_get_chat(out, BAREIRON_CHAT_LINES, &seq);
  CHECK(n == BAREIRON_CHAT_LINES);
  CHECK(seq == 10);
  CHECK(strcmp(out[0], "<Kody> hola 3") == 0);                       // the oldest kept
  CHECK(strcmp(out[BAREIRON_CHAT_LINES - 1], "<Kody> hola 10") == 0); // the newest last
}

static void test_fewer_lines_and_a_smaller_ask (void) {
  setup();
  statusChat("Kody joined the game");
  statusChat("<Kody> buenas");
  char out[BAREIRON_CHAT_LINES][BAREIRON_CHAT_LEN];
  uint32_t seq;
  CHECK(bareiron_get_chat(out, BAREIRON_CHAT_LINES, &seq) == 2 && seq == 2);
  CHECK(strcmp(out[1], "<Kody> buenas") == 0);
  CHECK(bareiron_get_chat(out, 1, &seq) == 1);
  CHECK(strcmp(out[0], "<Kody> buenas") == 0);                      // with room for one: the newest
}

static void test_a_long_line_is_cut (void) {
  setup();
  char long_line[400];
  memset(long_line, 'a', sizeof(long_line) - 1);
  long_line[sizeof(long_line) - 1] = '\0';
  statusChat(long_line);
  char out[BAREIRON_CHAT_LINES][BAREIRON_CHAT_LEN];
  uint32_t seq;
  CHECK(bareiron_get_chat(out, BAREIRON_CHAT_LINES, &seq) == 1);
  CHECK(strlen(out[0]) == BAREIRON_CHAT_LEN - 1);
}

static int last_is (const char *want) {
  char out[BAREIRON_CHAT_LINES][BAREIRON_CHAT_LEN];
  uint32_t seq;
  int n = bareiron_get_chat(out, BAREIRON_CHAT_LINES, &seq);
  if (n == 0) return 0;
  if (strcmp(out[n - 1], want)) { printf("  last line: \"%s\"\n", out[n - 1]); return 0; }
  return 1;
}

// A player who dies: the line everyone was shown
static void test_a_death_is_logged (void) {
  setup();
  ent_free();
  CHECK(ent_init(16) == 0);
  for (int i = 0; i < MAX_PLAYERS; i ++) player_data[i].client_fd = -1;
  PlayerData *p = &player_data[0];
  memset(p, 0, sizeof(*p));
  p->client_fd = 1000;
  strcpy(p->name, "Kody");
  p->health = 20;
  combat_forget_player(p);
  combat_hurt(p->client_fd, -5, D_generic, 100);
  CHECK(p->health == 0);
  CHECK(last_is("Kody was slain by a mob"));
}

// What a player types goes in as the others saw it
static void test_a_message_is_logged (void) {
  setup();
  int fds[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
  fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);
  CHECK(net_open(fds[0]) == 0);
  for (int i = 0; i < MAX_PLAYERS; i ++) player_data[i].client_fd = -1;
  PlayerData *p = &player_data[0];
  memset(p, 0, sizeof(*p));
  p->client_fd = fds[0];
  strcpy(p->name, "Manu");
  const char *text = "buenas, chavales";
  uint8_t frame[64]; int n = 2;
  frame[1] = 0x08;
  frame[n ++] = (uint8_t)strlen(text);
  memcpy(frame + n, text, strlen(text)); n += (int)strlen(text);
  frame[0] = (uint8_t)(n - 1);
  CHECK(write(fds[1], frame, n) == n);
  int length;
  CHECK(net_pump(fds[0]) == 0);
  CHECK(net_next_packet(fds[0], &length) == 1);
  CHECK(readVarInt(fds[0]) == 0x08);
  cs_chat(fds[0]);
  net_finish_packet(fds[0]);
  CHECK(last_is("<Manu> buenas, chavales"));
  net_close(fds[0]); close(fds[0]); close(fds[1]);
}

int main (void) {
  RUN(test_keeps_the_last_lines_in_order);
  RUN(test_fewer_lines_and_a_smaller_ask);
  RUN(test_a_long_line_is_cut);
  RUN(test_a_death_is_logged);
  RUN(test_a_message_is_logged);
  return CHECK_DONE();
}

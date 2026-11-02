// The status and the chat pushed to the host: on_status after each refresh,
// on_chat for each line, both the same as the pull calls answer
#include <string.h>

#include "check.h"
#include "bareiron.h"
#include "globals.h"
#include "status.h"
#include "entity.h"

static int status_calls;
static BareironStatus last_status;
static int chat_calls;
static char last_line[BAREIRON_CHAT_LEN];
static uint32_t last_seq;
static int events;
static int status_before_event;   // on_status had the player in before on_event came

static void on_status (const BareironStatus *s) { status_calls ++; last_status = *s; }
static void on_chat (const char *line, uint32_t seq) {
  chat_calls ++;
  strcpy(last_line, line);
  last_seq = seq;
}
static void on_event (const BareironEvent *e) {
  events ++;
  if (e->type == BAREIRON_EVT_PLAYER_JOIN && last_status.player_count == 1 &&
      strcmp(last_status.players[0].name, e->name) == 0) status_before_event ++;
}

static void setup (int hooks) {
  static BareironPlatform plat;
  plat = *bareiron_posix_platform();
  if (hooks) {
    plat.on_status = on_status;
    plat.on_chat = on_chat;
    plat.on_event = on_event;
  }
  platform = &plat;
  statusReset();
  status_calls = chat_calls = events = status_before_event = 0;
  memset(&last_status, 0, sizeof(last_status));
  for (int i = 0; i < MAX_PLAYERS; i ++) player_data[i].client_fd = -1;
  ent_free();
  CHECK(ent_init(16) == 0);
}

static void test_each_chat_line_is_pushed (void) {
  setup(1);
  statusChat("Kody joined the game");
  statusChat("<Kody> buenas");
  CHECK(chat_calls == 2);
  CHECK(strcmp(last_line, "<Kody> buenas") == 0);
  CHECK(last_seq == 2);
  char out[BAREIRON_CHAT_LINES][BAREIRON_CHAT_LEN];
  uint32_t seq;
  int n = bareiron_get_chat(out, BAREIRON_CHAT_LINES, &seq);
  CHECK(seq == last_seq && strcmp(out[n - 1], last_line) == 0);
  char long_line[400];
  memset(long_line, 'a', sizeof(long_line) - 1);
  long_line[sizeof(long_line) - 1] = '\0';
  statusChat(long_line);
  CHECK(strlen(last_line) == BAREIRON_CHAT_LEN - 1);   // cut as the pull call keeps it
}

static void test_the_status_is_pushed_with_the_seed (void) {
  setup(1);
  world_seed = 0xCAFEF00Du;
  statusSetRunning(1);
  CHECK(status_calls == 1);
  CHECK(last_status.running == 1);
  CHECK(last_status.world_seed == 0xCAFEF00Du);
  world_time = 6000;
  statusUpdate(1234);
  CHECK(status_calls == 2);
  CHECK(last_status.world_time == 6000 && last_status.loop_max_us == 1234);
  BareironStatus pulled;
  bareiron_get_status(&pulled);
  CHECK(memcmp(&pulled, &last_status, sizeof(pulled)) == 0);
  statusSetRunning(0);
  CHECK(status_calls == 3 && last_status.running == 0);
}

// A join is pushed before its event, so a host has the player when JOIN comes
static void test_a_join_is_pushed_before_its_event (void) {
  setup(1);
  statusSetRunning(1);
  PlayerData *p = &player_data[0];
  memset(p, 0, sizeof(*p));
  p->client_fd = 1000;
  strcpy(p->name, "Kody");
  p->x = 12; p->z = -7;
  statusPlayerJoined(p);
  CHECK(status_calls == 2);
  CHECK(events == 1 && status_before_event == 1);
  CHECK(last_status.players[0].x == 12 && last_status.players[0].z == -7);
  p->client_fd = -1;
  statusPlayerLeft(p);
  CHECK(status_calls == 3 && last_status.player_count == 0);
}

// Without the hooks nothing changes for a host that pulls
static void test_no_hooks (void) {
  setup(0);
  statusSetRunning(1);
  statusChat("<Kody> hola");
  statusUpdate(5);
  CHECK(status_calls == 0 && chat_calls == 0);
  BareironStatus pulled;
  bareiron_get_status(&pulled);
  CHECK(pulled.running == 1 && pulled.loop_max_us == 5);
}

int main (void) {
  RUN(test_each_chat_line_is_pushed);
  RUN(test_the_status_is_pushed_with_the_seed);
  RUN(test_a_join_is_pushed_before_its_event);
  RUN(test_no_hooks);
  return CHECK_DONE();
}

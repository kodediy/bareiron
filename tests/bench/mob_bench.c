// What a server tick costs: 6 players walking around, and (from Plan 5's
// Task 6 on) the mobs around them. Mac numbers to compare steps; the P4 is
// measured on the board.
//   cc -O2 -Iinclude -Ithird_party/miniz -DMINIZ_NO_STDIO -DMINIZ_NO_TIME \
//      -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES \
//      tests/bench/mob_bench.c $(ls src/*.c | grep -v main.c) third_party/miniz/miniz.c -o build/mob_bench
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include "bareiron.h"
#include "globals.h"
#include "blockindex.h"
#include "net.h"
#include "procedures.h"

static int64_t ns (void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec; }

int main (void) {
  static BareironPlatform p;
  p = *bareiron_posix_platform();
  platform = &p;
  world_seed = 0x432B506Fu;
  block_changes_cap = 1000;
  block_changes = calloc(block_changes_cap, sizeof(BlockChange));
  for (uint32_t i = 0; i < block_changes_cap; i ++) block_changes[i].block = 0xFF;
  bc_index_init(block_changes_cap);

  // 6 players, each on a socketpair whose far end is drained
  int far[6];
  for (int i = 0; i < MAX_PLAYERS; i ++) player_data[i].client_fd = -1;
  for (int i = 0; i < 6; i ++) {
    int sv[2]; socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    net_open(sv[0]);
    far[i] = sv[1];
    fcntl(sv[1], F_SETFL, O_NONBLOCK);   // draining must never wait
    PlayerData *pl = &player_data[i];
    memset(pl, 0, sizeof(*pl));
    pl->client_fd = sv[0];
    pl->x = (short)(i * 40); pl->y = 70; pl->z = 0;
    pl->health = 20; pl->hunger = 20;
  }

  const int ticks = 20 * 60;   // one minute of game
  int64_t worst = 0, total = 0;
  char sink[65536];
  for (int t = 0; t < ticks; t ++) {
    for (int i = 0; i < 6; i ++) player_data[i].x += (t & 1);
    int64_t t0 = ns();
    handleServerTick(TIME_BETWEEN_TICKS);
    int64_t dt = ns() - t0;
    total += dt; if (dt > worst) worst = dt;
    for (int i = 0; i < 6; i ++) { net_flush(player_data[i].client_fd); while (read(far[i], sink, sizeof(sink)) > 0) {} }
  }
  printf("tick with 6 players: mean %.1f us, worst %.1f us (%d ticks)\n", total / 1e3 / ticks, worst / 1e3, ticks);
  return 0;
}

// Paths over the block grid, within a node budget
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "bareiron.h"
#include "globals.h"
#include "registries.h"
#include "blockindex.h"
#include "procedures.h"
#include "worldgen.h"
#include "pathfind.h"

#define FLOOR 200   // up here the generator leaves sky

static void setup (void) {
  static BareironPlatform plat;
  plat = *bareiron_posix_platform();
  platform = &plat;
  world_seed = 0x1234567u;
  free(block_changes);
  block_changes_cap = 20000;
  block_changes = malloc(block_changes_cap * sizeof(BlockChange));
  for (uint32_t i = 0; i < block_changes_cap; i ++) block_changes[i].block = 0xFF;
  block_changes_count = 0;
  bc_index_free();
  CHECK(bc_index_init(block_changes_cap) == 0);
  for (short x = 0; x < 30; x ++) for (short z = 0; z < 30; z ++) makeBlockChange(x, FLOOR, z, B_stone);
}

static PathNode node (short x, uint8_t y, short z) { PathNode n = { x, y, z }; return n; }

// Each step is a neighbour: one block across (or diagonal), up one, or down at most three
static int connected (PathNode from, const PathNode *path, int n) {
  PathNode prev = from;
  for (int i = 0; i < n; i ++) {
    int dx = abs(path[i].x - prev.x), dz = abs(path[i].z - prev.z), dy = path[i].y - prev.y;
    if (dx > 1 || dz > 1 || dx + dz == 0 || dy > 1 || dy < -3) return 0;
    if (!path_standable(path[i].x, path[i].y, path[i].z)) return 0;
    prev = path[i];
  }
  return 1;
}

static void test_path_straight (void) {
  setup();
  PathNode out[64];
  int budget = PATH_TICK_BUDGET;
  int n = path_find(node(5, FLOOR + 1, 5), node(15, FLOOR + 1, 5), out, 64, &budget);
  CHECK(n == 10);
  CHECK(connected(node(5, FLOOR + 1, 5), out, n));
  CHECK(out[n - 1].x == 15 && out[n - 1].z == 5);
  CHECK(path_find(node(5, FLOOR + 1, 5), node(5, FLOOR + 1, 5), out, 64, &budget) == 0);
}

static void test_path_around_wall (void) {
  setup();
  // a wall 5 wide and 3 high (too high to jump) across the way
  for (short z = 3; z <= 7; z ++) for (uint8_t y = FLOOR + 1; y <= FLOOR + 3; y ++) makeBlockChange(10, y, z, B_stone);
  PathNode out[64];
  int budget = PATH_TICK_BUDGET;
  int n = path_find(node(5, FLOOR + 1, 5), node(15, FLOOR + 1, 5), out, 64, &budget);
  CHECK(n > 0);
  CHECK(connected(node(5, FLOOR + 1, 5), out, n));
  CHECK(out[n - 1].x == 15 && out[n - 1].z == 5);
  // the best way round is 10 steps across and 3 sideways, done diagonally: at most 4 more than that
  CHECK(n <= 10 + 4);
}

static void test_path_up_steps (void) {
  setup();
  // three steps up, then a landing
  for (short x = 8; x <= 12; x ++) {
    int top = x - 8 + 1; if (top > 3) top = 3;
    for (int y = 1; y <= top; y ++) makeBlockChange(x, FLOOR + y, 5, B_stone);
  }
  PathNode out[64];
  int budget = PATH_TICK_BUDGET;
  int n = path_find(node(5, FLOOR + 1, 5), node(12, FLOOR + 4, 5), out, 64, &budget);
  CHECK(n > 0);
  CHECK(connected(node(5, FLOOR + 1, 5), out, n));
  CHECK(out[n - 1].x == 12 && out[n - 1].y == FLOOR + 4);
}

static void test_path_respects_budget (void) {
  setup();
  // the target is shut in: stone all round it and over it
  for (short x = 19; x <= 21; x ++) for (short z = 19; z <= 21; z ++) for (uint8_t y = FLOOR + 1; y <= FLOOR + 3; y ++)
    if (!(x == 20 && z == 20 && y <= FLOOR + 2)) makeBlockChange(x, y, z, B_stone);
  PathNode out[64];
  int budget = PATH_TICK_BUDGET;
  CHECK(path_find(node(5, FLOOR + 1, 5), node(20, FLOOR + 1, 20), out, 64, &budget) == -1);
  CHECK(budget >= 0);
  CHECK(PATH_TICK_BUDGET - budget <= PATH_SEARCH_MAX);
  budget = 3;
  CHECK(path_find(node(5, FLOOR + 1, 5), node(25, FLOOR + 1, 25), out, 64, &budget) == PATH_NO_BUDGET);   // try again later
  CHECK(budget == 0);
}

static void test_path_never_through_lava (void) {
  setup();
  // a strip of lava across the way, with a stone bridge at z = 20
  for (short z = 0; z < 30; z ++) if (z != 20) makeBlockChange(10, FLOOR, z, B_lava);
  PathNode out[128];
  int budget = PATH_TICK_BUDGET;
  int n = path_find(node(5, FLOOR + 1, 15), node(15, FLOOR + 1, 15), out, 128, &budget);
  CHECK(n > 0);
  CHECK(connected(node(5, FLOOR + 1, 15), out, n));
  for (int i = 0; i < n; i ++) {
    uint8_t below = getBlockAt(out[i].x, out[i].y - 1, out[i].z);
    CHECK(!(below >= B_lava && below < B_lava + 4));
    CHECK(!(out[i].x == 10 && out[i].z != 20));
  }
}

// The pathfinder keeps the blocks it looked at, but only within one search
static void test_path_sees_new_blocks (void) {
  setup();
  PathNode out[64];
  int budget = PATH_TICK_BUDGET;
  int n = path_find(node(5, FLOOR + 1, 5), node(15, FLOOR + 1, 5), out, 64, &budget);
  CHECK(n == 10);
  for (short z = 0; z < 30; z ++) for (uint8_t y = FLOOR + 1; y <= FLOOR + 3; y ++) makeBlockChange(10, y, z, B_stone);
  budget = PATH_TICK_BUDGET;
  CHECK(path_find(node(5, FLOOR + 1, 5), node(15, FLOOR + 1, 5), out, 64, &budget) == -1);   // walled off now
  CHECK(path_standable(10, FLOOR + 4, 5));        // outside a search: the live world
}

int main (void) {
  RUN(test_path_straight);
  RUN(test_path_around_wall);
  RUN(test_path_up_steps);
  RUN(test_path_respects_budget);
  RUN(test_path_never_through_lava);
  RUN(test_path_sees_new_blocks);
  return CHECK_DONE();
}

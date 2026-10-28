// getBlockAt remembers what it computed; a block change or a new world forgets it
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "bareiron.h"
#include "globals.h"
#include "registries.h"
#include "blockindex.h"
#include "procedures.h"
#include "worldgen.h"

static void setup (uint32_t seed) {
  static BareironPlatform plat;
  plat = *bareiron_posix_platform();
  platform = &plat;
  world_seed = seed;
  free(block_changes);
  block_changes_cap = 4000;
  block_changes = malloc(block_changes_cap * sizeof(BlockChange));
  for (uint32_t i = 0; i < block_changes_cap; i ++) block_changes[i].block = 0xFF;
  block_changes_count = 0;
  bc_index_free();
  CHECK(bc_index_init(block_changes_cap) == 0);
}

static void test_cached_matches_uncached (void) {
  setup(0x1234567u);
  uint32_t r = 12345;
  int mismatches = 0;
  for (int i = 0; i < 20000; i ++) {
    r = r * 1103515245u + 12345u;
    int x = (int)(r % 64) - 32, z = (int)((r >> 8) % 64) - 32, y = 40 + (int)((r >> 16) % 50);
    if (i % 50 == 0) makeBlockChange((short)x, (uint8_t)y, (short)z, (r >> 4) & 1 ? B_stone : B_air);
    if (getBlockAt(x, y, z) != getBlockAtUncached(x, y, z)) mismatches ++;
  }
  CHECK(mismatches == 0);
}

static void test_change_is_seen_at_once (void) {
  setup(0x1234567u);
  int y = getHeightAt(5, 5) + 3;
  CHECK(getBlockAt(5, y, 5) == B_air);         // warm the cache with air
  makeBlockChange(5, (uint8_t)y, 5, B_stone);
  CHECK(getBlockAt(5, y, 5) == B_stone);
  makeBlockChange(5, (uint8_t)y, 5, B_air);
  CHECK(getBlockAt(5, y, 5) == B_air);
}

static void test_new_world_forgets (void) {
  setup(0x1111u);
  int differ = 0;
  uint8_t before[64];
  for (int i = 0; i < 64; i ++) before[i] = getBlockAt(i * 3, 60, i * 5);
  setup(0x22222222u);                          // another seed, as a new world would be
  for (int i = 0; i < 64; i ++) {
    uint8_t b = getBlockAt(i * 3, 60, i * 5);
    CHECK(b == getBlockAtUncached(i * 3, 60, i * 5));
    if (b != before[i]) differ ++;
  }
  CHECK(differ > 0);
}

int main (void) {
  RUN(test_cached_matches_uncached);
  RUN(test_change_is_seen_at_once);
  RUN(test_new_world_forgets);
  return CHECK_DONE();
}

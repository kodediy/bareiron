// Explosions: craters, damage, a full block store
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "bareiron.h"
#include "globals.h"
#include "registries.h"
#include "blockindex.h"
#include "procedures.h"
#include "worldgen.h"
#include "entity.h"
#include "mobs.h"
#include "explosion.h"

#define BASE 200   // a cube of material up in the sky
#define C 8        // its centre, in x and z

static void setup (uint32_t cap) {
  static BareironPlatform plat;
  plat = *bareiron_posix_platform();
  platform = &plat;
  world_seed = 0x1234567u;
  free(block_changes);
  block_changes_cap = cap;
  block_changes = malloc(block_changes_cap * sizeof(BlockChange));
  for (uint32_t i = 0; i < block_changes_cap; i ++) block_changes[i].block = 0xFF;
  block_changes_count = 0;
  bc_index_free();
  CHECK(bc_index_init(block_changes_cap) == 0);
  ent_free();
  CHECK(ent_init(512) == 0);
  for (int i = 0; i < MAX_PLAYERS; i ++) player_data[i].client_fd = -1;
}

static void cube (uint8_t block) {
  for (short x = 0; x <= 2 * C; x ++) for (short z = 0; z <= 2 * C; z ++) for (int y = 0; y <= 2 * C; y ++)
    makeBlockChange(x, BASE + y, z, block);
}

// How many blocks of the cube are air now, and how far the furthest is from the centre
static int crater (double *furthest) {
  int n = 0;
  *furthest = 0;
  for (short x = 0; x <= 2 * C; x ++) for (short z = 0; z <= 2 * C; z ++) for (int y = 0; y <= 2 * C; y ++) {
    if (getBlockAt(x, BASE + y, z) != B_air) continue;
    if (x == C && z == C && y == C) continue;   // where the TNT was
    n ++;
    double d = sqrt((x - C) * (x - C) + (y - C) * (y - C) + (z - C) * (z - C));
    if (d > *furthest) *furthest = d;
  }
  return n;
}

static void test_tnt_crater_size (void) {
  double stone_r, dirt_r;
  setup(20000);
  cube(B_stone);
  makeBlockChange(C, BASE + C, C, B_air);
  explode((int32_t)((C + 0.5) * ENT_UNIT), (int32_t)((BASE + C + 0.5) * ENT_UNIT), (int32_t)((C + 0.5) * ENT_UNIT), TNT_POWER, -1);
  int stone = crater(&stone_r);
  setup(20000);
  cube(B_dirt);
  makeBlockChange(C, BASE + C, C, B_air);
  explode((int32_t)((C + 0.5) * ENT_UNIT), (int32_t)((BASE + C + 0.5) * ENT_UNIT), (int32_t)((C + 0.5) * ENT_UNIT), TNT_POWER, -1);
  int dirt = crater(&dirt_r);
  printf("  stone: %d blocks, out to %.1f; dirt: %d blocks, out to %.1f\n", stone, stone_r, dirt, dirt_r);
  CHECK(stone >= 1 && stone_r <= 3.5);
  CHECK(dirt > stone);
  CHECK(dirt_r >= 2 && dirt_r <= 4.5);
}

static void test_explosion_with_full_change_store (void) {
  // Natural terrain: every block it breaks needs a new change, and there is room for one
  setup(1);
  short x = 40, z = 40;
  int y = getHeightAt(x, z) - 3;
  explode((int32_t)((x + 0.5) * ENT_UNIT), (int32_t)((y + 0.5) * ENT_UNIT), (int32_t)((z + 0.5) * ENT_UNIT), TNT_POWER, -1);
  CHECK((uint32_t)block_changes_count <= block_changes_cap);
  int air = 0;
  for (uint32_t i = 0; i < block_changes_cap; i ++) if (block_changes[i].block == B_air) air ++;
  CHECK(air <= 1);
}

static void test_explosion_damage_falls_with_distance (void) {
  setup(20000);
  for (short x = 0; x < 40; x ++) for (short z = 0; z < 10; z ++) makeBlockChange(x, BASE, z, B_stone);
  Entity *near = mob_spawn(E_cow, (int32_t)(12.5 * ENT_UNIT), (BASE + 1) * ENT_UNIT, (int32_t)(5.5 * ENT_UNIT));
  Entity *far = mob_spawn(E_cow, (int32_t)(15.5 * ENT_UNIT), (BASE + 1) * ENT_UNIT, (int32_t)(5.5 * ENT_UNIT));
  Entity *out = mob_spawn(E_cow, (int32_t)(30.5 * ENT_UNIT), (BASE + 1) * ENT_UNIT, (int32_t)(5.5 * ENT_UNIT));
  explode((int32_t)(10.5 * ENT_UNIT), (BASE + 1) * ENT_UNIT, (int32_t)(5.5 * ENT_UNIT), CREEPER_POWER, -1);
  CHECK(near->health < far->health || (near->flags & ENT_DEAD));
  CHECK(far->health < 10);
  CHECK(out->health == 10);
  CHECK(near->vx > 0);          // pushed away from the blast
}

static void test_every_block_has_resistance (void) {
  CHECK(block_blast_resistance(B_stone) == 6.0f);
  CHECK(block_blast_resistance(B_dirt) == 0.5f);
  CHECK(block_blast_resistance(B_obsidian) == 1200.0f);
  CHECK(block_blast_resistance(B_water) == 100.0f);
  CHECK(block_blast_resistance(B_bedrock) > 1e6f);
  CHECK(block_blast_resistance(B_air) == 0.0f);
}

int main (void) {
  RUN(test_tnt_crater_size);
  RUN(test_explosion_with_full_change_store);
  RUN(test_explosion_damage_falls_with_distance);
  RUN(test_every_block_has_resistance);
  return CHECK_DONE();
}

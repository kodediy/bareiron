// The terrain of a seed given as an argument: the same as the server's while
// world_seed holds it, and world_seed and the block cache are left alone
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "bareiron.h"
#include "globals.h"
#include "registries.h"
#include "blockindex.h"
#include "procedures.h"
#include "tools.h"
#include "worldgen.h"

static const uint32_t seeds[] = {
  0u, 1u, 0x1234567u, 0x9E3779B9u, 0xCAFEF00Du, 0xDEADBEEFu, 0x80000000u, 0xFFFFFFFFu
};
#define SEED_COUNT (int)(sizeof(seeds) / sizeof(seeds[0]))

// Columns spread over both signs, chunk edges and a few far ones
static void column (int i, int *x, int *z) {
  uint32_t r = 2463534242u + (uint32_t)i * 2654435761u;
  r ^= r << 13; r ^= r >> 17; r ^= r << 5;
  *x = (int)(r % 512) - 256;
  *z = (int)((r >> 9) % 512) - 256;
  if (i % 7 == 0) { *x *= 97; *z *= 89; }
}

static ChunkAnchor anchor_at (int x, int z, uint32_t seed, int seeded) {
  short ax = div_floor(x, CHUNK_SIZE), az = div_floor(z, CHUNK_SIZE);
  ChunkAnchor a = { .x = ax, .z = az };
  if (seeded) {
    a.hash = getChunkHashSeeded(ax, az, seed);
    a.biome = getChunkBiomeSeeded(ax, az, seed);
  } else {
    a.hash = getChunkHash(ax, az);
    a.biome = getChunkBiome(ax, az);
  }
  return a;
}

// FNV-1a over the hash, biome, height, feature and a column of blocks of
// `n` columns: with the seeded functions, or the server's under world_seed
static uint32_t digest (uint32_t seed, int seeded, int n) {
  uint32_t h = 2166136261u;
  #define MIX(v) do { h ^= (uint32_t)(v); h *= 16777619u; } while (0)
  for (int i = 0; i < n; i ++) {
    int x, z;
    column(i, &x, &z);
    ChunkAnchor a = anchor_at(x, z, seed, seeded);
    MIX(a.hash); MIX(a.biome);
    int rx = mod_abs(x, CHUNK_SIZE), rz = mod_abs(z, CHUNK_SIZE);
    ChunkFeature f;
    if (seeded) {
      MIX(getHeightAtSeeded(x, z, seed));
      MIX(getHeightAtFromHashSeeded(rx, rz, a.x, a.z, a.hash, a.biome, seed));
      f = getFeatureFromAnchorSeeded(a, seed);
    } else {
      MIX(getHeightAt(x, z));
      MIX(getHeightAtFromHash(rx, rz, a.x, a.z, a.hash, a.biome));
      f = getFeatureFromAnchor(a);
    }
    MIX(f.x); MIX(f.y); MIX(f.z);
    if (f.y != 255) MIX(f.variant);
    for (int y = 0; y <= 82; y ++) {
      if (seeded) MIX(getTerrainAtSeeded(x, y, z, a, seed));
      else MIX(getTerrainAt(x, y, z, a));
    }
  }
  #undef MIX
  return h;
}

#define COLUMNS 3000

// What the terrain was before the seed became an argument (worked out with
// the functions as they were, at 3891c3e): saves and terrain do not change
static const uint32_t golden[SEED_COUNT] = {
  0x4873AA31u, 0xB42FC92Au, 0x96B1C474u, 0x2E8BFBA2u,
  0x52F6B5B4u, 0x703BCCBDu, 0xF1A7AD7Au, 0xDE0330C9u
};

static void test_the_server_terrain_did_not_change (void) {
  for (int s = 0; s < SEED_COUNT; s ++) {
    world_seed = seeds[s];
    uint32_t d = digest(seeds[s], 0, COLUMNS);
    if (d != golden[s]) printf("  seed %08x: %08x, was %08x\n", seeds[s], d, golden[s]);
    CHECK(d == golden[s]);
  }
}

static void test_seeded_is_the_server_terrain (void) {
  for (int s = 0; s < SEED_COUNT; s ++) {
    world_seed = 0x5EED0000u ^ seeds[s];   // anything else: the seeded calls must not read it
    CHECK(digest(seeds[s], 1, COLUMNS) == golden[s]);
    world_seed = seeds[s];
    CHECK(digest(seeds[s], 1, COLUMNS) == digest(seeds[s], 0, COLUMNS));
  }
}

// Column by column, not only a digest: seed B's terrain while world_seed holds A
static void test_another_seed_while_the_world_runs (void) {
  const uint32_t A = 0x1234567u, B = 0xCAFEF00Du;
  int differs = 0;
  for (int i = 0; i < 400; i ++) {
    int x, z;
    column(i, &x, &z);
    world_seed = B;
    ChunkAnchor ab = anchor_at(x, z, 0, 0);
    uint8_t height_b = getHeightAt(x, z);
    uint8_t blocks_b[83];
    for (int y = 0; y <= 82; y ++) blocks_b[y] = getTerrainAt(x, y, z, ab);
    world_seed = A;
    ChunkAnchor sb = anchor_at(x, z, B, 1);
    CHECK(sb.hash == ab.hash && sb.biome == ab.biome);
    CHECK(getHeightAtSeeded(x, z, B) == height_b);
    for (int y = 0; y <= 82; y ++) CHECK(getTerrainAtSeeded(x, y, z, sb, B) == blocks_b[y]);
    CHECK(world_seed == A);
    if (getHeightAt(x, z) != height_b) differs ++;
  }
  CHECK(differs > 0);   // A and B are not the same terrain, or this proves nothing
}

// The seeded calls leave getBlockAt's cache and the block changes alone:
// the server's world reads the same before and after
static void test_the_block_cache_is_left_alone (void) {
  static BareironPlatform plat;
  plat = *bareiron_posix_platform();
  platform = &plat;
  free(block_changes);
  block_changes_cap = 64;
  block_changes = malloc(block_changes_cap * sizeof(BlockChange));
  for (uint32_t i = 0; i < block_changes_cap; i ++) block_changes[i].block = 0xFF;
  block_changes_count = 0;
  bc_index_free();
  CHECK(bc_index_init(block_changes_cap) == 0);
  const uint32_t A = 0x1234567u, B = 0xDEADBEEFu;
  world_seed = A;
  blockCacheForget();
  uint8_t before[32][20];
  for (int i = 0; i < 32; i ++) {
    for (int y = 0; y < 20; y ++) before[i][y] = getBlockAt(i * 3 - 40, 55 + y, 17 - i * 5);
  }
  int changes = block_changes_count;
  for (int i = 0; i < 32; i ++) {
    int x = i * 3 - 40, z = 17 - i * 5;
    ChunkAnchor b = anchor_at(x, z, B, 1);
    for (int y = 55; y < 75; y ++) (void)getTerrainAtSeeded(x, y, z, b, B);
  }
  CHECK(world_seed == A);
  CHECK(block_changes_count == changes);
  for (int i = 0; i < 32; i ++) {
    for (int y = 0; y < 20; y ++) {
      CHECK(getBlockAt(i * 3 - 40, 55 + y, 17 - i * 5) == before[i][y]);
      CHECK(getBlockAtUncached(i * 3 - 40, 55 + y, 17 - i * 5) == before[i][y]);
    }
  }
}

int main (void) {
  RUN(test_the_server_terrain_did_not_change);
  RUN(test_seeded_is_the_server_terrain);
  RUN(test_another_seed_while_the_world_runs);
  RUN(test_the_block_cache_is_left_alone);
  return CHECK_DONE();
}

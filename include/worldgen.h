#ifndef H_WORLDGEN
#define H_WORLDGEN

#include <stdint.h>

typedef struct {
  short x;
  short z;
  uint32_t hash;
  uint8_t biome;
} ChunkAnchor;

typedef struct {
  short x;
  uint8_t y;
  short z;
  uint8_t variant;
} ChunkFeature;

// The world in world_seed: the server's
uint32_t getChunkHash (short x, short z);
uint8_t getChunkBiome (short x, short z);
uint8_t getHeightAtFromHash (int rx, int rz, int _x, int _z, uint32_t chunk_hash, uint8_t biome);
uint8_t getHeightAt (int x, int z);
ChunkFeature getFeatureFromAnchor (ChunkAnchor anchor);
uint8_t getTerrainAt (int x, int y, int z, ChunkAnchor anchor);

// The same terrain for any seed, the same as the functions above give while
// world_seed holds it. They read no global, no block change and no cache
// (generated terrain only), so a host can draw any world from another thread
// while the server runs, and world_seed is only ever the server's.
uint32_t getChunkHashSeeded (short x, short z, uint32_t seed);
uint8_t getChunkBiomeSeeded (short x, short z, uint32_t seed);
uint8_t getHeightAtFromHashSeeded (int rx, int rz, int _x, int _z, uint32_t chunk_hash, uint8_t biome, uint32_t seed);
uint8_t getHeightAtSeeded (int x, int z, uint32_t seed);
ChunkFeature getFeatureFromAnchorSeeded (ChunkAnchor anchor, uint32_t seed);
uint8_t getTerrainAtSeeded (int x, int y, int z, ChunkAnchor anchor, uint32_t seed);
uint8_t getBlockAt (int x, int y, int z);          // cached; see blockCacheForget
uint8_t getBlockAtUncached (int x, int y, int z);  // worked out every time
void blockCacheForget (void);                      // a block changed or the world did

// 4096 bytes, one per block of a section, from tables_alloc
extern uint8_t *chunk_section;
uint8_t buildChunkSection (int cx, int cy, int cz);
uint8_t chunkTopY (short cx, short cz); // nothing but air above this Y in chunk (cx, cz)

#endif

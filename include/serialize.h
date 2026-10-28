#ifndef SERIALIZE_H
#define SERIALIZE_H

#include <stddef.h>
#include <stdint.h>

#include "globals.h"

// The world is saved as one snapshot: a header, the block changes in use
// and every remembered player. Changes only mark the world as dirty; the
// snapshot goes to platform->save_world every WORLD_SAVE_INTERVAL and
// when the server stops, never once per block.

#define WORLD_MAGIC 0x4B435731u   // "KCW1"
#define WORLD_VERSION 2   // 1: no entities, no beds (still read)

#pragma pack(push, 1)
typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint32_t world_seed;
  uint32_t block_changes_count;
  uint32_t player_count;
  uint32_t player_size;
  uint16_t world_time;
} WorldHeader;
// Version 2 adds, after the players: the persistent entities (animals and
// items on the ground) and each player slot's bed
typedef struct {
  uint16_t type;
  uint8_t  flags;
  uint8_t  yaw;
  int32_t  x, y, z;
  float    health;
  uint32_t age;
  uint16_t timer;
  uint16_t item;      // an item: what and how many
  uint8_t  count;
  uint8_t  wool;      // a sheep's colour
  uint16_t grow;      // a baby's ticks so far
  uint16_t breed_cd;
  uint16_t egg;
} EntitySave;

typedef struct {
  uint8_t has;
  short   x;
  uint8_t y;
  short   z;
} BedSave;
#pragma pack(pop)

size_t worldSnapshotSize (void);
size_t worldSnapshotWrite (uint8_t *dst, size_t capacity); // header + block_changes[0..count) + player_data[]
int    worldSnapshotRead (const uint8_t *src, size_t len);  // 0 ok, -5 bad magic/version/size

void markWorldDirty (void);
uint8_t worldIsDirty (void);
int  saveWorldNow (void);       // snapshot into an alloc_large buffer, handed to platform->save_world
void saveWorldIfDue (void);     // saves when dirty and WORLD_SAVE_INTERVAL has passed
int  loadWorld (void);          // 0 loaded, 1 no saved world, -5 unreadable world

// Callers in the game logic only say that something changed
#define writeBlockChangesToDisk(from, to) markWorldDirty()
#define writeChestChangesToDisk(storage_ptr, slot) markWorldDirty()
#define writePlayerDataToDisk() markWorldDirty()
#define writeDataToDiskOnInterval() saveWorldIfDue()

#endif

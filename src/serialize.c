#include <stdio.h>
#include <string.h>

#include "globals.h"
#include "tools.h"
#include "registries.h"
#include "status.h"
#include "serialize.h"
#include "entity.h"
#include "beds.h"
#include "bareiron.h"

static uint8_t world_dirty = false;
static int64_t last_save_time = 0;

void markWorldDirty (void) {
  world_dirty = true;
}

uint8_t worldIsDirty (void) {
  return world_dirty;
}

static uint8_t persistent (const Entity *e) {
  return e->type != 0 && (e->flags & ENT_PERSISTENT) && !(e->flags & ENT_DEAD);
}

static uint32_t persistentCount (void) {
  uint32_t n = 0;
  for (uint32_t i = 0; i < ent_capacity(); i ++) if (persistent(ent_at(i))) n ++;
  return n;
}

size_t worldSnapshotSize (void) {
  return sizeof(WorldHeader)
    + (size_t)block_changes_count * sizeof(BlockChange)
    + MAX_PLAYERS * sizeof(PlayerData)
    + sizeof(uint32_t) + (size_t)persistentCount() * sizeof(EntitySave)
    + MAX_PLAYERS * sizeof(BedSave);
}

size_t worldSnapshotWrite (uint8_t *dst, size_t capacity) {
  size_t size = worldSnapshotSize();
  if (capacity < size) return 0;

  WorldHeader header = {
    .magic = WORLD_MAGIC,
    .version = WORLD_VERSION,
    .world_seed = world_seed,
    .block_changes_count = block_changes_count,
    .player_count = MAX_PLAYERS,
    .player_size = sizeof(PlayerData),
    .world_time = world_time
  };
  memcpy(dst, &header, sizeof(header));
  dst += sizeof(header);

  memcpy(dst, block_changes, (size_t)block_changes_count * sizeof(BlockChange));
  dst += (size_t)block_changes_count * sizeof(BlockChange);

  // Everyone is offline in a saved world
  memcpy(dst, player_data, sizeof(player_data));
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    int offline = -1;
    memcpy(dst + i * sizeof(PlayerData) + offsetof(PlayerData, client_fd), &offline, sizeof(offline));
  }
  dst += sizeof(player_data);

  // Animals and items on the ground; hostiles, arrows and lit TNT are not kept
  uint32_t n = persistentCount();
  memcpy(dst, &n, sizeof(n));
  dst += sizeof(n);
  for (uint32_t i = 0; i < ent_capacity(); i ++) {
    const Entity *e = ent_at(i);
    if (!persistent(e)) continue;
    EntitySave save = {
      .type = e->type, .flags = e->flags, .yaw = e->yaw,
      .x = e->x, .y = e->y, .z = e->z,
      .health = e->health, .age = e->age, .timer = e->timer,
      .item = e->kind == ENT_KIND_ITEM ? e->u.item.item : 0,
      .count = e->kind == ENT_KIND_ITEM ? e->u.item.count : 0,
      .wool = e->kind == ENT_KIND_MOB ? e->u.mob.wool : 0,
      .grow = e->kind == ENT_KIND_MOB ? e->u.mob.grow : 0,
      .breed_cd = e->kind == ENT_KIND_MOB ? e->u.mob.breed_cd : 0,
      .egg = e->kind == ENT_KIND_MOB ? e->u.mob.egg : 0,
    };
    memcpy(dst, &save, sizeof(save));
    dst += sizeof(save);
  }

  for (int i = 0; i < MAX_PLAYERS; i ++) {
    // BedSave is packed: its fields get values, never pointers
    BedSave bed;
    short bx, bz; uint8_t by;
    bed.has = bed_spawn_point(i, &bx, &by, &bz);
    bed.x = bx; bed.y = by; bed.z = bz;
    memcpy(dst, &bed, sizeof(bed));
    dst += sizeof(bed);
  }

  return size;
}

int worldSnapshotRead (const uint8_t *src, size_t len) {
  WorldHeader header;
  if (len < sizeof(header)) return BAREIRON_ERR_BAD_WORLD;
  memcpy(&header, src, sizeof(header));

  if (header.magic != WORLD_MAGIC || (header.version != 1 && header.version != WORLD_VERSION)) return BAREIRON_ERR_BAD_WORLD;
  if (header.player_size != sizeof(PlayerData) || header.player_count > MAX_PLAYERS) return BAREIRON_ERR_BAD_WORLD;
  if (header.block_changes_count > block_changes_cap) return BAREIRON_ERR_BAD_WORLD;
  size_t base = sizeof(header)
    + (size_t)header.block_changes_count * sizeof(BlockChange)
    + (size_t)header.player_count * sizeof(PlayerData);
  uint32_t entities = 0;
  if (header.version == 1) {
    if (len != base) return BAREIRON_ERR_BAD_WORLD;
  } else {
    if (len < base + sizeof(entities)) return BAREIRON_ERR_BAD_WORLD;
    memcpy(&entities, src + base, sizeof(entities));
    if (entities > ent_capacity()) return BAREIRON_ERR_BAD_WORLD;
    if (len != base + sizeof(entities) + (size_t)entities * sizeof(EntitySave) + MAX_PLAYERS * sizeof(BedSave)) {
      return BAREIRON_ERR_BAD_WORLD;
    }
  }
  const uint8_t *extra = src + base + sizeof(entities);
  src += sizeof(header);

  // Every chest needs its 14 entries of item data inside the file
  #ifdef ALLOW_CHESTS
  const BlockChange *changes = (const BlockChange *)src;
  for (uint32_t i = 0; i < header.block_changes_count; i ++) {
    if (changes[i].block != B_chest) continue;
    if (i + 14 >= header.block_changes_count) return BAREIRON_ERR_BAD_WORLD;
    i += 14;
  }
  #endif

  world_seed = header.world_seed;
  world_time = header.world_time;

  block_changes_count = header.block_changes_count;
  memcpy(block_changes, src, (size_t)block_changes_count * sizeof(BlockChange));
  for (uint32_t i = block_changes_count; i < block_changes_cap; i ++) block_changes[i].block = 0xFF;
  src += (size_t)block_changes_count * sizeof(BlockChange);

  memset(player_data, 0, sizeof(player_data));
  memcpy(player_data, src, (size_t)header.player_count * sizeof(PlayerData));

  // Version 2: the animals and items, and the beds; version 1 had neither
  ent_clear();
  for (int i = 0; i < MAX_PLAYERS; i ++) bed_set_spawn_point(i, 0, 0, 0, 0);
  if (header.version >= 2) {
    for (uint32_t i = 0; i < entities; i ++) {
      EntitySave save;
      memcpy(&save, extra, sizeof(save));
      extra += sizeof(save);
      Entity *e = ent_spawn(save.type, save.x, save.y, save.z);
      if (!e) continue;
      e->flags = save.flags & (ENT_BABY | ENT_SHEARED | ENT_PERSISTENT | ENT_KEEP);
      e->yaw = e->head_yaw = e->sent_yaw = e->sent_head = save.yaw;
      e->health = save.health;
      e->age = save.age;
      e->timer = save.timer;
      if (e->kind == ENT_KIND_ITEM) {
        e->u.item.item = save.item;
        e->u.item.count = save.count;
      } else if (e->kind == ENT_KIND_MOB) {
        e->u.mob.wool = save.wool;
        e->u.mob.grow = save.grow;
        e->u.mob.breed_cd = save.breed_cd;
        e->u.mob.egg = save.egg;
      }
    }
    for (int i = 0; i < MAX_PLAYERS; i ++) {
      BedSave bed;
      memcpy(&bed, extra, sizeof(bed));
      extra += sizeof(bed);
      bed_set_spawn_point(i, bed.has, bed.x, bed.y, bed.z);
    }
  }
  player_data_count = 0;
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    PlayerData *player = &player_data[i];
    player->client_fd = -1;
    // Nothing from the file may index out of bounds or point anywhere:
    // drop the flags that only make sense while connected, and with them
    // any chest pointer kept in craft_items
    player->name[sizeof(player->name) - 1] = '\0';
    if (player->hotbar >= 9) player->hotbar = 0;
    if (player->flags & 0x80) memset(player->craft_items, 0, sizeof(player->craft_items));
    player->flags &= ~(0x01 | 0x04 | 0x08 | 0x10 | 0x20 | 0x40 | 0x80);
    for (int j = 0; j < 16; j ++) {
      if (player->uuid[j] == 0) continue;
      player_data_count ++;
      break;
    }
  }

  return 0;
}

int saveWorldNow (void) {
  size_t size = worldSnapshotSize();
  uint8_t *buffer = platform->alloc_large(size);
  if (!buffer) return BAREIRON_ERR_NO_MEMORY;
  worldSnapshotWrite(buffer, size);
  // From here on the buffer belongs to the platform
  int result = platform->save_world(buffer, size);

  last_save_time = get_program_time();
  if (result != 0) {
    printf("Failed to save the world (%d)\n", result);
    statusEmit(BAREIRON_EVT_ERROR, NULL, result);
    return result;
  }
  world_dirty = false;
  statusEmit(BAREIRON_EVT_WORLD_SAVED, NULL, (int)size);
  return 0;
}

void saveWorldIfDue (void) {
  if (!world_dirty) return;
  if (get_program_time() - last_save_time < WORLD_SAVE_INTERVAL) return;
  saveWorldNow();
}

int loadWorld (void) {
  world_dirty = false;
  last_save_time = get_program_time();

  size_t capacity = sizeof(WorldHeader)
    + (size_t)block_changes_cap * sizeof(BlockChange)
    + MAX_PLAYERS * sizeof(PlayerData)
    + sizeof(uint32_t) + (size_t)ent_capacity() * sizeof(EntitySave)
    + MAX_PLAYERS * sizeof(BedSave);
  uint8_t *buffer = platform->alloc_large(capacity);
  if (!buffer) return BAREIRON_ERR_NO_MEMORY;

  size_t len = 0;
  int result = platform->load_world(buffer, capacity, &len);
  if (result == 0) result = worldSnapshotRead(buffer, len);
  else if (result < 0) result = BAREIRON_ERR_BAD_WORLD;
  platform->free_large(buffer);

  if (result == 0) {
    printf("World loaded: %d block changes, seed %08X\n\n", block_changes_count, (unsigned int)world_seed);
  } else if (result == 1) {
    printf("No saved world, starting a new one with seed %08X\n\n", (unsigned int)world_seed);
  } else {
    printf("The saved world can't be read (%d). Not starting, so it isn't overwritten.\n", result);
  }
  return result;
}

_Static_assert(BAREIRON_MAX_SAVED_PLAYERS == MAX_PLAYERS, "BareironWorldInfo must hold every saved player");

uint32_t bareiron_seed_for (uint32_t config_seed) {
  return (uint32_t)splitmix64(config_seed);
}

// Same checks and order as worldSnapshotRead, but only copies out what it
// reports; the limits that depend on the running server (block_changes_cap,
// ent_capacity) are not checked, the exact length still bounds everything
int bareiron_peek_world (const void *snapshot, size_t len, BareironWorldInfo *out) {
  if (!snapshot || !out) return -1;
  const uint8_t *src = snapshot;
  WorldHeader header;
  if (len < sizeof(header)) return -1;
  memcpy(&header, src, sizeof(header));
  if (header.magic != WORLD_MAGIC || (header.version != 1 && header.version != WORLD_VERSION)) return -1;
  if (header.player_size != sizeof(PlayerData) || header.player_count > MAX_PLAYERS) return -1;

  // Each step is checked against len, so nothing here can wrap around
  size_t rest = len - sizeof(header);
  if (header.block_changes_count > rest / sizeof(BlockChange)) return -1;
  size_t changes = (size_t)header.block_changes_count * sizeof(BlockChange);
  rest -= changes;
  if (header.player_count > rest / sizeof(PlayerData)) return -1;
  size_t players = (size_t)header.player_count * sizeof(PlayerData);
  rest -= players;
  if (header.version == 1) {
    if (rest != 0) return -1;
  } else {
    uint32_t entities;
    if (rest < sizeof(entities)) return -1;
    memcpy(&entities, src + sizeof(header) + changes + players, sizeof(entities));
    rest -= sizeof(entities);
    size_t beds = MAX_PLAYERS * sizeof(BedSave);
    if (rest < beds || entities > (rest - beds) / sizeof(EntitySave)) return -1;
    if (rest != beds + (size_t)entities * sizeof(EntitySave)) return -1;
  }

  memset(out, 0, sizeof(*out));
  out->seed = header.world_seed;
  out->world_time = header.world_time;
  const uint8_t *p = src + sizeof(header) + changes;
  for (uint32_t i = 0; i < header.player_count; i ++, p += sizeof(PlayerData)) {
    PlayerData player;   // packed: copy it out, never point into it
    memcpy(&player, p, sizeof(player));
    if (player.name[0] == '\0') continue;
    BareironSavedPlayer *saved = &out->players[out->player_count ++];
    memcpy(saved->name, player.name, sizeof(saved->name));
    saved->name[sizeof(saved->name) - 1] = '\0';
    saved->x = player.x;
    saved->y = player.y;
    saved->z = player.z;
  }
  return 0;
}

#include <stdlib.h>
#include <string.h>

#include "globals.h"
#include "tools.h"
#include "varnum.h"
#include "packets.h"
#include "registries.h"
#include "entity.h"

static Entity *table = NULL;
static uint32_t capacity = 0;
static uint32_t used = 0;
static int32_t next_id = -2;   // -1 means "no entity" to some callers

int ent_init (uint32_t cap) {
  if (cap == 0) cap = ENT_DEFAULT_CAPACITY;
  table = platform->alloc_large(cap * sizeof(Entity));
  if (!table) return -1;
  capacity = cap;
  ent_clear();
  return 0;
}

void ent_free (void) {
  if (table) platform->free_large(table);
  table = NULL;
  capacity = 0;
  used = 0;
}

void ent_clear (void) {
  if (table) memset(table, 0, capacity * sizeof(Entity));
  used = 0;
  next_id = -2;
}

uint8_t ent_kind_of (uint16_t type) {
  if (type == E_item) return ENT_KIND_ITEM;
  if (type == E_arrow || type == E_spectral_arrow) return ENT_KIND_ARROW;
  if (type == E_tnt) return ENT_KIND_TNT;
  return ENT_KIND_MOB;
}

uint32_t ent_capacity (void) { return capacity; }
uint32_t ent_count (void) { return used; }
Entity *ent_at (uint32_t index) { return index < capacity ? &table[index] : NULL; }
int32_t ent_id (const Entity *e) { return e->id; }
void ent_moved (Entity *e) { e->dirty = 1; }

Entity *ent_by_id (int32_t id) {
  if (id >= -1) return NULL;
  for (uint32_t i = 0; i < capacity; i ++) {
    if (table[i].type != 0 && table[i].id == id) return &table[i];
  }
  return NULL;
}

// A free slot; for a mob, the oldest item on the ground gives way when
// there is none. Arrows, items and TNT never push anything out.
static Entity *freeSlot (uint8_t kind) {
  Entity *oldest_item = NULL;
  for (uint32_t i = 0; i < capacity; i ++) {
    if (table[i].type == 0) return &table[i];
    if (table[i].kind == ENT_KIND_ITEM && (!oldest_item || table[i].age > oldest_item->age)) {
      oldest_item = &table[i];
    }
  }
  if (kind != ENT_KIND_MOB || !oldest_item) return NULL;
  ent_remove(oldest_item);
  return oldest_item;
}

Entity *ent_spawn (uint16_t type, int32_t x, int32_t y, int32_t z) {
  if (!table || type == 0) return NULL;
  uint8_t kind = ent_kind_of(type);
  Entity *e = freeSlot(kind);
  if (!e) return NULL;
  memset(e, 0, sizeof(*e));
  e->type = type;
  e->kind = kind;
  e->id = next_id --;
  e->x = e->sent_x = x;
  e->y = e->sent_y = y;
  e->z = e->sent_z = z;
  e->u.mob.target = -1;
  used ++;
  return e;
}

static int playerIndex (const PlayerData *p) {
  return (int)(p - player_data);
}

void ent_remove (Entity *e) {
  if (!e || e->type == 0) return;
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (!(e->seen_by & ((uint64_t)1 << i))) continue;
    if (player_data[i].client_fd != -1) sc_removeEntity(player_data[i].client_fd, e->id);
  }
  e->type = 0;
  e->seen_by = 0;
  used --;
}

static int floorChunk (int32_t block) {
  return block >= 0 ? block / 16 : -((15 - block) / 16);
}

// Whether a player should have this entity spawned: online, loaded, within
// its view distance in chunks, and the entity not on its way out
static int shouldSee (const PlayerData *p, const Entity *e) {
  if (p->client_fd == -1 || (p->flags & 0x20)) return 0;
  if (e->flags & ENT_DEAD) return 0;
  int ecx = floorChunk(e->sent_x >> 12), ecz = floorChunk(e->sent_z >> 12);
  int pcx = floorChunk(p->x), pcz = floorChunk(p->z);
  return abs(ecx - pcx) <= VIEW_DISTANCE && abs(ecz - pcz) <= VIEW_DISTANCE;
}

static void spawnFor (const Entity *e, int client_fd) {
  uint8_t uuid[16] = { 0x6B, 0x63, 0, 0, 0, 0, 0x40, 0, 0x80 };   // "kc", version 4, variant
  memcpy(uuid + 12, &e->id, 4);
  sc_spawnEntity(client_fd, e->id, uuid, e->type,
    (double)e->sent_x / ENT_UNIT, (double)e->sent_y / ENT_UNIT, (double)e->sent_z / ENT_UNIT,
    e->sent_yaw, e->sent_pitch);
  ent_send_extra(e, client_fd);
}

static void trackOne (PlayerData *p, Entity *e) {
  int i = playerIndex(p);
  uint64_t bit = (uint64_t)1 << i;
  int sees = (e->seen_by & bit) != 0;
  int should = shouldSee(p, e);
  if (should && !sees) {
    spawnFor(e, p->client_fd);
    e->seen_by |= bit;
  } else if (!should && sees) {
    if (p->client_fd != -1) sc_removeEntity(p->client_fd, e->id);
    e->seen_by &= ~bit;
  }
}

void ent_track_player (PlayerData *p) {
  for (uint32_t j = 0; j < capacity; j ++) {
    if (table[j].type != 0) trackOne(p, &table[j]);
  }
}

void ent_untrack_player (PlayerData *p) {
  uint64_t bit = (uint64_t)1 << playerIndex(p);
  for (uint32_t j = 0; j < capacity; j ++) table[j].seen_by &= ~bit;
}

// Movement since the last turn to everyone who has it: a relative move
// (the protocol carries up to 8 blocks), or a full position sync
static void sendMove (Entity *e) {
  int32_t dx = e->x - e->sent_x, dy = e->y - e->sent_y, dz = e->z - e->sent_z;
  uint8_t moved = dx || dy || dz;
  uint8_t turned = e->yaw != e->sent_yaw || e->pitch != e->sent_pitch;
  uint8_t head = e->head_yaw != e->sent_head;
  uint8_t far = dx > 32767 || dx < -32768 || dy > 32767 || dy < -32768 || dz > 32767 || dz < -32768;
  uint8_t on_ground = (e->flags & ENT_ON_GROUND) ? 1 : 0;
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (!(e->seen_by & ((uint64_t)1 << i))) continue;
    int fd = player_data[i].client_fd;
    if (fd == -1) continue;
    if (far) {
      sc_syncEntity(fd, e->id, (double)e->x / ENT_UNIT, (double)e->y / ENT_UNIT, (double)e->z / ENT_UNIT,
        e->vx, e->vy, e->vz, e->yaw * 360.0f / 256.0f, e->pitch * 360.0f / 256.0f, on_ground);
    } else if (moved) {
      sc_moveEntity(fd, e->id, (int16_t)dx, (int16_t)dy, (int16_t)dz, turned, e->yaw, e->pitch, on_ground);
    } else if (turned) {
      sc_updateEntityRotation(fd, e->id, e->yaw, e->pitch);
    }
    if (head) sc_setHeadRotation(fd, e->id, e->head_yaw);
  }
  e->sent_x = e->x; e->sent_y = e->y; e->sent_z = e->z;
  e->sent_yaw = e->yaw; e->sent_pitch = e->pitch; e->sent_head = e->head_yaw;
}

void ent_network_tick (void) {
  if (!table || server_ticks % ENT_NETWORK_EVERY != 0) return;
  for (uint32_t j = 0; j < capacity; j ++) {
    Entity *e = &table[j];
    if (e->type == 0) continue;
    // Those who already have it get the move first; whoever starts seeing
    // it below is spawned where the others now believe it is
    if (e->dirty) {
      sendMove(e);
      e->dirty = 0;
    }
    for (int i = 0; i < MAX_PLAYERS; i ++) {
      PlayerData *p = &player_data[i];
      if (p->client_fd == -1 && !(e->seen_by & ((uint64_t)1 << i))) continue;
      trackOne(p, e);
    }
  }
}

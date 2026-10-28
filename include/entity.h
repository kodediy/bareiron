#ifndef H_ENTITY
#define H_ENTITY

#include <stdint.h>

#include "globals.h"
#include "pathfind.h"

// One table for everything in the world that is not a player: mobs, items
// on the ground, arrows, primed TNT. It lives in big memory (PSRAM on the
// Kode Dot). Each player is sent the entities within its view distance;
// movement goes out every ENT_NETWORK_EVERY ticks as deltas, or as a
// teleport past 8 blocks.

#define ENT_UNIT 4096               // positions are in 1/4096 of a block
#define ENT_DEFAULT_CAPACITY 512
#define ENT_NETWORK_EVERY 3         // vanilla sends living mobs every 3 ticks

// flags
#define ENT_ON_GROUND  0x01
#define ENT_IN_WATER   0x02
#define ENT_BABY       0x04
#define ENT_ON_FIRE    0x08
#define ENT_PERSISTENT 0x10
#define ENT_SHEARED    0x20
#define ENT_DEAD       0x40

// kinds
#define ENT_KIND_MOB   1
#define ENT_KIND_ITEM  2
#define ENT_KIND_ARROW 3
#define ENT_KIND_TNT   4

typedef struct Entity {
  uint16_t type;              // E_*; 0 = free slot
  uint8_t  kind;              // ENT_KIND_*
  uint8_t  flags;             // ENT_*
  int32_t  id;                // network id: negative, never a player's
  int32_t  x, y, z;           // ENT_UNIT
  float    vx, vy, vz;        // blocks per tick
  uint8_t  yaw, pitch, head_yaw;  // 1/256 of a turn
  uint8_t  dirty;             // moved since the last network turn
  float    health;
  uint32_t age;               // ticks since it appeared
  uint16_t timer;             // kind's countdown: death, fuse, pickup delay
  uint64_t seen_by;           // bit i: player_data[i] has it spawned
  int32_t  sent_x, sent_y, sent_z;   // what its trackers last got
  uint8_t  sent_yaw, sent_pitch, sent_head;
  union {
    struct {
      int32_t  target;        // the player it is after, -1 none
      uint8_t  panic;         // ticks of panic left
      uint8_t  wool;          // a sheep's colour
      uint8_t  goal;          // GOAL_* running now
      uint8_t  path_len, path_i;
      uint8_t  path_wait;     // ticks before it may ask for another path
      uint8_t  attack_cd;     // ticks before it may hit again
      uint8_t  look_ticks;    // ticks left looking at a player
      uint16_t alone_ticks;   // ticks with no player within 32 blocks
      PathNode path[8];       // the next steps of its path
    } mob;
    struct { uint16_t item; uint8_t count; } item;
    struct { int32_t owner; float damage; } arrow;
  } u;
} Entity;

int      ent_init (uint32_t capacity);  // alloc_large; 0 ok, -1 no memory
void     ent_free (void);
void     ent_clear (void);              // empties the table (new world)
Entity  *ent_spawn (uint16_t type, int32_t x, int32_t y, int32_t z); // NULL when full
void     ent_remove (Entity *e);        // Remove Entities to everyone tracking it
Entity  *ent_by_id (int32_t id);
int32_t  ent_id (const Entity *e);      // network id: never a player's, never reused
uint32_t ent_capacity (void);
Entity  *ent_at (uint32_t index);       // a slot, free or not (type 0 = free)
uint32_t ent_count (void);              // slots in use
void     ent_moved (Entity *e);         // mark for the next network turn
void     ent_network_tick (void);       // each tick; works every ENT_NETWORK_EVERY
void     ent_track_player (PlayerData *p);   // spawn what it should see, remove what it should not
void     ent_untrack_player (PlayerData *p); // it left: forget it, sends nothing
uint8_t  ent_kind_of (uint16_t type);

// What a newly tracking player is sent after Spawn Entity (metadata,
// equipment). Lives with the mob code.
void     ent_send_extra (const Entity *e, int client_fd);

#endif

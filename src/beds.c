#include <stdlib.h>
#include <string.h>

#include "globals.h"
#include "tools.h"
#include "registries.h"
#include "packets.h"
#include "procedures.h"
#include "worldgen.h"
#include "entity.h"
#include "entitymeta.h"
#include "mobs.h"
#include "beds.h"

// The 8 bed blocks run foot, head for north, south, west, east (build_registries.js)
enum { BED_NORTH, BED_SOUTH, BED_WEST, BED_EAST };
static const int8_t dir_x[4] = { 0, 0, -1, 1 };
static const int8_t dir_z[4] = { -1, 1, 0, 0 };

typedef struct {
  uint8_t sleeping;
  uint8_t has_spawn;
  short   x, z;    // the bed's foot
  uint8_t y;
} PlayerBed;

static PlayerBed state[MAX_PLAYERS];
static uint16_t asleep_ticks;

uint8_t bed_is_part (uint8_t block) {
  return block >= B_white_bed_foot_north && block <= B_white_bed_head_east;
}

static uint8_t facingOf (uint8_t block) { return (block - B_white_bed_foot_north) / 2; }
static uint8_t isHead (uint8_t block) { return (block - B_white_bed_foot_north) & 1; }
static uint8_t bedBlock (uint8_t facing, uint8_t head) { return B_white_bed_foot_north + facing * 2 + head; }

// Vanilla's yaw: 0 looks south (+z), 90 west, 180 north, -90 east
static uint8_t facingFromYaw (int8_t yaw) {
  int deg = yaw * 180 / 127;
  if (deg >= -45 && deg < 45) return BED_SOUTH;
  if (deg >= 45 && deg < 135) return BED_WEST;
  if (deg >= -135 && deg < -45) return BED_EAST;
  return BED_NORTH;
}

static uint8_t solidBelow (short x, uint8_t y, short z) {
  return !isPassableBlock(getBlockAt(x, y - 1, z));
}

uint8_t bed_place (PlayerData *p, short x, uint8_t y, short z) {
  uint8_t f = facingFromYaw(p->yaw);
  short hx = x + dir_x[f], hz = z + dir_z[f];
  if (!isReplaceableBlock(getBlockAt(x, y, z)) || !isReplaceableBlock(getBlockAt(hx, y, hz))) return 0;
  if (!solidBelow(x, y, z) || !solidBelow(hx, y, hz)) return 0;
  if (makeBlockChange(x, y, z, bedBlock(f, 0))) return 0;
  if (makeBlockChange(hx, y, hz, bedBlock(f, 1))) {
    makeBlockChange(x, y, z, B_air);
    return 0;
  }
  return 1;
}

// The foot of the bed this part belongs to
static void footOf (short x, uint8_t y, short z, uint8_t block, short *fx, short *fz) {
  uint8_t f = facingOf(block);
  *fx = isHead(block) ? x - dir_x[f] : x;
  *fz = isHead(block) ? z - dir_z[f] : z;
  (void)y;
}

void bed_break (short x, uint8_t y, short z, uint8_t block) {
  if (!bed_is_part(block)) return;
  uint8_t f = facingOf(block);
  short ox = isHead(block) ? x - dir_x[f] : x + dir_x[f];
  short oz = isHead(block) ? z - dir_z[f] : z + dir_z[f];
  uint8_t other = getBlockAt(ox, y, oz);
  if (bed_is_part(other) && facingOf(other) == f) makeBlockChange(ox, y, oz, B_air);
  if (getBlockAt(x, y, z) == block) makeBlockChange(x, y, z, B_air);
  // Whoever was asleep in it gets up
  short fx, fz;
  footOf(x, y, z, block, &fx, &fz);
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (state[i].sleeping && state[i].x == fx && state[i].z == fz && state[i].y == y) bed_wake(&player_data[i]);
  }
}

static uint64_t packPosition (int x, int y, int z) {
  return ((uint64_t)(x & 0x3FFFFFF) << 38) | ((uint64_t)(z & 0x3FFFFFF) << 12) | (uint64_t)(y & 0xFFF);
}

// Lying down or getting up, as everyone (the sleeper too) sees it
static void broadcastPose (const PlayerData *p, uint8_t sleeping, short x, uint8_t y, short z) {
  EntityData meta[2] = {
    { META_POSE, META_T_POSE, { 0 } },
    { META_SLEEPING_POS, META_T_OPT_POSITION, { 0 } },
  };
  meta[0].value.pose = sleeping ? POSE_SLEEPING : POSE_STANDING;
  meta[1].value.position.present = sleeping;
  meta[1].value.position.packed = packPosition(x, y, z);
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (player_data[i].client_fd == -1 || (player_data[i].flags & 0x20)) continue;
    sc_setEntityMetadata(player_data[i].client_fd, p->client_fd, meta, 2);
  }
}

static void tell (const PlayerData *p, const char *text) {
  sc_systemChat(p->client_fd, (char *)text, (uint16_t)strlen(text));
}

static uint8_t monstersNear (short x, uint8_t y, short z) {
  for (uint32_t i = 0; i < ent_capacity(); i ++) {
    Entity *e = ent_at(i);
    if (e->type == 0 || e->kind != ENT_KIND_MOB || (e->flags & ENT_DEAD)) continue;
    const MobType *t = mob_type(e->type);
    if (!t || t->category != CAT_MONSTER) continue;
    int ex = e->x >> 12, ey = e->y >> 12, ez = e->z >> 12;
    if (abs(ex - x) <= BED_MONSTER_RANGE && abs(ez - z) <= BED_MONSTER_RANGE && abs(ey - y) <= 5) return 1;
  }
  return 0;
}

uint8_t bed_use (PlayerData *p, short x, uint8_t y, short z) {
  uint8_t block = getBlockAt(x, y, z);
  if (!bed_is_part(block)) return 0;
  short fx, fz;
  footOf(x, y, z, block, &fx, &fz);
  // Vanilla: you can sleep from 12542 to 23459
  if (world_time < 12542 || world_time > 23459) {
    tell(p, "You can sleep only at night");
    return 1;
  }
  if (monstersNear(fx, y, fz)) {
    tell(p, "You may not rest now; there are monsters nearby");
    return 1;
  }
  PlayerBed *s = &state[p - player_data];
  s->sleeping = 1;
  s->has_spawn = 1;
  s->x = fx; s->y = y; s->z = fz;
  broadcastPose(p, 1, fx, y, fz);
  return 1;
}

void bed_wake (PlayerData *p) {
  PlayerBed *s = &state[p - player_data];
  if (!s->sleeping) return;
  s->sleeping = 0;
  broadcastPose(p, 0, s->x, s->y, s->z);
}

uint8_t bed_sleeping (const PlayerData *p) {
  return state[p - player_data].sleeping;
}

void beds_tick (void) {
  int awake = 0, asleep = 0;
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    PlayerData *p = &player_data[i];
    if (p->client_fd == -1 || (p->flags & 0x20) || p->health == 0) {
      state[i].sleeping = 0;
      continue;
    }
    if (state[i].sleeping) asleep ++; else awake ++;
  }
  // Everyone in the game asleep (Manuel's decision, as vanilla): morning
  if (!asleep || awake) { asleep_ticks = 0; return; }
  if (++ asleep_ticks < BED_SLEEP_TICKS) return;
  asleep_ticks = 0;
  world_time = 0;
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (!state[i].sleeping) continue;
    bed_wake(&player_data[i]);
    sc_updateTime(player_data[i].client_fd, world_time);
  }
}

uint8_t bed_spawn (const PlayerData *p, short *x, uint8_t *y, short *z) {
  const PlayerBed *s = &state[p - player_data];
  if (!s->has_spawn) return 0;
  uint8_t block = getBlockAt(s->x, s->y, s->z);
  if (!bed_is_part(block) || isHead(block)) return 0;
  *x = s->x; *y = s->y + 1; *z = s->z;   // on the bed
  return 1;
}

void beds_forget (PlayerData *p) {
  memset(&state[p - player_data], 0, sizeof(PlayerBed));
}

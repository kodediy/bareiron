#include <math.h>
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
#include "combat.h"
#include "effects.h"
#include "arrows.h"

#define SUBSTEP 0.25          // blocks per collision check along the flight
#define OWNER_GRACE 5         // ticks before an arrow can hit whoever shot it
#define BOW_MIN_POWER 0.1f

// Bow drawing, per player slot; not saved
static uint32_t draw_start[MAX_PLAYERS];
static uint8_t drawing[MAX_PLAYERS];

static void faceVelocity (Entity *e) {
  double h = sqrt(e->vx * e->vx + e->vz * e->vz);
  e->yaw = (uint8_t)(int)lround(atan2(e->vx, e->vz) * 128.0 / M_PI);
  e->pitch = (uint8_t)(int)lround(atan2(e->vy, h) * 128.0 / M_PI);
}

Entity *arrow_shoot (int32_t shooter_id, int32_t x, int32_t y, int32_t z,
                     float dir_x, float dir_y, float dir_z, float speed, float damage,
                     uint8_t can_pickup, uint8_t effect) {
  float len = sqrtf(dir_x * dir_x + dir_y * dir_y + dir_z * dir_z);
  if (len < 1e-6f) return NULL;
  Entity *e = ent_spawn(E_arrow, x, y, z);
  if (!e) return NULL;
  e->vx = dir_x / len * speed;
  e->vy = dir_y / len * speed;
  e->vz = dir_z / len * speed;
  e->u.arrow.owner = shooter_id;
  e->u.arrow.damage = damage;
  e->u.arrow.effect = effect;
  e->u.arrow.pickup = can_pickup;
  faceVelocity(e);
  e->sent_yaw = e->yaw;
  e->sent_pitch = e->pitch;
  ent_track_now(e);   // it may hit before the next network turn
  return e;
}

void arrows_send_extra (const Entity *e, int client_fd) {
  EntityData meta[2] = {
    { META_ARROW_FLAGS, META_T_BYTE, { (uint8_t)(e->u.arrow.crit ? 0x01 : 0) } },
    { META_ARROW_GROUND, META_T_BOOLEAN, { e->u.arrow.in_ground } },
  };
  sc_setEntityMetadata(client_fd, e->id, meta, 2);
  if (!e->u.arrow.in_ground) sc_setEntityMotion(client_fd, e->id, e->vx, e->vy, e->vz);
}

static void broadcastExtra (const Entity *e) {
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (!(e->seen_by & ((uint64_t)1 << i)) || player_data[i].client_fd == -1) continue;
    arrows_send_extra(e, player_data[i].client_fd);
  }
}

static uint8_t solid (double x, double y, double z) {
  int by = (int)floor(y);
  if (by < 0) return 1;
  if (by > 255) return 0;
  return !isPassableBlock(getBlockAt((int)floor(x), by, (int)floor(z)));
}

static uint8_t isWater (double x, double y, double z) {
  uint8_t b = getBlockAt((int)floor(x), (int)floor(y), (int)floor(z));
  return b >= B_water && b < B_water + 8;
}

// What the point (x, y, z) is inside of: a living mob's or a player's box,
// widened by the arrow's own half width. Returns its id, or 0.
static int32_t hitAt (const Entity *arrow, double x, double y, double z) {
  uint8_t grace = arrow->age < OWNER_GRACE;
  for (uint32_t j = 0; j < ent_capacity(); j ++) {
    Entity *m = ent_at(j);
    if (m->type == 0 || m->kind != ENT_KIND_MOB || (m->flags & ENT_DEAD)) continue;
    if (grace && m->id == arrow->u.arrow.owner) continue;
    const MobType *t = mob_type(m->type);
    double hw = (t ? t->body.width : 0.6) / 2 + 0.25, h = t ? t->body.height : 1.8;
    double mx = (double)m->x / ENT_UNIT, my = (double)m->y / ENT_UNIT, mz = (double)m->z / ENT_UNIT;
    if (fabs(x - mx) <= hw && fabs(z - mz) <= hw && y >= my - 0.25 && y <= my + h + 0.25) return m->id;
  }
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    PlayerData *p = &player_data[i];
    if (p->client_fd == -1 || (p->flags & 0x20) || p->health == 0) continue;
    if (grace && p->client_fd == arrow->u.arrow.owner) continue;
    double px = p->x + 0.5, pz = p->z + 0.5;
    if (fabs(x - px) <= 0.55 && fabs(z - pz) <= 0.55 && y >= p->y - 0.25 && y <= p->y + 2.05) return p->client_fd;
  }
  return 0;
}

static void strike (Entity *e, int32_t target, double speed) {
  float damage = ceilf((float)(speed * e->u.arrow.damage));
  if (e->u.arrow.crit) damage += fast_rand() % (uint32_t)(damage / 2 + 2);
  double from_x = (double)e->x / ENT_UNIT - e->vx, from_z = (double)e->z / ENT_UNIT - e->vz;
  if (combat_hurt(target, e->u.arrow.owner, D_arrow, damage)) combat_knockback(target, from_x, from_z, COMBAT_KNOCKBACK);
  if (target > 0 && e->u.arrow.effect != ARROW_NO_EFFECT) {
    PlayerData *p;
    if (!getPlayerData(target, &p)) eff_apply(p, e->u.arrow.effect, 0, 600);
  }
  ent_remove(e);
}

// A stuck arrow a player walks over goes back into its inventory
static void tryPickup (Entity *e) {
  if (!e->u.arrow.pickup) return;
  double x = (double)e->x / ENT_UNIT, y = (double)e->y / ENT_UNIT, z = (double)e->z / ENT_UNIT;
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    PlayerData *p = &player_data[i];
    if (p->client_fd == -1 || (p->flags & 0x20) || p->health == 0) continue;
    if (fabs(x - (p->x + 0.5)) > 1.3 || fabs(z - (p->z + 0.5)) > 1.3 || y < p->y - 0.5 || y > p->y + 2.3) continue;
    if (givePlayerItem(p, I_arrow, 1)) continue;
    for (int j = 0; j < MAX_PLAYERS; j ++) {
      if (!(e->seen_by & ((uint64_t)1 << j)) || player_data[j].client_fd == -1) continue;
      sc_pickupItem(player_data[j].client_fd, e->id, p->client_fd, 1);
    }
    ent_remove(e);
    return;
  }
}

static void fly (Entity *e) {
  double x = (double)e->x / ENT_UNIT, y = (double)e->y / ENT_UNIT, z = (double)e->z / ENT_UNIT;
  double speed = sqrt(e->vx * e->vx + e->vy * e->vy + e->vz * e->vz);
  int n = (int)ceil(speed / SUBSTEP);
  if (n < 1) n = 1;
  double sx = e->vx / n, sy = e->vy / n, sz = e->vz / n;
  for (int k = 0; k < n; k ++) {
    double nx = x + sx, ny = y + sy, nz = z + sz;
    if (solid(nx, ny, nz)) {
      // Into the block's face along the axis it crossed, and it stays there
      if (floor(nx) != floor(x)) x = sx > 0 ? floor(nx) - 0.001 : floor(nx) + 1.001;
      else if (floor(nz) != floor(z)) z = sz > 0 ? floor(nz) - 0.001 : floor(nz) + 1.001;
      else y = sy > 0 ? floor(ny) - 0.001 : floor(ny) + 1.001;
      e->x = (int32_t)llround(x * ENT_UNIT);
      e->y = (int32_t)llround(y * ENT_UNIT);
      e->z = (int32_t)llround(z * ENT_UNIT);
      e->vx = e->vy = e->vz = 0;
      e->u.arrow.in_ground = 1;
      e->timer = 0;
      ent_moved(e);
      broadcastExtra(e);
      return;
    }
    x = nx; y = ny; z = nz;
    e->x = (int32_t)llround(x * ENT_UNIT);
    e->y = (int32_t)llround(y * ENT_UNIT);
    e->z = (int32_t)llround(z * ENT_UNIT);
    int32_t target = hitAt(e, x, y, z);
    if (target) { strike(e, target, speed); return; }
  }
  // vanilla: after moving, drag, then gravity
  float drag = isWater(x, y, z) ? ARROW_WATER_DRAG : ARROW_DRAG;
  e->vx *= drag; e->vy *= drag; e->vz *= drag;
  e->vy -= ARROW_GRAVITY;
  faceVelocity(e);
  ent_moved(e);
}

void arrows_tick (void) {
  for (uint32_t i = 0; i < ent_capacity(); i ++) {
    Entity *e = ent_at(i);
    if (e->type != E_arrow) continue;
    e->age ++;
    if (e->u.arrow.in_ground) {
      if (++ e->timer > ARROW_STUCK_LIFETIME) { ent_remove(e); continue; }
      tryPickup(e);
      continue;
    }
    if (e->age > ARROW_STUCK_LIFETIME) { ent_remove(e); continue; }   // lost in the sky
    fly(e);
  }
}

float bow_power (uint32_t ticks) {
  float f = ticks / 20.0f;
  f = (f * f + 2 * f) / 3;
  return f > 1 ? 1 : f;
}

void bow_start (PlayerData *p) {
  int i = (int)(p - player_data);
  draw_start[i] = server_ticks;
  drawing[i] = 1;
}

static int arrowSlot (const PlayerData *p) {
  for (int s = 0; s < 36; s ++) if (p->inventory_count[s] && p->inventory_items[s] == I_arrow) return s;
  return -1;
}

void bow_release (PlayerData *p) {
  int i = (int)(p - player_data);
  if (!drawing[i]) return;
  drawing[i] = 0;
  if (p->inventory_items[p->hotbar] != I_bow) return;
  float f = bow_power(server_ticks - draw_start[i]);
  if (f < BOW_MIN_POWER) return;
  int slot = arrowSlot(p);
  if (slot < 0 && GAMEMODE != 1) return;
  // From the eyes, along the look
  double yaw = p->yaw * M_PI / 127.0, pitch = p->pitch * (M_PI / 2) / 127.0;
  float dx = (float)(-sin(yaw) * cos(pitch)), dy = (float)(-sin(pitch)), dz = (float)(cos(yaw) * cos(pitch));
  Entity *a = arrow_shoot(p->client_fd,
    (int32_t)((p->x + 0.5) * ENT_UNIT), (int32_t)((p->y + 1.52) * ENT_UNIT), (int32_t)((p->z + 0.5) * ENT_UNIT),
    dx, dy, dz, 3.0f * f, ARROW_BASE_DAMAGE, 1, ARROW_NO_EFFECT);
  if (!a) return;
  a->u.arrow.crit = f >= 1.0f;
  if (slot >= 0) {
    if (-- p->inventory_count[slot] == 0) p->inventory_items[slot] = 0;
    sc_setContainerSlot(p->client_fd, 0, serverSlotToClientSlot(0, slot), p->inventory_count[slot], p->inventory_items[slot]);
  }
  bumpToolDurability(p);
}

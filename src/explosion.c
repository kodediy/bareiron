#include <math.h>
#include <string.h>

#include "globals.h"
#include "tools.h"
#include "varnum.h"
#include "registries.h"
#include "packets.h"
#include "procedures.h"
#include "worldgen.h"
#include "entity.h"
#include "entitymeta.h"
#include "mobs.h"
#include "physics.h"
#include "combat.h"
#include "drops.h"
#include "explosion.h"

#define RAY_STEP 0.3
#define RAY_LOSS 0.22500001f      // vanilla's loss per step, besides the blocks
#define MAX_BROKEN 2048           // blocks one explosion may break
#define BROKEN_HASH 4096

static const PhysBody tnt_body = { 0.98f, 0.98f, 0.0f, 0.04f, 0.98f, 0 };

float block_blast_resistance (uint8_t block) {
  return B_blast_resistance[block];
}

// The blocks this explosion breaks, each once
// In big memory (PSRAM on the Kode Dot), taken on the first explosion
typedef struct { short x; uint8_t y; short z; } BrokenBlock;
static BrokenBlock *broken = NULL;
static int broken_n;
static int16_t *broken_slot = NULL;

static void markBroken (int x, int y, int z) {
  unsigned h = ((unsigned)(x * 73856093) ^ (unsigned)(y * 19349663) ^ (unsigned)(z * 83492791)) & (BROKEN_HASH - 1);
  for (int n = 0; n < BROKEN_HASH; n ++, h = (h + 1) & (BROKEN_HASH - 1)) {
    int i = broken_slot[h];
    if (i < 0) break;
    if (broken[i].x == x && broken[i].y == y && broken[i].z == z) return;
  }
  if (broken_n == MAX_BROKEN) return;
  broken[broken_n].x = (short)x; broken[broken_n].y = (uint8_t)y; broken[broken_n].z = (short)z;
  broken_slot[h] = (int16_t)broken_n ++;
}

static uint8_t blockAt (int x, int y, int z) {
  if (y < 0) return B_bedrock;
  if (y > 255) return B_air;
  return getBlockAt(x, y, z);
}

// Vanilla's rays from the faces of a 16 x 16 x 16 grid
static void castRays (double cx, double cy, double cz, float power) {
  for (int i = 0; i < 16; i ++) for (int j = 0; j < 16; j ++) for (int k = 0; k < 16; k ++) {
    if (i != 0 && i != 15 && j != 0 && j != 15 && k != 0 && k != 15) continue;
    double dx = i / 15.0 * 2 - 1, dy = j / 15.0 * 2 - 1, dz = k / 15.0 * 2 - 1;
    double len = sqrt(dx * dx + dy * dy + dz * dz);
    dx = dx / len * RAY_STEP; dy = dy / len * RAY_STEP; dz = dz / len * RAY_STEP;
    float strength = power * (0.7f + (float)(fast_rand() % 1000) / 1000.0f * 0.6f);
    double x = cx, y = cy, z = cz;
    while (strength > 0) {
      int bx = (int)floor(x), by = (int)floor(y), bz = (int)floor(z);
      uint8_t b = blockAt(bx, by, bz);
      if (b != B_air) strength -= (block_blast_resistance(b) + 0.3f) * 0.3f;
      if (strength > 0 && b != B_air && by >= 0 && by <= 255) markBroken(bx, by, bz);
      x += dx; y += dy; z += dz;
      strength -= RAY_LOSS;
    }
  }
}

static uint8_t clearLine (double x0, double y0, double z0, double x1, double y1, double z1) {
  double dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
  double len = sqrt(dx * dx + dy * dy + dz * dz);
  int n = (int)ceil(len / 0.25);
  for (int s = 1; s < n; s ++) {
    double t = (double)s / n;
    uint8_t b = blockAt((int)floor(x0 + dx * t), (int)floor(y0 + dy * t), (int)floor(z0 + dz * t));
    if (!isPassableBlock(b)) return 0;
  }
  return 1;
}

// The share of a box's 8 corners the blast reaches in a straight line
static float exposure (double cx, double cy, double cz, double x, double y, double z, double w, double h) {
  int seen = 0;
  for (int c = 0; c < 8; c ++) {
    double px = x + ((c & 1) ? w / 2 : -w / 2), py = y + ((c & 2) ? h : 0), pz = z + ((c & 4) ? w / 2 : -w / 2);
    if (clearLine(cx, cy, cz, px, py, pz)) seen ++;
  }
  return seen / 8.0f;
}

// Hurts and pushes one thing at (x, y, z) with a box w x h; returns the push
static float blastOn (double cx, double cy, double cz, float power, double x, double y, double z, double w, double h,
                      double *ux, double *uy, double *uz) {
  double dx = x - cx, dy = y + h * 0.85 - cy, dz = z - cz;
  double dist = sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy) + (z - cz) * (z - cz));
  double reach = 2.0 * power;
  if (dist >= reach) return -1;
  double len = sqrt(dx * dx + dy * dy + dz * dz);
  if (len < 1e-4) { dx = 0; dy = 1; dz = 0; len = 1; }
  *ux = dx / len; *uy = dy / len; *uz = dz / len;
  float impact = (float)(1 - dist / reach) * exposure(cx, cy, cz, x, y, z, w, h);
  return impact;
}

static void sendExplosion (double x, double y, double z, float power) {
  uint16_t particle = power >= 2 ? PT_explosion_emitter : PT_explosion;
  uint16_t sound = S_entity_generic_explode;
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    PlayerData *p = &player_data[i];
    if (p->client_fd == -1 || (p->flags & 0x20)) continue;
    double dx = p->x + 0.5 - x, dz = p->z + 0.5 - z;
    if (dx * dx + dz * dz > 64.0 * 64.0) continue;
    int fd = p->client_fd;
    writeVarInt(fd, 1 + 24 + 1 + sizeVarInt(particle) + sizeVarInt(sound + 1));
    writeByte(fd, P_explode);
    writeDouble(fd, x); writeDouble(fd, y); writeDouble(fd, z);
    writeByte(fd, 0);                 // no knockback here: players get their own motion
    writeVarInt(fd, particle);        // no particle data for these two
    writeVarInt(fd, sound + 1);       // a registered sound
  }
}

void explode (int32_t ex, int32_t ey, int32_t ez, float power, int32_t source_id) {
  double cx = (double)ex / ENT_UNIT, cy = (double)ey / ENT_UNIT, cz = (double)ez / ENT_UNIT;
  if (!broken) {
    broken = platform->alloc_large(MAX_BROKEN * sizeof(BrokenBlock));
    broken_slot = platform->alloc_large(BROKEN_HASH * sizeof(int16_t));
    if (!broken || !broken_slot) { broken = NULL; return; }
  }
  broken_n = 0;
  memset(broken_slot, 0xFF, BROKEN_HASH * sizeof(int16_t));
  castRays(cx, cy, cz, power);

  sendExplosion(cx, cy, cz, power);
  uint8_t player_caused = source_id > 0;
  uint8_t damage_type = player_caused ? D_player_explosion : D_explosion;

  // Entities: hurt and pushed by closeness and exposure
  for (uint32_t j = 0; j < ent_capacity(); j ++) {
    Entity *e = ent_at(j);
    if (e->type == 0 || (e->flags & ENT_DEAD) || e->id == source_id) continue;
    double ux, uy, uz, w = 0.25, h = 0.25;
    const MobType *t = e->kind == ENT_KIND_MOB ? mob_type(e->type) : NULL;
    if (t) { w = t->body.width; h = t->body.height; }
    float impact = blastOn(cx, cy, cz, power, (double)e->x / ENT_UNIT, (double)e->y / ENT_UNIT, (double)e->z / ENT_UNIT,
                           w, h, &ux, &uy, &uz);
    if (impact < 0) continue;
    if (e->kind == ENT_KIND_MOB) {
      float damage = (impact * impact + impact) / 2 * 7 * 2 * power + 1;
      combat_hurt(e->id, source_id, damage_type, damage);
    } else if (e->kind == ENT_KIND_ITEM) {
      ent_remove(e);   // items in a blast are destroyed
      continue;
    }
    e->vx += (float)(ux * impact);
    e->vy += (float)(uy * impact);
    e->vz += (float)(uz * impact);
    ent_moved(e);
  }
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    PlayerData *p = &player_data[i];
    if (p->client_fd == -1 || (p->flags & 0x20) || p->health == 0) continue;
    double ux, uy, uz;
    float impact = blastOn(cx, cy, cz, power, p->x + 0.5, p->y, p->z + 0.5, 0.6, 1.8, &ux, &uy, &uz);
    if (impact < 0) continue;
    float damage = (impact * impact + impact) / 2 * 7 * 2 * power + 1;
    combat_hurt(p->client_fd, source_id, damage_type, damage);
    sc_setEntityMotion(p->client_fd, p->client_fd, (float)(ux * impact), (float)(uy * impact), (float)(uz * impact));
  }

  // Blocks: TNT is set off, the rest breaks and sometimes drops
  for (int i = 0; i < broken_n; i ++) {
    int x = broken[i].x, y = broken[i].y, z = broken[i].z;
    uint8_t block = getBlockAt(x, y, z);
    if (block == B_air) continue;
    if (makeBlockChange(x, y, z, B_air)) continue;   // the store is full: this one stays
    if (block == B_tnt) {
      tnt_prime(x * ENT_UNIT + ENT_UNIT / 2, y * ENT_UNIT, z * ENT_UNIT + ENT_UNIT / 2, 10 + fast_rand() % 21, source_id);
      continue;
    }
    if ((float)(fast_rand() % 1000) / 1000.0f < 1.0f / power) {
      uint16_t item = getMiningResult(I_diamond_pickaxe, block);
      if (item) drop_pop(item, 1, x + 0.5, y + 0.5, z + 0.5);
    }
  }
}

Entity *tnt_prime (int32_t x, int32_t y, int32_t z, uint16_t fuse, int32_t lit_by) {
  Entity *e = ent_spawn(E_tnt, x, y, z);
  if (!e) return NULL;
  e->timer = fuse;
  e->u.tnt.lit_by = lit_by;
  // a little hop, as vanilla
  double a = (double)(fast_rand() % 1000) / 1000.0 * 2 * M_PI;
  e->vx = (float)(-sin(a) * 0.02);
  e->vz = (float)(-cos(a) * 0.02);
  e->vy = 0.2f;
  ent_track_now(e);
  return e;
}

void tnt_send_extra (const Entity *e, int client_fd) {
  EntityData fuse = { META_TNT_FUSE, META_T_VARINT, { 0 } };
  fuse.value.pose = e->timer;
  sc_setEntityMetadata(client_fd, e->id, &fuse, 1);
}

void tnt_tick (void) {
  for (uint32_t i = 0; i < ent_capacity(); i ++) {
    Entity *e = ent_at(i);
    if (e->type != E_tnt) continue;
    phys_step(e, &tnt_body);
    if (e->timer > 0) e->timer --;
    if (e->timer > 0) continue;
    int32_t x = e->x, y = e->y + ENT_UNIT / 16, z = e->z, by = e->u.tnt.lit_by;
    ent_remove(e);
    explode(x, y, z, TNT_POWER, by);
  }
}

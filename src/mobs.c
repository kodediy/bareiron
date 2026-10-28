#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "globals.h"
#include "tools.h"
#include "registries.h"
#include "packets.h"
#include "procedures.h"
#include "worldgen.h"
#include "entity.h"
#include "physics.h"
#include "pathfind.h"
#include "mobs.h"

#define BODY(w, h) { w, h, 0.6f, 0.08f, 0.98f }

// Vanilla's attributes (Normal difficulty)
static const MobType types[] = {
  { E_zombie, CAT_MONSTER, 20, 0.23f, 3, 35, BODY(0.6f, 1.95f), S_entity_zombie_ambient, 160, { 0 },
    { GOAL_FLOAT, GOAL_CHASE_MELEE, GOAL_WANDER, GOAL_LOOK_AT_PLAYER, 0 } },
  { E_cow, CAT_CREATURE, 10, 0.2f, 0, 16, BODY(0.9f, 1.4f), S_entity_cow_ambient, 160, { I_wheat },
    { GOAL_FLOAT, GOAL_PANIC, GOAL_TEMPT, GOAL_WANDER, GOAL_LOOK_AT_PLAYER, 0 } },
  { E_pig, CAT_CREATURE, 10, 0.25f, 0, 16, BODY(0.9f, 0.9f), S_entity_pig_ambient, 160, { I_carrot, I_potato, I_beetroot },
    { GOAL_FLOAT, GOAL_PANIC, GOAL_TEMPT, GOAL_WANDER, GOAL_LOOK_AT_PLAYER, 0 } },
  { E_sheep, CAT_CREATURE, 8, 0.23f, 0, 16, BODY(0.9f, 1.3f), S_entity_sheep_ambient, 160, { I_wheat },
    { GOAL_FLOAT, GOAL_PANIC, GOAL_TEMPT, GOAL_WANDER, GOAL_LOOK_AT_PLAYER, 0 } },
  { E_chicken, CAT_CREATURE, 4, 0.25f, 0, 16, BODY(0.4f, 0.7f), S_entity_chicken_ambient, 160, { I_wheat_seeds, I_melon_seeds, I_pumpkin_seeds },
    { GOAL_FLOAT, GOAL_PANIC, GOAL_TEMPT, GOAL_WANDER, GOAL_LOOK_AT_PLAYER, 0 } },
};

#define SPEED_WANDER 1.0f
#define SPEED_CHASE 1.0f
#define SPEED_PANIC 1.25f
#define SPEED_TEMPT 1.25f
#define PANIC_TICKS 100
#define WANDER_CHANCE 30         // one decision in this many starts a stroll (vanilla: every ~120 ticks)
#define LOOK_RANGE 8
#define TEMPT_RANGE 10
#define ATTACK_COOLDOWN 20

uint32_t mob_decisions_last_tick = 0;
static int path_budget;

const MobType *mob_type (uint16_t entity_type) {
  for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i ++) {
    if (types[i].type == entity_type) return &types[i];
  }
  return NULL;
}

Entity *mob_spawn (uint16_t type, int32_t x, int32_t y, int32_t z) {
  const MobType *t = mob_type(type);
  if (!t) return NULL;
  Entity *e = ent_spawn(type, x, y, z);
  if (!e) return NULL;
  e->health = t->max_health;
  e->yaw = e->head_yaw = (uint8_t)(fast_rand() & 255);
  return e;
}

static uint8_t hasGoal (const MobType *t, uint8_t goal) {
  for (int i = 0; i < 8 && t->goals[i]; i ++) if (t->goals[i] == goal) return 1;
  return 0;
}

static double ex (const Entity *e) { return (double)e->x / ENT_UNIT; }
static double ey (const Entity *e) { return (double)e->y / ENT_UNIT; }
static double ez (const Entity *e) { return (double)e->z / ENT_UNIT; }

// Players a mob may notice: in game, loaded, alive
static uint8_t playerActive (const PlayerData *p) {
  return p->client_fd != -1 && !(p->flags & 0x20) && p->health > 0;
}

static PlayerData *nearestPlayer (const Entity *e, double range, double *dist) {
  PlayerData *best = NULL;
  double best_d = range;
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    PlayerData *p = &player_data[i];
    if (!playerActive(p)) continue;
    double dx = p->x + 0.5 - ex(e), dy = p->y - ey(e), dz = p->z + 0.5 - ez(e);
    double d = sqrt(dx * dx + dy * dy + dz * dz);
    if (d <= best_d) { best_d = d; best = p; }
  }
  if (dist) *dist = best_d;
  return best;
}

static PlayerData *playerById (int32_t id) {
  if (id <= 0) return NULL;
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (player_data[i].client_fd == id) return playerActive(&player_data[i]) ? &player_data[i] : NULL;
  }
  return NULL;
}

static PathNode feetOf (const Entity *e) {
  PathNode n = { (short)floor(ex(e)), (uint8_t)floor(ey(e) + 0.01), (short)floor(ez(e)) };
  return n;
}

static uint8_t requestPath (Entity *e, int x, int y, int z) {
  if (e->u.mob.path_wait) return 0;
  e->u.mob.path_wait = MOB_PATH_EVERY;
  PathNode to = { (short)x, (uint8_t)y, (short)z };
  int n = path_find(feetOf(e), to, e->u.mob.path, 8, &path_budget);
  if (n < 0) { e->u.mob.path_len = 0; return 0; }
  e->u.mob.path_len = (uint8_t)n;
  e->u.mob.path_i = 0;
  return 1;
}

// A standable spot up to `r` blocks away, or 0 if none was found in a few tries
static uint8_t randomSpot (const Entity *e, int r, int *ox, int *oy, int *oz) {
  PathNode f = feetOf(e);
  for (int tries = 0; tries < 10; tries ++) {
    uint32_t rnd = fast_rand();
    int x = f.x + (int)(rnd % (2 * r + 1)) - r;
    int z = f.z + (int)((rnd >> 8) % (2 * r + 1)) - r;
    for (int y = f.y + 3; y >= f.y - 4; y --) {
      if (path_standable(x, y, z)) { *ox = x; *oy = y; *oz = z; return 1; }
    }
  }
  return 0;
}

static uint8_t holdsFood (const PlayerData *p, const MobType *t) {
  uint16_t held = p->inventory_items[p->hotbar];
  for (int i = 0; i < 3; i ++) if (t->food[i] && held == t->food[i]) return 1;
  return 0;
}

static float speedFor (uint8_t goal) {
  switch (goal) {
    case GOAL_PANIC: return SPEED_PANIC;
    case GOAL_TEMPT: return SPEED_TEMPT;
    case GOAL_CHASE_MELEE: return SPEED_CHASE;
    default: return SPEED_WANDER;
  }
}

static uint8_t yawToward (double dx, double dz) {
  double deg = atan2(-dx, dz) * 180.0 / M_PI;
  return (uint8_t)(int)lround(deg * 256.0 / 360.0);
}

// Pushes the mob along (dx, dz) the way vanilla's move control does: on the
// ground the push is speed², in the air or water 0.02 x speed
static void steer (Entity *e, double dx, double dz, float speed) {
  double len = sqrt(dx * dx + dz * dz);
  if (len < 1e-6) return;
  float push = (e->flags & ENT_ON_GROUND) && !(e->flags & ENT_IN_WATER) ? speed * speed : 0.02f * speed;
  e->vx += (float)(dx / len) * push;
  e->vz += (float)(dz / len) * push;
  e->yaw = e->head_yaw = yawToward(dx, dz);
}

// Decides what the mob does next: its highest-priority goal that applies
static void decide (Entity *e, const MobType *t) {
  mob_decisions_last_tick ++;
  double d;

  if (hasGoal(t, GOAL_PANIC) && e->u.mob.panic) {
    if (e->u.mob.goal != GOAL_PANIC || e->u.mob.path_i >= e->u.mob.path_len) {
      int x, y, z;
      if (randomSpot(e, 5, &x, &y, &z)) requestPath(e, x, y, z);
    }
    e->u.mob.goal = GOAL_PANIC;
    return;
  }

  if (hasGoal(t, GOAL_CHASE_MELEE)) {
    PlayerData *p = nearestPlayer(e, t->follow_range, &d);
    if (p) {
      e->u.mob.target = p->client_fd;
      e->u.mob.goal = GOAL_CHASE_MELEE;
      // A new path when the old one is used up or no longer ends near the player
      uint8_t stale = e->u.mob.path_i >= e->u.mob.path_len;
      if (!stale) {
        PathNode end = e->u.mob.path[e->u.mob.path_len - 1];
        stale = abs(end.x - p->x) + abs(end.z - p->z) > 2;
      }
      if (stale && d > 1.5) requestPath(e, p->x, p->y, p->z);
      return;
    }
    e->u.mob.target = -1;
  }

  if (hasGoal(t, GOAL_TEMPT)) {
    for (int i = 0; i < MAX_PLAYERS; i ++) {
      PlayerData *p = &player_data[i];
      if (!playerActive(p) || !holdsFood(p, t)) continue;
      double dx = p->x + 0.5 - ex(e), dz = p->z + 0.5 - ez(e);
      d = sqrt(dx * dx + dz * dz);
      if (d > TEMPT_RANGE) continue;
      e->u.mob.goal = GOAL_TEMPT;
      e->u.mob.target = p->client_fd;
      if (d > 2.5) requestPath(e, p->x, p->y, p->z);
      else e->u.mob.path_len = 0;
      return;
    }
  }

  // Still on its way somewhere: keep going
  if (e->u.mob.path_i < e->u.mob.path_len && e->u.mob.goal == GOAL_WANDER) return;
  e->u.mob.goal = GOAL_NONE;
  e->u.mob.target = -1;

  if (hasGoal(t, GOAL_WANDER) && fast_rand() % WANDER_CHANCE == 0) {
    int x, y, z;
    if (randomSpot(e, 10, &x, &y, &z) && requestPath(e, x, y, z)) {
      e->u.mob.goal = GOAL_WANDER;
      return;
    }
  }

  if (hasGoal(t, GOAL_LOOK_AT_PLAYER) && !e->u.mob.look_ticks && fast_rand() % 12 == 0 &&
      nearestPlayer(e, LOOK_RANGE, NULL)) {
    e->u.mob.look_ticks = 40 + fast_rand() % 40;
  }
}

static void attack (Entity *e, const MobType *t, PlayerData *p) {
  // Vanilla's reach: (2 x its width)² + the target's width, squared distance
  double dx = p->x + 0.5 - ex(e), dy = p->y - ey(e), dz = p->z + 0.5 - ez(e);
  double reach = (2 * t->body.width) * (2 * t->body.width) + 0.6;
  if (dx * dx + dz * dz > reach || fabs(dy) > 1.5 || e->u.mob.attack_cd) return;
  e->u.mob.attack_cd = ATTACK_COOLDOWN;
  hurtEntity(p->client_fd, e->id, D_generic, (uint8_t)t->attack);
}

// Every tick: walk the path (or straight at a close target), attack, look
static void control (Entity *e, const MobType *t) {
  uint8_t goal = e->u.mob.goal;
  float speed = t->speed * speedFor(goal);
  PlayerData *target = (goal == GOAL_CHASE_MELEE || goal == GOAL_TEMPT) ? playerById(e->u.mob.target) : NULL;

  if (e->u.mob.path_i < e->u.mob.path_len) {
    PathNode n = e->u.mob.path[e->u.mob.path_i];
    double dx = n.x + 0.5 - ex(e), dz = n.z + 0.5 - ez(e);
    if (dx * dx + dz * dz < 0.35 * 0.35 && fabs(ey(e) - n.y) < 1.1) {
      e->u.mob.path_i ++;
    } else {
      steer(e, dx, dz, speed);
    }
  } else if (target && goal == GOAL_CHASE_MELEE) {
    // No path (budget, or right next to it): straight at the target
    steer(e, target->x + 0.5 - ex(e), target->z + 0.5 - ez(e), speed);
  }

  if (target && goal == GOAL_CHASE_MELEE) {
    e->head_yaw = yawToward(target->x + 0.5 - ex(e), target->z + 0.5 - ez(e));
    attack(e, t, target);
  } else if (e->u.mob.look_ticks && e->u.mob.path_i >= e->u.mob.path_len) {
    PlayerData *p = nearestPlayer(e, LOOK_RANGE, NULL);
    if (p) {
      uint8_t yaw = yawToward(p->x + 0.5 - ex(e), p->z + 0.5 - ez(e));
      if (yaw != e->head_yaw) { e->head_yaw = yaw; ent_moved(e); }
    }
  }
}

static void ambientSound (const Entity *e, const MobType *t) {
  if (!t->ambient_sound || fast_rand() % t->ambient_every) return;
  uint8_t category = t->category == CAT_MONSTER ? 5 : 6;   // hostile, neutral
  float pitch = (e->flags & ENT_BABY ? 1.5f : 1.0f) + ((float)(fast_rand() % 100) - 50) / 250.0f;
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (!(e->seen_by & ((uint64_t)1 << i)) || player_data[i].client_fd == -1) continue;
    sc_soundEntity(player_data[i].client_fd, t->ambient_sound, category, e->id, 1.0f, pitch);
  }
}

void mob_hurt_reaction (Entity *e, int32_t attacker_id) {
  const MobType *t = mob_type(e->type);
  if (!t) return;
  if (hasGoal(t, GOAL_PANIC)) {
    e->u.mob.panic = PANIC_TICKS;
    e->u.mob.path_len = 0;
    e->u.mob.path_wait = 0;
  } else if (attacker_id > 0) {
    e->u.mob.target = attacker_id;
  }
}

void mobs_tick (void) {
  path_budget = PATH_TICK_BUDGET;
  mob_decisions_last_tick = 0;

  // Nobody in the world: nothing moves, nothing leaves (vanilla simulates
  // only around players)
  uint8_t anyone = 0;
  for (int i = 0; i < MAX_PLAYERS && !anyone; i ++) anyone = playerActive(&player_data[i]);
  if (!anyone) return;

  for (uint32_t i = 0; i < ent_capacity(); i ++) {
    Entity *e = ent_at(i);
    if (e->type == 0 || e->kind != ENT_KIND_MOB) continue;
    const MobType *t = mob_type(e->type);
    e->age ++;

    // Dead: the client plays its fall, then smoke and it is gone
    if (e->flags & ENT_DEAD) {
      if (++ e->timer < (uint16_t)TICKS_PER_SECOND) continue;
      for (int j = 0; j < MAX_PLAYERS; j ++) {
        if (!(e->seen_by & ((uint64_t)1 << j)) || player_data[j].client_fd == -1) continue;
        sc_entityEvent(player_data[j].client_fd, e->id, 60);
      }
      ent_remove(e);
      continue;
    }
    if (!t) continue;

    if (e->u.mob.attack_cd) e->u.mob.attack_cd --;
    if (e->u.mob.path_wait) e->u.mob.path_wait --;
    if (e->u.mob.panic) e->u.mob.panic --;
    if (e->u.mob.look_ticks) e->u.mob.look_ticks --;

    // Far from every player: hostiles go, animals (for now) at twice that
    double d;
    PlayerData *near = nearestPlayer(e, 1e9, &d);
    double limit = t->category == CAT_MONSTER ? MOB_DESPAWN_FAR : MOB_DESPAWN_DISTANCE;
    if (!near || d > limit) {
      ent_remove(e);
      continue;
    }
    // Vanilla's random despawn: a hostile with nobody within 32 blocks for
    // 30 s may go, 1 in 800 a tick
    if (t->category == CAT_MONSTER) {
      if (d > MOB_DESPAWN_NEAR) {
        if (e->u.mob.alone_ticks < 0xFFFF) e->u.mob.alone_ticks ++;
        if (e->u.mob.alone_ticks > MOB_ALONE_TICKS && fast_rand() % 800 == 0) {
          ent_remove(e);
          continue;
        }
      } else {
        e->u.mob.alone_ticks = 0;
      }
    }

    // Hostiles burn in daylight above ground, once a second
    if (t->category == CAT_MONSTER && (world_time < 13000 || world_time > 23460) &&
        ey(e) > 48 && server_ticks % (uint32_t)TICKS_PER_SECOND == 0) {
      hurtEntity(e->id, -1, D_on_fire, 2);
      if (e->flags & ENT_DEAD) continue;
    }

    if ((server_ticks + i) % MOB_DECIDE_EVERY == 0) decide(e, t);
    control(e, t);
    phys_step(e, &t->body);
    ambientSound(e, t);
  }
}

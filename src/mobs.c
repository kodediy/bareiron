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
#include "combat.h"
#include "explosion.h"
#include "entitymeta.h"
#include "spawning.h"
#include "arrows.h"
#include "effects.h"
#include "drops.h"

#define BODY(w, h) { w, h, 0.6f, 0.08f, 0.98f, 0 }
#define BODY_CLIMB(w, h) { w, h, 0.6f, 0.08f, 0.98f, 1 }
#define ZOMBIE_GOALS { GOAL_FLOAT, GOAL_CHASE_MELEE, GOAL_WANDER, GOAL_LOOK_AT_PLAYER, 0 }
#define ANIMAL_GOALS { GOAL_FLOAT, GOAL_PANIC, GOAL_BREED, GOAL_TEMPT, GOAL_WANDER, GOAL_LOOK_AT_PLAYER, 0 }
#define SKELETON_GOALS { GOAL_FLOAT, GOAL_AVOID_SUN, GOAL_RANGED_BOW, GOAL_WANDER, GOAL_LOOK_AT_PLAYER, 0 }

// Vanilla's attributes (Normal difficulty)
static const MobType types[] = {
  { E_zombie, CAT_MONSTER, 20, 0.23f, 3, 35, BODY(0.6f, 1.95f), S_entity_zombie_ambient, 160, { 0 }, ZOMBIE_GOALS, 2, 1 },
  { E_zombie_villager, CAT_MONSTER, 20, 0.23f, 3, 35, BODY(0.6f, 1.95f), S_entity_zombie_villager_ambient, 160, { 0 }, ZOMBIE_GOALS, 2, 1 },
  { E_husk, CAT_MONSTER, 20, 0.23f, 3, 35, BODY(0.6f, 1.95f), S_entity_husk_ambient, 160, { 0 }, ZOMBIE_GOALS, 2, 0 },
  { E_skeleton, CAT_MONSTER, 20, 0.25f, 0, 16, BODY(0.6f, 1.99f), S_entity_skeleton_ambient, 160, { 0 }, SKELETON_GOALS, 0, 1 },
  { E_stray, CAT_MONSTER, 20, 0.25f, 0, 16, BODY(0.6f, 1.99f), S_entity_stray_ambient, 160, { 0 }, SKELETON_GOALS, 0, 1 },
  { E_spider, CAT_MONSTER, 16, 0.3f, 2, 16, BODY_CLIMB(1.4f, 0.9f), S_entity_spider_ambient, 160, { 0 },
    { GOAL_FLOAT, GOAL_LEAP, GOAL_CHASE_MELEE, GOAL_WANDER, GOAL_LOOK_AT_PLAYER, 0 }, 0, 0 },
  { E_creeper, CAT_MONSTER, 20, 0.25f, 0, 16, BODY(0.6f, 1.7f), 0, 160, { 0 },
    { GOAL_FLOAT, GOAL_SWELL, GOAL_CHASE_MELEE, GOAL_WANDER, GOAL_LOOK_AT_PLAYER, 0 }, 0, 0 },
  { E_cow, CAT_CREATURE, 10, 0.2f, 0, 16, BODY(0.9f, 1.4f), S_entity_cow_ambient, 160, { I_wheat },
    ANIMAL_GOALS, 0, 0 },
  { E_pig, CAT_CREATURE, 10, 0.25f, 0, 16, BODY(0.9f, 0.9f), S_entity_pig_ambient, 160, { I_carrot, I_potato, I_beetroot },
    ANIMAL_GOALS, 0, 0 },
  { E_sheep, CAT_CREATURE, 8, 0.23f, 0, 16, BODY(0.9f, 1.3f), S_entity_sheep_ambient, 160, { I_wheat },
    ANIMAL_GOALS, 0, 0 },
  { E_chicken, CAT_CREATURE, 4, 0.25f, 0, 16, BODY(0.4f, 0.7f), S_entity_chicken_ambient, 160, { I_wheat_seeds, I_melon_seeds, I_pumpkin_seeds },
    ANIMAL_GOALS, 0, 0 },
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
#define SWELL_START 3.0            // a creeper lights this close to its target
#define SWELL_STOP 7.0             // and calms down past this
#define SWELL_FUSE 30              // ticks from lit to blast
#define BOW_RANGE 15.0             // a skeleton shoots from up to this far
#define BOW_DRAW 20                // ticks drawing before the arrow leaves
#define BOW_COOLDOWN 20            // and after it: one arrow every 40 ticks
#define BOW_SPEED 1.6f
#define BOW_INACCURACY 6.0f        // vanilla: 14 - 4 x difficulty
#define TOO_CLOSE 4.0              // a skeleton backs off from a player this close
#define FIRE_TICKS 160             // 8 s alight after the sun touches it
#define PROVOKED_TICKS 200
#define BABY_CHANCE 20             // 1 zombie in 20 is a baby
#define BABY_SPEED 1.5f
#define BREED_SEEK 8.0             // a partner within this many blocks
#define BREED_TOUCH 3.0            // close enough to have the baby
#define EAT_GRASS_ADULT 1000       // a sheep eats grass 1 tick in this many
#define EAT_GRASS_BABY 50

// Vanilla's sheep colours: white 81.836 %, light gray, gray and black 5 %
// each, brown 3 %, pink 0.164 % (colour ids: 0 white, 8 light gray, 7 gray,
// 15 black, 12 brown, 6 pink)
static uint8_t sheepColour (void) {
  uint32_t r = fast_rand() % 100000;
  if (r < 81836) return 0;
  if (r < 86836) return 8;
  if (r < 91836) return 7;
  if (r < 96836) return 15;
  if (r < 99836) return 12;
  return 6;
}

uint32_t mob_decisions_last_tick = 0;
static void toTrackers (const Entity *e, void (*send)(const Entity *, int));
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
  if ((type == E_zombie || type == E_husk || type == E_zombie_villager) && fast_rand() % BABY_CHANCE == 0) {
    e->flags |= ENT_BABY;
  }
  if (type == E_sheep) e->u.mob.wool = sheepColour();
  if (type == E_chicken) e->u.mob.egg = 6000 + fast_rand() % 6000;
  if (t->category == CAT_CREATURE) e->flags |= ENT_PERSISTENT;
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
  PathNode to = { (short)x, (uint8_t)y, (short)z };
  int n = path_find(feetOf(e), to, e->u.mob.path, 8, &path_budget);
  if (n == PATH_NO_BUDGET) return 0;   // others used this tick's searching: next time, no wait
  e->u.mob.path_wait = MOB_PATH_EVERY;
  if (n < 0) { e->u.mob.path_len = 0; return 0; }
  e->u.mob.path_to = to;
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

// A spot within 10 blocks with something overhead, out of the sun
static uint8_t shadeSpot (const Entity *e, int *ox, int *oy, int *oz) {
  for (int tries = 0; tries < 10; tries ++) {
    int x, y, z;
    if (!randomSpot(e, 10, &x, &y, &z)) continue;
    if (!spawn_sky_above((short)x, (uint8_t)y, (short)z)) { *ox = x; *oy = y; *oz = z; return 1; }
  }
  return 0;
}

// Whether the mob's eyes see the player's
static uint8_t sees (const Entity *e, const MobType *t, const PlayerData *p) {
  return phys_line_clear(ex(e), ey(e) + t->body.height * 0.85, ez(e), p->x + 0.5, p->y + 1.62, p->z + 0.5);
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

static Entity *findPartner (const Entity *e) {
  Entity *best = NULL;
  double best_d = BREED_SEEK * BREED_SEEK;
  for (uint32_t j = 0; j < ent_capacity(); j ++) {
    Entity *o = ent_at(j);
    if (o == e || o->type != e->type || !o->u.mob.love || o->u.mob.breed_cd || (o->flags & (ENT_BABY | ENT_DEAD))) continue;
    double dx = ex(o) - ex(e), dz = ez(o) - ez(e), d = dx * dx + dz * dz;
    if (d <= best_d) { best_d = d; best = o; }
  }
  return best;
}

static int animalsNear (const Entity *e) {
  int n = 0;
  for (uint32_t j = 0; j < ent_capacity(); j ++) {
    Entity *o = ent_at(j);
    if (o->type == 0 || o->kind != ENT_KIND_MOB) continue;
    const MobType *t = mob_type(o->type);
    if (!t || t->category != CAT_CREATURE) continue;
    double dx = ex(o) - ex(e), dz = ez(o) - ez(e);
    if (dx * dx + dz * dz <= MOB_BREED_RANGE * MOB_BREED_RANGE) n ++;
  }
  return n;
}

static void hearts (const Entity *e, int client_fd) { sc_entityEvent(client_fd, e->id, 18); }

static void breed (Entity *a, Entity *b) {
  a->u.mob.love = b->u.mob.love = 0;
  a->u.mob.path_len = b->u.mob.path_len = 0;
  // The design's one departure from vanilla: a crowded farm stops growing
  if (animalsNear(a) >= MOB_BREED_LIMIT || !spawn_creature_room()) return;
  Entity *baby = mob_spawn(a->type, (a->x + b->x) / 2, a->y, (a->z + b->z) / 2);
  if (!baby) return;
  a->u.mob.breed_cd = b->u.mob.breed_cd = MOB_BREED_COOLDOWN;
  baby->flags |= ENT_BABY;
  baby->u.mob.grow = 0;
  if (a->type == E_sheep) baby->u.mob.wool = (fast_rand() & 1) ? a->u.mob.wool : b->u.mob.wool;
  toTrackers(a, hearts);
}

// Decides what the mob does next: its highest-priority goal that applies
static void decide (Entity *e, const MobType *t) {
  mob_decisions_last_tick ++;
  double d;

  // On fire by day: into the shade first
  if (hasGoal(t, GOAL_AVOID_SUN) && e->u.mob.fire && spawn_is_day()) {
    if (e->u.mob.goal != GOAL_AVOID_SUN || e->u.mob.path_i >= e->u.mob.path_len) {
      int x, y, z;
      if (shadeSpot(e, &x, &y, &z)) requestPath(e, x, y, z);
    }
    e->u.mob.goal = GOAL_AVOID_SUN;
    return;
  }

  if (hasGoal(t, GOAL_RANGED_BOW)) {
    PlayerData *p = nearestPlayer(e, t->follow_range, &d);
    if (p) {
      e->u.mob.target = p->client_fd;
      e->u.mob.goal = GOAL_RANGED_BOW;
      // Closer until it is in range and sees the player; then it stands and shoots
      if (d > BOW_RANGE || !sees(e, t, p)) {
        if (e->u.mob.path_i >= e->u.mob.path_len) requestPath(e, p->x, p->y, p->z);
      } else {
        e->u.mob.path_len = 0;
      }
      return;
    }
    e->u.mob.target = -1;
  }

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
    // A spider in daylight under the sky leaves players alone, unless one hit it
    if (p && e->type == E_spider && !e->u.mob.provoked && spawn_is_day() &&
        spawn_sky_above((short)floor(ex(e)), (uint8_t)floor(ey(e)), (short)floor(ez(e)))) {
      p = NULL;
    }
    if (p) {
      e->u.mob.target = p->client_fd;
      e->u.mob.goal = GOAL_CHASE_MELEE;
      // A new path when the old one is used up or was searched to where the
      // player no longer is (its 8 steps may end well short of the player)
      uint8_t stale = e->u.mob.path_i >= e->u.mob.path_len;
      if (!stale) {
        PathNode to = e->u.mob.path_to;
        stale = abs(to.x - p->x) + abs(to.z - p->z) > 2;
      }
      if (stale && d > 1.5) requestPath(e, p->x, p->y, p->z);
      return;
    }
    e->u.mob.target = -1;
  }

  if (hasGoal(t, GOAL_BREED) && e->u.mob.love && !e->u.mob.breed_cd && !(e->flags & ENT_BABY)) {
    Entity *partner = findPartner(e);
    if (partner) {
      double dx = ex(partner) - ex(e), dz = ez(partner) - ez(e);
      e->u.mob.goal = GOAL_BREED;
      if (dx * dx + dz * dz <= BREED_TOUCH * BREED_TOUCH) {
        breed(e, partner);
      } else if (e->u.mob.path_i >= e->u.mob.path_len) {
        requestPath(e, (int)floor(ex(partner)), (int)floor(ey(partner) + 0.01), (int)floor(ez(partner)));
      }
      return;
    }
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
  if (t->attack <= 0) return;   // a creeper does its harm another way
  // Vanilla's reach: (2 x its width)² + the target's width, squared distance
  double dx = p->x + 0.5 - ex(e), dy = p->y - ey(e), dz = p->z + 0.5 - ez(e);
  double reach = (2 * t->body.width) * (2 * t->body.width) + 0.6;
  if (dx * dx + dz * dz > reach || fabs(dy) > 1.5 || e->u.mob.attack_cd) return;
  if (!sees(e, t, p)) return;   // like vanilla, never through a wall
  e->u.mob.attack_cd = ATTACK_COOLDOWN;
  if (combat_hurt(p->client_fd, e->id, D_mob_attack, t->attack)) combat_knockback(p->client_fd, ex(e), ez(e), COMBAT_KNOCKBACK);
  if (e->type == E_husk) eff_apply(p, EFF_HUNGER, 0, 140);   // 7 s of Hunger
}

static void sendCreeperState (const Entity *e, int client_fd) {
  EntityData state = { META_CREEPER_STATE, META_T_VARINT, { 0 } };
  state.value.pose = e->u.mob.swell_dir > 0 ? 1 : -1;
  sc_setEntityMetadata(client_fd, e->id, &state, 1);
}

static void toTrackers (const Entity *e, void (*send)(const Entity *, int)) {
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (!(e->seen_by & ((uint64_t)1 << i)) || player_data[i].client_fd == -1) continue;
    send(e, player_data[i].client_fd);
  }
}

static void hiss (const Entity *e, int client_fd) {
  sc_soundEntity(client_fd, S_entity_creeper_primed, 5, e->id, 1.0f, 0.5f);
}

// A creeper's fuse. Returns 1 when it went off (and is gone).
static uint8_t swell (Entity *e, PlayerData *target) {
  double d = 1e9;
  if (target) {
    double dx = target->x + 0.5 - ex(e), dz = target->z + 0.5 - ez(e), dy = target->y - ey(e);
    d = sqrt(dx * dx + dy * dy + dz * dz);
  }
  int8_t dir = e->u.mob.swell_dir;
  // Like vanilla, it lights only when it sees its target, and loses the fuse when it doesn't
  uint8_t seen = target && d <= SWELL_STOP && sees(e, mob_type(e->type), target);
  if (seen && d < SWELL_START) dir = 1;
  else if (!seen) dir = -1;
  if (dir != e->u.mob.swell_dir) {
    if (dir > 0 && e->u.mob.swell == 0) toTrackers(e, hiss);
    e->u.mob.swell_dir = dir;
    toTrackers(e, sendCreeperState);
  }
  if (dir > 0) {
    if (++ e->u.mob.swell >= SWELL_FUSE) {
      explode(e->x, e->y, e->z, CREEPER_POWER, e->id);
      ent_remove(e);
      return 1;
    }
    e->u.mob.path_len = 0;   // it stands still while it swells
  } else if (e->u.mob.swell) {
    e->u.mob.swell --;
  }
  return 0;
}

static void sendLivingFlags (const Entity *e, int client_fd) {
  EntityData hand = { META_LIVING_FLAGS, META_T_BYTE, { (uint8_t)(e->u.mob.bow_ticks ? 0x01 : 0) } };
  sc_setEntityMetadata(client_fd, e->id, &hand, 1);
}

static void sendFlags (const Entity *e, int client_fd) {
  EntityData flags = { META_FLAGS, META_T_BYTE, { (uint8_t)((e->flags & ENT_ON_FIRE) ? META_FLAG_ON_FIRE : 0) } };
  sc_setEntityMetadata(client_fd, e->id, &flags, 1);
}

static void twang (const Entity *e, int client_fd) {
  sc_soundEntity(client_fd, S_entity_skeleton_shoot, 5, e->id, 1.0f, 1.0f);
}

static float spread (void) {
  // roughly a normal distribution: the sum of three uniforms
  float s = 0;
  for (int i = 0; i < 3; i ++) s += (float)(fast_rand() % 1000) / 1000.0f - 0.5f;
  return s;
}

// A skeleton's turn: back off when too close, draw, shoot
static void shootAt (Entity *e, const MobType *t, PlayerData *p) {
  double dx = p->x + 0.5 - ex(e), dz = p->z + 0.5 - ez(e);
  double h = sqrt(dx * dx + dz * dz);
  e->head_yaw = e->yaw = yawToward(dx, dz);
  ent_moved(e);
  if (h < TOO_CLOSE) steer(e, -dx, -dz, t->speed);
  if (h > BOW_RANGE || !sees(e, t, p)) {
    if (e->u.mob.bow_ticks) { e->u.mob.bow_ticks = 0; toTrackers(e, sendLivingFlags); }
    return;
  }
  if (!e->u.mob.bow_ticks) {
    if (e->u.mob.attack_cd) return;
    e->u.mob.bow_ticks = BOW_DRAW;
    toTrackers(e, sendLivingFlags);
    return;
  }
  if (-- e->u.mob.bow_ticks) return;
  // vanilla: aim at a third of the target's height, lifted by a fifth of the distance
  double eye = ey(e) + t->body.height * 0.87;
  double dy = p->y + 0.6 - eye + h * 0.2;
  double len = sqrt(dx * dx + dy * dy + dz * dz);
  float k = 0.0075f * BOW_INACCURACY;
  arrow_shoot(e->id, e->x, (int32_t)(eye * ENT_UNIT), e->z,
    (float)(dx / len) + spread() * k, (float)(dy / len) + spread() * k, (float)(dz / len) + spread() * k,
    BOW_SPEED, ARROW_BASE_DAMAGE, 0, e->type == E_stray ? EFF_SLOWNESS : ARROW_NO_EFFECT);
  e->u.mob.attack_cd = BOW_COOLDOWN;
  toTrackers(e, sendLivingFlags);
  toTrackers(e, twang);
}

// Every tick: walk the path (or straight at a close target), attack, look
static void control (Entity *e, const MobType *t) {
  uint8_t goal = e->u.mob.goal;
  float speed = t->speed * speedFor(goal);
  if (e->flags & ENT_BABY) speed *= BABY_SPEED;
  PlayerData *target = (goal == GOAL_CHASE_MELEE || goal == GOAL_TEMPT || goal == GOAL_RANGED_BOW)
    ? playerById(e->u.mob.target) : NULL;

  if (goal == GOAL_RANGED_BOW && target && e->u.mob.path_i >= e->u.mob.path_len) {
    shootAt(e, t, target);
    return;
  }

  // A spider jumps at a target 2 to 4 blocks away (vanilla: 0.4 up, 0.4 at it)
  if (hasGoal(t, GOAL_LEAP) && target && goal == GOAL_CHASE_MELEE && (e->flags & ENT_ON_GROUND) &&
      fast_rand() % 20 == 0) {
    double dx = target->x + 0.5 - ex(e), dz = target->z + 0.5 - ez(e), h = sqrt(dx * dx + dz * dz);
    if (h > 2 && h < 4) {
      e->vx = (float)(dx / h * 0.4) + e->vx * 0.2f;
      e->vz = (float)(dz / h * 0.4) + e->vz * 0.2f;
      e->vy = 0.4f;
    }
  }

  if (hasGoal(t, GOAL_SWELL)) {
    if (swell(e, goal == GOAL_CHASE_MELEE ? target : NULL)) return;
    if (e->u.mob.swell_dir > 0) return;
  }

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

uint8_t mob_feed (Entity *e, PlayerData *p) {
  const MobType *t = mob_type(e->type);
  if (!t || t->category != CAT_CREATURE || (e->flags & ENT_DEAD) || !holdsFood(p, t)) return 0;
  if (e->flags & ENT_BABY) {
    e->u.mob.grow += (MOB_GROW_TICKS - e->u.mob.grow) / 10;   // a fed baby grows a tenth of the way
  } else {
    if (e->u.mob.love || e->u.mob.breed_cd) return 0;
    e->u.mob.love = MOB_LOVE_TICKS;
    toTrackers(e, hearts);
  }
  uint8_t slot = p->hotbar;
  if (GAMEMODE != 1 && -- p->inventory_count[slot] == 0) p->inventory_items[slot] = 0;
  sc_setContainerSlot(p->client_fd, 0, serverSlotToClientSlot(0, slot), p->inventory_count[slot], p->inventory_items[slot]);
  return 1;
}

static void sendWool (const Entity *e, int client_fd) {
  EntityData wool = { META_SHEEP_WOOL, META_T_BYTE, { (uint8_t)((e->u.mob.wool & 0x0F) | ((e->flags & ENT_SHEARED) ? 0x10 : 0)) } };
  sc_setEntityMetadata(client_fd, e->id, &wool, 1);
}

static void sendBaby (const Entity *e, int client_fd) {
  EntityData baby = { META_BABY, META_T_BOOLEAN, { (uint8_t)((e->flags & ENT_BABY) ? 1 : 0) } };
  sc_setEntityMetadata(client_fd, e->id, &baby, 1);
}

static void eatAnimation (const Entity *e, int client_fd) { sc_entityEvent(client_fd, e->id, 10); }

static void cluck (const Entity *e, int client_fd) {
  sc_soundEntity(client_fd, S_entity_chicken_egg, 6, e->id, 1.0f, 1.0f);
}

// Grass a sheep ate, oldest first: it spreads back over the dirt after
// MOB_GRASS_REGROW_TICKS (vanilla grass creeps back the same way), so
// grazing never fills the block-change store. When the list is full the
// oldest grows back at once. A restart forgets the list: those few spots
// stay dirt.
#define GRAZED_MAX 64
typedef struct { short x, z; uint8_t y; uint32_t at; } Grazed;
static Grazed grazed[GRAZED_MAX];
static uint8_t grazed_first = 0, grazed_count = 0;

static void regrowOldest (void) {
  Grazed *g = &grazed[grazed_first];
  grazed_first = (grazed_first + 1) % GRAZED_MAX;
  grazed_count --;
  if (getBlockAt(g->x, g->y, g->z) == B_dirt && isPassableBlock(getBlockAt(g->x, g->y + 1, g->z))) {
    makeBlockChange(g->x, g->y, g->z, B_grass_block);
  }
}

static void grassEaten (int x, int y, int z) {
  if (grazed_count == GRAZED_MAX) regrowOldest();
  grazed[(grazed_first + grazed_count) % GRAZED_MAX] = (Grazed){ (short)x, (short)z, (uint8_t)y, server_ticks };
  grazed_count ++;
}

static void grassTick (void) {
  while (grazed_count && server_ticks - grazed[grazed_first].at >= MOB_GRASS_REGROW_TICKS) regrowOldest();
}

// An animal's day: growing up, sheep grazing, chickens laying
static void animalLife (Entity *e) {
  if (e->u.mob.love) e->u.mob.love --;
  if (e->u.mob.breed_cd) e->u.mob.breed_cd --;
  if (e->flags & ENT_BABY) {
    if (++ e->u.mob.grow >= MOB_GROW_TICKS) {
      e->flags &= ~ENT_BABY;
      toTrackers(e, sendBaby);
    }
  }
  if (e->type == E_sheep && fast_rand() % ((e->flags & ENT_BABY) ? EAT_GRASS_BABY : EAT_GRASS_ADULT) == 0) {
    int x = (int)floor(ex(e)), y = (int)floor(ey(e) + 0.01), z = (int)floor(ez(e));
    if (getBlockAt(x, y - 1, z) == B_grass_block && !makeBlockChange(x, y - 1, z, B_dirt)) {
      grassEaten(x, y - 1, z);
      toTrackers(e, eatAnimation);
      if (e->flags & ENT_SHEARED) { e->flags &= ~ENT_SHEARED; toTrackers(e, sendWool); }
      if (e->flags & ENT_BABY) e->u.mob.grow += 1200;   // a grazing lamb grows a minute
    }
  }
  if (e->type == E_chicken && !(e->flags & ENT_BABY) && e->u.mob.egg && -- e->u.mob.egg == 0) {
    drop_pop(I_egg, 1, ex(e), ey(e) + 0.3, ez(e));
    toTrackers(e, cluck);
    e->u.mob.egg = 6000 + fast_rand() % 6000;
  }
}

void mob_send_extra (const Entity *e, int client_fd) {
  if (e->flags & ENT_BABY) {
    EntityData baby = { META_BABY, META_T_BOOLEAN, { 1 } };
    sc_setEntityMetadata(client_fd, e->id, &baby, 1);
  }
  if (e->flags & ENT_ON_FIRE) sendFlags(e, client_fd);
  if (e->type == E_skeleton || e->type == E_stray) sc_setEquipment(client_fd, e->id, 0, I_bow, 1);
  if (e->type == E_creeper && e->u.mob.swell_dir > 0) sendCreeperState(e, client_fd);
  if (e->type == E_sheep) sendWool(e, client_fd);
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
    e->u.mob.provoked = PROVOKED_TICKS;
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
  grassTick();

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
    if (e->hurt_ticks) e->hurt_ticks --;
    if (e->u.mob.path_wait) e->u.mob.path_wait --;
    if (e->u.mob.panic) e->u.mob.panic --;
    if (e->u.mob.look_ticks) e->u.mob.look_ticks --;
    if (e->u.mob.provoked) e->u.mob.provoked --;

    // Far from every player: hostiles go; animals are kept, and past the
    // simulation distance they wait, frozen, as in unloaded chunks
    double d;
    PlayerData *near = nearestPlayer(e, 1e9, &d);
    if (t->category == CAT_MONSTER) {
      if (!near || d > MOB_DESPAWN_FAR) {
        ent_remove(e);
        continue;
      }
    } else if (!near || d > MOB_SIM_DISTANCE) {
      continue;
    } else {
      animalLife(e);
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

    // The undead catch fire under the open sky by day (checked once a second)
    // and burn for 8 s, 1 damage a second; water puts them out
    if (t->burns && (server_ticks + i) % 20 == 0 && spawn_is_day() && !(e->flags & ENT_IN_WATER) &&
        spawn_sky_above((short)floor(ex(e)), (uint8_t)floor(ey(e) + t->body.height), (short)floor(ez(e)))) {
      e->u.mob.fire = FIRE_TICKS;
    }
    if (e->flags & ENT_IN_WATER) e->u.mob.fire = 0;
    uint8_t burning = e->u.mob.fire > 0;
    if (burning != ((e->flags & ENT_ON_FIRE) != 0)) {
      e->flags ^= ENT_ON_FIRE;
      toTrackers(e, sendFlags);
    }
    if (burning) {
      e->u.mob.fire --;
      if (e->u.mob.fire % 20 == 0) combat_hurt(e->id, -1, D_on_fire, 1);
      if (e->flags & ENT_DEAD) continue;
    }

    if ((server_ticks + i) % MOB_DECIDE_EVERY == 0) decide(e, t);
    control(e, t);
    if (e->type == 0) continue;   // a creeper that went off
    PhysBody body = t->body;
    if (e->flags & ENT_BABY) { body.width /= 2; body.height /= 2; }
    // Standing still on dry ground with nowhere to go: physics every few ticks
    uint8_t idle = (e->flags & ENT_ON_GROUND) && !(e->flags & ENT_IN_WATER) &&
                   e->u.mob.path_i >= e->u.mob.path_len &&
                   fabsf(e->vx) < 1e-3f && fabsf(e->vz) < 1e-3f;
    if (!idle || (server_ticks + i) % MOB_IDLE_PHYSICS_EVERY == 0) phys_step(e, &body);
    ambientSound(e, t);
  }
}

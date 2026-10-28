#ifndef H_MOBS
#define H_MOBS

#include <stdint.h>

#include "entity.h"
#include "physics.h"

// Mobs: a table of types (vanilla's attributes) and goals in priority
// order. Physics runs every tick; each mob decides (target, path) every
// MOB_DECIDE_EVERY ticks, staggered so they do not all decide at once.

#define MOB_DECIDE_EVERY 4
#define MOB_PATH_EVERY 20        // a mob asks for a new path at most this often
#define MOB_DESPAWN_FAR 128      // a hostile further than this from every player goes at once
#define MOB_DESPAWN_NEAR 32      // further than this for MOB_ALONE_TICKS, it may go: 1 in 800 a tick
#define MOB_ALONE_TICKS 600

typedef enum { CAT_MONSTER, CAT_CREATURE, CAT_AMBIENT, CAT_WATER } MobCategory;

enum {
  GOAL_NONE = 0,
  GOAL_FLOAT,           // swim up in water (physics does it)
  GOAL_PANIC,           // after being hit: run at 1.25x for 5 s
  GOAL_CHASE_MELEE,     // follow the nearest player by path and hit within reach
  GOAL_TEMPT,           // follow a player holding its food
  GOAL_WANDER,          // stroll to a spot within 10 blocks now and then
  GOAL_LOOK_AT_PLAYER,  // turn the head to a player within 8 blocks
  GOAL_SWELL,           // a creeper: lit within 3 blocks of its target, out past 7, blows at 30 ticks
};

typedef struct {
  uint16_t type;              // E_*
  MobCategory category;
  float max_health, speed, attack, follow_range;
  PhysBody body;
  uint16_t ambient_sound;     // S_*
  uint16_t ambient_every;     // ticks, on average
  uint16_t food[3];           // I_* that tempt it; 0 = none
  uint8_t goals[8];           // GOAL_* in priority order, 0-terminated
  uint8_t armor;              // armor points (a zombie has 2)
} MobType;

const MobType *mob_type (uint16_t entity_type);
Entity *mob_spawn (uint16_t type, int32_t x, int32_t y, int32_t z); // sets health; sent by the next network turn
void mobs_tick (void);
void mob_hurt_reaction (Entity *e, int32_t attacker_id);   // a hit: panic or fight back

// How many mobs decided in the last mobs_tick (for the stagger test and the bench)
extern uint32_t mob_decisions_last_tick;

#endif

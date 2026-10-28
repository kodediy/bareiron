#include <math.h>
#include <stdio.h>
#include <string.h>

#include "globals.h"
#include "tools.h"
#include "registries.h"
#include "packets.h"
#include "procedures.h"
#include "entity.h"
#include "mobs.h"
#include "combat.h"
#include "drops.h"
#include "effects.h"

// What a player's combat needs from tick to tick and nobody saves
typedef struct {
  uint32_t last_swing;     // server tick of its last attack
  uint32_t invulnerable;   // no full damage before this tick
  float    last_damage;    // the hit that started the invulnerability
  float    carry;          // damage below a whole point, kept for the next hit
  double   last_y;
  uint8_t  on_ground;
  uint8_t  falling;
} PlayerCombat;

static PlayerCombat state[MAX_PLAYERS];

static PlayerCombat *stateOf (const PlayerData *p) {
  return &state[p - player_data];
}

void combat_forget_player (PlayerData *p) {
  memset(stateOf(p), 0, sizeof(PlayerCombat));
  stateOf(p)->on_ground = 1;
}

void combat_note_movement (PlayerData *p, double y, uint8_t on_ground) {
  PlayerCombat *s = stateOf(p);
  s->falling = !on_ground && y < s->last_y;
  s->on_ground = on_ground;
  s->last_y = y;
}

// Vanilla 1.21: attack damage (with the hand's 1) and attacks per second
static void weapon (uint16_t item, float *damage, float *speed, uint8_t *sword) {
  *sword = 0;
  switch (item) {
    case I_wooden_sword: case I_golden_sword: *damage = 4; *speed = 1.6f; *sword = 1; return;
    case I_stone_sword: *damage = 5; *speed = 1.6f; *sword = 1; return;
    case I_iron_sword: *damage = 6; *speed = 1.6f; *sword = 1; return;
    case I_diamond_sword: *damage = 7; *speed = 1.6f; *sword = 1; return;
    case I_netherite_sword: *damage = 8; *speed = 1.6f; *sword = 1; return;
    case I_wooden_axe: case I_golden_axe: *damage = 7; *speed = item == I_wooden_axe ? 0.8f : 1.0f; return;
    case I_stone_axe: *damage = 9; *speed = 0.8f; return;
    case I_iron_axe: *damage = 9; *speed = 0.9f; return;
    case I_diamond_axe: *damage = 9; *speed = 1.0f; return;
    case I_netherite_axe: *damage = 10; *speed = 1.0f; return;
    case I_wooden_pickaxe: case I_golden_pickaxe: *damage = 2; *speed = 1.2f; return;
    case I_stone_pickaxe: *damage = 3; *speed = 1.2f; return;
    case I_iron_pickaxe: *damage = 4; *speed = 1.2f; return;
    case I_diamond_pickaxe: *damage = 5; *speed = 1.2f; return;
    case I_netherite_pickaxe: *damage = 6; *speed = 1.2f; return;
    case I_wooden_shovel: case I_golden_shovel: *damage = 2.5f; *speed = 1.0f; return;
    case I_stone_shovel: *damage = 3.5f; *speed = 1.0f; return;
    case I_iron_shovel: *damage = 4.5f; *speed = 1.0f; return;
    case I_diamond_shovel: *damage = 5.5f; *speed = 1.0f; return;
    case I_netherite_shovel: *damage = 6.5f; *speed = 1.0f; return;
    default: *damage = 1; *speed = 4.0f; return;
  }
}

float combat_charge (const PlayerData *p) {
  float damage, speed;
  uint8_t sword;
  weapon(p->inventory_items[p->hotbar], &damage, &speed, &sword);
  const PlayerCombat *s = &state[p - player_data];
  float ticks = (float)(server_ticks - s->last_swing) + 0.5f;
  float f = ticks / (20.0f / speed);
  return f > 1 ? 1 : f;
}

float combat_player_attack (PlayerData *p, uint8_t *crit, uint8_t *sweep) {
  float base, speed;
  uint8_t sword;
  weapon(p->inventory_items[p->hotbar], &base, &speed, &sword);
  float f = combat_charge(p);
  PlayerCombat *s = stateOf(p);
  float damage = base * (0.2f + f * f * 0.8f);
  *crit = f > 0.9f && s->falling;
  *sweep = sword && f > 0.9f && s->on_ground && !(p->flags & 0x08) && !*crit;
  if (*crit) damage *= 1.5f;
  s->last_swing = server_ticks;
  return damage;
}

float combat_armor (float damage, int armor, int toughness) {
  float reduce = armor - 4.0f * damage / (toughness + 8.0f);
  if (reduce < armor / 5.0f) reduce = armor / 5.0f;
  if (reduce > 20) reduce = 20;
  return damage * (1 - reduce / 25.0f);
}

static int toughnessOf (uint16_t item) {
  switch (item) {
    case I_diamond_helmet: case I_diamond_chestplate: case I_diamond_leggings: case I_diamond_boots: return 2;
    case I_netherite_helmet: case I_netherite_chestplate: case I_netherite_leggings: case I_netherite_boots: return 3;
    default: return 0;
  }
}

static int playerToughness (const PlayerData *p) {
  int t = 0;
  for (int slot = 36; slot <= 39; slot ++) t += toughnessOf(p->inventory_items[slot]);
  return t;
}

void combat_knockback (int32_t target_id, double from_x, double from_z, float strength) {
  if (target_id < -1) {
    Entity *e = ent_by_id(target_id);
    if (!e) return;
    double dx = (double)e->x / ENT_UNIT - from_x, dz = (double)e->z / ENT_UNIT - from_z;
    double len = sqrt(dx * dx + dz * dz);
    if (len < 1e-4) { dx = 0; dz = 1; len = 1; }
    // vanilla: half the speed it had, plus the push; up only from the ground
    e->vx = e->vx / 2 + (float)(dx / len) * strength;
    e->vz = e->vz / 2 + (float)(dz / len) * strength;
    if (e->flags & ENT_ON_GROUND) e->vy = fminf(0.4f, e->vy / 2 + strength);
    return;
  }
  PlayerData *p;
  if (getPlayerData(target_id, &p)) return;
  double dx = p->x + 0.5 - from_x, dz = p->z + 0.5 - from_z;
  double len = sqrt(dx * dx + dz * dz);
  if (len < 1e-4) { dx = 0; dz = 1; len = 1; }
  // The client moves the player: it is told its new speed
  sc_setEntityMotion(p->client_fd, p->client_fd,
    (float)(dx / len) * strength, stateOf(p)->on_ground ? 0.4f : 0.0f, (float)(dz / len) * strength);
}

// Everyone who should see this entity's hurt and death: a player is seen by
// all, a mob by those tracking it
static void broadcastHurt (int32_t target_id, uint8_t damage_type, uint8_t died, const Entity *e) {
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    int fd = player_data[i].client_fd;
    if (fd == -1) continue;
    if (e && !(e->seen_by & ((uint64_t)1 << i))) continue;
    sc_damageEvent(fd, target_id, damage_type);
    if (!died) continue;
    sc_entityEvent(fd, target_id, 3);
    if (target_id > 0) sc_systemChat(fd, (char *)recv_buffer, strlen((char *)recv_buffer));
  }
}

// Fills recv_buffer with the death message
static void deathMessage (const PlayerData *player, int32_t attacker_id, uint8_t damage_type, float amount) {
  const char *name = player->name;
  if (damage_type == D_fall && amount > 8) snprintf((char *)recv_buffer, 64, "%s fell from a high place", name);
  else if (damage_type == D_fall) snprintf((char *)recv_buffer, 64, "%s hit the ground too hard", name);
  else if (damage_type == D_lava) snprintf((char *)recv_buffer, 64, "%s tried to swim in lava", name);
  else if (attacker_id < -1) snprintf((char *)recv_buffer, 64, "%s was slain by a mob", name);
  else if (attacker_id > 0) {
    PlayerData *attacker;
    if (getPlayerData(attacker_id, &attacker)) snprintf((char *)recv_buffer, 64, "%s died", name);
    else snprintf((char *)recv_buffer, 64, "%s was slain by %s", name, attacker->name);
  }
  else if (damage_type == D_cactus) snprintf((char *)recv_buffer, 64, "%s was pricked to death", name);
  else snprintf((char *)recv_buffer, 64, "%s died", name);
}

// The part of a hit that counts, given the invulnerability window
static float throughInvulnerability (uint32_t *until, float *last, float amount, uint8_t window_open) {
  if (window_open) {
    if (amount <= *last) return 0;
    float extra = amount - *last;
    *last = amount;
    return extra;
  }
  *until = server_ticks + COMBAT_INVULNERABLE_TICKS;
  *last = amount;
  return amount;
}

static void hurtPlayer (int32_t target_id, int32_t attacker_id, uint8_t damage_type, float amount) {
  PlayerData *player;
  if (getPlayerData(target_id, &player)) return;
  if (player->health == 0) return;
  PlayerCombat *s = stateOf(player);
  float dealt = throughInvulnerability(&s->invulnerable, &s->last_damage, amount, server_ticks < s->invulnerable);
  if (dealt <= 0) return;
  if (damage_type != D_fall && damage_type != D_starve) {
    dealt = combat_armor(dealt, getPlayerDefensePoints(player), playerToughness(player));
  }
  dealt += s->carry;
  int whole = (int)dealt;
  s->carry = dealt - whole;
  uint8_t died = 0;
  if (whole >= player->health) {
    player->health = 0;
    died = 1;
    deathMessage(player, attacker_id, damage_type, amount);
    drops_spill_inventory(player);
    eff_clear(player);
  } else {
    player->health -= whole;
  }
  sc_setHealth(target_id, player->health, player->hunger, player->saturation);
  broadcastHurt(target_id, damage_type, died, NULL);
}

static void hurtMob (int32_t target_id, int32_t attacker_id, uint8_t damage_type, float amount) {
  Entity *mob = ent_by_id(target_id);
  if (!mob || mob->kind != ENT_KIND_MOB || (mob->flags & ENT_DEAD)) return;
  uint32_t until = server_ticks + mob->hurt_ticks;
  float dealt = throughInvulnerability(&until, &mob->last_damage, amount, mob->hurt_ticks > 0);
  if (dealt <= 0) return;
  mob->hurt_ticks = (uint8_t)(until - server_ticks);
  const MobType *t = mob_type(mob->type);
  if (t && t->armor) dealt = combat_armor(dealt, t->armor, 0);
  mob_hurt_reaction(mob, attacker_id);
  uint8_t died = 0;
  if (mob->health <= dealt) {
    mob->health = 0;
    mob->flags |= ENT_DEAD;
    mob->timer = 0;   // counts the death animation's ticks
    died = 1;
    drop_loot(mob, attacker_id);
  } else {
    mob->health -= dealt;
  }
  broadcastHurt(target_id, damage_type, died, mob);
}

void combat_hurt (int32_t target_id, int32_t attacker_id, uint8_t damage_type, float amount) {
  if (amount <= 0) return;
  if (target_id > 0) hurtPlayer(target_id, attacker_id, damage_type, amount);
  else hurtMob(target_id, attacker_id, damage_type, amount);
}

void combat_attack (PlayerData *attacker, int32_t target_id) {
  if (target_id == attacker->client_fd || attacker->health == 0) return;
  uint8_t crit, sweep;
  float damage = combat_player_attack(attacker, &crit, &sweep);
  float knock = COMBAT_KNOCKBACK + ((attacker->flags & 0x08) ? COMBAT_SPRINT_KNOCKBACK : 0);
  double ax = attacker->x + 0.5, az = attacker->z + 0.5;

  combat_hurt(target_id, attacker->client_fd, D_player_attack, damage);
  combat_knockback(target_id, ax, az, knock);

  Entity *target = target_id < -1 ? ent_by_id(target_id) : NULL;
  if (crit) {
    for (int i = 0; i < MAX_PLAYERS; i ++) {
      if (player_data[i].client_fd == -1) continue;
      if (target && !(target->seen_by & ((uint64_t)1 << i))) continue;
      sc_entityAnimation(player_data[i].client_fd, target_id, 4);   // critical hit sparks
    }
  }
  if (sweep && target) {
    // The sword's sweep: 1 to everything right next to the target
    for (uint32_t j = 0; j < ent_capacity(); j ++) {
      Entity *other = ent_at(j);
      if (other->type == 0 || other == target || other->kind != ENT_KIND_MOB) continue;
      double dx = (double)(other->x - target->x) / ENT_UNIT, dz = (double)(other->z - target->z) / ENT_UNIT;
      double dy = (double)(other->y - target->y) / ENT_UNIT;
      if (dx * dx + dz * dz > 1.5 * 1.5 || fabs(dy) > 0.5) continue;
      combat_hurt(other->id, attacker->client_fd, D_player_attack, 1);
      combat_knockback(other->id, ax, az, COMBAT_KNOCKBACK);
    }
  }
}

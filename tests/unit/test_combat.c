// Vanilla 1.21 combat: charge, crits, armor, invulnerability
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "bareiron.h"
#include "globals.h"
#include "registries.h"
#include "blockindex.h"
#include "entity.h"
#include "mobs.h"
#include "combat.h"

static PlayerData *setup (void) {
  static BareironPlatform plat;
  plat = *bareiron_posix_platform();
  platform = &plat;
  free(block_changes);
  block_changes_cap = 100;
  block_changes = malloc(block_changes_cap * sizeof(BlockChange));
  for (uint32_t i = 0; i < block_changes_cap; i ++) block_changes[i].block = 0xFF;
  bc_index_free();
  CHECK(bc_index_init(block_changes_cap) == 0);
  ent_free();
  CHECK(ent_init(16) == 0);
  for (int i = 0; i < MAX_PLAYERS; i ++) player_data[i].client_fd = -1;
  PlayerData *p = &player_data[0];
  memset(p, 0, sizeof(*p));
  p->client_fd = 1000;
  p->x = 0; p->y = 70; p->z = 0;
  p->health = 20;
  combat_forget_player(p);
  server_ticks = 1000;
  return p;
}

static int near (float a, float b) { return fabsf(a - b) < 1e-3f; }

static void test_charge_scales_damage (void) {
  PlayerData *p = setup();
  p->inventory_items[0] = I_iron_sword;
  p->hotbar = 0;
  combat_note_movement(p, 70.0, 1);
  uint8_t crit, sweep;
  CHECK(near(combat_player_attack(p, &crit, &sweep), 6.0f));   // fully charged: the sword's 6
  server_ticks += 6;                                            // 6 ticks into a 12.5-tick swing
  float f = combat_charge(p);
  CHECK(near(f, 6.5f / 12.5f));
  float dmg = combat_player_attack(p, &crit, &sweep);
  CHECK(near(dmg, 6.0f * (0.2f + f * f * 0.8f)));
  CHECK(near(6.0f * (0.2f + 0.5f * 0.5f * 0.8f), 2.4f));      // the plan's figure for half a charge
  CHECK(!crit && !sweep);
}

static void test_crit_only_falling_and_charged (void) {
  PlayerData *p = setup();
  p->inventory_items[0] = I_iron_sword;
  uint8_t crit, sweep;
  combat_note_movement(p, 71.0, 0);
  combat_note_movement(p, 70.5, 0);                // in the air, going down
  float dmg = combat_player_attack(p, &crit, &sweep);
  CHECK(crit && near(dmg, 9.0f));
  server_ticks += 40;
  combat_note_movement(p, 70.0, 1);                // on the ground
  dmg = combat_player_attack(p, &crit, &sweep);
  CHECK(!crit && sweep && near(dmg, 6.0f));        // a full-charge sword hit on the ground sweeps
  server_ticks += 3;
  combat_note_movement(p, 71.0, 0);
  combat_note_movement(p, 70.5, 0);
  combat_player_attack(p, &crit, &sweep);
  CHECK(!crit);                                    // falling, but not charged
}

static void test_armor_formula (void) {
  // dmg · (1 − min(20, max(a/5, a − 4·dmg/(t+8)))/25)
  CHECK(near(combat_armor(10, 20, 8), 3.0f));      // max(4, 20 − 40/16 = 17.5) → 10 · (1 − 0.7)
  CHECK(near(combat_armor(4, 0, 0), 4.0f));
  CHECK(near(combat_armor(5, 10, 0), 3.5f));       // max(2, 10 − 2.5) = 7.5
  CHECK(near(combat_armor(20, 10, 0), 18.4f));     // a big hit wears armor down: max(2, 10 − 10) = 2 → 20 · 0.92
}

static void test_invulnerability_takes_only_the_difference (void) {
  setup();
  Entity *cow = mob_spawn(E_cow, 0, 70 * ENT_UNIT, 0);
  int32_t id = ent_id(cow);
  combat_hurt(id, -1, D_generic, 5);
  CHECK(near(cow->health, 5));
  combat_hurt(id, -1, D_generic, 3);               // smaller, within 10 ticks: nothing
  CHECK(near(cow->health, 5));
  combat_hurt(id, -1, D_generic, 8);               // bigger: the difference
  CHECK(near(cow->health, 2));
  for (int t = 0; t < COMBAT_INVULNERABLE_TICKS; t ++) { server_ticks ++; mobs_tick(); }
  combat_hurt(id, -1, D_generic, 1);
  CHECK(near(cow->health, 1));
}

static void test_player_armor_and_invulnerability (void) {
  PlayerData *p = setup();
  p->inventory_items[38] = I_diamond_chestplate;   // 8 points, toughness 2
  combat_hurt(p->client_fd, -1, D_generic, 10);
  // max(8/5, 8 − 40/10 = 4) = 4 → 10 · 0.84 = 8.4 → 8 off (the rest carries)
  CHECK(p->health == 12);
  combat_hurt(p->client_fd, -1, D_generic, 5);
  CHECK(p->health == 12);
}

// Clicking again inside the invulnerability window does no damage, so no push
static void test_no_knockback_without_damage (void) {
  PlayerData *p = setup();
  Entity *cow = mob_spawn(E_cow, 3 * ENT_UNIT, 70 * ENT_UNIT, 0);
  int32_t id = ent_id(cow);
  combat_attack(p, id);
  CHECK(cow->vx > 0);
  cow->vx = 0; cow->vz = 0;
  server_ticks += 1;                               // a weak, quick second click
  combat_attack(p, id);
  CHECK(cow->vx == 0);
  cow->health = 0.5f;
  server_ticks += 40; cow->hurt_ticks = 0;         // the window over, as mobs_tick would count it
  combat_attack(p, id);                            // the kill
  CHECK(cow->flags & ENT_DEAD);
  cow->vx = 0;
  server_ticks += 40; cow->hurt_ticks = 0;
  combat_attack(p, id);                            // hitting the body
  CHECK(cow->vx == 0);
}

int main (void) {
  RUN(test_charge_scales_damage);
  RUN(test_crit_only_falling_and_charged);
  RUN(test_armor_formula);
  RUN(test_invulnerability_takes_only_the_difference);
  RUN(test_player_armor_and_invulnerability);
  RUN(test_no_knockback_without_damage);
  return CHECK_DONE();
}

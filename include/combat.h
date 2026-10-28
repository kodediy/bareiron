#ifndef H_COMBAT
#define H_COMBAT

#include <stdint.h>

#include "globals.h"

// Damage as in vanilla 1.21 (Normal): the attack charge scales a player's
// hit, critical hits when falling, the sword's sweep, knockback both ways,
// 10 ticks of invulnerability after a hit (a bigger hit in that time does
// the difference), and armor with toughness.

#define COMBAT_INVULNERABLE_TICKS 10
#define COMBAT_KNOCKBACK 0.4f
#define COMBAT_SPRINT_KNOCKBACK 0.5f

// Any damage: a player (id > 0) or an entity (id < -1). attacker_id is the
// player or entity behind it, or -1.
uint8_t combat_hurt (int32_t target_id, int32_t attacker_id, uint8_t damage_type, float amount);

// A player's swing at target_id: charge, crit, sweep, knockback
void  combat_attack (PlayerData *attacker, int32_t target_id);

// The damage a player's swing does now, and whether it is a crit or sweeps;
// resets the charge
float combat_player_attack (PlayerData *p, uint8_t *crit, uint8_t *sweep);
float combat_charge (const PlayerData *p);   // 0..1, vanilla's attack strength

// What gets through armor (points) and toughness
float combat_armor (float damage, int armor, int toughness);

// The client's movement report: on the ground or not, and its height (for
// crits, which need the player to be falling)
void  combat_note_movement (PlayerData *p, double y, uint8_t on_ground);
void  combat_forget_player (PlayerData *p);   // a new player in this slot

// Pushes an entity or a player away from (from_x, from_z), in blocks
void  combat_knockback (int32_t target_id, double from_x, double from_z, float strength);

#endif

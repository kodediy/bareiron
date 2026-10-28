#ifndef H_ARROWS
#define H_ARROWS

#include <stdint.h>

#include "entity.h"

// Arrows as in vanilla: shot with a speed along a direction, they fall
// (0.05 a tick), slow down (x0.99, x0.6 in water), hurt what they hit for
// speed x damage, rounded up, and stick in blocks. A player's arrows can be
// picked up again; a skeleton's cannot.

#define ARROW_GRAVITY 0.05f
#define ARROW_DRAG 0.99f
#define ARROW_WATER_DRAG 0.6f
#define ARROW_BASE_DAMAGE 2.0f
#define ARROW_STUCK_LIFETIME 1200   // ticks in a block before it vanishes
#define ARROW_NO_EFFECT 0xFF

// dir need not be unit length. effect: EFF_* applied to a player it hits, or ARROW_NO_EFFECT
Entity *arrow_shoot (int32_t shooter_id, int32_t x, int32_t y, int32_t z,
                     float dir_x, float dir_y, float dir_z, float speed, float damage,
                     uint8_t can_pickup, uint8_t effect);
void arrows_tick (void);

// The bow: drawn with Use Item, shot when let go
float bow_power (uint32_t ticks);          // (f² + 2f) / 3, f = seconds drawn, at most 1
void  bow_start (PlayerData *p);
void  bow_release (PlayerData *p);
void  arrows_send_extra (const Entity *e, int client_fd);   // velocity and flags to a new tracker

#endif

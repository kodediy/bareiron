#ifndef H_EFFECTS
#define H_EFFECTS

#include <stdint.h>

#include "globals.h"

// Status effects on players, as in vanilla: the client is told when one
// starts and when it ends, and the server applies what it owns (hunger's
// exhaustion). The witch's effects come in Plan 6.

enum { EFF_HUNGER, EFF_SLOWNESS, EFF_COUNT };

#define EFF_EXHAUSTION_PER_POINT 4.0f   // vanilla: this much exhaustion costs a point
#define EFF_SATURATION_UNIT 500         // bareiron's saturation per vanilla point

void     eff_apply (PlayerData *p, uint8_t effect, uint8_t amplifier, uint16_t ticks); // stronger, or as strong and longer, wins
void     eff_tick (PlayerData *p);                    // each tick; Remove Mob Effect when one ends
void     eff_clear (PlayerData *p);                   // death or a new player in the slot
int      eff_alloc (void *(*alloc) (size_t size));    // see tables_alloc
uint16_t eff_ticks_left (const PlayerData *p, uint8_t effect);
uint8_t  eff_amplifier (const PlayerData *p, uint8_t effect);
float    eff_speed_factor (const PlayerData *p);      // slowness: 1 - 0.15 (amp + 1)
void     eff_exhaust (PlayerData *p, float amount);   // hunger and saturation drain, vanilla's way

#endif

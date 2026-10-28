#ifndef H_BEDS
#define H_BEDS

#include <stdint.h>

#include "globals.h"

// Beds as in vanilla: two blocks, foot where it is placed and head the way
// the player faces; sleeping at night with no monster close; the night
// skips once every player in the game sleeps (Manuel's decision: all of
// them, as vanilla); the bed becomes where its sleeper comes back to life.

#define BED_SLEEP_TICKS 100        // asleep this long, all of them, and it is morning
#define BED_MONSTER_RANGE 8        // no monster this close (5 up and down)

uint8_t bed_is_part (uint8_t block);
// Places a bed with its foot at (x, y, z), facing where p looks. 1 if placed.
uint8_t bed_place (PlayerData *p, short x, uint8_t y, short z);
// A broken part takes the other with it
void    bed_break (short x, uint8_t y, short z, uint8_t block);
// p uses the bed at (x, y, z): sleeps, or is told why not. 1 if it was a bed.
uint8_t bed_use (PlayerData *p, short x, uint8_t y, short z);
void    bed_wake (PlayerData *p);
uint8_t bed_sleeping (const PlayerData *p);
void    beds_tick (void);
// Where p comes back to life: its bed, if the bed is still there
uint8_t bed_spawn (const PlayerData *p, short *x, uint8_t *y, short *z);
void    beds_forget (PlayerData *p);   // a new player in this slot

#endif

// The recipes wave 1 needs: bow, arrows, TNT, bed, bone meal, flint and steel; flint from gravel
#include <string.h>

#include "check.h"
#include "globals.h"
#include "registries.h"
#include "crafting.h"
#include "procedures.h"

static PlayerData p;

static void grid (const uint16_t g[9]) {
  memset(&p, 0, sizeof(p));
  for (int i = 0; i < 9; i ++) { p.craft_items[i] = g[i]; p.craft_count[i] = g[i] ? 1 : 0; }
}

static int makes (const uint16_t g[9], uint16_t item, uint8_t count) {
  uint16_t out_item; uint8_t out_count;
  grid(g);
  getCraftingOutput(&p, &out_count, &out_item);
  return out_item == item && out_count == count;
}

#define S I_stick
#define T I_string
#define G I_gunpowder
#define A I_sand
#define W I_white_wool
#define P I_oak_planks

static void test_recipes_wave1 (void) {
  const uint16_t bow[9] = { 0, S, T,  S, 0, T,  0, S, T };
  const uint16_t bow_mirrored[9] = { T, S, 0,  T, 0, S,  T, S, 0 };
  CHECK(makes(bow, I_bow, 1));
  CHECK(makes(bow_mirrored, I_bow, 1));
  const uint16_t arrow[9] = { 0, I_flint, 0,  0, S, 0,  0, I_feather, 0 };
  const uint16_t arrow_left[9] = { I_flint, 0, 0,  S, 0, 0,  I_feather, 0, 0 };
  CHECK(makes(arrow, I_arrow, 4));
  CHECK(makes(arrow_left, I_arrow, 4));               // anywhere in the grid
  const uint16_t tnt[9] = { G, A, G,  A, G, A,  G, A, G };
  CHECK(makes(tnt, I_tnt, 1));
  const uint16_t bed[9] = { W, W, W,  P, P, P,  0, 0, 0 };
  const uint16_t bed_low[9] = { 0, 0, 0,  W, W, W,  P, P, P };
  CHECK(makes(bed, I_white_bed, 1));
  CHECK(makes(bed_low, I_white_bed, 1));
  const uint16_t bone[9] = { 0, 0, 0,  0, I_bone, 0,  0, 0, 0 };
  CHECK(makes(bone, I_bone_meal, 3));
  const uint16_t flint_steel[9] = { I_iron_ingot, 0, 0,  0, I_flint, 0,  0, 0, 0 };
  CHECK(makes(flint_steel, I_flint_and_steel, 1));     // shapeless
  const uint16_t not_tnt[9] = { G, A, G,  A, A, A,  G, A, G };
  CHECK(!makes(not_tnt, I_tnt, 1));
}

static void test_gravel_gives_flint (void) {
  int flint = 0;
  for (int i = 0; i < 1000; i ++) if (getMiningResult(I_wooden_shovel, B_gravel) == I_flint) flint ++;
  CHECK(flint >= 60 && flint <= 140);                  // 1 in 10
}

// Every player slot crafts: PlayerData is packed and 221 bytes long, so in
// every other slot craft_items sits at an odd address. The Kode Dot's
// RISC-V core panics on a misaligned 16-bit load, and run.sh makes UBSan
// stop on one as well.
static void test_every_slot_crafts (void) {
  const uint16_t tnt[9] = { G, A, G,  A, G, A,  G, A, G };
  for (int i = 0; i < 4; i ++) {
    PlayerData *q = &player_data[i];
    memset(q, 0, sizeof(*q));
    for (int k = 0; k < 9; k ++) { q->craft_items[k] = tnt[k]; q->craft_count[k] = 1; }
    uint16_t out_item; uint8_t out_count;
    getCraftingOutput(q, &out_count, &out_item);
    CHECK(out_item == I_tnt && out_count == 1);
  }
}

int main (void) {
  RUN(test_recipes_wave1);
  RUN(test_gravel_gives_flint);
  RUN(test_every_slot_crafts);
  return CHECK_DONE();
}

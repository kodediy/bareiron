#ifndef H_PHYSICS
#define H_PHYSICS

#include <stdint.h>

#include "entity.h"

// Vanilla's movement of a living entity, one tick at a time: its velocity
// moves its box through the blocks one axis at a time (Y, then X, then Z),
// a blocked axis stops, gravity and drag follow. Blocks are whole cubes:
// solid is whatever isPassableBlock says is not passable.

typedef struct {
  float width, height;   // the box, in blocks
  float step_up;         // a ledge this high is walked onto; higher ones are jumped when blocked
  float gravity;         // per tick: 0.08 for mobs
  float drag;            // vertical drag per tick: 0.98
} PhysBody;

#define PHYS_JUMP 0.42f        // vanilla's jump velocity
#define PHYS_GROUND_FRICTION (0.6f * 0.91f)
#define PHYS_AIR_FRICTION 0.91f
#define PHYS_WATER_DRAG 0.8f

// One tick: moves e by its velocity (blocks per tick), stops against
// blocks, jumps a one-block ledge when walking into it on the ground, floats
// in water, then applies gravity and friction. Sets ENT_ON_GROUND and
// ENT_IN_WATER, and marks e moved when it moved.
void phys_step (Entity *e, const PhysBody *b);

// 1 if the box with its feet centred at (x, y, z) (ENT_UNIT) touches no solid block
int phys_box_free (int32_t x, int32_t y, int32_t z, const PhysBody *b);

#endif

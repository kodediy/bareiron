#include <math.h>

#include "globals.h"
#include "registries.h"
#include "worldgen.h"
#include "procedures.h"
#include "physics.h"

// How far inside a face still counts as touching it: more than the
// rounding of a position to 1/4096, so a box resting against a wall is not
// "inside" it on the next tick
#define FACE_EPS 1e-3
// Longest stretch moved in one go, so nothing tunnels through a block
#define MAX_SUBSTEP 0.25

static int solidAt (int x, int y, int z) {
  if (y < 0) return 1;
  if (y > 255) return 0;
  return !isPassableBlock(getBlockAt(x, y, z));
}

static int isWater (uint8_t block) {
  return block >= B_water && block < B_water + 8;
}

static int boxHits (double x, double y, double z, const PhysBody *b) {
  double half = b->width / 2;
  int x0 = (int)floor(x - half + FACE_EPS), x1 = (int)floor(x + half - FACE_EPS);
  int y0 = (int)floor(y + FACE_EPS), y1 = (int)floor(y + b->height - FACE_EPS);
  int z0 = (int)floor(z - half + FACE_EPS), z1 = (int)floor(z + half - FACE_EPS);
  for (int bx = x0; bx <= x1; bx ++)
    for (int by = y0; by <= y1; by ++)
      for (int bz = z0; bz <= z1; bz ++)
        if (solidAt(bx, by, bz)) return 1;
  return 0;
}

int phys_box_free (int32_t x, int32_t y, int32_t z, const PhysBody *b) {
  return !boxHits((double)x / ENT_UNIT, (double)y / ENT_UNIT, (double)z / ENT_UNIT, b);
}

// Moves one axis (0 = x, 1 = y, 2 = z) by d, in short stretches. On contact
// the box is put against the face it hit. Returns 1 if it was stopped.
static int moveAxis (double pos[3], int axis, double d, const PhysBody *b) {
  if (d == 0) return 0;
  int n = (int)ceil(fabs(d) / MAX_SUBSTEP);
  double step = d / n, half = b->width / 2;
  for (int i = 0; i < n; i ++) {
    double next[3] = { pos[0], pos[1], pos[2] };
    next[axis] += step;
    if (!boxHits(next[0], next[1], next[2], b)) {
      pos[axis] = next[axis];
      continue;
    }
    if (axis == 1) {
      if (step < 0) pos[1] = floor(next[1] + FACE_EPS) + 1;            // feet on the block's top
      else pos[1] = floor(next[1] + b->height - FACE_EPS) - b->height;  // head under its bottom
    } else {
      if (step > 0) pos[axis] = floor(next[axis] + half - FACE_EPS) - half;
      else pos[axis] = floor(next[axis] - half + FACE_EPS) + 1 + half;
    }
    return 1;
  }
  return 0;
}

void phys_step (Entity *e, const PhysBody *b) {
  double pos[3] = { (double)e->x / ENT_UNIT, (double)e->y / ENT_UNIT, (double)e->z / ENT_UNIT };
  double start[3] = { pos[0], pos[1], pos[2] };
  uint8_t was_on_ground = e->flags & ENT_ON_GROUND;

  int stopped_y = moveAxis(pos, 1, e->vy, b);
  uint8_t on_ground = stopped_y && e->vy < 0;
  if (stopped_y) e->vy = 0;
  int stopped_x = moveAxis(pos, 0, e->vx, b);
  int stopped_z = moveAxis(pos, 2, e->vz, b);

  // Walking into a ledge one block high: jump it, as a mob's move control
  // does. A wall two blocks high stops it.
  uint8_t jumped = 0;
  if ((stopped_x || stopped_z) && (on_ground || was_on_ground) &&
      !boxHits(pos[0] + (stopped_x ? e->vx : 0), pos[1] + 1.0 + FACE_EPS,
               pos[2] + (stopped_z ? e->vz : 0), b)) {
    e->vy = PHYS_JUMP;
    jumped = 1;
  }
  if (stopped_x) e->vx = 0;
  if (stopped_z) e->vz = 0;

  int bx = (int)floor(pos[0]), bz = (int)floor(pos[2]);
  uint8_t in_water =
    isWater(getBlockAt(bx, (int)floor(pos[1]), bz)) ||
    isWater(getBlockAt(bx, (int)floor(pos[1] + b->height * 0.5), bz));

  if (in_water) {
    e->vx *= PHYS_WATER_DRAG;
    e->vz *= PHYS_WATER_DRAG;
    e->vy = e->vy * PHYS_WATER_DRAG - 0.02f;
    // Swims up while its eyes are under water, like a mob's float goal
    if (isWater(getBlockAt(bx, (int)floor(pos[1] + b->height * 0.85), bz))) e->vy += 0.04f;
  } else {
    float friction = on_ground ? PHYS_GROUND_FRICTION : PHYS_AIR_FRICTION;
    e->vx *= friction;
    e->vz *= friction;
    // The jump's own tick keeps its full speed; gravity takes over from the next
    if (!jumped) e->vy = (e->vy - b->gravity) * b->drag;
  }

  e->flags = (e->flags & ~(ENT_ON_GROUND | ENT_IN_WATER)) |
             (on_ground ? ENT_ON_GROUND : 0) | (in_water ? ENT_IN_WATER : 0);
  e->x = (int32_t)llround(pos[0] * ENT_UNIT);
  e->y = (int32_t)llround(pos[1] * ENT_UNIT);
  e->z = (int32_t)llround(pos[2] * ENT_UNIT);
  if (pos[0] != start[0] || pos[1] != start[1] || pos[2] != start[2]) ent_moved(e);
}

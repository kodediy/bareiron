#ifndef H_PATHFIND
#define H_PATHFIND

#include <stdint.h>

// A* over the block grid for a mob two blocks tall: a node is where its feet
// stand (solid below, room for feet and head). From a node it can walk to a
// neighbour on the same level (diagonals when both sides are open), step up
// one block, or drop up to three. It never stands in or on lava.
//
// Every node expanded costs one from *budget, which the caller shares out
// over a tick; a single search also stops at PATH_SEARCH_MAX expansions.

#define PATH_TICK_BUDGET 200   // nodes all searches may expand in one tick (one full search)
#define PATH_SEARCH_MAX 200    // nodes one search may expand

typedef struct { short x; uint8_t y; short z; } PathNode;

// Returns the number of nodes written to out (start excluded, the first
// max_out of the path), 0 if already there, -1 if no path within budget.
int path_find (PathNode from, PathNode to, PathNode *out, int max_out, int *budget);

// 1 if a two-block-tall mob can stand with its feet at (x, y, z)
int path_standable (int x, int y, int z);

#endif

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

#define PATH_NO_BUDGET -2      // path_find ran out of this tick's budget: ask again next tick

typedef struct { short x; uint8_t y; short z; } PathNode;

extern uint32_t path_searches;   // searches started so far

// Returns the number of nodes written to out (start excluded, the first
// max_out of the path), 0 if already there, -1 if no path within
// PATH_SEARCH_MAX expansions, PATH_NO_BUDGET if this tick's budget ran out.
int path_find (PathNode from, PathNode to, PathNode *out, int max_out, int *budget);

// 1 if a two-block-tall mob can stand with its feet at (x, y, z)
int path_standable (int x, int y, int z);

#endif

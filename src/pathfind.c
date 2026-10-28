#include <stdlib.h>
#include <string.h>

#include "globals.h"
#include "registries.h"
#include "worldgen.h"
#include "procedures.h"
#include "pathfind.h"

// Room for every node one search can create: each expansion adds at most 8
#define POOL (PATH_SEARCH_MAX * 8 + 1)
#define HASH 4096
#define COST_STRAIGHT 10
#define COST_DIAGONAL 14
#define COST_CLIMB 4      // on top of the step across: a jump is a little slower
#define COST_DROP 2       // per block fallen

typedef struct {
  short x; uint8_t y; short z;
  int g, f;
  int parent;
  uint8_t closed;
} Node;

// The search's memory lives in big memory (PSRAM on the Kode Dot), where
// it does not take internal RAM the network needs
typedef struct {
  Node pool[POOL];
  int16_t hash_slot[HASH];
  int heap[POOL];
} SearchMemory;
static SearchMemory *mem = NULL;
#define pool (mem->pool)
#define hash_slot (mem->hash_slot)
#define heap (mem->heap)
static int pool_used;
static int heap_size;

static int isLava (uint8_t b) { return b >= B_lava && b < B_lava + 4; }

// The blocks one search has looked at: neighbouring nodes ask for the same
// ones again and again, and on the Kode Dot a lookup costs microseconds
#define BLOCK_CACHE 1024
typedef struct { short x, z; uint8_t y, block; uint16_t gen; } CachedBlock;
static CachedBlock *block_cache = NULL;
static uint16_t cache_gen = 0;   // a new search makes every older entry stale

static uint8_t blockAt (int x, int y, int z) {
  if (y < 0 || y > 255) return getBlockAt(x, y, z);
  unsigned h = ((unsigned)(x * 73856093) ^ (unsigned)(y * 19349663) ^ (unsigned)(z * 83492791)) & (BLOCK_CACHE - 1);
  CachedBlock *c = &block_cache[h];
  if (c->gen == cache_gen && c->x == x && c->y == y && c->z == z) return c->block;
  uint8_t b = getBlockAt(x, y, z);
  c->x = (short)x; c->y = (uint8_t)y; c->z = (short)z; c->block = b; c->gen = cache_gen;
  return b;
}

// The cache only holds during a search; outside one the world may have changed
static uint8_t searching = 0;
static uint8_t lookup (int x, int y, int z) {
  return searching ? blockAt(x, y, z) : getBlockAt(x, y, z);
}

// Room for feet or head: passable and not lava
static int open (int x, int y, int z) {
  if (y > 255) return 1;
  if (y < 0) return 0;
  uint8_t b = lookup(x, y, z);
  return isPassableBlock(b) && !isLava(b);
}

int path_standable (int x, int y, int z) {
  if (y < 1 || y > 254) return 0;
  uint8_t below = lookup(x, y - 1, z);
  if (isPassableBlock(below) || isLava(below)) return 0;
  return open(x, y, z) && open(x, y + 1, z);
}

static unsigned hashOf (int x, int y, int z) {
  return ((unsigned)(x * 73856093) ^ (unsigned)(y * 19349663) ^ (unsigned)(z * 83492791)) & (HASH - 1);
}

static int findNode (int x, int y, int z) {
  for (unsigned h = hashOf(x, y, z), n = 0; n < HASH; h = (h + 1) & (HASH - 1), n ++) {
    int i = hash_slot[h];
    if (i < 0) return -1;
    if (pool[i].x == x && pool[i].y == y && pool[i].z == z) return i;
  }
  return -1;
}

static void putHash (int i) {
  unsigned h = hashOf(pool[i].x, pool[i].y, pool[i].z);
  while (hash_slot[h] >= 0) h = (h + 1) & (HASH - 1);
  hash_slot[h] = (int16_t)i;
}

static void heapPush (int i) {
  int k = heap_size ++;
  heap[k] = i;
  while (k > 0) {
    int up = (k - 1) / 2;
    if (pool[heap[up]].f <= pool[heap[k]].f) break;
    int t = heap[up]; heap[up] = heap[k]; heap[k] = t;
    k = up;
  }
}

static int heapPop (void) {
  int top = heap[0];
  heap[0] = heap[-- heap_size];
  int k = 0;
  while (1) {
    int l = 2 * k + 1, r = l + 1, m = k;
    if (l < heap_size && pool[heap[l]].f < pool[heap[m]].f) m = l;
    if (r < heap_size && pool[heap[r]].f < pool[heap[m]].f) m = r;
    if (m == k) break;
    int t = heap[m]; heap[m] = heap[k]; heap[k] = t;
    k = m;
  }
  return top;
}

// Octile distance plus height: never more than the real cost
static int estimate (int x, int y, int z, PathNode to) {
  int dx = abs(x - to.x), dz = abs(z - to.z), dy = abs(y - to.y);
  int lo = dx < dz ? dx : dz, hi = dx < dz ? dz : dx;
  return lo * COST_DIAGONAL + (hi - lo) * COST_STRAIGHT + dy * COST_DROP;
}

static void consider (int from, int x, int y, int z, int cost, PathNode to) {
  int g = pool[from].g + cost;
  int i = findNode(x, y, z);
  if (i >= 0) {
    if (pool[i].closed || pool[i].g <= g) return;
    pool[i].g = g;
    pool[i].f = g + estimate(x, y, z, to);
    pool[i].parent = from;
    heapPush(i);   // a stale copy may stay in the heap; it is skipped once closed
    return;
  }
  if (pool_used == POOL) return;
  i = pool_used ++;
  pool[i] = (Node){ (short)x, (uint8_t)y, (short)z, g, g + estimate(x, y, z, to), from, 0 };
  putHash(i);
  heapPush(i);
}

static void expand (int i, PathNode to) {
  int x = pool[i].x, y = pool[i].y, z = pool[i].z;
  static const int dirs[8][2] = { {1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}, {1,-1}, {-1,1}, {-1,-1} };
  for (int d = 0; d < 8; d ++) {
    int dx = dirs[d][0], dz = dirs[d][1], nx = x + dx, nz = z + dz;
    int diagonal = dx && dz;
    if (diagonal) {
      // Only across an open corner, never squeezing between two blocks
      if (!open(x + dx, y, z) || !open(x + dx, y + 1, z) || !open(x, y, z + dz) || !open(x, y + 1, z + dz)) continue;
      if (path_standable(nx, y, nz)) consider(i, nx, y, nz, COST_DIAGONAL, to);
      continue;
    }
    if (path_standable(nx, y, nz)) {
      consider(i, nx, y, nz, COST_STRAIGHT, to);
    } else if (!open(nx, y, nz)) {
      // A block in the way: up onto it, with room overhead for the jump
      if (open(x, y + 2, z) && path_standable(nx, y + 1, nz)) consider(i, nx, y + 1, nz, COST_STRAIGHT + COST_CLIMB, to);
    } else if (open(nx, y + 1, nz)) {
      // Nothing to stand on: down, three blocks at most
      for (int drop = 1; drop <= 3; drop ++) {
        if (!open(nx, y - drop, nz)) break;
        if (path_standable(nx, y - drop, nz)) {
          consider(i, nx, y - drop, nz, COST_STRAIGHT + drop * COST_DROP, to);
          break;
        }
      }
    }
  }
}

static int search (PathNode from, PathNode to, PathNode *out, int max_out, int *budget);

uint32_t path_searches = 0;

int path_find (PathNode from, PathNode to, PathNode *out, int max_out, int *budget) {
  if (*budget <= 0) return PATH_NO_BUDGET;
  path_searches ++;
  searching = 1;
  int n = search(from, to, out, max_out, budget);
  searching = 0;
  return n;
}

static int search (PathNode from, PathNode to, PathNode *out, int max_out, int *budget) {
  if (from.x == to.x && from.y == to.y && from.z == to.z) return 0;
  if (!mem) {
    mem = platform->alloc_large(sizeof(SearchMemory));
    block_cache = platform->alloc_large(BLOCK_CACHE * sizeof(CachedBlock));
    if (!mem || !block_cache) { mem = NULL; return -1; }
    memset(block_cache, 0, BLOCK_CACHE * sizeof(CachedBlock));
  }
  memset(hash_slot, 0xFF, sizeof(hash_slot));
  if (++ cache_gen == 0) {   // wrapped: clear once so no stale entry matches
    memset(block_cache, 0, BLOCK_CACHE * sizeof(CachedBlock));
    cache_gen = 1;
  }
  pool_used = 0;
  heap_size = 0;
  pool[0] = (Node){ from.x, from.y, from.z, 0, estimate(from.x, from.y, from.z, to), -1, 0 };
  pool_used = 1;
  putHash(0);
  heapPush(0);

  int expanded = 0;
  while (heap_size > 0) {
    int i = heapPop();
    if (pool[i].closed) continue;
    if (pool[i].x == to.x && pool[i].y == to.y && pool[i].z == to.z) {
      int len = 0;
      for (int k = i; pool[k].parent >= 0; k = pool[k].parent) len ++;
      int n = len < max_out ? len : max_out;
      // walk back to the first n nodes after the start
      int k = i;
      for (int skip = len - n; skip > 0; skip --) k = pool[k].parent;
      for (int w = n - 1; w >= 0; w --) {
        out[w] = (PathNode){ pool[k].x, pool[k].y, pool[k].z };
        k = pool[k].parent;
      }
      return n;
    }
    if (expanded == PATH_SEARCH_MAX) return -1;
    if (*budget <= 0) return PATH_NO_BUDGET;
    (*budget) --;
    expanded ++;
    pool[i].closed = 1;
    expand(i, to);
  }
  return -1;
}

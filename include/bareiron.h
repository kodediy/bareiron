#ifndef H_BAREIRON
#define H_BAREIRON

// Public API for embedding bareiron in a host program (a PC binary, or a
// firmware such as KodeCraft on the Kode Dot). Everything that depends on
// the platform reaches the server through BareironPlatform.

#include <stddef.h>
#include <stdint.h>

typedef enum {
  BAREIRON_EVT_READY,
  BAREIRON_EVT_PLAYER_JOIN,
  BAREIRON_EVT_PLAYER_LEAVE,
  BAREIRON_EVT_WORLD_SAVED,
  BAREIRON_EVT_STOPPED,
  BAREIRON_EVT_ERROR
} BareironEventType;

typedef struct {
  BareironEventType type;
  char name[16];
  int code;
} BareironEvent;

typedef struct {
  void   *(*alloc_large) (size_t size);   // big buffers (PSRAM on the Dot); NULL on failure
  void    (*free_large) (void *ptr);
  void    (*lock) (void);                  // guards the status snapshot
  void    (*unlock) (void);
  int64_t (*now_us) (void);                // monotonic microseconds
  void    (*yield) (void);                 // called once per loop iteration
  void    (*on_event) (const BareironEvent *event); // may be NULL
  // Takes ownership of `data` (allocated with alloc_large): write it now
  // or hand it to another task, and free_large it once written. Called on
  // the server thread, so it should return quickly. 0 = accepted.
  int     (*save_world) (void *data, size_t len);
  int     (*load_world) (void *data, size_t capacity, size_t *len); // 0 = ok, 1 = no world yet, <0 = error
} BareironPlatform;

typedef struct {
  uint16_t port;               // 25565
  uint32_t world_seed;         // used only when there is no saved world
  uint32_t max_block_changes;  // capacity of block_changes
  uint32_t entity_capacity;    // mobs, items, arrows at once; 0 = 512
  const BareironPlatform *platform;
} BareironConfig;

// Most clients that can be connected at once
#define BAREIRON_MAX_CONNECTIONS 8

typedef struct {
  char name[16];
  short x;
  uint8_t y;
  short z;
  int64_t joined_us;
} BareironPlayerInfo;

typedef struct {
  uint8_t  running;
  uint8_t  player_count;                         // players in game (not status pings)
  BareironPlayerInfo players[BAREIRON_MAX_CONNECTIONS];
  uint16_t world_time;                           // 0..23999
  int64_t  started_us;
  uint32_t loop_max_us;                          // worst loop iteration in the last second
  uint32_t block_changes_used;
  // Where the time goes: the worst of each in the last second, in microseconds
  uint32_t tick_max_us;                          // one server tick, all of it
  uint32_t spawn_max_us, mobs_max_us, things_max_us, net_max_us;  // parts of a tick
  uint32_t chunks_max_us;                        // chunks sent in one loop iteration
  uint8_t  catchup_max;                          // most ticks run in one iteration
  uint16_t entities;                             // mobs, items, arrows alive
} BareironStatus;

// Error codes returned by bareiron_run
#define BAREIRON_ERR_SOCKET -1
#define BAREIRON_ERR_BIND -2
#define BAREIRON_ERR_LISTEN -3
#define BAREIRON_ERR_NO_MEMORY -4
#define BAREIRON_ERR_BAD_WORLD -5

// Runs the server on the calling thread until bareiron_request_stop is
// called. Returns 0 after a requested stop, or a negative error code if
// the server could not start.
int bareiron_run (const BareironConfig *config);

// Asks a running server to stop. Safe to call from any thread; the
// server returns from bareiron_run within a second.
void bareiron_request_stop (void);

// Copies the latest server status. Safe to call from any thread.
void bareiron_get_status (BareironStatus *out);

// The last lines of the game's chat, as players saw them: what was said
// ("<name> text"), who joined or left and who died. Oldest first, at most
// `max` (the newest ones); *seq counts every line ever added, so a host can
// tell when there is something new. Thread-safe.
#define BAREIRON_CHAT_LINES 8
#define BAREIRON_CHAT_LEN 96
int bareiron_get_chat (char out[][BAREIRON_CHAT_LEN], int max, uint32_t *seq);

// The seed a new world gets when the server starts with `config_seed` (the
// config's seed is mixed before use), so a host can know the terrain ahead
uint32_t bareiron_seed_for (uint32_t config_seed);

// What a saved world says about itself, read without loading it. Players
// with no name never played and are left out.
#define BAREIRON_MAX_SAVED_PLAYERS 64   // a build check keeps it equal to MAX_PLAYERS
typedef struct {
  char name[16];
  short x;
  uint8_t y;
  short z;
} BareironSavedPlayer;

typedef struct {
  uint32_t seed;
  uint16_t world_time;
  uint8_t  player_count;                         // entries used in players
  BareironSavedPlayer players[BAREIRON_MAX_SAVED_PLAYERS];
} BareironWorldInfo;

// Reads the header and players of a world snapshot, touching no server
// state. 0 = ok; -1 = not a bareiron world, cut short, or another version.
int bareiron_peek_world (const void *snapshot, size_t len, BareironWorldInfo *out);

// Platform implementation for POSIX systems (macOS, Linux)
const BareironPlatform *bareiron_posix_platform (void);

#endif

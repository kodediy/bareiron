#ifndef H_STATUS
#define H_STATUS

#include "globals.h"

// Keeps the snapshot that bareiron_get_status hands to the host, and
// sends events to the host through platform->on_event

void statusReset (void);
void statusEmit (BareironEventType type, const char *name, int code);
void statusPlayerJoined (PlayerData *player);
void statusPlayerLeft (PlayerData *player);
void statusSetRunning (uint8_t running);
// The worst of each part in the current second; statusUpdate publishes and clears it
typedef struct {
  uint32_t tick, spawn, mobs, things, net, chunks;
  uint8_t catchup;
} TickProfile;
extern TickProfile tick_profile;
#define PROFILE_MAX(field, us) do { uint32_t _u = (uint32_t)(us); if (_u > tick_profile.field) tick_profile.field = _u; } while (0)

// Refreshes positions, world time and counters; call about once a second
void statusUpdate (uint32_t loop_max_us);
// Players in the game right now, not counting status pings or logins
int  statusPlayersOnline (void);

#endif

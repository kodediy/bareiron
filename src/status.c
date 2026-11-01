#include <string.h>

#include "globals.h"
#include "entity.h"
#include "tools.h"
#include "status.h"

static BareironStatus status;
// When each remembered player entered the game, by player_data index
static int64_t *joined_us = NULL;   // MAX_PLAYERS, see tables_alloc

// The chat's last lines, a ring; chat_seq counts every line ever added
static char (*chat)[BAREIRON_CHAT_LEN] = NULL;   // BAREIRON_CHAT_LINES of them
static uint32_t chat_seq;

int statusAlloc (void *(*alloc) (size_t size)) {
  if (!joined_us) {
    joined_us = alloc(MAX_PLAYERS * sizeof(int64_t));
    if (joined_us) memset(joined_us, 0, MAX_PLAYERS * sizeof(int64_t));
  }
  if (!chat) {
    chat = alloc(BAREIRON_CHAT_LINES * BAREIRON_CHAT_LEN);
    if (chat) memset(chat, 0, BAREIRON_CHAT_LINES * BAREIRON_CHAT_LEN);
  }
  return (joined_us && chat) ? 0 : -1;
}

void statusReset (void) {
  memset(&status, 0, sizeof(status));
  memset(joined_us, 0, MAX_PLAYERS * sizeof(int64_t));
  memset(chat, 0, BAREIRON_CHAT_LINES * BAREIRON_CHAT_LEN);
  chat_seq = 0;
}

void statusChat (const char *line) {
  platform->lock();
  char *slot = chat[chat_seq % BAREIRON_CHAT_LINES];
  strncpy(slot, line, BAREIRON_CHAT_LEN - 1);
  slot[BAREIRON_CHAT_LEN - 1] = '\0';
  chat_seq ++;
  platform->unlock();
}

int bareiron_get_chat (char out[][BAREIRON_CHAT_LEN], int max, uint32_t *seq) {
  if (!chat) {
    // The server never started: no chat yet
    if (seq) *seq = 0;
    return 0;
  }
  platform->lock();
  uint32_t kept = chat_seq < BAREIRON_CHAT_LINES ? chat_seq : BAREIRON_CHAT_LINES;
  int n = (int)kept < max ? (int)kept : max;
  for (int i = 0; i < n; i ++) {
    memcpy(out[i], chat[(chat_seq - (uint32_t)n + (uint32_t)i) % BAREIRON_CHAT_LINES], BAREIRON_CHAT_LEN);
  }
  if (seq) *seq = chat_seq;
  platform->unlock();
  return n;
}

void statusEmit (BareironEventType type, const char *name, int code) {
  if (!platform->on_event) return;
  BareironEvent event = { .type = type, .code = code };
  if (name) strncpy(event.name, name, sizeof(event.name) - 1);
  platform->on_event(&event);
}

int statusPlayersOnline (void) {
  int online = 0;
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (player_data[i].client_fd == -1) continue;
    if (player_data[i].flags & 0x20) continue;
    online ++;
  }
  return online;
}

// Rebuilds the snapshot from player_data; the caller holds the lock
static void fillPlayers (void) {
  status.player_count = 0;
  for (int i = 0; i < MAX_PLAYERS && status.player_count < MAX_CONNECTIONS; i ++) {
    PlayerData *player = &player_data[i];
    if (player->client_fd == -1) continue;
    if (player->flags & 0x20) continue;
    BareironPlayerInfo *info = &status.players[status.player_count ++];
    memcpy(info->name, player->name, sizeof(info->name));
    info->name[sizeof(info->name) - 1] = '\0';
    info->x = player->x;
    info->y = player->y;
    info->z = player->z;
    info->joined_us = joined_us[i];
  }
}

void statusSetRunning (uint8_t running) {
  platform->lock();
  status.running = running;
  if (running) status.started_us = get_program_time();
  else status.player_count = 0;
  platform->unlock();
}

void statusPlayerJoined (PlayerData *player) {
  joined_us[player - player_data] = get_program_time();
  platform->lock();
  fillPlayers();
  platform->unlock();
  statusEmit(BAREIRON_EVT_PLAYER_JOIN, player->name, 0);
}

void statusPlayerLeft (PlayerData *player) {
  platform->lock();
  fillPlayers();
  platform->unlock();
  statusEmit(BAREIRON_EVT_PLAYER_LEAVE, player->name, 0);
}

TickProfile tick_profile;

void statusUpdate (uint32_t loop_max_us) {
  uint32_t used = 0;
  for (int i = 0; i < block_changes_count; i ++) {
    if (block_changes[i].block != 0xFF) used ++;
  }
  platform->lock();
  fillPlayers();
  status.world_time = world_time;
  status.loop_max_us = loop_max_us;
  status.block_changes_used = used;
  status.tick_max_us = tick_profile.tick;
  status.spawn_max_us = tick_profile.spawn;
  status.mobs_max_us = tick_profile.mobs;
  status.things_max_us = tick_profile.things;
  status.net_max_us = tick_profile.net;
  status.chunks_max_us = tick_profile.chunks;
  status.catchup_max = tick_profile.catchup;
  status.entities = (uint16_t)ent_count();
  platform->unlock();
  memset(&tick_profile, 0, sizeof(tick_profile));
}

void bareiron_get_status (BareironStatus *out) {
  if (!platform) {
    memset(out, 0, sizeof(*out));
    return;
  }
  platform->lock();
  *out = status;
  platform->unlock();
}

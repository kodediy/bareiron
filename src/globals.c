#include <stdio.h>
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
#endif
#include <unistd.h>

#include "globals.h"
#include "beds.h"
#include "chunkqueue.h"
#include "combat.h"
#include "effects.h"
#include "mobs.h"
#include "status.h"
#include "worldgen.h"


const BareironPlatform *platform = NULL;

ssize_t recv_count;
uint8_t recv_buffer[MAX_RECV_BUF_LEN] = {0};

uint32_t world_seed = INITIAL_WORLD_SEED;
uint32_t rng_seed = INITIAL_RNG_SEED;

uint16_t world_time = 0;
uint32_t server_ticks = 0;

char motd[] = { "KodeCraft" };
uint8_t motd_len = sizeof(motd) - 1;

#ifdef SEND_BRAND
  char brand[] = { "kodecraft" };
  uint8_t brand_len = sizeof(brand) - 1;
#endif

uint16_t client_count;

BlockChange *block_changes = NULL;
uint32_t block_changes_cap = 0;
int block_changes_count = 0;

PlayerData *player_data = NULL;
int player_data_count = 0;

// See globals.h
int tables_alloc (void *(*alloc) (size_t size)) {
  if (!player_data) {
    player_data = alloc(MAX_PLAYERS * sizeof(PlayerData));
    if (player_data) memset(player_data, 0, MAX_PLAYERS * sizeof(PlayerData));
  }
  if (!chunk_section) {
    chunk_section = alloc(4096);
    if (chunk_section) memset(chunk_section, 0, 4096);
  }
  if (!player_data || !chunk_section) return -1;
  if (combat_alloc(alloc) || eff_alloc(alloc) || beds_alloc(alloc)) return -1;
  if (cq_alloc(alloc) || mobs_alloc(alloc) || statusAlloc(alloc)) return -1;
  return 0;
}


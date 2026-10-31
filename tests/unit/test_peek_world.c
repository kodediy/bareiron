// Reading a saved world's seed, time and players without loading it
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "check.h"
#include "bareiron.h"
#include "globals.h"
#include "tools.h"
#include "serialize.h"

static void resetWorld (void) {
  if (block_changes) free(block_changes);
  block_changes_cap = 2000;
  block_changes = malloc(block_changes_cap * sizeof(BlockChange));
  for (uint32_t i = 0; i < block_changes_cap; i ++) block_changes[i].block = 0xFF;
  block_changes_count = 0;
  memset(player_data, 0, sizeof(player_data));
  for (int i = 0; i < MAX_PLAYERS; i ++) player_data[i].client_fd = -1;
  player_data_count = 0;
}

static void addPlayer (int slot, const char *name, short x, uint8_t y, short z) {
  player_data[slot].uuid[0] = slot + 1;
  snprintf(player_data[slot].name, 16, "%s", name);
  player_data[slot].x = x;
  player_data[slot].y = y;
  player_data[slot].z = z;
  player_data_count ++;
}

static uint8_t *goodWorld (size_t *size) {
  resetWorld();
  world_seed = 0xA103DE6A;
  world_time = 13500;
  addPlayer(0, "Kody", 10, 70, -5);
  addPlayer(1, "Manu", -3, 64, 8);
  *size = worldSnapshotSize();
  uint8_t *buffer = malloc(*size);
  CHECK(worldSnapshotWrite(buffer, *size) == *size);
  return buffer;
}

static void test_peek_reads_seed_time_and_players (void) {
  size_t size;
  uint8_t *buffer = goodWorld(&size);
  // Reading must not touch the loaded world
  world_seed = 7;
  world_time = 8;
  BareironWorldInfo info;
  memset(&info, 0xEE, sizeof(info));
  CHECK(bareiron_peek_world(buffer, size, &info) == 0);
  CHECK(info.seed == 0xA103DE6A);
  CHECK(info.world_time == 13500);
  CHECK(info.player_count == 2);
  CHECK(strcmp(info.players[0].name, "Kody") == 0);
  CHECK(info.players[0].x == 10 && info.players[0].y == 70 && info.players[0].z == -5);
  CHECK(strcmp(info.players[1].name, "Manu") == 0);
  CHECK(info.players[1].x == -3 && info.players[1].y == 64 && info.players[1].z == 8);
  CHECK(world_seed == 7 && world_time == 8);
  free(buffer);
}

static void test_peek_skips_players_who_never_played (void) {
  resetWorld();
  world_seed = 1;
  world_time = 2;
  addPlayer(3, "Late", 1, 2, 3);
  addPlayer(5, "", 4, 5, 6);              // a slot with no name never played
  size_t size = worldSnapshotSize();
  uint8_t *buffer = malloc(size);
  CHECK(worldSnapshotWrite(buffer, size) == size);
  BareironWorldInfo info;
  CHECK(bareiron_peek_world(buffer, size, &info) == 0);
  CHECK(info.player_count == 1);
  CHECK(strcmp(info.players[0].name, "Late") == 0 && info.players[0].z == 3);
  free(buffer);
}

static void test_peek_refuses_garbage_and_short (void) {
  size_t size;
  uint8_t *buffer = goodWorld(&size);
  BareironWorldInfo info;
  uint8_t garbage[16] = { 0x13, 0x77, 0xA0, 0x5C, 0x99, 0x01, 0xFE, 0x42, 0x10, 0x8B, 0x3D, 0xC4, 0x6E, 0x27, 0xD9, 0x50 };
  CHECK(bareiron_peek_world(garbage, sizeof(garbage), &info) == -1);
  CHECK(bareiron_peek_world(buffer, size / 2, &info) == -1);
  CHECK(bareiron_peek_world(buffer, size - 1, &info) == -1);
  CHECK(bareiron_peek_world(buffer, 0, &info) == -1);
  CHECK(bareiron_peek_world(NULL, 0, &info) == -1);
  buffer[0] ^= 0xFF;                      // bad magic
  CHECK(bareiron_peek_world(buffer, size, &info) == -1);
  buffer[0] ^= 0xFF;
  buffer[4] = 9;                          // a version this code does not know
  CHECK(bareiron_peek_world(buffer, size, &info) == -1);
  free(buffer);
}

// A version 1 world (no entities, no beds) is still a world
static void test_peek_reads_a_version_1_world (void) {
  resetWorld();
  world_seed = 5;
  world_time = 6;
  addPlayer(0, "Old", 9, 8, 7);
  size_t size = worldSnapshotSize();
  uint8_t *buffer = malloc(size);
  CHECK(worldSnapshotWrite(buffer, size) == size);
  size_t v1 = sizeof(WorldHeader) + MAX_PLAYERS * sizeof(PlayerData);   // no block changes
  WorldHeader header;
  memcpy(&header, buffer, sizeof(header));
  header.version = 1;
  memcpy(buffer, &header, sizeof(header));
  BareironWorldInfo info;
  CHECK(bareiron_peek_world(buffer, v1, &info) == 0);
  CHECK(info.seed == 5 && info.player_count == 1 && info.players[0].x == 9);
  CHECK(bareiron_peek_world(buffer, v1 + 1, &info) == -1);
  free(buffer);
}

// What the server will really use: world_seed is splitmix64 of the config's
static void test_seed_for_matches_the_server (void) {
  uint32_t seeds[] = { 0, 1, 0xA103DE6C, 0xFFFFFFFFu };
  for (int i = 0; i < 4; i ++) {
    CHECK(bareiron_seed_for(seeds[i]) == (uint32_t)splitmix64(seeds[i]));
  }
  CHECK(bareiron_seed_for(0) == 0x7B1DCDAFu);            // splitmix64(0) = 0xE220A8397B1DCDAF, low half
  CHECK(bareiron_seed_for(0xA103DE6C) == 0x6932B5DCu);   // computed outside C
  CHECK(bareiron_seed_for(1) != bareiron_seed_for(2));
}

int main (void) {
  RUN(test_peek_reads_seed_time_and_players);
  RUN(test_peek_skips_players_who_never_played);
  RUN(test_peek_reads_a_version_1_world);
  RUN(test_peek_refuses_garbage_and_short);
  RUN(test_seed_for_matches_the_server);
  return CHECK_DONE();
}

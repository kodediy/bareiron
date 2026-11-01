#include <string.h>

#include "globals.h"
#include "tools.h"
#include "varnum.h"
#include "registries.h"
#include "packets.h"
#include "effects.h"

typedef struct {
  uint16_t ticks[EFF_COUNT];
  uint8_t  amp[EFF_COUNT];
  float    exhaustion;
} PlayerEffects;

static PlayerEffects *state = NULL;   // MAX_PLAYERS, see tables_alloc
static const uint16_t registry_id[EFF_COUNT] = { MEF_hunger, MEF_slowness };

int eff_alloc (void *(*alloc) (size_t size)) {
  if (!state) {
    state = alloc(MAX_PLAYERS * sizeof(PlayerEffects));
    if (state) memset(state, 0, MAX_PLAYERS * sizeof(PlayerEffects));
  }
  return state ? 0 : -1;
}

static PlayerEffects *stateOf (const PlayerData *p) {
  return &state[p - player_data];
}

// S->C Update Mob Effect: shows the icon and particles
static void sendEffect (const PlayerData *p, uint8_t effect, uint8_t amp, uint16_t ticks) {
  int fd = p->client_fd, id = p->client_fd;
  uint16_t reg = registry_id[effect];
  writeVarInt(fd, 1 + sizeVarInt(id) + sizeVarInt(reg) + sizeVarInt(amp) + sizeVarInt(ticks) + 1);
  writeByte(fd, P_update_mob_effect);
  writeVarInt(fd, id);
  writeVarInt(fd, reg);
  writeVarInt(fd, amp);
  writeVarInt(fd, ticks);
  writeByte(fd, 0x06);   // particles and icon, not ambient
}

// S->C Remove Mob Effect
static void sendRemove (const PlayerData *p, uint8_t effect) {
  int fd = p->client_fd, id = p->client_fd;
  uint16_t reg = registry_id[effect];
  writeVarInt(fd, 1 + sizeVarInt(id) + sizeVarInt(reg));
  writeByte(fd, P_remove_mob_effect);
  writeVarInt(fd, id);
  writeVarInt(fd, reg);
}

void eff_clear (PlayerData *p) {
  memset(stateOf(p), 0, sizeof(PlayerEffects));
}

uint16_t eff_ticks_left (const PlayerData *p, uint8_t effect) {
  return state[p - player_data].ticks[effect];
}

uint8_t eff_amplifier (const PlayerData *p, uint8_t effect) {
  return state[p - player_data].amp[effect];
}

float eff_speed_factor (const PlayerData *p) {
  const PlayerEffects *s = &state[p - player_data];
  if (!s->ticks[EFF_SLOWNESS]) return 1.0f;
  float f = 1.0f - 0.15f * (s->amp[EFF_SLOWNESS] + 1);
  return f < 0 ? 0 : f;
}

void eff_apply (PlayerData *p, uint8_t effect, uint8_t amplifier, uint16_t ticks) {
  if (effect >= EFF_COUNT || p->client_fd == -1) return;
  PlayerEffects *s = stateOf(p);
  uint8_t active = s->ticks[effect] > 0;
  if (active && (amplifier < s->amp[effect] || (amplifier == s->amp[effect] && ticks <= s->ticks[effect]))) return;
  s->amp[effect] = amplifier;
  s->ticks[effect] = ticks;
  sendEffect(p, effect, amplifier, ticks);
}

void eff_exhaust (PlayerData *p, float amount) {
  PlayerEffects *s = stateOf(p);
  s->exhaustion += amount;
  while (s->exhaustion >= EFF_EXHAUSTION_PER_POINT) {
    s->exhaustion -= EFF_EXHAUSTION_PER_POINT;
    if (p->saturation >= EFF_SATURATION_UNIT) p->saturation -= EFF_SATURATION_UNIT;
    else if (p->saturation > 0) p->saturation = 0;
    else if (p->hunger > 0) p->hunger --;
    sc_setHealth(p->client_fd, p->health, p->hunger, p->saturation);
  }
}

void eff_tick (PlayerData *p) {
  PlayerEffects *s = stateOf(p);
  for (uint8_t e = 0; e < EFF_COUNT; e ++) {
    if (!s->ticks[e]) continue;
    if (e == EFF_HUNGER) eff_exhaust(p, 0.005f * (s->amp[e] + 1));
    if (-- s->ticks[e] == 0) sendRemove(p, e);
  }
}

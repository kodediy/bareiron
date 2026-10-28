#include <math.h>
#include <string.h>

#include "globals.h"
#include "tools.h"
#include "registries.h"
#include "packets.h"
#include "procedures.h"
#include "entity.h"
#include "entitymeta.h"
#include "physics.h"
#include "drops.h"

// An item's box, as vanilla: small, lighter gravity, no jumping ledges
static const PhysBody item_body = { 0.25f, 0.25f, 0.0f, 0.04f, 0.98f, 0 };

#define MERGE_EVERY 10
#define RESTING_EVERY 10

static void sendItemMeta (const Entity *e, int client_fd) {
  EntityData meta = { META_ITEM, META_T_SLOT, { 0 } };
  meta.value.slot.item = e->u.item.item;
  meta.value.slot.count = e->u.item.count;
  sc_setEntityMetadata(client_fd, e->id, &meta, 1);
}

static void broadcastItemMeta (const Entity *e) {
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (!(e->seen_by & ((uint64_t)1 << i)) || player_data[i].client_fd == -1) continue;
    sendItemMeta(e, player_data[i].client_fd);
  }
}

void drops_send_meta (const Entity *e, int client_fd) {
  sendItemMeta(e, client_fd);
}

Entity *drop_item (uint16_t item, uint8_t count, int32_t x, int32_t y, int32_t z,
                   float vx, float vy, float vz, uint16_t pickup_delay) {
  if (!item || !count) return NULL;
  Entity *e = ent_spawn(E_item, x, y, z);
  if (!e) return NULL;
  e->u.item.item = item;
  e->u.item.count = count;
  e->vx = vx; e->vy = vy; e->vz = vz;
  e->timer = pickup_delay;
  e->flags |= ENT_PERSISTENT;
  ent_track_now(e);
  return e;
}

static float randomSpread (float range) {
  return ((float)(fast_rand() % 1000) / 1000.0f - 0.5f) * 2 * range;
}

Entity *drop_pop (uint16_t item, uint8_t count, double x, double y, double z) {
  return drop_item(item, count,
    (int32_t)(x * ENT_UNIT), (int32_t)(y * ENT_UNIT), (int32_t)(z * ENT_UNIT),
    randomSpread(0.1f), 0.2f, randomSpread(0.1f), DROP_PICKUP_DELAY);
}

static uint8_t activePlayer (const PlayerData *p) {
  return p->client_fd != -1 && !(p->flags & 0x20) && p->health > 0;
}

// Whether the item is inside the player's pickup box: its body widened by a
// block sideways and half a block up and down
static uint8_t inReach (const PlayerData *p, const Entity *e) {
  double x = (double)e->x / ENT_UNIT, y = (double)e->y / ENT_UNIT, z = (double)e->z / ENT_UNIT;
  return fabs(x - (p->x + 0.5)) <= 0.3 + 1.0 && fabs(z - (p->z + 0.5)) <= 0.3 + 1.0 &&
         y >= p->y - 0.5 && y <= p->y + 1.8 + 0.5;
}

// As many as fit: givePlayerItem takes all or nothing, so it tries fewer
static uint8_t giveWhatFits (PlayerData *p, uint16_t item, uint8_t count) {
  for (uint8_t n = count; n > 0; n --) {
    if (givePlayerItem(p, item, n) == 0) return n;
  }
  return 0;
}

static void tryPickup (Entity *e) {
  if (e->timer) return;
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    PlayerData *p = &player_data[i];
    if (!activePlayer(p) || !inReach(p, e)) continue;
    uint8_t taken = giveWhatFits(p, e->u.item.item, e->u.item.count);
    if (!taken) continue;
    for (int j = 0; j < MAX_PLAYERS; j ++) {
      if (!(e->seen_by & ((uint64_t)1 << j)) || player_data[j].client_fd == -1) continue;
      sc_pickupItem(player_data[j].client_fd, e->id, p->client_fd, taken);
    }
    if (taken == e->u.item.count) { ent_remove(e); return; }
    e->u.item.count -= taken;
    broadcastItemMeta(e);
  }
}

static void tryMerge (Entity *e) {
  uint8_t stack = getItemStackSize(e->u.item.item);
  if (e->u.item.count >= stack) return;
  for (uint32_t j = 0; j < ent_capacity(); j ++) {
    Entity *o = ent_at(j);
    if (o == e || o->type != E_item || o->u.item.item != e->u.item.item) continue;
    double dx = (double)(o->x - e->x) / ENT_UNIT, dy = (double)(o->y - e->y) / ENT_UNIT, dz = (double)(o->z - e->z) / ENT_UNIT;
    if (fabs(dx) > DROP_MERGE_RANGE || fabs(dz) > DROP_MERGE_RANGE || fabs(dy) > DROP_MERGE_RANGE) continue;
    if (e->u.item.count + o->u.item.count > stack) continue;
    e->u.item.count += o->u.item.count;
    if (o->age < e->age) e->age = o->age;   // the younger one's time to live
    if (o->timer > e->timer) e->timer = o->timer;
    e->flags |= o->flags & ENT_KEEP;
    ent_remove(o);
    broadcastItemMeta(e);
    if (e->u.item.count >= stack) return;
  }
}

void drops_tick (void) {
  for (uint32_t i = 0; i < ent_capacity(); i ++) {
    Entity *e = ent_at(i);
    if (e->type != E_item || !ent_simulated(e)) continue;
    if (++ e->age > DROP_LIFETIME) { ent_remove(e); continue; }
    if (e->timer) e->timer --;
    // At rest on the ground it only needs looking at now and then (does its
    // floor still hold it?): every RESTING_EVERY ticks, staggered
    uint8_t resting = (e->flags & ENT_ON_GROUND) && fabsf(e->vx) < 1e-3f && fabsf(e->vz) < 1e-3f;
    if (!resting || (server_ticks + i) % RESTING_EVERY == 0) phys_step(e, &item_body);
    if ((server_ticks + i) % MERGE_EVERY == 0) tryMerge(e);
    if (e->type == E_item) tryPickup(e);
  }
}

// Vanilla 1.21 loot, without Looting
void drop_loot (const Entity *mob, int32_t killer_id) {
  double x = (double)mob->x / ENT_UNIT, y = (double)mob->y / ENT_UNIT + 0.5, z = (double)mob->z / ENT_UNIT;
  uint8_t by_player = killer_id > 0;
  uint32_t r = fast_rand();
  #define POP(item, n) do { uint8_t _n = (n); if (_n) drop_pop((item), _n, x, y, z); } while (0)
  switch (mob->type) {
    case E_zombie: case E_husk: case E_zombie_villager:
      POP(I_rotten_flesh, r % 3);
      if (by_player && fast_rand() % 1000 < 25) {
        uint16_t rare[3] = { I_iron_ingot, I_carrot, I_potato };
        POP(rare[fast_rand() % 3], 1);
      }
      break;
    case E_skeleton: case E_stray:
      POP(I_bone, r % 3);
      POP(I_arrow, (r >> 4) % 3);
      if (mob->type == E_stray && by_player && (fast_rand() & 1)) POP(I_tipped_arrow, 1);
      break;
    case E_spider:
      POP(I_string, r % 3);
      if (by_player && fast_rand() % 3 == 0) POP(I_spider_eye, 1);
      break;
    case E_creeper:
      POP(I_gunpowder, r % 3);
      break;
    case E_cow:
      POP(I_leather, r % 3);
      POP(I_beef, 1 + (r >> 4) % 3);
      break;
    case E_pig:
      POP(I_porkchop, 1 + r % 3);
      break;
    case E_sheep:
      if (!(mob->flags & ENT_SHEARED)) {
        static const uint16_t wool[16] = {
          I_white_wool, I_orange_wool, I_magenta_wool, I_light_blue_wool, I_yellow_wool, I_lime_wool,
          I_pink_wool, I_gray_wool, I_light_gray_wool, I_cyan_wool, I_purple_wool, I_blue_wool,
          I_brown_wool, I_green_wool, I_red_wool, I_black_wool
        };
        POP(wool[mob->u.mob.wool & 15], 1);
      }
      POP(I_mutton, 1 + (r & 1));
      break;
    case E_chicken:
      POP(I_feather, r % 3);
      POP(I_chicken, 1);
      break;
    default: break;
  }
  #undef POP
}

// Kept from the table's pushing out; with no room at all, the stack stays
// with the player rather than being lost
static uint8_t spill (uint16_t item, uint8_t count, double x, double y, double z) {
  Entity *e = drop_pop(item, count, x, y, z);
  if (!e) return 0;
  e->flags |= ENT_KEEP;
  return 1;
}

void drops_spill_inventory (PlayerData *p) {
  double x = p->x + 0.5, y = p->y + 1.0, z = p->z + 0.5;
  for (int i = 0; i < 41; i ++) {
    if (!p->inventory_count[i]) continue;
    if (!spill(p->inventory_items[i], p->inventory_count[i], x, y, z)) continue;
    p->inventory_items[i] = 0;
    p->inventory_count[i] = 0;
    sc_setContainerSlot(p->client_fd, 0, serverSlotToClientSlot(0, i), 0, 0);
  }
  for (int i = 0; i < 9; i ++) {
    if (!p->craft_count[i]) continue;
    if (!spill(p->craft_items[i], p->craft_count[i], x, y, z)) continue;
    p->craft_items[i] = 0;
    p->craft_count[i] = 0;
  }
}

void drops_throw (PlayerData *p, uint8_t whole_stack) {
  uint8_t slot = p->hotbar;
  if (!p->inventory_count[slot]) return;
  uint8_t n = whole_stack ? p->inventory_count[slot] : 1;
  uint16_t item = p->inventory_items[slot];
  // From the eyes, along the look, as vanilla throws it
  double yaw = p->yaw * M_PI / 127.0, pitch = p->pitch * (M_PI / 2) / 127.0;
  float dx = (float)(-sin(yaw) * cos(pitch)), dy = (float)(-sin(pitch)), dz = (float)(cos(yaw) * cos(pitch));
  Entity *e = drop_item(item, n,
    (int32_t)((p->x + 0.5) * ENT_UNIT), (int32_t)((p->y + 1.32) * ENT_UNIT), (int32_t)((p->z + 0.5) * ENT_UNIT),
    dx * 0.3f, dy * 0.3f + 0.1f, dz * 0.3f, DROP_THROWN_DELAY);
  if (!e) {   // no room anywhere: it stays in the hand
    sc_setContainerSlot(p->client_fd, 0, serverSlotToClientSlot(0, slot), p->inventory_count[slot], item);
    return;
  }
  p->inventory_count[slot] -= n;
  if (!p->inventory_count[slot]) p->inventory_items[slot] = 0;
  sc_setContainerSlot(p->client_fd, 0, serverSlotToClientSlot(0, slot), p->inventory_count[slot], p->inventory_items[slot]);
}

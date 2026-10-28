// Entity, packet, sound and effect ids come from the 1.21.8 reports
#include "check.h"
#include "registries.h"

static void test_registry_ids_match_reports (void) {
  // entity_type (registries.json)
  CHECK(E_zombie == 145);
  CHECK(E_creeper == 30);
  CHECK(E_arrow == 6);
  CHECK(E_item == 69);
  CHECK(E_tnt == 127);
  CHECK(E_skeleton == 110);
  CHECK(E_cow == 28);
  CHECK(E_husk == 65);
  CHECK(E_stray == 123);
  CHECK(E_spider == 119);
  CHECK(E_sheep == 106);
  CHECK(E_pig == 95);
  CHECK(E_chicken == 25);
  CHECK(E_zombie_villager == 147);
  // play clientbound (packets.json)
  CHECK(P_move_entity_pos == 46);
  CHECK(P_move_entity_pos_rot == 47);
  CHECK(P_set_entity_motion == 94);
  CHECK(P_explode == 32);
  CHECK(P_update_mob_effect == 125);
  CHECK(P_remove_mob_effect == 71);
  CHECK(P_set_equipment == 95);
  CHECK(P_take_item_entity == 117);
  CHECK(P_sound_entity == 109);
  // sound_event, dots as underscores
  CHECK(S_entity_zombie_ambient == 1701);
  CHECK(S_entity_creeper_primed == 397);
  // mob_effect
  CHECK(MEF_hunger == 16);
  CHECK(MEF_slowness == 1);
}

static void test_entity_ids_are_distinct (void) {
  int ids[] = { E_zombie, E_creeper, E_arrow, E_item, E_tnt, E_skeleton, E_cow, E_husk,
                E_stray, E_spider, E_sheep, E_pig, E_chicken, E_zombie_villager };
  int n = sizeof(ids) / sizeof(ids[0]);
  for (int i = 0; i < n; i ++) for (int j = i + 1; j < n; j ++) CHECK(ids[i] != ids[j]);
}

int main (void) {
  RUN(test_registry_ids_match_reports);
  RUN(test_entity_ids_are_distinct);
  return CHECK_DONE();
}

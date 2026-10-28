import os
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from mcbot import Bot  # noqa: E402
from server import ServerProcess  # noqa: E402

PORT = int(os.environ.get("BAREIRON_TEST_PORT", "25565"))  # another port if 25565 is taken
# A night, with one skeleton near the bot: vanilla skeletons only notice
# players within 16 blocks, and the ones that spawn may never come that close
NIGHT = {"BAREIRON_TEST_TIME": "14000", "BAREIRON_TEST_MOBS": "110:1"}
ZOMBIE, SKELETON, SPIDER, CREEPER = 145, 110, 119, 30
HUSK, STRAY, ZOMBIE_VILLAGER = 65, 123, 147
KIND = {ZOMBIE: "zombie", HUSK: "zombie", ZOMBIE_VILLAGER: "zombie", SKELETON: "skeleton", STRAY: "skeleton",
        SPIDER: "spider", CREEPER: "creeper"}
D_ARROW, D_MOB_ATTACK = 0, 28


class WaveOneNightTest(unittest.TestCase):

  def test_wave1_night(self):
    """A night by the spawn: at least three kinds of monster, hit both up close and by arrows."""
    with ServerProcess(PORT, env=NIGHT):
      bot = Bot(port=PORT, name="camper")
      bot.login()
      kinds = set()
      arrows_seen = set()
      deadline = time.monotonic() + 120
      while time.monotonic() < deadline:
        bot.pump(1.0)
        kinds |= {KIND[e["type"]] for e in bot.entities.values() if e["type"] in KIND}
        arrows_seen |= {eid for eid, e in bot.entities.items() if e["type"] == 6}
        if len(kinds) >= 3 and D_ARROW in bot.damage_types and D_MOB_ATTACK in bot.damage_types and arrows_seen:
          break
      print("kinds %s, hits %s, deaths %d" % (sorted(kinds), sorted(set(bot.damage_types)), bot.deaths))
      self.assertGreaterEqual(len(kinds), 3)
      self.assertIn(D_MOB_ATTACK, bot.damage_types)
      self.assertIn(D_ARROW, bot.damage_types)
      self.assertTrue(arrows_seen, "the arrows should be seen in flight")
      bot.close()


if __name__ == "__main__":
  unittest.main()

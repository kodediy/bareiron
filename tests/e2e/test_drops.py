import math
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from mcbot import Bot  # noqa: E402
from server import ServerProcess  # noqa: E402

PORT = int(os.environ.get("BAREIRON_TEST_PORT", "25565"))  # another port if 25565 is taken
ITEM = 69   # minecraft:item


def items(bot):
  return {eid: e for eid, e in bot.entities.items() if e["type"] == ITEM}


class DropsTest(unittest.TestCase):

  def test_mined_block_drops_and_is_picked_up(self):
    with ServerProcess(PORT):
      bot = Bot(port=PORT, name="miner")
      bot.login()
      bot.pump(2.0)
      x, y, z = bot.position
      bot.break_block(math.floor(x), math.floor(y) - 1, math.floor(z))
      bot.pump(0.3)
      self.assertTrue(items(bot), "the block should pop out as an item")
      bot.move_to(x, y - 1, z)            # step into the hole, onto the item
      bot.pump(1.5)
      self.assertTrue(any(c == bot.entity_id for _, c, _ in bot.pickups))
      bot.close()

  def test_death_spills_inventory(self):
    with ServerProcess(PORT):
      bot = Bot(port=PORT, name="faller")
      bot.login()
      bot.pump(2.0)
      x, y, z = bot.position
      bot.break_block(math.floor(x), math.floor(y) - 1, math.floor(z))
      bot.move_to(x, y - 1, z)
      bot.pump(1.5)                       # picks the block up
      self.assertTrue(bot.pickups)
      before = set(items(bot))
      bot.move_to(x, y + 40, z)           # up, then a fall that kills
      bot.pump(0.2)
      bot.move_to(x, y - 1, z)
      bot.pump(0.1)
      bot.move_to(x, y - 1, z)            # bareiron counts the fall on the next report from the ground
      bot.pump(1.0)
      self.assertTrue(set(items(bot)) - before, "what it carried should drop where it died")
      bot.close()


if __name__ == "__main__":
  unittest.main()

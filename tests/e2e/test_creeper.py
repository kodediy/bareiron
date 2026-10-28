import os
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from mcbot import Bot  # noqa: E402
from server import ServerProcess  # noqa: E402

PORT = int(os.environ.get("BAREIRON_TEST_PORT", "25565"))  # another port if 25565 is taken
CREEPER = 30
ONE_CREEPER = {"BAREIRON_TEST_MOBS": "%d:1" % CREEPER}   # by day: nothing else spawns


class CreeperTest(unittest.TestCase):

  def test_creeper_explodes_near_bot(self):
    """A creeper walks up, lights (metadata), explodes, hurts and breaks blocks."""
    with ServerProcess(PORT, env=ONE_CREEPER):
      bot = Bot(port=PORT, name="victim")
      bot.login()
      deadline = time.monotonic() + 40
      while time.monotonic() < deadline and not bot.explosions:
        bot.pump(0.5)
      self.assertTrue(bot.explosions, "no explosion within 40 s")
      self.assertTrue(bot.metadata, "the fuse should have been shown")
      bot.pump(0.5)
      self.assertIsNotNone(bot.health)
      self.assertLess(bot.health, 20)
      bot.close()


if __name__ == "__main__":
  unittest.main()

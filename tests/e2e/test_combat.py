import os
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from mcbot import Bot  # noqa: E402
from server import ServerProcess  # noqa: E402

PORT = int(os.environ.get("BAREIRON_TEST_PORT", "25565"))  # another port if 25565 is taken
NIGHT = {"BAREIRON_TEST_TIME": "14000"}


class CombatTest(unittest.TestCase):

  def test_zombie_hit_knocks_back_player(self):
    """A zombie's hit pushes the player away, as in vanilla."""
    with ServerProcess(PORT, env=NIGHT):
      bot = Bot(port=PORT, name="target")
      bot.login()
      deadline = time.monotonic() + 90
      while time.monotonic() < deadline and not bot.motions:
        bot.pump(1.0)
      self.assertIsNotNone(bot.entity_id)
      self.assertTrue(bot.motions, "no hit within 90 s of night")
      vx, vy, vz = bot.motions[0]
      self.assertGreater((vx * vx + vz * vz) ** 0.5, 0.3)
      bot.close()


if __name__ == "__main__":
  unittest.main()

import os
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from mcbot import Bot  # noqa: E402
from server import ServerProcess  # noqa: E402

PORT = int(os.environ.get("BAREIRON_TEST_PORT", "25565"))  # another port if 25565 is taken
NIGHT = {"BAREIRON_TEST_TIME": "14000"}
ZOMBIE, HUSK, SKELETON, STRAY, SPIDER, CREEPER, ZOMBIE_VILLAGER = 145, 65, 110, 123, 119, 30, 147
HOSTILE = {ZOMBIE, HUSK, SKELETON, STRAY, SPIDER, CREEPER, ZOMBIE_VILLAGER}


class SpawningTest(unittest.TestCase):

  def test_night_brings_monsters(self):
    """A player standing on the surface at night has monsters around it within a minute."""
    with ServerProcess(PORT, env=NIGHT):
      bot = Bot(port=PORT, name="night")
      bot.login()
      most = 0
      deadline = time.monotonic() + 60
      while time.monotonic() < deadline and most < 4:
        bot.pump(1.0)
        x, _, z = bot.position
        near = [e for eid, e in bot.entities.items()
                if eid < -1 and e["type"] in HOSTILE and (e["x"] - x) ** 2 + (e["z"] - z) ** 2 <= 32 * 32]
        most = max(most, len(near))
      self.assertGreaterEqual(most, 4)
      bot.close()


if __name__ == "__main__":
  unittest.main()

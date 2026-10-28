import math
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from mcbot import Bot  # noqa: E402
from server import ServerProcess  # noqa: E402

PORT = int(os.environ.get("BAREIRON_TEST_PORT", "25565"))  # another port if 25565 is taken
WHITE_BED = 1041
NIGHT_WITH_BED = {"BAREIRON_TEST_TIME": "14000", "BAREIRON_TEST_KIT": "%d:1" % WHITE_BED}


class SleepTest(unittest.TestCase):

  def test_sleeping_alone_skips_the_night(self):
    """The only player places a bed, sleeps in it, and it is morning."""
    with ServerProcess(PORT, env=NIGHT_WITH_BED):
      bot = Bot(port=PORT, name="sleeper")
      bot.login()
      bot.pump(2.0)
      x, y, z = bot.position
      bx, by, bz = math.floor(x) + 2, math.floor(y) - 1, math.floor(z)
      bot.use_item_on(bx, by, bz, face=1)        # the bed on the ground beside it
      bot.pump(0.5)
      bot.use_item_on(bx, by + 1, bz, face=1)    # into it
      bot.pump(6.0)                              # 100 ticks asleep, then morning
      night = [t for _, t in bot.time_of_day if 13000 <= t % 24000 <= 23000]
      self.assertTrue(night, "it was night when it lay down")
      self.assertLess(bot.time_of_day[-1][1] % 24000, 1000, "and morning after")
      bot.close()


if __name__ == "__main__":
  unittest.main()

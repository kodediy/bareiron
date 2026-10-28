import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from mcbot import Bot  # noqa: E402
from server import ServerProcess  # noqa: E402

PORT = int(os.environ.get("BAREIRON_TEST_PORT", "25565"))  # another port if 25565 is taken
BOW, ARROW = 858, 859
ARROW_ENTITY = 6
KIT = {"BAREIRON_TEST_KIT": "%d:1,%d:16" % (BOW, ARROW)}


class BowTest(unittest.TestCase):

  def test_bot_shoots_bow(self):
    """Drawing for a second and letting go shoots an arrow and uses one up."""
    with ServerProcess(PORT, env=KIT):
      bot = Bot(port=PORT, name="archer")
      bot.login()
      bot.pump(2.0)
      bot.use_item()
      bot.pump(1.0)
      bot.release_use_item()
      bot.pump(0.5)
      arrows = [e for e in bot.entities.values() if e["type"] == ARROW_ENTITY]
      self.assertTrue(arrows, "an arrow should be in the air")
      self.assertIn((ARROW, 15), bot.slots.values(), "one arrow used up")
      bot.close()


if __name__ == "__main__":
  unittest.main()

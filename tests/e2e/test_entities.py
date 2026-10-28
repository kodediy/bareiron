import os
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from mcbot import Bot  # noqa: E402
from server import ServerProcess  # noqa: E402

PORT = int(os.environ.get("BAREIRON_TEST_PORT", "25565"))  # another port if 25565 is taken
VIEW_DISTANCE = 2
COWS = {"BAREIRON_TEST_MOBS": "6"}   # six cows around the spawn when the server starts


def chunk(v):
  return int(v // 16)


def mobs(bot):
  return {eid: e for eid, e in bot.entities.items() if eid < -1}


def walk_to(bot, x, z, step=4.0):
  """Walks in a straight line, a few blocks every 50 ms, answering the server."""
  sx, y, sz = bot.position
  dist = ((x - sx) ** 2 + (z - sz) ** 2) ** 0.5
  steps = max(1, int(dist / step))
  for i in range(1, steps + 1):
    bot.move_to(sx + (x - sx) * i / steps, y, sz + (z - sz) * i / steps)
    bot.pump(0.05)


class EntityTrackingTest(unittest.TestCase):

  def test_join_sees_existing_mobs(self):
    """A player who joins is sent the mobs already around it."""
    with ServerProcess(PORT, env=COWS):
      first = Bot(port=PORT, name="first")
      first.login()
      first.pump(3.0)
      self.assertGreaterEqual(len(mobs(first)), 6, "the six cows around the spawn")

      second = Bot(port=PORT, name="second")
      second.login()
      second.pump(3.0)
      first.pump(0.5)
      sx, _, sz = second.position
      for eid, e in mobs(first).items():
        # well inside the second bot's view, so a mob wandering at the edge does not count
        if abs(chunk(e["x"]) - chunk(sx)) <= VIEW_DISTANCE - 1 and abs(chunk(e["z"]) - chunk(sz)) <= VIEW_DISTANCE - 1:
          self.assertIn(eid, second.entities, "mob %d at %.0f,%.0f" % (eid, e["x"], e["z"]))
      self.assertEqual(second.duplicate_spawns, 0)
      first.close()
      second.close()

  def test_leaving_range_removes_entities(self):
    """Walking away removes what is out of view; coming back spawns it once."""
    with ServerProcess(PORT, env=COWS):
      bot = Bot(port=PORT, name="walker")
      bot.login()
      bot.pump(3.0)
      near = dict(mobs(bot))
      self.assertGreaterEqual(len(near), 6)
      x0, _, z0 = bot.position
      far = max(abs(e["x"] - x0) for e in near.values()) + (VIEW_DISTANCE + 2) * 16
      walk_to(bot, x0 + far, z0)
      bot.pump(1.0)
      for eid in near:
        self.assertNotIn(eid, bot.entities)
      walk_to(bot, x0, z0)
      bot.pump(1.0)
      self.assertEqual(bot.duplicate_spawns, 0)
      bot.close()


if __name__ == "__main__":
  unittest.main()

import os
import re
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from mcbot import Bot  # noqa: E402
from server import ServerProcess  # noqa: E402

PORT = int(os.environ.get("BAREIRON_TEST_PORT", "25565"))  # another port if 25565 is taken


class PersistenceTest(unittest.TestCase):

  def test_stop_and_restart_keeps_world(self):
    world = tempfile.mkdtemp(prefix="bareiron-persist-")

    first = ServerProcess(PORT, world).start()
    bot = Bot(port=PORT, name="miner")
    bot.login()
    bot.play_for(3)
    x, y, z = (int(v // 1) for v in bot.position)
    bot.break_block(x, y - 1, z)
    # The status snapshot refreshes once a second
    deadline = time.monotonic() + 3
    while first.status()["block_changes_used"] < 1 and time.monotonic() < deadline:
      bot.play_for(0.2)
    self.assertGreaterEqual(first.status()["block_changes_used"], 1)
    code = first.stop()
    bot.close()
    self.assertEqual(code, 0, first.log[-1500:])
    self.assertTrue(any(e["type"] == "world_saved" for e in first.events()), first.log[-1500:])
    seed = re.search(r"seed ([0-9A-F]{8})", first.log).group(1)

    second = ServerProcess(PORT, world).start()
    try:
      time.sleep(1.2)
      loaded = re.search(r"World loaded: (\d+) block changes, seed ([0-9A-F]{8})", second.log)
      self.assertIsNotNone(loaded, second.log[-1500:])
      self.assertGreaterEqual(int(loaded.group(1)), 1)
      self.assertEqual(loaded.group(2), seed)
      self.assertGreaterEqual(second.status()["block_changes_used"], 1)
    finally:
      second.stop()


  def test_items_on_ground_survive_restart(self):
    """A block mined out of reach lies on the ground; after a restart it is still there."""
    world = tempfile.mkdtemp(prefix="bareiron-persist-")
    first = ServerProcess(PORT, world).start()
    try:
      miner = Bot(port=PORT, name="miner")
      miner.login()
      miner.pump(2.0)
      x, y, z = miner.position
      miner.break_block(int(x // 1) + 3, int(y // 1) - 1, int(z // 1))   # 3 blocks off: not picked up
      miner.pump(1.0)
      before = sorted((round(e["x"], 2), round(e["y"], 2), round(e["z"], 2))
                      for e in miner.entities.values() if e["type"] == 69)
      self.assertTrue(before, "a mined block lies on the ground")
      miner.close()
    finally:
      first.stop()
    second = ServerProcess(PORT, world).start()
    try:
      looker = Bot(port=PORT, name="looker")
      looker.login()
      looker.pump(2.0)
      after = sorted((round(e["x"], 2), round(e["y"], 2), round(e["z"], 2))
                     for e in looker.entities.values() if e["type"] == 69)
      self.assertEqual(before, after)
      looker.close()
    finally:
      second.stop()

  def test_animals_survive_restart(self):
    """Animals are saved with the world, in the same spots."""
    world = tempfile.mkdtemp(prefix="bareiron-persist-")
    first = ServerProcess(PORT, world, env={"BAREIRON_TEST_MOBS": "4"}).start()
    bot = Bot(port=PORT, name="farmer")
    bot.login()
    bot.pump(2.0)
    cows = len([e for e in bot.entities.values() if e["type"] == 28])
    bot.close()
    time.sleep(0.5)                            # nobody on: the cows stand still
    first.stop()
    self.assertGreaterEqual(cows, 4)
    second = ServerProcess(PORT, world).start()   # no test cows this time
    try:
      bot = Bot(port=PORT, name="farmer")
      bot.login()
      bot.pump(2.0)
      self.assertEqual(len([e for e in bot.entities.values() if e["type"] == 28]), cows)
      bot.close()
    finally:
      second.stop()


if __name__ == "__main__":
  unittest.main()

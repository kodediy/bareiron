import os
import sys
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from mcbot import Bot, PLAY_ENTITY_POSITION_SYNC  # noqa: E402
from server import ServerProcess  # noqa: E402

PORT = int(os.environ.get("BAREIRON_TEST_PORT", "25565"))  # another port if 25565 is taken


class TickRateTest(unittest.TestCase):
  """The game runs at 20 ticks a second, like vanilla; once-a-second things stay once a second."""

  def test_keepalive_once_per_second(self):
    with ServerProcess(PORT):
      bot = Bot(port=PORT, name="ka")
      bot.login()
      bot.pump(1.5)
      start = len(bot.keep_alives)
      bot.pump(5.0)
      self.assertTrue(4 <= len(bot.keep_alives) - start <= 6, len(bot.keep_alives) - start)
      bot.close()

  def test_time_advances_20_ticks_per_second(self):
    with ServerProcess(PORT):
      bot = Bot(port=PORT, name="clock")
      bot.login()
      bot.pump(1.5)
      first = bot.time_of_day[-1]
      bot.pump(3.0)
      last = bot.time_of_day[-1]
      per_second = (last[1] - first[1]) / (last[0] - first[0])
      self.assertTrue(18 <= per_second <= 22, per_second)
      bot.close()

  def test_other_player_moves_smoothly(self):
    """Like vanilla, a player's moves reach the others 10 times a second
    however many are connected (bareiron used to send 1 in N with N players)."""
    with ServerProcess(PORT):
      walker, watcher = Bot(port=PORT, name="walker"), Bot(port=PORT, name="watcher")
      idle = [Bot(port=PORT, name="idle%d" % i) for i in range(2)]
      for bot in [walker, watcher] + idle:
        bot.login()
      for bot in [walker, watcher] + idle:
        bot.pump(0.3)
      thread = threading.Thread(target=walker.walk_circle, args=(4.0,), kwargs={"speed": 4.3})
      thread.start()
      start = time.monotonic()
      watcher.pump(4.0)
      thread.join()
      moves = [t for t in watcher.packet_times.get(PLAY_ENTITY_POSITION_SYNC, []) if t > start + 0.5]
      per_second = len(moves) / 3.5
      self.assertTrue(8 <= per_second <= 12, per_second)
      for bot in [walker, watcher] + idle:
        bot.close()


if __name__ == "__main__":
  unittest.main()

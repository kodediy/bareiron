"""Minimal Minecraft Java 1.21.8 (protocol 772) client for end-to-end tests.

Speaks just enough of the protocol to log in, finish configuration, stay in
the Play state answering keep-alives, and move around. Understands protocol
compression once the server sends Set Compression.
"""

import json
import math
import socket
import struct
import time
import uuid
import zlib

PROTOCOL = 772

# Clientbound
LOGIN_SET_COMPRESSION = 0x03
LOGIN_SUCCESS = 0x02
CONFIG_FINISH = 0x03
CONFIG_KEEP_ALIVE = 0x04
PLAY_CHUNK = 0x27
PLAY_KEEP_ALIVE = 0x26
PLAY_SYNC_POSITION = 0x41
PLAY_DISCONNECT = 0x1C
PLAY_SET_TIME = 0x6A         # Update Time
PLAY_TELEPORT_ENTITY = 0x76
PLAY_ENTITY_POSITION_SYNC = 0x1F   # what bareiron sends for another player's move
PLAY_ADD_ENTITY = 0x01
PLAY_LOGIN = 0x2B
PLAY_SET_ENTITY_MOTION = 0x5E
PLAY_TAKE_ITEM_ENTITY = 0x75
PLAY_SET_CONTAINER_SLOT = 0x14
PLAY_EXPLODE = 0x20
PLAY_SET_HEALTH = 0x61
PLAY_SET_ENTITY_DATA = 0x5C
PLAY_DAMAGE_EVENT = 0x19
PLAY_REMOVE_ENTITIES = 0x46
PLAY_MOVE_ENTITY_POS = 0x2E
PLAY_MOVE_ENTITY_POS_ROT = 0x2F

# Serverbound
SB_CONFIG_CLIENT_INFO = 0x00
SB_CONFIG_FINISH_ACK = 0x03
SB_CONFIG_KEEP_ALIVE = 0x04
SB_CONFIG_KNOWN_PACKS = 0x07
SB_PLAY_CONFIRM_TELEPORT = 0x00
SB_PLAY_KEEP_ALIVE = 0x1B
SB_PLAY_SET_POSITION = 0x1D
SB_PLAY_PLAYER_ACTION = 0x28
SB_PLAY_PLAYER_LOADED = 0x2B
SB_PLAY_USE_ITEM = 0x40
SB_PLAY_CLIENT_COMMAND = 0x0B
SB_PLAY_USE_ITEM_ON = 0x3F


class Disconnected(Exception):
  pass


def varint(value):
  value &= 0xFFFFFFFF
  out = bytearray()
  while True:
    if value & ~0x7F == 0:
      out.append(value)
      return bytes(out)
    out.append((value & 0x7F) | 0x80)
    value >>= 7


def string(text):
  data = text.encode()
  return varint(len(data)) + data


def read_varint(buf, pos):
  value = shift = 0
  while True:
    if pos >= len(buf):
      raise IndexError("varint past end")
    byte = buf[pos]
    pos += 1
    value |= (byte & 0x7F) << shift
    if not byte & 0x80:
      break
    shift += 7
    if shift >= 35:
      raise ValueError("varint too long")
  if value & 0x80000000:
    value -= 1 << 32
  return value, pos


def block_position(x, y, z):
  return struct.pack(">q", ((x & 0x3FFFFFF) << 38) | ((z & 0x3FFFFFF) << 12) | (y & 0xFFF))


class Bot:
  def __init__(self, host="127.0.0.1", port=25565, name="bot"):
    self.host, self.port, self.name = host, port, name
    self.sock = None
    self.rx = bytearray()
    self.threshold = -1
    self.bytes_received = 0
    self.chunks_received = 0
    self.keep_alives = []        # monotonic timestamps of received keep-alives
    self.position = None         # last (x, y, z) sent by the server
    self.loaded = False
    self.state = "none"
    self.packet_times = {}       # play packet id -> monotonic timestamps
    self.time_of_day = []        # (monotonic, time of day) from Update Time
    self.entities = {}           # entity id -> {"type", "x", "y", "z"} for what is spawned
    self.duplicate_spawns = 0    # Spawn Entity for an id already spawned
    self.spawn_log = []          # (entity id, type) of every Spawn Entity, in order
    self.removed = []            # ids removed, in order
    self.entity_id = None        # this bot's own entity id (Login)
    self.motions = []            # (vx, vy, vz) blocks/tick the server set on this bot
    self.health = None           # last Set Health
    self.pickups = []            # (item entity id, collector id, count) from Take Item Entity
    self.slots = {}              # player inventory slot -> (item, count), from Set Container Slot
    self.explosions = []         # (x, y, z) of each Explosion
    self.metadata = []           # (entity id, raw payload) of each Set Entity Data
    self.damage_types = []       # damage type ids of the hits this bot took
    self.deaths = 0              # times it died (it respawns at once, as a client does)

  # -- transport --------------------------------------------------------

  def _connect(self):
    self.sock = socket.create_connection((self.host, self.port), timeout=10)
    self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

  def send(self, packet_id, payload=b""):
    data = varint(packet_id) + payload
    if self.threshold >= 0:
      if len(data) >= self.threshold:
        body = varint(len(data)) + zlib.compress(data)
      else:
        body = varint(0) + data
    else:
      body = data
    self.sock.sendall(varint(len(body)) + body)

  def _fill(self, timeout):
    self.sock.settimeout(timeout)
    try:
      chunk = self.sock.recv(1 << 16)
    except socket.timeout:
      return False
    if not chunk:
      raise Disconnected(self.name + ": server closed the connection")
    self.bytes_received += len(chunk)
    self.rx += chunk
    return True

  def recv(self, timeout=10.0):
    """Return (packet_id, payload) of the next packet, or None on timeout."""
    deadline = time.monotonic() + timeout
    while True:
      try:
        length, pos = read_varint(self.rx, 0)
        if len(self.rx) >= pos + length:
          frame = bytes(self.rx[pos:pos + length])
          del self.rx[:pos + length]
          return self._unframe(frame)
      except IndexError:
        pass
      left = deadline - time.monotonic()
      if left <= 0 or not self._fill(left):
        return None

  def _unframe(self, frame):
    if self.threshold >= 0:
      data_length, pos = read_varint(frame, 0)
      frame = zlib.decompress(frame[pos:]) if data_length else frame[pos:]
    packet_id, pos = read_varint(frame, 0)
    return packet_id, frame[pos:]

  def close(self, abrupt=False):
    if not self.sock:
      return
    if abrupt:
      self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
    self.sock.close()
    self.sock = None

  # -- protocol flows ----------------------------------------------------

  def _handshake(self, intent):
    self.send(0x00, varint(PROTOCOL) + string(self.host) + struct.pack(">H", self.port) + varint(intent))

  def status(self):
    self._connect()
    self._handshake(1)
    self.send(0x00)
    packet = self.recv()
    if packet is None:
      raise TimeoutError("no status response")
    length, pos = read_varint(packet[1], 0)
    info = json.loads(packet[1][pos:pos + length])
    self.send(0x01, struct.pack(">q", 42))
    self.recv(timeout=5)
    self.close()
    return info

  def login(self, timeout=20.0):
    """Log in and return once the server has placed the player in the world."""
    self._connect()
    self._handshake(2)
    player_uuid = uuid.uuid3(uuid.NAMESPACE_OID, "OfflinePlayer:" + self.name).bytes
    self.send(0x00, string(self.name) + player_uuid)
    self.state = "login"
    deadline = time.monotonic() + timeout
    while self.position is None:
      packet = self.recv(timeout=max(0.1, deadline - time.monotonic()))
      if packet is None:
        raise TimeoutError(self.name + ": login timed out in state " + self.state)
      self._handle(*packet)

  def _handle(self, packet_id, payload):
    if self.state == "login":
      if packet_id == LOGIN_SET_COMPRESSION:
        self.threshold, _ = read_varint(payload, 0)
      elif packet_id == LOGIN_SUCCESS:
        self.send(0x03)  # Login Acknowledged
        self.state = "config"
        self.send(SB_CONFIG_CLIENT_INFO, string("en_us") + bytes([2]) + varint(0) + bytes([1, 0x7F])
                  + varint(1) + bytes([0, 1]) + varint(0))
    elif self.state == "config":
      if packet_id == 0x0E:  # Clientbound Known Packs
        self.send(SB_CONFIG_KNOWN_PACKS, varint(0))
      elif packet_id == CONFIG_KEEP_ALIVE:
        self.send(SB_CONFIG_KEEP_ALIVE, payload)
      elif packet_id == CONFIG_FINISH:
        self.send(SB_CONFIG_FINISH_ACK)
        self.state = "play"
    elif self.state == "play":
      self.packet_times.setdefault(packet_id, []).append(time.monotonic())
      if packet_id == PLAY_EXPLODE:
        self.explosions.append(struct.unpack(">ddd", payload[:24]))
      elif packet_id == PLAY_SET_HEALTH:
        self.health = struct.unpack(">f", payload[:4])[0]
        if self.health <= 0:
          self.deaths += 1
          self.send(SB_PLAY_CLIENT_COMMAND, varint(0))   # Perform Respawn
      elif packet_id == PLAY_DAMAGE_EVENT:
        eid, pos = read_varint(payload, 0)
        if eid == self.entity_id:
          self.damage_types.append(read_varint(payload, pos)[0])
      elif packet_id == PLAY_SET_ENTITY_DATA:
        eid, pos = read_varint(payload, 0)
        self.metadata.append((eid, bytes(payload[pos:])))
      if packet_id == PLAY_SET_CONTAINER_SLOT:
        window, pos = read_varint(payload, 0)
        _, pos = read_varint(payload, pos)          # state id
        slot = struct.unpack(">h", payload[pos:pos + 2])[0]
        count, pos = read_varint(payload, pos + 2)
        item = read_varint(payload, pos)[0] if count else 0
        if window == 0:
          self.slots[slot] = (item, count)
      if packet_id == PLAY_TAKE_ITEM_ENTITY:
        collected, pos = read_varint(payload, 0)
        collector, pos = read_varint(payload, pos)
        count, pos = read_varint(payload, pos)
        self.pickups.append((collected, collector, count))
      if packet_id == PLAY_LOGIN:
        self.entity_id = struct.unpack(">i", payload[:4])[0]
      elif packet_id == PLAY_SET_ENTITY_MOTION:
        eid, pos = read_varint(payload, 0)
        if eid == self.entity_id:
          vx, vy, vz = struct.unpack(">hhh", payload[pos:pos + 6])
          self.motions.append((vx / 8000.0, vy / 8000.0, vz / 8000.0))
      if packet_id == PLAY_ADD_ENTITY:
        eid, pos = read_varint(payload, 0)
        pos += 16                               # uuid
        etype, pos = read_varint(payload, pos)
        x, y, z = struct.unpack(">ddd", payload[pos:pos + 24])
        if eid in self.entities:
          self.duplicate_spawns += 1
        self.spawn_log.append((eid, etype))
        self.entities[eid] = {"type": etype, "x": x, "y": y, "z": z}
      elif packet_id == PLAY_REMOVE_ENTITIES:
        count, pos = read_varint(payload, 0)
        for _ in range(count):
          eid, pos = read_varint(payload, pos)
          self.entities.pop(eid, None)
          self.removed.append(eid)
      elif packet_id in (PLAY_MOVE_ENTITY_POS, PLAY_MOVE_ENTITY_POS_ROT):
        eid, pos = read_varint(payload, 0)
        dx, dy, dz = struct.unpack(">hhh", payload[pos:pos + 6])
        if eid in self.entities:
          e = self.entities[eid]
          e["x"] += dx / 4096.0
          e["y"] += dy / 4096.0
          e["z"] += dz / 4096.0
      elif packet_id == PLAY_ENTITY_POSITION_SYNC:
        eid, pos = read_varint(payload, 0)
        x, y, z = struct.unpack(">ddd", payload[pos:pos + 24])
        if eid in self.entities:
          self.entities[eid].update(x=x, y=y, z=z)
      if packet_id == PLAY_SET_TIME:
        _, day_time = struct.unpack(">qq", payload[:16])
        self.time_of_day.append((time.monotonic(), day_time))
      if packet_id == PLAY_KEEP_ALIVE:
        self.keep_alives.append(time.monotonic())
        self.send(SB_PLAY_KEEP_ALIVE, payload)
      elif packet_id == PLAY_CHUNK:
        self.chunks_received += 1
      elif packet_id == PLAY_SYNC_POSITION:
        teleport_id, pos = read_varint(payload, 0)
        x, y, z = struct.unpack(">ddd", payload[pos:pos + 24])
        self.position = (x, y, z)
        self.send(SB_PLAY_CONFIRM_TELEPORT, varint(teleport_id))
        if not self.loaded:
          # Like the vanilla client: once, when the world first shows
          self.send(SB_PLAY_PLAYER_LOADED)
          self.loaded = True
      elif packet_id == PLAY_DISCONNECT:
        raise Disconnected(self.name + ": kicked")

  def pump(self, seconds):
    """Process incoming packets for `seconds`."""
    end = time.monotonic() + seconds
    while True:
      left = end - time.monotonic()
      if left <= 0:
        return
      packet = self.recv(timeout=left)
      if packet is not None:
        self._handle(*packet)

  def play_for(self, seconds):
    self.pump(seconds)

  def move_to(self, x, y, z):
    self.send(SB_PLAY_SET_POSITION, struct.pack(">dddB", x, y, z, 1))
    self.position = (x, y, z)

  def walk_circle(self, seconds, radius=40.0, step_s=0.05, speed=20.0):
    """Walk in a circle around the spawn point, answering keep-alives.

    `speed` is in blocks per second: the default 20 is a stress test, about
    five times a walking player (4.3)."""
    cx, y, cz = self.position
    start = time.monotonic()
    while time.monotonic() - start < seconds:
      angle = (time.monotonic() - start) * speed / radius
      self.move_to(cx + radius * math.cos(angle), y, cz + radius * math.sin(angle))
      self.pump(step_s)

  def use_item(self, yaw=0.0, pitch=0.0):
    """Right click in the air with the held item (draws a bow)."""
    self.send(SB_PLAY_USE_ITEM, varint(0) + varint(0) + struct.pack(">ff", yaw, pitch))

  def use_item_on(self, x, y, z, face=1):
    """Right click on a block face (1 = its top): places the held block, uses a bed."""
    self.send(SB_PLAY_USE_ITEM_ON, varint(0) + block_position(x, y, z) + varint(face)
              + struct.pack(">fff", 0.5, 1.0, 0.5) + bytes([0, 0]) + varint(0))

  def release_use_item(self):
    """Lets go of the held item (shoots a drawn bow)."""
    self.send(SB_PLAY_PLAYER_ACTION, varint(5) + block_position(0, 0, 0) + bytes([0]) + varint(0))

  def break_block(self, x, y, z):
    for status in (0, 2):
      self.send(SB_PLAY_PLAYER_ACTION, varint(status) + block_position(x, y, z) + bytes([1]) + varint(1))

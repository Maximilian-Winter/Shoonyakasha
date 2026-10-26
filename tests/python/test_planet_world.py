"""Tests for PlanetWorld's probes of shoonyakasha.planet (world.py), on a
world that is not set up, with a fake engine whose read-backs are given:
the ground under the viewpoint is taken from a probe only while the
viewpoint is over the same body and within GROUND_PROBE_REACH of where the
probe measured it, and only for the current terrain version.

    python -m unittest discover -s tests/python
"""

import unittest

import numpy as np

from shoonyakasha.planet import world as planet_world
from shoonyakasha.planet.vec import add, normalize, scale


class FakeEngine:
    def __init__(self):
        self.readbacks = {}
        self.framebuffer_size = (1600, 900)

    def get_readback(self, name):
        return self.readbacks.get(name)


class GroundProbeTest(unittest.TestCase):
    def setUp(self):
        self.engine = FakeEngine()
        self.world = planet_world.PlanetWorld(self.engine)
        self.up = self.world.site_frame()[1]
        self.east = self.world.site_frame()[2]
        self.world._lod = _NoLod()

    def answer(self, stamp, ground):
        """ProbeHeights with `ground` under the viewpoint and the site at 400 m, for `stamp`."""
        rows = np.zeros((planet_world.PROBES, 4), dtype=np.float32)
        rows[:, 1] = stamp
        rows[0, 0] = ground
        rows[1, 0] = 400.0
        self.engine.readbacks["ProbeHeights"] = {"data": rows.tobytes(), "frame": 0}

    def ask(self, stamp, direction):
        world = self.world
        world.probe_requests[float(stamp)] = (world.terrain_version, world.planet, direction, None, [])

    def at(self, direction, height=1000.0):
        return scale(direction, planet_world.RADIUS + height)

    def test_ground_taken_where_it_was_measured(self):
        self.ask(1, self.up)
        self.world.viewpoint = self.at(self.up)
        self.answer(1, 812.0)
        self.world.read_back_heights()
        self.assertEqual(self.world.ground, 812.0)
        self.assertEqual(self.world.site_height, 400.0)

    def test_ground_dropped_after_a_jump(self):
        self.ask(2, self.up)
        far = normalize(add(self.up, scale(self.east, 5_000.0 / planet_world.RADIUS)))
        self.world.viewpoint = self.at(far)
        self.answer(2, 2177.0)
        self.world.read_back_heights()
        self.assertIsNone(self.world.ground)

    def test_ground_kept_for_a_short_move(self):
        self.ask(3, self.up)
        near = normalize(add(self.up, scale(self.east, 300.0 / planet_world.RADIUS)))
        self.world.viewpoint = self.at(near)
        self.answer(3, 640.0)
        self.world.read_back_heights()
        self.assertEqual(self.world.ground, 640.0)

    def test_old_terrain_version_ignored(self):
        self.ask(4, self.up)
        self.world.terrain_version += 1
        self.world.viewpoint = self.at(self.up)
        self.answer(4, 999.0)
        self.world.read_back_heights()
        self.assertIsNone(self.world.ground)


class _NoLod:
    """Stands in for sk.PlanetLod where no chunk is read back."""

    def take_heights(self, data):
        pass


if __name__ == "__main__":
    unittest.main()

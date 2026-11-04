"""Tests for PlanetWorld's probes of shoonyakasha.planet (world.py), on a
world that is not set up, with a fake engine whose read-backs are given:
the ground under the viewpoint is taken from a probe only while the
viewpoint is over the same body and within GROUND_PROBE_REACH of where the
probe measured it, and only for the current terrain version, with the
ore richness there. A survey's heights come back by index, in batches
of at most PATCH_PROBES from where the last ended, only for its own
round, and hold the terrain back until it ends; the flat sites are
uploaded and build again the bodies whose sites changed; the clearings
are checked and uploaded without building anything again.

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
        self.custom = {}

    def get_readback(self, name):
        return self.readbacks.get(name)

    def set_custom_vec4(self, name, value):
        self.custom[name] = tuple(value)

    def set_custom_vec4_array(self, name, values):
        self.custom[name] = [tuple(v) for v in values]


class GroundProbeTest(unittest.TestCase):
    def setUp(self):
        self.engine = FakeEngine()
        self.world = planet_world.PlanetWorld(self.engine)
        self.up = self.world.site_frame()[1]
        self.east = self.world.site_frame()[2]
        self.world._lod = _NoLod()

    def answer(self, stamp, ground, ore=0.0):
        """ProbeHeights with `ground` and `ore` under the viewpoint and the site at 400 m, for `stamp`."""
        rows = np.zeros((planet_world.PROBES, 8), dtype=np.float32)
        rows[:, 1] = stamp
        rows[0, 0] = ground
        rows[0, 4] = ore
        rows[1, 0] = 400.0
        self.engine.readbacks["ProbeHeights"] = {"data": rows.tobytes(), "frame": 0}

    def ask(self, stamp, direction):
        world = self.world
        world.probe_requests[float(stamp)] = (world.terrain_version, world.planet, direction, None, [],
                                              (world.survey_round, []))

    def at(self, direction, height=1000.0):
        return scale(direction, planet_world.RADIUS + height)

    def test_ground_taken_where_it_was_measured(self):
        self.ask(1, self.up)
        self.world.viewpoint = self.at(self.up)
        self.answer(1, 812.0, ore=0.5)
        self.world.read_back_heights()
        self.assertEqual(self.world.ground, 812.0)
        self.assertEqual(self.world.ground_ore, 0.5)
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


class SurveyTest(unittest.TestCase):
    def setUp(self):
        self.engine = FakeEngine()
        self.world = planet_world.PlanetWorld(self.engine)
        self.world._lod = _NoLod()
        self.up = self.world.site_frame()[1]
        self.points = [(self.up, self.world.planet)] * (planet_world.PATCH_PROBES + 30)

    def answer(self, stamp, batch, heights):
        world = self.world
        world.probe_requests[float(stamp)] = (world.terrain_version, world.planet, self.up, None, [], batch)
        rows = np.zeros((planet_world.PROBES, 8), dtype=np.float32)
        rows[:, 1] = stamp
        rows[2:2 + len(heights), 0] = heights
        self.engine.readbacks["ProbeHeights"] = {"data": rows.tobytes(), "frame": 0}
        world.viewpoint = scale(self.up, planet_world.RADIUS + 10.0)
        world.read_back_heights()

    def test_batches_come_back_by_index(self):
        world = self.world
        world.start_survey(self.points)
        first = world.survey_batch()
        self.assertEqual(first, list(range(planet_world.PATCH_PROBES)))
        second = world.survey_batch()
        self.assertEqual(second[:30], list(range(planet_world.PATCH_PROBES, len(self.points))))
        self.answer(1, (world.survey_round, first), [float(i) for i in first])
        self.assertIsNone(world.surveyed())
        self.assertEqual(world.survey_batch()[:2], [planet_world.PATCH_PROBES, planet_world.PATCH_PROBES + 1])
        rest = list(range(planet_world.PATCH_PROBES, len(self.points)))
        self.answer(2, (world.survey_round, rest), [float(i) for i in rest])
        self.assertEqual(world.surveyed(), [float(i) for i in range(len(self.points))])

    def test_another_rounds_heights_ignored(self):
        world = self.world
        world.start_survey(self.points)
        batch = world.survey_batch()
        old = world.survey_round
        world.start_survey(self.points)
        self.answer(1, (old, batch), [5.0] * len(batch))
        self.assertEqual(world.survey_heights, {})

    def test_terrain_waits_for_the_end_of_the_survey(self):
        world = self.world
        world.site_height = 400.0
        world.start_survey(self.points)
        self.assertFalse(world.begin_frame())
        self.assertFalse(world.lod.created)
        world.end_survey()
        self.assertEqual(world.survey_batch(), [])
        self.assertTrue(world.begin_frame())
        self.assertIn(world.planet.index, world.lod.created)

    def test_sites_uploaded_and_their_body_built_again(self):
        world = self.world
        world.lod.trees = {0}
        site = planet_world.Site(0, (0.0, 1.0, 0.0), 300.0, 150.0, 600.0)
        world.set_sites([site])
        sites = self.engine.custom["planet.terrain.sites"]
        shapes = self.engine.custom["planet.terrain.siteShapes"]
        self.assertEqual(len(sites), planet_world.MAX_SITES)
        self.assertEqual(sites[0], (0.0, 1.0, 0.0, 300.0))
        self.assertEqual(shapes[0], (150.0, 600.0, 0.0, planet_world.RADIUS))
        self.assertEqual(shapes[1], (0.0, 0.0, 0.0, 0.0))
        self.assertEqual(world.lod.dropped, [0])
        self.assertEqual(world.lod.created, [0])
        world.set_sites([site])
        self.assertEqual(world.lod.dropped, [0])
        with self.assertRaises(ValueError):
            world.set_sites([site] * (planet_world.MAX_SITES + 1))

    def test_lakes_uploaded_and_the_planet_built_again(self):
        world = self.world
        world.lod.trees = {0, 1}
        lake = planet_world.Lake((0.0, 2.0, 0.0), 120.0, 300.0, 15.0, 180.0, 0.2)
        world.set_lakes([lake])
        centres = self.engine.custom["planet.terrain.lakes"]
        shapes = self.engine.custom["planet.terrain.lakeShapes"]
        self.assertEqual(len(centres), planet_world.MAX_LAKES)
        self.assertEqual(len(shapes), planet_world.MAX_LAKES)
        self.assertEqual(centres[0], (0.0, 1.0, 0.0, 120.0))
        self.assertEqual(shapes[0], (300.0, 15.0, 180.0, 0.2))
        self.assertEqual(shapes[1], (0.0, 0.0, 0.0, 0.0))
        self.assertEqual(world.lod.dropped, [0])
        self.assertEqual(world.lod.created, [0])
        self.assertEqual(world.terrain_version, 1)
        world.set_lakes([lake])
        self.assertEqual(world.lod.dropped, [0])
        world.set_lakes([])
        self.assertEqual(world.lod.dropped, [0, 0])
        self.assertEqual(self.engine.custom["planet.terrain.lakeShapes"][0], (0.0, 0.0, 0.0, 0.0))

    def test_lakes_set_before_the_planet_is_made_build_nothing(self):
        world = self.world
        world.lod.trees = set()
        world.set_lakes([planet_world.Lake((0.0, 1.0, 0.0), 120.0, 300.0, 15.0, 180.0)])
        self.assertEqual(world.lod.created, [])
        self.assertEqual(world.terrain_version, 0)

    def test_lakes_checked(self):
        world = self.world
        good = planet_world.Lake((0.0, 1.0, 0.0), 120.0, 300.0, 15.0, 180.0)
        for bad in (planet_world.Lake((0.0, 1.0, 0.0), 120.0, 0.0, 15.0, 180.0),
                    planet_world.Lake((0.0, 1.0, 0.0), 120.0, 300.0, 0.0, 180.0),
                    planet_world.Lake((0.0, 1.0, 0.0), 120.0, 300.0, 15.0, -1.0),
                    planet_world.Lake((0.0, 1.0, 0.0), 120.0, 300.0, 15.0, 180.0, 0.8)):
            with self.assertRaises(ValueError):
                world.set_lakes([bad])
        with self.assertRaises(ValueError):
            world.set_lakes([good] * (planet_world.MAX_LAKES + 1))
        self.assertEqual(world.lakes, [])

    def test_lake_level(self):
        world = self.world
        world.set_lakes([planet_world.Lake((0.0, 1.0, 0.0), 120.0, 300.0, 15.0, 180.0, 0.25)])
        reach = 300.0 * (1.0 + planet_world.LAKE_WOBBLE_REACH * 0.25)
        east = (1.0, 0.0, 0.0)
        at = lambda metres: normalize(add((0.0, 1.0, 0.0), scale(east, metres / planet_world.RADIUS)))
        self.assertEqual(world.lake_level((0.0, 1.0, 0.0)), 120.0)
        self.assertEqual(world.lake_level(at(reach - 1.0)), 120.0)
        self.assertIsNone(world.lake_level(at(reach + 1.0)))

    def test_regenerate_drops_the_lakes(self):
        world = self.world
        world.set_lakes([planet_world.Lake((0.0, 1.0, 0.0), 120.0, 300.0, 15.0, 180.0)])
        world.regenerate()
        self.assertEqual(world.lakes, [])
        self.assertEqual(self.engine.custom["planet.terrain.lakeShapes"][0], (0.0, 0.0, 0.0, 0.0))

    def test_clearings_uploaded_without_building_again(self):
        world = self.world
        world.lod.trees = {0, 1}
        circle = planet_world.Clearing(0, (0.0, 2.0, 0.0), radius=40.0, edge=5.0, clears=planet_world.CLEAR_PLANTS)
        rectangle = planet_world.Clearing(1, (0.0, 0.0, 1.0), size=(30.0, 10.0), heading=0.5 * np.pi)
        world.set_clearings([circle, rectangle])
        centres = self.engine.custom["planet.vegetation.clearings"]
        shapes = self.engine.custom["planet.vegetation.clearingShapes"]
        axes = self.engine.custom["planet.vegetation.clearingAxes"]
        for values in (centres, shapes, axes):
            self.assertEqual(len(values), planet_world.MAX_CLEARINGS)
            self.assertEqual(values[2], (0.0, 0.0, 0.0, 0.0))
        self.assertEqual(centres[0], (0.0, 1.0, 0.0, 0.0))
        self.assertEqual(shapes[0], (40.0, 0.0, 5.0, float(planet_world.CLEAR_PLANTS)))
        self.assertEqual(axes[0][3], planet_world.RADIUS)
        self.assertEqual(centres[1], (0.0, 0.0, 1.0, 1.0))
        self.assertEqual(shapes[1], (15.0, 5.0, 0.0, float(planet_world.CLEAR_ALL)))
        np.testing.assert_allclose(axes[1], (1.0, 0.0, 0.0, planet_world.MOON_RADIUS), atol=1e-12)
        self.assertEqual(world.clearings, [circle, rectangle])
        self.assertEqual(world.lod.dropped, [])
        self.assertEqual(world.terrain_version, 0)

    def test_clearings_checked(self):
        world = self.world
        good = planet_world.Clearing(0, (0.0, 1.0, 0.0), radius=10.0)
        for bad in (planet_world.Clearing(0, (0.0, 1.0, 0.0)),
                    planet_world.Clearing(0, (0.0, 1.0, 0.0), radius=10.0, size=(5.0, 5.0)),
                    planet_world.Clearing(0, (0.0, 1.0, 0.0), size=(5.0, 0.0)),
                    planet_world.Clearing(0, (0.0, 1.0, 0.0), radius=10.0, clears=0),
                    planet_world.Clearing(0, (0.0, 1.0, 0.0), radius=10.0, clears=8)):
            with self.assertRaises(ValueError):
                world.set_clearings([bad])
        with self.assertRaises(ValueError):
            world.set_clearings([good] * (planet_world.MAX_CLEARINGS + 1))
        self.assertEqual(world.clearings, [])

    def test_regenerate_drops_the_planets_sites(self):
        world = self.world
        planet_site = planet_world.Site(0, self.up, 300.0, 150.0, 600.0)
        moon_site = planet_world.Site(1, (0.0, 1.0, 0.0), -40.0, 100.0, 400.0)
        world.set_sites([planet_site, moon_site])
        world.regenerate()
        self.assertEqual(world.sites, [moon_site])


class _NoLod:
    """Stands in for sk.PlanetLod where no chunk is read back; records the trees made and dropped."""

    def __init__(self):
        self.trees = set()
        self.created = []
        self.dropped = []

    def take_heights(self, data):
        pass

    def has_tree(self, index):
        return index in self.trees

    def create_tree(self, index, *args):
        self.trees.add(index)
        self.created.append(index)

    def drop_tree(self, index):
        self.trees.discard(index)
        self.dropped.append(index)


if __name__ == "__main__":
    unittest.main()

"""Tests for PlanetWorld's probes of shoonyakasha.planet (world.py), on a
world that is not set up, with a fake engine whose read-backs are given:
the ground under the viewpoint is taken from a probe only while the
viewpoint is over the same body and within GROUND_PROBE_REACH of where the
probe measured it, and only for the current terrain version, with the
ore richness there. A survey's heights come back by index, in batches
of at most PATCH_PROBES from where the last ended, only for its own
round, and hold the terrain back until it ends; the flat sites are
uploaded and build again the bodies whose sites changed; the craters are
uploaded and queue the chunks near those added or removed; the clearings
are checked and uploaded without building anything again; the rivers
are uploaded with their water's mesh and build the planet again when
they change, their water and the lakes' make water_level, and the height
grid is asked for and taken back by its stamp.

    python -m unittest discover -s tests/python
"""

import unittest

import numpy as np

from shoonyakasha.planet import rivers
from shoonyakasha.planet import world as planet_world
from shoonyakasha.planet.vec import add, normalize, scale


class FakeEngine:
    def __init__(self):
        self.readbacks = {}
        self.framebuffer_size = (1600, 900)
        self.custom = {}
        self.buffers = {}
        self.passes = {}
        self.readback_requests = []

    def set_buffer(self, name, data):
        self.buffers[name] = np.asarray(data)

    def set_pass_enabled(self, name, on):
        self.passes[name] = on

    def request_readback(self, name):
        self.readback_requests.append(name)
        return True

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

    def test_craters_uploaded_and_the_chunks_near_them_queued(self):
        world = self.world
        world.lod.trees = {0}
        crater = planet_world.Crater(0, (0.0, 2.0, 0.0), 50.0, 8.0, 3.0)
        version = world.terrain_version
        self.assertEqual(world.set_craters([crater]), 7)
        centres = self.engine.custom["planet.terrain.craters"]
        shapes = self.engine.custom["planet.terrain.craterShapes"]
        self.assertEqual(len(centres), planet_world.MAX_CRATERS)
        self.assertEqual(centres[0], (0.0, 1.0, 0.0, 50.0))
        self.assertEqual(shapes[0], (8.0, 3.0, 0.0, planet_world.RADIUS))
        self.assertEqual(centres[1][3], 0.0)
        self.assertEqual(world.lod.requeued, [(0, (0.0, 1.0, 0.0), 100.0)])
        self.assertEqual(world.terrain_version, version + 1)
        self.assertEqual(world.lod.dropped, [])
        self.assertEqual(world.set_craters([crater]), 0)               # unchanged
        moon = planet_world.Crater(1, (1.0, 0.0, 0.0), 20.0, 4.0, 1.0)   # no moon tree: nothing queued
        self.assertEqual(world.set_craters([crater, moon]), 0)
        self.assertEqual(world.terrain_version, version + 1)
        world.set_craters([moon])                                        # removed: queued again
        self.assertEqual(world.lod.requeued[-1], (0, (0.0, 1.0, 0.0), 100.0))
        with self.assertRaises(ValueError):
            world.set_craters([crater] * (planet_world.MAX_CRATERS + 1))

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

    def river_and_frame(self):
        frame = rivers.GridFrame(self.up, planet_world.RADIUS, size=32, spacing=200.0)
        xs = np.linspace(-1_000.0, 1_000.0, 5)
        points = tuple(tuple(d) for d in frame.from_plane(xs, np.zeros_like(xs)))
        return planet_world.River(points, (90.0, 89.0, 88.0, 87.0, 86.0), (12.0,) * 5, (2.0,) * 5), frame

    def test_rivers_uploaded_and_the_planet_built_again(self):
        world = self.world
        world.lod.trees = {0, 1}
        river, frame = self.river_and_frame()
        world.set_rivers([river], frame)
        engine = self.engine
        self.assertEqual(engine.buffers["RiverData"].shape, (rivers.MAX_DATA, 4))
        self.assertEqual(engine.custom["planet.terrain.riverFrame"][0][3], 4.0)
        self.assertEqual(engine.buffers["RiverVertices"].shape, (10, 8))
        self.assertEqual(list(engine.buffers["RiverDraws"]), [1, 0, 0, 0, 24, 1, 0, 0, 0])
        self.assertEqual(world.lod.dropped, [0])
        self.assertEqual(world.lod.created, [0])
        world.set_rivers([river], frame)
        self.assertEqual(world.lod.dropped, [0])
        world.set_rivers([])
        self.assertEqual(world.lod.dropped, [0, 0])
        self.assertEqual(list(engine.buffers["RiverDraws"]), [0])
        self.assertEqual(engine.custom["planet.terrain.riverFrame"][0], (0.0, 0.0, 0.0, 0.0))
        with self.assertRaises(ValueError):
            world.set_rivers([river])

    def test_the_rivers_origin_follows_the_camera(self):
        world = self.world
        river, frame = self.river_and_frame()
        world.set_rivers([river], frame)
        world.position = add(scale(self.up, planet_world.RADIUS + 50.0), scale(self.world.site_frame()[2], 300.0))
        world.upload_river_origin()
        origin = self.engine.custom["planet.terrain.riverOrigin"]
        np.testing.assert_allclose(origin[:3], np.subtract(scale(tuple(frame.centre), planet_world.RADIUS),
                                                           world.position), atol=1e-9)

    def test_water_level_of_the_lakes_and_the_rivers(self):
        world = self.world
        river, frame = self.river_and_frame()
        world.set_rivers([river], frame)
        lake_centre = tuple(frame.from_plane(np.array(0.0), np.array(2_000.0)))
        world.set_lakes([planet_world.Lake(lake_centre, 120.0, 300.0, 15.0, 180.0, 0.2)])
        self.assertEqual(world.water_level(tuple(frame.from_plane(np.array(0.0), np.array(2_000.0)))), 120.0)
        self.assertAlmostEqual(world.water_level(tuple(frame.from_plane(np.array(0.0), np.array(3.0)))), 88.0)
        self.assertIsNone(world.water_level(tuple(frame.from_plane(np.array(0.0), np.array(700.0)))))

    def test_the_height_grid_by_its_stamp(self):
        world, engine = self.world, self.engine
        frame = rivers.GridFrame(self.up, planet_world.RADIUS, size=4, spacing=100.0)
        world.request_height_grid(frame)
        self.assertTrue(engine.passes["HeightGrid"])
        stamp = engine.custom["planet.heightGrid.grid"][2]
        self.assertEqual(engine.custom["planet.heightGrid.grid"][:2], (4.0, 100.0))
        values = np.zeros(planet_world.HEIGHT_GRID_HEADER + 16, dtype=np.float32)
        values[planet_world.HEIGHT_GRID_HEADER:] = np.arange(16)
        values[0] = stamp - 1.0
        engine.readbacks["HeightGrid"] = {"data": values.tobytes(), "frame": 0}
        self.assertIsNone(world.height_grid())
        self.assertTrue(engine.passes["HeightGrid"])
        values[0] = stamp
        engine.readbacks["HeightGrid"] = {"data": values.tobytes(), "frame": 1}
        heights = world.height_grid()
        np.testing.assert_array_equal(heights, np.arange(16).reshape(4, 4))
        self.assertFalse(engine.passes["HeightGrid"])
        self.assertIsNone(world.height_grid())
        with self.assertRaises(ValueError):
            world.request_height_grid(rivers.GridFrame(self.up, planet_world.RADIUS, size=513))

    def test_regenerate_drops_the_rivers(self):
        world = self.world
        river, frame = self.river_and_frame()
        world.set_rivers([river], frame)
        world.regenerate()
        self.assertEqual(world.rivers, [])
        self.assertEqual(list(self.engine.buffers["RiverDraws"]), [0])

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
    """Stands in for sk.PlanetLod where no chunk is read back; records the trees made and dropped and
    the chunks queued near a point (each call queueing 7)."""

    def __init__(self):
        self.trees = set()
        self.created = []
        self.dropped = []
        self.requeued = []

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

    def requeue_near(self, index, direction, reach):
        self.requeued.append((index, tuple(direction), reach))
        return 7


if __name__ == "__main__":
    unittest.main()

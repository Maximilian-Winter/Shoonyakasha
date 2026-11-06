"""Tests for the walker's meteor showers (examples/python/simulations/
planet/events.py), loaded from its folder: a planned shower's start, centre
and impacts within their ranges, one large impact at the centre with its
crater; a meteor's velocity as it strikes; its path flown back from where it
strikes to the entry altitude under gravity; the craters kept, the oldest
far one dropped first; a crater's record; and the heat. Plain Python: no
engine needed.

    python -m unittest discover -s tests/python
"""

import math
import random
import sys
import unittest
from pathlib import Path

PLANET_DIR = Path(__file__).resolve().parents[2] / "examples" / "python" / "simulations" / "planet"
sys.path.insert(0, str(PLANET_DIR))

import events  # noqa: E402
from shoonyakasha.planet.vec import dot, length, local_frame, normalize, scale  # noqa: E402
from shoonyakasha.planet.world import Crater  # noqa: E402

RADIUS = 100_000.0
GM = 9.81 * RADIUS * RADIUS
PLAYER = normalize((0.3, 0.8, 0.5))


class Showers(unittest.TestCase):
    def test_planned_within_the_ranges(self):
        for seed in range(40):
            shower = events.schedule(1_000.0, random.Random(seed), PLAYER, RADIUS)
            self.assertGreaterEqual(shower.start, 1_000.0 + events.MIN_GAP)
            away = events.ground_distance(PLAYER, shower.centre, RADIUS)
            self.assertTrue(events.AREA_NEAR - 1e-6 <= away <= events.AREA_FAR + 1e-6, away)
            times = [i.time for i in shower.impacts]
            self.assertEqual(times, sorted(times))
            self.assertTrue(all(shower.start <= t <= shower.start + events.SPREAD_SECONDS for t in times))
            large = [i for i in shower.impacts if i.large]
            self.assertEqual(len(large), 1)
            self.assertEqual(large[0].direction, shower.centre)
            self.assertTrue(events.LARGE_MASS[0] <= large[0].mass <= events.LARGE_MASS[1])
            self.assertEqual(large[0].crater, events.crater_of(large[0].mass))
            small = [i for i in shower.impacts if not i.large]
            self.assertTrue(events.SMALL_IMPACTS[0] <= len(small) <= events.SMALL_IMPACTS[1])
            for i in shower.impacts:
                self.assertTrue(events.ground_distance(i.direction, shower.centre, RADIUS) <= events.SPREAD[1] + 1e-6)
                self.assertAlmostEqual(length(i.direction), 1.0)
                self.assertTrue(events.SPEED[0] <= i.speed <= events.SPEED[1])
                self.assertTrue(events.ANGLE[0] <= i.angle <= events.ANGLE[1])
                self.assertTrue(events.GRADE[0] <= i.grade <= events.GRADE[1])
            for i in small:
                self.assertTrue(events.SMALL_MASS[0] <= i.mass <= events.SMALL_MASS[1])
            bearings = [i.bearing for i in shower.impacts]
            spread = max((b - bearings[0] + 180.0) % 360.0 - 180.0 for b in bearings) \
                - min((b - bearings[0] + 180.0) % 360.0 - 180.0 for b in bearings)
            self.assertLessEqual(spread, 2.0 * events.RADIANT_JITTER + 1e-9)

    def test_a_given_start(self):
        shower = events.schedule(1_000.0, random.Random(1), PLAYER, RADIUS, start=1_100.0)
        self.assertEqual(shower.start, 1_100.0)

    def test_the_crater_grows_with_the_mass(self):
        light, heavy = events.crater_of(events.LARGE_MASS[0]), events.crater_of(events.LARGE_MASS[1])
        self.assertEqual(light[0], events.CRATER_RADIUS[0])
        self.assertEqual(heavy[0], events.CRATER_RADIUS[1])
        self.assertAlmostEqual(heavy[1], events.DEPTH_SHARE * heavy[0])
        self.assertAlmostEqual(heavy[2], events.RIM_SHARE * heavy[0])

    def test_along_ground(self):
        east, north = local_frame(PLAYER)
        d = events.along_ground(PLAYER, 90.0, 5_000.0, RADIUS)
        self.assertAlmostEqual(events.ground_distance(PLAYER, d, RADIUS), 5_000.0, places=6)
        self.assertGreater(dot(d, east), 0.0)
        self.assertAlmostEqual(dot(d, north), 0.0, places=9)


class Paths(unittest.TestCase):
    def impact(self, speed, angle, bearing=0.0):
        return events.Impact(0.0, PLAYER, 1.0, 0.5, speed, angle, bearing)

    def test_velocity(self):
        east, north = local_frame(PLAYER)
        v = events.velocity(self.impact(3_000.0, 30.0, 90.0), PLAYER)
        self.assertAlmostEqual(length(v), 3_000.0)
        self.assertAlmostEqual(dot(v, PLAYER), -3_000.0 * math.sin(math.radians(30.0)))
        self.assertAlmostEqual(dot(v, east), 3_000.0 * math.cos(math.radians(30.0)))
        self.assertAlmostEqual(dot(v, north), 0.0, places=6)

    def test_flown_back_to_the_entry_altitude(self):
        point = scale(PLAYER, RADIUS + 250.0)
        for speed, angle in ((2_000.0, 25.0), (4_000.0, 60.0)):
            impact = self.impact(speed, angle)
            path = events.Path(point, events.velocity(impact, PLAYER), GM, RADIUS)
            self.assertEqual(path.at(0.0), point)
            entry = length(path.at(path.duration)) - RADIUS
            self.assertGreaterEqual(entry, events.ENTRY_ALTITUDE)
            self.assertLess(entry, events.ENTRY_ALTITUDE + speed * events.PATH_STEP)
            heights = [length(path.at(t)) for t in (0.0, 0.25 * path.duration, 0.5 * path.duration, path.duration)]
            self.assertEqual(heights, sorted(heights))
            # A step back moves it about its speed times the step.
            step = length(tuple(a - b for a, b in zip(path.at(events.PATH_STEP), point)))
            self.assertAlmostEqual(step, speed * events.PATH_STEP, delta=0.01 * speed * events.PATH_STEP)

    def test_gravity_bends_it(self):
        """Flown back, it lies below the straight line back along its velocity."""
        point = scale(PLAYER, RADIUS)
        impact = self.impact(2_000.0, 25.0)
        v = events.velocity(impact, PLAYER)
        path = events.Path(point, v, GM, RADIUS)
        t = 10.0
        straight = tuple(p - u * t for p, u in zip(point, v))
        self.assertLess(length(path.at(t)), length(straight))

    def test_between_steps(self):
        point = scale(PLAYER, RADIUS)
        path = events.Path(point, events.velocity(self.impact(3_000.0, 40.0), PLAYER), GM, RADIUS)
        a, b = path.at(events.PATH_STEP), path.at(2.0 * events.PATH_STEP)
        middle = path.at(1.5 * events.PATH_STEP)
        for m, x, y in zip(middle, a, b):
            self.assertAlmostEqual(m, 0.5 * (x + y))
        self.assertEqual(path.at(-1.0), point)
        self.assertEqual(path.at(path.duration + 5.0), path.points[-1])


class Craters(unittest.TestCase):
    def crater(self, bearing, distance):
        return Crater(0, events.along_ground(PLAYER, bearing, distance, RADIUS), 30.0, 5.0, 2.0)

    def test_kept_until_full(self):
        craters = []
        for k in range(events.MAX_CRATERS):
            craters = events.keep(craters, self.crater(0.0, 1_000.0 * k), float(k), PLAYER, RADIUS)
        self.assertEqual(len(craters), events.MAX_CRATERS)
        self.assertEqual([t for _, t in craters], [float(k) for k in range(events.MAX_CRATERS)])

    def test_the_oldest_far_one_dropped_first(self):
        near = [(self.crater(0.0, 100.0 * k), float(k)) for k in range(events.MAX_CRATERS - 2)]
        far = [(self.crater(180.0, 30_000.0), 20.0), (self.crater(90.0, 25_000.0), 30.0)]
        craters = near + far
        new = self.crater(45.0, 500.0)
        kept = events.keep(craters, new, 40.0, PLAYER, RADIUS)
        self.assertEqual(len(kept), events.MAX_CRATERS)
        self.assertNotIn(far[0], kept)
        self.assertIn(far[1], kept)
        self.assertEqual(kept[-1], (new, 40.0))

    def test_the_oldest_dropped_when_none_is_far(self):
        craters = [(self.crater(0.0, 100.0 * k), 10.0 - k) for k in range(events.MAX_CRATERS)]
        kept = events.keep(craters, self.crater(45.0, 500.0), 40.0, PLAYER, RADIUS)
        self.assertNotIn(craters[-1], kept)          # the oldest
        self.assertIn(craters[0], kept)
        self.assertEqual(len(kept), events.MAX_CRATERS)

    def test_record(self):
        crater = self.crater(30.0, 2_000.0)
        record = events.crater_record(crater, 123.0, "planet")
        self.assertEqual(record["type"], "crater")
        self.assertEqual(record["body"], "planet")
        self.assertEqual(events.crater_from_record(record, 0, Crater), (crater, 123.0))


class Heat(unittest.TestCase):
    def test_cools_evenly(self):
        self.assertEqual(events.heat(0.0, 60.0), 1.0)
        self.assertAlmostEqual(events.heat(15.0, 60.0), 0.75)
        self.assertEqual(events.heat(60.0, 60.0), 0.0)
        self.assertEqual(events.heat(600.0, 60.0), 0.0)
        self.assertEqual(events.heat(-1.0, 60.0), 1.0)


if __name__ == "__main__":
    unittest.main()

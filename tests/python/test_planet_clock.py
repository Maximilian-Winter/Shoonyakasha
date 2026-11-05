"""Tests for the planet example's day (examples/python/simulations/planet/
clock.py), loaded from its file: the sun at noon 90 - AXIS_TILT degrees
up in the south, at six in the east on the horizon, at eighteen in the
west, at midnight as far below; a whole day brings it back; the hours and
the time nearest another of the same hour; and the exposure's factor from
day to night. Also shoonyakasha.planet.world's horizon rule: the azimuth
turn about the local up, and its tolerance from low to high sun.

    python -m unittest discover -s tests/python
"""

import importlib.util
import math
import unittest
from pathlib import Path

from shoonyakasha.planet import world

REPO = Path(__file__).resolve().parents[2]
_SPEC = importlib.util.spec_from_file_location(
    "planet_clock", REPO / "examples" / "python" / "simulations" / "planet" / "clock.py")
clock = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(clock)

EAST, NORTH, UP = (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)
DAY = clock.DAY_LENGTH


def angles(time):
    return clock.site_angles(clock.sun_direction(time, UP, EAST, NORTH), UP, EAST, NORTH)


class Sun(unittest.TestCase):
    def test_noon_six_eighteen_and_midnight(self):
        top = 90.0 - math.degrees(clock.AXIS_TILT)
        elevation, azimuth = angles(0.0)
        self.assertAlmostEqual(elevation, top)
        self.assertAlmostEqual(azimuth, 270.0)                 # south
        elevation, azimuth = angles(-DAY / 4.0)
        self.assertAlmostEqual(elevation, 0.0, places=6)
        self.assertAlmostEqual(azimuth % 360.0, 0.0, places=6)  # east
        elevation, azimuth = angles(DAY / 4.0)
        self.assertAlmostEqual(elevation, 0.0, places=6)
        self.assertAlmostEqual(azimuth, 180.0, places=6)        # west
        self.assertAlmostEqual(angles(DAY / 2.0)[0], -top)
        for a, b in zip(clock.sun_direction(123.0, UP, EAST, NORTH),
                        clock.sun_direction(123.0 + DAY, UP, EAST, NORTH)):
            self.assertAlmostEqual(a, b)

    def test_hours(self):
        self.assertAlmostEqual(clock.hour(0.0), 12.0)
        self.assertAlmostEqual(clock.hour(-DAY / 4.0), 6.0)
        self.assertAlmostEqual(clock.hour(DAY / 2.0), 0.0)
        now = 5.3 * DAY
        later = clock.time_of(20.0, now)
        self.assertAlmostEqual(clock.hour(later), 20.0)
        self.assertLessEqual(abs(later - now), DAY / 2.0)

    def test_local_hours(self):
        for time in (0.0, 0.1 * DAY, -0.3 * DAY, 0.45 * DAY):
            self.assertAlmostEqual(clock.local_hour(UP, time, UP, EAST, NORTH), clock.hour(time), places=6)
        # A quarter turn east about the axis is six hours later.
        axis = (0.0, math.cos(clock.AXIS_TILT), math.sin(clock.AXIS_TILT))
        east_of_site = clock.rotate(UP, axis, math.pi / 2.0)
        self.assertAlmostEqual(clock.local_hour(east_of_site, 0.0, UP, EAST, NORTH), 18.0, places=6)

    def test_exposure_from_day_to_night(self):
        self.assertEqual(clock.exposure_scale(30.0), 1.0)
        self.assertAlmostEqual(clock.exposure_scale(clock.NIGHT_ELEVATION - 5.0), clock.NIGHT_SCALE)
        middle = clock.exposure_scale(0.5 * (clock.DAY_ELEVATION + clock.NIGHT_ELEVATION))
        self.assertAlmostEqual(middle, math.sqrt(clock.NIGHT_SCALE))


class HorizonRule(unittest.TestCase):
    def test_azimuth_turn_and_tolerance(self):
        a = (math.cos(0.3), 0.0, math.sin(0.3))
        higher = (math.cos(0.6), 0.0, math.sin(0.6))           # same azimuth, higher
        turned = (math.cos(0.3) * math.cos(0.1), math.cos(0.3) * math.sin(0.1), math.sin(0.3))
        self.assertAlmostEqual(world.azimuth_turn(a, higher, UP), 0.0)
        self.assertAlmostEqual(world.azimuth_turn(a, turned, UP), 0.1)
        self.assertEqual(world.azimuth_turn(UP, turned, UP), 0.0)
        low, high = world.HORIZON_HIGH_SUN
        self.assertEqual(world.horizon_tolerance(low - 0.1), world.HORIZON_AZIMUTH_STEP)
        self.assertEqual(world.horizon_tolerance(high + 0.1), world.HORIZON_AZIMUTH_HIGH)


if __name__ == "__main__":
    unittest.main()

"""Tests for the planet example's acoustics.py: the world bus's gain and
cutoff for the air's density at the listener, silent in vacuum, and how
loud a strike is through the suit in vacuum.

    python -m unittest discover -s tests/python
"""

import math
import sys
import unittest
from pathlib import Path

PLANET_DIR = Path(__file__).resolve().parents[2] / "examples" / "python" / "simulations" / "planet"
sys.path.insert(0, str(PLANET_DIR))

import acoustics  # noqa: E402
from shoonyakasha.planet.orbits import Planet  # noqa: E402


class AirAudioTest(unittest.TestCase):
    def test_sea_level_is_open(self):
        self.assertEqual(acoustics.air_audio(1.0), (1.0, 20_000.0))
        self.assertEqual(acoustics.air_audio(2.0), (1.0, 20_000.0))

    def test_thinner_air_is_quieter_and_duller(self):
        gain, cutoff = acoustics.air_audio(0.25)
        self.assertAlmostEqual(gain, 0.25 + 0.75 * 0.5)
        self.assertAlmostEqual(cutoff, 400.0 + 19_600.0 * 0.25 ** 0.7)
        last = (1.0, 20_000.0)
        for d in (0.5, 0.1, 0.01, 0.001):
            gain, cutoff = acoustics.air_audio(d)
            self.assertLess(gain, last[0])
            self.assertLess(cutoff, last[1])
            last = (gain, cutoff)

    def test_vacuum_is_silent(self):
        self.assertEqual(acoustics.air_audio(0.0), (0.0, acoustics.VACUUM_CUTOFF))
        self.assertEqual(acoustics.air_audio(acoustics.VACUUM), (0.0, acoustics.VACUUM_CUTOFF))
        self.assertGreater(acoustics.air_audio(2.0 * acoustics.VACUUM)[0], 0.25)

    def test_the_planets_air_by_height(self):
        planet = Planet()
        def share(height):
            return planet.density((planet.radius + height, 0.0, 0.0)) / planet.air_density
        self.assertAlmostEqual(share(0.0), 1.0)
        self.assertGreater(acoustics.air_audio(share(10_000.0))[0], 0.25)
        vacuum_height = -planet.scale_height * math.log(acoustics.VACUUM)
        self.assertEqual(acoustics.air_audio(share(vacuum_height + 1.0))[0], 0.0)

    def test_sound_delay(self):
        self.assertAlmostEqual(acoustics.sound_delay(3_430.0), 10.0)
        self.assertEqual(acoustics.sound_delay(0.0), 0.0)

    def test_distance_cutoff(self):
        self.assertEqual(acoustics.distance_cutoff(1.0), acoustics.NEAR_CUTOFF)
        self.assertAlmostEqual(acoustics.distance_cutoff(acoustics.NEAR_DISTANCE), acoustics.NEAR_CUTOFF)
        self.assertAlmostEqual(acoustics.distance_cutoff(acoustics.FAR_DISTANCE), acoustics.FAR_CUTOFF)
        last = acoustics.NEAR_CUTOFF
        for d in (50.0, 300.0, 1_000.0, 4_000.0):
            self.assertLess(acoustics.distance_cutoff(d), last)
            last = acoustics.distance_cutoff(d)
        self.assertEqual(acoustics.distance_cutoff(1.0e7), acoustics.LOWEST_CUTOFF)

    def test_rumble_volume(self):
        self.assertEqual(acoustics.rumble_volume(0.0, True), 1.0)
        self.assertAlmostEqual(acoustics.rumble_volume(300.0, True), 0.8)
        self.assertEqual(acoustics.rumble_volume(300.0, False), 0.0)
        self.assertEqual(acoustics.rumble_volume(2_000.0, True), 0.0)

    def test_reverb_in_the_open_and_enclosed(self):
        times, wet = acoustics.reverb_from([None] * 13)
        self.assertAlmostEqual(times[1], acoustics.OPEN_RT60)
        self.assertAlmostEqual(wet, acoustics.OPEN_WET)
        self.assertEqual(times, tuple(acoustics.OPEN_RT60 * b for b in acoustics.BANDS))
        times, wet = acoustics.reverb_from([acoustics.REVERB_ROOM] * 13)
        self.assertAlmostEqual(times[1], acoustics.OPEN_RT60 + acoustics.ENCLOSED_RT60)
        self.assertAlmostEqual(wet, acoustics.OPEN_WET + acoustics.ENCLOSED_WET)
        small, _ = acoustics.reverb_from([5.0] * 13)
        self.assertLess(small[1], times[1])
        half, half_wet = acoustics.reverb_from([acoustics.REVERB_ROOM] * 6 + [None] * 6)
        self.assertAlmostEqual(half_wet, acoustics.OPEN_WET + 0.5 * acoustics.ENCLOSED_WET)
        self.assertLess(half[1], times[1])
        self.assertGreater(times[0], times[1])
        self.assertLess(times[2], times[1])

    def test_occluded_audio(self):
        self.assertEqual(acoustics.occluded_audio(False), (1.0, None))
        self.assertEqual(acoustics.occluded_audio(True), (acoustics.OCCLUDED_GAIN, acoustics.OCCLUDED_CUTOFF))

    def test_contact_volume(self):
        self.assertEqual(acoustics.contact_volume(0.0), 1.0)
        self.assertAlmostEqual(acoustics.contact_volume(0.5 * acoustics.CONTACT_DISTANCE), 0.5)
        self.assertEqual(acoustics.contact_volume(acoustics.CONTACT_DISTANCE), 0.0)
        self.assertEqual(acoustics.contact_volume(20.0), 0.0)


if __name__ == "__main__":
    unittest.main()

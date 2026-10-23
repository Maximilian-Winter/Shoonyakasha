"""
Tests for shoonyakasha.ies: reading IES LM-63 files, filling in angles by
the file's symmetry, and the default pipeline's lightProfiles atlas.

Run from the repository root:  python -m pytest tests/python
"""

import sys
import unittest
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "python"))

from shoonyakasha import ies  # noqa: E402

# A downlight: symmetric about its axis, 100 cd straight down, 50 at 45
# degrees, none from 90 up. Multiplier 2.
DOWNLIGHT = """IESNA:LM-63-2002
[TEST] unit test
[MANUFAC] nobody
TILT=NONE
1 1000 2.0 3 1 1 2 0 0 0
1.0 1.0 10
0 45 90
0
50 25 0
"""

# Quadrant symmetry (horizontal 0 to 90), values split over lines and commas;
# TILT=INCLUDE data to skip.
QUADRANT = """IESNA91
TILT=INCLUDE
1
2
0, 90
1, 1
1 -1 1 2 2 1 1 0 0 0
1 1 0
0 90
0 90
10 20
30 40
"""


def file_with(horizontal, rows, photometric_type=1):
    text = "TILT=NONE\n1 -1 1 2 %d %d 1 0 0 0\n1 1 0\n0 90\n" % (len(horizontal), photometric_type)
    text += " ".join(str(h) for h in horizontal) + "\n"
    text += "\n".join(" ".join(str(v) for v in row) for row in rows) + "\n"
    return ies.parse(text)


class Parse(unittest.TestCase):
    def test_angles_candela_and_keywords(self):
        p = ies.parse(DOWNLIGHT)
        self.assertEqual(p.vertical_angles, [0.0, 45.0, 90.0])
        self.assertEqual(p.horizontal_angles, [0.0])
        self.assertEqual(p.candela, [[100.0, 50.0, 0.0]])     # times the multiplier
        self.assertEqual(p.peak, 100.0)
        self.assertEqual(p.keywords["MANUFAC"], "nobody")

    def test_tilt_data_and_commas_are_read_past(self):
        p = ies.parse(QUADRANT)
        self.assertEqual(p.horizontal_angles, [0.0, 90.0])
        self.assertEqual(p.candela, [[10.0, 20.0], [30.0, 40.0]])

    def test_bad_files_raise(self):
        with self.assertRaises(ValueError):
            ies.parse("no tilt line here")
        with self.assertRaises(ValueError):
            ies.parse(DOWNLIGHT.replace("0\n50 25 0", "0\n50"))       # ends early
        with self.assertRaises(ValueError):
            file_with([0], [[1, 1]], photometric_type=2)              # type B


class Sample(unittest.TestCase):
    def test_vertical_interpolation_and_outside_range(self):
        p = ies.parse(DOWNLIGHT)
        self.assertAlmostEqual(float(p.sample(22.5, 0)), 75.0)
        self.assertAlmostEqual(float(p.sample(30.0, 123.0)), float(p.sample(30.0, 0.0)))   # axial
        self.assertEqual(float(p.sample(135.0, 0)), 0.0)

    def test_quadrant_symmetry(self):
        p = ies.parse(QUADRANT)
        self.assertAlmostEqual(float(p.sample(0, 45)), 20.0)
        for c in (135.0, 225.0, 315.0):                      # mirrors of 45
            self.assertAlmostEqual(float(p.sample(0, c)), 20.0)
        self.assertAlmostEqual(float(p.sample(0, 180)), 10.0)
        self.assertAlmostEqual(float(p.sample(0, 270)), 30.0)

    def test_bilateral_symmetry(self):
        p = file_with([0, 90, 180], [[1, 1], [2, 2], [3, 3]])
        self.assertAlmostEqual(float(p.sample(0, 270)), 2.0)
        self.assertAlmostEqual(float(p.sample(0, 315)), 1.5)

    def test_symmetry_about_the_90_to_270_plane(self):
        p = file_with([90, 180, 270], [[1, 1], [2, 2], [3, 3]])
        self.assertAlmostEqual(float(p.sample(0, 0)), 2.0)      # mirror of 180
        self.assertAlmostEqual(float(p.sample(0, 45)), 1.5)     # mirror of 135

    def test_full_turn_closes_on_the_first_angle(self):
        p = file_with([0, 120, 240], [[0, 0], [3, 3], [6, 6]])
        self.assertAlmostEqual(float(p.sample(0, 300)), 3.0)    # halfway from 240 back to 0
        self.assertAlmostEqual(float(p.sample(0, -60)), 3.0)


class Atlas(unittest.TestCase):
    def test_tiles_are_profiles_over_their_peaks(self):
        p = ies.parse(DOWNLIGHT)
        out = ies.atlas([p, p], tile_size=8)
        self.assertEqual(out.shape, (32, 32))
        self.assertEqual(out.dtype, np.float16)
        # Row 0 is near the light's direction, row 7 past 90 degrees.
        self.assertAlmostEqual(float(out[0, 0]), float(p.sample(11.25, 22.5)) / 100.0, places=3)
        self.assertEqual(float(out[7, 0]), 0.0)
        self.assertEqual(float(out[0, 8]), float(out[0, 0]))       # tile 2
        self.assertEqual(float(out[31, 31]), 1.0)                  # not given

    def test_slots_and_limits(self):
        self.assertEqual(ies.slot(1), 17)
        self.assertEqual(ies.slot(16), 32)
        with self.assertRaises(ValueError):
            ies.slot(0)
        with self.assertRaises(ValueError):
            ies.atlas([ies.parse(DOWNLIGHT)] * 17)
        with self.assertRaises(ValueError):
            ies.atlas([file_with([0], [[0, 0]])])


if __name__ == "__main__":
    unittest.main()

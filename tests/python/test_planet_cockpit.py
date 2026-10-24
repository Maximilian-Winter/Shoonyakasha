"""Tests for the planet example's cockpit geometry
(examples/python/simulations/planet/cockpit.py), loaded from its file.

    python -m unittest discover -s tests/python
"""

import importlib.util
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
_SPEC = importlib.util.spec_from_file_location(
    "planet_cockpit", REPO / "examples" / "python" / "simulations" / "planet" / "cockpit.py")
cockpit = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(cockpit)


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


class Cockpit(unittest.TestCase):
    def test_every_triangle_winds_counter_clockwise_around_its_normal(self):
        positions, normals, colours = cockpit.cockpit_mesh()
        self.assertEqual(len(positions) % 3, 0)
        self.assertEqual(len(positions), len(normals))
        self.assertEqual(len(positions), len(colours))
        for t in range(0, len(positions), 3):
            a, b, c = positions[t:t + 3]
            self.assertGreater(dot(cross(sub(b, a), sub(c, a)), normals[t]), 0.0, "triangle %d" % (t // 3))

    def test_the_displays_face_the_eye(self):
        for centre, right, up, _ in cockpit.SCREENS:
            normal = cross(right, up)
            self.assertGreater(dot(normal, tuple(-c for c in centre)), 0.0)
            self.assertAlmostEqual(dot(right, up), 0.0, places=12)

    def test_the_displays_sit_in_front_of_the_view_and_below_it(self):
        for centre, _, _, _ in cockpit.SCREENS:
            self.assertLess(centre[2], 0.0)       # forward is -Z
            self.assertLess(centre[1], 0.0)       # below the eye


if __name__ == "__main__":
    unittest.main()

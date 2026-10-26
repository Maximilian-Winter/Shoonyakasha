"""Tests for the shadow cascades of shoonyakasha.planet (shadows.py).

    python -m unittest discover -s tests/python
"""

import math
import random
import unittest

from shoonyakasha.planet import shadows

SPLITS = (0.05, 40.0, 400.0, 3000.0, 20000.0)
FOV, ASPECT, RESOLUTION, REACH = 70.0, 16.0 / 9.0, 2048, 5000.0
CAMERA = (1234.5, 98765.25, 4321.0)          # planet space, about the planet's radius out
FORWARD = shadows._normalize((0.3, -0.2, -1.0))
SUN = shadows._normalize((0.5, 0.7, 0.2))


def view_axes(forward):
    right = shadows._normalize(shadows._cross(forward, (0.0, 1.0, 0.0)))
    return right, shadows._cross(right, forward)


def frustum_point(forward, distance, sx, sy):
    """A point of the view at `distance` along it, sx and sy across the screen (-1 .. 1)."""
    right, up = view_axes(forward)
    t = math.tan(math.radians(FOV) * 0.5)
    return shadows._add(shadows._scale(forward, distance),
                        shadows._add(shadows._scale(right, sx * t * ASPECT * distance),
                                     shadows._scale(up, sy * t * distance)))


class Cascades(unittest.TestCase):
    def cascades(self, camera=CAMERA, forward=FORWARD):
        return shadows.fit_cascades(camera, forward, SUN, FOV, ASPECT, SPLITS, RESOLUTION, REACH)

    def test_every_point_of_a_slice_falls_in_its_window_and_depth(self):
        rng = random.Random(3)
        for cascade in self.cascades():
            for _ in range(500):
                d = rng.uniform(cascade.near, cascade.far)
                p = frustum_point(FORWARD, d, rng.uniform(-1, 1), rng.uniform(-1, 1))
                x, y, z, w = shadows.transform(cascade.matrix, p)
                self.assertEqual(w, 1.0)
                self.assertTrue(-1.0 <= x <= 1.0 and -1.0 <= y <= 1.0, (cascade.near, x, y))
                self.assertTrue(0.0 <= z <= 1.0, z)

    def test_casters_towards_the_sun_within_reach_are_in_depth_and_nearer(self):
        for cascade in self.cascades():
            p = frustum_point(FORWARD, 0.5 * (cascade.near + cascade.far), 0.0, 0.0)
            z0 = shadows.transform(cascade.matrix, p)[2]
            above = shadows.transform(cascade.matrix, shadows._add(p, shadows._scale(SUN, 0.9 * REACH)))
            self.assertTrue(0.0 <= above[2] < z0)

    def test_depth_follows_the_suns_direction_and_x_y_do_not(self):
        cascade = self.cascades()[2]
        p = frustum_point(FORWARD, 1000.0, 0.2, -0.1)
        a = shadows.transform(cascade.matrix, p)
        b = shadows.transform(cascade.matrix, shadows._add(p, shadows._scale(SUN, 100.0)))
        self.assertAlmostEqual(a[0], b[0], places=9)
        self.assertAlmostEqual(a[1], b[1], places=9)
        self.assertAlmostEqual(a[2] - b[2], 100.0 / (2.0 * (RESOLUTION * cascade.texel / 2.0) + REACH), places=9)

    def test_a_planet_point_keeps_its_place_in_its_texel_as_the_camera_moves(self):
        fixed = shadows._add(CAMERA, frustum_point(FORWARD, 200.0, 0.1, 0.1))   # planet space
        fractions = []
        for step in range(20):
            camera = shadows._add(CAMERA, (step * 0.37, -step * 0.21, step * 0.53))
            cascade = self.cascades(camera=camera)[1]
            x, y, _, _ = shadows.transform(cascade.matrix, shadows._sub(fixed, camera))
            texels = ((x + 1.0) * RESOLUTION / 2.0, (y + 1.0) * RESOLUTION / 2.0)
            fractions.append(tuple(t - math.floor(t) for t in texels))
        for f in fractions:
            self.assertAlmostEqual(f[0], fractions[0][0], delta=1e-4)
            self.assertAlmostEqual(f[1], fractions[0][1], delta=1e-4)

    def test_turning_the_camera_keeps_each_windows_size(self):
        sizes = [c.texel for c in self.cascades()]
        for forward in ((1.0, 0.0, 0.0), (0.0, 0.0, -1.0), shadows._normalize((-0.4, 0.5, 0.6))):
            self.assertEqual([c.texel for c in self.cascades(forward=forward)], sizes)

    def test_the_cascades_grow_with_distance(self):
        texels = [c.texel for c in self.cascades()]
        self.assertTrue(all(b > a for a, b in zip(texels, texels[1:])))


if __name__ == "__main__":
    unittest.main()

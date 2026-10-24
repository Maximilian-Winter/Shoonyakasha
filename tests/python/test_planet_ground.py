"""Tests for the planet example's ground patch
(examples/python/simulations/planet/ground.py), loaded from its file.

    python -m unittest discover -s tests/python
"""

import importlib.util
import math
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
_SPEC = importlib.util.spec_from_file_location(
    "planet_ground", REPO / "examples" / "python" / "simulations" / "planet" / "ground.py")
ground = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(ground)

RADIUS = 100_000.0
CENTRE = (0.36, 0.48, 0.8)          # a unit direction


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def point_at(patch, east, north, height):
    """The point `height` above the radius over the tangent plane's point
    `east`, `north` metres from the patch's centre."""
    on_plane = tuple(patch.centre[k] * RADIUS + patch.east[k] * east + patch.north[k] * north for k in range(3))
    r = math.sqrt(dot(on_plane, on_plane))
    return tuple(c / r * (RADIUS + height) for c in on_plane)


def plane_offsets(patch):
    """(east, north) of each grid direction where it meets the tangent plane."""
    offsets = []
    for d in patch.directions:
        t = RADIUS / dot(d, patch.centre)
        on_plane = tuple(d[k] * t - patch.centre[k] * RADIUS for k in range(3))
        offsets.append((dot(on_plane, patch.east), dot(on_plane, patch.north)))
    return offsets


class Layout(unittest.TestCase):
    def test_the_grid_is_square_around_the_centre(self):
        patch = ground.GroundPatch(RADIUS, CENTRE, 4.0)
        self.assertEqual(len(patch.directions), ground.SIZE * ground.SIZE)
        middle = patch.directions[(ground.SIZE * ground.SIZE) // 2]
        for got, want in zip(middle, CENTRE):
            self.assertAlmostEqual(got, want, places=12)
        offsets = plane_offsets(patch)
        mid = (ground.SIZE - 1) * 0.5
        for index, (east, north) in enumerate(offsets):
            i, j = index % ground.SIZE, index // ground.SIZE
            self.assertAlmostEqual(east, (i - mid) * 4.0, places=6)
            self.assertAlmostEqual(north, (j - mid) * 4.0, places=6)

    def test_east_and_north_make_a_right_handed_frame_with_the_centre(self):
        east, north = ground.tangent_axes(CENTRE)
        up = (east[1] * north[2] - east[2] * north[1], east[2] * north[0] - east[0] * north[2],
              east[0] * north[1] - east[1] * north[0])
        for got, want in zip(up, CENTRE):
            self.assertAlmostEqual(got, want, places=12)


class Query(unittest.TestCase):
    def test_level_ground_gives_the_height_and_the_upward_normal(self):
        patch = ground.GroundPatch(RADIUS, CENTRE, 5.0)
        patch.set_heights([120.0] * (ground.SIZE * ground.SIZE))
        for east, north in ((0.0, 0.0), (3.3, -7.1), (9.9, 9.9)):
            point = point_at(patch, east, north, 130.0)
            depth, normal = patch.query(point)
            self.assertAlmostEqual(depth, -10.0, delta=1e-3)
            radial = tuple(c / math.sqrt(dot(point, point)) for c in point)
            self.assertGreater(dot(normal, radial), 0.99999)

    def test_a_slope_is_interpolated_with_its_normal(self):
        patch = ground.GroundPatch(RADIUS, CENTRE, 5.0)
        slope = 0.3                       # metres up per metre east
        patch.set_heights([1_000.0 + slope * east for east, _ in plane_offsets(patch)])
        expected_normal = tuple((-slope * patch.east[k] + patch.centre[k]) / math.sqrt(1.0 + slope * slope)
                                for k in range(3))
        for east, north in ((0.0, 0.0), (2.5, 1.0), (-6.0, 8.0)):
            point = point_at(patch, east, north, 1_000.0 + slope * east + 2.0)
            depth, normal = patch.query(point)
            self.assertAlmostEqual(depth, -2.0 / math.sqrt(1.0 + slope * slope), delta=2e-3)
            self.assertGreater(dot(normal, expected_normal), 0.99999)

    def test_below_the_surface_is_a_positive_depth(self):
        patch = ground.GroundPatch(RADIUS, CENTRE, 5.0)
        patch.set_heights([50.0] * (ground.SIZE * ground.SIZE))
        depth, _ = patch.query(point_at(patch, 1.0, 1.0, 49.0))
        self.assertAlmostEqual(depth, 1.0, delta=1e-3)

    def test_the_sea_is_a_floor_at_sea_level(self):
        patch = ground.GroundPatch(RADIUS, CENTRE, 5.0)
        patch.set_heights([-300.0] * (ground.SIZE * ground.SIZE))
        depth, _ = patch.query(point_at(patch, 0.0, 0.0, 4.0))
        self.assertAlmostEqual(depth, -4.0, delta=1e-3)

    def test_points_just_outside_take_the_edge_and_far_ones_have_no_answer(self):
        patch = ground.GroundPatch(RADIUS, CENTRE, 5.0)
        patch.set_heights([10.0] * (ground.SIZE * ground.SIZE))
        half = (ground.SIZE - 1) * 0.5 * 5.0
        depth, _ = patch.query(point_at(patch, half + 3.0, 0.0, 12.0))
        self.assertAlmostEqual(depth, -2.0, delta=1e-3)
        self.assertIsNone(patch.query(point_at(patch, 3.0 * half, 0.0, 12.0)))
        self.assertIsNone(patch.query(tuple(-c * RADIUS for c in CENTRE)))


if __name__ == "__main__":
    unittest.main()

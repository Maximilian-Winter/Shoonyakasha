"""Tests for the rivers traced over a grid of heights
(shoonyakasha.planet.rivers), on made-up terrain: a valley falling to a
sea in the south. The grid frame's directions and plane agree; a river
runs down the valley's floor to the sea, its levels never rising, its
width and depth within their bounds and its width growing downstream;
the water crosses a hollow without rising; a tributary ends at the river
it joins; the rivers keep out of the circles to avoid and of the wet
ground; the segments stay within their budget; the packed data and
RiverLookup find the water where the rivers are; and the ribbon has two
vertices per point at the water's edges.

    python -m unittest discover -s tests/python
"""

import math
import unittest
from unittest import mock

import numpy as np

from shoonyakasha.planet import rivers
from shoonyakasha.planet.world import River

RADIUS = 100_000.0
SIZE, SPACING = 96, 200.0
SEA_Y = -7_000.0                 # m: the sea lies south of this


def valley(frame, hollow=False):
    """Heights falling southwards at 1 %, rising 5 % either side of x = 0,
    and below sea level south of SEA_Y; with `hollow`, a pit 15 m deep and
    600 m across on the valley's floor at y = 2 km."""
    o = frame.offsets()
    x, y = np.meshgrid(o, o)
    h = 100.0 + 0.01 * y + 0.05 * np.abs(x)
    h = np.where(y < SEA_Y, -20.0, h)
    if hollow:
        h -= 15.0 * np.clip(1.0 - np.hypot(x, y - 2_000.0) / 300.0, 0.0, 1.0)
    return h


def segment_gap(point, line):
    """Metres from `point` to the nearest segment of the polyline `line` (n, 2)."""
    a, b = line[:-1], line[1:]
    ab = b - a
    t = np.clip(np.einsum("ij,ij->i", point - a, ab) / np.maximum(np.einsum("ij,ij->i", ab, ab), 1e-12), 0.0, 1.0)
    return float(np.min(np.hypot(*(a + ab * t[:, None] - point).T)))


def plane_points(frame, river):
    x, y = frame.to_plane(np.array(river.points))
    return np.stack([x, y], axis=1)


class Frame(unittest.TestCase):
    def test_directions_and_plane_agree(self):
        frame = rivers.GridFrame((0.3, 0.2, 0.9), RADIUS, size=5, spacing=150.0)
        directions = frame.directions()
        self.assertEqual(directions.shape, (25, 3))
        np.testing.assert_allclose(np.linalg.norm(directions, axis=1), 1.0)
        np.testing.assert_allclose(directions[12], frame.centre, atol=1e-12)
        x, y = frame.to_plane(directions)
        o = frame.offsets()
        np.testing.assert_allclose(x.reshape(5, 5), np.tile(o, (5, 1)), atol=1e-6)
        np.testing.assert_allclose(y.reshape(5, 5), np.tile(o[:, None], (1, 5)), atol=1e-6)
        np.testing.assert_allclose(frame.from_plane(x, y), directions, atol=1e-12)


class Trace(unittest.TestCase):
    def setUp(self):
        self.frame = rivers.GridFrame((0.0, 0.0, 1.0), RADIUS, size=SIZE, spacing=SPACING)

    def main_river(self, found):
        return max(found, key=lambda r: max(r.widths))

    def test_a_river_down_the_valley_to_the_sea(self):
        found = rivers.trace(valley(self.frame), self.frame)
        self.assertTrue(found)
        main = self.main_river(found)
        xy = plane_points(self.frame, main)
        reach = sum(w[0] for waves in rivers.MEANDER_WAVES for w in waves) + SPACING
        self.assertLess(float(np.max(np.abs(xy[:, 0]))), reach)
        self.assertLess(float(xy[-1, 1]), SEA_Y + 2.0 * SPACING)
        self.assertGreater(float(xy[0, 1]), float(xy[-1, 1]))
        for river in found:
            self.assertTrue(np.all(np.diff(river.levels) <= 1e-9))
            self.assertGreaterEqual(min(river.levels), 0.0)
            self.assertTrue(all(rivers.WIDTH[0] <= w <= rivers.WIDTH[1] for w in river.widths))
            self.assertTrue(all(rivers.DEPTH[0] <= d <= rivers.DEPTH[1] for d in river.depths))
        self.assertGreater(main.widths[-1], main.widths[0])

    def test_the_water_crosses_a_hollow_without_rising(self):
        found = rivers.trace(valley(self.frame, hollow=True), self.frame)
        main = self.main_river(found)
        xy = plane_points(self.frame, main)
        self.assertTrue(np.any(np.abs(xy[:, 1] - 2_000.0) < SPACING))
        self.assertTrue(np.all(np.diff(main.levels) <= 1e-9))

    def test_a_tributary_ends_at_the_river_it_joins(self):
        # Each strip of the valley's sides drains about 2 km2 by the floor, so a smaller catchment makes tributaries.
        with mock.patch.object(rivers, "MIN_AREA", 1.0e6):
            found = rivers.trace(valley(self.frame), self.frame)
        joined = [r for r in found if plane_points(self.frame, r)[-1, 1] > SEA_Y + 2.0 * SPACING]
        self.assertTrue(joined)
        for river in joined:
            end = plane_points(self.frame, river)[-1]
            gap = min(segment_gap(end, plane_points(self.frame, other)) for other in found if other is not river)
            self.assertLess(gap, 0.5 * rivers.WIDTH[1] + 1.0)

    def test_round_the_circles_to_avoid_and_out_of_the_wet(self):
        centre = tuple(self.frame.from_plane(np.array(0.0), np.array(-2_000.0)))
        found = rivers.trace(valley(self.frame), self.frame, avoid=[(centre, 500.0)])
        for river in found:
            xy = plane_points(self.frame, river)
            self.assertTrue(np.all(np.hypot(xy[:, 0], xy[:, 1] + 2_000.0) > 400.0))
        wet_centre = np.array(self.frame.from_plane(np.array(0.0), np.array(1_000.0)))

        def wet(direction):
            return 50.0 if math.acos(min(1.0, float(np.dot(direction, wet_centre)))) * RADIUS < 700.0 else None
        found = rivers.trace(valley(self.frame), self.frame, wet=wet)
        self.assertTrue(found)
        for river in found:
            self.assertTrue(all(wet(np.array(p)) is None for p in river.points))

    def test_the_segments_stay_within_the_budget(self):
        with mock.patch.object(rivers, "MAX_SEGMENTS", 12):
            found = rivers.trace(valley(self.frame), self.frame)
        self.assertLessEqual(sum(len(r.points) - 1 for r in found), 12)


class Packing(unittest.TestCase):
    def setUp(self):
        self.frame = rivers.GridFrame((0.0, 0.0, 1.0), RADIUS, size=SIZE, spacing=SPACING)
        xs = np.linspace(-3_000.0, 3_000.0, 7)
        points = tuple(tuple(d) for d in self.frame.from_plane(xs, np.full_like(xs, 500.0)))
        self.river = River(points, tuple(np.linspace(80.0, 74.0, 7)), (10.0,) * 7, (2.0,) * 7)

    def test_the_data_and_the_frame(self):
        data, params = rivers.pack([self.river], self.frame, RADIUS)
        self.assertEqual(data.shape, (rivers.MAX_DATA, 4))
        cells = rivers.LOOKUP * rivers.LOOKUP
        first_index, first_segment = int(params[3][0]), int(params[3][1])
        self.assertEqual(first_index, cells)
        self.assertEqual(params[0][3], 6.0)
        self.assertAlmostEqual(params[1][3], SIZE * SPACING / rivers.LOOKUP)
        self.assertEqual(params[2][3], float(rivers.LOOKUP))
        listed = int(data[:cells, 1].sum())
        self.assertGreater(listed, 0)
        np.testing.assert_allclose(data[first_segment, :3], self.river.points[0], atol=1e-7)
        self.assertEqual(data[first_segment, 3], 80.0)
        np.testing.assert_allclose(data[first_segment + 2], (10.0, 10.0, 2.0, 2.0))

    def test_the_lookup_finds_the_water(self):
        lookup = rivers.RiverLookup([self.river], self.frame, RADIUS)
        on = self.frame.from_plane(np.array(0.0), np.array(503.0))
        off = self.frame.from_plane(np.array(0.0), np.array(507.0))
        self.assertAlmostEqual(lookup.level(on), 77.0, delta=0.01)
        self.assertIsNone(lookup.level(off))
        self.assertIsNone(rivers.RiverLookup([], self.frame, RADIUS).level(on))

    def test_the_ribbon(self):
        vertices, indices = rivers.ribbon([self.river], self.frame, RADIUS)
        self.assertEqual(vertices.shape, (14, 8))
        self.assertEqual(len(indices), 6 * 6)
        np.testing.assert_array_equal(vertices[:, 3], np.tile([-1.0, 1.0], 7))
        origin = self.frame.centre * RADIUS
        for k in range(7):
            left, right = vertices[2 * k, :3] + origin, vertices[2 * k + 1, :3] + origin
            self.assertAlmostEqual(float(np.linalg.norm(left - right)), 10.0, delta=1e-2)
            middle = 0.5 * (left + right)
            self.assertAlmostEqual(float(np.linalg.norm(middle)), RADIUS + self.river.levels[k], delta=1e-2)
            np.testing.assert_allclose(vertices[2 * k, 4:7], self.river.points[k], atol=1e-6)
            self.assertEqual(vertices[2 * k, 7], 2.0)


if __name__ == "__main__":
    unittest.main()

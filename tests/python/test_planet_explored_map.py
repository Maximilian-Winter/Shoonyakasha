"""Tests for the walker's explored map
(examples/python/simulations/planet/explored_map.py), loaded from its file:
a grid's cells and their directions, the cells around a point nearest
first, measuring them with the extra probes as the props do (only while
the probes are free, letting them go when asked), the image's pixels and
cells both ways, and the rendered image (unexplored cells, the land and sea
ramps, ore, north up or turned with the heading, the minimap's circle),
contour lines and darkened rows, the full map's labels kept apart and
merged when close, its scale bar, its size on small canvases, and the
contacts' distances and bearings.

    python -m unittest discover -s tests/python
"""

import importlib.util
import math
import sys
import unittest
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[2]
PLANET_DIR = REPO / "examples" / "python" / "simulations" / "planet"
sys.path.insert(0, str(PLANET_DIR))
_SPEC = importlib.util.spec_from_file_location("planet_explored_map", PLANET_DIR / "explored_map.py")
em = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(em)

RADIUS = 100_000.0
UP = (0.0, 0.0, 1.0)                 # east (1, 0, 0), north (0, 1, 0)


class Body:
    def __init__(self, name="planet", ocean=True):
        self.name, self.ocean, self.radius = name, ocean, RADIUS


class World:
    def __init__(self):
        self.planet = Body()
        self.extra_probes, self.extra_heights, self.extra_ores = [], [], []

    def site_frame(self):
        return (0.0, 0.0, RADIUS), UP, (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)

    def answer(self, height=lambda d: 100.0, ore=lambda d: 0.0):
        """The probes' heights back for what was asked."""
        self.extra_heights = [(p, height(p[0])) for p in self.extra_probes]
        self.extra_ores = [(p, ore(p[0])) for p in self.extra_probes]


def small_grid(body=None, size=65):
    return em.Grid(body or Body(), UP, size=size)


class GridTest(unittest.TestCase):
    def test_cells_and_directions_both_ways(self):
        grid = small_grid()
        mid = grid.half
        self.assertEqual(grid.index(UP), (mid, mid))
        d = grid.direction(mid + 3.0, mid - 2.0)
        i, j = grid.index(d)
        self.assertAlmostEqual(i, mid + 3.0, places=6)
        self.assertAlmostEqual(j, mid - 2.0, places=6)
        self.assertGreater(d[0], 0.0)                  # east
        self.assertLess(d[1], 0.0)                     # south
        self.assertAlmostEqual(RADIUS * math.acos(d[2]), math.hypot(3.0, 2.0) * em.CELL, delta=0.5)
        self.assertIsNone(grid.index((0.0, 0.0, -1.0)))

    def test_reveal_offsets_nearest_first_within_reach(self):
        offsets = em.reveal_offsets(100.0, 25.0)
        self.assertEqual(offsets[0], (0, 0))
        distances = [math.hypot(*o) for o in offsets]
        self.assertEqual(distances, sorted(distances))
        self.assertTrue(all(d * 25.0 <= 100.0 for d in distances))
        self.assertIn((4, 0), offsets)
        self.assertNotIn((3, 3), offsets)


class ProbeTest(unittest.TestCase):
    def test_nearest_cells_measured_a_round_at_a_time(self):
        world = World()
        explored = em.ExploredMap(world, size=65, reveal=100.0)
        planet = world.planet
        at = (0.0, 0.0, RADIUS)
        explored.probe(planet, at, 4)
        self.assertEqual(len(world.extra_probes), 4)
        grid = explored.grids["planet"]
        self.assertEqual(grid.measured, 0)
        explored.probe(planet, at, 4)                  # not back yet: the same round
        self.assertEqual(len(grid.pending), 4)
        world.answer(height=lambda d: 50.0, ore=lambda d: 0.5)
        asked = list(world.extra_probes)
        explored.probe(planet, at, 4)
        self.assertEqual(grid.measured, 4)
        self.assertTrue(grid.is_measured(int(grid.half), int(grid.half)))
        self.assertEqual(float(grid.heights[int(grid.half), int(grid.half)]), 50.0)
        self.assertNotEqual(world.extra_probes, asked)  # the next round
        for _ in range(100):
            world.answer()
            explored.probe(planet, at, 4)
        self.assertEqual(grid.measured, len(explored.offsets))
        self.assertEqual(world.extra_probes, [])       # nothing left within reach

    def test_waits_for_others_and_yields(self):
        world = World()
        explored = em.ExploredMap(world, size=65, reveal=100.0)
        planet = world.planet
        theirs = [((1.0, 0.0, 0.0), planet)]
        world.extra_probes = theirs
        explored.probe(planet, (0.0, 0.0, RADIUS), 4)
        self.assertIs(world.extra_probes, theirs)
        world.extra_probes = []
        explored.probe(planet, (0.0, 0.0, RADIUS), 4)
        world.answer()
        explored.probe(planet, (0.0, 0.0, RADIUS), 4, yield_to=True)
        self.assertEqual(world.extra_probes, [])
        self.assertEqual(explored.grids["planet"].measured, 4)


class ImageTest(unittest.TestCase):
    def test_pixels_and_cells_both_ways(self):
        centre = (100.0, 200.0)
        for heading in (None, 0.0, 37.0, 250.0):
            px = em.to_pixel((103.0, 198.5), centre, 400.0, 200, heading)
            back = em.from_pixel(px, centre, 400.0, 200, heading)
            self.assertAlmostEqual(back[0], 103.0, places=6)
            self.assertAlmostEqual(back[1], 198.5, places=6)
        x, y = em.to_pixel((100.0, 201.0), centre, 400.0, 200)          # a cell north: above
        self.assertAlmostEqual(x, 100.0)
        self.assertLess(y, 100.0)
        x, y = em.to_pixel((101.0, 200.0), centre, 400.0, 200, 90.0)    # facing east: east is up
        self.assertAlmostEqual(x, 100.0)
        self.assertLess(y, 100.0)

    def test_render(self):
        grid = small_grid()
        mid = int(grid.half)
        image = em.render(grid, (grid.half, grid.half), 200.0, 40)
        self.assertEqual(image.shape, (40, 40, 4))
        self.assertEqual(image.dtype, np.uint8)
        np.testing.assert_array_equal(image[20, 20], np.round(em.UNEXPLORED * 255.0).astype(np.uint8))
        grid.heights[:] = 200.0
        grid.heights[mid, mid - 10:mid] = -500.0       # sea to the west
        grid.ores[mid + 3, mid] = 0.9                  # ore to the north
        image = em.render(grid, (grid.half, grid.half), 200.0, 40).astype(int)
        land, sea = image[20, 22], image[20, 15]
        self.assertGreater(land[1], land[2])           # green over blue
        self.assertGreater(sea[2], sea[0])             # blue over red
        north = image[12, 20]                          # 3 cells (75 m) north: row 12 at 10 m a pixel
        self.assertGreater(north[0], land[0] + 40)     # turned towards amber
        turned = em.render(grid, (grid.half, grid.half), 200.0, 40, heading=90.0).astype(int)
        self.assertGreater(turned[20, 12][0], land[0] + 40)        # facing east, north is to the left
        circle = em.render(grid, (grid.half, grid.half), 200.0, 40, circle=True)
        self.assertEqual(circle[0, 0, 3], 0)
        self.assertEqual(circle[20, 20, 3], 255)
        moon = em.render(small_grid(Body("moon", ocean=False)), (32.0, 32.0), 200.0, 8)
        self.assertEqual(moon.shape, (8, 8, 4))

    def test_contours_and_scan_rows(self):
        grid = small_grid()
        mid = int(grid.half)
        grid.heights[:] = 100.0
        grid.heights[:, mid:] = 160.0                  # a step of 60 m east of the middle
        plain = em.render(grid, (grid.half, grid.half), 200.0, 40).astype(int)
        lined = em.render(grid, (grid.half, grid.half), 200.0, 40, contour=50.0).astype(int)
        self.assertGreater(lined[20, 17][1], plain[20, 17][1])   # the cell west of the step, 25 m west
        np.testing.assert_array_equal(lined[20, 10], plain[20, 10])   # away from it
        np.testing.assert_array_equal(lined[20, 19], plain[20, 19])   # the cell east of the step
        scanned = em.render(grid, (grid.half, grid.half), 200.0, 40, scan=True).astype(int)
        np.testing.assert_array_equal(scanned[20], plain[20])
        self.assertLess(scanned[21, 10][1], plain[21, 10][1])

    def test_labels_kept_apart(self):
        labels = em.separate([("Base", (400.0, 300.0), None), ("Ship", (405.0, 302.0), None),
                              ("Far", (600.0, 300.0), None)])
        self.assertEqual(labels[0][1], (400.0, 300.0))
        self.assertEqual(labels[1][1], (405.0, 302.0 + em.LABEL_HEIGHT))
        self.assertEqual(labels[2][1], (600.0, 300.0))

    def test_close_labels_merged(self):
        reach = em.MERGE_REACH
        merged = em.merge([("Base", (400.0, 300.0), "a"), ("Ship", (400.0, 300.0 + 0.5 * reach), "b"),
                           ("Far", (400.0 + 2.0 * reach, 300.0), "c")])
        self.assertEqual(merged, [("Base, Ship", (400.0, 300.0), "a"), ("Far", (400.0 + 2.0 * reach, 300.0), "c")])

    def test_scale_bar_fits_a_quarter(self):
        self.assertEqual(em.scale_bar(500.0, 720.0), (250.0, 180.0))
        step, length = em.scale_bar(8_000.0, 720.0)
        self.assertEqual(step, 2_500.0)
        self.assertLessEqual(length, 0.25 * 720.0)


    def test_full_size_fits_the_canvas(self):
        self.assertEqual(em.full_size(1600, 900), em.FULL_MAP)
        small = em.full_size(1280, 720)
        self.assertLess(small, em.FULL_MAP)
        self.assertLessEqual(small + 2 * em.FULL_PAD + em.FULL_HEADER + em.FULL_FOOTER, 720)

    def test_contacts_nearest_first_with_bearings(self):
        found = em.contacts([("Far", (10.0, 0.0), "a"), ("East", (2.0, 0.0), "b"), ("South", (0.0, -1.0), "c")],
                            (0.0, 0.0), cell=25.0)
        self.assertEqual([c[0] for c in found], ["South", "East", "Far"])
        self.assertAlmostEqual(found[0][1], 25.0)
        self.assertAlmostEqual(found[0][2], 180.0)
        self.assertAlmostEqual(found[1][2], 90.0)


if __name__ == "__main__":
    unittest.main()

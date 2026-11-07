"""Tests for the walker's Field Slate style
(examples/python/simulations/planet/fieldslate.py), loaded from its file:
the colours, rounded rectangles (bounds, winding, corners), bands and
strokes, bars, rings, the text helpers and the rolling number.

    python -m unittest discover -s tests/python
"""

import importlib.util
import math
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PLANET_DIR = REPO / "examples" / "python" / "simulations" / "planet"
sys.path.insert(0, str(PLANET_DIR))
_SPEC = importlib.util.spec_from_file_location("planet_fieldslate", PLANET_DIR / "fieldslate.py")
fs = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(fs)

from shoonyakasha.toolkit.drawing import Mesh  # noqa: E402


def signed_area(points):
    """Positive for clockwise on screen (y down)."""
    return 0.5 * sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(points, points[1:] + points[:1]))


class ColourTest(unittest.TestCase):
    def test_hex(self):
        self.assertEqual(fs.hex_colour("#7FE3FF"), (127 / 255, 227 / 255, 1.0, 1.0))
        self.assertEqual(fs.hex_colour("000000", 0.5), (0.0, 0.0, 0.0, 0.5))

    def test_blend_alpha_matches_an_srgb_blend(self):
        for colour, alpha, ground in ((fs.ICE, 0.035, fs.ON_SLATE), (fs.SLATE, 0.88, fs.ON_WORLD),
                                      (fs.AMBER, 0.16, fs.ON_SLATE)):
            a = fs.blend_alpha(colour, alpha, ground)
            linear = wanted = 0.0
            for c, g, w in zip(colour[:3], ground[:3], fs.LUMINANCE):
                linear += w * (fs.to_linear(g) + a * (fs.to_linear(c) - fs.to_linear(g)))
                wanted += w * fs.to_linear(g + alpha * (c - g))
            self.assertAlmostEqual(linear, wanted, delta=0.1 * wanted)       # the luminance within 10 %
        self.assertLess(fs.blend_alpha(fs.ICE, 0.035, fs.ON_SLATE), 0.035)     # light over dark: less
        self.assertGreater(fs.GROUND[3], 0.88)                                 # dark over light: more
        self.assertEqual(fs.blend_alpha(fs.ICE, 0.3, fs.ICE), 0.3)             # nothing to blend

    def test_faces_registered(self):
        for face in fs.FACES:
            self.assertIn(face, fs.scifi.FONTS)
            self.assertIn(face, fs.scifi.ESTIMATE)


class RoundedRectTest(unittest.TestCase):
    def test_bounds_and_winding(self):
        points = fs.rounded_rect(10.0, 20.0, 200.0, 80.0, 14.0)
        xs, ys = [p[0] for p in points], [p[1] for p in points]
        self.assertAlmostEqual(min(xs), 10.0)
        self.assertAlmostEqual(max(xs), 210.0)
        self.assertAlmostEqual(min(ys), 20.0)
        self.assertAlmostEqual(max(ys), 100.0)
        self.assertGreater(signed_area(points), 0.0)
        self.assertLess(signed_area(points), 200.0 * 80.0)

    def test_corners_are_on_their_arcs(self):
        r = 14.0
        for x, y in fs.rounded_rect(0.0, 0.0, 100.0, 50.0, r):
            cx = min(max(x, r), 100.0 - r)
            cy = min(max(y, r), 50.0 - r)
            if (cx, cy) != (x, y):
                self.assertAlmostEqual(math.hypot(x - cx, y - cy), r, places=6)

    def test_radius_clamped_to_half_the_sides(self):
        points = fs.rounded_rect(0.0, 0.0, 60.0, 20.0, 50.0)
        self.assertAlmostEqual(min(p[1] for p in points), 0.0)
        self.assertAlmostEqual(max(p[1] for p in points), 20.0)

    def test_zero_radius_is_a_rectangle(self):
        self.assertEqual(fs.rounded_rect(1.0, 2.0, 3.0, 4.0, 0.0), [(1.0, 2.0), (4.0, 2.0), (4.0, 6.0), (1.0, 6.0)])


class ShapeTest(unittest.TestCase):
    def test_band_goes_outwards(self):
        mesh = Mesh()
        square = fs.rounded_rect(0.0, 0.0, 10.0, 10.0, 0.0)
        fs.band(mesh, square, (1, 1, 1, 1), (1, 1, 1, 0), 5.0)
        xs = [p[0] for p in mesh.positions]
        self.assertAlmostEqual(min(xs), -5.0)
        self.assertAlmostEqual(max(xs), 15.0)

    def test_stroke_straddles_the_edge(self):
        mesh = Mesh()
        fs.stroke(mesh, fs.rounded_rect(0.0, 0.0, 10.0, 10.0, 0.0), (1, 1, 1, 1), 2.0)
        opaque = [p for p, c in zip(mesh.positions, mesh.colors) if c[3] == 1.0]
        self.assertAlmostEqual(min(p[0] for p in opaque), -1.0)
        self.assertAlmostEqual(max(p[0] for p in opaque), 11.0)

    def test_bar_lights_its_share(self):
        mesh = Mesh()
        self.assertAlmostEqual(fs.bar(mesh, 0.0, 0.0, 200.0, 0.25), 50.0)
        self.assertAlmostEqual(fs.bar(mesh, 0.0, 0.0, 200.0, 2.0), 200.0)
        self.assertEqual(fs.bar(Mesh(), 0.0, 0.0, 200.0, 0.0, track=None), 0.0)

    def test_ring_lit_clockwise_from_the_top(self):
        mesh = Mesh()
        fs.ring(mesh, 0.0, 0.0, 20.0, 0.25, colour=(1, 0, 0, 1), track=None)
        lit = [p for p, c in zip(mesh.positions, mesh.colors) if c[3] == 1.0]
        self.assertGreaterEqual(min(p[0] for p in lit), -1e-6)      # the top right quarter only
        self.assertLessEqual(max(p[1] for p in lit), 1e-6)
        self.assertAlmostEqual(max(math.hypot(*p) for p in lit), 21.5)


class TextTest(unittest.TestCase):
    def test_numbers(self):
        self.assertEqual(fs.grouped(1430), "1,430")
        self.assertEqual(fs.grouped(5.854, 2), "5.85")
        self.assertEqual(fs.credits(1999.6), "2,000 cr")
        self.assertEqual(fs.mass(1500), "1,500 kg")

    def test_distances(self):
        self.assertEqual(fs.distance(640.2), "640 m")
        self.assertEqual(fs.distance(2_400.0), "2.4 km")
        self.assertEqual(fs.distance(75_300.0), "75 km")
        self.assertEqual(fs.distance(12_345_000.0), "12,345 km")

    def test_widths_grow_with_content(self):
        self.assertGreater(fs.pill_width("High demand"), fs.pill_width("Rep"))
        self.assertGreater(fs.button_width("Sell cargo", "1,430 cr"), fs.button_width("Sell cargo"))
        self.assertGreaterEqual(fs.keycap_width("E"), fs.CAP_HEIGHT)
        self.assertGreater(fs.hints_width([(("Tab",), "Slate"), (("M",), "Map")]), fs.hint_width((("Tab",), "Slate")))


class RollTest(unittest.TestCase):
    def test_rolls_to_its_target(self):
        roll = fs.Roll(100.0)
        self.assertEqual(roll.update(100.0, 0.1), 100.0)
        first = roll.update(200.0, 0.1)
        self.assertGreater(first, 100.0)
        self.assertLess(first, 200.0)
        self.assertEqual(roll.update(200.0, fs.ROLL_SECONDS), 200.0)

    def test_retarget_starts_from_what_is_shown(self):
        roll = fs.Roll(0.0)
        shown = roll.update(100.0, 0.5 * fs.ROLL_SECONDS)
        self.assertLess(roll.update(0.0, 0.0), shown + 1e-9)
        self.assertEqual(roll.update(0.0, fs.ROLL_SECONDS), 0.0)


if __name__ == "__main__":
    unittest.main()

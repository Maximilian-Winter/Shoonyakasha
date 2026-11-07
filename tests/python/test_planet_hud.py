"""Tests for the walker's head-up display
(examples/python/simulations/planet/hud.py), loaded from its file: bearings
along the ground, wrapping, the compass strip's positions and ticks, the
markers (nearest first, at most MAX_MARKERS, beyond the span held at the
end), their label rows, the distances' text, the gun's key hints and the
status under the crosshair.

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
_SPEC = importlib.util.spec_from_file_location("planet_hud", PLANET_DIR / "hud.py")
hud = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(hud)

UP = (0.0, 0.0, 1.0)                 # east (1, 0, 0), north (0, 1, 0) here
RADIUS = 100_000.0
CENTRE = hud.REFERENCE_SIZE[0] * 0.5


def on_ground(bearing_deg, metres):
    """A point on the sphere `metres` along the ground from the top, at `bearing_deg`."""
    a = metres / RADIUS
    b = math.radians(bearing_deg)
    return (RADIUS * math.sin(a) * math.sin(b), RADIUS * math.sin(a) * math.cos(b), RADIUS * math.cos(a))


class BearingTest(unittest.TestCase):
    def test_bearings_clockwise_from_north(self):
        self.assertAlmostEqual(hud.bearing(UP, (0.0, 1.0, 0.0)), 0.0)
        self.assertAlmostEqual(hud.bearing(UP, (1.0, 0.0, 0.0)), 90.0)
        self.assertAlmostEqual(hud.bearing(UP, (0.0, -1.0, 0.3)), 180.0)
        self.assertAlmostEqual(hud.bearing(UP, (-1.0, 0.0, -0.5)), 270.0)
        self.assertIsNone(hud.bearing(UP, (0.0, 0.0, 2.0)))

    def test_relative_wraps_to_half_a_turn(self):
        self.assertAlmostEqual(hud.relative(10.0, 350.0), 20.0)
        self.assertAlmostEqual(hud.relative(350.0, 10.0), -20.0)
        self.assertAlmostEqual(hud.relative(180.0, 0.0), -180.0)

    def test_ground_distance(self):
        self.assertAlmostEqual(hud.ground_distance(RADIUS, UP, hud.normalize(on_ground(30.0, 2_500.0))), 2_500.0,
                               places=3)


class CompassTest(unittest.TestCase):
    def test_strip_positions_and_clamping(self):
        half = 0.5 * hud.COMPASS_WIDTH
        self.assertEqual(hud.compass_x(0.0), (CENTRE, True))
        self.assertEqual(hud.compass_x(hud.COMPASS_SPAN), (CENTRE + half, True))
        self.assertEqual(hud.compass_x(-90.0), (CENTRE - half, False))

    def test_ticks_every_five_degrees_labelled_every_45(self):
        ticks = hud.compass_ticks(10.0)
        offsets = [t[0] for t in ticks]
        self.assertEqual(offsets, [float(a - 10) for a in range(-50, 71, 5)])
        labelled = {t[2]: t[0] for t in ticks if t[2]}
        self.assertEqual(labelled, {"NW": -55.0, "N": -10.0, "NE": 35.0})
        sizes = {t[0] + 10.0: t[1] for t in ticks}
        self.assertEqual((sizes[0.0], sizes[15.0], sizes[5.0]), (1.0, 0.6, 0.35))
        self.assertEqual({t[2] for t in hud.compass_ticks(180.0) if t[2]}, {"SE", "S", "SW"})

    def test_markers_nearest_first_with_bearings_and_distances(self):
        eye = (0.0, 0.0, RADIUS + 1.7)
        targets = [("far", on_ground(20.0, 9_000.0), (1, 1, 1, 1)),
                   ("near", on_ground(-10.0, 300.0), (1, 1, 1, 1)),
                   ("behind", on_ground(180.0, 1_000.0), (1, 1, 1, 1))]
        shown = hud.markers(eye, UP, 0.0, RADIUS, targets)
        self.assertEqual([m[2] for m in shown], ["near", "behind", "far"])
        near, behind, far = shown
        self.assertAlmostEqual(near[0], hud.compass_x(-10.0)[0], places=3)
        self.assertTrue(near[1])
        self.assertAlmostEqual(near[3], 300.0, delta=0.1)
        self.assertFalse(behind[1])
        self.assertAlmostEqual(far[0], hud.compass_x(20.0)[0], places=3)
        many = [("m%d" % k, on_ground(0.0, 100.0 * (k + 1)), (1, 1, 1, 1)) for k in range(12)]
        self.assertEqual(len(hud.markers(eye, UP, 0.0, RADIUS, many)), hud.MAX_MARKERS)

    def test_label_rows_keep_their_gap(self):
        gap = hud.LABEL_GAP
        xs = [800.0, 800.0 + gap * 0.5, 800.0 + gap * 0.2, None, 800.0 + gap]
        self.assertEqual(hud.label_rows(xs, rows=2), [0, 1, None, None, 0])
        self.assertEqual(hud.label_rows(xs), [0, None, None, None, 0])      # the HUD's one row


class LayoutTest(unittest.TestCase):
    def test_due(self):
        self.assertEqual(hud.due_text(38.4), "1 d 14 h")
        self.assertEqual(hud.due_text(6.9), "6 h")
        self.assertEqual(hud.due_text(0.5), "30 min")

    def test_ladder_steps_evenly_between_its_marks(self):
        top, bottom = 100.0, 400.0
        step = (bottom - top) / (len(hud.LADDER) - 1)
        for k, mark in enumerate(hud.LADDER):
            self.assertAlmostEqual(hud.ladder_y(mark, top, bottom), bottom - k * step)
        self.assertAlmostEqual(hud.ladder_y(65.0, top, bottom), bottom - 1.5 * step)
        self.assertEqual(hud.ladder_y(0.0, top, bottom), bottom)
        self.assertEqual(hud.ladder_y(5_000.0, top, bottom), top)

    def test_project(self):
        view = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))
        self.assertEqual(hud.project((0.0, 0.0, 5.0), view, 90.0, 1600.0, 900.0), (800.0, 450.0))
        x, y = hud.project((1.0, 1.0, 1.0), view, 90.0, 1600.0, 900.0)      # 45 degrees up: the top edge
        self.assertAlmostEqual(y, 0.0)
        self.assertAlmostEqual(x, 800.0 + 450.0)
        self.assertIsNone(hud.project((0.0, 0.0, -1.0), view, 90.0, 1600.0, 900.0))

    def test_wait(self):
        self.assertEqual(hud.wait_text(252.0), "04:12")
        self.assertEqual(hud.wait_text(4_000.0), ">1 h")
        self.assertEqual(hud.wait_text(math.inf), "--:--")

    def test_fit(self):
        self.assertEqual(hud.fit("Short", 500.0), "Short")
        long = "A very long line of text that cannot fit in a small width at all"
        cut = hud.fit(long, 120.0)
        self.assertTrue(cut.endswith("…"))
        self.assertLessEqual(hud.fs.text_width(cut, "body"), 120.0)


class TextTest(unittest.TestCase):
    def test_distances(self):
        self.assertEqual(hud.distance_text(15.4), "15 m")
        self.assertEqual(hud.distance_text(72_940.0), "72.9 km")

    def test_hints(self):
        self.assertEqual(hud.hints("idle", False, 0.0), [])
        self.assertEqual(hud.hints("too_heavy", False, 0.0), [])
        self.assertEqual(hud.hints("too_hot", False, 0.0), [])
        self.assertEqual(hud.hints("can_grab", False, 0.0), [(("RMB",), "Grab")])
        self.assertEqual(hud.hints("can_grab", False, 0.0, pad=True), [(("pad:LT",), "Grab")])
        held = hud.hints("holding", True, 0.0)
        self.assertEqual([h[0] for h in held], [("RMB",), ("LMB",), ("R",), ("Wheel",)])
        pad = hud.hints("holding", True, 0.0, pad=True)
        self.assertEqual([h[0] for h in pad], [("pad:LT",), ("pad:RT",), ("pad:LB", "pad:RB")])
        self.assertEqual(hud.hints("holding", True, 0.5), [(("LMB",), "Release to throw")])

    def test_status(self):
        self.assertEqual(hud.status_text("idle", False, 0.0), (None, None))
        self.assertEqual(hud.status_text("too_heavy", False, 0.0), ("TOO HEAVY", hud.WARNING))
        self.assertEqual(hud.status_text("too_hot", False, 0.0), ("TOO HOT", hud.WARNING))
        self.assertEqual(hud.status_text("holding", True, 0.5, 3.0), ("CHARGE 50 %", hud.ACCENT))
        self.assertEqual(hud.status_text("holding", True, 0.0, 3.0), ("HOLD 3.0 M", hud.TEXT))
        self.assertEqual(hud.status_text("holding", True, 0.0), (None, None))


if __name__ == "__main__":
    unittest.main()

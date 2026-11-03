"""Tests for the walker's head-up display
(examples/python/simulations/planet/hud.py), loaded from its file: bearings
along the ground, wrapping, the compass strip's positions and ticks, the
markers (nearest first, at most MAX_MARKERS, beyond the span held at the
end), their label rows, the distances' text and the gun's prompts.

    python -m unittest discover -s tests/python
"""

import importlib.util
import math
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
_SPEC = importlib.util.spec_from_file_location(
    "planet_hud", REPO / "examples" / "python" / "simulations" / "planet" / "hud.py")
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
        self.assertEqual(hud.label_rows([800.0, 800.0 + gap * 0.5, 800.0 + gap * 0.2, None, 800.0 + gap]),
                         [0, 1, None, None, 0])


class TextTest(unittest.TestCase):
    def test_distances(self):
        self.assertEqual(hud.distance_text(15.4), "15 m")
        self.assertEqual(hud.distance_text(72_940.0), "72.9 km")

    def test_prompts(self):
        self.assertEqual(hud.prompt("idle", False, 0.0), "")
        self.assertEqual(hud.prompt("can_grab", False, 0.0), "RMB  grab")
        self.assertEqual(hud.prompt("can_grab", False, 0.0, pad=True), "LT  grab")
        self.assertEqual(hud.prompt("too_heavy", False, 0.0), "too heavy")
        self.assertIn("R + mouse  turn", hud.prompt("holding", True, 0.0))
        self.assertNotIn("turn", hud.prompt("holding", True, 0.0, pad=True))
        self.assertEqual(hud.prompt("holding", True, 0.5), "LMB  release to throw  50 %")


if __name__ == "__main__":
    unittest.main()

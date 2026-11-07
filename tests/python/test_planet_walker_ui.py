"""Tests for the walker's interface style and screens
(examples/python/simulations/planet/scifi.py and slate.py): panels' cut
corners, segmented bars, wrapping, key hints' widths and drawing, and the
items' sliders, choices and pointer (a click does the region under it,
the topmost first; a held button clicks once; a slider dragged while the
button is held). Plain Python: no engine needed.

    python -m unittest discover -s tests/python
"""

import sys
import unittest
from pathlib import Path

PLANET_DIR = Path(__file__).resolve().parents[2] / "examples" / "python" / "simulations" / "planet"
sys.path.insert(0, str(PLANET_DIR))

import scifi  # noqa: E402
import slate  # noqa: E402
from shoonyakasha.toolkit.drawing import Mesh  # noqa: E402


class FakeLayer:
    def __init__(self):
        self.mesh = Mesh()
        self.texts = []

    def text(self, text, at, size, colour, face="body", align="left", shadow=True):
        self.texts.append((text, at, face, align))
        return scifi.measure.width(text, size, face)


class ShapesTest(unittest.TestCase):
    def test_cut_corners(self):
        self.assertEqual(scifi.cut_rect(0, 0, 100, 50, 10, ()), [(0, 0), (100, 0), (100, 50), (0, 50)])
        points = scifi.cut_rect(0, 0, 100, 50, 10, ("tl", "br"))
        self.assertEqual(points, [(0, 10), (10, 0), (100, 0), (100, 40), (90, 50), (0, 50)])
        self.assertEqual(len(scifi.cut_rect(0, 0, 100, 50, 10, ("tl", "tr", "br", "bl"))), 8)
        self.assertEqual(scifi.cut_rect(0, 0, 10, 10, 30, ("tl",))[:2], [(0, 5), (5, 0)])   # at most half

    def test_segments_lit(self):
        mesh = Mesh()
        self.assertEqual(scifi.segments(mesh, 0, 0, 100, 8, 10, 0.34, (1, 1, 1, 1), (0, 0, 0, 1)), 3)
        self.assertEqual(len(mesh.indices), 10 * 6)
        self.assertEqual(scifi.segments(Mesh(), 0, 0, 100, 8, 10, 1.5, (1, 1, 1, 1), (0, 0, 0, 1)), 10)

    def test_wrap(self):
        lines = scifi.wrap("one two three four five six seven", 16, scifi.measure.width("one two three", 16) + 1)
        self.assertEqual(lines[0], "one two three")
        self.assertEqual(" ".join(lines), "one two three four five six seven")
        self.assertEqual(scifi.wrap("", 16, 100), [])

    def test_hints_measured_as_drawn(self):
        hints = [(("RMB",), "Drop"), (("pad:LB", "pad:RB"), "Distance")]
        layer = FakeLayer()
        width = scifi.caps(layer, hints, 10.0, 50.0)
        self.assertAlmostEqual(width, scifi.hints_width(hints))
        shown = [t[0] for t in layer.texts]
        self.assertEqual(shown, ["RMB", "Drop", "LB", "RB", "Distance"])
        self.assertEqual(scifi.cap_text("pad:A"), ("A", True))


class MenuValuesTest(unittest.TestCase):
    def test_slider_values_snap_and_clamp(self):
        item = {"slider": "S", "min": 0.25, "max": 3.0, "step": 0.05}
        self.assertAlmostEqual(slate.slider_value(item, 0.5), 1.65)
        self.assertEqual(slate.slider_value(item, -1.0), 0.25)
        self.assertEqual(slate.slider_value(item, 2.0), 3.0)
        self.assertAlmostEqual(slate.slider_value({"min": 0.0, "max": 10.0}, 0.33), 3.3)

    def test_options_wrap(self):
        options = [(1, "a"), (2, "b"), (3, "c")]
        self.assertEqual(slate.next_option(options, 3, 1), 1)
        self.assertEqual(slate.next_option(options, 1, -1), 3)
        self.assertEqual(slate.next_option(options, 9, 1), 1)

    def test_value_text(self):
        self.assertEqual(slate.value_text({"format": "%.1f"}, 2.25), "2.2")
        self.assertEqual(slate.value_text({"show": lambda v: "%d %%" % (100 * v)}, 0.5), "50 %")


class Input:
    def __init__(self, down):
        self.down = down

    def is_mouse_button_down(self, button):
        return self.down


class Layer:
    def end(self):
        pass


class PointerTest(unittest.TestCase):
    def setUp(self):
        surface = self.surface = slate.Surface.__new__(slate.Surface)
        surface.layer, surface.drag, surface.was_down, surface.point = Layer(), None, False, None
        self.values = {"s": 0.0}
        self.done = []
        self.slider = {"slider": "S", "min": 0.0, "max": 1.0, "get": lambda: self.values["s"],
                       "set": lambda v: self.values.update(s=v)}

        def grab():
            surface.drag = (self.slider, 100.0, 200.0)
            self.slider["set"](slate.slider_value(self.slider, (surface.point[0] - 100.0) / 200.0))
        self.regions = [(0, 0, 100, 20, lambda: self.done.append("under")),
                        (0, 0, 50, 20, lambda: self.done.append("over")),
                        (100, 60, 200, 20, grab)]

    def frame(self, point, down):
        """One frame: the regions drawn, the pointer at `point`, the button `down`."""
        self.surface.regions = list(self.regions)
        self.surface.point = point
        self.surface.finish(Input(down))

    def test_a_click_does_the_topmost_region(self):
        self.frame((20, 10), True)
        self.frame((20, 10), False)
        self.assertEqual(self.done, ["over"])
        self.frame((80, 10), True)
        self.assertEqual(self.done, ["over", "under"])
        self.frame((80, 50), False)
        self.frame((80, 50), True)                       # over nothing
        self.assertEqual(self.done, ["over", "under"])

    def test_a_held_button_clicks_once(self):
        for _ in range(3):
            self.frame((80, 10), True)
        self.assertEqual(self.done, ["under"])

    def test_slider_follows_while_held(self):
        self.frame((150, 70), True)
        self.assertAlmostEqual(self.values["s"], 0.25)
        self.frame((250, 400), True)                     # dragged off the row, still held
        self.assertAlmostEqual(self.values["s"], 0.75)
        self.frame((250, 400), False)
        self.assertIsNone(self.surface.drag)
        self.frame((50, 400), True)                      # over nothing: no change
        self.assertAlmostEqual(self.values["s"], 0.75)


if __name__ == "__main__":
    unittest.main()

"""Tests for the walker's interface style and menu
(examples/python/simulations/planet/scifi.py and terminal.py): panels'
cut corners, segmented bars, wrapping, key hints' widths and drawing, and
the menu's sliders, choices and pointer (clicks on buttons, toggles,
choices and tabs, a slider dragged while the button is held). Plain
Python: no engine needed.

    python -m unittest discover -s tests/python
"""

import sys
import unittest
from pathlib import Path

PLANET_DIR = Path(__file__).resolve().parents[2] / "examples" / "python" / "simulations" / "planet"
sys.path.insert(0, str(PLANET_DIR))

import scifi  # noqa: E402
import terminal  # noqa: E402
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
        self.assertAlmostEqual(terminal.slider_value(item, 0.5), 1.65)
        self.assertEqual(terminal.slider_value(item, -1.0), 0.25)
        self.assertEqual(terminal.slider_value(item, 2.0), 3.0)
        self.assertAlmostEqual(terminal.slider_value({"min": 0.0, "max": 10.0}, 0.33), 3.3)

    def test_options_wrap(self):
        options = [(1, "a"), (2, "b"), (3, "c")]
        self.assertEqual(terminal.next_option(options, 3, 1), 1)
        self.assertEqual(terminal.next_option(options, 1, -1), 3)
        self.assertEqual(terminal.next_option(options, 9, 1), 1)

    def test_value_text(self):
        self.assertEqual(terminal.value_text({"format": "%.1f"}, 2.25), "2.2")
        self.assertEqual(terminal.value_text({"show": lambda v: "%d %%" % (100 * v)}, 0.5), "50 %")


class PointerTest(unittest.TestCase):
    def setUp(self):
        self.menu = terminal.Terminal.__new__(terminal.Terminal)
        self.menu.regions, self.menu.hover, self.menu.drag, self.menu.was_down, self.menu.tab = [], None, None, False, 0
        self.values = {"on": False, "s": 0.0, "c": 1}
        self.done = []
        self.button = {"button": "Go", "do": lambda: self.done.append(1)}
        self.toggle = {"toggle": "T", "get": lambda: self.values["on"], "set": lambda v: self.values.update(on=v)}
        self.slider = {"slider": "S", "min": 0.0, "max": 1.0, "get": lambda: self.values["s"],
                       "set": lambda v: self.values.update(s=v)}
        self.choice = {"choice": "C", "options": [(1, "a"), (2, "b")], "get": lambda: self.values["c"],
                       "set": lambda v: self.values.update(c=v)}
        self.menu.regions = [(0, 0, 100, 20, self.button, "item"), (0, 30, 100, 20, self.toggle, "item"),
                             (0, 60, 300, 20, self.slider, "label"), (100, 60, 200, 20, self.slider, "track"),
                             (0, 90, 100, 20, self.choice, "item"), (0, 90, 20, 20, self.choice, "back"),
                             (200, 0, 50, 20, 2, "tab")]

    def click(self, point):
        self.menu.handle(point, True)
        self.menu.handle(point, False)

    def test_clicks(self):
        self.click((50, 10))
        self.assertEqual(self.done, [1])
        self.click((50, 40))
        self.assertTrue(self.values["on"])
        self.click((50, 100))
        self.assertEqual(self.values["c"], 2)
        self.click((10, 100))
        self.assertEqual(self.values["c"], 1)
        self.click((220, 10))
        self.assertEqual(self.menu.tab, 2)

    def test_a_held_button_clicks_once(self):
        for _ in range(3):
            self.menu.handle((50, 10), True)
        self.assertEqual(self.done, [1])

    def test_slider_follows_while_held(self):
        self.menu.handle((150, 70), True)
        self.assertAlmostEqual(self.values["s"], 0.25)
        self.menu.handle((250, 400), True)               # dragged off the row, still held
        self.assertAlmostEqual(self.values["s"], 0.75)
        self.menu.handle((250, 400), False)
        self.assertIsNone(self.menu.drag)
        self.menu.handle((50, 70), True)                 # the label: no change
        self.assertAlmostEqual(self.values["s"], 0.75)


if __name__ == "__main__":
    unittest.main()

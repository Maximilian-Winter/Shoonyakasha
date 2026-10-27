"""Tests for the key caps and text wrapping of shoonyakasha.toolkit.overlay.

    python -m unittest discover -s tests/python
"""

import unittest

from shoonyakasha.toolkit import overlay


class CapTest(unittest.TestCase):
    def test_pad_prefix(self):
        self.assertEqual(overlay.cap_text("pad:A"), ("A", True))
        self.assertEqual(overlay.cap_text("A"), ("A", False))
        self.assertEqual(overlay.cap_width("pad:Start"), overlay.cap_width("Start"))

    def test_caps_at_least_square(self):
        self.assertEqual(overlay.cap_width("I"), overlay.CAP_HEIGHT)
        self.assertGreater(overlay.cap_width("Backspace"), overlay.CAP_HEIGHT)

    def test_row_width_adds_up(self):
        one = [(("W", "S"), "Throttle")]
        two = one + [(("Tab",), "Free camera")]
        expected = overlay.hint_width(one[0]) + overlay.HINT_GAP + overlay.hint_width(two[1])
        self.assertAlmostEqual(overlay.row_width(two), expected)
        self.assertEqual(overlay.row_width([]), 0.0)

    def test_label_widens_a_hint(self):
        self.assertGreater(overlay.hint_width((("F1",), "Settings")), overlay.hint_width((("F1",), "")))


class WrapTest(unittest.TestCase):
    TEXT = ("A slow neutron drifts toward a uranium nucleus. Nothing pushes it away: "
            "it has no charge, so the protons' repulsion does not reach it.")

    def test_lines_fit_and_keep_every_word(self):
        lines = overlay.wrap_text(self.TEXT, 16, 200)
        self.assertGreater(len(lines), 1)
        for line in lines:
            self.assertLessEqual(overlay.measure.width(line, 16), 200)
        self.assertEqual(" ".join(lines).split(), self.TEXT.split())

    def test_wider_means_fewer_lines(self):
        self.assertLess(len(overlay.wrap_text(self.TEXT, 16, 400)), len(overlay.wrap_text(self.TEXT, 16, 200)))

    def test_line_breaks_and_long_words(self):
        self.assertEqual(overlay.wrap_text("one\ntwo", 16, 500), ["one", "two"])
        self.assertEqual(overlay.wrap_text("a " + "x" * 80 + " b", 16, 100), ["a", "x" * 80, "b"])
        self.assertEqual(overlay.wrap_text("", 16, 100), [""])


if __name__ == "__main__":
    unittest.main()

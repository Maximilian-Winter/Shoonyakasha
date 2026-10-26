"""Tests for the key caps of shoonyakasha.toolkit.overlay.

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


if __name__ == "__main__":
    unittest.main()

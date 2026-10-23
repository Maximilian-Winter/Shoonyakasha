"""
Tests for shoonyakasha.cookies, the default pipeline's spot light cookie
atlas: where each tile goes, and how tiles are converted.

Run from the repository root:  python -m pytest tests/python
"""

import sys
import unittest
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "python"))

from shoonyakasha import cookies  # noqa: E402


class Atlas(unittest.TestCase):
    def test_tiles_fill_the_grid_left_to_right_top_to_bottom(self):
        red = np.zeros((8, 8, 3), np.uint8)
        red[..., 0] = 255
        blue = np.zeros((8, 8, 3), np.uint8)
        blue[..., 2] = 255
        out = cookies.atlas([red, blue] + [red] * 3 + [blue], tile_size=8)
        self.assertEqual(out.shape, (32, 32, 4))
        self.assertEqual(out.dtype, np.uint8)
        self.assertEqual(tuple(out[0, 0]), (255, 0, 0, 255))     # tile 1
        self.assertEqual(tuple(out[0, 8]), (0, 0, 255, 255))     # tile 2, right of it
        self.assertEqual(tuple(out[8, 8]), (0, 0, 255, 255))     # tile 6, second row
        self.assertEqual(tuple(out[31, 31]), (255, 255, 255, 255))   # not given: white

    def test_grey_and_float_tiles_and_resampling(self):
        grey = np.array([[0, 255], [255, 0]], np.uint8)   # 2 x 2, resampled to 4 x 4
        half = np.full((3, 3), 0.5)
        out = cookies.atlas([grey, half], tile_size=4)
        self.assertEqual(tuple(out[0, 0]), (0, 0, 0, 255))
        self.assertEqual(tuple(out[0, 3]), (255, 255, 255, 255))
        self.assertEqual(tuple(out[3, 0]), (255, 255, 255, 255))
        self.assertEqual(tuple(out[0, 4]), (128, 128, 128, 255))

    def test_alpha_is_kept(self):
        tile = np.zeros((2, 2, 4), np.uint8)
        tile[..., 3] = 7
        self.assertEqual(cookies.atlas([tile], tile_size=2)[0, 0, 3], 7)

    def test_too_many_or_misshapen_tiles_raise(self):
        with self.assertRaises(ValueError):
            cookies.atlas([np.zeros((2, 2))] * 17, tile_size=2)
        with self.assertRaises(ValueError):
            cookies.atlas([np.zeros((2, 2, 2))], tile_size=2)


if __name__ == "__main__":
    unittest.main()

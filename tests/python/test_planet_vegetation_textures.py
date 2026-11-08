"""Tests for the vegetation's textures and impostors of shoonyakasha.planet
(vegetation_textures.py, vegetation_impostors.py), with the leaves, needles
and blades drawn there rather than the downloaded scans: the regions of a
mask are found with their boxes, the atlas has every card in its cell with
some but not all of it covered, its leaves' mean near CARD_MEAN and its
transparent texels the mean, the cards' rects are distinct cells inside
the atlas, the bark tile is opaque, and the impostors cover part of each
cell, are bark at the foot of the trunk and foliage in the crown, with
unit normals facing the viewer and their rects in the atlas. Needs Pillow;
skipped without it.

    python -m unittest discover -s tests/python
"""

import unittest

import numpy as np

from shoonyakasha.planet import vegetation, vegetation_impostors, vegetation_textures as vt

try:
    import PIL  # noqa: F401
    HAVE_PIL = True
except ImportError:
    HAVE_PIL = False


def _linear(srgb):
    c = srgb / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


class RegionsTest(unittest.TestCase):
    def test_boxes_of_regions(self):
        mask = np.zeros((10, 12), bool)
        mask[1:4, 2:5] = True             # 9 cells
        mask[6:9, 8:10] = True            # 6 cells
        mask[0, 11] = True                # 1 cell: too small
        self.assertEqual(vt._regions(mask, 2), [(1, 2, 4, 5), (6, 8, 9, 10)])


@unittest.skipUnless(HAVE_PIL, "Pillow is needed to bake the textures")
class AtlasTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.atlas = vt.atlas(scanned=False)

    def cell(self, name):
        index = vt.CARDS.index(name)
        y, x = divmod(index, vt.ATLAS_COLUMNS)
        return self.atlas[y * vt.CELL:(y + 1) * vt.CELL, x * vt.CELL:(x + 1) * vt.CELL]

    def test_shape(self):
        rows = (len(vt.CARDS) + vt.ATLAS_COLUMNS - 1) // vt.ATLAS_COLUMNS
        self.assertEqual(self.atlas.shape, (rows * vt.CELL, vt.ATLAS_COLUMNS * vt.CELL, 4))
        self.assertEqual(self.atlas.dtype, np.uint8)

    def test_every_card_partly_covered_with_its_mean_near_card_mean(self):
        for name in vt.CARDS:
            cell = self.cell(name)
            covered = cell[..., 3] > 127
            self.assertGreater(covered.mean(), 0.03, name)
            self.assertLess(covered.mean(), 0.9, name)
            mean = _linear(cell[covered][:, :3].astype(float)).mean(axis=0)
            np.testing.assert_allclose(mean, vt.CARD_MEAN, atol=0.12, err_msg=name)
            clear = _linear(cell[cell[..., 3] == 0][:, :3].astype(float))
            np.testing.assert_allclose(clear, vt.CARD_MEAN, atol=0.01, err_msg=name)

    def test_card_rects_are_distinct_cells(self):
        rects = [vt.card_rect(name) for name in vt.CARDS]
        self.assertEqual(len(set(rects)), len(rects))
        for u0, v0, u1, v1 in rects:
            self.assertTrue(0.0 < u0 < u1 < 1.0 and 0.0 < v0 < v1 < 1.0)

    def test_bark_is_opaque(self):
        bark = vt.bark(scanned=False)
        self.assertEqual(bark.shape, (vt.BARK_SIZE, vt.BARK_SIZE, 4))
        self.assertTrue((bark[..., 3] == 255).all())


@unittest.skipUnless(HAVE_PIL, "Pillow is needed to bake the textures")
class ImpostorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.albedo, cls.normals, cls.rects = vegetation_impostors.bake(
            vegetation.trees(), vt.atlas(scanned=False), vt.bark(scanned=False))

    def cell(self, image, name):
        index = list(vegetation.trees()).index(name)
        y, x = divmod(index, vegetation_impostors.COLUMNS)
        size = vegetation_impostors.CELL
        return image[y * size:(y + 1) * size, x * size:(x + 1) * size]

    def test_cells_cover_part_with_bark_below_and_foliage_above(self):
        size = vegetation_impostors.CELL
        for name in vegetation.near_trees():
            albedo, normals = self.cell(self.albedo, name), self.cell(self.normals, name)
            covered = albedo[..., 3] > 127
            self.assertGreater(covered.mean(), 0.05, name)
            self.assertLess(covered.mean(), 0.8, name)
            foot = (size - 3, size // 2)
            self.assertGreater(albedo[foot + (3,)], 127, name)
            self.assertEqual(normals[foot + (3,)], 0, name)                # bark at the foot of the trunk
            crown = covered & (np.arange(size)[:, None] < size * 0.6)
            self.assertGreater((normals[..., 3][crown] == 255).mean(), 0.7, name)

    def test_normals_are_unit_and_face_the_viewer(self):
        for name in vegetation.near_trees():
            albedo, normals = self.cell(self.albedo, name), self.cell(self.normals, name)
            n = normals[albedo[..., 3] > 200][:, :3] / 255.0 * 2.0 - 1.0
            np.testing.assert_allclose(np.linalg.norm(n, axis=1), 1.0, atol=0.03)
            self.assertGreater(float(n[:, 2].mean()), 0.0, name)

    def test_rects(self):
        self.assertEqual(set(self.rects), set(vegetation.near_trees()))
        for u0, v0, u1, v1 in self.rects.values():
            self.assertTrue(0.0 < u0 < u1 < 1.0 and 0.0 < v0 < v1 < 1.0)


if __name__ == "__main__":
    unittest.main()

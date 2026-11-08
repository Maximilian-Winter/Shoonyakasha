"""Tests for the vegetation's textures and impostors of shoonyakasha.planet
(vegetation_textures.py, vegetation_impostors.py), with the leaves, needles
and blades drawn there rather than the downloaded scans: the regions of a
mask are found with their boxes, the atlas has every card in its cell with
some but not all of it covered, its leaves' mean near CARD_MEAN and its
transparent texels the mean, the cards' rects are distinct cells inside
the atlas, the bark tile is opaque. Needs Pillow; skipped without it. The
octahedral impostors, baked on the GPU (skipped without a Vulkan device):
each tree's block covered in part, its side views bark at the foot of the
trunk and foliage in the crown, its view from above the crown alone
(foliage around the middle, nothing at the corners), unit normals where
covered, their blocks in the atlas; and the hemi-octahedral mapping
(impostor_oct.glsl, mirrored here) taking each view's direction back to
its view.

    python -m unittest discover -s tests/python
"""

import unittest

import numpy as np

import shoonyakasha as sk
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
class TwigCardTest(unittest.TestCase):
    def test_a_twig_seen_from_above(self):
        import os
        import tempfile
        from PIL import Image
        from shoonyakasha.planet import scans
        with tempfile.TemporaryDirectory() as folder:
            texture = np.zeros((8, 8, 4), np.uint8)
            texture[:, :4] = (40, 120, 40, 255)          # the left half opaque, the right clear
            path = os.path.join(folder, "albedo.png")
            Image.fromarray(texture).save(path)
            # One triangle in the x-z plane: its point at z = 1 (the stalk), its wide end at z = 0.
            positions = np.array([[-1.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 0.0, 1.0]], np.float32)
            uvs = np.array([[0.0, 0.0], [1.0, 0.0], [0.0, 1.0]], np.float32)
            scan = scans.Scan(scans.CARD_SCANS[0], positions, np.tile([0.0, 1.0, 0.0], (3, 1)),
                              np.tile([1.0, 0.0, 0.0, 1.0], (3, 1)), uvs, np.zeros(3, np.uint8),
                              np.array([0, 1, 2], np.uint32), np.zeros((0, 3), np.float32),
                              [{"albedo": path, "cutout": True}])
            card = np.asarray(vt.twig_card(scan, 64))
        alpha = card[..., 3] > 127
        self.assertTrue(alpha.any())
        rows = np.nonzero(alpha.any(axis=1))[0]
        # The triangle narrows towards its stalk, at the card's base (the bottom rows).
        self.assertGreater(alpha[rows[0]].sum(), alpha[rows[-1]].sum())
        # Only the texture's opaque half, u below 1/2: where x < z, so at the wide end (z = 0) its left side.
        self.assertGreater(alpha[:, :32].sum(), 1.3 * alpha[:, 32:].sum())
        self.assertGreater(alpha[rows[0], :32].sum(), 3 * alpha[rows[0], 32:].sum())


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
def oct_encode(d):
    d = np.asarray(d, np.float64) / np.abs(d).sum()
    return np.array([d[0] + d[2], d[0] - d[2]]) * 0.5 + 0.5


def oct_decode(uv):
    f = np.asarray(uv, np.float64) * 2.0 - 1.0
    d = np.array([(f[0] + f[1]) * 0.5, 0.0, (f[0] - f[1]) * 0.5])
    d[1] = 1.0 - abs(d[0]) - abs(d[2])
    return d / np.linalg.norm(d)


class OctahedralMappingTest(unittest.TestCase):
    def test_each_views_direction_lands_in_its_view(self):
        n = vegetation_impostors.VIEWS
        for i in range(n):
            for j in range(n):
                d = oct_decode(((i + 0.5) / n, (j + 0.5) / n))
                self.assertGreaterEqual(d[1], 0.0)
                g = oct_encode(d) * n - 0.5
                np.testing.assert_allclose(g, (i, j), atol=1e-9)

    def test_the_shaders_constants(self):
        source = (vegetation.__file__.rsplit("planet", 1)[0] + "pipelines/planet/shaders/impostor_oct.glsl")
        text = open(source, encoding="utf-8").read()
        self.assertIn("const int OCT_VIEWS = %d;" % vegetation_impostors.VIEWS, text)
        self.assertIn("const int OCT_TEXELS = %d;" % vegetation_impostors.TEXELS, text)


class ImpostorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not sk.extension_available():
            raise unittest.SkipTest("the engine's extension is not importable here")
        try:
            ctx = sk.ComputeContext()
        except RuntimeError as exc:
            raise unittest.SkipTest("no Vulkan device: %s" % exc)
        try:
            cls.albedo, cls.normals, cls.rects, cls.spheres = vegetation_impostors.bake(
                vegetation.trees(), vt.atlas(scanned=False), vt.bark(scanned=False), ctx)
        finally:
            ctx.shutdown()

    def view(self, image, name, i, j):
        """View (i, j) of a tree's block: (i along u, j along v)."""
        index = list(vegetation.trees()).index(name)
        y, x = divmod(index, vegetation_impostors.COLUMNS)
        block, texels = vegetation_impostors.BLOCK, vegetation_impostors.TEXELS
        top, left = y * block + j * texels, x * block + i * texels
        return image[top:top + texels, left:left + texels]

    def test_blocks_cover_part_of_their_views(self):
        block = vegetation_impostors.BLOCK
        for name in vegetation.near_trees():
            y, x = divmod(list(vegetation.trees()).index(name), vegetation_impostors.COLUMNS)
            covered = self.albedo[y * block:(y + 1) * block, x * block:(x + 1) * block, 3] > 127
            self.assertGreater(covered.mean(), 0.03, name)
            self.assertLess(covered.mean(), 0.8, name)

    def test_side_views_are_bark_below_and_foliage_above(self):
        n, texels = vegetation_impostors.VIEWS, vegetation_impostors.TEXELS
        for name in vegetation.near_trees():
            centre, radius = self.spheres[name]
            albedo, normals = self.view(self.albedo, name, 0, 0), self.view(self.normals, name, 0, 0)
            # The tree's foot (y = 0) in this side view, through the view's frame (octBasis).
            d = oct_decode((0.5 / n, 0.5 / n))
            right = np.cross((0.0, 1.0, 0.0), d)
            up = np.cross(d, right / np.linalg.norm(right))
            row = int(round((0.5 - 0.5 * np.dot((0.0, -centre, 0.0), up) / radius) * texels))
            foot = albedo[max(row - 4, 0):row, texels // 2 - 3:texels // 2 + 3]
            self.assertGreater(foot[..., 3].max(), 127, name)
            bark = normals[max(row - 4, 0):row, texels // 2 - 3:texels // 2 + 3][..., 3]
            self.assertEqual(bark.min(), 0, name)                  # bark at the foot of the trunk
            covered = albedo[..., 3] > 127
            crown = covered & (np.arange(texels)[:, None] < texels * 0.45)
            self.assertGreater((normals[..., 3][crown] == 255).mean(), 0.6, name)

    def test_from_above_the_crown_alone(self):
        n, texels = vegetation_impostors.VIEWS, vegetation_impostors.TEXELS
        for name in vegetation.near_trees():
            i = j = n // 2
            g = oct_encode((0.0, 1.0, 0.0)) * n - 0.5
            i, j = int(round(g[0])), int(round(g[1]))
            albedo, normals = self.view(self.albedo, name, i, j), self.view(self.normals, name, i, j)
            covered = albedo[..., 3] > 127
            middle = covered[texels // 3:2 * texels // 3, texels // 3:2 * texels // 3]
            self.assertGreater(middle.mean(), 0.3, name)
            for corner in (covered[:8, :8], covered[:8, -8:], covered[-8:, :8], covered[-8:, -8:]):
                self.assertFalse(corner.any(), name)
            self.assertGreater((normals[..., 3][covered] == 255).mean(), 0.6, name)

    def test_normals_are_unit_where_covered(self):
        for name in vegetation.near_trees():
            for i, j in ((0, 0), (3, 3), (5, 2)):
                albedo, normals = self.view(self.albedo, name, i, j), self.view(self.normals, name, i, j)
                n = normals[albedo[..., 3] > 200][:, :3] / 255.0 * 2.0 - 1.0
                if len(n):
                    np.testing.assert_allclose(np.linalg.norm(n, axis=1), 1.0, atol=0.05)

    def test_blocks(self):
        self.assertEqual(set(self.rects), set(vegetation.near_trees()))
        for u0, v0, u1, v1 in self.rects.values():
            self.assertTrue(0.0 <= u0 < u1 <= 1.0 and 0.0 <= v0 < v1 <= 1.0)
        self.assertEqual(self.albedo.shape[:2], self.normals.shape[:2])


if __name__ == "__main__":
    unittest.main()

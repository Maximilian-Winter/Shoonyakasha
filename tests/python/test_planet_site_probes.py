"""
shoonyakasha.planet.site_probes: a box's triangles face outward, and the
sky visibility baked on the GPU (site_probes.comp) around a box is full
far from it, less beside it, and zeros inside it; the grid's constants
match site_probes.glsl's.
"""

import unittest
from pathlib import Path

import numpy as np

import shoonyakasha as sk
from shoonyakasha.planet import site_probes as sp

SHADERS = Path(sp.__file__).resolve().parent.parent / "pipelines" / "planet" / "shaders"


def share(probe):
    """The share of the whole sphere a probe sees: L00 times Y00."""
    return 0.282095 * float(probe[0])


def probe_at(grid, x, y, z):
    nx, ny, nz = sp.COUNTS
    return grid[y * nz + z, x]


def index_of(point):
    return tuple(int((p - m) / s) for p, m, s in zip(point, sp.GRID_MIN, sp.SPACING))


class BoxTrianglesTest(unittest.TestCase):
    def test_outward_and_closed(self):
        centre = np.array([1.0, 2.0, 3.0])
        tris = sp.box_triangles(centre, (2.0, 4.0, 6.0))
        self.assertEqual(tris.shape, (12, 3, 3))
        normals = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
        self.assertTrue((np.einsum("ij,ij->i", normals, tris.mean(axis=1) - centre) > 0.0).all())
        volume = np.einsum("ij,ij->i", tris[:, 0] - centre, np.cross(tris[:, 1] - centre, tris[:, 2] - centre)).sum() / 6
        self.assertAlmostEqual(float(volume), 48.0, places=3)


class BakeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not sk.extension_available():
            raise unittest.SkipTest("the engine's extension is not importable here")
        try:
            ctx = sk.ComputeContext()
        except RuntimeError as exc:
            raise unittest.SkipTest("no Vulkan device: %s" % exc)
        try:
            # A box 12 m wide and 20 m tall at the site's centre, and a pad on the ground beside it.
            cls.grid = sp.bake(np.concatenate([sp.box_triangles((0.0, 10.0, 0.0), (12.0, 20.0, 12.0)),
                                               sp.box_triangles((-40.0, 0.1, -40.0), (20.0, 0.2, 20.0))]), ctx)
        finally:
            ctx.shutdown()

    def test_shape(self):
        nx, ny, nz = sp.COUNTS
        self.assertEqual(self.grid.shape, (ny * nz, nx, 4))

    def test_far_sees_all(self):
        self.assertGreater(share(probe_at(self.grid, 31, 0, 31)), 0.98)

    def test_inside_is_zeros(self):
        x, y, z = index_of((1.0, 5.0, 1.0))
        np.testing.assert_array_equal(probe_at(self.grid, x, y, z), 0.0)

    def test_beside_sees_less_towards_it(self):
        x, y, z = index_of((-8.5, 1.0, 1.0))
        probe = probe_at(self.grid, x, y, z)
        self.assertLess(share(probe), 0.9)
        self.assertGreater(share(probe), 0.3)
        self.assertLess(float(probe[1]), 0.0)          # less sky towards +x, where the box stands

    def test_a_pad_on_the_ground_hides_nothing(self):
        x, y, z = index_of((-40.0, 1.0, -40.0))
        self.assertGreater(share(probe_at(self.grid, x, y, z)), 0.97)


class ConstantsTest(unittest.TestCase):
    def test_counts_match_the_shader(self):
        text = (SHADERS / "site_probes.glsl").read_text()
        self.assertIn("const ivec3 SITE_PROBE_COUNTS = ivec3(%d, %d, %d);" % sp.COUNTS, text)


if __name__ == "__main__":
    unittest.main()

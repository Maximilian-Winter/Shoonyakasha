"""Tests for the vegetation's meshes of shoonyakasha.planet (vegetation.py):
every kind is one unit tall from its base, its normals are those of its
triangles and point out (wood away from the trunk's axis, foliage away from
its crown, cones' bases down), foliage and wood are marked, grass blades
face up with their normals leaning further up and are darker at their base, the
meshes fit the pipeline's buffers, and the draws' template gives each kind
its range and its instances' first index.

    python -m unittest discover -s tests/python
"""

import json
import unittest

import numpy as np

from shoonyakasha.planet import vegetation, world


class VegetationMeshTest(unittest.TestCase):
    def setUp(self):
        self.vertices, self.indices, self.ranges = vegetation.meshes()

    def kind(self, name):
        first, count = self.ranges[vegetation.KINDS.index(name)]
        return self.vertices[first:first + count]

    def test_ranges_cover_the_vertices_in_whole_triangles(self):
        total = 0
        for first, count in self.ranges:
            self.assertEqual(first, total)
            self.assertEqual(count % 3, 0)
            total += count
        self.assertEqual(total, len(self.vertices))
        np.testing.assert_array_equal(self.indices, np.arange(len(self.vertices)))

    def test_kinds_are_about_one_unit_tall_from_their_base(self):
        for name in vegetation.KINDS:
            part = self.kind(name)
            self.assertGreaterEqual(part[:, 1].min(), -0.06, name)
            self.assertLessEqual(part[:, 1].max(), 1.1, name)
            self.assertGreater(part[:, 1].max(), 0.7, name)

    def test_normals_are_the_triangles_and_unit(self):
        tri = self.vertices.reshape(-1, 3, 12)
        tri = tri[tri[:, 0, 7] < 1.5]
        a, b, c = tri[:, 0, :3], tri[:, 1, :3], tri[:, 2, :3]
        n = np.cross(b - a, c - a)
        n /= np.linalg.norm(n, axis=1, keepdims=True)
        for k in range(3):
            np.testing.assert_allclose(tri[:, k, 4:7], n, atol=1e-5)

    def test_wood_points_away_from_the_trunk(self):
        for name in vegetation.KINDS:
            part = self.kind(name)
            wood = part[part[:, 7] < 0.5]
            if len(wood):
                outward = np.einsum("ij,ij->i", wood[:, [4, 6]], wood[:, [0, 2]])
                self.assertTrue((outward > 0.0).all(), name)

    def test_crowns_point_away_from_their_centre(self):
        for name in ("broadleaf", "broadleaf far", "bush"):
            part = self.kind(name)
            foliage = part[part[:, 7] > 0.5].reshape(-1, 3, 12)
            centre = foliage[:, :, :3].reshape(-1, 3).mean(axis=0)
            middles = foliage[:, :, :3].mean(axis=1)
            self.assertTrue((np.einsum("ij,ij->i", foliage[:, 0, 4:7], middles - centre) > 0.0).all(), name)

    def test_cones_open_outward_and_close_downward(self):
        for name in ("conifer", "conifer far"):
            part = self.kind(name)
            foliage = part[part[:, 7] > 0.5].reshape(-1, 3, 12)
            normal_y = foliage[:, 0, 5]
            sides = foliage[normal_y > 0.0]
            bases = foliage[normal_y <= 0.0]
            np.testing.assert_allclose(bases[:, 0, 4:7], np.tile([0.0, -1.0, 0.0], (len(bases), 1)), atol=1e-6)
            middles = sides[:, :, :3].mean(axis=1)
            self.assertTrue((np.einsum("ij,ij->i", sides[:, 0, [4, 6]], middles[:, [0, 2]]) > 0.0).all(), name)

    def test_grass_blades_face_up_lean_further_up_and_darken_at_their_base(self):
        part = self.kind("grass")
        self.assertTrue((part[:, 7] == 2.0).all())
        np.testing.assert_allclose(np.linalg.norm(part[:, 4:7], axis=1), 1.0, atol=1e-5)
        self.assertTrue((part[:, 5] > 0.8).all())
        tri = part.reshape(-1, 3, 12)
        a, b, c = tri[:, 0, :3], tri[:, 1, :3], tri[:, 2, :3]
        n = np.cross(b - a, c - a)
        self.assertTrue((n[:, 1] > 0.0).all())
        self.assertTrue((np.einsum("ij,ij->i", n, tri[:, 0, 4:7]) > 0.0).all())
        base = part[part[:, 1] < 1e-6]
        tip = part[part[:, 1] > 0.7]
        self.assertTrue((base[:, 9].max() < tip[:, 9].min()))

    def test_meshes_fit_the_pipeline_buffers(self):
        layouts = json.loads(world.PIPELINE.read_text(encoding="utf-8"))["bufferLayouts"]
        self.assertLessEqual(len(self.vertices), layouts["VegetationMesh"]["elementCount"])
        self.assertLessEqual(len(self.indices), layouts["VegetationIndices"]["elementCount"])
        trees = len(vegetation.KINDS) - 1
        self.assertEqual(layouts["VegetationInstances"]["elementCount"],
                         (trees * world.VEGETATION_CAPACITY + world.GRASS_CAPACITY) * 2)
        self.assertGreaterEqual(layouts["VegetationDraws"]["elementCount"], len(vegetation.KINDS) * 5)

    def test_draw_template(self):
        firsts = [7 * kind + 3 for kind in range(len(self.ranges))]
        words = vegetation.draw_template(self.ranges, firsts).reshape(-1, 5)
        self.assertEqual(len(words), len(vegetation.KINDS))
        for kind, (first, count) in enumerate(self.ranges):
            self.assertEqual(tuple(words[kind]), (count, 0, first, 0, firsts[kind]))


if __name__ == "__main__":
    unittest.main()

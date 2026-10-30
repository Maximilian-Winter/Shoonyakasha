"""Tests for the vegetation's meshes of shoonyakasha.planet (vegetation.py):
the kinds' index ranges cover whole triangles of their own vertices, every
kind is one unit tall from its base, normals are of unit length, each kind
is made of the parts it should be (trees of bark and cards, far kinds of
one impostor quad as wide as the tree, bushes of cards, the cactus plain,
grass of grass cards), cards show their atlas cards and impostors the
rects given, crowns' normals point out of the crown, grass leans up and is
darker at its base, the meshes, kinds and draws fit the pipeline's buffers
and passes and the shaders' constants, the trees thinned with distance fit
their kinds' instances where a tree grows in every cell, and the draws'
template gives each kind its range and its instances' first index.

    python -m unittest discover -s tests/python
"""

import json
import re
import unittest

import numpy as np

from shoonyakasha.planet import vegetation, vegetation_textures, world


class VegetationMeshTest(unittest.TestCase):
    def setUp(self):
        self.rects = {name: (0.1 * k, 0.0, 0.1 * k + 0.05, 0.5) for k, name in enumerate(vegetation.TREES)}
        self.vertices, self.indices, self.ranges = vegetation.meshes(self.rects)

    def kind(self, name):
        """The kind's vertices (those its indices use)."""
        first, count = self.ranges[vegetation.KINDS.index(name)]
        used = np.unique(self.indices[first:first + count])
        return self.vertices[used]

    def parts(self, name):
        return set(int(p) for p in self.kind(name)[:, 7])

    def test_ranges_cover_whole_triangles_of_their_own_vertices(self):
        total, last_vertex = 0, -1
        for first, count in self.ranges:
            self.assertEqual(first, total)
            self.assertEqual(count % 3, 0)
            used = self.indices[first:first + count]
            self.assertGreater(used.min(), last_vertex)
            last_vertex = used.max()
            total += count
        self.assertEqual(total, len(self.indices))
        self.assertEqual(last_vertex, len(self.vertices) - 1)

    def test_kinds_are_one_unit_tall_from_their_base(self):
        for name in vegetation.KINDS:
            part = self.kind(name)
            self.assertAlmostEqual(float(part[:, 1].max()), 1.0, places=5, msg=name)
            self.assertGreaterEqual(float(part[:, 1].min()), -0.06, name)

    def test_normals_are_unit(self):
        np.testing.assert_allclose(np.linalg.norm(self.vertices[:, 4:7], axis=1), 1.0, atol=1e-4)

    def test_parts(self):
        for tree in vegetation.TREES:
            self.assertEqual(self.parts(tree), {vegetation.PART_BARK, vegetation.PART_CARD}, tree)
            self.assertEqual(self.parts(tree + "_far"), {vegetation.PART_IMPOSTOR}, tree)
        self.assertEqual(self.parts("bush"), {vegetation.PART_CARD})
        self.assertEqual(self.parts("shrub"), {vegetation.PART_CARD})
        self.assertEqual(self.parts("cactus"), {vegetation.PART_PLAIN})
        self.assertEqual(self.parts("grass"), {vegetation.PART_GRASS})

    def test_cards_show_atlas_cards(self):
        rects = np.array([vegetation_textures.card_rect(name) for name in vegetation_textures.CARDS])
        cards = self.vertices[np.isin(self.vertices[:, 7], (vegetation.PART_CARD, vegetation.PART_GRASS))]
        u, v = cards[:, 3], cards[:, 11]
        inside = ((u[:, None] >= rects[:, 0] - 1e-6) & (u[:, None] <= rects[:, 2] + 1e-6)
                  & (v[:, None] >= rects[:, 1] - 1e-6) & (v[:, None] <= rects[:, 3] + 1e-6))
        self.assertTrue(inside.any(axis=1).all())

    def test_impostors_are_quads_as_wide_as_their_tree_showing_their_rect(self):
        trees = vegetation.trees()
        for tree in vegetation.TREES:
            quad = self.kind(tree + "_far")
            self.assertEqual(len(quad), 4)
            width = vegetation.impostor_width(trees[tree][0])
            self.assertAlmostEqual(float(quad[:, 0].max() - quad[:, 0].min()), width, places=5)
            u0, v0, u1, v1 = self.rects[tree]
            self.assertEqual(sorted(set(np.round(quad[:, 3], 6))), sorted({round(u0, 6), round(u1, 6)}))
            self.assertEqual(sorted(set(np.round(quad[:, 11], 6))), sorted({round(v0, 6), round(v1, 6)}))

    def test_impostor_width_reaches_the_furthest_point(self):
        vertices = np.zeros((3, 12), np.float32)
        vertices[:, 0], vertices[:, 2] = (0.3, -0.1, 0.0), (0.4, 0.0, -0.2)
        self.assertAlmostEqual(vegetation.impostor_width(vertices), 1.0, places=6)

    def test_crowns_normals_point_out(self):
        for name in ("broadleaf_a", "broadleaf_b", "bush"):
            part = self.kind(name)
            cards = part[part[:, 7] == vegetation.PART_CARD]
            centre = cards[:, :3].mean(axis=0)
            outward = np.einsum("ij,ij->i", cards[:, 4:7], cards[:, :3] - centre)
            self.assertGreater((outward > 0.0).mean(), 0.9, name)

    def test_grass_leans_up_and_darkens_at_its_base(self):
        grass = self.kind("grass")
        self.assertTrue((grass[:, 5] > 0.6).all())
        base, tip = grass[grass[:, 1] < 1e-6], grass[grass[:, 1] > 0.99]
        self.assertTrue(base[:, 8].max() < tip[:, 8].min())

    def test_meshes_kinds_and_draws_fit_the_pipeline(self):
        pipeline = json.loads(world.PIPELINE.read_text(encoding="utf-8"))
        layouts = pipeline["bufferLayouts"]
        self.assertLessEqual(len(self.vertices), layouts["VegetationMesh"]["elementCount"])
        self.assertLessEqual(len(self.indices), layouts["VegetationIndices"]["elementCount"])
        others = len(vegetation.KINDS) - 1
        self.assertEqual(vegetation.GRASS, len(vegetation.KINDS) - 1)
        self.assertEqual(layouts["VegetationInstances"]["elementCount"],
                         (others * world.VEGETATION_CAPACITY + world.GRASS_CAPACITY) * 2)
        self.assertGreaterEqual(layouts["VegetationDraws"]["elementCount"], len(vegetation.KINDS) * 5)
        passes = {p["name"]: p for p in pipeline["passes"]}
        self.assertEqual(passes["Vegetation"]["execution"]["arguments"]["drawCount"], len(vegetation.KINDS))
        self.assertEqual(passes["ShadowVegetation{cascade}"]["execution"]["arguments"]["drawCount"],
                         vegetation.GRASS)
        glsl = (world.PIPELINE.parent / "shaders" / "vegetation.glsl").read_text(encoding="utf-8")
        constant = lambda name: int(re.search(r"const int %s = (\d+);" % name, glsl).group(1))
        self.assertEqual(constant("VEGETATION_KINDS"), len(vegetation.KINDS))
        self.assertEqual(constant("VEGETATION_GRASS"), vegetation.GRASS)
        self.assertEqual(constant("VEGETATION_TREE_KINDS"), 2 * len(vegetation.TREES))
        for name, value in (("BARK", vegetation.PART_BARK), ("CARD", vegetation.PART_CARD),
                            ("GRASS", vegetation.PART_GRASS), ("IMPOSTOR", vegetation.PART_IMPOSTOR),
                            ("PLAIN", vegetation.PART_PLAIN)):
            self.assertEqual(constant("VEGETATION_PART_" + name), value)

    def test_thinned_trees_fit_their_kinds_instances(self):
        cell = (np.pi / 2.0 * world.RADIUS / world.VEGETATION_CELLS) ** 2
        for vegetation_range in (500.0, 700.0, 1_000.0, 2_500.0, 5_000.0):
            full = world.vegetation_thinning(vegetation_range)
            self.assertLessEqual(full, vegetation_range)
            r = np.linspace(0.0, vegetation_range, 200_001)[1:]
            kept = 2.0 * np.pi * r * np.minimum(1.0, (full / r) ** 2) / cell * (r[1] - r[0])
            near, far = kept[r <= world.VEGETATION_NEAR].sum(), kept[r > world.VEGETATION_NEAR].sum()
            self.assertLess(near, world.VEGETATION_CAPACITY, vegetation_range)
            self.assertLess(far, 0.9 * world.VEGETATION_CAPACITY, vegetation_range)

    def test_draw_template(self):
        firsts = [7 * kind + 3 for kind in range(len(self.ranges))]
        words = vegetation.draw_template(self.ranges, firsts).reshape(-1, 5)
        self.assertEqual(len(words), len(vegetation.KINDS))
        for kind, (first, count) in enumerate(self.ranges):
            self.assertEqual(tuple(words[kind]), (count, 0, first, 0, firsts[kind]))


if __name__ == "__main__":
    unittest.main()

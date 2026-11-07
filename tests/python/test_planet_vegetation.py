"""Tests for the vegetation's meshes of shoonyakasha.planet (vegetation.py):
the kinds' index ranges cover whole triangles of their own vertices, every
kind is one unit tall from its base (a rock reaching ROCK_SINK below it),
normals are of unit length, each kind is made of the parts it should be
(trees of bark and cards, far kinds of one impostor quad as wide as the
tree, bushes of cards, the cactus and the rocks plain, grass of grass
cards), the rocks are centred on their axis and closed with outward
normals, cards show their atlas cards and impostors the
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

from shoonyakasha.planet import scans, vegetation, vegetation_textures, world


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
            if count == 0:              # a scan not prepared
                continue
            self.assertGreater(used.min(), last_vertex)
            last_vertex = used.max()
            total += count
        self.assertEqual(total, len(self.indices))
        self.assertEqual(last_vertex, len(self.vertices) - 1)

    def test_kinds_are_one_unit_tall_from_their_base(self):
        for name in vegetation.KINDS[:vegetation.SCAN_FIRST]:     # the scans: test_scan_mesh_from_a_prepared_scan
            part = self.kind(name)
            self.assertAlmostEqual(float(part[:, 1].max()), 1.0, places=5, msg=name)
            if name in vegetation.ROCKS:
                self.assertAlmostEqual(float(part[:, 1].min()), -vegetation.ROCK_SINK, places=5, msg=name)
            else:
                self.assertGreaterEqual(float(part[:, 1].min()), -0.06, name)

    def test_rocks_are_centred_and_closed(self):
        for name in vegetation.ROCKS:
            part = self.kind(name)
            for axis in (0, 2):
                self.assertAlmostEqual(float(part[:, axis].min() + part[:, axis].max()), 0.0, places=5)
            positions, normals, shades = vegetation.rock_triangles(name)
            self.assertEqual(len(positions) % 3, 0)
            # A closed surface with outward normals: the flux of the position field is three times the volume.
            a, b, c = positions[0::3], positions[1::3], positions[2::3]
            volume = float(np.einsum("ij,ij->i", a, np.cross(b, c)).sum()) / 6.0
            self.assertGreater(volume, 0.0)
            centres = (a + b + c) / 3.0 - (0.0, 0.5 - 0.5 * vegetation.ROCK_SINK, 0.0)
            self.assertTrue(np.all(np.einsum("ij,ij->i", normals[0::3], centres) > 0.0))
            self.assertTrue(np.all((shades >= 0.6) & (shades <= 1.0)))
            points = vegetation.rock_points(name)
            subdivisions = vegetation.ROCKS[name][2]
            self.assertEqual(len(points), 10 * 4 ** subdivisions + 2)
            self.assertEqual(len(positions), 3 * 20 * 4 ** subdivisions)

    def test_normals_are_unit(self):
        np.testing.assert_allclose(np.linalg.norm(self.vertices[:, 4:7], axis=1), 1.0, atol=1e-4)

    def test_parts(self):
        for tree in vegetation.TREES:
            self.assertEqual(self.parts(tree), {vegetation.PART_BARK, vegetation.PART_CARD}, tree)
            self.assertEqual(self.parts(tree + "_far"), {vegetation.PART_IMPOSTOR}, tree)
        self.assertEqual(self.parts("bush"), {vegetation.PART_CARD})
        self.assertEqual(self.parts("shrub"), {vegetation.PART_CARD})
        self.assertEqual(self.parts("cactus"), {vegetation.PART_PLAIN})
        self.assertEqual(self.parts("rock"), {vegetation.PART_PLAIN})
        self.assertEqual(self.parts("boulder"), {vegetation.PART_PLAIN})
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
        self.assertEqual(vegetation.SCAN_FIRST, vegetation.GRASS + 1)
        self.assertEqual(vegetation.KINDS[vegetation.SCAN_FIRST:], scans.NAMES)
        self.assertEqual(layouts["VegetationInstances"]["elementCount"],
                         (others * world.VEGETATION_CAPACITY + world.GRASS_CAPACITY) * 2)
        firsts = vegetation.instance_firsts(world.VEGETATION_CAPACITY, world.GRASS_CAPACITY)
        self.assertEqual(firsts[vegetation.SCAN_FIRST], vegetation.GRASS * world.VEGETATION_CAPACITY
                         + world.GRASS_CAPACITY)
        self.assertEqual(firsts[-1] + world.VEGETATION_CAPACITY,
                         layouts["VegetationInstances"]["elementCount"] // 2)
        self.assertGreaterEqual(layouts["VegetationDraws"]["elementCount"], len(vegetation.KINDS) * 5)
        passes = {p["name"]: p for p in pipeline["passes"]}
        self.assertEqual(passes["Vegetation"]["execution"]["arguments"]["drawCount"], len(vegetation.KINDS))
        self.assertEqual(passes["ShadowVegetation{cascade}"]["execution"]["arguments"]["drawCount"],
                         vegetation.GRASS)
        scan_shadows = passes["ShadowScans{cascade}"]["execution"]["arguments"]
        self.assertEqual(scan_shadows["offset"], vegetation.SCAN_FIRST * 20)
        self.assertEqual(scan_shadows["drawCount"], len(scans.SCANS))
        glsl = (world.PIPELINE.parent / "shaders" / "vegetation.glsl").read_text(encoding="utf-8")
        constant = lambda name: int(re.search(r"const int %s = (\d+);" % name, glsl).group(1))
        self.assertEqual(constant("VEGETATION_KINDS"), len(vegetation.KINDS))
        self.assertEqual(constant("VEGETATION_GRASS"), vegetation.GRASS)
        self.assertEqual(constant("VEGETATION_TREE_KINDS"), 2 * len(vegetation.TREES))
        self.assertEqual(constant("VEGETATION_SCAN_FIRST"), vegetation.SCAN_FIRST)
        self.assertEqual(constant("VEGETATION_SCANS"), len(scans.SCANS))
        self.assertEqual(constant("VEGETATION_PART_SCAN"), vegetation.PART_SCAN)
        for name, value in (("BARK", vegetation.PART_BARK), ("CARD", vegetation.PART_CARD),
                            ("GRASS", vegetation.PART_GRASS), ("IMPOSTOR", vegetation.PART_IMPOSTOR),
                            ("PLAIN", vegetation.PART_PLAIN)):
            self.assertEqual(constant("VEGETATION_PART_" + name), value)

    def test_the_scatters_scan_table_matches_scans(self):
        comp = (world.PIPELINE.parent / "shaders" / "vegetation_scatter.comp").read_text(encoding="utf-8")
        body = lambda name: re.search(r"%s\[VEGETATION_SCANS\] = \w+\[VEGETATION_SCANS\]\((.*?)\);" % name, comp,
                                      re.S).group(1)
        families = [int(x) for x in body("SCAN_FAMILY").split(",")]
        order = {"desert": 0, "forest": 1, "rock": 2}
        self.assertEqual(families, [order[s.family] for s in scans.SCANS])
        heights = [tuple(float(v) for v in pair.split(","))
                   for pair in re.findall(r"vec2\(([^)]*)\)", body("SCAN_HEIGHTS"))]
        self.assertEqual(heights, [s.heights for s in scans.SCANS])
        self.assertEqual(len(body("SCAN_WEIGHTS").split(",")), len(scans.SCANS))
        self.assertEqual([x.strip() == "true" for x in body("SCAN_SMALL").split(",")], [s.small for s in scans.SCANS])
        self.assertEqual([x.strip() == "true" for x in body("SCAN_ROCK").split(",")], [s.rock for s in scans.SCANS])

    def test_scan_mesh_from_a_prepared_scan(self):
        kind = next(s for s in scans.SCANS if not s.rock)
        positions = np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 2.0, 0.0], [0.0, 2.0, 1.0]], np.float32)
        scan = scans.Scan(kind, positions, np.tile([0.0, 0.0, 1.0], (4, 1)),
                          np.array([[1, 0, 0, 1], [1, 0, 0, 1], [1, 0, 0, -1], [1, 0, 0, -1]], np.float32),
                          np.array([[0.1, 0.2], [0.3, 0.4], [0.5, 0.6], [0.7, 0.8]], np.float32),
                          np.array([0, 0, 1, 1], np.uint8), np.array([0, 1, 2, 1, 2, 3], np.uint32),
                          np.zeros((0, 3), np.float32),
                          [{"albedo": "a.png", "cutout": False}, {"albedo": "b.png", "cutout": True}])
        vertices, indices = vegetation.scan_mesh(scan, {(kind.name, 0): 3, (kind.name, 1): 5})
        self.assertAlmostEqual(vertices[:, 1].max(), 1.0 - kind.sink, places=6)
        self.assertEqual(list(vertices[:, 7]), [vegetation.scan_part(3, False, False)] * 2
                         + [vegetation.scan_part(5, True, True)] * 2)
        self.assertEqual(list(vertices[:, 3]), [0.1, 0.3, 0.5, 0.7])
        self.assertEqual(list(indices), [0, 1, 2, 1, 2, 3])
        _, kept = vegetation.scan_mesh(scan, {(kind.name, 0): 3})
        self.assertEqual(len(kept), 0)

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

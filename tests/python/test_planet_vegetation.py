"""Tests for the vegetation's meshes of shoonyakasha.planet (vegetation.py):
the kinds' index ranges cover whole triangles of their own vertices, every
kind is one unit tall from its base (a rock reaching ROCK_SINK below it),
normals are of unit length, each kind is made of the parts it should be
(trees of bark and cards, far kinds of one square about the tree's
bounding sphere, bushes of cards, the cactus and the rocks plain, grass of grass
cards), the rocks are centred on their axis and closed with outward
normals, cards show their atlas cards and impostors the
rects given, crowns' normals point out of the crown, grass leans up and is
darker at its base, the meshes, kinds and draws fit the pipeline's buffers
and passes and the shaders' constants, the trees thinned with distance fit
their kinds' instances where a tree grows in every cell, and the draws'
template gives each kind its range and its instances' first index. The
grown trees: every variant of every species, the same for the same seed
and different for another, with a trunk thinner than the tree and
leaving it below the crown's top; and on the GPU, vegetation.glsl's
instances' layout and variant kinds equal vegetation.py's.

    python -m unittest discover -s tests/python
"""

import json
import re
import shutil
import struct
import tempfile
import unittest

from pathlib import Path

import numpy as np

import shoonyakasha as sk
from shoonyakasha import shaders
from shoonyakasha.planet import scans, tree_generator, vegetation, vegetation_impostors, vegetation_textures, world


class VegetationMeshTest(unittest.TestCase):
    def setUp(self):
        self.rects = {name: (0.02 * k, 0.0, 0.02 * k + 0.01, 0.5) for k, name in enumerate(vegetation.near_trees())}
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
        # The scans: test_scan_mesh_from_a_prepared_scan; the far kinds: test_impostors_are_squares_about_their_sphere.
        for name in [k for k in vegetation.KINDS if k not in scans.NAMES and not k.endswith("_far")]:
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
        for tree in vegetation.near_trees():
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

    def test_impostors_are_squares_about_their_sphere(self):
        trees = vegetation.trees()
        for tree in vegetation.near_trees():
            quad = self.kind(tree + "_far")
            self.assertEqual(len(quad), 4)
            centre, radius = vegetation_impostors.sphere(trees[tree][0])
            corners = quad[:, 8:10]
            self.assertEqual(sorted(map(tuple, corners.tolist())), [(-1, -1), (-1, 1), (1, -1), (1, 1)])
            np.testing.assert_allclose(quad[:, 10], radius, rtol=1e-6)
            np.testing.assert_allclose(quad[:, 0], corners[:, 0] * radius, rtol=1e-5, atol=1e-6)
            np.testing.assert_allclose(quad[:, 1], centre + corners[:, 1] * radius, rtol=1e-5, atol=1e-6)
            u0, v0 = self.rects[tree][:2]
            np.testing.assert_allclose(quad[:, 3], u0, atol=1e-6)
            np.testing.assert_allclose(quad[:, 11], v0, atol=1e-6)

    def test_the_sphere_reaches_the_furthest_point(self):
        vertices = np.zeros((3, 12), np.float32)
        vertices[:, 0], vertices[:, 1], vertices[:, 2] = (0.3, 0.0, 0.0), (0.0, 1.0, 0.2), (0.0, 0.0, 0.0)
        centre, radius = vegetation_impostors.sphere(vertices)
        self.assertAlmostEqual(centre, 0.5, places=6)
        self.assertAlmostEqual(radius, np.hypot(0.3, 0.5) * vegetation_impostors.RADIUS_MARGIN, places=5)

    def test_crowns_normals_point_out(self):
        for name in vegetation.near_trees() + ["bush"]:
            part = self.kind(name)
            cards = part[part[:, 7] == vegetation.PART_CARD]
            centre = cards[:, :3].mean(axis=0)
            outward = np.einsum("ij,ij->i", cards[:, 4:7], cards[:, :3] - centre)
            self.assertGreater((outward > 0.0).mean(), 0.75, name)

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
        self.assertEqual(vegetation.SCAN_FIRST, vegetation.GRASS + 1)
        self.assertEqual(vegetation.KINDS[vegetation.SCAN_FIRST:vegetation.VARIANT_FIRST], scans.NAMES)
        trees = [k for k in vegetation.KINDS if vegetation.is_tree(k)]
        self.assertEqual(len(trees), 2 * len(vegetation.TREES) * vegetation.VARIANTS)
        capacities = [vegetation.kind_capacity(k, world.VEGETATION_CAPACITY, world.GRASS_CAPACITY)
                      for k in range(len(vegetation.KINDS))]
        self.assertEqual(layouts["VegetationInstances"]["elementCount"], sum(capacities) * 2)
        firsts = vegetation.instance_firsts(world.VEGETATION_CAPACITY, world.GRASS_CAPACITY)
        self.assertEqual(firsts, [sum(capacities[:k]) for k in range(len(capacities))])
        self.assertGreaterEqual(layouts["VegetationDraws"]["elementCount"], len(vegetation.KINDS) * 5)
        passes = {p["name"]: p for p in pipeline["passes"]}
        self.assertEqual(passes["Vegetation"]["execution"]["arguments"]["drawCount"], len(vegetation.KINDS))
        self.assertEqual(passes["ShadowVegetation{cascade}"]["execution"]["arguments"]["drawCount"],
                         vegetation.GRASS)
        scan_shadows = passes["ShadowScans{cascade}"]["execution"]["arguments"]
        self.assertEqual(scan_shadows["offset"], vegetation.SCAN_FIRST * 20)
        self.assertEqual(scan_shadows["drawCount"], len(vegetation.KINDS) - vegetation.SCAN_FIRST)
        glsl = (world.PIPELINE.parent / "shaders" / "vegetation.glsl").read_text(encoding="utf-8")
        constant = lambda name: int(re.search(r"const int %s = (\d+);" % name, glsl).group(1))
        self.assertEqual(constant("VEGETATION_KINDS"), len(vegetation.KINDS))
        self.assertEqual(constant("VEGETATION_GRASS"), vegetation.GRASS)
        self.assertEqual(constant("VEGETATION_TREE_KINDS"), 2 * len(vegetation.TREES))
        self.assertEqual(constant("VEGETATION_SCAN_FIRST"), vegetation.SCAN_FIRST)
        self.assertEqual(constant("VEGETATION_SCANS"), len(scans.SCANS))
        self.assertEqual(constant("VEGETATION_VARIANTS"), vegetation.VARIANTS)
        self.assertEqual(constant("VEGETATION_VARIANT_FIRST"), vegetation.VARIANT_FIRST)
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



class GrownTreesTest(unittest.TestCase):
    def test_every_variant_grows_the_same_for_the_same_seed(self):
        a = vegetation.forest((1.0, 2.0, 3.0))
        b = vegetation._forest.__wrapped__((1.0, 2.0, 3.0))
        c = vegetation.forest((4.0, 5.0, 6.0))
        self.assertEqual(sorted(a.meshes), sorted(vegetation.near_trees()))
        for name in vegetation.near_trees():
            np.testing.assert_array_equal(a.meshes[name][0], b.meshes[name][0])
            np.testing.assert_array_equal(a.meshes[name][1], b.meshes[name][1])
            self.assertFalse(np.array_equal(a.meshes[name][0][:50], c.meshes[name][0][:50]), name)
            radius, top = a.trunks[name]
            self.assertEqual(a.trunks[name + "_far"], (radius, top))
            self.assertTrue(0.005 < radius < 0.08, name)
            self.assertTrue(0.05 < top < 0.9, name)
        first = vegetation.near_trees()[0]
        self.assertFalse(np.array_equal(a.meshes[first][0], a.meshes[vegetation.near_trees()[1]][0]))

    def test_species_presets(self):
        self.assertEqual(sorted(tree_generator.SPECIES), sorted(vegetation.TREES))
        for name, species in tree_generator.SPECIES.items():
            vertices, indices, _ = tree_generator.grow(species, 5)
            self.assertLess(len(indices) // 3, 2000, name)
            self.assertAlmostEqual(float(vertices[:, 1].max()), 1.0, places=5)


LAYOUT_KERNEL = """#version 450
layout(local_size_x = 64) in;
#define VEGETATION_SET 0
#define VEGETATION_PARAMS 0
#include "vegetation.glsl"
layout(std430, set = 0, binding = 1) writeonly buffer Outputs { uint values[]; } dst;
layout(push_constant) uniform Push { uint count; uint capacity; uint grass; } pc;

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    int kind = int(i);
    dst.values[i * 8u] = vegetationFirstInstance(kind, pc.capacity, pc.grass);
    dst.values[i * 8u + 1u] = vegetationCapacity(kind, pc.capacity, pc.grass);
    dst.values[i * 8u + 2u] = vegetationIsTree(kind) ? 1u : 0u;
    for (int v = 0; v < VEGETATION_VARIANTS; ++v) {
        dst.values[i * 8u + 3u + uint(v)] = kind < VEGETATION_TREE_KINDS ? uint(vegetationVariantKind(kind, v)) : 0u;
    }
}
"""


class VegetationLayoutShader(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not sk.extension_available():
            raise unittest.SkipTest("the engine's extension is not importable here")
        try:
            shaders.find_glslc()
        except shaders.GlslcNotFound as exc:
            raise unittest.SkipTest(str(exc).splitlines()[0])
        try:
            cls.ctx = sk.ComputeContext()
        except RuntimeError as exc:
            raise unittest.SkipTest("no Vulkan device: %s" % exc)
        cls.tmp = Path(tempfile.mkdtemp())
        path = cls.tmp / "vegetation_layout_probe.comp"
        path.write_text(LAYOUT_KERNEL, encoding="utf-8")
        cls.kernel = cls.ctx.kernel(path, args=["-I", str(world.PIPELINE.parent / "shaders")])

    @classmethod
    def tearDownClass(cls):
        cls.ctx.shutdown()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_the_layout_and_variants_equal_vegetation_py(self):
        count = len(vegetation.KINDS)
        uniform = self.ctx.upload(bytes(16 * 9 + 16 * 96))
        out = np.zeros(count * 8, dtype=np.uint32)
        self.ctx.run(self.kernel, [uniform, out], threads=count,
                     push=struct.pack("<III", count, world.VEGETATION_CAPACITY, world.GRASS_CAPACITY))
        out = out.reshape(count, 8)
        firsts = vegetation.instance_firsts(world.VEGETATION_CAPACITY, world.GRASS_CAPACITY)
        self.assertEqual(out[:, 0].tolist(), firsts)
        self.assertEqual(out[:, 1].tolist(), [vegetation.kind_capacity(k, world.VEGETATION_CAPACITY,
                                                                       world.GRASS_CAPACITY) for k in range(count)])
        self.assertEqual(out[:, 2].tolist(), [int(vegetation.is_tree(k)) for k in vegetation.KINDS])
        for k, name in enumerate(vegetation.KINDS[:2 * len(vegetation.TREES)]):
            species, far = vegetation.species_of(name), name.endswith("_far")
            want = [vegetation.KINDS.index(("%s.%d" % (species, v) if v else species) + ("_far" if far else ""))
                    for v in range(vegetation.VARIANTS)]
            self.assertEqual(out[k, 3:3 + vegetation.VARIANTS].tolist(), want, name)


if __name__ == "__main__":
    unittest.main()

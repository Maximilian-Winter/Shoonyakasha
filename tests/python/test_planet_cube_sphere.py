"""Tests for the planet example's cube-sphere geometry and quadtree
(examples/python/simulations/planet/cube_sphere.py and quadtree.py), loaded
from their files. The chunk meshes need numpy; those tests skip without it.

    python -m unittest discover -s tests/python
"""

import importlib.util
import itertools
import math
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PLANET = REPO / "examples" / "python" / "simulations" / "planet"


def _load(name):
    spec = importlib.util.spec_from_file_location(name, PLANET / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module          # quadtree.py imports cube_sphere by name
    spec.loader.exec_module(module)
    return module


cube_sphere = _load("cube_sphere")
quadtree = _load("quadtree")

try:
    import numpy  # noqa: F401
    HAVE_NUMPY = True
except ImportError:
    HAVE_NUMPY = False

RADIUS = 100_000.0


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


class CubeSphereFaces(unittest.TestCase):
    def test_every_face_frame_is_right_handed_and_orthonormal(self):
        for face, (n, u, v) in enumerate(cube_sphere.FACES):
            with self.subTest(face=face):
                self.assertEqual(cross(u, v), n)
                self.assertEqual((dot(n, u), dot(n, v), dot(u, v)), (0.0, 0.0, 0.0))

    def test_the_faces_cover_all_six_directions(self):
        normals = sorted(n for n, _, _ in cube_sphere.FACES)
        expected = sorted(tuple(s * (axis == i) * 1.0 for i in range(3))
                          for axis in range(3) for s in (1.0, -1.0))
        self.assertEqual(normals, expected)

    def test_spherify_lands_on_the_unit_sphere(self):
        steps = [-1.0 + 2.0 * k / 16 for k in range(17)]
        for face, u, v in itertools.product(range(6), steps, steps):
            d = cube_sphere.direction(face, u, v)
            self.assertAlmostEqual(math.sqrt(dot(d, d)), 1.0, delta=1e-15)

    def test_spherify_keeps_face_centres_and_cube_corners_on_their_axes(self):
        self.assertEqual(cube_sphere.direction(4, 0.0, 0.0), (0.0, 0.0, 1.0))
        corner = cube_sphere.spherify((1.0, 1.0, 1.0))
        for component in corner:
            self.assertAlmostEqual(component, 1.0 / math.sqrt(3.0), delta=1e-15)

    def test_neighbouring_chunks_share_their_edge_vertices_exactly(self):
        """Every vertex on a chunk's edge is the same direction, to the bit,
        as in each chunk next to it: 2 chunks along an edge, 4 at a corner
        inside a face, 3 at a cube corner, 2 or 4 elsewhere on a face edge."""
        level, quads = 2, 4
        side = 1 << level
        uses = {}
        for face, y, x in itertools.product(range(6), range(side), range(side)):
            for j in range(quads + 1):
                for i in range(quads + 1):
                    if 0 < i < quads and 0 < j < quads:
                        continue
                    u, v = cube_sphere.chunk_uv(level, x, y, i, j, quads)
                    uses.setdefault(cube_sphere.direction(face, u, v), set()).add((face, x, y))
        counts = {len(keys) for keys in uses.values()}
        self.assertNotIn(1, counts, "edge vertices that no neighbouring chunk shares")
        self.assertEqual(counts, {2, 3, 4})
        self.assertEqual(sum(1 for keys in uses.values() if len(keys) == 3), 8)


@unittest.skipUnless(HAVE_NUMPY, "needs numpy")
class CubeSphereChunkMeshes(unittest.TestCase):
    LEVEL, QUADS, SKIRT = 2, 4, 50.0

    def chunks(self):
        side = 1 << self.LEVEL
        for face, y, x in itertools.product(range(6), range(side), range(side)):
            yield (face, x, y), cube_sphere.chunk_mesh(face, self.LEVEL, x, y, self.QUADS, RADIUS, self.SKIRT)

    def grid_size(self):
        return (self.QUADS + 1) ** 2

    def test_grid_directions_match_direction_to_the_bit(self):
        for (face, x, y), (_, _, normals, uvs, _) in self.chunks():
            for k in range(self.grid_size()):
                i, j = k % (self.QUADS + 1), k // (self.QUADS + 1)
                u, v = cube_sphere.chunk_uv(self.LEVEL, x, y, i, j, self.QUADS)
                self.assertEqual(tuple(uvs[k]), (u, v))
                self.assertEqual(tuple(normals[k]), cube_sphere.direction(face, u, v))

    def test_grid_triangles_face_outward(self):
        grid_triangles = 2 * self.QUADS * self.QUADS
        for key, (centre, positions, _, _, indices) in self.chunks():
            for t in range(grid_triangles):
                a, b, c = (positions[i] for i in indices[3 * t:3 * t + 3])
                middle = [centre[k] + (a[k] + b[k] + c[k]) / 3.0 for k in range(3)]
                self.assertGreater(dot(cross(b - a, c - a), middle), 0.0, "chunk %s triangle %d" % (key, t))

    def test_grid_lies_on_the_sphere_and_skirts_lie_below_their_edges(self):
        for key, (centre, positions, normals, _, indices) in self.chunks():
            radii = [math.sqrt(dot([centre[k] + p[k] for k in range(3)], [centre[k] + p[k] for k in range(3)]))
                     for p in positions]
            for r in radii[:self.grid_size()]:
                self.assertAlmostEqual(r, RADIUS, delta=1e-8)
            skirt = radii[self.grid_size():]
            self.assertEqual(len(skirt), 4 * (self.QUADS + 1))
            for r in skirt:
                self.assertAlmostEqual(r, RADIUS - self.SKIRT, delta=1e-8)

    def test_every_skirt_wall_is_drawn_from_both_sides(self):
        _, (_, _, _, _, indices) = next(self.chunks())
        skirt = [tuple(indices[t:t + 3]) for t in range(6 * self.QUADS * self.QUADS, len(indices), 3)]
        self.assertEqual(len(skirt), 4 * self.QUADS * 4)
        oriented = {tuple(t[i:] + t[:i]) for t in skirt for i in range(3)}
        for a, b, c in skirt:
            self.assertIn((a, c, b), oriented)


@unittest.skipUnless(HAVE_NUMPY, "needs numpy")
class GpuChunkBuffers(unittest.TestCase):
    """pipeline.json's chunk buffers fit the chunks cube_sphere.chunk_mesh
    lays out, which chunk_build.comp writes: 16 x 16 quads with skirts."""

    QUADS, SLOTS = 16, 2048

    def setUp(self):
        import json
        document = json.loads((PLANET / "pipeline" / "pipeline.json").read_text(encoding="utf-8"))
        self.layouts = document["bufferLayouts"]
        _, self.positions, _, _, self.indices = cube_sphere.chunk_mesh(4, 0, 0, 0, self.QUADS, RADIUS, 1.0)

    def test_the_pool_holds_every_slot_and_the_indices_fit_one_chunk(self):
        self.assertEqual(self.layouts["ChunkPool"]["elementCount"], self.SLOTS * len(self.positions))
        self.assertEqual(self.layouts["ChunkIndices"]["elementCount"], len(self.indices))
        self.assertLess(int(self.indices.max()), len(self.positions))

    def test_the_draw_buffers_hold_a_count_and_a_command_per_slot(self):
        for name in ("ChunkDraws", "OceanDraws"):
            self.assertEqual(self.layouts[name]["elementCount"], 4 + 5 * self.SLOTS, name)
        self.assertEqual(self.layouts["ChunkInstances"]["elementCount"], self.SLOTS)


class FakeChunks:
    """Stands in for the engine: counts chunks made and released."""

    def __init__(self):
        self.live = set()
        self.made = 0

    def make(self, node):
        self.made += 1
        self.live.add(node.key())
        return node.key()

    def release(self, node):
        self.live.remove(node.key())


class QuadTreeSelection(unittest.TestCase):
    MAX_LEVEL = 8

    def setUp(self):
        self.chunks = FakeChunks()
        self.tree = quadtree.QuadTree(RADIUS, self.chunks.make, self.chunks.release,
                                      max_level=self.MAX_LEVEL, split_factor=2.0, evict_frames=10)
        self.frame = 0
        self.ground = tuple(c * (RADIUS + 10.0) for c in cube_sphere.direction(4, 0.3, -0.2))

    def run_frames(self, camera, count, budget=8):
        for _ in range(count):
            self.frame += 1
            self.tree.update(camera, self.frame)
            self.tree.build(budget)
            self.tree.evict(self.frame)
        self.tree.update(camera, self.frame + 1)

    def assert_selection_covers_the_sphere_once(self):
        area = sum(4 ** (self.MAX_LEVEL - node.level) for node in self.tree.selected)
        self.assertEqual(area, 6 * 4 ** self.MAX_LEVEL)
        self.assertEqual(len({node.key() for node in self.tree.selected}), len(self.tree.selected))

    def test_starts_with_the_six_faces(self):
        self.tree.update(self.ground, 1)
        self.assertEqual(sorted(node.key() for node in self.tree.selected),
                         [(face, 0, 0, 0) for face in range(6)])
        self.assertEqual(self.chunks.made, 6)

    def test_refines_to_the_deepest_level_near_the_camera_and_covers_the_sphere(self):
        self.run_frames(self.ground, 200)
        self.assertEqual(self.tree.missing, [])
        self.assertEqual(max(node.level for node in self.tree.selected), self.MAX_LEVEL)
        self.assert_selection_covers_the_sphere_once()

    def test_draws_the_parent_until_all_four_children_are_built(self):
        self.run_frames(self.ground, 5, budget=0)
        self.assertEqual(len(self.tree.selected), 6)
        self.assertTrue(self.tree.missing)
        self.assertEqual({node.level for _, node in self.tree.missing}, {1})
        self.run_frames(self.ground, 1, budget=3)
        self.assertEqual(len(self.tree.selected), 6, "three of four children built: the parent is still drawn")
        self.run_frames(self.ground, 1, budget=1)
        self.assertEqual(len(self.tree.selected), 9, "the nearest root drawn through its four children")
        self.assert_selection_covers_the_sphere_once()

    def test_builds_the_nearest_missing_chunks_first(self):
        self.tree.update(self.ground, 1)
        distances = [distance for distance, _ in self.tree.missing]
        self.assertEqual(distances, sorted(distances))

    def test_a_split_node_merges_only_beyond_the_merge_distance(self):
        self.run_frames(self.ground, 200)
        node = next(n for n in self.tree.roots if n.face == 4)
        direction = cube_sphere.direction(4, 0.0, 0.0)
        just_past_split = (2.0 * 1.1) * node.edge + node.bound_radius        # beyond split, within merge
        self.run_frames(tuple(c * (RADIUS + just_past_split) for c in direction), 3)
        self.assertNotIn(node, self.tree.selected)
        past_merge = (2.0 * 1.25 * 1.1) * node.edge + node.bound_radius
        self.run_frames(tuple(c * (RADIUS + past_merge) for c in direction), 3)
        self.assertIn(node, self.tree.selected)

    def test_bounds_hold_the_patch_through_its_height_range(self):
        node = quadtree.Node(4, 5, 13, 17, RADIUS, -500.0, 3000.0, 0.05)
        u0, v0, size = cube_sphere.chunk_bounds(5, 13, 17)
        for su, sv, h in itertools.product((0.0, 0.3, 1.0), (0.0, 0.7, 1.0), (-500.0, 0.0, 3000.0)):
            d = cube_sphere.direction(4, u0 + su * size, v0 + sv * size)
            point = tuple(c * (RADIUS + h) for c in d)
            self.assertLessEqual(math.dist(point, node.bound_centre), node.bound_radius)
        wide = node.bound_radius
        node.set_heights(100.0, 140.0)
        self.assertTrue(node.measured)
        self.assertLess(node.bound_radius, wide)

    def test_children_start_with_their_parents_height_range(self):
        self.tree = quadtree.QuadTree(RADIUS, self.chunks.make, self.chunks.release, max_level=self.MAX_LEVEL,
                                      split_factor=2.0, heights=(-100.0, 900.0))
        root = self.tree.roots[4]
        root.set_heights(10.0, 50.0)
        self.run_frames(self.ground, 1, budget=0)
        self.assertTrue(root.children)
        for child in root.children:
            self.assertEqual((child.low, child.high, child.measured), (10.0, 50.0, False))

    def test_unused_subtrees_are_released(self):
        self.run_frames(self.ground, 200)
        self.assertGreater(len(self.chunks.live), 6)
        far = tuple(c * 50 * RADIUS for c in cube_sphere.direction(4, 0.0, 0.0))
        self.run_frames(far, 30)
        self.assertEqual(sorted(self.chunks.live), [(face, 0, 0, 0) for face in range(6)])
        self.assertEqual(sum(1 for _ in self.tree.nodes()), 6)


def segment_meets_sphere(a, b, radius):
    """Whether the segment from a to b passes through the sphere of `radius` around the origin."""
    ab = tuple(b[k] - a[k] for k in range(3))
    t = -sum(a[k] * ab[k] for k in range(3)) / sum(c * c for c in ab)
    t = min(1.0, max(0.0, t))
    nearest = tuple(a[k] + t * ab[k] for k in range(3))
    return sum(c * c for c in nearest) < radius * radius


class FaceCoordinates(unittest.TestCase):
    def test_face_uv_inverts_direction_across_every_face_and_up_to_the_corners(self):
        for face in range(6):
            for u in (-1.0, -0.999, -0.6, 0.0, 0.3141, 0.97, 1.0):
                for v in (-1.0, -0.5, 0.0, 0.73, 0.999):
                    d = cube_sphere.direction(face, u, v)
                    got_face, got_u, got_v = cube_sphere.face_uv(d)
                    back = cube_sphere.direction(got_face, got_u, got_v)
                    self.assertLess(max(abs(back[k] - d[k]) for k in range(3)), 1e-12)
                    if max(abs(u), abs(v)) < 0.999:
                        self.assertEqual(got_face, face)
                        self.assertAlmostEqual(got_u, u, places=10)
                        self.assertAlmostEqual(got_v, v, places=10)


class HorizonCulling(unittest.TestCase):
    OCCLUDER = RADIUS - 1_650.0

    def test_the_far_side_is_hidden_and_the_near_side_is_not(self):
        camera = (0.0, 0.0, RADIUS + 1_000.0)
        self.assertTrue(quadtree.hidden_by_horizon((0.0, 0.0, -RADIUS), 5_000.0, camera, self.OCCLUDER))
        self.assertFalse(quadtree.hidden_by_horizon((0.0, 0.0, RADIUS), 5_000.0, camera, self.OCCLUDER))

    def test_a_peak_rising_over_the_horizon_is_not_hidden(self):
        camera = (0.0, 0.0, RADIUS + 1_000.0)
        # 300 km around the sphere from the camera, its top 6 km up: over the horizon seen from 1 km.
        angle = 0.3
        top = RADIUS + 6_000.0
        centre = (math.sin(angle) * top, 0.0, math.cos(angle) * top)
        self.assertFalse(segment_meets_sphere(camera, centre, self.OCCLUDER))
        self.assertFalse(quadtree.hidden_by_horizon(centre, 100.0, camera, self.OCCLUDER))

    def test_a_camera_inside_the_occluder_hides_nothing(self):
        self.assertFalse(quadtree.hidden_by_horizon((0.0, 0.0, -RADIUS), 10.0, (0.0, 0.0, 1_000.0), self.OCCLUDER))

    def test_every_hidden_sphere_is_wholly_behind_the_occluder(self):
        random = __import__("random").Random(7)
        hidden = 0
        for _ in range(3_000):
            height = 10.0 ** random.uniform(0.0, 6.5)
            camera = tuple(c * (RADIUS + height) for c in normalized(random.gauss(0, 1) for _ in range(3)))
            direction = normalized(random.gauss(0, 1) for _ in range(3))
            centre = tuple(c * (RADIUS + random.uniform(-2_000.0, 8_000.0)) for c in direction)
            radius = 10.0 ** random.uniform(1.0, 4.5)
            if not quadtree.hidden_by_horizon(centre, radius, camera, self.OCCLUDER):
                continue
            hidden += 1
            for _ in range(40):
                offset = normalized(random.gauss(0, 1) for _ in range(3))
                point = tuple(centre[k] + offset[k] * radius * random.random() ** (1 / 3) for k in range(3))
                self.assertTrue(segment_meets_sphere(camera, point, self.OCCLUDER))
        self.assertGreater(hidden, 300)


def normalized(values):
    v = tuple(values)
    n = math.sqrt(sum(c * c for c in v))
    return tuple(c / n for c in v)


if __name__ == "__main__":
    unittest.main()

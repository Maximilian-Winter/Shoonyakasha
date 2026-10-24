"""Tests for the planet example's cube-sphere geometry
(examples/python/simulations/planet/cube_sphere.py), loaded from its file.

    python -m unittest discover -s tests/python
"""

import importlib.util
import itertools
import math
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
_SPEC = importlib.util.spec_from_file_location(
    "planet_cube_sphere", REPO / "examples" / "python" / "simulations" / "planet" / "cube_sphere.py")
cube_sphere = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(cube_sphere)

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


class CubeSphereChunks(unittest.TestCase):
    LEVEL, QUADS = 2, 4

    def chunks(self):
        side = 1 << self.LEVEL
        for face, y, x in itertools.product(range(6), range(side), range(side)):
            yield (face, x, y), cube_sphere.chunk_mesh(face, self.LEVEL, x, y, self.QUADS, RADIUS)

    def test_triangles_face_outward(self):
        for key, (centre, positions, _, _, indices) in self.chunks():
            for t in range(0, len(indices), 3):
                a, b, c = (positions[i] for i in indices[t:t + 3])
                normal = cross(tuple(b[k] - a[k] for k in range(3)), tuple(c[k] - a[k] for k in range(3)))
                middle = tuple(centre[k] + (a[k] + b[k] + c[k]) / 3.0 for k in range(3))
                self.assertGreater(dot(normal, middle), 0.0, "chunk %s triangle %d" % (key, t // 3))

    def test_vertices_lie_on_the_sphere_and_normals_point_away_from_the_centre(self):
        for key, (centre, positions, normals, _, _) in self.chunks():
            for p, n in zip(positions, normals):
                world = tuple(centre[k] + p[k] for k in range(3))
                self.assertAlmostEqual(math.sqrt(dot(world, world)), RADIUS, delta=1e-8)
                self.assertAlmostEqual(dot(n, world) / RADIUS, 1.0, delta=1e-12)

    def test_neighbouring_chunks_share_their_edge_vertices_exactly(self):
        """Every vertex on a chunk's edge is the same direction, to the bit,
        as in each chunk next to it: 2 chunks along an edge, 4 at a corner
        inside a face, 3 at a cube corner, 2 or 4 elsewhere on a face edge."""
        uses = {}
        last = self.QUADS
        for key, _ in self.chunks():
            face, x, y = key
            for j in range(last + 1):
                for i in range(last + 1):
                    if 0 < i < last and 0 < j < last:
                        continue
                    u, v = cube_sphere.chunk_uv(self.LEVEL, x, y, i, j, self.QUADS)
                    uses.setdefault(cube_sphere.direction(face, u, v), set()).add(key)
        counts = {len(keys) for keys in uses.values()}
        self.assertNotIn(1, counts, "edge vertices that no neighbouring chunk shares")
        self.assertEqual(counts, {2, 3, 4})
        cube_corners = [d for d, keys in uses.items() if len(keys) == 3]
        self.assertEqual(len(cube_corners), 8)


if __name__ == "__main__":
    unittest.main()

"""Tests for sk.PlanetPhysics (include/Planet/PlanetPhysics.h): bodies
falling under central gravity onto a grid mesh and coming to rest, the
origin shifted without waking them, ray casts, motion changes, batched
transforms and errors. Where this interpreter's shoonyakasha has no
extension (another test put the source tree first on sys.path), the tests
run in a fresh interpreter, which imports the installed package.

    python -m unittest discover -s tests/python
"""

import math
import os
import subprocess
import sys
import unittest

import numpy as np

import shoonyakasha as sk

GM = 4.905 * 100_000.0 ** 2        # the planet's: 4.905 m/s^2 at 100 km
RADIUS = 100_000.0
STEP = 1.0 / 120.0


def flat_grid(columns, spacing):
    """A flat square grid of points in the XZ plane, centred on the origin,
    columns along +X and rows along -Z, so that its triangles face
    cross(+X, -Z) = +Y."""
    mid = (columns - 1) * 0.5
    points = np.zeros((columns, columns, 3), dtype=np.float32)
    for j in range(columns):
        for i in range(columns):
            points[j, i] = ((i - mid) * spacing, 0.0, -(j - mid) * spacing)
    return points


class World:
    """A physics with the planet's centre RADIUS below the origin and a flat 20 m mesh at the origin."""

    def __init__(self):
        self.physics = sk.PlanetPhysics(max_bodies=256, threads=1)
        self.centre = (0.0, -RADIUS, 0.0)
        self.mesh = self.physics.create_grid_mesh(flat_grid(21, 1.0), 21, 21, (0.0, 0.0, 0.0))

    def run(self, seconds):
        for _ in range(int(round(seconds / STEP))):
            self.physics.apply_central_gravity(self.centre, GM)
            self.physics.step(STEP)


class PlanetPhysicsTest(unittest.TestCase):
    def setUp(self):
        if not sk.extension_available():
            if __name__ == "__main__":
                self.fail("no engine extension in a fresh interpreter")
            self.skipTest("runs in a fresh interpreter (test_in_fresh_interpreter)")

    def test_box_falls_onto_the_mesh_and_sleeps(self):
        w = World()
        box = w.physics.create_box((0.3, 0.3, 0.3), (0.0, 2.0, 0.0), density=500.0)
        self.assertAlmostEqual(w.physics.mass(box), 500.0 * 0.6 ** 3, delta=0.5)
        w.run(1.0)
        (_, y, _), _ = w.physics.get_transform(box)
        self.assertLess(y, 1.0)
        w.run(4.0)
        (x, y, z), _ = w.physics.get_transform(box)
        self.assertAlmostEqual(y, 0.3, delta=0.01)
        self.assertLess(math.hypot(x, z), 0.01)
        self.assertFalse(w.physics.is_active(box))

    def test_gravity_accelerates_towards_the_centre(self):
        w = World()
        box = w.physics.create_box((0.3, 0.3, 0.3), (5.0, 50.0, 0.0), mass=10.0)
        w.run(0.5)
        v, _ = w.physics.get_velocity(box)
        g = GM / (RADIUS + 50.0) ** 2
        self.assertAlmostEqual(v[1], -g * 0.5, delta=0.05)
        # Towards the centre, (0, -RADIUS, 0): a little towards -x from x = 5 m.
        self.assertLess(v[0], 0.0)
        self.assertAlmostEqual(v[0] / v[1], 5.0 / (RADIUS + 50.0), delta=1e-5)

    def test_shift_origin_moves_every_body_without_waking(self):
        w = World()
        box = w.physics.create_box((0.3, 0.3, 0.3), (0.0, 0.31, 0.0))
        w.run(3.0)
        self.assertFalse(w.physics.is_active(box))
        (x, y, z), q = w.physics.get_transform(box)
        w.physics.shift_origin((400.0, 0.0, -250.0))
        w.centre = (-400.0, -RADIUS, 250.0)
        (x2, y2, z2), q2 = w.physics.get_transform(box)
        self.assertAlmostEqual(x2, x - 400.0, delta=1e-3)
        self.assertAlmostEqual(y2, y, delta=1e-4)
        self.assertAlmostEqual(z2, z + 250.0, delta=1e-3)
        self.assertFalse(w.physics.is_active(box))
        w.run(2.0)
        (x3, y3, z3), _ = w.physics.get_transform(box)
        self.assertLess(math.dist((x3, y3, z3), (x2, y2, z2)), 1e-4)
        mesh_hit = w.physics.cast_ray((-395.0, 5.0, 250.0), (0.0, -1.0, 0.0), 10.0)   # beside the box
        self.assertIsNotNone(mesh_hit)
        self.assertEqual(mesh_hit[0], w.mesh)
        self.assertAlmostEqual(mesh_hit[1][1], 0.0, delta=1e-4)

    def test_cast_ray(self):
        w = World()
        box = w.physics.create_box((0.5, 0.5, 0.5), (3.0, 0.5, 0.0), motion="static")
        hit = w.physics.cast_ray((0.0, 10.0, 0.0), (0.0, -1.0, 0.0), 20.0)
        body, point, normal, fraction = hit
        self.assertEqual(body, w.mesh)
        self.assertAlmostEqual(point[1], 0.0, delta=1e-4)
        self.assertAlmostEqual(normal[1], 1.0, delta=1e-4)
        self.assertAlmostEqual(fraction, 0.5, delta=1e-4)
        hit = w.physics.cast_ray((3.0, 10.0, 0.0), (0.0, -1.0, 0.0), 20.0)
        self.assertEqual(hit[0], box)
        self.assertAlmostEqual(hit[1][1], 1.0, delta=1e-3)
        self.assertIsNone(w.physics.cast_ray((0.0, 10.0, 0.0), (0.0, 1.0, 0.0), 20.0))
        self.assertIsNone(w.physics.cast_ray((0.0, 10.0, 0.0), (0.0, -1.0, 0.0), 5.0))

    def test_static_bodies_stay_and_turn_dynamic(self):
        w = World()
        box = w.physics.create_box((0.3, 0.3, 0.3), (0.0, 3.0, 0.0), motion="static")
        self.assertEqual(w.physics.motion(box), "static")
        self.assertEqual(w.physics.mass(box), 0.0)
        w.run(1.0)
        (_, y, _), _ = w.physics.get_transform(box)
        self.assertEqual(y, 3.0)
        w.physics.set_motion(box, "dynamic")
        self.assertEqual(w.physics.motion(box), "dynamic")
        self.assertTrue(w.physics.is_active(box))
        w.run(4.0)
        (_, y, _), _ = w.physics.get_transform(box)
        self.assertAlmostEqual(y, 0.3, delta=0.01)       # on the mesh, not through it

    def test_impulse_and_velocity(self):
        w = World()
        ball = w.physics.create_sphere(0.5, (0.0, 20.0, 0.0), mass=4.0)
        w.physics.add_impulse(ball, (8.0, 0.0, 0.0))
        v, _ = w.physics.get_velocity(ball)
        self.assertAlmostEqual(v[0], 2.0, delta=1e-5)
        w.physics.set_velocity(ball, (0.0, 0.0, 1.0), (0.0, 3.0, 0.0))
        v, omega = w.physics.get_velocity(ball)
        self.assertAlmostEqual(v[2], 1.0, delta=1e-6)
        self.assertAlmostEqual(omega[1], 3.0, delta=1e-6)

    def test_get_transforms_in_order(self):
        w = World()
        a = w.physics.create_box((0.1, 0.1, 0.1), (1.0, 2.0, 3.0))
        b = w.physics.create_sphere(0.2, (-4.0, 5.0, -6.0))
        rows = np.frombuffer(w.physics.get_transforms([b, a]), dtype=np.float64).reshape(-1, 7)
        np.testing.assert_allclose(rows[0, :3], (-4.0, 5.0, -6.0))
        np.testing.assert_allclose(rows[1, :3], (1.0, 2.0, 3.0))
        np.testing.assert_allclose(rows[:, 3:], [(1.0, 0.0, 0.0, 0.0)] * 2, atol=1e-7)
        self.assertEqual(w.physics.get_transforms([]), b"")

    def test_remove_and_errors(self):
        w = World()
        box = w.physics.create_box((0.1, 0.1, 0.1), (0.0, 1.0, 0.0))
        self.assertEqual(w.physics.body_count, 2)
        w.physics.remove(box)
        self.assertFalse(w.physics.contains(box))
        self.assertEqual(w.physics.body_count, 1)
        with self.assertRaises(ValueError):
            w.physics.get_transform(box)
        with self.assertRaises(ValueError):
            w.physics.create_grid_mesh(np.zeros(10, dtype=np.float32), 21, 21, (0.0, 0.0, 0.0))
        with self.assertRaises(ValueError):
            w.physics.create_box((0.1, 0.1, 0.1), (0.0, 0.0, 0.0), motion="floating")


class FreshInterpreterTest(unittest.TestCase):
    def test_in_fresh_interpreter(self):
        if sk.extension_available() or __name__ == "__main__":
            self.skipTest("ran in this interpreter")
        here = os.path.dirname(os.path.abspath(__file__))
        result = subprocess.run([sys.executable, os.path.abspath(__file__)], cwd=here, capture_output=True,
                                text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()

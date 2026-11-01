"""Tests for sk.PlanetPhysics (include/Planet/PlanetPhysics.h): bodies
falling under central gravity onto a grid mesh and coming to rest, the
origin shifted without waking them, ray casts, near and far bodies on the
fine and the coarse ground, linear damping by default and none once set, a turned capsule, strikes
on watched bodies reported above a momentum, motion changes, batched
transforms and errors; characters standing and walking on a mesh along
any up, moved by an origin shift, riding a kinematic platform, holding
up bodies that land on them, letting bodies set to pass through them fall
through, and skipped by rays cast from inside them. Where this interpreter's shoonyakasha has no
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

    def test_capsule(self):
        w = World()
        # Lying along X: turned a quarter about Z, from (-1.4, 1.5, 0) to (1.4, 1.5, 0), 0.4 thick.
        s = math.sqrt(0.5)
        capsule = w.physics.create_capsule(0.4, 1.0, (0.0, 1.5, 0.0), (s, 0.0, 0.0, s), motion="static")
        hit = w.physics.cast_ray((0.5, 5.0, 0.0), (0.0, -1.0, 0.0), 10.0)
        self.assertEqual(hit[0], capsule)
        self.assertAlmostEqual(hit[1][1], 1.9, delta=1e-3)
        hit = w.physics.cast_ray((1.2, 5.0, 0.0), (0.0, -1.0, 0.0), 10.0)
        self.assertAlmostEqual(hit[1][1], 1.5 + math.sqrt(0.4 ** 2 - 0.2 ** 2), delta=1e-3)
        self.assertEqual(w.physics.cast_ray((1.5, 5.0, 0.0), (0.0, -1.0, 0.0), 10.0)[0], w.mesh)

    def test_near_and_far_bodies_on_fine_and_coarse_ground(self):
        w = World()
        coarse_points = flat_grid(21, 1.0)
        w.physics.create_grid_mesh(coarse_points, 21, 21, (0.0, 0.5, 0.0), coarse=True)
        near = w.physics.create_box((0.2, 0.2, 0.2), (-3.0, 1.5, 0.0))
        far = w.physics.create_box((0.2, 0.2, 0.2), (3.0, 1.5, 0.0))
        w.physics.set_far(far)
        self.assertTrue(w.physics.is_far(far))
        self.assertFalse(w.physics.is_far(near))
        w.run(2.0)
        # The near box on the fine mesh at 0, through the coarse one; the far box on the coarse mesh at 0.5.
        self.assertAlmostEqual(w.physics.get_transform(near)[0][1], 0.2, delta=0.03)
        self.assertAlmostEqual(w.physics.get_transform(far)[0][1], 0.7, delta=0.03)
        # Rays see what a near body does.
        self.assertEqual(w.physics.cast_ray((5.0, 3.0, 5.0), (0.0, -1.0, 0.0), 10.0)[0], w.mesh)
        # set_motion makes a moving body near; static bodies stay as they are.
        w.physics.set_motion(far, "dynamic")
        self.assertFalse(w.physics.is_far(far))
        w.physics.set_far(w.mesh)
        self.assertFalse(w.physics.is_far(w.mesh))

    def test_linear_damping(self):
        physics = sk.PlanetPhysics(max_bodies=8, threads=1)
        damped = physics.create_box((0.2, 0.2, 0.2), (0.0, 0.0, 0.0))
        free = physics.create_box((0.2, 0.2, 0.2), (0.0, 5.0, 0.0))
        physics.set_linear_damping(free, 0.0)
        for body in (damped, free):
            physics.set_velocity(body, (150.0, 0.0, 0.0))
        for _ in range(120):
            physics.step(STEP)
        self.assertAlmostEqual(physics.get_velocity(free)[0][0], 150.0, places=3)
        self.assertAlmostEqual(physics.get_velocity(damped)[0][0], 150.0 * (1.0 - 0.05 * STEP) ** 120, delta=0.05)

    def test_strikes_on_watched_bodies(self):
        w = World()
        w.physics.set_strike_momentum(100.0)
        target = w.physics.create_box((0.5, 0.5, 0.5), (0.0, 0.5, 0.0), motion="static")
        other = w.physics.create_box((0.5, 0.5, 0.5), (3.0, 0.5, 0.0), motion="static")
        w.physics.watch_strikes(target)
        # 10 kg dropped from 0.1 m above: about 1 m/s, 10 kg m/s, below the threshold.
        soft = w.physics.create_box((0.2, 0.2, 0.2), (0.0, 1.3, 0.0), mass=10.0)
        w.run(1.0)
        self.assertEqual(w.physics.take_struck(), [])
        w.physics.remove(soft)
        # 50 kg thrown down at 5 m/s: 250 kg m/s, reported once; the unwatched box is not.
        hard = w.physics.create_box((0.2, 0.2, 0.2), (0.0, 2.5, 0.0), mass=50.0)
        w.physics.set_velocity(hard, (0.0, -5.0, 0.0))
        thrown = w.physics.create_box((0.2, 0.2, 0.2), (3.0, 2.5, 0.0), mass=50.0)
        w.physics.set_velocity(thrown, (0.0, -5.0, 0.0))
        w.run(1.0)
        self.assertEqual(w.physics.take_struck(), [target])
        self.assertEqual(w.physics.take_struck(), [])
        w.physics.watch_strikes(target, False)
        w.physics.set_velocity(hard, (0.0, -5.0, 0.0))
        w.physics.set_transform(hard, (0.0, 2.5, 0.0), (1.0, 0.0, 0.0, 0.0))
        w.run(1.0)
        self.assertEqual(w.physics.take_struck(), [])
        self.assertTrue(w.physics.contains(other))

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


class CharacterTest(unittest.TestCase):
    def setUp(self):
        if not sk.extension_available():
            if __name__ == "__main__":
                self.fail("no engine extension in a fresh interpreter")
            self.skipTest("runs in a fresh interpreter (test_in_fresh_interpreter)")

    def settle(self, w, character, seconds, velocity=None, up=(0.0, 1.0, 0.0)):
        """The character stepped with gravity towards w.centre: on the
        ground moving with it plus `velocity`, in the air falling with
        `velocity` along the ground."""
        for _ in range(int(round(seconds / STEP))):
            feet = w.physics.character_position(character)
            r = tuple(f - c for f, c in zip(feet, w.centre))
            d = math.sqrt(sum(x * x for x in r))
            g = tuple(-x / d * GM / d ** 2 for x in r)
            v = w.physics.character_velocity(character)
            state, _, ground_v, _ = w.physics.character_ground(character)
            along = velocity if velocity is not None else (0.0, 0.0, 0.0)
            if state == "on_ground":
                v = tuple(ground_v[k] + along[k] for k in range(3))
            else:
                down = sum(a * b for a, b in zip(v, up))
                v = tuple(along[k] + up[k] * down + g[k] * STEP for k in range(3))
            w.physics.character_set_velocity(character, v)
            w.physics.character_update(character, STEP, g)
            w.physics.apply_central_gravity(w.centre, GM)
            w.physics.step(STEP)

    def test_stands_and_walks_on_the_mesh(self):
        w = World()
        c = w.physics.create_character((0.0, 1.0, 0.0), (0.0, 1.0, 0.0))
        self.settle(w, c, 1.5)
        state, normal, ground_v, body = w.physics.character_ground(c)
        self.assertEqual(state, "on_ground")
        self.assertEqual(body, w.mesh)
        self.assertAlmostEqual(normal[1], 1.0, delta=1e-4)
        self.assertLess(abs(w.physics.character_position(c)[1]), 0.05)
        self.settle(w, c, 2.0, velocity=(2.0, 0.0, 0.0))
        x, y, _ = w.physics.character_position(c)
        self.assertAlmostEqual(x, 4.0, delta=0.1)
        self.assertLess(abs(y), 0.05)

    def test_up_along_another_axis(self):
        physics = sk.PlanetPhysics(max_bodies=64, threads=1)
        # A wall facing +X through the origin, the centre RADIUS along -X.
        mid = 10.0
        points = np.zeros((21, 21, 3), dtype=np.float32)
        for j in range(21):
            for i in range(21):
                points[j, i] = (0.0, i - mid, j - mid)      # cross(+Y, +Z) = +X
        physics.create_grid_mesh(points, 21, 21, (0.0, 0.0, 0.0))
        w = World.__new__(World)
        w.physics, w.centre = physics, (-RADIUS, 0.0, 0.0)
        c = physics.create_character((1.0, 0.0, 0.0), (1.0, 0.0, 0.0))
        self.settle(w, c, 1.5, up=(1.0, 0.0, 0.0))
        self.assertEqual(physics.character_ground(c)[0], "on_ground")
        self.assertLess(abs(physics.character_position(c)[0]), 0.05)

    def test_shift_origin_moves_characters(self):
        w = World()
        c = w.physics.create_character((1.0, 0.5, 2.0), (0.0, 1.0, 0.0))
        w.physics.shift_origin((10.0, 0.0, -5.0))
        x, y, z = w.physics.character_position(c)
        self.assertAlmostEqual(x, -9.0, delta=1e-4)
        self.assertAlmostEqual(z, 7.0, delta=1e-4)

    def test_rides_a_kinematic_platform(self):
        w = World()
        hull = np.array([(x, y, z) for x in (-2.0, 2.0) for y in (0.0, 0.5) for z in (-2.0, 2.0)], dtype=np.float32)
        platform = w.physics.create_convex_hull(hull, (0.0, 1.0, 0.0), motion="kinematic")
        self.assertEqual(w.physics.motion(platform), "kinematic")
        c = w.physics.create_character((0.0, 1.6, 0.0), (0.0, 1.0, 0.0))
        self.settle(w, c, 1.0)
        self.assertEqual(w.physics.character_ground(c)[3], platform)
        x0 = 0.0
        for k in range(120):
            x0 += 0.5 * STEP
            w.physics.move_kinematic(platform, (x0, 1.0, 0.0), (1.0, 0.0, 0.0, 0.0), STEP)
            self.settle(w, c, STEP)
        ground_v = w.physics.character_ground(c)[2]
        self.assertAlmostEqual(ground_v[0], 0.5, delta=0.05)
        self.assertAlmostEqual(w.physics.character_position(c)[0], 0.5, delta=0.1)

    def test_bodies_land_on_a_character(self):
        w = World()
        c = w.physics.create_character((0.0, 0.0, 0.0), (0.0, 1.0, 0.0))
        box = w.physics.create_box((0.2, 0.2, 0.2), (0.0, 3.0, 0.0))
        self.settle(w, c, 2.0)
        self.assertGreater(w.physics.get_transform(box)[0][1], 1.8)
        w.physics.remove_character(c)
        with self.assertRaises(ValueError):
            w.physics.character_position(c)

    def test_bodies_pass_through_a_character(self):
        w = World()
        c = w.physics.create_character((0.0, 0.0, 0.0), (0.0, 1.0, 0.0))
        box = w.physics.create_box((0.2, 0.2, 0.2), (0.0, 3.0, 0.0))
        w.physics.set_pass_through(box, c)
        self.settle(w, c, 2.0)
        self.assertLess(w.physics.get_transform(box)[0][1], 0.3)
        # Through no character again: the character stands on the box once it is under its feet.
        w.physics.set_pass_through(box, None)
        w.physics.set_transform(box, (0.0, 0.2, 0.0), (1.0, 0.0, 0.0, 0.0))
        w.physics.character_set_position(c, (0.0, 0.5, 0.0))
        self.settle(w, c, 1.0)
        self.assertEqual(w.physics.character_ground(c)[3], box)
        with self.assertRaises(ValueError):
            w.physics.set_pass_through(box, c + 1)

    def test_ray_skips_a_character(self):
        w = World()
        c = w.physics.create_character((0.0, 0.0, 0.0), (0.0, 1.0, 0.0))
        box = w.physics.create_box((0.2, 0.2, 0.2), (3.0, 1.5, 0.0), motion="static")
        eye = (0.0, 1.5, 0.0)
        # From inside the capsule the ray stops on the character's inner body at once.
        inside = w.physics.cast_ray(eye, (1.0, 0.0, 0.0), 10.0)
        self.assertNotEqual(inside[0], box)
        self.assertLess(inside[3], 1e-3)
        hit = w.physics.cast_ray(eye, (1.0, 0.0, 0.0), 10.0, skip_character=c)
        self.assertEqual(hit[0], box)
        self.assertAlmostEqual(hit[1][0], 2.8, delta=1e-3)
        held = w.physics.create_box((0.2, 0.2, 0.2), (1.5, 1.5, 0.0), motion="dynamic")
        w.physics.set_pass_through(held, c)
        self.assertEqual(w.physics.cast_ray(eye, (1.0, 0.0, 0.0), 10.0, skip_character=c)[0], box)


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

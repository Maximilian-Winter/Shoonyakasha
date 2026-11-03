"""Tests for the planet example's gravity gun
(examples/python/simulations/planet/gun.py), loaded from its file with the
example's directory on sys.path, on sk.PlanetPhysics in a Bubble over a
flat mesh at the top of a sphere, held by the example's Player: grabbing,
holding and dropping a crate, pulling a far one in until it is grabbed, a
crate too heavy on the planet but not on the moon, the punt's speeds and
recoil, the charged punt (full speed for a light crate, a plain punt after
a click or within the cooldown), the ground punt lifting the player, turning the held crate, and
the hold distance's range. Where this interpreter's shoonyakasha has no
extension, the tests run in a fresh interpreter.

    python -m unittest discover -s tests/python
"""

import math
import os
import subprocess
import sys
import unittest
from pathlib import Path

import numpy as np

import shoonyakasha as sk
from shoonyakasha.planet.bubble import Bubble
from shoonyakasha.planet.character import Controls, Player

PLANET_DIR = Path(__file__).resolve().parents[2] / "examples" / "python" / "simulations" / "planet"
sys.path.insert(0, str(PLANET_DIR))
import gun as gun_module        # noqa: E402
from shoonyakasha.planet.vec import quat_rotate  # noqa: E402

Gun, GunControls = gun_module.Gun, gun_module.GunControls

STEP = 1.0 / 120.0
PLANET = (100_000.0, 4.905)         # radius (m), surface gravity (m/s^2)
MOON = (25_000.0, 0.981)
SIZE = 0.6                          # m, the crates' edge
RADIUS = 0.5 * math.sqrt(3.0) * SIZE


class Body:
    def __init__(self, radius):
        self.radius = radius

    def centre(self):
        return (0.0, 0.0, 0.0)

    def relative(self, point):
        return point


def flat_mesh(physics, columns=81, spacing=1.0):
    mid = (columns - 1) * 0.5
    points = np.zeros((columns, columns, 3), dtype=np.float32)
    for j in range(columns):
        for i in range(columns):
            points[j, i] = ((i - mid) * spacing, 0.0, -(j - mid) * spacing)
    return physics.create_grid_mesh(points, columns, columns, (0.0, 0.0, 0.0))


class Scene:
    """A bubble at the top (+Y) of a sphere, a flat mesh 80 m across at its
    origin, and a player there facing -Z with a gun."""

    def __init__(self, body=PLANET):
        radius, g = body
        self.physics = sk.PlanetPhysics(max_bodies=64, threads=1)
        self.bubble = Bubble(self.physics, Body(radius), g * radius * radius, (0.0, radius, 0.0))
        flat_mesh(self.physics)
        self.player = Player(self.bubble, (0.0, 0.05, 0.0), (0.0, 0.0, -1.0))
        self.crates = []
        self.gun = Gun(self.player, lambda: [(c, RADIUS, self.physics.mass(c)) for c in self.crates])
        self.controls = GunControls()
        self.run(0.5)

    def crate(self, position, mass):
        body = self.physics.create_box((0.5 * SIZE,) * 3, position, motion="dynamic", mass=mass)
        self.crates.append(body)
        return body

    def crate_at_hold_point(self, mass):
        _, _, forward = self.player.basis()
        eye = self.player.eye()
        return self.crate(tuple(e + f * (self.gun.hold_distance + RADIUS) for e, f in zip(eye, forward)), mass)

    def run(self, seconds, each=None):
        for _ in range(int(round(seconds / STEP))):
            self.player.step(Controls(), STEP)
            self.gun.step(self.controls, STEP)
            self.bubble.step(STEP)
            if each is not None:
                each()

    def click(self):
        """The grab control held for 0.1 s, then let go."""
        self.controls.grab = True
        self.run(0.1)
        self.controls.grab = False

    def off_hold_point(self):
        p = self.physics.get_transform(self.gun.held)[0]
        target = self.gun.hold_point(self.player.eye(), self.player.basis()[2])
        return math.dist(p, target)


class GunTest(unittest.TestCase):
    def setUp(self):
        if not sk.extension_available():
            if __name__ == "__main__":
                self.fail("no engine extension in a fresh interpreter")
            self.skipTest("runs in a fresh interpreter (test_in_fresh_interpreter)")

    def test_grab_hold_and_drop(self):
        s = Scene()
        crate = s.crate_at_hold_point(50.0)
        s.run(0.05)
        self.assertEqual(s.gun.status, "can_grab")
        s.click()
        self.assertEqual(s.gun.held, crate)
        s.run(1.0)
        self.assertEqual(s.gun.held, crate)
        self.assertLess(s.off_hold_point(), 0.02)
        s.click()
        self.assertIsNone(s.gun.held)
        s.run(1.5)
        self.assertLess(s.physics.get_transform(crate)[0][1], SIZE)

    def test_pull_brings_a_far_crate_in(self):
        s = Scene()
        crate = s.crate((0.0, 0.3, -10.0), 20.0)
        s.player.pitch = -math.atan2(s.player.eye()[1] - 0.3, 10.0)
        s.run(0.5)
        self.assertGreater(s.gun.target.distance, s.gun.p.grab_range)
        self.assertTrue(s.gun.target.grabbable)
        s.controls.grab = True
        s.run(3.0)
        s.controls.grab = False
        self.assertEqual(s.gun.held, crate)

    def test_too_heavy_on_the_planet_not_on_the_moon(self):
        for body, held in ((PLANET, False), (MOON, True)):
            s = Scene(body)
            crate = s.crate_at_hold_point(2_000.0)
            s.run(0.05)
            self.assertEqual(s.gun.status, "can_grab" if held else "too_heavy")
            s.click()
            self.assertEqual(s.gun.held == crate, held)

    def test_punt_speed_and_recoil(self):
        for mass, speed, recoil in ((5.0, 30.0, 0.6 * 150.0 / 80.0), (500.0, 3.2, 8.0)):
            s = Scene()
            crate = s.crate_at_hold_point(mass)
            s.click()
            s.run(0.5)
            forward = s.player.basis()[2]
            v0, p0 = s.physics.get_velocity(crate)[0], s.physics.character_velocity(s.player.character)
            s.gun.punt(crate, forward)
            v1, p1 = s.physics.get_velocity(crate)[0], s.physics.character_velocity(s.player.character)
            self.assertIsNone(s.gun.held)
            self.assertAlmostEqual(sum((b - a) * f for a, b, f in zip(v0, v1, forward)), speed, delta=0.01 * speed)
            self.assertAlmostEqual(-sum((b - a) * f for a, b, f in zip(p0, p1, forward)), recoil, delta=0.01 * recoil)

    def charged(self, s, crate, seconds):
        """`crate` held, the punt control held for `seconds` and let go; its speed after."""
        s.controls.punt, s.controls.punt_held = True, True
        s.run(seconds)
        s.controls.punt_held = False
        s.run(STEP)
        return math.sqrt(sum(c * c for c in s.physics.get_velocity(crate)[0]))

    def test_charged_punt(self):
        s = Scene(MOON)
        crate = s.crate_at_hold_point(5.0)
        s.click()
        s.run(0.5)
        self.assertEqual(s.gun.held, crate)
        self.assertAlmostEqual(self.charged(s, crate, 1.6), s.gun.p.charged_speed, delta=2.0)
        self.assertIsNone(s.gun.held)
        # A charge within the cooldown (about 1.4 s after the last) is a plain punt; a click too.
        for seconds in (0.5, 0.1):
            s.physics.set_velocity(crate, (0.0, 0.0, 0.0))
            s.physics.set_transform(crate, s.gun.hold_point(s.player.eye(), s.player.basis()[2]),
                                    (1.0, 0.0, 0.0, 0.0))
            s.controls.grab = False
            s.run(0.1)
            s.click()
            s.run(0.3)
            self.assertEqual(s.gun.held, crate)
            self.assertAlmostEqual(self.charged(s, crate, seconds), s.gun.p.punt_speed, delta=1.0)

    def test_ground_punt_lifts_the_player(self):
        s = Scene()
        s.player.pitch = -math.radians(89.0)
        start = s.player.feet()[1]
        top = [start]
        s.controls.punt = True
        s.run(4.0, each=lambda: top.append(s.player.feet()[1]))
        rise = max(top) - start
        self.assertAlmostEqual(rise, gun_module.MAX_RECOIL ** 2 / (2.0 * PLANET[1]), delta=0.3)

    def test_rotate_turns_the_held_crate(self):
        s = Scene()
        crate = s.crate_at_hold_point(20.0)
        s.click()
        s.run(0.5)
        s.controls.rotate = (0.5 * math.pi / gun_module.ROTATE_RADIANS_PER_PIXEL, 0.0)
        s.run(1.5)
        # Turned a quarter about the view's up: the crate's X axis now along +Z or -Z.
        x = quat_rotate(s.physics.get_transform(crate)[1], (1.0, 0.0, 0.0))
        self.assertGreater(abs(x[2]), 0.97)

    def test_hold_distance_range(self):
        s = Scene()
        s.controls.distance = 100.0
        s.run(STEP)
        self.assertEqual(s.gun.hold_distance, gun_module.MAX_HOLD)
        s.controls.distance = -100.0
        s.run(STEP)
        self.assertEqual(s.gun.hold_distance, gun_module.MIN_HOLD)


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

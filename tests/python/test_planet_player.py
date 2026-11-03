"""Tests for the character on foot (shoonyakasha.planet.character), on
sk.PlanetPhysics in a Bubble over a flat mesh at the top of a sphere: the
jump's height on the planet and the moon, walking and running speeds,
turning the view, the jetpack and its fuel, being held off the terrain
mesh, and the eye's height standing, crouching and after a step up. Where
this interpreter's shoonyakasha has no extension, the tests run in a fresh
interpreter.

    python -m unittest discover -s tests/python
"""

import math
import os
import subprocess
import sys
import unittest

import numpy as np

import shoonyakasha as sk
from shoonyakasha.planet.bubble import Bubble
from shoonyakasha.planet.character import Controls, Player

STEP = 1.0 / 120.0
PLANET = (100_000.0, 4.905)         # radius (m), surface gravity (m/s^2)
MOON = (25_000.0, 0.981)


class Body:
    def __init__(self, radius):
        self.radius = radius

    def centre(self):
        return (0.0, 0.0, 0.0)

    def relative(self, point):
        return point


def flat_mesh(physics, columns=81, spacing=1.0, at=(0.0, 0.0, 0.0)):
    mid = (columns - 1) * 0.5
    points = np.zeros((columns, columns, 3), dtype=np.float32)
    for j in range(columns):
        for i in range(columns):
            points[j, i] = ((i - mid) * spacing, 0.0, -(j - mid) * spacing)
    return physics.create_grid_mesh(points, columns, columns, at)


class Scene:
    """A bubble at the top (+Y) of a sphere, a flat mesh 80 m across at its origin."""

    def __init__(self, body=PLANET):
        radius, g = body
        self.physics = sk.PlanetPhysics(max_bodies=64, threads=1)
        self.bubble = Bubble(self.physics, Body(radius), g * radius * radius, (0.0, radius, 0.0))
        self.mesh = flat_mesh(self.physics)
        self.player = Player(self.bubble, (0.0, 0.05, 0.0), (0.0, 0.0, -1.0))

    def run(self, seconds, controls=None, covered=True, each=None):
        controls = controls or Controls()
        for _ in range(int(round(seconds / STEP))):
            self.player.step(controls, STEP, covered)
            self.bubble.step(STEP)
            if each is not None:
                each()


class PlayerTest(unittest.TestCase):
    def setUp(self):
        if not sk.extension_available():
            if __name__ == "__main__":
                self.fail("no engine extension in a fresh interpreter")
            self.skipTest("runs in a fresh interpreter (test_in_fresh_interpreter)")

    def jump_height(self, body):
        scene = Scene(body)
        scene.run(0.5)
        self.assertTrue(scene.player.on_ground)
        highest = [0.0]
        scene.run(STEP, Controls(jump=True))
        scene.run(8.0, each=lambda: highest.__setitem__(0, max(highest[0], scene.player.feet()[1])))
        self.assertTrue(scene.player.on_ground)
        return highest[0]

    def test_jump_is_higher_on_the_moon(self):
        planet = self.jump_height(PLANET)
        moon = self.jump_height(MOON)
        self.assertAlmostEqual(planet, 3.3 ** 2 / (2 * 4.905), delta=0.08)
        self.assertAlmostEqual(moon, 3.3 ** 2 / (2 * 0.981), delta=0.25)

    def test_walk_and_run_speeds(self):
        scene = Scene()
        scene.run(0.5)
        scene.run(1.0, Controls(forward=1.0))
        x, _, z = scene.player.feet()
        start = z
        scene.run(1.0, Controls(forward=1.0))
        self.assertAlmostEqual(start - scene.player.feet()[2], 4.0, delta=0.1)     # heading -Z
        self.assertLess(abs(scene.player.feet()[0] - x), 0.01)
        start = scene.player.feet()[0]
        scene.run(0.5, Controls(strafe=1.0, run=True))
        scene.run(1.0, Controls(strafe=1.0, run=True))
        self.assertGreater(scene.player.feet()[0] - start, 9.0)
        self.assertTrue(scene.player.on_ground)

    def test_look_turns_the_heading_and_pitch(self):
        scene = Scene()
        p = scene.player
        p.look(-math.pi / 2 / 0.0025, 0.0)           # a quarter turn left: from -Z to -X
        right, up, forward = p.basis()
        np.testing.assert_allclose(forward, (-1.0, 0.0, 0.0), atol=1e-9)
        p.look(0.0, -10_000.0)                        # far past straight up
        _, _, forward = p.basis()
        self.assertAlmostEqual(math.degrees(math.asin(forward[1])), 89.0, places=6)
        self.assertAlmostEqual(sum(a * b for a, b in zip(right, up)), 0.0, places=12)

    def test_jetpack_climbs_and_burns_fuel(self):
        scene = Scene(MOON)
        scene.run(0.5)
        scene.run(STEP, Controls(jump=True))
        scene.run(0.5, Controls(jet=True))
        self.assertGreater(scene.player.feet()[1], 2.5)
        self.assertAlmostEqual(scene.player.jet_fuel, 1.0 - 0.25 * 0.5, delta=0.01)
        scene.run(30.0)
        self.assertTrue(scene.player.on_ground)
        self.assertGreater(scene.player.jet_fuel, 0.9)

    def test_held_off_the_terrain_mesh(self):
        scene = Scene()
        scene.player.physics.character_set_position(scene.player.character, (0.0, 5.0, 0.0))
        scene.run(1.0, covered=False)
        self.assertAlmostEqual(scene.player.feet()[1], 5.0, delta=1e-4)
        self.assertTrue(scene.player.held)

    def test_eye_height_crouch_and_step_up(self):
        scene = Scene()
        scene.run(1.0)
        p = scene.player
        self.assertAlmostEqual(p.eye()[1] - p.feet()[1], 1.65, delta=0.005)
        p.bob = False
        scene.run(1.0, Controls(crouch=True))
        self.assertAlmostEqual(p.eye()[1] - p.feet()[1], 1.0, delta=0.005)
        # A 0.3 m step 1 m ahead, 8 m long: walking onto it lowers the eye for a moment.
        box = scene.physics.create_box((2.0, 0.15, 4.0), (0.0, 0.15, -5.0), motion="static")
        self.assertTrue(scene.physics.contains(box))
        scene.run(1.0)
        lowest = [1.65]
        scene.run(1.5, Controls(forward=1.0),
                  each=lambda: lowest.__setitem__(0, min(lowest[0], p.eye()[1] - p.feet()[1])))
        self.assertGreater(p.feet()[1], 0.25)
        self.assertLess(lowest[0], 1.6)
        scene.run(1.0)
        self.assertAlmostEqual(p.eye()[1] - p.feet()[1], 1.65, delta=0.005)


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

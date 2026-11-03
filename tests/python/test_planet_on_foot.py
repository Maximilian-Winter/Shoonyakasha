"""Tests for the player on foot (shoonyakasha.planet.on_foot) with a stand-in
engine and world at the top of a planet: the gamepad stick's dead zone; the
controls read from keys, the mouse and the gamepad (a punt and a jump taken
once per press, the mouse turning the view only while captured, R turning a
held body instead); the bubble made on the body under a point, a player
spawned in it, held off the terrain mesh, walking at WALK on a flat mesh in
fixed steps with before_step called each step, standing still while not
active, and gone with the bubble, which calls on_drop; and the bubble
dropped without a player once the viewpoint is above its reach. Where this
interpreter's shoonyakasha has no extension, the tests run in a fresh
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
from shoonyakasha import keys
from shoonyakasha.planet import on_foot
from shoonyakasha.planet.orbits import Planet

RADIUS = 100_000.0
G = 4.905


class Body:
    name = "planet"
    index = 0
    radius = RADIUS
    heights = (-2_000.0, 6_000.0)

    def centre(self):
        return (0.0, 0.0, 0.0)

    def velocity(self):
        return (0.0, 0.0, 0.0)

    def relative(self, point):
        return point


class World:
    def __init__(self):
        self.planet = Body()
        self.moon = None
        self.bodies = [self.planet]
        self.physics = Planet(radius=RADIUS, surface_gravity=G, air_density=0.0)
        self.terrain_version = 0
        self.vegetation, self.vegetation_density, self.rocks = True, 1.0, True
        self.extra_probes, self.extra_heights = [], []

    def body_under(self, point):
        return self.planet

    def height_above_ground(self, point):
        return math.sqrt(sum(c * c for c in point)) - RADIUS


class FakeScene:
    def __getattr__(self, name):
        return lambda *args, **kwargs: None


class FakeEngine:
    def __init__(self):
        self.scene = FakeScene()

    def get_readback(self, name):
        return None

    def __getattr__(self, name):
        return lambda *args, **kwargs: None


class FakeInput:
    def __init__(self):
        self.keys, self.buttons, self.pad_buttons, self.axes = set(), set(), set(), {}
        self.captured, self.pad, self.delta = False, False, (0.0, 0.0)

    def is_key_down(self, key):
        return key in self.keys

    def is_mouse_captured(self):
        return self.captured

    def is_mouse_button_down(self, button):
        return button in self.buttons

    def is_gamepad_connected(self):
        return self.pad

    def get_gamepad_axis(self, axis):
        return self.axes.get(axis, -1.0 if axis in (keys.GAMEPAD_LEFT_TRIGGER, keys.GAMEPAD_RIGHT_TRIGGER) else 0.0)

    def is_gamepad_button_down(self, button):
        return button in self.pad_buttons

    def get_mouse_delta(self):
        return self.delta


def flat_mesh(physics, columns=81, spacing=1.0):
    mid = (columns - 1) * 0.5
    points = np.zeros((columns, columns, 3), dtype=np.float32)
    for j in range(columns):
        for i in range(columns):
            points[j, i] = ((i - mid) * spacing, 0.0, -(j - mid) * spacing)
    return physics.create_grid_mesh(points, columns, columns, (0.0, 0.0, 0.0))


class StickTest(unittest.TestCase):
    def test_dead_zone_cut_out_and_the_rest_stretched(self):
        self.assertEqual(on_foot.stick(on_foot.GAMEPAD_DEAD_ZONE), 0.0)
        self.assertEqual(on_foot.stick(-0.05), 0.0)
        self.assertAlmostEqual(on_foot.stick(1.0), 1.0)
        self.assertAlmostEqual(on_foot.stick(-1.0), -1.0)
        half = on_foot.GAMEPAD_DEAD_ZONE + 0.5 * (1.0 - on_foot.GAMEPAD_DEAD_ZONE)
        self.assertAlmostEqual(on_foot.stick(half), 0.5)


class OnFootTest(unittest.TestCase):
    def setUp(self):
        if not sk.extension_available():
            if __name__ == "__main__":
                self.fail("no engine extension in a fresh interpreter")
            self.skipTest("runs in a fresh interpreter (test_in_fresh_interpreter)")
        self.world = World()
        self.foot = on_foot.OnFoot(FakeEngine(), self.world, ([], []))
        self.top = (0.0, RADIUS, 0.0)

    def spawn_on_flat_mesh(self):
        bubble = self.foot.ensure_bubble(self.top)
        flat_mesh(self.foot.physics)
        self.foot.spawn((0.0, RADIUS + 0.05, 0.0), (0.0, 0.0, -1.0))
        return bubble

    def frames(self, seconds, active=True, before_step=None, frame=1.0 / 60.0):
        for _ in range(int(round(seconds / frame))):
            self.foot.step(frame, self.top, self.top, 0.05, active=active, before_step=before_step)

    def test_controls_from_keys_mouse_and_gamepad(self):
        foot, pad = self.foot, FakeInput()
        pad.keys = {keys.W, keys.D, keys.LEFT_SHIFT, keys.SPACE}
        foot.read_controls(pad)
        c = foot.controls
        self.assertEqual((c.forward, c.strafe, c.run, c.crouch, c.jet), (1.0, 1.0, True, False, True))
        self.assertFalse(foot.gun_controls.grab)
        pad.keys = set()
        pad.captured, pad.buttons = True, {keys.MOUSE_LEFT, keys.MOUSE_RIGHT}
        foot.read_controls(pad)
        self.assertTrue(foot.gun_controls.punt and foot.gun_controls.punt_held and foot.gun_controls.grab)
        foot.gun_controls.punt = False
        foot.read_controls(pad)
        self.assertFalse(foot.gun_controls.punt)        # once per press
        pad.captured, pad.buttons = False, set()
        foot.read_controls(pad)                         # the punt let go
        pad.pad, pad.pad_buttons = True, {keys.GAMEPAD_A}
        pad.axes = {keys.GAMEPAD_LEFT_Y: -1.0, keys.GAMEPAD_RIGHT_TRIGGER: 1.0}
        foot.read_controls(pad)
        self.assertTrue(c.jump and c.jet)
        self.assertEqual(c.forward, 1.0)
        self.assertTrue(foot.gun_controls.punt)
        c.jump = False
        foot.read_controls(pad)
        self.assertFalse(c.jump)                        # once per press

    def test_mouse_turns_the_view_only_while_captured_and_r_turns_the_held_body(self):
        self.spawn_on_flat_mesh()
        foot, pad = self.foot, FakeInput()
        heading = foot.player.heading
        pad.delta = (100.0, 0.0)
        foot.read_controls(pad)
        np.testing.assert_allclose(foot.player.heading, heading, atol=1e-12)
        self.assertEqual(foot.look_delta, (0.0, 0.0))
        pad.captured = True
        foot.read_controls(pad)
        self.assertNotEqual(foot.player.heading, heading)
        self.assertEqual(foot.look_delta, (100.0, 0.0))
        foot.gun.held = 1
        pad.keys = {keys.R}
        heading = foot.player.heading
        foot.read_controls(pad)
        np.testing.assert_allclose(foot.player.heading, heading, atol=1e-12)
        self.assertEqual(foot.gun_controls.rotate, (100.0, 0.0))
        foot.gun.held = None

    def test_player_held_off_the_mesh_walks_on_it_and_goes_with_the_bubble(self):
        bubble = self.spawn_on_flat_mesh()
        self.assertIs(bubble.body, self.world.planet)
        player = self.foot.player
        self.foot.controls.forward = 1.0
        self.frames(0.5)
        self.assertTrue(player.held)                    # no terrain mesh measured yet
        self.foot.fine.covers = lambda point, margin=0.0: True
        steps = []
        self.frames(1.0, before_step=lambda b, fraction: steps.append(fraction))
        self.assertFalse(player.held)
        self.assertAlmostEqual(len(steps), 120, delta=2)
        self.assertEqual(steps[-1], 1.0)
        start = player.feet()[2]
        self.frames(1.0)
        self.assertAlmostEqual(start - player.feet()[2], 4.0, delta=0.1)   # WALK along -Z
        self.frames(1.0, active=False)
        start = player.feet()[2]
        self.frames(0.5, active=False)
        self.assertLess(abs(player.feet()[2] - start), 0.01)
        eye = self.foot.place_eye()
        self.assertAlmostEqual(eye[1] - bubble.to_world(player.feet())[1], 1.65, delta=0.05)   # up is +Y here
        self.assertEqual(self.foot.max_level(self.world.planet), on_foot.FOOT_MAX_LEVEL)
        self.assertIsNone(self.foot.max_level(self.world.planet, active=False))
        dropped = []
        self.foot.on_drop = lambda: dropped.append(True)
        self.foot.drop_bubble()
        self.assertEqual(dropped, [True])
        self.assertIsNone(self.foot.player)
        self.assertIsNone(self.foot.bubble)
        self.assertIsNone(self.foot.view)

    def test_bubble_dropped_without_a_player_above_its_reach(self):
        self.foot.ensure_bubble(self.top)
        high = (0.0, RADIUS + 3_000.0, 0.0)
        self.foot.step(1.0 / 60.0, high, high, 0.05)
        self.assertIsNone(self.foot.bubble)


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

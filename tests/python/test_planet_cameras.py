"""Tests for the cameras of shoonyakasha.planet (cameras.py), driven by a
scripted mouse on a PlanetWorld that is not set up (no GPU).

    python -m unittest discover -s tests/python
"""

import math
import unittest

from shoonyakasha.planet import cameras, world as planet_world
from shoonyakasha.planet.vec import dot, length, normalize, sub

SIZE = (1600, 900)


class FakeInput:
    def __init__(self):
        self.position = (800.0, 450.0)
        self.delta = (0.0, 0.0)
        self.buttons = set()

    def is_mouse_button_down(self, button):
        return button in self.buttons

    def get_mouse_position(self):
        return self.position

    def get_mouse_delta(self):
        return self.delta

    def is_key_down(self, key):
        return False


class FakeEngine:
    def __init__(self):
        self.input = FakeInput()
        self.framebuffer_size = SIZE


def make_world():
    engine = FakeEngine()
    world = planet_world.PlanetWorld(engine)
    world.build_moon()
    return engine, world


def move(engine, camera, to, buttons, dt=1.0 / 60.0):
    """One frame with the pointer moved to `to` and `buttons` held."""
    old = engine.input.position
    engine.input.delta = (to[0] - old[0], to[1] - old[1])
    engine.input.position = to
    engine.input.buttons = set(buttons)
    camera.update(dt)


class CursorRayTest(unittest.TestCase):
    def test_centre_is_forward_and_corners_spread_by_the_field_of_view(self):
        basis = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, -1.0))
        self.assertAlmostEqual(dot(cameras.cursor_ray((800, 450), SIZE, basis, 70.0), basis[2]), 1.0)
        top = cameras.cursor_ray((800, 0), SIZE, basis, 70.0)
        self.assertAlmostEqual(math.degrees(math.acos(dot(top, basis[2]))), 35.0, places=6)
        right = cameras.cursor_ray((1600, 450), SIZE, basis, 70.0)
        self.assertAlmostEqual(right[0] / -right[2], math.tan(math.radians(35.0)) * 1600 / 900)

    def test_ray_sphere(self):
        self.assertAlmostEqual(cameras.ray_sphere((0, 0, 10), (0, 0, -1), (0, 0, 0), 2.0), 8.0)
        self.assertIsNone(cameras.ray_sphere((0, 0, 10), (0, 0, 1), (0, 0, 0), 2.0))
        self.assertIsNone(cameras.ray_sphere((0, 5, 10), (0, 0, -1), (0, 0, 0), 2.0))


class GlobeCameraTest(unittest.TestCase):
    def setUp(self):
        self.engine, self.world = make_world()
        self.camera = cameras.GlobeCamera(self.world, range_=300_000.0, tilt=math.radians(20.0))

    def test_basis_is_orthonormal_and_looks_at_the_target(self):
        for tilt in (0.0, 30.0, 85.0):
            self.camera.look_at(self.world.planet, self.camera.target, tilt=math.radians(tilt))
            right, up, forward = self.camera.basis
            for a, b in ((right, up), (up, forward), (forward, right)):
                self.assertAlmostEqual(dot(a, b), 0.0, places=9)
            for v in (right, up, forward):
                self.assertAlmostEqual(length(v), 1.0, places=9)
            to_target = normalize(sub(self.camera.target_point(), self.camera.position))
            self.assertAlmostEqual(dot(to_target, forward), 1.0, places=9)

    def test_dragging_keeps_the_grabbed_ground_under_the_pointer(self):
        start, end = (700.0, 500.0), (900.0, 380.0)
        move(self.engine, self.camera, start, [])
        grabbed = self.camera.hit(start)
        self.assertIsNotNone(grabbed)
        move(self.engine, self.camera, start, [0])
        steps = 10
        for k in range(1, steps + 1):
            t = k / steps
            move(self.engine, self.camera, (start[0] + (end[0] - start[0]) * t, start[1] + (end[1] - start[1]) * t), [0])
        self.assertGreater(dot(self.camera.hit(end), grabbed), math.cos(1e-6))

    def test_letting_go_while_moving_coasts_and_slows(self):
        move(self.engine, self.camera, (700.0, 450.0), [])
        move(self.engine, self.camera, (700.0, 450.0), [0])
        move(self.engine, self.camera, (720.0, 450.0), [0])
        move(self.engine, self.camera, (740.0, 450.0), [0])
        move(self.engine, self.camera, (740.0, 450.0), [])           # let go
        before = self.camera.target
        move(self.engine, self.camera, (740.0, 450.0), [])
        first = math.acos(min(1.0, dot(before, self.camera.target)))
        for _ in range(60):
            move(self.engine, self.camera, (740.0, 450.0), [])
        later_start = self.camera.target
        move(self.engine, self.camera, (740.0, 450.0), [])
        later = math.acos(min(1.0, dot(later_start, self.camera.target)))
        self.assertGreater(first, 0.0)
        self.assertLess(later, first * 0.1)

    def test_zoom_keeps_the_ground_under_the_pointer_near_it(self):
        pointer = (860.0, 490.0)
        move(self.engine, self.camera, pointer, [])
        before = self.camera.hit(pointer)
        old_range = self.camera.range
        self.camera.scroll(1.0)
        self.camera.place()
        self.assertAlmostEqual(self.camera.range, old_range * cameras.GlobeCamera.ZOOM_STEP)
        after = self.camera.hit(pointer)
        # The target moves 20 % of the way to the point; on a flat ground seen from above the
        # point would stay exactly; on the sphere it stays within a small angle.
        self.assertGreater(dot(before, after), math.cos(math.radians(0.5)))

    def test_right_drag_tilts_and_turns_within_limits(self):
        self.camera.look_at(self.world.planet, self.camera.target, heading=0.0, tilt=0.0)
        move(self.engine, self.camera, (800.0, 450.0), [1])
        move(self.engine, self.camera, (900.0, 350.0), [1])
        self.assertAlmostEqual(self.camera.heading, 100 * cameras.TURN_PER_PIXEL)
        self.assertAlmostEqual(self.camera.tilt, 100 * cameras.TURN_PER_PIXEL)
        move(self.engine, self.camera, (900.0, -5000.0), [1])
        self.assertAlmostEqual(self.camera.tilt, cameras.GlobeCamera.TILT_MAX)

    def test_double_click_flies_to_the_point(self):
        pointer = (850.0, 480.0)
        move(self.engine, self.camera, pointer, [])
        point = self.camera.hit(pointer)
        for buttons in ([0], [], [0], []):
            move(self.engine, self.camera, pointer, buttons)
        self.assertIsNotNone(self.camera.animation)
        for _ in range(200):
            move(self.engine, self.camera, pointer, [])
        self.assertIsNone(self.camera.animation)
        self.assertLess(math.acos(min(1.0, dot(self.camera.target, point))), 1e-6)


class FocusCameraTest(unittest.TestCase):
    def setUp(self):
        self.engine, self.world = make_world()
        self.camera = cameras.FocusCamera(self.world, distance=1_500_000.0)

    def pixel_of(self, point):
        """Where `point` (planet space) is on the screen."""
        right, up, forward = self.camera.basis
        d = sub(point, self.camera.position)
        t = math.tan(math.radians(self.world.fov) * 0.5)
        x = dot(d, right) / dot(d, forward) / (t * SIZE[0] / SIZE[1])
        y = dot(d, up) / dot(d, forward) / t
        return ((x + 1.0) * 0.5 * SIZE[0], (1.0 - y) * 0.5 * SIZE[1])

    def test_clicking_a_body_focuses_it(self):
        moon = self.world.moon
        self.camera.yaw, self.camera.pitch = 0.3, 0.2
        self.camera.place()
        pointer = self.pixel_of(moon.centre())
        self.assertTrue(0 <= pointer[0] <= SIZE[0] and 0 <= pointer[1] <= SIZE[1])
        self.assertIs(self.camera.pick(pointer), moon)
        move(self.engine, self.camera, pointer, [])
        move(self.engine, self.camera, pointer, [0])
        move(self.engine, self.camera, pointer, [])
        self.assertIs(self.camera.body, moon)
        self.assertIsNotNone(self.camera.transition)

    def test_dragging_does_not_count_as_a_click(self):
        self.camera.place()
        pointer = self.pixel_of(self.world.planet.centre())
        move(self.engine, self.camera, pointer, [])
        move(self.engine, self.camera, pointer, [0])
        move(self.engine, self.camera, (pointer[0] + 40.0, pointer[1]), [0])
        move(self.engine, self.camera, (pointer[0] + 40.0, pointer[1]), [])
        self.assertIsNone(self.camera.transition)

    def test_focus_ends_three_radii_away(self):
        moon = self.world.moon
        self.camera.focus(moon)
        for _ in range(120):
            self.camera.update(1.0 / 60.0)
        self.assertIsNone(self.camera.transition)
        self.assertAlmostEqual(self.camera.distance, 3.0 * moon.radius, delta=1e-6 * moon.radius)
        self.assertAlmostEqual(length(sub(self.camera.position, moon.centre())), 3.0 * moon.radius,
                               delta=1e-3)

    def test_zoom_stays_above_the_ground_and_within_reach(self):
        for _ in range(100):
            self.camera.scroll(1.0)
        self.assertGreaterEqual(self.camera.distance, self.world.planet.radius)
        for _ in range(200):
            self.camera.scroll(-1.0)
        self.assertLessEqual(self.camera.distance,
                             cameras.FocusCamera.MAX_DISTANCE_RADII * self.world.planet.radius)


if __name__ == "__main__":
    unittest.main()

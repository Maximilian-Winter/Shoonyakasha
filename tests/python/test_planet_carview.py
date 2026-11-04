"""Tests for the walker's view of its flying car
(examples/python/simulations/planet/carview.py) and the HUD's driving
hints (hud.py): the chase camera behind and above the car looking at it,
farther with speed, lagging a turn and catching up, turned by the mouse
and easing back behind it, kept above the ground; the field of view
widening with speed; the sticks' dead zone; and the hints while driving.
Plain Python: no engine needed.

    python -m unittest discover -s tests/python
"""

import math
import sys
import unittest
from pathlib import Path

PLANET_DIR = Path(__file__).resolve().parents[2] / "examples" / "python" / "simulations" / "planet"
sys.path.insert(0, str(PLANET_DIR))

import carview  # noqa: E402
import hovercar  # noqa: E402
import hud  # noqa: E402
from shoonyakasha.planet.vec import dot, length, normalize, sub  # noqa: E402

UP = (0.0, 0.0, 1.0)
NORTH = (0.0, 1.0, 0.0)
EAST = (1.0, 0.0, 0.0)
CENTRE = (0.0, 0.0, 100_000.0)


def settle(camera, heading=NORTH, speed=0.0, seconds=3.0, ground=None):
    eye = basis = None
    for _ in range(int(seconds * 60)):
        eye, basis = camera.update(1.0 / 60.0, CENTRE, heading, UP, speed, ground)
    return eye, basis


class ChaseTest(unittest.TestCase):
    def test_behind_above_and_looking_at_the_car(self):
        eye, (right, up, forward) = settle(carview.ChaseCamera())
        offset = sub(eye, CENTRE)
        self.assertLess(dot(offset, NORTH), 0.0)                  # behind
        self.assertGreater(dot(offset, UP), 0.0)                  # above
        self.assertAlmostEqual(length(offset), carview.CHASE_DISTANCE, delta=1e-6)
        target = normalize(sub((CENTRE[0], CENTRE[1], CENTRE[2] + carview.LOOK_LIFT), eye))
        self.assertAlmostEqual(dot(forward, target), 1.0, places=9)
        self.assertGreater(dot(right, EAST), 0.99)

    def test_farther_with_speed(self):
        eye, _ = settle(carview.ChaseCamera(), speed=50.0)
        self.assertAlmostEqual(length(sub(eye, CENTRE)), carview.CHASE_DISTANCE + 50.0 * carview.CHASE_STRETCH,
                               delta=1e-6)

    def test_lags_a_turn_then_catches_up(self):
        camera = carview.ChaseCamera()
        settle(camera)
        eye, _ = camera.update(1.0 / 60.0, CENTRE, EAST, UP, 0.0)
        self.assertLess(dot(sub(eye, CENTRE), EAST), 0.0)          # starting to swing west
        self.assertLess(dot(sub(eye, CENTRE), NORTH), -5.0)        # but still mostly south
        eye, _ = settle(camera, heading=EAST)
        self.assertLess(dot(sub(eye, CENTRE), EAST), -10.0)        # behind the car again: west of it
        self.assertAlmostEqual(dot(sub(eye, CENTRE), NORTH), 0.0, delta=1e-3)

    def test_orbit_and_return(self):
        camera = carview.ChaseCamera()
        settle(camera)
        camera.orbit(math.pi / 2 / carview.ORBIT_PER_PIXEL, 0.0)  # a quarter turn
        eye, _ = settle(camera, seconds=0.5)
        self.assertLess(dot(sub(eye, CENTRE), EAST), -5.0)         # moved round to the car's left
        eye, _ = settle(camera, seconds=carview.ORBIT_RETURN + 6.0 * carview.RETURN_EASE)
        self.assertLess(dot(sub(eye, CENTRE), NORTH), -10.0)       # back behind it
        camera.orbit(0.0, 1e6)
        self.assertEqual(camera.pitch, carview.MAX_PITCH)

    def test_kept_above_the_ground(self):
        def high_ground(point):
            return 0.2                                             # 0.2 m above the ground everywhere
        eye, _ = settle(carview.ChaseCamera(), ground=high_ground)
        plain, _ = settle(carview.ChaseCamera())
        self.assertAlmostEqual(eye[2] - plain[2], carview.CLEARANCE - 0.2, delta=1e-6)


class ViewTest(unittest.TestCase):
    def test_fov_widens_with_speed(self):
        self.assertEqual(carview.fov(70.0, 0.0), 70.0)
        self.assertEqual(carview.fov(70.0, 1e6), 70.0 + carview.FOV_GAIN)
        middle = carview.fov(70.0, 0.5 * hovercar.MAX_SPEED * hovercar.BOOST)
        self.assertAlmostEqual(middle, 70.0 + 0.5 * carview.FOV_GAIN)

    def test_stick_dead_zone(self):
        self.assertEqual(carview.stick(0.1), 0.0)
        self.assertEqual(carview.stick(1.0), 1.0)
        self.assertEqual(carview.stick(-1.0), -1.0)
        self.assertAlmostEqual(carview.stick(0.56), (0.56 - carview.DEAD_ZONE) / (1.0 - carview.DEAD_ZONE))

    def test_drive_hints(self):
        keys = [h[0] for h in hud.drive_hints()]
        self.assertEqual(keys, [("W", "S"), ("A", "D"), ("Space", "Ctrl"), ("Shift",)])
        self.assertEqual(hud.drive_hints(can_leave=True)[-1], (("F",), "Get out"))
        self.assertEqual(hud.drive_hints(pad=True, can_leave=True)[-1], (("pad:X",), "Get out"))


if __name__ == "__main__":
    unittest.main()

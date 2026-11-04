"""Tests for the walker's flying car
(examples/python/simulations/planet/hovercar.py) over a stand-in ground:
settling on the cushion over flat ground, rising before a hill and over
it, climbing to a target and the height's limits, holding its height
where the ground is not known, never below the floor, top speeds forward,
with boost and backward, braking, turning (slower when still), grip on
the ground and drift in the air, banking into a turn, and when it can be
left. Plain Python: no engine needed.

    python -m unittest discover -s tests/python
"""

import math
import sys
import unittest
from pathlib import Path

PLANET_DIR = Path(__file__).resolve().parents[2] / "examples" / "python" / "simulations" / "planet"
sys.path.insert(0, str(PLANET_DIR))

import hovercar as hc  # noqa: E402
from shoonyakasha.planet.vec import dot, length, normalize, sub  # noqa: E402

RADIUS = 100_000.0
START = (0.0, 0.0, RADIUS + 1.0)     # north (0, 1, 0), east (1, 0, 0) here
NORTH = (0.0, 1.0, 0.0)


def flat(point):
    return length(point) - RADIUS


def hill(point):
    """A ramp from 200 m north rising 30 m over 40 m, flat beyond."""
    north = RADIUS * math.atan2(point[1], point[2])
    return length(point) - RADIUS - 30.0 * min(1.0, max(0.0, (north - 200.0) / 40.0))


def unknown(point):
    return None


def run(car, controls, seconds, ground=flat):
    car.step(seconds, controls, ground)
    return car


def north_of(car):
    return RADIUS * math.atan2(car.position[1], car.position[2])


class HeightTest(unittest.TestCase):
    def test_settles_on_the_cushion(self):
        car = run(hc.Car(START, NORTH), hc.CarControls(), 6.0)
        self.assertAlmostEqual(car.height, hc.HOVER_MIN, delta=0.03)
        self.assertLess(abs(dot(car.velocity, car.up())), 0.05)

    def test_rises_before_a_hill_and_over_it(self):
        car = hc.Car((0.0, 0.0, RADIUS + hc.HOVER_MIN), NORTH)
        controls = hc.CarControls(throttle=0.6)
        lowest = math.inf
        for _ in range(1200):
            car.step(1.0 / 120.0, controls, hill)
            lowest = min(lowest, hill(car.position))
            if north_of(car) > 280.0:
                break
        self.assertGreater(north_of(car), 240.0)
        self.assertGreaterEqual(lowest, hc.FLOOR - 1e-6)
        run(car, hc.CarControls(), 4.0, hill)
        self.assertAlmostEqual(car.height, hc.HOVER_MIN, delta=0.1)

    def test_climbs_to_its_target_within_limits(self):
        car = run(hc.Car(START, NORTH), hc.CarControls(climb=1.0), 2.0)
        self.assertAlmostEqual(car.target, hc.HOVER_MIN + 2.0 * hc.CLIMB_RATE, delta=0.01)
        run(car, hc.CarControls(), 8.0)
        self.assertAlmostEqual(car.height, car.target, delta=0.3)
        run(car, hc.CarControls(climb=1.0), 60.0)
        self.assertEqual(car.target, hc.MAX_HEIGHT)
        self.assertLessEqual(abs(dot(car.velocity, car.up())), hc.MAX_CLIMB + 1e-9)
        run(car, hc.CarControls(climb=-1.0), 60.0)
        self.assertEqual(car.target, hc.HOVER_MIN)

    def test_holds_its_height_where_the_ground_is_unknown(self):
        car = hc.Car(START, NORTH)
        car.velocity = (0.0, 0.0, 5.0)
        run(car, hc.CarControls(), 3.0, unknown)
        self.assertLess(dot(car.velocity, car.up()), 5.0 * math.exp(-hc.HOLD_DAMPING * 3.0) + 1e-6)
        self.assertGreater(length(car.position), RADIUS + 1.0)

    def test_never_below_the_floor(self):
        car = hc.Car((0.0, 0.0, RADIUS + 5.0), NORTH)
        car.velocity = (0.0, 0.0, -60.0)
        for _ in range(240):
            car.step(1.0 / 120.0, hc.CarControls(), flat)
            self.assertGreaterEqual(flat(car.position), hc.FLOOR - 1e-6)


class DriveTest(unittest.TestCase):
    def forward_speed(self, car):
        return dot(car.velocity, car.heading)

    def test_top_speeds(self):
        car = run(hc.Car(START, NORTH), hc.CarControls(throttle=1.0), 40.0)
        self.assertAlmostEqual(self.forward_speed(car), hc.MAX_SPEED, delta=1.0)
        run(car, hc.CarControls(throttle=1.0, boost=True), 40.0)
        self.assertAlmostEqual(self.forward_speed(car), hc.MAX_SPEED * hc.BOOST, delta=1.5)
        back = run(hc.Car(START, NORTH), hc.CarControls(throttle=-1.0), 40.0)
        self.assertAlmostEqual(self.forward_speed(back), -hc.REVERSE_SHARE * hc.MAX_SPEED, delta=0.5)

    def test_braking_stops_sooner_than_coasting(self):
        coast = run(hc.Car(START, NORTH), hc.CarControls(throttle=1.0), 10.0)
        brake = run(hc.Car(START, NORTH), hc.CarControls(throttle=1.0), 10.0)
        run(coast, hc.CarControls(), 2.0)
        run(brake, hc.CarControls(throttle=-1.0), 2.0)
        self.assertLess(self.forward_speed(brake), 0.5 * self.forward_speed(coast))

    def test_turns_right_and_slower_when_still(self):
        still = run(hc.Car(START, NORTH), hc.CarControls(steer=1.0), 1.0)
        moving = hc.Car(START, NORTH)
        moving.velocity = (0.0, 30.0, 0.0)
        run(moving, hc.CarControls(throttle=0.5, steer=1.0), 1.0)
        self.assertGreater(still.heading[0], 0.0)          # towards east: right of north
        angle = lambda car: math.acos(max(-1.0, min(1.0, dot(car.heading, NORTH))))  # noqa: E731
        self.assertGreater(angle(moving), 1.8 * angle(still))
        self.assertGreater(moving.bank, 0.0)               # into the turn

    def test_drifts_more_in_the_air(self):
        def sideways_after(height):
            car = hc.Car((0.0, 0.0, RADIUS + height), NORTH, target=height)
            car.height = height
            car.velocity = (10.0, 0.0, 0.0)                 # all sideways
            run(car, hc.CarControls(), 0.5)
            return abs(dot(car.velocity, car.right()))
        self.assertLess(sideways_after(hc.HOVER_MIN), 0.5 * sideways_after(100.0))

    def test_can_leave_only_slow_and_low(self):
        car = run(hc.Car(START, NORTH), hc.CarControls(), 5.0)
        self.assertTrue(car.can_leave())
        run(car, hc.CarControls(throttle=1.0), 2.0)
        self.assertFalse(car.can_leave())
        high = run(hc.Car(START, NORTH, target=50.0), hc.CarControls(), 15.0)
        self.assertFalse(high.can_leave())

    def test_attitude_is_orthonormal(self):
        car = hc.Car(START, NORTH)
        car.bank, car.pitch = 0.3, -0.1
        right, up, forward = car.attitude()
        for a in (right, up, forward):
            self.assertAlmostEqual(length(a), 1.0, places=9)
        self.assertAlmostEqual(dot(right, up), 0.0, places=9)
        self.assertAlmostEqual(dot(up, forward), 0.0, places=9)
        self.assertLess(dot(right, normalize(START)), 0.0)  # the right side dipped


if __name__ == "__main__":
    unittest.main()

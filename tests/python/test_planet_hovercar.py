"""Tests for the walker's flying car
(examples/python/simulations/planet/hovercar.py) over a stand-in ground:
settling on the cushion over flat ground, rising before a hill and over
it, climbing to a target and the height's limits, holding its height
where the ground is not known, never below the floor, top speeds forward,
with boost and backward, braking, turning (slower when still), grip on
the ground and drift in the air, the wind moving it in the air but not
on its cushion and slowing it into a headwind, banking into a turn, when
it can be left, and its fuel: none burnt on the cushion, the throttle's and the
boost's and holding height's burn, and dry, settling onto the cushion
without driving but steering; the hold, taking rocks within its mass
and its volume; and space mode: its ship's parameters, the rocket
equation's delta-v, enough for one way to the moon, and hovering on the
assist in the planet's gravity. Plain Python: no engine needed.

    python -m unittest discover -s tests/python
"""

import math
import sys
import unittest
from pathlib import Path

PLANET_DIR = Path(__file__).resolve().parents[2] / "examples" / "python" / "simulations" / "planet"
sys.path.insert(0, str(PLANET_DIR))

import flight  # noqa: E402
import hovercar as hc  # noqa: E402
from shoonyakasha.planet.vec import quat_from_basis  # noqa: E402
from shoonyakasha.planet.resources import ICE  # noqa: E402
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

    def test_the_wind_moves_it_in_the_air_not_on_its_cushion(self):
        east = (1.0, 0.0, 0.0)

        def blown(point):
            return (15.0, 0.0, 0.0)                         # towards east: across the nose

        def east_after(height):
            car = hc.Car((0.0, 0.0, RADIUS + height), NORTH, target=height)
            car.height = height
            car.step(10.0, hc.CarControls(), flat, wind=blown)
            return dot(car.velocity, east)
        self.assertEqual(east_after(hc.HOVER_MIN), 0.0)
        self.assertGreater(east_after(100.0), 0.1)
        self.assertLess(east_after(100.0), 15.0)

    def test_a_headwind_lowers_its_top_speed(self):
        def top(wind):
            car = hc.Car((0.0, 0.0, RADIUS + 100.0), NORTH, target=100.0)
            car.height = 100.0
            car.step(30.0, hc.CarControls(throttle=1.0), flat, wind=wind)
            return dot(car.velocity, NORTH)
        still = top(None)
        self.assertLess(top(lambda point: (0.0, -20.0, 0.0)), still - 0.5)
        self.assertEqual(top(lambda point: (0.0, 0.0, 0.0)), still)

    def test_can_leave_only_slow_and_low(self):
        car = run(hc.Car(START, NORTH), hc.CarControls(), 5.0)
        self.assertTrue(car.can_leave())
        run(car, hc.CarControls(throttle=1.0), 2.0)
        self.assertFalse(car.can_leave())
        high = run(hc.Car(START, NORTH, target=50.0), hc.CarControls(), 15.0)
        self.assertFalse(high.can_leave())

    def test_burns_fuel_and_settles_when_dry(self):
        p = hc.CarParams()
        cushion = (0.0, 0.0, RADIUS + hc.HOVER_MIN)
        parked = run(hc.Car(cushion, NORTH), hc.CarControls(), 5.0)
        self.assertAlmostEqual(parked.fuel, p.fuel_capacity, places=6)  # none on the cushion
        driven = run(hc.Car(cushion, NORTH), hc.CarControls(throttle=1.0), 10.0)
        # (a little more: over the curving ground the car rises a few mm off its cushion)
        self.assertAlmostEqual(p.fuel_capacity - driven.fuel, 10.0 * p.drive_burn, delta=0.001)
        boosted = run(hc.Car(cushion, NORTH), hc.CarControls(throttle=1.0, boost=True), 10.0)
        self.assertAlmostEqual(p.fuel_capacity - boosted.fuel, 10.0 * p.drive_burn * p.boost_burn, delta=0.002)
        high = hc.Car((0.0, 0.0, RADIUS + 100.0), NORTH, target=100.0)
        high.height = 100.0
        run(high, hc.CarControls(), 10.0)
        self.assertAlmostEqual(p.fuel_capacity - high.fuel, 10.0 * p.air_burn, delta=0.01)
        # Dry: no drive, no climb, down onto the cushion, still steering.
        dry = hc.Car((0.0, 0.0, RADIUS + 30.0), NORTH, target=30.0, fuel=0.0)
        dry.height = 30.0
        run(dry, hc.CarControls(throttle=1.0, climb=1.0, steer=1.0), 20.0)
        self.assertTrue(dry.dry())
        self.assertLess(abs(self.forward_speed(dry)), 0.5)
        self.assertAlmostEqual(dry.height, hc.HOVER_MIN, delta=0.1)
        turned = run(hc.Car(cushion, NORTH, fuel=0.0), hc.CarControls(steer=1.0), 1.0)
        self.assertGreater(turned.heading[0], 0.0)                       # towards east

    def test_the_hold_takes_rocks_within_its_mass_and_volume(self):
        car = hc.Car(START, NORTH)
        p = car.params
        car.cargo = [{"mass": p.cargo_capacity - 100.0, "resource": hc.FERRITE}]
        self.assertTrue(car.can_take(100.0, hc.FERRITE))
        self.assertFalse(car.can_take(100.5, hc.FERRITE))
        self.assertAlmostEqual(car.cargo_volume, hc.load_volume(p.cargo_capacity - 100.0, hc.FERRITE))
        car.cargo = []
        ice = p.hold_m3 / hc.load_volume(1.0, ICE)                     # kg of ice that fill the hold
        self.assertLess(ice, p.cargo_capacity)
        self.assertTrue(car.can_take(ice - 1.0, ICE))
        self.assertFalse(car.can_take(ice + 1.0, ICE))
        # A crate fills its own volume.
        car.cargo = [{"mass": 60.0, "volume": 0.6 * p.hold_m3}]
        self.assertAlmostEqual(car.cargo_volume, 0.6 * p.hold_m3)
        self.assertTrue(car.can_take(60.0, None, 0.39 * p.hold_m3))
        self.assertFalse(car.can_take(60.0, None, 0.41 * p.hold_m3))

    def test_attitude_is_orthonormal(self):
        car = hc.Car(START, NORTH)
        car.bank, car.pitch = 0.3, -0.1
        right, up, forward = car.attitude()
        for a in (right, up, forward):
            self.assertAlmostEqual(length(a), 1.0, places=9)
        self.assertAlmostEqual(dot(right, up), 0.0, places=9)
        self.assertAlmostEqual(dot(up, forward), 0.0, places=9)
        self.assertLess(dot(right, normalize(START)), 0.0)  # the right side dipped



class SpaceTest(unittest.TestCase):
    HALF = (1.452, 0.756, 3.097)

    def test_its_ships_parameters(self):
        p = hc.CarParams()
        s = hc.space_params(p, self.HALF)
        self.assertEqual((s.dry_mass, s.fuel_capacity, s.isp_main, s.isp_rcs), (p.dry_mass, p.fuel_capacity,
                                                                                p.space_isp, p.space_isp))
        self.assertEqual((s.main_thrust, s.rcs_thrust), (p.main_thrust, p.rcs_thrust))
        self.assertEqual(len(s.gear), 4)
        self.assertTrue(all(g[1] == -self.HALF[1] for g in s.gear))
        self.assertTrue(all(abs(g[0]) < self.HALF[0] and abs(g[2]) < self.HALF[2] for g in s.gear))
        self.assertAlmostEqual(s.inertia[1], p.dry_mass / 3.0 * (self.HALF[0] ** 2 + self.HALF[2] ** 2))

    def test_delta_v(self):
        p = hc.CarParams()
        self.assertAlmostEqual(hc.delta_v(p, 100.0), p.space_isp * flight.G0 * math.log(1.0 + 100.0 / p.dry_mass))
        self.assertLess(hc.delta_v(p, 100.0, cargo=500.0), hc.delta_v(p, 100.0))
        self.assertEqual(hc.delta_v(p, 0.0), 0.0)

    def test_enough_for_one_way_to_the_moon(self):
        """A full stock tank: to a 20 km orbit (its speed, a fifth more for gravity and turning), the
        Hohmann transfer's first burn to the moon's distance, and the moon's circular speed at its surface
        to land."""
        planet = flight.Planet()
        moon = flight.Moon()
        r1 = planet.radius + 20_000.0
        orbit = flight.circular_speed(planet, 20_000.0) * 1.2
        transfer = math.sqrt(planet.gm / r1) * (math.sqrt(2.0 * moon.distance / (r1 + moon.distance)) - 1.0)
        land = math.sqrt(moon.gm / moon.radius)
        p = hc.CarParams()
        self.assertGreater(hc.delta_v(p, p.fuel_capacity), orbit + transfer + land)

    def test_hovers_on_the_assist_in_the_planets_gravity(self):
        planet = flight.Planet()
        position = (0.0, 0.0, planet.radius + 500.0)
        right, up, back = (0.0, -1.0, 0.0), (0.0, 0.0, 1.0), (-1.0, 0.0, 0.0)
        ship = flight.Ship(position, orientation=quat_from_basis(right, up, back),
                           params=hc.space_params(hc.CarParams(), self.HALF))
        for _ in range(int(round(10.0 * 120))):
            ship.step(planet, flight.Controls(assist=True), 1.0 / 120.0)
        self.assertAlmostEqual(length(ship.position) - planet.radius, 500.0, delta=2.0)
        self.assertLess(ship.fuel, hc.CarParams().fuel_capacity)

class OreByResourceTest(unittest.TestCase):
    def test_summed_by_resource_heaviest_first(self):
        cargo = [{"mass": 10.0, "grade": 0.5, "resource": ICE}, {"mass": 30.0, "grade": 1.0, "resource": ICE},
                 {"mass": 25.0, "grade": 0.8}, {"mass": 60.0, "grade": 1.0, "contract": "c1"}]
        rows = hc.ore_by_resource(cargo)
        self.assertEqual([(r, kg) for r, kg, _, _ in rows], [(ICE, 40.0), (hc.FERRITE, 25.0)])
        self.assertAlmostEqual(rows[0][2], (10.0 * 0.5 + 30.0 * 1.0) / 40.0)
        self.assertEqual(len(rows[0][3]), 2)
        self.assertEqual(hc.ore_by_resource([]), [])


if __name__ == "__main__":
    unittest.main()

"""Tests for the planet example's flight model
(examples/python/simulations/planet/flight.py), loaded from its file.

    python -m unittest discover -s tests/python
"""

import importlib.util
import math
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
_SPEC = importlib.util.spec_from_file_location(
    "planet_flight", REPO / "examples" / "python" / "simulations" / "planet" / "flight.py")
flight = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(flight)

DT = 1.0 / 120.0


def run(ship, planet, controls, seconds, dt=DT):
    for _ in range(int(round(seconds / dt))):
        ship.step(planet, controls, dt)


def level_ship(height, planet, speed=0.0):
    """A ship `height` metres above the radius over the +Z pole, nose along
    +X, level, moving forward at `speed`."""
    position = (0.0, 0.0, planet.radius + height)
    right, up, back = (0.0, -1.0, 0.0), (0.0, 0.0, 1.0), (-1.0, 0.0, 0.0)   # right x up == back
    return flight.Ship(position=position, velocity=(speed, 0.0, 0.0),
                       orientation=flight.quat_from_basis(right, up, back))


class Quaternions(unittest.TestCase):
    def test_a_basis_turns_into_a_quaternion_that_turns_the_axes_back_onto_it(self):
        right, up, back = (0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (1.0, 0.0, 0.0)
        q = flight.quat_from_basis(right, up, back)
        for axis, expected in (((1, 0, 0), right), ((0, 1, 0), up), ((0, 0, 1), back)):
            for got, want in zip(flight.quat_rotate(q, axis), expected):
                self.assertAlmostEqual(got, want, places=12)

    def test_the_ships_axes_follow_its_orientation(self):
        ship = level_ship(0.0, flight.Planet())
        right, up, forward = ship.axes()
        for got, want in zip(forward, (1.0, 0.0, 0.0)):
            self.assertAlmostEqual(got, want, places=12)
        for got, want in zip(up, (0.0, 0.0, 1.0)):
            self.assertAlmostEqual(got, want, places=12)


class Orbits(unittest.TestCase):
    def test_a_circular_orbit_above_the_air_holds_for_five_minutes(self):
        planet = flight.Planet()
        height = 40_000.0
        ship = level_ship(height, planet, speed=flight.circular_speed(planet, height))
        coast = flight.Controls(assist=False)
        energy0 = 0.5 * flight.dot(ship.velocity, ship.velocity) - planet.gm / flight.length(ship.position)
        radii = []
        for _ in range(300):
            run(ship, planet, coast, 1.0)
            radii.append(flight.length(ship.position) - planet.radius)
        energy = 0.5 * flight.dot(ship.velocity, ship.velocity) - planet.gm / flight.length(ship.position)
        self.assertLess(max(abs(r - height) for r in radii), 50.0)
        self.assertLess(abs(energy - energy0) / abs(energy0), 1e-4)

    def test_the_orbit_of_a_circular_motion_is_circular(self):
        planet = flight.Planet()
        ship = level_ship(40_000.0, planet, speed=flight.circular_speed(planet, 40_000.0))
        periapsis, apoapsis = flight.orbit(planet, ship.position, ship.velocity)
        self.assertAlmostEqual(periapsis, 40_000.0, delta=1e-3)
        self.assertAlmostEqual(apoapsis, 40_000.0, delta=1e-3)

    def test_escape_speed_has_no_apoapsis(self):
        planet = flight.Planet()
        ship = level_ship(0.0, planet, speed=math.sqrt(2.0 * planet.gm / planet.radius) * 1.01)
        self.assertEqual(flight.orbit(planet, ship.position, ship.velocity)[1], math.inf)


class OrbitPath(unittest.TestCase):
    def assert_close(self, a, b, delta):
        self.assertLess(flight.length(flight.sub(a, b)), delta)

    def test_a_circular_orbit_is_a_circle_starting_at_the_ship(self):
        planet = flight.Planet()
        ship = level_ship(40_000.0, planet, speed=flight.circular_speed(planet, 40_000.0))
        points = flight.orbit_path(planet, ship.position, ship.velocity, count=64)
        self.assertEqual(len(points), 64)
        self.assert_close(points[0], ship.position, 1e-3)
        self.assert_close(points[-1], ship.position, 1e-3)
        for p in points:
            self.assertAlmostEqual(flight.length(p), planet.radius + 40_000.0, delta=1e-3)

    def test_an_ellipse_stays_between_its_periapsis_and_apoapsis_and_runs_the_ships_way(self):
        planet = flight.Planet()
        ship = level_ship(20_000.0, planet, speed=flight.circular_speed(planet, 20_000.0) * 1.15)
        ship.velocity = flight.add(ship.velocity, (0.0, 0.0, 60.0))      # climbing too
        periapsis, apoapsis = flight.orbit(planet, ship.position, ship.velocity)
        points = flight.orbit_path(planet, ship.position, ship.velocity)
        self.assert_close(points[0], ship.position, 1e-3)
        self.assertGreater(flight.dot(flight.sub(points[1], points[0]), ship.velocity), 0.0)
        heights = [flight.length(p) - planet.radius for p in points]
        self.assertAlmostEqual(min(heights), periapsis, delta=50.0)
        self.assertAlmostEqual(max(heights), apoapsis, delta=50.0)
        for p in points:       # every point lies in the plane of the motion
            self.assertAlmostEqual(flight.dot(flight.normalize(p),
                                              flight.normalize(flight.cross(ship.position, ship.velocity))), 0.0,
                                   places=9)

    def test_an_escape_runs_out_to_the_reach(self):
        planet = flight.Planet()
        escape = math.sqrt(2.0 * planet.gm / (planet.radius + 1_000.0)) * 1.2
        ship = level_ship(1_000.0, planet, speed=escape)
        points = flight.orbit_path(planet, ship.position, ship.velocity, reach=10.0)
        self.assert_close(points[0], ship.position, 1e-3)
        distances = [flight.length(p) for p in points]
        self.assertTrue(all(b >= a for a, b in zip(distances, distances[1:])))
        self.assertAlmostEqual(distances[-1], 10.0 * (planet.radius + 1_000.0), delta=1.0)

    def test_a_slow_nearly_vertical_descent_is_a_thin_ellipse(self):
        planet = flight.Planet()
        for velocity in ((1e-4, 0.0, -6.0), (0.02, 0.0, -0.5), (3e-7, 1e-7, -2.0)):
            ship = level_ship(30.0, planet)
            ship.velocity = velocity
            points = flight.orbit_path(planet, ship.position, ship.velocity)
            self.assertEqual(len(points), 256)
            self.assert_close(points[0], ship.position, 1e-3)
            for p in points:
                self.assertTrue(all(math.isfinite(c) for c in p))
                self.assertLessEqual(flight.length(p), flight.length(ship.position) + 1.0)

    def test_moving_straight_up_has_no_path(self):
        planet = flight.Planet()
        ship = level_ship(1_000.0, planet)
        ship.velocity = (0.0, 0.0, 300.0)
        self.assertEqual(flight.orbit_path(planet, ship.position, ship.velocity), [])


class ClimbToOrbit(unittest.TestCase):
    """A scripted ascent with assist off: burn at 60 degrees until the
    apoapsis is 40 km up, coast to it, burn along the velocity until the
    periapsis is above the air, then coast five minutes."""

    def point(self, ship, forward):
        radial = flight.normalize(ship.position)
        right = flight.normalize(flight.cross(forward, radial))
        up = flight.cross(right, forward)
        ship.orientation = flight.quat_from_basis(right, up, flight.scale(forward, -1.0))
        ship.angular_velocity = (0.0, 0.0, 0.0)

    def test_a_ship_can_climb_from_the_ground_to_orbit(self):
        planet = flight.Planet()
        ship = level_ship(500.0, planet)
        burn, coast = flight.Controls(throttle=1.0, assist=False), flight.Controls(assist=False)
        seconds = 0.0

        def east_of(position):
            radial = flight.normalize(position)
            return flight.normalize(flight.sub((1.0, 0.0, 0.0), flight.scale(radial, radial[0])))

        while flight.orbit(planet, ship.position, ship.velocity)[1] < 40_000.0 and seconds < 300.0:
            radial = flight.normalize(ship.position)
            pitch = math.radians(60.0)
            self.point(ship, flight.add(flight.scale(radial, math.sin(pitch)), flight.scale(east_of(ship.position), math.cos(pitch))))
            ship.step(planet, burn, DT)
            seconds += DT
        while flight.dot(ship.velocity, flight.normalize(ship.position)) > 0.0 and seconds < 900.0:
            ship.step(planet, coast, DT)
            seconds += DT
        while flight.orbit(planet, ship.position, ship.velocity)[0] < 30_000.0 and seconds < 1_000.0:
            self.point(ship, flight.normalize(ship.velocity))
            ship.step(planet, burn, DT)
            seconds += DT

        periapsis, apoapsis = flight.orbit(planet, ship.position, ship.velocity)
        self.assertGreaterEqual(periapsis, 30_000.0)
        self.assertLess(apoapsis, 80_000.0)
        lowest = math.inf
        for _ in range(300):
            run(ship, planet, coast, 1.0)
            lowest = min(lowest, flight.length(ship.position) - planet.radius)
        self.assertGreater(lowest, 29_000.0)


class FallingAndAir(unittest.TestCase):
    def test_free_fall_without_air_takes_the_time_of_constant_gravity(self):
        planet = flight.Planet(density_factor=0.0)
        ship = level_ship(1_000.0, planet)
        seconds = 0.0
        while flight.length(ship.position) > planet.radius:
            ship.step(planet, flight.Controls(assist=False), DT)
            seconds += DT
        expected = math.sqrt(2.0 * 1_000.0 / planet.surface_gravity)
        self.assertAlmostEqual(seconds, expected, delta=0.01 * expected)

    def test_falling_through_air_reaches_terminal_speed(self):
        planet = flight.Planet()
        ship = level_ship(0.0, planet)
        ship.params = flight.ShipParams(lift_area=0.0)
        hold = flight.Controls(assist=False)
        for _ in range(int(150 / DT)):      # about six times v_t / g
            ship.step(planet, hold, DT)
            ship.position = (0.0, 0.0, planet.radius)      # keep it at height 0
        terminal = math.sqrt(2.0 * ship.params.mass * planet.surface_gravity / (planet.air_density * ship.params.drag_area))
        self.assertAlmostEqual(flight.length(ship.velocity), terminal, delta=0.01 * terminal)

    def test_drag_slows_a_coasting_ship_as_its_square_law_says(self):
        planet = flight.Planet(density_factor=1.0)
        ship = level_ship(1_000.0, planet, speed=200.0)
        ship.params = flight.ShipParams(lift_area=0.0)
        rho = planet.density(ship.position)
        run(ship, planet, flight.Controls(assist=False), 5.0)
        # dv/dt = -k v^2 with k = rho Cd A / (2 m): 1/v = 1/v0 + k t
        k = 0.5 * rho * ship.params.drag_area / ship.params.mass
        expected = 1.0 / (1.0 / 200.0 + k * 5.0)
        self.assertAlmostEqual(ship.velocity[0], expected, delta=1.0)


class Assist(unittest.TestCase):
    def test_translation_assist_hovers_in_place(self):
        planet = flight.Planet()
        ship = level_ship(500.0, planet)
        run(ship, planet, flight.Controls(assist=True), 20.0)
        self.assertAlmostEqual(flight.length(ship.position) - planet.radius, 500.0, delta=5.0)
        self.assertLess(flight.length(ship.velocity), 0.5)

    def test_translation_assist_flies_at_the_throttles_speed(self):
        planet = flight.Planet()
        ship = level_ship(5_000.0, planet)
        run(ship, planet, flight.Controls(throttle=0.1, assist=True), 30.0)
        _, _, forward = ship.axes()
        self.assertAlmostEqual(flight.dot(ship.velocity, forward), 0.1 * ship.params.assist_max_speed, delta=5.0)

    def test_the_stick_asks_for_a_rate_and_letting_go_stops_it(self):
        planet = flight.Planet(density_factor=0.0)
        ship = level_ship(50_000.0, planet)
        run(ship, planet, flight.Controls(pitch=1.0, assist=True), 3.0)
        self.assertAlmostEqual(ship.angular_velocity[0], ship.params.max_rates[0], delta=0.02)
        run(ship, planet, flight.Controls(assist=True), 3.0)
        self.assertLess(max(abs(w) for w in ship.angular_velocity), 0.01)

    def test_pitching_up_raises_the_nose(self):
        planet = flight.Planet(density_factor=0.0)
        ship = level_ship(50_000.0, planet)
        run(ship, planet, flight.Controls(pitch=1.0, assist=True), 0.5)
        _, _, forward = ship.axes()
        self.assertGreater(forward[2], 0.05)        # towards +Z, the ship's up


def level_ground(planet):
    """The sphere of the planet's radius as the ground."""
    def query(point):
        r = flight.length(point)
        return planet.radius - r, flight.scale(point, 1.0 / r)
    return query


def sloping_ground(planet, degrees):
    """A plane through the +Z pole on the sphere, tilted `degrees` about the
    Y axis, so it falls away towards -X."""
    a = math.radians(degrees)
    normal = (math.sin(a), 0.0, math.cos(a))
    anchor = (0.0, 0.0, planet.radius)
    return normal, lambda point: (flight.dot(flight.sub(anchor, point), normal), normal)


def fly_over(ship, planet, controls, seconds, ground):
    for _ in range(int(round(seconds / DT))):
        ship.step(planet, controls, DT, ground)


GEAR_HEIGHT = 2.0       # the gear points' distance below the ship's centre


class Ground(unittest.TestCase):
    def test_a_ship_dropped_onto_level_ground_settles_on_its_gear(self):
        planet = flight.Planet()
        ship = level_ship(GEAR_HEIGHT + 3.0, planet)
        fly_over(ship, planet, flight.Controls(assist=False), 10.0, level_ground(planet))
        self.assertFalse(ship.crashed)
        self.assertTrue(ship.on_ground)
        self.assertAlmostEqual(flight.length(ship.position) - planet.radius, GEAR_HEIGHT, delta=0.02)
        self.assertLess(flight.length(ship.velocity), 0.02)
        self.assertLess(max(abs(w) for w in ship.angular_velocity), 0.01)

    def test_a_ship_sliding_along_the_ground_stops(self):
        planet = flight.Planet()
        ship = level_ship(GEAR_HEIGHT, planet, speed=20.0)
        fly_over(ship, planet, flight.Controls(assist=False), 10.0, level_ground(planet))
        self.assertFalse(ship.crashed)
        self.assertLess(flight.length(ship.velocity), 0.02)
        self.assertAlmostEqual(flight.length(ship.position) - planet.radius, GEAR_HEIGHT, delta=0.02)

    def test_on_a_slope_it_turns_onto_its_gear_and_holds(self):
        planet = flight.Planet()
        normal, slope = sloping_ground(planet, 20.0)
        ship = level_ship(GEAR_HEIGHT + 1.5, planet)
        fly_over(ship, planet, flight.Controls(assist=False), 10.0, slope)
        self.assertFalse(ship.crashed)
        _, up, _ = ship.axes()
        self.assertGreater(flight.dot(up, normal), 0.999)
        before = ship.position
        fly_over(ship, planet, flight.Controls(assist=False), 5.0, slope)
        self.assertLess(flight.length(flight.sub(ship.position, before)), 0.05)
        depth, _ = slope(ship.position)
        self.assertAlmostEqual(-depth, GEAR_HEIGHT, delta=0.03)

    def test_coming_down_slowly_with_assist_lands_and_rests(self):
        planet = flight.Planet()
        ship = level_ship(GEAR_HEIGHT + 20.0, planet)
        fly_over(ship, planet, flight.Controls(vertical=-0.1, assist=True), 15.0, level_ground(planet))
        fly_over(ship, planet, flight.Controls(assist=True), 5.0, level_ground(planet))
        self.assertFalse(ship.crashed)
        self.assertTrue(ship.on_ground)
        self.assertEqual(ship.thrust, (0.0, 0.0, 0.0))
        self.assertAlmostEqual(flight.length(ship.position) - planet.radius, GEAR_HEIGHT, delta=0.02)

    def test_holding_full_down_with_assist_lands_without_crashing(self):
        planet = flight.Planet()
        for height in (30.0, 300.0, 1_500.0):
            ship = level_ship(GEAR_HEIGHT + height, planet)
            fastest = 0.0
            for _ in range(int(200 / DT)):
                ship.step(planet, flight.Controls(vertical=-1.0, assist=True), DT, level_ground(planet))
                fastest = max(fastest, ship.impact_speed)
                if ship.on_ground:
                    break
            self.assertFalse(ship.crashed, "from %.0f m" % height)
            self.assertTrue(ship.on_ground, "from %.0f m" % height)
            self.assertLess(fastest, 0.5 * ship.params.gear_crash_speed, "from %.0f m" % height)

    def test_the_descent_limit_leaves_assist_alone_high_above_the_ground(self):
        planet = flight.Planet()
        ship = level_ship(20_000.0, planet)
        fly_over(ship, planet, flight.Controls(vertical=-1.0, assist=True), 20.0, level_ground(planet))
        descent = -flight.dot(ship.velocity, flight.normalize(ship.position))
        self.assertAlmostEqual(descent, ship.params.assist_climb_rate, delta=1.0)

    def test_the_descent_limit_brakes_to_the_touchdown_speed(self):
        ship = level_ship(0.0, flight.Planet())
        p = ship.params
        self.assertAlmostEqual(ship.descent_limit(0.0), p.touchdown_speed)
        self.assertAlmostEqual(ship.descent_limit(-1.0), p.touchdown_speed)
        self.assertAlmostEqual(ship.descent_limit(100.0) ** 2, p.touchdown_speed ** 2 + 2.0 * p.assist_brake * 100.0)

    def test_thrusting_up_lifts_it_off(self):
        planet = flight.Planet()
        ship = level_ship(GEAR_HEIGHT, planet)
        fly_over(ship, planet, flight.Controls(assist=True), 2.0, level_ground(planet))
        fly_over(ship, planet, flight.Controls(vertical=0.5, assist=True), 3.0, level_ground(planet))
        self.assertFalse(ship.on_ground)
        self.assertGreater(flight.length(ship.position) - planet.radius, GEAR_HEIGHT + 10.0)

    def test_hitting_the_ground_too_fast_crashes_and_stops_the_ship(self):
        planet = flight.Planet()
        ship = level_ship(GEAR_HEIGHT + 10.0, planet)
        ship.velocity = (5.0, 0.0, -20.0)
        fly_over(ship, planet, flight.Controls(assist=False), 2.0, level_ground(planet))
        self.assertTrue(ship.crashed)
        self.assertGreater(ship.impact_speed, ship.params.gear_crash_speed)
        where = ship.position
        fly_over(ship, planet, flight.Controls(throttle=1.0, assist=False), 1.0, level_ground(planet))
        self.assertEqual(ship.position, where)
        self.assertGreater(flight.length(ship.position) - planet.radius, 0.0)

    def test_the_hull_crashes_at_a_lower_speed_than_the_gear(self):
        planet = flight.Planet()
        ship = level_ship(1.5 + 2.0, planet)            # upside down: the roof 1.5 m above the centre, 2 m up
        ship.orientation = flight.quat_multiply(ship.orientation, (0.0, 0.0, 0.0, 1.0))   # rolled 180 degrees
        fly_over(ship, planet, flight.Controls(assist=False), 2.0, level_ground(planet))
        self.assertTrue(ship.crashed)
        self.assertLess(ship.impact_speed, ship.params.gear_crash_speed)

    def test_a_fast_ship_does_not_pass_through_the_ground_in_one_step(self):
        planet = flight.Planet(density_factor=0.0)
        ship = level_ship(GEAR_HEIGHT + 0.5, planet)
        ship.velocity = (0.0, 0.0, -3_000.0)            # 25 m per step
        fly_over(ship, planet, flight.Controls(assist=False), 0.1, level_ground(planet))
        self.assertTrue(ship.crashed)
        self.assertGreater(flight.length(ship.position) - planet.radius, 0.0)


if __name__ == "__main__":
    unittest.main()

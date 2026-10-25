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


def planet_with_moon(phase=0.0):
    """The planet with a moon circling in the plane of X (angle 0) and Y."""
    planet = flight.Planet(density_factor=0.0)
    planet.moon = flight.Moon(phase=phase, u=(1.0, 0.0, 0.0), v=(0.0, 1.0, 0.0), primary_gm=planet.gm)
    return planet


def coast(ship, planet, seconds, dt):
    """Coast with no thrust, the planet's clock running with the flight."""
    hold = flight.Controls(assist=False)
    for _ in range(int(round(seconds / dt))):
        ship.step(planet, hold, dt)
        planet.time += dt


def ship_at(position, velocity):
    return flight.Ship(position=position, velocity=velocity)


class Moon(unittest.TestCase):
    def test_the_moon_goes_round_its_circle_at_its_speed(self):
        planet = planet_with_moon(phase=0.3)
        moon = planet.moon
        period = 2.0 * math.pi / moon.rate
        for t in (0.0, 1234.5, period / 3.0):
            self.assertAlmostEqual(flight.length(moon.position(t)), moon.distance, delta=1e-6)
            # The velocity is the position's rate of change, square to it.
            h = 0.01
            difference = flight.scale(flight.sub(moon.position(t + h), moon.position(t - h)), 0.5 / h)
            self.assertLess(flight.length(flight.sub(difference, moon.velocity(t))), 1e-3)
            self.assertAlmostEqual(flight.dot(moon.velocity(t), moon.position(t)), 0.0, delta=1e-3)
        start, end = moon.position(0.0), moon.position(period)
        self.assertLess(flight.length(flight.sub(start, end)), 1e-3)
        self.assertAlmostEqual(period / 3600.0, 3.64, delta=0.01)

    def test_its_sphere_of_influence_decides_what_orbits_are_reckoned_around(self):
        planet = planet_with_moon()
        moon = planet.moon
        self.assertAlmostEqual(moon.influence, 104_000.0, delta=1_000.0)
        centre = moon.position(0.0)
        inside = flight.add(centre, (0.0, 0.99 * moon.influence, 0.0))
        outside = flight.add(centre, (0.0, 1.01 * moon.influence, 0.0))
        body, position, velocity = flight.reference(planet, inside, (1.0, 2.0, 3.0))
        self.assertIs(body, moon)
        self.assertLess(flight.length(flight.sub(position, (0.0, 0.99 * moon.influence, 0.0))), 1e-6)
        self.assertLess(flight.length(flight.sub(velocity, flight.sub((1.0, 2.0, 3.0), moon.velocity(0.0)))), 1e-9)
        self.assertIs(flight.reference(planet, outside, (0.0, 0.0, 0.0))[0], planet)

    def test_a_low_orbit_of_the_planet_barely_feels_the_moon(self):
        planet = planet_with_moon(phase=math.pi)        # the moon on the far side
        r = planet.radius + 40_000.0
        ship = ship_at((r, 0.0, 0.0), (0.0, flight.circular_speed(planet, 40_000.0), 0.0))
        heights = []
        for _ in range(30):
            coast(ship, planet, 10.0, 1.0 / 30.0)
            heights.append(flight.length(ship.position) - planet.radius)
        # Five minutes: the moon's tide (its pull less the planet's fall towards it) is small.
        self.assertLess(max(abs(h - 40_000.0) for h in heights), 20.0)

    def test_an_orbit_of_the_moon_holds_while_the_moon_goes_round(self):
        planet = planet_with_moon()
        moon = planet.moon
        r = moon.radius + 15_000.0
        offset, relative = (0.0, 0.0, r), (0.0, math.sqrt(moon.gm / r), 0.0)   # over the moon's pole
        ship = ship_at(flight.add(moon.position(0.0), offset), flight.add(moon.velocity(0.0), relative))
        period = 2.0 * math.pi * math.sqrt(r ** 3 / moon.gm)
        distances = []
        for _ in range(20):
            coast(ship, planet, period / 20.0, 0.25)
            distances.append(flight.length(flight.sub(ship.position, moon.position(planet.time))))
        self.assertLess(max(abs(d - r) for d in distances), 2_000.0)
        body, position, velocity = flight.reference(planet, ship.position, ship.velocity)
        self.assertIs(body, moon)

    def test_a_hohmann_transfer_from_a_low_orbit_reaches_the_moon(self):
        planet = flight.Planet(density_factor=0.0)
        r1 = planet.radius + 40_000.0
        moon = flight.Moon(u=(1.0, 0.0, 0.0), v=(0.0, 1.0, 0.0), primary_gm=planet.gm)
        r2 = moon.distance
        a = 0.5 * (r1 + r2)
        transfer = math.pi * math.sqrt(a ** 3 / planet.gm)
        # The moon leads by what it travels during the transfer, short of half a turn.
        moon.phase = math.pi - moon.rate * transfer
        planet.moon = moon
        perigee_speed = math.sqrt(planet.gm * (2.0 / r1 - 1.0 / a))
        self.assertAlmostEqual(perigee_speed - flight.circular_speed(planet, 40_000.0), 162.0, delta=2.0)
        ship = ship_at((r1, 0.0, 0.0), (0.0, perigee_speed, 0.0))
        closest = math.inf
        for _ in range(int(1.1 * transfer / 10.0)):
            coast(ship, planet, 10.0, 0.5)
            closest = min(closest, flight.length(flight.sub(ship.position, moon.position(planet.time))))
        self.assertLess(closest, moon.influence)
        self.assertAlmostEqual(transfer / 60.0, 53.0, delta=1.0)


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


class ReentryGlow(unittest.TestCase):
    def test_no_glow_in_orbit_or_in_ordinary_low_flight(self):
        planet = flight.Planet()
        orbiting = level_ship(40_000.0, planet, speed=flight.circular_speed(planet, 40_000.0))
        self.assertEqual(flight.reentry_glow(planet, orbiting.position, orbiting.velocity), 0.0)
        cruising = level_ship(1_000.0, planet, speed=300.0)
        self.assertEqual(flight.reentry_glow(planet, cruising.position, cruising.velocity), 0.0)

    def test_coming_down_from_orbit_glows_between_25_and_8_km(self):
        planet = flight.Planet()
        ship = level_ship(40_000.0, planet, speed=flight.circular_speed(planet, 40_000.0) - 80.0)
        coast = flight.Controls(assist=False)
        glows = []
        dt = 1.0 / 30.0
        while flight.length(ship.position) - planet.radius > 2_000.0:
            ship.step(planet, coast, dt)
            height = flight.length(ship.position) - planet.radius
            glows.append((height, flight.reentry_glow(planet, ship.position, ship.velocity)))
        self.assertTrue(all(g == 0.0 for h, g in glows if h > 30_000.0))
        brightest = max(glows, key=lambda hg: hg[1])
        self.assertGreater(brightest[1], 0.9)
        self.assertTrue(8_000.0 < brightest[0] < 25_000.0)
        self.assertTrue(all(g < 0.1 for h, g in glows if h < 3_000.0))


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


GEAR_HEIGHT = -flight.ShipParams().gear[0][1]       # the gear points' distance below the ship's centre


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
        ship = level_ship(1.9 + 2.0, planet)            # upside down: the roof 1.9 m above the centre, 2 m up
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


def moon_ground(planet):
    """The sphere of the moon's radius, where the moon is at the planet's time, as the ground."""
    moon = planet.moon

    def query(point):
        offset = flight.sub(point, moon.position(planet.time))
        r = flight.length(offset)
        return moon.radius - r, flight.scale(offset, 1.0 / r)
    return query


def ship_on_moon(planet, height):
    """A ship `height` metres above the moon's radius over its +Z pole (off
    the plane of its orbit), level, nose along +X, moving with the moon."""
    moon = planet.moon
    position = flight.add(moon.position(planet.time), (0.0, 0.0, moon.radius + height))
    right, up, back = (0.0, -1.0, 0.0), (0.0, 0.0, 1.0), (-1.0, 0.0, 0.0)
    return flight.Ship(position=position, velocity=moon.velocity(planet.time),
                       orientation=flight.quat_from_basis(right, up, back))


def fly_over_moon(ship, planet, controls, seconds):
    """Fly over the moon's ground, the planet's clock running with the flight;
    returns the fastest a point reached the surface at."""
    ground = moon_ground(planet)
    fastest = 0.0
    for _ in range(int(round(seconds / DT))):
        ship.step(planet, controls, DT, ground)
        planet.time += DT
        fastest = max(fastest, ship.impact_speed)
    return fastest


def on_moon(ship, planet):
    """(height above the moon's radius, speed relative to the moon)."""
    moon = planet.moon
    offset = flight.sub(ship.position, moon.position(planet.time))
    return (flight.length(offset) - moon.radius,
            flight.length(flight.sub(ship.velocity, moon.velocity(planet.time))))


class MoonGround(unittest.TestCase):
    def test_the_surface_moves_with_the_moon_inside_its_sphere_of_influence_only(self):
        planet = planet_with_moon()
        moon = planet.moon
        velocity, acceleration, shift = planet.surface_motion(
            flight.add(moon.position(0.0), (0.0, 0.0, 30_000.0)), 2.0)
        self.assertEqual(velocity, moon.velocity(0.0))
        self.assertAlmostEqual(flight.length(acceleration), moon.rate ** 2 * moon.distance, delta=1e-9)
        self.assertLess(flight.dot(acceleration, moon.position(0.0)), 0.0)
        self.assertEqual(shift, flight.sub(moon.position(2.0), moon.position(0.0)))
        self.assertEqual(planet.surface_motion((0.0, 0.0, planet.radius + 1_000.0), 2.0),
                         ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)))

    def test_a_ship_set_down_on_the_moon_rests_there_as_it_moves(self):
        planet = planet_with_moon()
        ship = ship_on_moon(planet, GEAR_HEIGHT + 1.0)
        fly_over_moon(ship, planet, flight.Controls(assist=False), 20.0)
        height, speed = on_moon(ship, planet)
        self.assertFalse(ship.crashed)
        self.assertTrue(ship.on_ground)
        self.assertAlmostEqual(height, GEAR_HEIGHT, delta=0.02)
        self.assertLess(speed, 0.02)

    def test_a_ship_at_rest_on_the_moon_does_not_creep(self):
        planet = planet_with_moon(phase=1.0)
        ship = ship_on_moon(planet, GEAR_HEIGHT + 0.5)
        fly_over_moon(ship, planet, flight.Controls(assist=False), 10.0)
        settled = flight.sub(ship.position, planet.moon.position(planet.time))
        fly_over_moon(ship, planet, flight.Controls(assist=False), 120.0)
        moved = flight.sub(flight.sub(ship.position, planet.moon.position(planet.time)), settled)
        self.assertLess(flight.length(moved), 0.005)

    def test_assist_hovers_over_the_moon(self):
        planet = planet_with_moon()
        ship = ship_on_moon(planet, 50.0)
        fly_over_moon(ship, planet, flight.Controls(assist=True), 20.0)
        height, speed = on_moon(ship, planet)
        self.assertAlmostEqual(height, 50.0, delta=0.5)
        self.assertLess(speed, 0.05)

    def test_holding_full_down_with_assist_lands_on_the_moon(self):
        planet = planet_with_moon()
        ship = ship_on_moon(planet, GEAR_HEIGHT + 300.0)
        fastest = fly_over_moon(ship, planet, flight.Controls(vertical=-1.0, assist=True), 60.0)
        height, speed = on_moon(ship, planet)
        self.assertFalse(ship.crashed)
        self.assertTrue(ship.on_ground)
        self.assertLess(fastest, 0.5 * ship.params.gear_crash_speed)
        self.assertAlmostEqual(height, GEAR_HEIGHT, delta=0.05)
        self.assertLess(speed, 0.05)

    def test_a_crashed_ship_moves_with_the_moon(self):
        planet = planet_with_moon()
        ship = ship_on_moon(planet, GEAR_HEIGHT + 10.0)
        ship.velocity = flight.add(ship.velocity, (0.0, 0.0, -20.0))
        fly_over_moon(ship, planet, flight.Controls(assist=False), 2.0)
        self.assertTrue(ship.crashed)
        where = flight.sub(ship.position, planet.moon.position(planet.time))
        fly_over_moon(ship, planet, flight.Controls(throttle=1.0, assist=False), 5.0)
        moved = flight.sub(flight.sub(ship.position, planet.moon.position(planet.time)), where)
        self.assertLess(flight.length(moved), 1.0)


if __name__ == "__main__":
    unittest.main()

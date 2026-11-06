"""Tests for the wind (shoonyakasha.planet.wind) and PlanetWorld's use of
it: the height profile, the prevailing direction turning once a period,
the value noise's range and smoothness, the wind along the ground and
still where the prevailing direction is straight up, its gusts within
their share and carried with it; PlanetWorld.wind_at none on the moon and
at the height above the ground under the viewpoint by default, the clouds
drifting along the prevailing direction, and CLOUD_ROTATION the same as
clouds.glsl's; a storm's centre moving along the ground, its strength
rising and falling over FADE and falling over its edge, the wind in it
STORM_WIND times stronger, and PlanetWorld's wind and storm uniforms with
one. Plain Python with a fake engine: no GPU needed.

    python -m unittest discover -s tests/python
"""

import math
import re
import unittest

from shoonyakasha.planet import wind
from shoonyakasha.planet import world as planet_world
from shoonyakasha.planet.vec import add, cross, dot, length, normalize, scale, sub

from test_planet_world import FakeEngine

RADIUS = planet_world.RADIUS


class Profile(unittest.TestCase):
    def test_log_profile(self):
        self.assertAlmostEqual(wind.profile(1.0), 0.25)
        self.assertAlmostEqual(wind.profile(10.0), 0.5)
        self.assertAlmostEqual(wind.profile(1_000.0), 1.0)
        self.assertEqual(wind.profile(0.2), wind.profile(1.0))
        self.assertEqual(wind.profile(-50.0), wind.profile(1.0))


class Prevailing(unittest.TestCase):
    def test_turns_once_a_period(self):
        self.assertEqual(wind.prevailing(0.0), normalize(wind.BASE_DIRECTION))
        for t in (0.0, 1234.0, 0.37 * wind.TURN_PERIOD):
            self.assertAlmostEqual(length(wind.prevailing(t)), 1.0)
            for a, b in zip(wind.prevailing(t), wind.prevailing(t + wind.TURN_PERIOD)):
                self.assertAlmostEqual(a, b)
        quarter = wind.prevailing(0.25 * wind.TURN_PERIOD)
        self.assertAlmostEqual(dot(quarter, wind.BASE_DIRECTION), 0.0)
        self.assertAlmostEqual(dot(quarter, wind.TURN_AXIS), 0.0)


class Noise(unittest.TestCase):
    def test_range_and_smoothness(self):
        values = [wind.noise3(0.37 * i, 0.11 * i + 3.0, -0.23 * i) for i in range(500)]
        self.assertTrue(all(-1.0 <= v <= 1.0 for v in values))
        self.assertGreater(max(values) - min(values), 0.8)
        for i in range(200):
            x, y, z = 0.173 * i, 0.091 * i, 1.0 - 0.057 * i
            self.assertLess(abs(wind.noise3(x + 1e-4, y, z) - wind.noise3(x, y, z)), 1e-2)


class WindAt(unittest.TestCase):
    def setUp(self):
        self.direction = wind.prevailing(0.0)
        side = normalize(cross(self.direction, wind.TURN_AXIS))
        self.up = normalize(add(side, scale(self.direction, 0.4)))       # the direction partly along the ground
        self.point = scale(self.up, RADIUS + 50.0)

    def test_along_the_ground_within_its_gusts(self):
        tangent = sub(self.direction, scale(self.up, dot(self.direction, self.up)))
        steady = 15.0 * length(tangent) * wind.profile(10.0)
        for k in range(50):
            point = add(self.point, scale(normalize(tangent), 37.0 * k))
            w = wind.wind_at(point, self.up, 0.0, 15.0, 10.0)
            self.assertAlmostEqual(dot(w, self.up), 0.0)
            self.assertAlmostEqual(length(cross(w, tangent)), 0.0, places=6)
            self.assertLessEqual(abs(length(w) - steady), wind.GUST_SHARE * steady + 1e-9)
            self.assertGreater(dot(w, tangent), 0.0)

    def test_still_where_the_direction_is_straight_up(self):
        up = self.direction
        self.assertEqual(wind.wind_at(scale(up, RADIUS), up, 0.0, 15.0, 10.0), (0.0, 0.0, 0.0))
        self.assertEqual(wind.wind_at(self.point, self.up, 0.0, 0.0, 10.0), (0.0, 0.0, 0.0))

    def test_gusts_travel_with_the_wind(self):
        along = normalize(sub(self.direction, scale(self.up, dot(self.direction, self.up))))
        a = wind.gust(self.point, 10.0, along, 12.0)
        b = wind.gust(add(self.point, scale(along, 12.0 * 3.0)), 13.0, along, 12.0)
        self.assertAlmostEqual(a, b)


class Storms(unittest.TestCase):
    def setUp(self):
        self.centre = normalize((0.2, 0.9, 0.3))
        self.axis = normalize(cross(self.centre, (1.0, 0.0, 0.0)))
        self.storm = wind.Storm(self.centre, self.axis, 10.0, 10_000.0, 100.0, 100.0 + 3_600.0, RADIUS)

    def test_its_centre_moves_along_the_ground(self):
        later = self.storm.centre_at(100.0 + 1_000.0)
        self.assertAlmostEqual(RADIUS * math.acos(dot(later, self.centre)), 10_000.0, places=3)
        self.assertAlmostEqual(dot(later, self.axis), 0.0)

    def test_its_strength_rises_and_falls(self):
        s = self.storm
        self.assertEqual(s.strength(50.0), 0.0)
        self.assertAlmostEqual(s.strength(100.0 + 0.5 * wind.FADE), 0.5)
        self.assertEqual(s.strength(100.0 + 1_800.0), 1.0)
        self.assertAlmostEqual(s.strength(s.end - 0.25 * wind.FADE), 0.25)
        self.assertEqual(s.strength(s.end + 1.0), 0.0)

    def test_it_falls_over_its_edge(self):
        s, t = self.storm, 100.0 + 1_800.0
        centre = s.centre_at(t)
        side = normalize(cross(centre, s.axis))

        def at(distance):
            return s.at(normalize(add(scale(centre, math.cos(distance / RADIUS)),
                                      scale(side, math.sin(distance / RADIUS)))), t)
        self.assertEqual(at(0.0), 1.0)
        self.assertEqual(at((1.0 - wind.EDGE) * s.radius - 10.0), 1.0)
        self.assertAlmostEqual(at((1.0 - 0.5 * wind.EDGE) * s.radius), 0.5, places=3)
        self.assertEqual(at(s.radius + 10.0), 0.0)

    def test_the_wind_in_it(self):
        direction = wind.prevailing(0.0)
        up = normalize(cross(direction, wind.TURN_AXIS))
        point = scale(up, RADIUS)
        calm = wind.wind_at(point, up, 0.0, 15.0, 10.0)
        stormy = wind.wind_at(point, up, 0.0, 15.0, 10.0, storm=1.0)
        steady = 15.0 * wind.profile(10.0)
        n = wind.noise3(*(c / wind.GUST_SCALE for c in point))
        self.assertAlmostEqual(length(calm), steady * (1.0 + wind.GUST_SHARE * n))
        self.assertAlmostEqual(length(stormy), wind.STORM_WIND * steady * (1.0 + wind.STORM_GUST * n))


class World(unittest.TestCase):
    def setUp(self):
        self.world = planet_world.PlanetWorld(FakeEngine())
        self.world.time = 500.0
        self.up = normalize(cross(wind.prevailing(500.0), wind.TURN_AXIS))
        self.point = scale(self.up, RADIUS + 320.0)

    def test_none_on_the_moon(self):
        self.assertEqual(self.world.wind_at(self.world.moon, self.point), (0.0, 0.0, 0.0))

    def test_its_height_above_the_ground_under_the_viewpoint_by_default(self):
        self.world.ground = 300.0
        for a, b in zip(self.world.wind_at(self.world.planet, self.point),
                        self.world.wind_at(self.world.planet, self.point, 20.0)):
            self.assertAlmostEqual(a, b, places=9)
        self.world.ground = None
        expected = wind.wind_at(self.point, self.up, 500.0, self.world.wind_speed, 320.0)
        for a, b in zip(self.world.wind_at(self.world.planet, self.point), expected):
            self.assertAlmostEqual(a, b, places=9)

    def test_the_clouds_drift_along_the_prevailing_direction(self):
        """A point of the coverage noise, R d f + offset, stays put for d moved along the direction."""
        w = self.world
        before = w.cloud_wind
        w.blow_clouds(60.0)
        speed = w.wind_speed * wind.profile(0.5 * sum(planet_world.CLOUD_LAYER))
        moved = scale(wind.prevailing(w.time), speed * 60.0 / RADIUS)       # in units of the radius
        columns = planet_world.CLOUD_ROTATION
        turned = tuple(sum(columns[c][r] * moved[c] for c in range(3)) for r in range(3))
        f = planet_world.CLOUD_SHAPE[0]
        for t, b, a in zip(turned, before, w.cloud_wind):
            self.assertAlmostEqual(t * f + (a - b), 0.0)
        self.assertEqual(w.engine.custom["planet.clouds.wind"][:3], w.cloud_wind)

    def test_the_wind_and_the_uniforms_with_a_storm(self):
        w = self.world
        centre = normalize(self.point)
        w.storm = wind.Storm(centre, normalize(cross(centre, (1.0, 0.0, 0.0))), 0.0, 10_000.0,
                             w.time - wind.FADE, w.time + 3_600.0, RADIUS)
        calm = wind.wind_at(self.point, self.up, w.time, w.wind_speed, 20.0)
        stormy = wind.wind_at(self.point, self.up, w.time, w.wind_speed, 20.0, storm=1.0)
        self.assertEqual(w.wind_at(w.planet, self.point, 20.0), stormy)
        self.assertNotEqual(calm, stormy)
        w.upload_storm(add(w.planet.centre(), self.point))
        storm, shape = w.engine.custom["planet.clouds.storm"], w.engine.custom["planet.clouds.stormShape"]
        for a, b in zip(storm[:3], centre):
            self.assertAlmostEqual(a, b)
        self.assertAlmostEqual(storm[3], 10_000.0 / RADIUS)
        self.assertEqual(shape[0], 1.0)
        self.assertAlmostEqual(shape[1], wind.EDGE * 10_000.0 / RADIUS)
        self.assertAlmostEqual(shape[2], planet_world.STORM_HAZE)
        self.assertEqual(shape[3], planet_world.STORM_COVERAGE)
        w.storm = None
        w.upload_storm(add(w.planet.centre(), self.point))
        self.assertEqual(w.engine.custom["planet.clouds.stormShape"][0], 0.0)
        self.assertEqual(w.engine.custom["planet.clouds.stormShape"][2], 0.0)

    def test_cloud_rotation_is_the_shaders(self):
        shader = (planet_world.PIPELINE.parent / "shaders" / "clouds.glsl").read_text(encoding="utf-8")
        body = re.search(r"const mat3 CLOUD_ROTATION = mat3\((.*?)\);", shader, re.S).group(1)
        values = [float(v) for v in body.replace("\n", " ").split(",")]
        self.assertEqual([v for column in planet_world.CLOUD_ROTATION for v in column], values)


if __name__ == "__main__":
    unittest.main()

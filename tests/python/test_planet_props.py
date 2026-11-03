"""Tests for the props (shoonyakasha.planet.props)
on sk.PlanetPhysics in a Bubble at the top of a planet with stand-in
terrain colliders and world: a crate over the fine mesh is near and over
only the coarse one far; one asleep off the fine mesh rests where it lies
and comes back only over the fine mesh; one over neither and low leaves the physics,
flying, and comes back in where it is, moving as it was, once a mesh
covers it; one beyond the bubble's radius leaves it; a flying crate keeps
a circular orbit for a period and lands on its measured ground, resting
at its rest height; a slow one on its measured ground rests; the ground is
asked for with the world's extra probes and taken back only when they are
its own; leave_all takes every prop out; and the props' records (an
asleep one resting, an awake one flying) go through JSON and come back
as the same records, the resting one into the physics asleep where it
lay. Where this interpreter's
shoonyakasha has no extension, the tests run in a fresh interpreter.

    python -m unittest discover -s tests/python
"""

import json
import math
import os
import subprocess
import sys
import unittest

import shoonyakasha as sk
from shoonyakasha.planet.bubble import Bubble
from shoonyakasha.planet.orbits import Planet
from shoonyakasha.planet.vec import length, normalize, sub

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
        self.physics = Planet(radius=RADIUS, surface_gravity=G, air_density=0.0)
        self.extra_probes = []
        self.extra_heights = []


class Collider:
    """Covers the points within `reach` metres of the bubble's origin, along the ground."""

    def __init__(self, reach):
        self.reach = reach

    def covers(self, p, margin=0.0):
        return math.hypot(p[0], p[2]) <= self.reach - margin


class FakeScene:
    def __getattr__(self, name):
        return lambda *args: None


class FakeEngine:
    def __init__(self):
        self.scene = FakeScene()

    def create_mesh(self, positions, normals=None, colors=None, name=""):
        return 1


class PropsTest(unittest.TestCase):
    def setUp(self):
        if not sk.extension_available():
            if __name__ == "__main__":
                self.fail("no engine extension in a fresh interpreter")
            self.skipTest("runs in a fresh interpreter (test_in_fresh_interpreter)")
        from shoonyakasha.planet import props as props_module
        self.module = props_module
        self.physics = sk.PlanetPhysics(max_bodies=64, threads=1)
        self.world = World()
        self.bubble = Bubble(self.physics, self.world.planet, G * RADIUS ** 2, (0.0, RADIUS, 0.0))
        self.props = props_module.Props(FakeEngine(), ([], []), self.world)
        self.fine, self.coarse = Collider(16.0), Collider(128.0)

    def crate(self, x, y=1.0, velocity=(0.0, 0.0, 0.0)):
        crate = self.props.spawn_crate(self.bubble, (x, y, 0.0), (1.0, 0.0, 0.0, 0.0), 0.4, mass=10.0)
        self.physics.set_velocity(crate.body, velocity)
        return crate

    def test_near_far_out_and_back(self):
        near, far, out = self.crate(2.0), self.crate(60.0), self.crate(300.0, velocity=(5.0, 0.0, 0.0))
        out.ground = 0.0
        self.props.update(self.bubble, self.fine, self.coarse)
        self.assertFalse(self.physics.is_far(near.body))
        self.assertTrue(self.physics.is_far(far.body))
        self.assertIsNone(out.body)
        self.assertEqual(out.state, "flying")
        self.assertAlmostEqual(out.velocity[0], 5.0, places=4)
        self.assertAlmostEqual(out.position[1], RADIUS + 1.0, places=3)
        # The coarse mesh comes to it: back into the physics where it is, moving as it was.
        self.coarse.reach = 400.0
        self.props.update(self.bubble, self.fine, self.coarse)
        self.assertIsNotNone(out.body)
        self.assertEqual(out.state, "jolt")
        p = self.physics.get_transform(out.body)[0]
        self.assertAlmostEqual(p[0], 300.0, places=3)
        self.assertAlmostEqual(self.physics.get_velocity(out.body)[0][0], 5.0, places=3)

    def test_high_or_beyond_the_bubble(self):
        high = self.crate(500.0, y=300.0)
        high.ground = 0.0
        gone = self.crate(2100.0, y=300.0)
        self.props.update(self.bubble, self.fine, self.coarse)
        self.assertIsNotNone(high.body)                     # more than CLEARANCE above its ground
        self.assertIsNone(gone.body)

    def test_slow_on_the_ground_rests(self):
        # 0.2 m above the sphere, 300 m along it from the bubble's origin.
        y = math.sqrt((RADIUS + 0.2) ** 2 - 300.0 ** 2) - RADIUS
        crate = self.crate(300.0, y=y)
        crate.ground = 0.0
        self.props.update(self.bubble, self.fine, self.coarse)
        self.assertEqual(crate.state, "resting")
        self.props.fly(1.0, 0.05)
        self.assertAlmostEqual(length(crate.position), RADIUS + 0.2, places=3)

    def test_circular_orbit_and_landing(self):
        r = RADIUS + 5_000.0
        crate = self.crate(0.0, y=5_000.0)
        self.physics.remove(crate.body)
        crate.body, crate.state = None, "flying"
        crate.position, crate.velocity = (0.0, r, 0.0), (math.sqrt(G * RADIUS ** 2 / r), 0.0, 0.0)
        period = 2.0 * math.pi * math.sqrt(r ** 3 / (G * RADIUS ** 2))
        for _ in range(int(period)):
            self.props.fly(1.0, 0.05)
        self.props.fly(period - int(period), 0.05)
        self.assertLess(length(sub(crate.position, (0.0, r, 0.0))), 5.0)
        # Its ground 4,999.9 m up: it lands there and rests.
        crate.ground = 4_999.9
        self.props.fly(0.1, 0.05)
        self.assertEqual(crate.state, "resting")
        self.assertAlmostEqual(length(crate.position), RADIUS + 4_999.9 + crate.rest, places=6)
        self.assertEqual(crate.velocity, (0.0, 0.0, 0.0))

    def test_probes(self):
        crate = self.crate(300.0, y=20.0, velocity=(5.0, 0.0, 0.0))
        self.props.update(self.bubble, self.fine, self.coarse)
        self.assertEqual(crate.state, "flying")
        self.props.probe(self.bubble, 4)
        asked = self.world.extra_probes
        self.assertEqual(len(asked), 1)
        direction, body = asked[0]
        self.assertAlmostEqual(direction[0], normalize(crate.position)[0], places=9)
        self.assertIs(body, self.world.planet)
        # Not back yet: nothing taken; then back: its ground set.
        self.props.probe(self.bubble, 4)
        self.assertIsNone(crate.ground)
        self.world.extra_heights = [(asked[0], 12.5)]
        self.props.probe(self.bubble, 4)
        self.assertEqual(crate.ground, 12.5)
        # Someone else's probes are left alone.
        self.world.extra_probes = [((0.0, 1.0, 0.0), body)]
        self.world.extra_heights = []
        self.props.probe(self.bubble, 4)
        self.assertEqual(self.world.extra_probes, [((0.0, 1.0, 0.0), body)])

    def test_asleep_off_the_fine_mesh_rests_until_it_is_back(self):
        crate = self.crate(60.0, y=0.2)
        for _ in range(60):
            self.physics.step(1.0 / 120.0)
        self.physics.set_velocity(crate.body, (0.0, 0.0, 0.0))
        while self.physics.is_active(crate.body):
            self.physics.step(1.0 / 120.0)
        p = self.physics.get_transform(crate.body)[0]
        self.props.update(self.bubble, self.fine, self.coarse)
        self.assertEqual(crate.state, "resting")
        self.assertEqual(crate.position, self.bubble.body_relative(p))
        # Back over the coarse mesh only: still resting; over the fine one: back in, where it lay.
        self.props.update(self.bubble, self.fine, self.coarse)
        self.assertIsNone(crate.body)
        self.fine.reach = 100.0
        self.props.update(self.bubble, self.fine, self.coarse)
        self.assertIsNotNone(crate.body)
        self.assertFalse(self.physics.is_active(crate.body))
        self.assertLess(length(sub(self.physics.get_transform(crate.body)[0], p)), 1e-4)

    def test_records(self):
        asleep, moving = self.crate(2.0, y=0.2), self.crate(5.0, velocity=(3.0, 0.0, 0.0))
        self.physics.deactivate(asleep.body)
        out = self.crate(300.0, velocity=(5.0, 0.0, 0.0))
        out.ground = 0.0
        self.props.update(self.bubble, self.fine, self.coarse)
        records = json.loads(json.dumps(self.props.records(self.bubble)))
        self.assertEqual([r["state"] for r in records], ["resting", "flying", "flying"])
        p = self.physics.get_transform(asleep.body)[0]
        self.assertEqual(records[0]["position"], list(self.bubble.body_relative(p)))
        self.assertEqual(records[0]["velocity"], [0.0, 0.0, 0.0])
        self.assertAlmostEqual(records[1]["velocity"][0], 3.0, places=4)
        self.assertEqual(records[2]["ground"], 0.0)
        self.assertEqual((records[0]["kind"], records[0]["on"], records[0]["key"]), ("crate", "planet", None))
        self.assertAlmostEqual(records[0]["mass"], 10.0, places=4)
        # Made again out of the physics: the same records; the resting one back in asleep where it lay.
        again = self.module.Props(FakeEngine(), ([], []), self.world)
        for record in records:
            again.restore(record, {"planet": self.world.planet})
        self.assertEqual(again.records(self.bubble), records)
        self.props.clear(self.bubble)
        again.update(self.bubble, self.fine, self.coarse)
        lying = again.crates[0]
        self.assertIsNotNone(lying.body)
        self.assertFalse(self.physics.is_active(lying.body))
        self.assertLess(length(sub(self.physics.get_transform(lying.body)[0], p)), 1e-6)
        self.assertAlmostEqual(self.physics.mass(lying.body), asleep.mass, places=4)

    def test_leave_all(self):
        crates = [self.crate(x) for x in (0.0, 1.0, 2.0)]
        self.props.leave_all(self.bubble)
        self.assertTrue(all(c.body is None for c in crates))
        self.assertEqual(self.physics.body_count, 0)
        self.assertEqual(self.props.in_physics(), [])


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

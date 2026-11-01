"""Tests for the planet example's ore scanner
(examples/python/simulations/planet/scanner.py) with a stand-in world
whose extra probes are answered by a richness function: the point ahead
coasts along a circular orbit; the probes go along the ground track at
RATE a second, only while nothing else uses them, and are released when
scanning stops; each richness goes into the strip; rich points become
contacts, merged within MERGE (keeping the richest), the poorest dropped
past MAX_CONTACTS; the nearest contact; and the contacts' records. Plain
Python: no engine needed.

    python -m unittest discover -s tests/python
"""

import math
import sys
import unittest
from pathlib import Path

PLANET_DIR = Path(__file__).resolve().parents[2] / "examples" / "python" / "simulations" / "planet"
sys.path.insert(0, str(PLANET_DIR))

import scanner  # noqa: E402

RADIUS = 100_000.0
GM = 4.905 * RADIUS ** 2


class Body:
    name = "planet"
    radius = RADIUS

    def centre(self):
        return (0.0, 0.0, 0.0)

    def velocity(self):
        return (0.0, 0.0, 0.0)

    def relative(self, point):
        return point


class World:
    def __init__(self, richness):
        self.richness = richness
        self.extra_probes = []
        self.extra_heights = []
        self.extra_ores = []

    def answer(self):
        """The extra probes asked for, measured."""
        self.extra_heights = [(p, 100.0) for p in self.extra_probes]
        self.extra_ores = [(p, self.richness(p[0])) for p in self.extra_probes]


def orbit(height=40_000.0):
    r = RADIUS + height
    return (0.0, r, 0.0), (math.sqrt(GM / r), 0.0, 0.0)


class ScannerTest(unittest.TestCase):
    def setUp(self):
        self.body = Body()

    def test_ahead_on_a_circular_orbit(self):
        s = scanner.Scanner(World(lambda d: 0.0), 4)
        position, velocity = orbit()
        d = s.ahead(position, velocity, self.body, GM)
        r = length(position)
        angle = math.sqrt(GM / r) * scanner.AHEAD / r
        self.assertAlmostEqual(d[0], math.sin(angle), places=4)
        self.assertAlmostEqual(d[1], math.cos(angle), places=4)

    def test_rate_strip_and_contacts(self):
        # Rich (0.8) ahead of the start (x of the direction above 0.01; the track starts near 0.042), else 0.2.
        world = World(lambda d: 0.8 if d[0] > 0.01 else 0.2)
        s = scanner.Scanner(world, 4)
        position, velocity = orbit()
        dt = 1.0 / 60.0
        for frame in range(600):
            world.answer()
            s.update(dt, position, velocity, self.body, GM)
            # Move the ship along its orbit (a circle) by dt.
            r = length(position)
            a = math.sqrt(GM / r) * dt / r
            c, n = math.cos(a), math.sin(a)
            position = (position[0] * c + position[1] * n, -position[0] * n + position[1] * c, 0.0)
            velocity = (velocity[0] * c + velocity[1] * n, -velocity[0] * n + velocity[1] * c, 0.0)
        # 10 s at RATE a second, less a round or two waiting.
        self.assertGreater(s.scanned, 0.9 * scanner.RATE * 10.0 - 8)
        self.assertLessEqual(s.scanned, scanner.RATE * 10.0 + 8)
        self.assertEqual(len(s.strip), scanner.STRIP)
        self.assertEqual(s.strip[-1], 0.8)
        # About 4.2 km of ground track (423 m/s along the ground for 10 s), all rich: a contact every MERGE.
        self.assertGreaterEqual(len(s.contacts), 3)
        self.assertLessEqual(len(s.contacts), 6)
        for a in s.contacts:
            self.assertEqual(a.richness, 0.8)
            self.assertEqual(a.height, 100.0)
            for b in s.contacts:
                if a is not b:
                    self.assertGreaterEqual(length(sub(a.direction, b.direction)) * RADIUS, scanner.MERGE)

    def test_shares_and_releases_the_probes(self):
        world = World(lambda d: 0.0)
        s = scanner.Scanner(world, 4)
        position, velocity = orbit()
        theirs = [((0.0, 1.0, 0.0), self.body)]
        world.extra_probes = theirs
        for _ in range(10):
            s.update(0.1, position, velocity, self.body, GM)
        self.assertEqual(world.extra_probes, theirs)
        world.extra_probes = []
        for _ in range(3):
            s.update(0.1, position, velocity, self.body, GM)
        self.assertEqual(len(world.extra_probes), 4)
        s.update(0.1, position, velocity, self.body, GM, active=False)
        self.assertEqual(world.extra_probes, [])

    def test_merge_limit_nearest_and_records(self):
        s = scanner.Scanner(World(lambda d: 0.0), 4)
        up = (0.0, 1.0, 0.0)
        near = normalize((500.0 / RADIUS, 1.0, 0.0))
        s.note(self.body, up, 0.7, 10.0)
        s.note(self.body, near, 0.9, 20.0)
        self.assertEqual(len(s.contacts), 1)
        self.assertEqual((s.contacts[0].richness, s.contacts[0].direction), (0.9, near))
        for k in range(scanner.MAX_CONTACTS + 5):
            s.note(self.body, normalize((math.cos(k), 0.0, math.sin(k))), 0.6 + 0.001 * k, 0.0)
        self.assertEqual(len(s.contacts), scanner.MAX_CONTACTS)
        self.assertNotIn(0.6, [c.richness for c in s.contacts])
        contact, distance = s.nearest(self.body, (0.0, RADIUS + 1_000.0, 0.0))
        self.assertIs(contact, s.contacts[0])
        self.assertAlmostEqual(distance, length(sub(contact.point(), (0.0, RADIUS + 1_000.0, 0.0))))
        again = scanner.Scanner(World(lambda d: 0.0), 4)
        again.restore(s.records(), {"planet": self.body})
        self.assertEqual([(c.direction, c.richness, c.height) for c in again.contacts],
                         [(c.direction, c.richness, c.height) for c in s.contacts])
        self.assertIsNone(s.nearest(object(), (0.0, RADIUS, 0.0)))


def length(v):
    return math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def normalize(v):
    n = length(v)
    return (v[0] / n, v[1] / n, v[2] / n)


if __name__ == "__main__":
    unittest.main()

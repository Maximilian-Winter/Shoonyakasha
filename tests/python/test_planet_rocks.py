"""Tests for shoonyakasha.planet.rocks and the planet example's rocks
(examples/python/simulations/planet/rocks.py): the taken rocks' keys, slot
and table (each key found by linear probing from its slot as
vegetation_scatter.comp's rockTaken finds it, the hash and constants the
shader's), its upload only when changed, its limit and clearing one body;
the rocks' colours as the shader's; and on sk.PlanetPhysics a fixed rock
made for a listed rock (a static hull standing on its base, its mass from
its hull at the rocks' density, strikes on it watched), rocks of the other
body and taken ones left out, and a promotion (the body dynamic, its cell
taken, an entity in its colour, a prop) and its release, a loose
rock's record made again into the same rock, the ore rocks' colour as
the shader's, and an ore boulder, fixed or loose, broken by a fast strike
into 3 to 6 chunks of its mass and grade, its cell kept taken. Where this
interpreter's shoonyakasha has no extension, the physics tests run in a
fresh interpreter.

    python -m unittest discover -s tests/python
"""

import math
import os
import re
import subprocess
import sys
import unittest
from pathlib import Path

import numpy as np

import shoonyakasha as sk
from shoonyakasha.planet import rocks, world
from shoonyakasha.planet.bubble import Bubble
from shoonyakasha.planet.nearby_trees import NearbyTree

PLANET_DIR = Path(__file__).resolve().parents[2] / "examples" / "python" / "simulations" / "planet"
SHADER = world.PIPELINE.parent / "shaders" / "vegetation_scatter.comp"


class FakeScene:
    def __init__(self):
        self.calls = []

    def __getattr__(self, name):
        return lambda *args: self.calls.append((name,) + args)


class FakeEngine:
    def __init__(self):
        self.buffers = {}
        self.scene = FakeScene()
        self.meshes = []

    def set_buffer(self, name, data):
        self.buffers[name] = np.array(data)

    def create_mesh(self, positions, normals=None, colors=None, name=""):
        self.meshes.append((positions, normals, colors, name))
        return 1000 + len(self.meshes)


class TakenRocksTest(unittest.TestCase):
    def test_table_finds_every_key_from_its_slot(self):
        taken = rocks.TakenRocks(FakeEngine())
        keys = [(b, g, f, cu, cv) for b in (0, 1) for g in (0, 1) for f in range(6) for cu, cv in
                ((0, 0), (65535, 1), (17, 65535), (40000, 123))]
        for key in keys:
            self.assertTrue(taken.add(key))
        table = taken.table().reshape(-1, 2)
        self.assertEqual(int((table[:, 0] != rocks.EMPTY).sum()), len(keys))
        for key in keys:
            words = rocks.taken_words(key)
            s = rocks.slot(words)
            while tuple(table[s]) != words:
                self.assertNotEqual(table[s, 0], rocks.EMPTY, key)
                s = (s + 1) % rocks.SLOTS
        absent = rocks.taken_words((1, 1, 5, 9, 9))
        s = rocks.slot(absent)
        while table[s, 0] != rocks.EMPTY:
            self.assertNotEqual(tuple(table[s]), absent)
            s = (s + 1) % rocks.SLOTS

    def test_hash_and_constants_are_the_shaders(self):
        shader = SHADER.read_text(encoding="utf-8")
        self.assertIn("TAKEN_SLOTS = %du" % rocks.SLOTS, shader)
        self.assertIn("(key.x * 0x9E3779B1u) ^ (key.y * 0x85EBCA77u) ^ (key.x >> 16)", shader)
        self.assertIn("uint(body) | (uint(face) << 1) | (uint(grid) << 4) | ((uint(cell.x) & 0xFFFFu) << 8)", shader)
        self.assertEqual(rocks.taken_words((1, 1, 5, 300, 7)), (1 | 5 << 1 | 1 << 4 | 300 << 8, 7))
        for name, body in (("PLANET_ROCK", 0), ("MOON_ROCK", 1)):
            values = re.search(r"const vec3 %s = vec3\(([^)]*)\);" % name, shader).group(1)
            self.assertEqual(tuple(float(v) for v in values.split(",")), rocks.ROCK_COLOURS[body])

    def test_upload_when_changed_limit_and_clear_body(self):
        engine = FakeEngine()
        taken = rocks.TakenRocks(engine)
        self.assertEqual(taken.upload(), 0)
        self.assertIn("TakenRocks", engine.buffers)
        engine.buffers.clear()
        self.assertEqual(taken.upload(), 0)
        self.assertNotIn("TakenRocks", engine.buffers)
        taken.add((0, 0, 1, 2, 3))
        taken.add((1, 1, 1, 2, 3))
        self.assertEqual(taken.upload(), 2)
        self.assertIn("TakenRocks", engine.buffers)
        taken.clear_body(0)
        self.assertEqual(set(taken.keys), {(1, 1, 1, 2, 3)})
        for k in range(rocks.MAX_TAKEN):
            taken.add((0, 0, 0, k, 0))
        self.assertFalse(taken.add((0, 0, 0, 0, 999)))
        self.assertEqual(len(taken), rocks.MAX_TAKEN)

    def test_tinted(self):
        self.assertEqual(rocks.tinted((1.0, 0.5, 0.0), 0.0), (0.8, 0.4, 0.0))
        self.assertAlmostEqual(rocks.tinted((1.0, 0.5, 0.0), 1.0)[0], 1.15)

    def test_ore_colour_is_the_shaders(self):
        shader = SHADER.read_text(encoding="utf-8")
        self.assertIn("const float ORE_ROCKS = %s;" % rocks.ORE_ROCKS, shader)
        self.assertIn("const float ORE_TINT_SHARE = %s;" % rocks.ORE_TINT_SHARE, shader)
        values = re.search(r"const vec3 ORE_TINT = vec3\(([^)]*)\);", shader).group(1)
        self.assertEqual(tuple(float(v) for v in values.split(",")), rocks.ORE_TINT)
        self.assertEqual(rocks.rock_colour(1, 0.0), rocks.ROCK_COLOURS[1])
        self.assertEqual(rocks.rock_colour(0, rocks.ORE_ROCKS), rocks.ROCK_COLOURS[0])
        share = rocks.ORE_TINT_SHARE * 1.5
        self.assertAlmostEqual(rocks.rock_colour(0, 0.5)[0],
                               rocks.ROCK_COLOURS[0][0] + (rocks.ORE_TINT[0] - rocks.ROCK_COLOURS[0][0]) * share)


class Body:
    name = "planet"

    def __init__(self, radius, index=0):
        self.radius = radius
        self.index = index

    def centre(self):
        return (0.0, 0.0, 0.0)

    def relative(self, point):
        return point


class FakeWorld:
    def __init__(self, engine):
        self.taken_rocks = rocks.TakenRocks(engine)


class ExampleRocksTest(unittest.TestCase):
    def setUp(self):
        if not sk.extension_available():
            if __name__ == "__main__":
                self.fail("no engine extension in a fresh interpreter")
            self.skipTest("runs in a fresh interpreter (test_in_fresh_interpreter)")
        sys.path.insert(0, str(PLANET_DIR))
        from shoonyakasha.planet import props
        import rocks as example_rocks
        self.example = example_rocks
        radius = 100_000.0
        self.physics = sk.PlanetPhysics(max_bodies=64, threads=1)
        self.bubble = Bubble(self.physics, Body(radius), 4.905 * radius ** 2, (0.0, radius, 0.0))
        self.engine = FakeEngine()
        self.world = FakeWorld(self.engine)
        self.props = props.Props(self.engine, ([], []), self.world, on_remove=lambda p: self.rocks.released(p))
        self.rocks = example_rocks.Rocks(self.engine, self.world, self.props)
        self.radius = radius

    def rock(self, key, x, height=0.5, kind="boulder", grade=0.0):
        return NearbyTree(key, kind, (x, self.radius, 0.0), height, 1.0, (1.0, 0.0), 0.5, grade)

    def test_ore_boulder_splits(self):
        here = (0.0, self.radius + 1.7, 0.0)
        plain, ore = self.rock((0, 0, 4, 1, 1), 3.0, 1.5), self.rock((0, 0, 4, 1, 2), -3.0, 1.5, grade=0.8)
        self.rocks.update(self.bubble, [plain, ore], here, 40.0)
        fixed = self.rocks.rocks[ore.key]
        self.assertEqual(fixed.grade, 0.8)
        self.assertEqual(fixed.colour, rocks.tinted(rocks.rock_colour(0, 0.8), 0.5))
        self.assertFalse(self.rocks.breaks(fixed.body, self.example.BREAK_SPEED - 1.0))
        self.assertTrue(self.rocks.breaks(fixed.body, self.example.BREAK_SPEED))
        self.assertFalse(self.rocks.breaks(self.rocks.rocks[plain.key].body, 100.0))
        chunks = self.rocks.split(self.bubble, fixed.body)
        self.assertTrue(3 <= len(chunks) <= 6)
        self.assertAlmostEqual(sum(self.physics.mass(c.body) for c in chunks), fixed.mass, delta=1e-3 * fixed.mass)
        self.assertTrue(all(c.grade == 0.8 and c.kind == "rock" and c.key is None and c.glow is not None
                            for c in chunks))
        self.assertIn(ore.key, self.world.taken_rocks)
        self.assertNotIn(ore.key, self.rocks.rocks)
        self.assertFalse(self.physics.contains(fixed.body))
        # The same cell splits into the same chunks; removing the chunks keeps the cell taken.
        self.props.clear(self.bubble)
        self.assertIn(ore.key, self.world.taken_rocks)

    def test_loose_ore_boulder_watched_and_splits(self):
        here = (0.0, self.radius + 1.7, 0.0)
        ore = self.rock((0, 0, 4, 1, 2), 3.0, 1.5, grade=0.5)
        self.rocks.update(self.bubble, [ore], here, 40.0)
        body = self.rocks.rocks[ore.key].body
        self.rocks.promote(self.bubble, body)
        loose = self.props.rocks[0]
        self.assertTrue(loose.breakable)
        self.assertIsNotNone(loose.glow)
        self.assertTrue(self.rocks.breaks(body, self.example.BREAK_SPEED))
        chunks = self.rocks.split(self.bubble, body)
        self.assertTrue(3 <= len(chunks) <= 6)
        self.assertNotIn(loose, self.props.rocks)
        self.assertIn(ore.key, self.world.taken_rocks)

    def test_fixed_rocks_and_promotion(self):
        here = (0.0, self.radius + 1.7, 0.0)
        mine = self.rock((0, 0, 4, 1, 1), 3.0)
        other = NearbyTree((1, 0, 4, 1, 2), "rock", (5.0, self.radius, 0.0), 0.2, 1.0)
        taken = self.rock((0, 0, 4, 1, 3), -3.0)
        self.world.taken_rocks.add(taken.key)
        self.rocks.update(self.bubble, [mine, other, taken], here, 40.0)
        self.assertEqual(list(self.rocks.rocks), [mine.key])
        rock = self.rocks.rocks[mine.key]
        self.assertEqual(self.physics.motion(rock.body), "static")
        # Its origin half its height above its base; its hull from its base up.
        p = self.physics.get_transform(rock.body)[0]
        self.assertAlmostEqual(p[1], 0.25, places=5)
        hit = self.physics.cast_ray((3.0, 5.0, 0.0), (0.0, -1.0, 0.0), 10.0)
        self.assertEqual(hit[0], rock.body)
        self.assertAlmostEqual(hit[1][1], 0.5, delta=0.05)
        self.assertTrue(self.rocks.is_static_rock(rock.body))
        body, radius, mass = self.rocks.grabbable()[0]
        self.assertEqual(body, rock.body)
        self.assertGreater(radius, 0.5)

        self.assertTrue(self.rocks.promote(self.bubble, rock.body))
        self.assertEqual(self.physics.motion(rock.body), "dynamic")
        self.assertAlmostEqual(self.physics.mass(rock.body), mass, delta=1e-3 * mass)
        self.assertGreater(mass, 0.3 * rocks.DENSITY * 1.8 * 0.5 * 1.8 * 0.5 * 0.5)
        self.assertIn(mine.key, self.world.taken_rocks)
        self.assertEqual(self.rocks.rocks, {})
        self.assertEqual(len(self.props.rocks), 1)
        self.assertEqual(len(self.engine.meshes), 1)
        colour = [c for c in self.engine.scene.calls if c[0] == "set_material_vec4" and c[2] == "baseColorFactor"]
        self.assertEqual(colour[-1][3][:3], rocks.tinted(rocks.ROCK_COLOURS[0], 0.5))
        self.assertFalse(self.rocks.promote(self.bubble, rock.body))
        # Listed again by a list made before the promotion: left out, the cell is taken.
        self.rocks.update(self.bubble, [mine], here, 40.0)
        self.assertEqual(self.rocks.rocks, {})
        self.props.clear(self.bubble)
        self.assertNotIn(mine.key, self.world.taken_rocks)

    def test_record_and_restore(self):
        here = (0.0, self.radius + 1.7, 0.0)
        mine = self.rock((0, 0, 4, 1, 1), 3.0)
        self.rocks.update(self.bubble, [mine], here, 40.0)
        self.rocks.promote(self.bubble, self.rocks.rocks[mine.key].body)
        loose = self.props.rocks[0]
        record = self.props.record(self.bubble, loose)
        self.assertEqual((record["kind"], record["on"], record["key"]), ("boulder", "planet", list(mine.key)))
        self.assertEqual(record["extent"], list(loose.extent))
        self.assertEqual(record["colour"], list(rocks.tinted(rocks.ROCK_COLOURS[0], 0.5)))
        points, mass = loose.shape[1], loose.mass
        self.props.clear(self.bubble)
        self.world.taken_rocks.add(mine.key)
        restored = self.rocks.restore(self.physics, record, {"planet": self.bubble.body})
        self.assertIsNone(restored.body)
        self.assertEqual(restored.key, mine.key)
        self.assertEqual(restored.mass, mass)
        np.testing.assert_allclose(restored.shape[1], points, rtol=1e-6)
        self.assertEqual(self.props.record(self.bubble, restored), record)
        scales = [c for c in self.engine.scene.calls if c[0] == "set_scale"]
        self.assertEqual(scales[-1][2], tuple(record["extent"]))

    def test_strikes_watched_and_far_rocks_removed(self):
        here = (0.0, self.radius + 1.7, 0.0)
        mine = self.rock((0, 0, 4, 1, 1), 3.0)
        self.rocks.update(self.bubble, [mine], here, 40.0)
        rock = self.rocks.rocks[mine.key]
        crate = self.physics.create_box((0.2, 0.2, 0.2), (3.0, 3.0, 0.0), mass=100.0)
        self.physics.set_velocity(crate, (0.0, -10.0, 0.0))
        for _ in range(60):
            self.bubble.step(1.0 / 120.0)
        self.assertEqual(self.physics.take_struck(), [rock.body])
        self.rocks.update(self.bubble, [], (0.0, self.radius + 1.7, 60.0), 40.0)
        self.assertEqual(self.rocks.rocks, {})
        self.assertFalse(self.physics.contains(rock.body))


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

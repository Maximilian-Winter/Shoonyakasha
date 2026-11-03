"""Tests for shoonyakasha.planet.nearby_trees and the planet example's
trunks (shoonyakasha.planet.trunks): the NearbyTrees
buffer and parameter fit the pipeline, every plant with a trunk has its
trunk's shares, the width factor matches vegetation.glsl's, a list read
back by stamp (each plant's key, kind, base from that frame's camera,
height and width, an ore rock's grade; the count capped; older stamps dropped; an unknown stamp
giving None), and on sk.PlanetPhysics the trunks made for a list: a
capsule per plant standing along the up, kept while the list leaves it
out, made again when its base moves, and removed beyond the radius.

    python -m unittest discover -s tests/python
"""

import json
import math
import os
import subprocess
import sys
import unittest

import numpy as np

import shoonyakasha as sk
from shoonyakasha.planet import nearby_trees, vegetation, world
from shoonyakasha.planet.bubble import Bubble
from shoonyakasha.planet.nearby_trees import CAPACITY, HEADER, WORDS, NearbyTree, NearbyTrees



class FakeEngine:
    def __init__(self):
        self.custom = {}
        self.buffers = {}
        self.readback = None

    def set_custom_vec4(self, name, value):
        self.custom[name] = value

    def set_buffer(self, name, data):
        self.buffers[name] = data

    def get_readback(self, name):
        return self.readback


def readback(stamp, plants, count=None):
    """NearbyTrees' words: (relative, size, tint, kind, face, cu, cv, body, turn) per plant."""
    words = np.zeros(HEADER + CAPACITY * WORDS, dtype=np.uint32)
    words[0] = len(plants) if count is None else count
    words[1] = stamp
    for k, (relative, size, tint, kind, face, cu, cv, body, turn) in enumerate(plants):
        at = HEADER + k * WORDS
        words[at:at + 5] = np.array(list(relative) + [size, tint], dtype=np.float32).view(np.uint32)
        words[at + 5:at + 10] = np.array([kind, face, cu, cv, body], dtype=np.int32).view(np.uint32)
        words[at + 10:at + 12] = np.array([np.cos(turn), np.sin(turn)], dtype=np.float32).view(np.uint32)
    return {"data": words.tobytes(), "frame": 0}


class NearbyTreesTest(unittest.TestCase):
    def test_buffer_and_parameter_fit_the_pipeline(self):
        pipeline = json.loads(world.PIPELINE.read_text())
        layout = pipeline["bufferLayouts"]["NearbyTrees"]
        self.assertEqual(layout["elementCount"], HEADER + CAPACITY * WORDS)
        nearby = [f for f in pipeline["bufferLayouts"]["VegetationParams"]["fields"] if f["name"] == "nearby"][0]
        self.assertEqual(nearby["default"][2], CAPACITY)
        shader = (world.PIPELINE.parent / "shaders" / "vegetation_scatter.comp").read_text()
        self.assertIn("NEARBY_HEADER = %du" % HEADER, shader)
        self.assertIn("NEARBY_WORDS = %du" % WORDS, shader)

    def test_every_trunk_has_its_shares(self):
        for name in vegetation.TREES + ("cactus",):
            radius, top = nearby_trees.TRUNKS[name]
            self.assertGreater(radius, 0.0)
            self.assertLessEqual(top, 1.0)
        tree = NearbyTree((0, 0, 0, 1, 2), "conifer_a_far", (0.0, 0.0, 0.0), 10.0, 1.1)
        radius, height = tree.trunk
        self.assertAlmostEqual(radius, nearby_trees.TRUNKS["conifer_a"][0] * 11.0)
        self.assertAlmostEqual(height, nearby_trees.TRUNKS["conifer_a"][1] * 10.0)

    def test_width_factor(self):
        for tint in (0.0, 0.1, 0.37, 0.999):
            t = np.float32(tint) * np.float32(7.31)
            self.assertAlmostEqual(nearby_trees.width_factor(tint), 0.85 + 0.3 * float(t - np.floor(t)), places=6)
            self.assertTrue(0.85 <= nearby_trees.width_factor(tint) <= 1.15)

    def test_read_by_stamp(self):
        engine = FakeEngine()
        nearby = NearbyTrees(engine)
        self.assertIsNone(nearby.read())
        nearby.request((100.0, 0.0, 0.0), 40.0)
        nearby.request((200.0, 0.0, 0.0), 40.0, [(0.0, 0.0, 0.0), (1000.0, 0.0, 0.0)])
        self.assertEqual(engine.custom["planet.vegetation.nearby"], (40.0, 2.0, float(CAPACITY), 0.0))
        self.assertEqual(int(engine.buffers["NearbyTrees"][0]), 0)
        engine.readback = readback(2, [((1.0, 2.0, -3.0), 12.0, 0.25, vegetation.KINDS.index("acacia"), 4, -7, 9, 0, 0.0),
                                       ((0.5, 0.0, 0.0), 0.2, 0.5, vegetation.KINDS.index("rock"), 2, 70, 80, 1, 1.0),
                                       ((0.0, 0.0, 5.0), 0.9, 0.5, vegetation.KINDS.index("boulder") | (204 << 8),
                                        3, 7, 8, 1, 2.0)])
        stamp, keys = nearby_trees.listed_keys(engine.readback["data"])
        self.assertIn((1, 0, 3, 7, 8), keys)
        trees = nearby.read()
        self.assertEqual(len(trees), 3)
        tree, rock, boulder = trees
        self.assertEqual(tree.key, (0, 0, 4, -7, 9))
        self.assertEqual(tree.kind, "acacia")
        self.assertFalse(tree.rock)
        self.assertEqual(tree.base, (201.0, 2.0, -3.0))
        self.assertAlmostEqual(tree.height, 12.0)
        self.assertAlmostEqual(tree.width, nearby_trees.width_factor(0.25))
        self.assertAlmostEqual(tree.tint, 0.25)
        # Small rocks lie on grid 1, boulders on the trees' grid 0.
        self.assertEqual(rock.key, (1, 1, 2, 70, 80))
        self.assertEqual(boulder.key, (1, 0, 3, 7, 8))
        self.assertTrue(rock.rock and boulder.rock)
        self.assertEqual(rock.body, 1)
        # An ore boulder's grade in the kind's word from bit 8.
        self.assertEqual(boulder.kind, "boulder")
        self.assertAlmostEqual(boulder.grade, 204.0 / 255.0)
        self.assertEqual((rock.grade, tree.grade), (0.0, 0.0))
        # Bases from their body's centre as it was that frame.
        self.assertEqual(rock.base, (200.5 - 1000.0, 0.0, 0.0))
        self.assertAlmostEqual(rock.turn[0], np.cos(1.0), places=6)
        self.assertAlmostEqual(rock.turn[1], np.sin(1.0), places=6)
        # Stamp 1 was older than 2: dropped; stamp 2 taken: a second read of it gives None.
        self.assertIsNone(nearby.read())
        engine.readback = readback(1, [])
        self.assertIsNone(nearby.read())

    def test_count_capped(self):
        engine = FakeEngine()
        nearby = NearbyTrees(engine)
        nearby.request((0.0, 0.0, 0.0), 40.0)
        engine.readback = readback(1, [((0.0, 0.0, 0.0), 1.0, 0.0, 0, 0, 0, 0, 0, 0.0)] * CAPACITY, count=CAPACITY + 50)
        self.assertEqual(len(nearby.read()), CAPACITY)


class Body:
    def __init__(self, radius):
        self.radius = radius
        self.index = 0

    def centre(self):
        return (0.0, 0.0, 0.0)

    def relative(self, point):
        return point


class TrunksTest(unittest.TestCase):
    def setUp(self):
        if not sk.extension_available():
            if __name__ == "__main__":
                self.fail("no engine extension in a fresh interpreter")
            self.skipTest("runs in a fresh interpreter (test_in_fresh_interpreter)")
        from shoonyakasha.planet import trunks
        self.trunks_module = trunks

    def test_capsules_kept_moved_and_removed(self):
        radius = 100_000.0
        physics = sk.PlanetPhysics(max_bodies=64, threads=1)
        bubble = Bubble(physics, Body(radius), 4.905 * radius ** 2, (0.0, radius, 0.0))
        trunks = self.trunks_module.Trunks()
        camera = (0.0, radius + 1.7, 0.0)
        near = NearbyTree((0, 0, 4, 0, 0), "broadleaf_a", (5.0, radius, 0.0), 12.0, 1.0)
        far = NearbyTree((0, 0, 4, 1, 0), "conifer_a", (0.0, radius, -30.0), 15.0, 1.0)
        trunks.update(bubble, [near, far], camera, 40.0)
        self.assertEqual(len(trunks.trunks), 2)
        trunk_radius, height = near.trunk
        # A ray across the near trunk 1 m up hits its side; one above its top misses it.
        hit = physics.cast_ray((0.0, 1.0, 0.0), (1.0, 0.0, 0.0), 10.0)
        self.assertEqual(hit[0], trunks.trunks[near.key].body)
        self.assertAlmostEqual(hit[1][0], 5.0 - trunk_radius, delta=2e-3)
        self.assertIsNone(physics.cast_ray((0.0, height + trunk_radius + 0.1, 0.0), (1.0, 0.0, 0.0), 10.0))
        # Left out of a list: kept. Its base moved: made again.
        body = trunks.trunks[near.key].body
        trunks.update(bubble, [], camera, 40.0)
        self.assertEqual(trunks.trunks[near.key].body, body)
        moved = NearbyTree(near.key, near.kind, (6.0, radius, 0.0), 12.0, 1.0)
        trunks.update(bubble, [moved], camera, 40.0)
        self.assertFalse(physics.contains(body))
        self.assertTrue(physics.contains(trunks.trunks[near.key].body))
        # The camera 25 m south: the far trunk is 55 m off, beyond 40 + KEEP; the near one 26 m, kept.
        trunks.update(bubble, [], (0.0, radius + 1.7, 25.0), 40.0)
        self.assertEqual(set(trunks.trunks), {near.key})
        trunks.clear(bubble)
        self.assertEqual(trunks.trunks, {})
        self.assertEqual(physics.body_count, 0)


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

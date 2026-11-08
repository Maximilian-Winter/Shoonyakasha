"""Tests for shoonyakasha.planet.outpost_kit and outpost_layout: the parts'
triangles facing out and their hulls within budget, purposes seeded and
differing on a body, layouts the same for the same seed, every part on the
site, none overlapping, the approach corridor clear, and the site model's
pieces and bodies.

    python -m unittest discover -s tests/python
"""

import math
import random
import unittest

import numpy as np

from shoonyakasha.planet import outpost_kit as kit
from shoonyakasha.planet import outpost_layout as layout

SEEDS = (1, 7, 42, 1234)
BODIES = {"planet": ["Kestrel", "Halden", "Varga"], "moon": ["Lowrim", "Selene Deep"]}


def all_layouts():
    for seed in SEEDS:
        for purpose in layout.PURPOSES:
            yield seed, purpose, layout.layout(seed, "Outpost %s" % purpose, purpose)


class KitTest(unittest.TestCase):
    def test_box_faces_out(self):
        g = kit.Geometry()
        g.box((1.0, 2.0, 3.0), (2.0, 1.0, 4.0), kit.WHITE, kit.yaw(0.4))
        positions, normals, colours = g.arrays()
        self.assertEqual(len(positions), 36)
        centres = positions.reshape(-1, 3, 3).mean(axis=1)
        out = centres - np.array([1.0, 2.0, 3.0])
        self.assertTrue(np.all(np.sum(normals[0::3] * out, axis=1) > 0.0))
        np.testing.assert_allclose(colours, np.tile(kit.WHITE, (36, 1)), rtol=1e-6)

    def test_round_shapes_face_out(self):
        g = kit.Geometry()
        g.cylinder((0.0, 0.0, 0.0), (0.0, 1.0, 0.0), 2.0, 3.0, kit.WHITE)
        g.dome((0.0, 0.0, 0.0), 2.0, kit.WHITE)
        g.mound((0.0, 0.0, 0.0), 3.0, 2.0, kit.WHITE, random.Random(1))
        positions, normals, _ = g.arrays()
        centres = positions.reshape(-1, 3, 3).mean(axis=1)
        out = centres - np.array([0.0, 1.0, 0.0])
        out[:, 1] = np.where(np.abs(centres[:, 1]) < 1e-6, -1.0, out[:, 1])
        facing = np.sum(normals[0::3] * out, axis=1)
        area = np.linalg.norm(np.cross(positions[1::3] - positions[0::3], positions[2::3] - positions[0::3]), axis=1)
        self.assertTrue(np.all(facing[area > 1e-9] > -1e-6))

    def test_every_part_builds(self):
        for name in kit.PARTS:
            part = kit.build(name, random.Random(3))
            self.assertEqual(part.name, name)
            self.assertTrue(part.pieces)
            for piece in part.pieces:
                self.assertGreater(len(piece.solid) + len(piece.lamps) + len(piece.screens), 0)
                for hull in piece.hulls:
                    self.assertTrue(4 <= len(hull) <= 64, (name, len(hull)))
                self.assertEqual(piece.dynamic, piece.mass > 0.0)
        self.assertIsNotNone(kit.build("mast", random.Random(1), height=20.0).beacon)


class LayoutTest(unittest.TestCase):
    def test_purposes(self):
        fixed = {"Selene Deep": "fuel"}
        for seed in SEEDS:
            p = layout.purposes(seed, BODIES, fixed)
            self.assertEqual(set(p), {n for names in BODIES.values() for n in names})
            self.assertEqual(p["Selene Deep"], "fuel")
            self.assertEqual(len({p[n] for n in BODIES["planet"]}), 3)
            self.assertNotEqual(p["Lowrim"], "fuel")
            self.assertEqual(p, layout.purposes(seed, BODIES, fixed))
        self.assertGreater(len({tuple(sorted(layout.purposes(s, BODIES).items())) for s in range(20)}), 1)

    def test_same_seed_same_layout(self):
        a = layout.layout(5, "Kestrel", "mining")
        self.assertEqual(a, layout.layout(5, "Kestrel", "mining"))
        self.assertNotEqual(a, layout.layout(6, "Kestrel", "mining"))

    def test_parts_on_the_site_apart_and_off_the_approach(self):
        for seed, purpose, placed in all_layouts():
            pad = placed[0]
            self.assertEqual((pad.name, pad.x, pad.z), ("pad", 0.0, 0.0))
            clear = layout.corridor(pad.heading)
            names = [p.name for p in placed]
            self.assertIn("terminal", names)
            self.assertIn("ore_bin", names)
            self.assertIn("mast", names)
            self.assertGreater(names.count("barrier"), 10)
            footprints = [p.footprint(layout.half_of(p.name, p.params, None)) for p in placed]
            for k, (p, f) in enumerate(zip(placed, footprints)):
                reach = max(np.linalg.norm(layout.corners(*f), axis=1))
                self.assertLess(reach, layout.OUTER + layout.BARRIER_OUT + 3.0, (seed, purpose, p.name))
                if k:
                    self.assertFalse(layout.overlaps(f, clear), (seed, purpose, p.name))
                for q, g in zip(placed[k + 1:], footprints[k + 1:]):
                    if {p.name, q.name} == {"pipe", "tank_farm"}:
                        continue
                    self.assertFalse(layout.overlaps(f, g), (seed, purpose, p.name, q.name))
            if purpose == "fuel":
                self.assertIn("tank_farm", names)
            if purpose == "research":
                self.assertIn("dome", names)

    def test_solar_arrays_lean_towards_the_equator(self):
        placed = layout.layout(3, "Kestrel", "research", equator=0.5)
        solar = [p for p in placed if p.name == "solar"]
        self.assertTrue(solar)
        for p in solar:
            self.assertLess(abs(math.remainder(p.heading - 0.5, 2.0 * math.pi)), 0.2)

    def test_site_model(self):
        placed = layout.layout(9, "Halden", "trade")
        model = layout.site_model(placed, 25_000.0, 9)
        positions, normals, colours = model.solid
        self.assertEqual(positions.shape, normals.shape)
        self.assertEqual(len(positions) % 3, 0)
        np.testing.assert_allclose(np.linalg.norm(normals, axis=1), 1.0, atol=1e-4)
        dynamic = [b for b in model.bodies if b.dynamic]
        self.assertEqual(len(dynamic), len(model.loose))
        self.assertEqual(sorted(b.loose for b in dynamic), list(range(len(model.loose))))
        self.assertEqual(sum(p.name == "barrier" for p in placed)
                         + sum(p.params["count"] for p in placed if p.name == "container_stack"), len(model.loose))
        self.assertEqual(len(model.beacons), 1)
        self.assertEqual(len(model.bins), 1)
        self.assertGreater(len(model.lamps[0]), 0)
        self.assertGreater(len(model.screens[0]), 0)
        # Parts away from the centre sit lower, on the body's curve.
        far = max(placed, key=lambda p: math.hypot(p.x, p.z))
        barrier = next(b for b in model.bodies if b.dynamic and math.isclose(b.position[0], far.x))
        drop = (far.x ** 2 + far.z ** 2) / (2.0 * 25_000.0)
        self.assertAlmostEqual(barrier.position[1], 0.5 * kit.BARRIER[1] - drop, places=6)
        merged = layout.merged(model.loose)
        self.assertEqual(len(merged[0]), sum(len(piece.triangles[0]) for piece in model.loose))


if __name__ == "__main__":
    unittest.main()

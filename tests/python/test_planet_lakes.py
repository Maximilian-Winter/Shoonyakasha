"""Tests for the lakes of the planet pipeline (lakes.glsl), on the GPU,
with the lakes packed by PlanetWorld.set_lakes: within the shore the
ground is the bowl under the water level (its height plus the water's
depth there is the level, the depth largest at the centre) and lakeAt
names the lake; beyond the bank the terrain is the planet's own and no
lake holds it; on the bank the ground lies between the water level and
the planet's own; the ground stays continuous across the shore and the
bank; the shore moves with the wobble within its reach; and lakeNear
finds the lake whose water comes nearest within the reach given. The
tests need a Vulkan 1.3 device and glslc; without them they are skipped.

    python -m unittest discover -s tests/python
"""

import math
import shutil
import struct
import tempfile
import unittest
from pathlib import Path

import numpy as np

import shoonyakasha as sk
from shoonyakasha import shaders
from shoonyakasha.planet import world
from shoonyakasha.planet.vec import add, local_frame, normalize, scale
from shoonyakasha.planet.world import LAKE_WOBBLE_REACH, Lake

KERNEL = """#version 450
layout(local_size_x = 64) in;
layout(std140, set = 0, binding = 0) uniform TerrainParams {
    vec4 continent; vec4 mountains; vec4 hills; vec4 seed; vec4 site; vec4 siteShape; vec4 detail;
    vec4 climate; vec4 biomeView; vec4 sites[8]; vec4 siteShapes[8]; vec4 lakes[32]; vec4 lakeShapes[32];
} terrain;
layout(std430, set = 0, binding = 1) readonly buffer Inputs { vec4 items[]; } src;
layout(std430, set = 0, binding = 2) writeonly buffer Outputs { vec4 values[]; } dst;
layout(push_constant) uniform Push { uint count; } pc;

#include "terrain.glsl"
#include "lakes.glsl"

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    vec3 d = normalize(src.items[i].xyz);
    float natural = terrainHeight(d);
    float depth;
    int lake = lakeAt(d, depth);
    dst.values[2u * i] = vec4(natural, terrainWithLakes(natural, d), float(lake), depth);
    dst.values[2u * i + 1u] = vec4(float(lakeNear(d, src.items[i].w)), 0.0, 0.0, 0.0);
}
"""
CENTRE = normalize((0.6, 0.0, 0.8))
OTHER = normalize((0.6, 0.05, 0.8))
RADIUS, DEPTH, BANK, WOBBLE = 300.0, 15.0, 180.0, 0.25
REACH = RADIUS * (1.0 + LAKE_WOBBLE_REACH * WOBBLE)


class FakeEngine:
    def __init__(self):
        self.custom = {}

    def set_custom_vec4(self, name, value):
        self.custom[name] = tuple(value)

    def set_custom_vec4_array(self, name, values):
        self.custom[name] = [tuple(v) for v in values]


class NoLod:
    def has_tree(self, index):
        return False


def towards(direction, bearing, distance):
    east, north = local_frame(direction)
    tangent = add(scale(north, math.cos(bearing)), scale(east, math.sin(bearing)))
    angle = distance / world.RADIUS
    return normalize(add(scale(direction, math.cos(angle)), scale(tangent, math.sin(angle))))


def ring(distance, count=16, centre=CENTRE):
    return [towards(centre, 2.0 * math.pi * k / count, distance) for k in range(count)]


class LakesShader(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not sk.extension_available():
            raise unittest.SkipTest("the engine's extension is not importable here")
        try:
            shaders.find_glslc()
        except shaders.GlslcNotFound as exc:
            raise unittest.SkipTest(str(exc).splitlines()[0])
        try:
            cls.ctx = sk.ComputeContext()
        except RuntimeError as exc:
            raise unittest.SkipTest("no Vulkan device: %s" % exc)
        cls.tmp = Path(tempfile.mkdtemp())
        path = cls.tmp / "lakes_probe.comp"
        path.write_text(KERNEL, encoding="utf-8")
        cls.kernel = cls.ctx.kernel(path, args=["-I", str(world.PIPELINE.parent / "shaders")])
        natural = cls.run_kernel(cls, [], [CENTRE, OTHER])[:, 0]
        # Each lake's water at the natural height at its centre, so its bank eases across the local relief alone.
        cls.lake = Lake(CENTRE, float(natural[0]), RADIUS, DEPTH, BANK, WOBBLE)
        cls.second = Lake(OTHER, float(natural[1]), 200.0, 10.0, 100.0, 0.0)

    @classmethod
    def tearDownClass(cls):
        cls.ctx.shutdown()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_kernel(self, lakes, directions, reach=0.0):
        """Per direction: (natural, with the lakes, lakeAt's index, its depth, lakeNear's index within `reach`)."""
        engine = FakeEngine()
        w = world.PlanetWorld(engine)
        w._lod = NoLod()
        w.set_lakes(lakes)
        t = world.TERRAIN
        params = (list(t["continent"]) + list(t["mountains"]) + list(t["hills"]) + [0.0] * 4 + [0.0] * 4
                  + [150.0, 600.0, world.RADIUS, 0.0] + list(t["detail"]) + [0.0] * 8
                  + [0.0, 0.0, 1.0, 0.0] * 8 + [0.0] * 32
                  + [c for v in engine.custom["planet.terrain.lakes"] for c in v]
                  + [c for v in engine.custom["planet.terrain.lakeShapes"] for c in v])
        uniform = self.ctx.upload(struct.pack("<%df" % len(params), *params))
        items = np.array([tuple(d) + (reach,) for d in directions], dtype=np.float32)
        out = np.zeros((2 * len(directions), 4), dtype=np.float32)
        self.ctx.run(self.kernel, [uniform, items, out], threads=len(directions),
                     push=struct.pack("<I", len(directions)))
        return np.concatenate([out[0::2], out[1::2, :1]], axis=1)

    def test_the_bowl_within_the_shore(self):
        inner = RADIUS * (1.0 - LAKE_WOBBLE_REACH * WOBBLE)
        directions = [CENTRE] + [d for r in (50.0, 120.0, 0.95 * inner) for d in ring(r)]
        out = self.run_kernel([self.lake], directions)
        np.testing.assert_array_equal(out[:, 2], 0.0)
        np.testing.assert_allclose(out[:, 1] + out[:, 3], self.lake.level, atol=2e-3)
        self.assertAlmostEqual(float(out[0, 3]), DEPTH, places=3)
        self.assertTrue(np.all((out[1:, 3] > 0.0) & (out[1:, 3] < DEPTH)))
        rings = out[1:].reshape(3, 16, 5)
        self.assertTrue(np.all(rings[0, :, 3] > rings[1, :, 3]))
        self.assertTrue(np.all(rings[1, :, 3] > rings[2, :, 3]))

    def test_the_planets_own_beyond_the_bank(self):
        directions = [d for r in (REACH + BANK + 5.0, 3.0 * REACH) for d in ring(r)]
        out = self.run_kernel([self.lake], directions)
        np.testing.assert_array_equal(out[:, 1], out[:, 0])
        np.testing.assert_array_equal(out[:, 2], -1.0)
        np.testing.assert_array_equal(out[:, 3], 0.0)

    def test_the_bank_between_the_water_and_the_planets_own(self):
        out = self.run_kernel([self.lake], ring(REACH + 0.5 * BANK, count=32))
        np.testing.assert_array_equal(out[:, 2], -1.0)
        low = np.minimum(out[:, 0], self.lake.level) - 1e-3
        high = np.maximum(out[:, 0], self.lake.level) + 1e-3
        self.assertTrue(np.all((out[:, 1] >= low) & (out[:, 1] <= high)))

    def test_continuous_across_the_shore_and_the_bank(self):
        step = 0.25
        distances = np.arange(0.0, REACH + BANK + 10.0, step)
        for bearing in np.linspace(0.0, 2.0 * math.pi, 6, endpoint=False):
            out = self.run_kernel([self.lake], [towards(CENTRE, bearing, r) for r in distances])
            jumps = np.abs(np.diff(out[:, 1]))
            natural_jumps = np.abs(np.diff(out[:, 0]))
            self.assertLess(float(np.max(jumps)), max(0.5, 2.0 * float(np.max(natural_jumps))), bearing)
            inside = out[:, 2] == 0.0
            self.assertTrue(inside[0])
            # One shore along the bearing: inside, then out for good.
            self.assertEqual(int(np.sum(np.diff(inside.astype(int)) != 0)), 1, bearing)

    def test_the_shore_wobbles_within_its_reach(self):
        inner = RADIUS * (1.0 - LAKE_WOBBLE_REACH * WOBBLE)
        out = self.run_kernel([self.lake], ring(inner - 1.0, 64) + ring(REACH + 1.0, 64) + ring(RADIUS, 64))
        np.testing.assert_array_equal(out[:64, 2], 0.0)
        np.testing.assert_array_equal(out[64:128, 2], -1.0)
        at_radius = out[128:, 2]
        self.assertTrue(np.any(at_radius == 0.0) and np.any(at_radius == -1.0))

    def test_lake_near(self):
        gap = 50.0
        outside = ring(REACH + gap, 8)
        out = self.run_kernel([self.lake], outside, reach=gap + 5.0)
        np.testing.assert_array_equal(out[:, 4], 0.0)
        out = self.run_kernel([self.lake], outside, reach=gap - 5.0)
        np.testing.assert_array_equal(out[:, 4], -1.0)
        out = self.run_kernel([self.lake, self.second], [OTHER, CENTRE], reach=1e6)
        np.testing.assert_array_equal(out[:, 4], [1.0, 0.0])

    def test_no_lakes(self):
        out = self.run_kernel([], ring(0.0, 1) + ring(100.0), reach=1e6)
        np.testing.assert_array_equal(out[:, 1], out[:, 0])
        np.testing.assert_array_equal(out[:, 2], -1.0)
        np.testing.assert_array_equal(out[:, 4], -1.0)


if __name__ == "__main__":
    unittest.main()

"""Tests for the ore richness of the planet pipeline (resources.glsl), on
the GPU over 50,000 random directions with the default terrain: the
planet's richness lies in 0 .. 1, is 0 below sea level, is above 0.3 on
2 % to 15 % of the land and above 0.6 on a smaller share, more often in
the mountains' bands than outside them, and moves with the seed; the
moon's lies in 0 .. 1 and is above 0.3 on 2 % to 25 % of it. The tests
need a Vulkan 1.3 device and glslc; without them they are skipped.

    python -m unittest discover -s tests/python
"""

import shutil
import struct
import tempfile
import unittest
from pathlib import Path

import numpy as np

import shoonyakasha as sk
from shoonyakasha import shaders
from shoonyakasha.planet import world

KERNEL = """#version 450
layout(local_size_x = 64) in;
layout(std140, set = 0, binding = 0) uniform TerrainParams {
    vec4 continent; vec4 mountains; vec4 hills; vec4 seed; vec4 site; vec4 siteShape; vec4 detail;
    vec4 climate; vec4 biomeView;
} terrain;
layout(std430, set = 0, binding = 1) readonly buffer Inputs { vec4 items[]; } src;
layout(std430, set = 0, binding = 2) writeonly buffer Outputs { vec4 values[]; } dst;
layout(push_constant) uniform Push { uint count; uint mode; } pc;

#include "terrain.glsl"
#include "moon.glsl"
#include "resources.glsl"

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    vec3 d = normalize(src.items[i].xyz);
    if (pc.mode == 0u) {                 // the planet: height, richness, mountain mask, richness at height -1
        float h = terrainHeightNatural(d);
        dst.values[i] = vec4(h, planetOre(d, h), terrainMountainMask(d), planetOre(d, -1.0));
    } else {                             // the moon: richness
        dst.values[i] = vec4(moonOre(d), 0.0, 0.0, 0.0);
    }
}
"""
COUNT = 50_000


class ResourcesShader(unittest.TestCase):
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
        path = cls.tmp / "resources_probe.comp"
        path.write_text(KERNEL, encoding="utf-8")
        cls.kernel = cls.ctx.kernel(path, args=["-I", str(world.PIPELINE.parent / "shaders")])
        rng = np.random.default_rng(5)
        directions = rng.normal(size=(COUNT, 3))
        directions /= np.linalg.norm(directions, axis=1, keepdims=True)
        cls.items = np.zeros((COUNT, 4), dtype=np.float32)
        cls.items[:, :3] = directions

    @classmethod
    def tearDownClass(cls):
        cls.ctx.shutdown()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_kernel(self, mode, seed=(0.0, 0.0, 0.0)):
        t = world.TERRAIN
        params = (list(t["continent"]) + list(t["mountains"]) + list(t["hills"]) + list(seed) + [0.0] + [0.0] * 4
                  + [150.0, 600.0, world.RADIUS, 0.0] + list(t["detail"]) + [0.0] * 8)
        uniform = self.ctx.upload(struct.pack("<36f", *params))
        out = np.zeros((COUNT, 4), dtype=np.float32)
        self.ctx.run(self.kernel, [uniform, self.items, out], threads=COUNT, push=struct.pack("<II", COUNT, mode))
        return out

    def test_planet(self):
        out = self.run_kernel(0)
        height, ore, mask, under_sea = out[:, 0], out[:, 1], out[:, 2], out[:, 3]
        self.assertTrue(np.all((ore >= 0.0) & (ore <= 1.0)))
        self.assertTrue(np.all(ore[height <= 0.0] == 0.0))
        self.assertTrue(np.all(under_sea == 0.0))
        land = ore[height > 0.0]
        rich, richer = (land > 0.3).mean(), (land > 0.6).mean()
        self.assertTrue(0.02 <= rich <= 0.15, rich)
        self.assertTrue(0.0 < richer < rich, richer)
        mountains = (height > 0.0) & (mask > 0.5)
        plains = (height > 0.0) & (mask < 0.1)
        self.assertGreater((ore[mountains] > 0.3).mean(), (ore[plains] > 0.3).mean())
        # Another seed, another field.
        moved = self.run_kernel(0, seed=(17.0, -3.0, 41.0))
        self.assertGreater(np.abs(moved[:, 1] - ore).max(), 0.5)

    def test_moon(self):
        ore = self.run_kernel(1)[:, 0]
        self.assertTrue(np.all((ore >= 0.0) & (ore <= 1.0)))
        rich = (ore > 0.3).mean()
        self.assertTrue(0.02 <= rich <= 0.25, rich)


if __name__ == "__main__":
    unittest.main()

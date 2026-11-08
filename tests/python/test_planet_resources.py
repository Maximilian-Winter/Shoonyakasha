"""Tests for the ore richness and resources of the planet pipeline
(resources.glsl), on the GPU over 50,000 random directions with the
default terrain: the planet's richness lies in 0 .. 1, is 0 below sea
level, is above 0.3 on 2 % to 15 % of the land and above 0.6 on a smaller
share, more often in the mountains' bands than outside them, and moves
with the seed; the moon's lies in 0 .. 1 and is above 0.3 on 2 % to 25 %
of it. The planet's resources are those shoonyakasha.planet.resources
picks from the shader's inputs (but at ties within rounding), each of
ferrite to evaporite on more than 5 % of the ore and ferrite the most
common, quartz colder than amber, amber wetter and evaporite drier than
the ore's mean; the moon's are ice or
ilmenite by their crater's latitude, each on more than 5 % of the ore;
the ids and constants are the shader's. The GPU tests need a Vulkan 1.3
device and glslc; without them they are skipped.

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
from shoonyakasha.planet import resources, rocks, world

KERNEL = """#version 450
layout(local_size_x = 64) in;
layout(std140, set = 0, binding = 0) uniform TerrainParams {
    vec4 continent; vec4 mountains; vec4 hills; vec4 seed; vec4 site; vec4 siteShape; vec4 detail;
    vec4 climate; vec4 biomeView; vec4 gullies;
} terrain;
layout(std430, set = 0, binding = 1) readonly buffer Inputs { vec4 items[]; } src;
layout(std430, set = 0, binding = 2) writeonly buffer Outputs { vec4 values[]; } dst;
layout(push_constant) uniform Push { uint count; uint mode; } pc;

#include "terrain.glsl"
#include "climate.glsl"
#include "moon.glsl"
#include "resources.glsl"

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    vec3 d = normalize(src.items[i].xyz);
    if (pc.mode == 0u) {                 // the planet: height, richness, mountain mask, richness at height -1
        float h = terrainHeightNatural(d);
        dst.values[i] = vec4(h, planetOre(d, h), terrainMountainMask(d), planetOre(d, -1.0));
    } else if (pc.mode == 1u) {          // the moon: richness
        dst.values[i] = vec4(moonOre(d), 0.0, 0.0, 0.0);
    } else if (pc.mode == 2u) {          // the planet's resource and its inputs, three vec4 per direction
        float h = terrainHeightNatural(d);
        vec2 climate = climateAt(d, h).xy;
        uint id = planetResource(d, climate.x, climate.y);
        dst.values[3u * i] = vec4(h, climate, terrainMountainMask(d));
        dst.values[3u * i + 1u] = vec4(planetResourceNoise(d, 0), planetResourceNoise(d, 1),
                                       planetResourceNoise(d, 2), planetResourceNoise(d, 3));
        dst.values[3u * i + 2u] = vec4(planetResourceNoise(d, 4), float(id), planetOre(d, h), 0.0);
    } else {                             // the moon: richness, resource, the richest crater's centre's y
        vec3 crater;
        float ore = moonOreCrater(d, crater);
        dst.values[i] = vec4(ore, float(moonResource(d)), crater.y, 0.0);
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

    def run_kernel(self, mode, seed=(0.0, 0.0, 0.0), rows=1):
        t = world.TERRAIN
        params = (list(t["continent"]) + list(t["mountains"]) + list(t["hills"]) + list(seed) + [0.0] + [0.0] * 4
                  + [150.0, 600.0, world.RADIUS, 0.0] + list(t["detail"]) + [0.0] * 12)
        uniform = self.ctx.upload(struct.pack("<40f", *params))
        out = np.zeros((COUNT * rows, 4), dtype=np.float32)
        self.ctx.run(self.kernel, [uniform, self.items, out], threads=COUNT, push=struct.pack("<II", COUNT, mode))
        return out.reshape(COUNT, rows * 4)

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

    def test_planet_resources_are_the_mirrors(self):
        out = self.run_kernel(2, rows=3)
        height, temperature, moisture, mask = out[:, 0], out[:, 1], out[:, 2], out[:, 3]
        noise = out[:, 4:9]
        picked, ore = out[:, 9].astype(np.int64), out[:, 10]
        weights = resources.planet_weights(temperature, moisture, mask)
        mirrored = resources.planet_pick(weights, noise)
        ores = (height > 0.0) & (ore > rocks.ORE_ROCKS)
        # The same choice but where two scores tie within float rounding.
        self.assertGreater((mirrored[ores] == picked[ores]).mean(), 0.999)
        self.assertTrue(np.all((picked >= resources.FERRITE) & (picked <= resources.EVAPORITE)))
        found = {int(r): float((picked[ores] == r).mean()) for r in range(resources.FERRITE, resources.EVAPORITE + 1)}
        for r, share in found.items():
            self.assertGreater(share, 0.05, (resources.name(r), found))
        self.assertEqual(max(found, key=found.get), resources.FERRITE, found)
        # Quartz in the cold, amber in the warm and wet, evaporite in the dry.
        chosen = {r: ores & (picked == r) for r in found}
        self.assertLess(temperature[chosen[resources.QUARTZ]].mean(), temperature[chosen[resources.AMBER]].mean())
        self.assertGreater(moisture[chosen[resources.AMBER]].mean(), moisture[ores].mean())
        self.assertLess(moisture[chosen[resources.EVAPORITE]].mean(), moisture[ores].mean())

    def test_moon_resources_by_their_craters_latitude(self):
        out = self.run_kernel(3)
        ore, picked, crater_y = out[:, 0], out[:, 1].astype(np.int64), out[:, 2]
        ores = ore > 0.0
        np.testing.assert_array_equal(picked[ores], resources.moon_resource(crater_y[ores]))
        for r in (resources.ICE, resources.ILMENITE):
            self.assertGreater((picked[ore > rocks.ORE_ROCKS] == r).mean(), 0.05, resources.name(r))

    def test_constants_are_the_shaders(self):
        shader = (world.PIPELINE.parent / "shaders" / "resources.glsl").read_text(encoding="utf-8")
        for name in ("NONE", "FERRITE", "CUPRITE", "QUARTZ", "AMBER", "EVAPORITE", "ICE", "ILMENITE", "IRIDIUM"):
            self.assertIn("const uint RESOURCE_%s = %du;" % (name, getattr(resources, name)), shader)
        self.assertIn("const int PLANET_RESOURCES = %d;" % resources.PLANET_RESOURCES, shader)
        self.assertIn("const float RESOURCE_NOISE = %s;" % resources.RESOURCE_NOISE, shader)
        self.assertIn("const float MOON_ICE_LATITUDE = %s;" % resources.MOON_ICE_LATITUDE, shader)


if __name__ == "__main__":
    unittest.main()

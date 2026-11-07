"""Tests for the flat sites of the planet pipeline (sites.glsl), on the
GPU: with a site on the planet and one on the moon, the ground within a
site's flat radius lies at its height and counts as on the site; beyond
its blend radius it is the body's own; between them it lies between the
two; and a site leaves the other body's terrain alone. The tests need a
Vulkan 1.3 device and glslc; without them they are skipped.

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

KERNEL = """#version 450
layout(local_size_x = 64) in;
layout(std140, set = 0, binding = 0) uniform TerrainParams {
    vec4 continent; vec4 mountains; vec4 hills; vec4 seed; vec4 site; vec4 siteShape; vec4 detail;
    vec4 climate; vec4 biomeView; vec4 gullies; vec4 sites[8]; vec4 siteShapes[8];
} terrain;
layout(std430, set = 0, binding = 1) readonly buffer Inputs { vec4 items[]; } src;
layout(std430, set = 0, binding = 2) writeonly buffer Outputs { vec4 values[]; } dst;
layout(push_constant) uniform Push { uint count; float moonRadius; } pc;

#include "terrain.glsl"
#include "moon.glsl"
#include "sites.glsl"

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    vec3 d = normalize(src.items[i].xyz);
    int body = int(src.items[i].w);
    float natural = body == 1 ? moonHeight(d, pc.moonRadius, 0.0) : terrainHeight(d);
    dst.values[i] = vec4(natural, terrainWithSites(natural, d, body), terrainOnSite(d, body) ? 1.0 : 0.0, 0.0);
}
"""
PLANET_SITE = (0.6, 0.0, 0.8)
MOON_SITE = (0.0, 1.0, 0.0)
PLANET_HEIGHT, MOON_HEIGHT = 123.0, -40.0
PLANET_SHAPE = (150.0, 600.0)
MOON_SHAPE = (100.0, 400.0)


def around(direction, radius, distance, count=8):
    """`count` unit directions `distance` metres along a sphere of `radius` around `direction`."""
    d = np.array(direction, dtype=np.float64)
    east = np.cross((0.0, 1.0, 0.0), d)
    if np.linalg.norm(east) < 1e-9:
        east = np.array((1.0, 0.0, 0.0))
    east /= np.linalg.norm(east)
    north = np.cross(d, east)
    angle = distance / radius
    return [tuple(d * math.cos(angle) + (east * math.cos(t) + north * math.sin(t)) * math.sin(angle))
            for t in np.linspace(0.0, 2.0 * math.pi, count, endpoint=False)]


class SitesShader(unittest.TestCase):
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
        path = cls.tmp / "sites_probe.comp"
        path.write_text(KERNEL, encoding="utf-8")
        cls.kernel = cls.ctx.kernel(path, args=["-I", str(world.PIPELINE.parent / "shaders")])

    @classmethod
    def tearDownClass(cls):
        cls.ctx.shutdown()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_kernel(self, points):
        """(natural, with the sites, on a site) at `points`, (direction, body index) pairs."""
        t = world.TERRAIN
        sites = [PLANET_SITE + (PLANET_HEIGHT,), MOON_SITE + (MOON_HEIGHT,)] + [(0.0, 0.0, 1.0, 0.0)] * 6
        shapes = ([PLANET_SHAPE + (0.0, world.RADIUS), MOON_SHAPE + (1.0, world.MOON_RADIUS)]
                  + [(0.0, 0.0, 0.0, 0.0)] * 6)
        params = (list(t["continent"]) + list(t["mountains"]) + list(t["hills"]) + [0.0] * 4 + [0.0] * 4
                  + [150.0, 600.0, world.RADIUS, 0.0] + list(t["detail"]) + [0.0] * 12
                  + [c for s in sites for c in s] + [c for s in shapes for c in s])
        uniform = self.ctx.upload(struct.pack("<%df" % len(params), *params))
        items = np.array([tuple(d) + (float(body),) for d, body in points], dtype=np.float32)
        out = np.zeros((len(points), 4), dtype=np.float32)
        self.ctx.run(self.kernel, [uniform, items, out], threads=len(points),
                     push=struct.pack("<If", len(points), world.MOON_RADIUS))
        return out

    def check_site(self, direction, body, radius, height, shape):
        flat = [(d, body) for r in (0.0, 50.0, 0.9 * shape[0]) for d in around(direction, radius, r)]
        blend = [(d, body) for d in around(direction, radius, 0.5 * (shape[0] + shape[1]))]
        beyond = [(d, body) for d in around(direction, radius, 1.2 * shape[1])]
        out = self.run_kernel(flat + blend + beyond)
        n, m = len(flat), len(blend)
        np.testing.assert_allclose(out[:n, 1], height, atol=1e-3)
        self.assertTrue(np.all(out[:n, 2] == 1.0))
        between = out[n:n + m]
        low, high = np.minimum(between[:, 0], height), np.maximum(between[:, 0], height)
        self.assertTrue(np.all((between[:, 1] >= low - 1e-3) & (between[:, 1] <= high + 1e-3)))
        self.assertTrue(np.all(between[:, 2] == 0.0))
        np.testing.assert_array_equal(out[n + m:, 1], out[n + m:, 0])
        self.assertTrue(np.all(out[n + m:, 2] == 0.0))

    def test_planet_site(self):
        self.check_site(PLANET_SITE, 0, world.RADIUS, PLANET_HEIGHT, PLANET_SHAPE)

    def test_moon_site(self):
        self.check_site(MOON_SITE, 1, world.MOON_RADIUS, MOON_HEIGHT, MOON_SHAPE)

    def test_a_site_leaves_the_other_body_alone(self):
        points = [(d, 0) for d in around(MOON_SITE, world.RADIUS, 20.0)]
        points += [(d, 1) for d in around(PLANET_SITE, world.MOON_RADIUS, 20.0)]
        out = self.run_kernel(points)
        np.testing.assert_array_equal(out[:, 1], out[:, 0])
        self.assertTrue(np.all(out[:, 2] == 0.0))


if __name__ == "__main__":
    unittest.main()

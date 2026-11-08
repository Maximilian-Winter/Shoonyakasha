"""Tests for the craters of the planet pipeline (craters.glsl), on the
GPU: with a crater on the planet and one on the moon, the ground at a
crater's centre lies its depth below the body's own; within its radius it
is lower and counts as in the crater; its rim raises the ground just
beyond the radius; beyond CRATER_REACH radii the ground is the body's own;
the shape is continuous along a line through the crater; a crater leaves
the other body's terrain alone; and the craters smaller than `smallest`
are left out. The tests need a Vulkan 1.3 device and glslc; without them
they are skipped.

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

from test_planet_sites import around

KERNEL = """#version 450
layout(local_size_x = 64) in;
layout(std140, set = 0, binding = 0) uniform TerrainParams {
    vec4 continent; vec4 mountains; vec4 hills; vec4 seed; vec4 site; vec4 siteShape; vec4 detail;
    vec4 climate; vec4 biomeView; vec4 gullies; vec4 sites[8]; vec4 siteShapes[8]; vec4 lakes[32]; vec4 lakeShapes[32];
    vec4 riverFrame[4]; vec4 riverOrigin; vec4 craters[16]; vec4 craterShapes[16];
} terrain;
layout(std430, set = 0, binding = 1) readonly buffer Inputs { vec4 items[]; } src;
layout(std430, set = 0, binding = 2) writeonly buffer Outputs { vec4 values[]; } dst;
layout(push_constant) uniform Push { uint count; float moonRadius; float smallest; } pc;

#include "terrain.glsl"
#include "moon.glsl"
#include "craters.glsl"

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    vec3 d = normalize(src.items[i].xyz);
    int body = int(src.items[i].w);
    float natural = body == 1 ? moonHeight(d, pc.moonRadius, 0.0) : terrainHeight(d);
    dst.values[i] = vec4(natural, terrainWithCraters(natural, d, body, pc.smallest),
                         terrainInCrater(d, body) ? 1.0 : 0.0, 0.0);
}
"""
PLANET_CRATER = world.Crater(0, (0.6, 0.0, 0.8), 60.0, 9.0, 3.0)
MOON_CRATER = world.Crater(1, (0.0, 1.0, 0.0), 40.0, 6.0, 2.0)


class CratersShader(unittest.TestCase):
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
        path = cls.tmp / "craters_probe.comp"
        path.write_text(KERNEL, encoding="utf-8")
        cls.kernel = cls.ctx.kernel(path, args=["-I", str(world.PIPELINE.parent / "shaders")])

    @classmethod
    def tearDownClass(cls):
        cls.ctx.shutdown()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_kernel(self, points, smallest=0.0):
        """(natural, with the craters, in a crater) at `points`, (direction, body index) pairs."""
        t = world.TERRAIN
        radii = {0: world.RADIUS, 1: world.MOON_RADIUS}
        craters = [PLANET_CRATER, MOON_CRATER]
        centres = [tuple(c.direction) + (c.radius,) for c in craters] + [(0.0, 0.0, 1.0, 0.0)] * 14
        shapes = [(c.depth, c.rim, float(c.body), radii[c.body]) for c in craters] + [(0.0,) * 4] * 14
        params = (list(t["continent"]) + list(t["mountains"]) + list(t["hills"]) + [0.0] * 4 + [0.0] * 4
                  + [150.0, 600.0, world.RADIUS, 0.0] + list(t["detail"]) + [0.0] * 12
                  + [0.0] * 4 * (8 + 8 + 32 + 32 + 4 + 1)
                  + [c for v in centres for c in v] + [c for v in shapes for c in v])
        uniform = self.ctx.upload(struct.pack("<%df" % len(params), *params))
        items = np.array([tuple(d) + (float(body),) for d, body in points], dtype=np.float32)
        out = np.zeros((len(points), 4), dtype=np.float32)
        self.ctx.run(self.kernel, [uniform, items, out], threads=len(points),
                     push=struct.pack("<Iff", len(points), world.MOON_RADIUS, smallest))
        return out

    def check_crater(self, crater, radius):
        def ring(r):
            return [(d, crater.body) for d in around(crater.direction, radius, r * crater.radius)]
        centre, inside, rim = ring(0.0)[:1], ring(0.6), ring(1.15)
        beyond = ring(2.05) + ring(3.0)
        out = self.run_kernel(centre + inside + rim + beyond)
        a, b, c = 1, 1 + len(inside), 1 + len(inside) + len(rim)
        np.testing.assert_allclose(out[0, 1], out[0, 0] - crater.depth, atol=0.01)
        dug = out[a:b, 0] - out[a:b, 1]
        self.assertTrue(np.all(dug > 0.5 * crater.depth * (1.0 - 0.36)))
        self.assertTrue(np.all(out[:b, 2] == 1.0))
        raised = out[b:c, 1] - out[b:c, 0]
        self.assertTrue(np.all(raised > 0.3 * crater.rim))
        self.assertTrue(np.all(raised <= crater.rim + 1e-3))
        self.assertTrue(np.all(out[b:, 2] == 0.0))
        np.testing.assert_array_equal(out[c:, 1], out[c:, 0])

    def test_planet_crater(self):
        self.check_crater(PLANET_CRATER, world.RADIUS)

    def test_moon_crater(self):
        self.check_crater(MOON_CRATER, world.MOON_RADIUS)

    def test_the_shape_is_continuous(self):
        crater = PLANET_CRATER
        d = np.array(crater.direction)
        east = np.cross((0.0, 1.0, 0.0), d)
        east /= np.linalg.norm(east)
        steps = np.linspace(-2.5, 2.5, 2001) * crater.radius / world.RADIUS
        points = [(tuple(d * np.cos(s) + east * np.sin(s)), 0) for s in steps]
        out = self.run_kernel(points)
        shape = (out[:, 1] - out[:, 0]).astype(np.float64)
        spacing = 5.0 * crater.radius / 2000
        # The steepest the profile gets: the bowl's wall, 2 * depth / radius per metre, with room for float error.
        self.assertLess(np.max(np.abs(np.diff(shape))), 3.0 * crater.depth / crater.radius * spacing + 0.01)

    def test_a_crater_leaves_the_other_body_alone(self):
        points = [(d, 0) for d in around(MOON_CRATER.direction, world.RADIUS, 10.0)]
        points += [(d, 1) for d in around(PLANET_CRATER.direction, world.MOON_RADIUS, 10.0)]
        out = self.run_kernel(points)
        np.testing.assert_array_equal(out[:, 1], out[:, 0])
        self.assertTrue(np.all(out[:, 2] == 0.0))

    def test_craters_smaller_than_smallest_left_out(self):
        points = [(PLANET_CRATER.direction, 0), (MOON_CRATER.direction, 1)]
        out = self.run_kernel(points, smallest=50.0)
        np.testing.assert_allclose(out[0, 1], out[0, 0] - PLANET_CRATER.depth, atol=0.01)
        np.testing.assert_array_equal(out[1, 1], out[1, 0])


if __name__ == "__main__":
    unittest.main()

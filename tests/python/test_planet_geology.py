"""Tests for the planet's geology (geology.glsl, and biome.glsl's ground
with it), on the GPU: the strata's shade within its range and changing
with height; a slope facing the equator warmer for its snow and one
facing the pole colder, flat ground and the equator neither; scree only
under cliffs on moderate slopes; the cover and the scree through a
climate's z; the shore wet just above the water and dry above it; and
the ground's layers with scree summing to 1 with the scree in the dry
ground, and more snow on a slope facing the pole than on one facing the
equator. The tests need a Vulkan 1.3 device and glslc; without them they
are skipped.

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
    vec4 climate; vec4 biomeView; vec4 gullies;
} terrain;
layout(std430, set = 0, binding = 1) readonly buffer Inputs { vec4 items[]; } src;   // three per item
layout(std430, set = 0, binding = 2) writeonly buffer Outputs { float values[]; } dst;  // OUT per item
layout(push_constant) uniform Push { uint count; uint mode; } pc;

#include "terrain.glsl"
#include "biome.glsl"

const uint OUT = 8u;

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    vec4 a = src.items[i * 3u], b = src.items[i * 3u + 1u], c = src.items[i * 3u + 2u];
    uint o = i * OUT;
    if (pc.mode == 0u) {                 // strataShade(dir a.xyz, height a.w)
        dst.values[o] = strataShade(a.xyz, a.w);
    } else if (pc.mode == 1u) {          // aspectWarmth(radial a.xyz, normal b.xyz)
        dst.values[o] = aspectWarmth(a.xyz, b.xyz);
    } else if (pc.mode == 2u) {          // screeShare(slope a.x, rise a.y)
        dst.values[o] = screeShare(a.x, a.y);
    } else if (pc.mode == 3u) {          // coverWithScree(cover a.x, scree a.y), then both back
        vec3 climate = vec3(0.0, 0.0, coverWithScree(a.x, a.y));
        dst.values[o] = coverOfClimate(climate);
        dst.values[o + 1u] = screeOfClimate(climate);
    } else if (pc.mode == 4u) {          // wetShore(dir a.xyz, above a.w)
        dst.values[o] = wetShore(a.xyz, a.w);
    } else {                             // groundLayers(radial a.xyz, height a.w, normal b.xyz, climate c.xyz)
        float w[GROUND_LAYERS];
        vec3 colours[GROUND_LAYERS];
        groundLayers(a.xyz, a.w, b.xyz, c.xyz, w, colours);
        for (int k = 0; k < GROUND_LAYERS; ++k) dst.values[o + uint(k)] = w[k];
        dst.values[o + 7u] = distance(colours[GROUND_DRY], SCREE);
    }
}
"""
OUT = 8
DRY, SNOW = 1, 5


def tilted(radial, towards, amount):
    """The unit normal of ground at `radial` leaning `amount` (its sine) towards the tangent `towards`."""
    radial = np.asarray(radial, np.float64)
    t = np.asarray(towards, np.float64) - radial * np.dot(towards, radial)
    t /= np.linalg.norm(t)
    return radial * np.sqrt(1.0 - amount * amount) + t * amount


class GeologyShader(unittest.TestCase):
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
        path = cls.tmp / "geology_probe.comp"
        path.write_text(KERNEL, encoding="utf-8")
        cls.kernel = cls.ctx.kernel(path, args=["-I", str(world.PIPELINE.parent / "shaders")])

    @classmethod
    def tearDownClass(cls):
        cls.ctx.shutdown()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_items(self, mode, items):
        """The kernel's outputs (OUT per item) for items of three vec4 each."""
        items = np.ascontiguousarray(items, dtype=np.float32).reshape(-1, 12)
        params = [0.0] * 4 * 10
        params[4 * 5 + 2] = world.RADIUS                         # siteShape.z
        uniform = self.ctx.upload(struct.pack("<40f", *params))
        out = np.zeros(len(items) * OUT, dtype=np.float32)
        self.ctx.run(self.kernel, [uniform, items, out], threads=len(items),
                     push=struct.pack("<II", len(items), mode))
        return out.reshape(-1, OUT).astype(np.float64)

    def items(self, rows):
        """Items from rows of up to three vec4s, each given as up to four numbers."""
        out = np.zeros((len(rows), 12))
        for k, row in enumerate(rows):
            for j, part in enumerate(row):
                part = np.asarray(part, np.float64)
                out[k, 4 * j:4 * j + len(part)] = part
        return out

    def test_strata(self):
        d = np.array([0.6, 0.0, 0.8])
        heights = np.linspace(0.0, 500.0, 2001)
        shade = self.run_items(0, self.items([(np.append(d, h),) for h in heights]))[:, 0]
        self.assertGreaterEqual(shade.min(), 0.7)
        self.assertLessEqual(shade.max(), 1.1)
        self.assertGreater(shade.std(), 0.03)

    def test_aspect(self):
        north = np.array([0.0, 0.5, 0.866])
        north /= np.linalg.norm(north)
        south = north * np.array([1.0, -1.0, 1.0])
        equator = np.array([0.6, 0.0, 0.8])
        up = np.array([0.0, 1.0, 0.0])
        rows = [(north, tilted(north, -up, 0.5)), (north, tilted(north, up, 0.5)), (north, north),
                (south, tilted(south, up, 0.5)), (south, tilted(south, -up, 0.5)), (equator, tilted(equator, up, 0.5))]
        warmth = self.run_items(1, self.items(rows))[:, 0]
        self.assertAlmostEqual(warmth[0], 2.5, places=3)          # northern slope facing the equator
        self.assertAlmostEqual(warmth[1], -2.5, places=3)         # facing the pole
        self.assertAlmostEqual(warmth[2], 0.0, places=5)          # flat
        self.assertAlmostEqual(warmth[3], 2.5, places=3)          # southern, facing the equator
        self.assertAlmostEqual(warmth[4], -2.5, places=3)
        self.assertAlmostEqual(warmth[5], 0.0, places=5)          # on the equator

    def test_scree(self):
        rows = [([0.06, 1.2],), ([0.06, 0.3],), ([0.0, 1.2],), ([0.3, 1.2],), ([0.06, 0.8],)]
        share = self.run_items(2, self.items(rows))[:, 0]
        self.assertGreater(share[0], 0.95)
        np.testing.assert_allclose(share[1:4], 0.0, atol=1e-6)
        self.assertTrue(0.0 < share[4] < share[0])

    def test_cover_and_scree_through_the_climate(self):
        rows = [([0.7, 0.0],), ([0.7, 0.01],), ([0.7, 0.5],), ([0.0, 1.0],)]
        out = self.run_items(3, self.items(rows))[:, :2]
        np.testing.assert_allclose(out, [[0.7, 0.0], [0.7, 0.0], [0.0, 0.5], [0.0, 1.0]], atol=1e-6)

    def test_wet_shore(self):
        d = np.array([0.6, 0.0, 0.8])
        wet = self.run_items(4, self.items([(np.append(d, a),) for a in (-0.5, 0.0, 0.3, 3.0, 10.0)]))[:, 0]
        np.testing.assert_allclose(wet, [0.0, 1.0, 1.0, 0.0, 0.0], atol=1e-6)

    def test_ground_with_scree_and_snow_by_aspect(self):
        radial = np.array([0.0, 0.5, 0.866])
        radial /= np.linalg.norm(radial)
        up = np.array([0.0, 1.0, 0.0])
        slope = tilted(radial, -up, 0.3)
        rows = [(np.append(radial, 500.0), slope, [12.0, 0.4, 0.6]),          # grassland, no scree
                (np.append(radial, 500.0), slope, [12.0, 0.4, -0.8])]         # the same with scree
        weights = self.run_items(5, self.items(rows))
        np.testing.assert_allclose(weights[:, :7].sum(axis=1), 1.0, atol=1e-5)
        self.assertGreater(weights[1, DRY], weights[0, DRY] + 0.5)
        self.assertLess(weights[1, 7], 0.2)                        # the dry ground near SCREE's colour
        # At the snow's temperature the slope facing the pole has more snow than the one facing the equator.
        sunny, shaded = tilted(radial, -up, 0.4), tilted(radial, up, 0.4)
        rows = [(np.append(radial, 500.0), sunny, [-6.0, 0.4, 0.5]),
                (np.append(radial, 500.0), shaded, [-6.0, 0.4, 0.5])]
        weights = self.run_items(5, self.items(rows))
        self.assertGreater(weights[1, SNOW], weights[0, SNOW] + 0.5)


if __name__ == "__main__":
    unittest.main()

"""Tests for the planet's biomes: shoonyakasha.planet.biomes names the
biomes of a temperature and moisture as biome.glsl classifies them (Whittaker's
corners, weights summing to 1, smooth across the band edges), the cards' rows,
and the palettes' cycle; and, on the GPU through sk.ComputeContext,
biome.glsl itself: its biome weights equal biomes.weights over a grid of
temperatures and moistures, the climate settings' offsets are added (the
moisture clamped), the ground's layer weights sum to 1 on land, beaches,
cliffs, snow and the sea floor, and a hue turn keeps a colour's luminance
and leaves it alone at 0. The GPU tests need a Vulkan 1.3 device and glslc;
they are skipped without them.

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
from shoonyakasha.planet import biomes, settings, world

KERNEL = """#version 450
layout(local_size_x = 64) in;
layout(std140, set = 0, binding = 0) uniform TerrainParams {
    vec4 continent; vec4 mountains; vec4 hills; vec4 seed; vec4 site; vec4 siteShape; vec4 detail;
    vec4 climate; vec4 biomeView;
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
    if (pc.mode == 0u) {                 // biomeWeights(temperature a.x, moisture a.y)
        float w[BIOMES];
        biomeWeights(a.x, a.y, w);
        for (int k = 0; k < BIOMES; ++k) dst.values[o + uint(k)] = w[k];
    } else if (pc.mode == 1u) {          // groundLayers(radial a.xyz, height a.w, normal b.xyz, climate c.xyz)
        float w[GROUND_LAYERS];
        vec3 colours[GROUND_LAYERS];
        groundLayers(a.xyz, a.w, b.xyz, c.xyz, w, colours);
        for (int k = 0; k < GROUND_LAYERS; ++k) dst.values[o + uint(k)] = w[k];
    } else if (pc.mode == 2u) {          // climateWithSettings(a.xyz)
        vec3 s = climateWithSettings(a.xyz);
        dst.values[o] = s.x;
        dst.values[o + 1u] = s.y;
        dst.values[o + 2u] = s.z;
    } else {                             // hueTurn(a.xyz, a.w)
        vec3 t = hueTurn(a.xyz, a.w);
        dst.values[o] = t.x;
        dst.values[o + 1u] = t.y;
        dst.values[o + 2u] = t.z;
    }
}
"""
OUT = 8


class BiomeTable(unittest.TestCase):
    def test_corners_of_the_table(self):
        for temperature, moisture, name in ((26.0, 0.05, "desert"), (26.0, 0.45, "savanna"),
                                            (26.0, 0.8, "tropical forest"), (12.0, 0.25, "steppe"),
                                            (12.0, 0.45, "grassland"), (12.0, 0.75, "deciduous forest"),
                                            (3.0, 0.7, "taiga"), (3.0, 0.1, "steppe"), (-8.0, 0.5, "tundra")):
            self.assertEqual(biomes.NAMES[biomes.dominant(temperature, moisture)], name, (temperature, moisture))

    def test_weights_sum_to_one_and_change_smoothly(self):
        grid = [(t, m) for t in np.linspace(-30.0, 40.0, 141) for m in np.linspace(-0.2, 1.2, 141)]
        for t, m in grid:
            w = biomes.weights(t, m)
            self.assertAlmostEqual(sum(w), 1.0, places=6)
            self.assertGreaterEqual(min(w), -1e-9)
        # A step of 0.05 degrees or 0.001 of moisture moves no weight by more than 0.05.
        for t, m in grid[::7]:
            here = np.array(biomes.weights(t, m))
            self.assertLess(np.abs(np.array(biomes.weights(t + 0.05, m)) - here).max(), 0.05)
            self.assertLess(np.abs(np.array(biomes.weights(t, m + 0.001)) - here).max(), 0.05)

    def test_snow_and_card_rows(self):
        self.assertTrue(biomes.snowy(biomes.SNOW_TEMPERATURE - 0.5))
        self.assertFalse(biomes.snowy(biomes.SNOW_TEMPERATURE + 0.5))
        self.assertEqual(biomes.card_rows(None), [("BIOME", "-"), ("CLIMATE", "-")])
        self.assertEqual(biomes.card_rows((-10.0, 0.5)), [("BIOME", "tundra, snow"), ("CLIMATE", "-10 C, moisture 0.50")])
        self.assertEqual(biomes.card_rows((24.0, 0.8))[0], ("BIOME", "tropical forest"))

    def test_palettes_cycle_from_any_hues(self):
        class Hues:
            flora_hue, soil_hue = 33.0, 0.0
        hues = Hues()
        names = [settings.next_palette(hues) for _ in range(len(biomes.PALETTES) + 1)]
        self.assertEqual(names, [p[0] for p in biomes.PALETTES] + [biomes.PALETTES[0][0]])
        self.assertEqual((hues.flora_hue, hues.soil_hue), biomes.PALETTES[0][1:])


class BiomeShader(unittest.TestCase):
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
        path = cls.tmp / "biome_probe.comp"
        path.write_text(KERNEL, encoding="utf-8")
        cls.kernel = cls.ctx.kernel(path, args=["-I", str(world.PIPELINE.parent / "shaders")])

    @classmethod
    def tearDownClass(cls):
        cls.ctx.shutdown()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_items(self, mode, items, climate=(0.0, 0.0, 0.0, 0.0)):
        """The kernel's outputs (OUT per item) for items of three vec4 each."""
        items = np.ascontiguousarray(items, dtype=np.float32).reshape(-1, 12)
        params = [0.0] * 4 * 7 + list(climate) + [0.0] * 4    # the seed at 0
        params[4 * 5 + 2] = world.RADIUS                         # siteShape.z
        uniform = self.ctx.upload(struct.pack("<36f", *params))
        out = np.zeros(len(items) * OUT, dtype=np.float32)
        self.ctx.run(self.kernel, [uniform, items, out], threads=len(items),
                     push=struct.pack("<II", len(items), mode))
        return out.reshape(-1, OUT)

    def test_weights_equal_the_python_mirror(self):
        grid = np.array([(t, m) for t in np.linspace(-25.0, 35.0, 121) for m in np.linspace(0.0, 1.0, 101)])
        items = np.zeros((len(grid), 12))
        items[:, :2] = grid
        gpu = self.run_items(0, items)
        cpu = np.array([biomes.weights(t, m) for t, m in grid])
        np.testing.assert_allclose(gpu, cpu, atol=2e-5)

    def test_settings_offsets(self):
        items = np.zeros((4, 12))
        items[:, :3] = [(10.0, 0.5, 0.3), (10.0, 0.9, 0.7), (-5.0, 0.1, 0.0), (30.0, 0.2, 1.0)]
        out = self.run_items(2, items, climate=(-4.0, 0.25, 0.0, 0.0))
        np.testing.assert_allclose(out[:, :3], [(6.0, 0.75, 0.3), (6.0, 1.0, 0.7), (-9.0, 0.35, 0.0),
                                                (26.0, 0.45, 1.0)], atol=1e-5)

    def test_ground_layers_sum_to_one(self):
        rng = np.random.default_rng(3)
        count = 4096
        radial = rng.normal(size=(count, 3))
        radial /= np.linalg.norm(radial, axis=1, keepdims=True)
        tilt = rng.normal(size=(count, 3)) * rng.uniform(0.0, 1.5, size=(count, 1))
        normal = radial + tilt
        normal /= np.linalg.norm(normal, axis=1, keepdims=True)
        items = np.zeros((count, 12))
        items[:, :3] = radial
        items[:, 3] = rng.uniform(-500.0, 3000.0, size=count)    # sea floor, beaches, land, high ground
        items[:, 4:7] = normal
        items[:, 8] = rng.uniform(-30.0, 35.0, size=count)
        items[:, 9] = rng.uniform(-0.2, 1.2, size=count)
        items[:, 10] = rng.uniform(0.0, 1.0, size=count)
        out = self.run_items(1, items)[:, :7]
        np.testing.assert_allclose(out.sum(axis=1), 1.0, atol=1e-5)
        self.assertGreaterEqual(out.min(), -1e-6)

    def test_hue_turn_keeps_luminance(self):
        rng = np.random.default_rng(4)
        colours = rng.uniform(0.05, 0.5, size=(256, 3)) * np.array([0.6, 1.0, 0.5])
        items = np.zeros((len(colours) * 2, 12))
        items[:len(colours), :3] = colours
        items[:len(colours), 3] = 0.0
        items[len(colours):, :3] = colours
        items[len(colours):, 3] = rng.uniform(-np.pi, np.pi, size=len(colours))
        out = self.run_items(3, items)[:, :3]
        np.testing.assert_allclose(out[:len(colours)], colours, atol=1e-6)
        luminance = np.array([0.299, 0.587, 0.114])
        turned = out[len(colours):]
        unclipped = (turned > 0.0).all(axis=1)
        self.assertGreater(unclipped.sum(), len(colours) // 2)
        np.testing.assert_allclose(turned[unclipped] @ luminance, colours[unclipped] @ luminance, atol=1e-4)


if __name__ == "__main__":
    unittest.main()

"""
shoonyakasha.planet.brdf_lut: the GGX split-sum table computed on the GPU
(brdf_lut.comp), checked at its corners against what the split sum gives
there, and the sky probe's constants (sky_probe.glsl) against the
pipeline's images.
"""

import json
import unittest
from pathlib import Path

import numpy as np

import shoonyakasha as sk
from shoonyakasha.planet import brdf_lut

SHADERS = Path(brdf_lut.__file__).resolve().parent.parent / "pipelines" / "planet"


class TableTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not sk.extension_available():
            raise unittest.SkipTest("the engine's extension is not importable here")
        try:
            ctx = sk.ComputeContext()
        except RuntimeError as exc:
            raise unittest.SkipTest("no Vulkan device: %s" % exc)
        try:
            cls.table = brdf_lut._compute(ctx)
        finally:
            ctx.shutdown()

    def test_shape_and_range(self):
        self.assertEqual(self.table.shape, (brdf_lut.SIZE, brdf_lut.SIZE, 2))
        self.assertFalse(np.isnan(self.table).any())
        self.assertTrue((self.table >= 0.0).all() and (self.table.sum(axis=-1) <= 1.0 + 1e-3).all())

    def test_smooth_head_on_reflects_f0_alone(self):
        a, b = self.table[0, -1]
        self.assertAlmostEqual(float(a), 1.0, places=2)
        self.assertAlmostEqual(float(b), 0.0, places=2)

    def test_smooth_grazing_reflects_mostly_the_fresnel_bias(self):
        a, b = self.table[0, 0]
        self.assertGreater(float(b), 0.8)

    def test_rougher_reflects_less_head_on(self):
        self.assertLess(float(self.table[-1, -1].sum()), float(self.table[0, -1].sum()))

    def test_rgba16(self):
        texels = brdf_lut.rgba16(self.table)
        self.assertEqual(texels.dtype, np.float16)
        self.assertEqual(texels.shape, (brdf_lut.SIZE, brdf_lut.SIZE, 4))
        np.testing.assert_allclose(texels[..., :2].astype(np.float32), self.table, atol=2e-3)


class ProbeImagesTest(unittest.TestCase):
    def test_images_match_the_shaders_constants(self):
        text = (SHADERS / "shaders" / "sky_probe.glsl").read_text()
        size = int(text.split("const int SKY_PROBE_SIZE = ")[1].split(";")[0])
        levels = int(text.split("const int SKY_PROBE_LEVELS = ")[1].split(";")[0])
        images = {r["name"]: r["image"] for r in json.loads((SHADERS / "pipeline.json").read_text())["resources"]
                  if r.get("kind") == "image" and "image" in r}
        self.assertEqual((images["skyRadiance"]["width"], images["skyRadiance"]["height"]), (size, size))
        self.assertEqual((images["skyProbe"]["width"], images["skyProbe"]["height"]), (size, size * levels))
        self.assertEqual((images["skySh"]["width"], images["skySh"]["height"]), (9, 1))


if __name__ == "__main__":
    unittest.main()

"""Tests for the planet example's key caps (examples/python/simulations/planet/hints.py)
and ground textures (ground_textures.py), loaded from their files.

    python -m unittest discover -s tests/python
"""

import importlib.util
import tempfile
import unittest
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[2]
PLANET = REPO / "examples" / "python" / "simulations" / "planet"


def load(name):
    spec = importlib.util.spec_from_file_location("planet_" + name, PLANET / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


hints = load("hints")
ground_textures = load("ground_textures")


class FakeEngine:
    """Records set_pipeline_image_data's calls."""

    def __init__(self):
        self.images = {}

    def set_pipeline_image_data(self, name, data, format="R8G8B8A8_SRGB", mipmaps=False):
        self.images[name] = (data.shape, format, mipmaps)


class CapTest(unittest.TestCase):
    def test_pad_prefix(self):
        self.assertEqual(hints.cap_text("pad:A"), ("A", True))
        self.assertEqual(hints.cap_text("A"), ("A", False))
        self.assertEqual(hints.cap_width("pad:Start"), hints.cap_width("Start"))

    def test_caps_at_least_square(self):
        self.assertEqual(hints.cap_width("I"), hints.CAP_HEIGHT)
        self.assertGreater(hints.cap_width("Backspace"), hints.CAP_HEIGHT)

    def test_row_width_adds_up(self):
        one = [(("W", "S"), "Throttle")]
        two = one + [(("Tab",), "Free camera")]
        expected = hints.hint_width(one[0]) + hints.HINT_GAP + hints.hint_width(two[1])
        self.assertAlmostEqual(hints.row_width(two), expected)
        self.assertEqual(hints.row_width([]), 0.0)

    def test_label_widens_a_hint(self):
        self.assertGreater(hints.hint_width((("F1",), "Settings")), hints.hint_width((("F1",), "")))


class GroundTexturesTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.saved = ground_textures.DIRECTORY
        ground_textures.DIRECTORY = self.directory.name

    def tearDown(self):
        ground_textures.DIRECTORY = self.saved
        self.directory.cleanup()

    def write_layers(self, colour):
        """Every layer's files, lossless (PNG data under the .jpg names, which
        Pillow reads by their content)."""
        from PIL import Image
        for layer, _, _ in ground_textures.LAYERS:
            albedo, normal = ground_textures.paths(layer)
            Image.new("RGB", (8, 8), colour).save(albedo, format="PNG")
            Image.new("RGB", (8, 8), (128, 128, 255)).save(normal, format="PNG")

    def test_missing_files_load_nothing(self):
        engine = FakeEngine()
        self.assertFalse(ground_textures.available())
        self.assertIsNone(ground_textures.load(engine))
        self.assertEqual(engine.images, {})

    def test_load_hands_every_layer_over_with_mipmaps(self):
        self.write_layers((255, 255, 255))
        engine = FakeEngine()
        means = ground_textures.load(engine)
        self.assertEqual(len(means), len(ground_textures.LAYERS))
        for index in range(len(ground_textures.LAYERS)):
            shape, fmt, mipmaps = engine.images["groundAlbedo%d" % index]
            self.assertEqual((shape, fmt, mipmaps), ((8, 8, 4), "R8G8B8A8_SRGB", True))
            shape, fmt, mipmaps = engine.images["groundNormal%d" % index]
            self.assertEqual((shape, fmt, mipmaps), ((8, 8, 4), "R8G8B8A8_UNORM", True))

    def test_means_are_linear(self):
        self.write_layers((188, 188, 188))      # sRGB 188 is about 0.5 linear
        means = ground_textures.load(FakeEngine())
        for mean in means:
            for channel in mean:
                self.assertAlmostEqual(channel, 0.5029, places=3)

    def test_srgb_to_linear_ends(self):
        values = ground_textures._srgb_to_linear(np.array([0.0, 0.04045, 1.0]))
        self.assertAlmostEqual(values[0], 0.0)
        self.assertAlmostEqual(values[1], 0.04045 / 12.92)
        self.assertAlmostEqual(values[2], 1.0)


if __name__ == "__main__":
    unittest.main()

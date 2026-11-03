"""Tests for the planet's temporal anti-aliasing (world.py's taa_jitter and
upload_taa, the planet pipeline's TAA passes): the Halton sequence, the
jitter's cycle of distinct points within half a pixel whose mean is near
the pixel's centre; the values handed over each frame with a fake engine
(the history dropped on the first frame, after the camera jumps and after
turning it back on, the camera's motion since the frame before relative
to the body under it, nothing
jittered while it is off); and the pipeline: TAA after every pass that
draws into hdrColor and before Atmosphere, its history persistent and
copied after it, the atmosphere reading its result, and the passes whose
image it resolves jittered while the shadows and the orbit path are not.

    python -m unittest discover -s tests/python
"""

import json
import unittest

import numpy as np

from shoonyakasha.planet import world as planet_world


class FakeEngine:
    def __init__(self):
        self.framebuffer_size = (1600, 900)
        self.values = {}

    def set_custom_vec4(self, name, value):
        self.values[name] = tuple(value)


class JitterTest(unittest.TestCase):
    def test_halton(self):
        self.assertEqual([planet_world.halton(k, 2) for k in range(1, 5)], [0.5, 0.25, 0.75, 0.125])
        np.testing.assert_allclose([planet_world.halton(k, 3) for k in range(1, 4)], [1 / 3, 2 / 3, 1 / 9])

    def test_cycle_of_distinct_points_within_half_a_pixel(self):
        size = (1600, 900)
        points = [planet_world.taa_jitter(frame, size) for frame in range(planet_world.TAA_JITTER_FRAMES)]
        self.assertEqual(len(set(points)), planet_world.TAA_JITTER_FRAMES)
        self.assertEqual(planet_world.taa_jitter(planet_world.TAA_JITTER_FRAMES, size), points[0])
        pixels = np.array(points) * np.array(size) / 2.0
        self.assertTrue((np.abs(pixels) <= 0.5).all())
        self.assertLess(float(np.abs(pixels.mean(axis=0)).max()), 0.1)


class UploadTest(unittest.TestCase):
    def setUp(self):
        self.engine = FakeEngine()
        self.world = planet_world.PlanetWorld(self.engine)
        self.start = (0.0, 0.0, planet_world.RADIUS + 100.0)

    def taa(self):
        return self.engine.values["planet.taa"], self.engine.values["planet.taaMotion"]

    def test_first_frame_drops_the_history_then_motion_is_handed_over(self):
        self.world.upload_taa(self.start)
        (jx, jy, on, reset), motion = self.taa()
        self.assertEqual((on, reset), (1.0, 1.0))
        self.assertEqual(motion, (0.0, 0.0, 0.0, 0.0))
        self.world.upload_taa((3.0, -2.0, planet_world.RADIUS + 101.0))
        (jx2, jy2, on, reset), motion = self.taa()
        self.assertEqual((on, reset), (1.0, 0.0))
        np.testing.assert_allclose(motion, (3.0, -2.0, 1.0, 0.0))
        self.assertNotEqual((jx, jy), (jx2, jy2))

    def test_a_jump_drops_the_history(self):
        self.world.upload_taa(self.start)
        self.world.upload_taa((planet_world.TAA_RESET_DISTANCE * 1.5, 0.0, planet_world.RADIUS))
        self.assertEqual(self.taa()[0][3], 1.0)

    def test_motion_is_relative_to_the_body_under_the_camera(self):
        class Orbit:
            influence = 50_000.0

            def position(self, time):
                return (2_000_000.0 + 1_000.0 * time, 0.0, 0.0)

            def velocity(self, time):
                return (1_000.0, 0.0, 0.0)

        world = self.world
        moon = planet_world.Body(1, "moon", 25_000.0, None, world, orbit=Orbit(), floor=-2_000.0,
                                 heights=(-2_000.0, 1_500.0))
        world.bodies.append(moon)
        world.upload_taa(planet_world.add(moon.centre(), (0.0, 0.0, 25_002.0)))
        world.physics.time += 1.0 / 60.0
        world.upload_taa(planet_world.add(moon.centre(), (0.5, 0.0, 25_002.0)))
        (_, _, on, reset), motion = self.taa()
        self.assertEqual((on, reset), (1.0, 0.0))
        np.testing.assert_allclose(motion, (0.5, 0.0, 0.0, 0.0), atol=1e-6)
        world.upload_taa(self.start)
        self.assertEqual(self.taa()[0][3], 1.0)

    def test_off_jitters_nothing_and_turning_on_drops_the_history(self):
        self.world.upload_taa(self.start)
        self.world.taa = False
        self.world.upload_taa(self.start)
        self.assertEqual(self.taa(), ((0.0, 0.0, 0.0, 1.0), (0.0, 0.0, 0.0, 0.0)))
        self.world.taa = True
        self.world.upload_taa(self.start)
        self.assertEqual(self.taa()[0][2:], (1.0, 1.0))


class PipelineTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pipeline = json.loads(planet_world.PIPELINE.read_text(encoding="utf-8"))
        cls.passes = [p["name"] for p in cls.pipeline["passes"]]

    def index(self, name):
        return self.passes.index(name)

    def test_taa_resolves_every_hdr_pass_before_the_atmosphere(self):
        taa = self.index("TAA")
        for p in self.pipeline["passes"]:
            if any(o["resource"] == "hdrColor" for o in p.get("outputs", [])):
                self.assertLess(self.index(p["name"]), taa, p["name"])
        self.assertLess(taa, self.index("TAAHistory"))
        self.assertLess(self.index("TAAHistory"), self.index("Atmosphere"))
        bindings = self.pipeline["descriptorSetLayouts"]["atmosphereInputSet"]["bindings"]
        self.assertEqual(bindings[0]["autoBindResource"], "taaColor")
        resources = {r["name"]: r for r in self.pipeline["resources"]}
        self.assertTrue(resources["taaHistory"]["image"]["persistent"])

    def test_jittered_passes(self):
        for p in self.pipeline["passes"]:
            shader = p.get("pipeline", {}).get("vertexShader")
            if not shader or "fullscreen" in shader:
                continue
            source = (planet_world.PIPELINE.parent / shader.replace(".spv", "")).read_text(encoding="utf-8")
            draws_hdr = any(o["resource"] == "hdrColor" for o in p.get("outputs", []))
            self.assertEqual("jittered(" in source, draws_hdr, p["name"])


if __name__ == "__main__":
    unittest.main()

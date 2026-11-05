"""Tests for the planet pipeline's local lights (shoonyakasha.planet.world's
Light and PlanetWorld.upload_lights, lights.glsl): the MAX_LIGHTS nearest
the camera that reach within their range plus LIGHT_VIEW_REACH of it are
sent, nearest first, relative to the camera with their ranges, a spot
with its direction and its cone's cosines and a point light with none,
the rest of the slots empty and the count set; and MAX_LIGHTS is the
shader's and pipeline.json's.

    python -m unittest discover -s tests/python
"""

import json
import math
import types
import unittest

from shoonyakasha.planet import world


class FakeEngine:
    def __init__(self):
        self.values = {}

    def set_custom_vec4(self, name, value):
        self.values[name] = tuple(value)

    def set_custom_vec4_array(self, name, values):
        self.values[name] = [tuple(v) for v in values]


def upload(lights, camera):
    fake = types.SimpleNamespace(lights=lights, engine=FakeEngine(), lights_drawn=0)
    world.PlanetWorld.upload_lights(fake, camera)
    return fake


class UploadLights(unittest.TestCase):
    def test_nearest_within_reach_relative_to_the_camera(self):
        camera = (1_000.0, 2_000.0, 3_000.0)
        near = [world.Light((1_000.0 + 10.0 * k, 2_000.0, 3_000.0), (1.0, 2.0, 3.0), 50.0) for k in range(1, 11)]
        far = world.Light((1_000.0 + world.LIGHT_VIEW_REACH + 200.0, 2_000.0, 3_000.0), (1.0, 1.0, 1.0), 100.0)
        spot = world.Light((1_000.0, 2_000.0, 3_005.0), (5.0, 5.0, 5.0), 30.0, (0.0, 0.0, -1.0),
                           math.radians(10.0), math.radians(20.0))
        fake = upload(near[::-1] + [far, spot], camera)
        values = fake.engine.values
        self.assertEqual(fake.lights_drawn, world.MAX_LIGHTS)
        self.assertEqual(values["planet.lights.count"][0], float(world.MAX_LIGHTS))
        positions = values["planet.lights.position"]
        self.assertEqual(len(positions), world.MAX_LIGHTS)
        # The spot first (5 m), then the point lights from 10 m on; the far one left out.
        self.assertEqual(positions[0], (0.0, 0.0, 5.0, 30.0))
        self.assertEqual([p[0] for p in positions[1:]], [10.0 * k for k in range(1, world.MAX_LIGHTS)])
        directions, colours = values["planet.lights.direction"], values["planet.lights.colour"]
        self.assertEqual(directions[0][:3], (0.0, 0.0, -1.0))
        self.assertAlmostEqual(directions[0][3], math.cos(math.radians(20.0)))
        self.assertAlmostEqual(colours[0][3], math.cos(math.radians(10.0)))
        self.assertEqual(directions[1][:3], (0.0, 0.0, 0.0))
        self.assertEqual(colours[1], (1.0, 2.0, 3.0, 1.0))

    def test_empty_slots(self):
        values = upload([], (0.0, 0.0, 0.0)).engine.values
        self.assertEqual(values["planet.lights.count"][0], 0.0)
        self.assertEqual(values["planet.lights.position"], [(0.0, 0.0, 0.0, 0.0)] * world.MAX_LIGHTS)

    def test_count_is_the_shaders(self):
        shaders = world.PIPELINE.parent / "shaders"
        self.assertIn("const int MAX_LIGHTS = %d;" % world.MAX_LIGHTS,
                      (shaders / "lights.glsl").read_text(encoding="utf-8"))
        self.assertIn("lightPosition[%d];" % world.MAX_LIGHTS, (shaders / "camera.glsl").read_text(encoding="utf-8"))
        pipeline = json.loads(world.PIPELINE.read_text(encoding="utf-8"))
        fields = {f["name"]: f for f in pipeline["bufferLayouts"]["AtmosphereParams"]["fields"]}
        for name in ("lightPosition", "lightDirection", "lightColour"):
            self.assertEqual(fields[name]["arrayCount"], world.MAX_LIGHTS)


if __name__ == "__main__":
    unittest.main()

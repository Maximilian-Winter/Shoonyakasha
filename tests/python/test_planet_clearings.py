"""Tests for the clearings of the planet pipeline (clearings.glsl), on the
GPU, with the clearings packed by PlanetWorld.set_clearings: nothing is
kept within a circle or a rectangle (turned by its heading) and
everything beyond it; across its edge the share kept rises linearly; a
clearing clears only the kinds it names and only on its own body; where
clearings overlap the least share is kept; and with no clearings
everything is kept. The tests need a Vulkan 1.3 device and glslc;
without them they are skipped.

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
from shoonyakasha.planet.vec import add, local_frame, normalize, scale
from shoonyakasha.planet.world import CLEAR_ALL, CLEAR_GRASS, CLEAR_PLANTS, CLEAR_ROCKS, Clearing

KERNEL = """#version 450
layout(local_size_x = 64) in;
layout(std140, set = 0, binding = 0) uniform VegetationParams {
    vec4 clearings[32]; vec4 clearingShapes[32]; vec4 clearingAxes[32];
} vegetation;
layout(std430, set = 0, binding = 1) readonly buffer Inputs { vec4 items[]; } src;
layout(std430, set = 0, binding = 2) writeonly buffer Outputs { vec4 values[]; } dst;
layout(push_constant) uniform Push { uint count; } pc;

#include "clearings.glsl"

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    uint code = uint(src.items[i].w);
    dst.values[i] = vec4(clearingKeep(normalize(src.items[i].xyz), int(code / 8u), code % 8u), 0.0, 0.0, 0.0);
}
"""
PLANET_CENTRE = normalize((0.6, 0.0, 0.8))
MOON_CENTRE = (0.0, 1.0, 0.0)


class FakeEngine:
    def __init__(self):
        self.custom = {}

    def set_custom_vec4(self, name, value):
        self.custom[name] = tuple(value)

    def set_custom_vec4_array(self, name, values):
        self.custom[name] = [tuple(v) for v in values]


def towards(direction, radius, bearing, distance):
    """The unit direction `distance` metres along a sphere of `radius` from
    `direction` towards `bearing` (radians from north towards east)."""
    east, north = local_frame(direction)
    tangent = add(scale(north, math.cos(bearing)), scale(east, math.sin(bearing)))
    angle = distance / radius
    return normalize(add(scale(direction, math.cos(angle)), scale(tangent, math.sin(angle))))


class ClearingsShader(unittest.TestCase):
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
        path = cls.tmp / "clearings_probe.comp"
        path.write_text(KERNEL, encoding="utf-8")
        cls.kernel = cls.ctx.kernel(path, args=["-I", str(world.PIPELINE.parent / "shaders")])

    @classmethod
    def tearDownClass(cls):
        cls.ctx.shutdown()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def keep(self, clearings, points):
        """The share kept at `points`, (direction, body index, kinds) triples, with `clearings`."""
        engine = FakeEngine()
        world.PlanetWorld(engine).set_clearings(clearings)
        params = [c for name in ("clearings", "clearingShapes", "clearingAxes")
                  for v in engine.custom["planet.vegetation." + name] for c in v]
        uniform = self.ctx.upload(struct.pack("<%df" % len(params), *params))
        items = np.array([tuple(d) + (float(body * 8 + kinds),) for d, body, kinds in points], dtype=np.float32)
        out = np.zeros((len(points), 4), dtype=np.float32)
        self.ctx.run(self.kernel, [uniform, items, out], threads=len(points), push=struct.pack("<I", len(points)))
        return out[:, 0]

    def ring(self, distance, body=0, kinds=CLEAR_ALL, centre=PLANET_CENTRE, radius=world.RADIUS, count=8):
        return [(towards(centre, radius, 2.0 * math.pi * k / count, distance), body, kinds) for k in range(count)]

    def test_circle(self):
        circle = Clearing(0, PLANET_CENTRE, radius=40.0)
        inside = self.ring(0.0, count=1) + self.ring(20.0) + self.ring(39.0)
        outside = self.ring(41.0) + self.ring(100.0)
        keep = self.keep([circle], inside + outside)
        np.testing.assert_array_equal(keep[:len(inside)], 0.0)
        np.testing.assert_array_equal(keep[len(inside):], 1.0)

    def test_circle_edge(self):
        circle = Clearing(0, PLANET_CENTRE, radius=40.0, edge=20.0)
        keep = self.keep([circle], self.ring(39.0) + self.ring(45.0) + self.ring(50.0) + self.ring(61.0))
        np.testing.assert_array_equal(keep[:8], 0.0)
        np.testing.assert_allclose(keep[8:16], 0.25, atol=0.01)
        np.testing.assert_allclose(keep[16:24], 0.5, atol=0.01)
        np.testing.assert_array_equal(keep[24:], 1.0)

    def test_rectangle_turned_by_its_heading(self):
        heading = math.radians(30.0)
        rectangle = Clearing(0, PLANET_CENTRE, size=(100.0, 20.0), heading=heading)
        r = world.RADIUS

        def at(bearing, distance):
            return (towards(PLANET_CENTRE, r, bearing, distance), 0, CLEAR_ALL)

        inside = [at(heading, 45.0), at(heading + math.pi, 45.0), at(heading + 0.5 * math.pi, 9.0),
                  at(heading - 0.5 * math.pi, 9.0), at(heading + math.atan2(9.0, 45.0), math.hypot(45.0, 9.0))]
        outside = [at(heading, 55.0), at(heading + math.pi, 55.0), at(heading + 0.5 * math.pi, 11.0),
                   at(heading + 0.5 * math.pi, 45.0), at(heading - 0.5 * math.pi, 45.0), at(0.0, 45.0)]
        keep = self.keep([rectangle], inside + outside)
        np.testing.assert_array_equal(keep[:len(inside)], 0.0)
        np.testing.assert_array_equal(keep[len(inside):], 1.0)

    def test_rectangle_edge(self):
        heading = math.radians(-70.0)
        rectangle = Clearing(0, PLANET_CENTRE, size=(100.0, 20.0), heading=heading, edge=20.0)
        points = [(towards(PLANET_CENTRE, world.RADIUS, heading, 60.0), 0, CLEAR_ALL),
                  (towards(PLANET_CENTRE, world.RADIUS, heading + 0.5 * math.pi, 15.0), 0, CLEAR_ALL)]
        np.testing.assert_allclose(self.keep([rectangle], points), [0.5, 0.25], atol=0.01)

    def test_only_the_kinds_it_clears(self):
        grass = Clearing(0, PLANET_CENTRE, radius=40.0, clears=CLEAR_GRASS)
        points = [self.ring(10.0, kinds=kinds, count=1)[0]
                  for kinds in (CLEAR_PLANTS, CLEAR_GRASS, CLEAR_ROCKS, CLEAR_PLANTS | CLEAR_GRASS)]
        np.testing.assert_array_equal(self.keep([grass], points), [1.0, 0.0, 1.0, 0.0])

    def test_only_on_its_own_body(self):
        moon = Clearing(1, MOON_CENTRE, radius=40.0)
        points = (self.ring(10.0, body=1, centre=MOON_CENTRE, radius=world.MOON_RADIUS)
                  + self.ring(10.0, body=0, centre=MOON_CENTRE))
        keep = self.keep([moon], points)
        np.testing.assert_array_equal(keep[:8], 0.0)
        np.testing.assert_array_equal(keep[8:], 1.0)

    def test_overlapping_keep_the_least(self):
        wide = Clearing(0, PLANET_CENTRE, radius=10.0, edge=80.0)
        narrow = Clearing(0, PLANET_CENTRE, radius=40.0, edge=40.0)
        np.testing.assert_allclose(self.keep([wide], self.ring(50.0)), 0.5, atol=0.01)
        np.testing.assert_allclose(self.keep([narrow], self.ring(50.0)), 0.25, atol=0.01)
        np.testing.assert_allclose(self.keep([wide, narrow], self.ring(50.0)), 0.25, atol=0.01)
        np.testing.assert_allclose(self.keep([narrow, wide], self.ring(50.0)), 0.25, atol=0.01)

    def test_none(self):
        np.testing.assert_array_equal(self.keep([], self.ring(0.0, count=1) + self.ring(30.0)), 1.0)


if __name__ == "__main__":
    unittest.main()

"""Tests for the rivers and the height grid of the planet pipeline on the
GPU: rivers.glsl with the data rivers.pack lays out for a straight river
(the channel under the water within its half width, the water level at
its edge, the planet's own terrain beyond its bank, the ground continuous
across, the water where RiverLookup finds it, and no change without
rivers), and the HeightGrid pass's shader (height_grid.comp) measuring
the terrain at the directions of rivers.GridFrame, with a lake's water as
its surface and the frame's stamp first. The tests need a Vulkan 1.3
device and glslc; without them they are skipped.

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
from shoonyakasha.planet import rivers, world
from shoonyakasha.planet.world import Lake, River

TERRAIN_BLOCK = """layout(std140, set = 0, binding = 0) uniform TerrainParams {
    vec4 continent; vec4 mountains; vec4 hills; vec4 seed; vec4 site; vec4 siteShape; vec4 detail;
    vec4 climate; vec4 biomeView; vec4 sites[8]; vec4 siteShapes[8]; vec4 lakes[32]; vec4 lakeShapes[32];
    vec4 riverFrame[4]; vec4 riverOrigin;
} terrain;"""
RIVER_KERNEL = """#version 450
layout(local_size_x = 64) in;
%s
layout(std430, set = 0, binding = 1) readonly buffer RiverData { vec4 data[]; } riverData;
layout(std430, set = 0, binding = 2) readonly buffer Inputs { vec4 items[]; } src;
layout(std430, set = 0, binding = 3) writeonly buffer Outputs { vec4 values[]; } dst;
layout(push_constant) uniform Push { uint count; } pc;

#include "terrain.glsl"
#include "rivers.glsl"

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    vec3 d = normalize(src.items[i].xyz);
    float natural = terrainHeight(d);
    float depth;
    bool wet = riverAt(d, depth);
    dst.values[i] = vec4(natural, terrainWithRivers(natural, d), wet ? 1.0 : 0.0, depth);
}
""" % TERRAIN_BLOCK
NATURAL_KERNEL = """#version 450
layout(local_size_x = 64) in;
%s
layout(std430, set = 0, binding = 1) readonly buffer Inputs { vec4 items[]; } src;
layout(std430, set = 0, binding = 2) writeonly buffer Outputs { vec4 values[]; } dst;
layout(push_constant) uniform Push { uint count; } pc;

#include "terrain.glsl"
#include "lakes.glsl"

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    vec3 d = normalize(src.items[i].xyz);
    float depth;
    float h = terrainWithLakes(terrainHeight(d), d);
    if (lakeAt(d, depth) >= 0) h += depth;
    dst.values[i] = vec4(h, 0.0, 0.0, 0.0);
}
""" % TERRAIN_BLOCK
CENTRE = (0.6, 0.0, 0.8)
WIDTH, DEPTH = 20.0, 3.0


class FakeEngine:
    def __init__(self):
        self.custom = {}

    def set_custom_vec4(self, name, value):
        self.custom[name] = tuple(value)

    def set_custom_vec4_array(self, name, values):
        self.custom[name] = [tuple(v) for v in values]


class NoLod:
    def has_tree(self, index):
        return False


def terrain_params(lakes=(), river_frame=None):
    """TerrainParams' floats: the default terrain, no sites, `lakes`, and
    the rivers' frame (none when None)."""
    engine = FakeEngine()
    w = world.PlanetWorld(engine)
    w._lod = NoLod()
    w.set_lakes(list(lakes))
    t = world.TERRAIN
    frame = river_frame if river_frame is not None else [(0.0, 0.0, 0.0, 0.0)] * 4
    return (list(t["continent"]) + list(t["mountains"]) + list(t["hills"]) + [0.0] * 4 + [0.0] * 4
            + [150.0, 600.0, world.RADIUS, 0.0] + list(t["detail"]) + [0.0] * 8
            + [0.0, 0.0, 1.0, 0.0] * 8 + [0.0] * 32
            + [c for v in engine.custom["planet.terrain.lakes"] for c in v]
            + [c for v in engine.custom["planet.terrain.lakeShapes"] for c in v]
            + [c for v in frame for c in v] + [0.0] * 4)


class Shaders(unittest.TestCase):
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
        include = ["-I", str(world.PIPELINE.parent / "shaders")]
        for name, source in (("river_probe.comp", RIVER_KERNEL), ("natural_probe.comp", NATURAL_KERNEL)):
            (cls.tmp / name).write_text(source, encoding="utf-8")
        cls.river_kernel = cls.ctx.kernel(cls.tmp / "river_probe.comp", args=include)
        cls.natural_kernel = cls.ctx.kernel(cls.tmp / "natural_probe.comp", args=include)
        cls.grid_kernel = cls.ctx.kernel(world.PIPELINE.parent / "shaders" / "height_grid.comp", args=include)
        cls.frame = rivers.GridFrame(CENTRE, world.RADIUS, size=64, spacing=200.0)
        # The water 5 m below the lowest ground near the river, as a traced river's lies below its ground.
        gx, gy = np.meshgrid(np.linspace(-2_200.0, 2_200.0, 89), np.linspace(-200.0, 200.0, 21))
        around = cls.run_natural(cls, cls.frame.from_plane(gx, gy).reshape(-1, 3), [])
        cls.level = float(np.min(around)) - 5.0
        xs = np.linspace(-2_000.0, 2_000.0, 9)
        points = tuple(tuple(d) for d in cls.frame.from_plane(xs, np.zeros_like(xs)))
        cls.river = River(points, (cls.level,) * 9, (WIDTH,) * 9, (DEPTH,) * 9)

    @classmethod
    def tearDownClass(cls):
        cls.ctx.shutdown()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def at(self, x, y):
        return tuple(self.frame.from_plane(np.array(x), np.array(y)))

    def run_rivers(self, directions, with_rivers=True):
        """(natural, with the rivers, wet, depth) at `directions`."""
        found = [self.river] if with_rivers else []
        if found:
            data, frame_params = rivers.pack(found, self.frame, world.RADIUS)
        else:
            data, frame_params = np.zeros((rivers.MAX_DATA, 4), dtype=np.float32), None
        params = terrain_params(river_frame=frame_params)
        uniform = self.ctx.upload(struct.pack("<%df" % len(params), *params))
        items = np.array([tuple(d) + (0.0,) for d in directions], dtype=np.float32)
        out = np.zeros((len(directions), 4), dtype=np.float32)
        self.ctx.run(self.river_kernel, [uniform, data, items, out], threads=len(directions),
                     push=struct.pack("<I", len(directions)))
        return out

    def test_the_channel_and_its_edge(self):
        across = [0.0, 3.0, 6.0, 9.0, 0.5 * WIDTH - 0.01]
        out = self.run_rivers([self.at(100.0, y) for y in across])
        for (natural, h, wet, depth), y in zip(out, across):
            u = y / (0.5 * WIDTH)
            self.assertAlmostEqual(float(h), self.level - DEPTH * (1.0 - u * u), delta=2e-3)
            self.assertEqual(wet, 1.0)
            self.assertAlmostEqual(float(depth), DEPTH * (1.0 - u * u), delta=2e-3)

    def test_the_planets_own_beyond_the_bank(self):
        far = 0.5 * WIDTH + rivers.BANK_MAX + 5.0
        out = self.run_rivers([self.at(x, far) for x in (-1_500.0, 0.0, 900.0)]
                              + [self.at(x, -far) for x in (-700.0, 300.0)] + [self.at(3_000.0, 0.0)])
        # mix(level, height, 1) is level + (height - level), within a unit in the last place of height.
        np.testing.assert_allclose(out[:, 1], out[:, 0], atol=1e-4)
        np.testing.assert_array_equal(out[:, 2], 0.0)

    def test_continuous_across_the_banks(self):
        ys = np.arange(-180.0, 180.0, 0.25)
        out = self.run_rivers([self.at(400.0, y) for y in ys])
        jumps = np.abs(np.diff(out[:, 1]))
        natural_jumps = np.abs(np.diff(out[:, 0]))
        slope = rivers.BANK_SLOPE
        self.assertLess(float(np.max(jumps)), max(0.5, 2.0 * float(np.max(natural_jumps))) + 0.25 / slope)
        self.assertTrue(np.all(out[:, 1] <= out[:, 0] + 1e-3))

    def test_the_water_where_the_lookup_finds_it(self):
        lookup = rivers.RiverLookup([self.river], self.frame, world.RADIUS)
        directions = [self.at(x, y) for x in (-1_200.0, 50.0, 1_700.0) for y in (-12.0, -9.0, 0.0, 4.0, 9.9, 10.1, 30.0)]
        out = self.run_rivers(directions)
        for direction, row in zip(directions, out):
            self.assertEqual(row[2] == 1.0, lookup.level(np.array(direction)) is not None)

    def test_no_rivers(self):
        out = self.run_rivers([self.at(100.0, 0.0), self.at(0.0, 50.0)], with_rivers=False)
        np.testing.assert_array_equal(out[:, 1], out[:, 0])
        np.testing.assert_array_equal(out[:, 2], 0.0)

    def test_the_height_grid(self):
        frame = rivers.GridFrame(CENTRE, world.RADIUS, size=24, spacing=300.0)
        lake = Lake(tuple(frame.from_plane(np.array(600.0), np.array(-300.0))), 0.0, 400.0, 10.0, 200.0, 0.2)
        natural = self.run_natural(frame.directions(), [lake], level_only=True)
        lake = Lake(lake.direction, float(natural) + 5.0, lake.radius, lake.depth, lake.bank, lake.wobble)
        params = terrain_params(lakes=[lake])
        terrain = self.ctx.upload(struct.pack("<%df" % len(params), *params))
        stamp = 7.0
        grid = (tuple(frame.centre) + (0.0,) + tuple(frame.east) + (0.0,) + tuple(frame.north) + (0.0,)
                + (float(frame.size), frame.spacing, stamp, 0.0))
        grid_params = self.ctx.upload(struct.pack("<16f", *grid))
        n = frame.size * frame.size
        out = np.zeros(world.HEIGHT_GRID_HEADER + n, dtype=np.float32)
        self.ctx.run(self.grid_kernel, [grid_params, terrain, out], threads=n)
        self.assertEqual(out[0], stamp)
        expected = self.run_natural(frame.directions(), [lake])
        # The shader makes each direction in 32-bit floats at the planet's radius, about a centimetre from these.
        np.testing.assert_allclose(out[world.HEIGHT_GRID_HEADER:], expected, atol=0.02)
        under = np.flatnonzero(np.isclose(expected, lake.level, atol=1e-3))
        self.assertGreater(len(under), 0)

    def run_natural(self, directions, lakes, level_only=False):
        """The terrain with `lakes`' water as its surface at `directions`;
        with `level_only`, the planet's own terrain at the first lake's centre."""
        if level_only:
            directions, lakes = [lakes[0].direction], []
        params = terrain_params(lakes=lakes)
        uniform = self.ctx.upload(struct.pack("<%df" % len(params), *params))
        items = np.array([tuple(d) + (0.0,) for d in directions], dtype=np.float32)
        out = np.zeros((len(directions), 4), dtype=np.float32)
        self.ctx.run(self.natural_kernel, [uniform, items, out], threads=len(directions),
                     push=struct.pack("<I", len(directions)))
        return out[0, 0] if level_only else out[:, 0]


if __name__ == "__main__":
    unittest.main()

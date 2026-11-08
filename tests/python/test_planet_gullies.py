"""Tests for the terrain's gullies (terrain.glsl), on the GPU: the noise
with gradients (ridged3d, erodedFbm3d) gives the same values as the noise
without, and gradients matching finite differences where they are exact;
octavesLongerThan counts the octaves at least a wavelength long; the
gullies are none on flat ground, within their octaves' summed depth,
about as much above as below, and run down the slope: they change less
along it than across it. The tests need a Vulkan 1.3 device and glslc;
without them they are skipped.

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
layout(std430, set = 0, binding = 1) readonly buffer Inputs { vec4 items[]; } src;
layout(std430, set = 0, binding = 2) writeonly buffer Outputs { vec4 values[]; } dst;
layout(push_constant) uniform Push { uint count; uint mode; } pc;

#include "terrain.glsl"

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    vec3 p = src.items[i].xyz;
    if (pc.mode == 0u) {
        dst.values[i] = vec4(ridged3(p, 6, 2.1, 0.5), erodedFbm3(p, 6, 2.0, 0.5, 0.5),
                             ridged3d(p, 6, 2.1, 0.5, 6).x, erodedFbm3d(p, 6, 2.0, 0.5, 0.5, 6).x);
    } else if (pc.mode == 1u) {
        dst.values[i] = ridged3d(p, 1, 2.1, 0.5, 1);
    } else if (pc.mode == 2u) {
        dst.values[i] = erodedFbm3d(p, 4, 2.0, 0.5, 0.0, 4);
    } else if (pc.mode == 3u) {
        dst.values[i] = vec4(ridged3(p, 1, 2.1, 0.5), erodedFbm3(p, 4, 2.0, 0.5, 0.0), 0.0, 0.0);
    } else if (pc.mode == 4u) {
        dst.values[i] = vec4(terrainGullies(normalize(p), src.items[i + pc.count].xyz), 0.0, 0.0, 0.0);
    } else {
        int a = octavesLongerThan(p.x, terrain.siteShape.z, p.y, p.z);
        dst.values[i] = vec4(float(a), 0.0, 0.0, 0.0);
    }
}
"""
DEPTH, WAVELENGTH, OCTAVES, FULL = 50.0, 900.0, 4.0, 0.25


def frame(direction):
    """(east, north) at the unit `direction`."""
    d = np.asarray(direction, dtype=np.float64)
    east = np.cross((0.0, 1.0, 0.0), d)
    east /= np.linalg.norm(east)
    return east, np.cross(d, east)


class GulliesShader(unittest.TestCase):
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
        path = cls.tmp / "gullies_probe.comp"
        path.write_text(KERNEL, encoding="utf-8")
        cls.kernel = cls.ctx.kernel(path, args=["-I", str(world.PIPELINE.parent / "shaders")])

    @classmethod
    def tearDownClass(cls):
        cls.ctx.shutdown()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_kernel(self, items, mode, count=None, gullies=(DEPTH, WAVELENGTH, OCTAVES, FULL)):
        """The kernel's values for the first `count` (all when None) of `items` in `mode`."""
        count = len(items) if count is None else count
        t = world.TERRAIN
        params = (list(t["continent"]) + list(t["mountains"]) + list(t["hills"]) + [0.0] * 4 + [0.0] * 4
                  + [150.0, 600.0, world.RADIUS, 0.0] + list(t["detail"]) + [0.0] * 8 + list(gullies))
        uniform = self.ctx.upload(struct.pack("<%df" % len(params), *params))
        items = np.ascontiguousarray(items, dtype=np.float32)
        out = np.zeros((len(items), 4), dtype=np.float32)
        self.ctx.run(self.kernel, [uniform, items, out], threads=count, push=struct.pack("<II", count, mode))
        return out[:count].astype(np.float64)

    def test_noise_with_gradients_gives_the_same_values(self):
        rng = np.random.default_rng(1)
        points = np.concatenate([rng.uniform(-40.0, 40.0, (2000, 3)), np.zeros((2000, 1))], axis=1)
        out = self.run_kernel(points, 0)
        np.testing.assert_allclose(out[:, 2], out[:, 0], atol=1e-5)
        np.testing.assert_allclose(out[:, 3], out[:, 1], atol=1e-5)

    def test_gradients_match_finite_differences(self):
        rng = np.random.default_rng(2)
        p = rng.uniform(-40.0, 40.0, (500, 3))
        h = 1e-3
        for mode, plain in ((1, 0), (2, 1)):
            got = self.run_kernel(np.concatenate([p, np.zeros((500, 1))], axis=1), mode)[:, 1:]
            numeric = np.zeros_like(p)
            for axis in range(3):
                step = np.zeros(3)
                step[axis] = h
                up = self.run_kernel(np.concatenate([p + step, np.zeros((500, 1))], axis=1), 3)[:, plain]
                down = self.run_kernel(np.concatenate([p - step, np.zeros((500, 1))], axis=1), 3)[:, plain]
                numeric[:, axis] = (up - down) / (2.0 * h)
            # ridged3's crease (|n| at n = 0) has no gradient; points near it are left out.
            near = np.linalg.norm(got - numeric, axis=1) > 0.05 * (1.0 + np.linalg.norm(numeric, axis=1))
            self.assertLess(near.mean(), 0.02, mode)

    def test_octaves_longer_than(self):
        cases = [(900.0, 6.0, 2.1), (900.0, 40.0, 2.0), (1e6, 6.0, 2.1), (100.0, 40.0, 2.0)]
        out = self.run_kernel([c + (0.0,) for c in cases], 5)[:, 0]
        for (wavelength, frequency, lacunarity), count in zip(cases, out):
            want = sum(1 for i in range(64) if world.RADIUS / (frequency * lacunarity ** i) >= wavelength)
            self.assertEqual(int(count), want, (wavelength, frequency))

    def gullies_at(self, directions, gradients, gullies=(DEPTH, WAVELENGTH, OCTAVES, FULL)):
        """terrainGullies at each direction with its gradient (metres per unit of the direction)."""
        items = np.concatenate([np.concatenate([directions, np.zeros((len(directions), 1))], axis=1),
                                np.concatenate([gradients, np.zeros((len(gradients), 1))], axis=1)])
        return self.run_kernel(items, 4, count=len(directions), gullies=gullies)[:, 0]

    def slope_case(self, count=3000, slope=0.4, seed=3):
        """Directions near (0.6, 0.0, 0.8) and the gradient of ground rising `slope` towards north at each."""
        rng = np.random.default_rng(seed)
        centre = np.array([0.6, 0.0, 0.8])
        east, north = frame(centre)
        offsets = rng.uniform(-20_000.0, 20_000.0, (count, 2))
        points = centre * world.RADIUS + offsets[:, :1] * east + offsets[:, 1:] * north
        directions = points / np.linalg.norm(points, axis=1, keepdims=True)
        gradients = np.tile(north * slope * world.RADIUS, (count, 1))
        return directions, gradients, east, north

    def test_none_on_flat_ground(self):
        directions, gradients, _, _ = self.slope_case(500)
        values = self.gullies_at(directions, gradients * 0.0)
        np.testing.assert_array_equal(values, 0.0)
        values = self.gullies_at(directions, gradients, gullies=(0.0, WAVELENGTH, OCTAVES, FULL))
        np.testing.assert_array_equal(values, 0.0)

    def test_within_their_depth_and_about_level(self):
        directions, gradients, _, _ = self.slope_case()
        values = self.gullies_at(directions, gradients)
        self.assertLessEqual(np.abs(values).max(), DEPTH * sum(0.4 ** i for i in range(int(OCTAVES))) + 1e-3)
        self.assertGreater(values.std(), 0.1 * DEPTH)
        self.assertLess(abs(values.mean()), 0.15 * DEPTH)

    def test_they_run_down_the_slope(self):
        directions, gradients, east, north = self.slope_case(2000)
        step = 120.0
        base = self.gullies_at(directions, gradients, gullies=(DEPTH, WAVELENGTH, 1.0, FULL))
        changes = {}
        for name, way in (("along", north), ("across", east)):
            moved = directions * world.RADIUS + step * way
            moved /= np.linalg.norm(moved, axis=1, keepdims=True)
            changes[name] = np.abs(self.gullies_at(moved, gradients, gullies=(DEPTH, WAVELENGTH, 1.0, FULL)) - base).mean()
        self.assertLess(changes["along"], 0.5 * changes["across"], changes)


if __name__ == "__main__":
    unittest.main()

"""sk.ComputeContext on a device without a window: kernels compiled from
GLSL, their reflected bindings, arrays in and out, push constants, uniform
buffers, buffers kept on the GPU between runs, .npy files from the results,
and the errors for mismatched calls. Needs a Vulkan 1.3 GPU and glslc;
skipped without them. Where this interpreter's shoonyakasha has no
extension (another test put the source tree first on sys.path), the tests
run in a fresh interpreter, which imports the installed package.

    python -m unittest discover -s tests/python
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

import shoonyakasha as sk
from shoonyakasha import shaders

SQUARE = """#version 450
layout(local_size_x = 64) in;
layout(std430, set = 0, binding = 0) readonly buffer Input { float values[]; } src;
layout(std430, set = 0, binding = 1) buffer Output { float values[]; } dst;
layout(push_constant) uniform Push { float scale; uint count; } pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    dst.values[i] = src.values[i] * src.values[i] * pc.scale;
}
"""

# A 2D table from a uniform block: table[y][x] = x * dx + y * dy.
TABLE = """#version 450
layout(local_size_x = 8, local_size_y = 8) in;
layout(std140, set = 0, binding = 0) uniform Params { float dx; float dy; uint width; uint height; } params;
layout(std430, set = 0, binding = 2) writeonly buffer Table { float texels[]; } table;
void main() {
    uvec2 p = gl_GlobalInvocationID.xy;
    if (p.x >= params.width || p.y >= params.height) return;
    table.texels[p.y * params.width + p.x] = float(p.x) * params.dx + float(p.y) * params.dy;
}
"""

ACCUMULATE = """#version 450
layout(local_size_x = 32) in;
layout(std430, binding = 0) buffer Sum { uint values[]; } sum;
void main() { sum.values[gl_GlobalInvocationID.x] += gl_GlobalInvocationID.x; }
"""


@unittest.skipUnless(sk.extension_available(), "runs in a fresh interpreter instead (see InstalledPackage)")
class Compute(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            shaders.find_glslc()
        except shaders.GlslcNotFound as exc:
            raise unittest.SkipTest(str(exc).splitlines()[0])
        try:
            cls.ctx = sk.ComputeContext()
        except RuntimeError as exc:
            raise unittest.SkipTest("no Vulkan device: %s" % exc)
        cls.tmp = Path(tempfile.mkdtemp())

    @classmethod
    def tearDownClass(cls):
        cls.ctx.shutdown()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def kernel(self, name, source):
        path = self.tmp / name
        path.write_text(source, encoding="utf-8")
        return self.ctx.kernel(path)

    def test_reflection(self):
        square = self.kernel("square.comp", SQUARE)
        self.assertEqual(square.local_size, (64, 1, 1))
        self.assertEqual(square.push_constant_size, 8)
        self.assertEqual(square.bindings, [
            {"binding": 0, "type": "storage", "read_only": True, "name": "src"},
            {"binding": 1, "type": "storage", "read_only": False, "name": "dst"},
        ])
        table = self.kernel("table.comp", TABLE)
        self.assertEqual(table.local_size, (8, 8, 1))
        self.assertEqual([(b["binding"], b["type"]) for b in table.bindings], [(0, "uniform"), (2, "storage")])

    def test_arrays_in_and_out(self):
        square = self.kernel("square.comp", SQUARE)
        values = np.linspace(-3, 3, 1000, dtype=np.float32)   # not a multiple of the local size
        result = np.full_like(values, -1.0)
        self.ctx.run(square, [values, result], threads=values.size, push=struct.pack("<fI", 2.0, values.size))
        np.testing.assert_allclose(result, values * values * 2.0, rtol=1e-6)

    def test_short_push_constants_are_zero_filled(self):
        square = self.kernel("square.comp", SQUARE)
        result = np.full(64, 7.0, dtype=np.float32)
        # Only the scale is given: count is 0, so nothing is written.
        self.ctx.run(square, [np.ones(64, np.float32), result], groups=1, push=np.float32(5.0).tobytes())
        np.testing.assert_array_equal(result, 7.0)

    def test_uniform_buffer_and_2d_table_saved_as_npy(self):
        table = self.kernel("table.comp", TABLE)
        width, height = 37, 19
        params = self.ctx.upload(struct.pack("<ffII", 1.0, 100.0, width, height))
        out = self.ctx.buffer(width * height * 4)
        self.ctx.run(table, [params, out], threads=(width, height))
        texels = out.to_numpy(np.float32, (height, width))
        y, x = np.mgrid[0:height, 0:width]
        np.testing.assert_array_equal(texels, (x + 100.0 * y).astype(np.float32))

        path = self.tmp / "table.npy"
        np.save(path, texels)
        np.testing.assert_array_equal(np.load(path), texels)

    def test_buffers_stay_on_the_gpu_between_runs(self):
        accumulate = self.kernel("accumulate.comp", ACCUMULATE)
        sums = self.ctx.buffer(256 * 4)
        np.testing.assert_array_equal(sums.to_numpy(np.uint32), 0)   # new buffers are zero
        for _ in range(3):
            self.ctx.run(accumulate, [sums], threads=256)
        np.testing.assert_array_equal(sums.to_numpy(np.uint32), np.arange(256, dtype=np.uint32) * 3)

    def test_buffer_copies(self):
        data = np.arange(10, dtype=np.int32)
        buf = self.ctx.upload(data)
        self.assertEqual(buf.size, 40)
        buf.upload(np.array([99, 98], dtype=np.int32), offset=8)
        self.assertEqual(buf.to_bytes(8, 8), struct.pack("<ii", 99, 98))
        np.testing.assert_array_equal(buf.to_numpy(np.int32)[:5], [0, 1, 99, 98, 4])
        with self.assertRaises(IndexError):
            buf.upload(b"\0" * 8, offset=36)
        with self.assertRaises(ValueError):
            self.ctx.upload(b"abc").to_numpy(np.float32)   # 3 bytes are no whole float

    def test_mismatched_calls(self):
        square = self.kernel("square.comp", SQUARE)
        a = self.ctx.buffer(64)
        with self.assertRaises(ValueError):
            self.ctx.run(square, [a], groups=1)
        with self.assertRaises(ValueError):
            self.ctx.run(square, [a, a], groups=1, threads=1)
        with self.assertRaises(ValueError):
            self.ctx.run(square, [a, a], groups=1, push=b"\0" * 12)
        other = sk.ComputeContext()
        try:
            with self.assertRaises(ValueError):
                self.ctx.run(square, [a, other.buffer(64)], groups=1)
        finally:
            other.shutdown()

    def test_kernels_that_are_not_buffers_only_are_rejected(self):
        source = """#version 450
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, rgba8) uniform writeonly image2D img;
void main() { imageStore(img, ivec2(0), vec4(1)); }
"""
        with self.assertRaisesRegex(RuntimeError, "only buffers"):
            self.kernel("image.comp", source)

    def test_shutdown_releases_everything(self):
        with sk.ComputeContext() as ctx:
            path = self.tmp / "accumulate.comp"
            path.write_text(ACCUMULATE, encoding="utf-8")
            kernel = ctx.kernel(path)
            buf = ctx.buffer(128)
        self.assertTrue(ctx.is_shutdown)
        self.assertFalse(buf.valid)
        with self.assertRaisesRegex(RuntimeError, "shut down"):
            buf.to_bytes()
        with self.assertRaisesRegex(RuntimeError, "shut down"):
            ctx.run(kernel, [buf], groups=1)


class InstalledPackage(unittest.TestCase):
    def test_in_fresh_interpreter(self):
        if sk.extension_available():
            self.skipTest("the extension is importable here; Compute runs directly")
        here = os.path.dirname(os.path.abspath(__file__))
        result = subprocess.run([sys.executable, os.path.abspath(__file__)], cwd=here, capture_output=True,
                                text=True)
        self.assertEqual(result.returncode, 0, result.stdout[-3000:] + result.stderr[-3000:])


if __name__ == "__main__":
    unittest.main()

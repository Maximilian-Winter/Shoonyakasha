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

    def test_unsupported_descriptor_types_are_rejected(self):
        source = """#version 450
#extension GL_EXT_samplerless_texture_functions : require
layout(local_size_x = 1) in;
layout(set = 0, binding = 0) uniform texture2D tex;
layout(std430, binding = 1) buffer Out { vec4 v; } outv;
void main() { outv.v = texelFetch(tex, ivec2(0), 0); }
"""
        with self.assertRaisesRegex(RuntimeError, "not a storage buffer, uniform buffer, storage image or sampler"):
            self.kernel("texture.comp", source)

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


WRITE_2D = """#version 450
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0, rgba32f) uniform writeonly image2D target;
void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(p, imageSize(target)))) return;
    imageStore(target, p, vec4(p.x, p.y, p.x * p.y, 1.0));
}
"""

WRITE_3D = """#version 450
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
layout(binding = 0, r32f) uniform writeonly image3D volume;
void main() {
    ivec3 p = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(p, imageSize(volume)))) return;
    imageStore(volume, p, vec4(p.z * 100 + p.y * 10 + p.x));
}
"""

# Doubles each layer of an array in place: read and written through one binding.
DOUBLE_LAYERS = """#version 450
layout(local_size_x = 8, local_size_y = 8) in;
layout(binding = 0, r32f) uniform image2DArray layers;
void main() {
    ivec3 p = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(p, imageSize(layers)))) return;
    imageStore(layers, p, imageLoad(layers, p) * 2.0);
}
"""

# Samples a texture at the given coordinates.
SAMPLE_2D = """#version 450
layout(local_size_x = 64) in;
layout(binding = 0) uniform sampler2D tex;
layout(std430, binding = 1) readonly buffer Coords { vec2 uv[]; } coords;
layout(std430, binding = 2) writeonly buffer Out { vec4 values[]; } outv;
layout(push_constant) uniform Push { uint count; } pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    outv.values[i] = textureLod(tex, coords.uv[i], 0.0);
}
"""

SAMPLE_CUBE = """#version 450
layout(local_size_x = 6) in;
layout(binding = 0) uniform samplerCube sky;
layout(std430, binding = 1) writeonly buffer Out { float values[]; } outv;
const vec3 dirs[6] = vec3[](vec3(1,0,0), vec3(-1,0,0), vec3(0,1,0), vec3(0,-1,0), vec3(0,0,1), vec3(0,0,-1));
void main() { outv.values[gl_LocalInvocationID.x] = textureLod(sky, dirs[gl_LocalInvocationID.x], 0.0).r; }
"""


@unittest.skipUnless(sk.extension_available(), "runs in a fresh interpreter instead (see InstalledPackage)")
class ComputeImages(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        Compute.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        Compute.tearDownClass.__func__(cls)

    kernel = Compute.kernel

    def test_reflection(self):
        write = self.kernel("write2d.comp", WRITE_2D)
        self.assertEqual(write.bindings, [{"binding": 0, "type": "storage_image", "read_only": False,
                                           "name": "target", "dim": "2d", "arrayed": False,
                                           "format": "R32G32B32A32_SFLOAT"}])
        cube = self.kernel("cube.comp", SAMPLE_CUBE)
        self.assertEqual({k: cube.bindings[0][k] for k in ("type", "dim", "arrayed", "format", "read_only")},
                         {"type": "sampled_image", "dim": "cube", "arrayed": False, "format": None,
                          "read_only": True})
        layers = self.kernel("layers.comp", DOUBLE_LAYERS)
        self.assertEqual((layers.bindings[0]["dim"], layers.bindings[0]["arrayed"]), ("2d", True))

    def test_storage_image_2d(self):
        write = self.kernel("write2d.comp", WRITE_2D)
        width, height = 21, 13
        img = self.ctx.image(width, height, "R32G32B32A32_SFLOAT")
        self.assertEqual((img.shape, img.texel_size, img.nbytes), ((height, width, 4), 16, width * height * 16))
        self.ctx.run(write, [img], threads=(width, height))
        texels = img.to_numpy()
        y, x = np.mgrid[0:height, 0:width].astype(np.float32)
        np.testing.assert_array_equal(texels, np.stack([x, y, x * y, np.ones_like(x)], axis=-1))
        path = self.tmp / "image.npy"
        np.save(path, texels)
        np.testing.assert_array_equal(np.load(path), texels)

    def test_storage_image_3d(self):
        write = self.kernel("write3d.comp", WRITE_3D)
        vol = self.ctx.image(5, 6, "R32_SFLOAT", depth=7)
        self.ctx.run(write, [vol], threads=(5, 6, 7))
        z, y, x = np.mgrid[0:7, 0:6, 0:5]
        np.testing.assert_array_equal(vol.to_numpy(), (z * 100 + y * 10 + x).astype(np.float32))

    def test_image_array_read_and_written(self):
        double = self.kernel("layers.comp", DOUBLE_LAYERS)
        data = np.random.default_rng(1).random((3, 9, 10), dtype=np.float32)
        img = self.ctx.upload_image(data, kind="array")
        self.assertEqual((img.format, img.layers, img.width, img.height), ("R32_SFLOAT", 3, 10, 9))
        self.ctx.run(double, [img], threads=(10, 9, 3))
        np.testing.assert_array_equal(img.to_numpy(), data * 2)

    def test_sampled_image_filters_linearly(self):
        sample = self.kernel("sample2d.comp", SAMPLE_2D)
        texels = np.array([[[0, 0, 0, 255], [255, 0, 0, 255]]], dtype=np.uint8)   # 2x1, uint8 -> R8G8B8A8_UNORM
        tex = self.ctx.upload_image(texels)
        self.assertEqual(tex.format, "R8G8B8A8_UNORM")
        np.testing.assert_array_equal(tex.to_numpy(), texels)
        coords = np.array([[0.25, 0.5], [0.75, 0.5], [0.5, 0.5], [-1.0, 0.5]], dtype=np.float32)
        out = np.zeros((4, 4), dtype=np.float32)
        self.ctx.run(sample, [tex, coords, out], threads=4, push=struct.pack("<I", 4))
        np.testing.assert_allclose(out[:, 0], [0.0, 1.0, 0.5, 0.0], atol=1 / 255)   # centres, midway, clamped
        np.testing.assert_allclose(out[:, 3], 1.0)

    def test_cube_from_six_layers(self):
        sample = self.kernel("cube.comp", SAMPLE_CUBE)
        faces = np.repeat(np.arange(1, 7, dtype=np.float32), 4 * 4).reshape(6, 4, 4)
        sky = self.ctx.upload_image(faces, kind="array")
        out = np.zeros(6, dtype=np.float32)
        self.ctx.run(sample, [sky, out], groups=1)
        np.testing.assert_array_equal(out, [1, 2, 3, 4, 5, 6])   # +X, -X, +Y, -Y, +Z, -Z

    def test_mismatches(self):
        write2d = self.kernel("write2d.comp", WRITE_2D)
        with self.assertRaisesRegex(ValueError, "declared R32G32B32A32_SFLOAT"):
            self.ctx.run(write2d, [self.ctx.image(4, 4, "R32_SFLOAT")], groups=1)
        with self.assertRaisesRegex(ValueError, "2D image"):
            self.ctx.run(write2d, [self.ctx.image(4, 4, "R32G32B32A32_SFLOAT", depth=2)], groups=1)
        with self.assertRaisesRegex(TypeError, "give a GpuImage"):
            self.ctx.run(write2d, [self.ctx.buffer(16)], groups=1)
        with self.assertRaisesRegex(TypeError, "give a GpuImage"):
            self.ctx.run(write2d, [np.zeros((4, 4, 4), np.float32)], groups=1)
        accumulate = self.kernel("accumulate.comp", ACCUMULATE)
        with self.assertRaisesRegex(ValueError, "is a buffer, an image was given"):
            self.ctx.run(accumulate, [self.ctx.image(4, 4, "R32_SFLOAT")], groups=1)
        sample = self.kernel("cube.comp", SAMPLE_CUBE)
        with self.assertRaisesRegex(ValueError, "cube"):
            self.ctx.run(sample, [self.ctx.image(4, 8, "R32_SFLOAT", layers=6), np.zeros(6, np.float32)], groups=1)

    def test_bad_images(self):
        with self.assertRaises(ValueError):
            self.ctx.image(4, 4, "NOT_A_FORMAT")
        with self.assertRaises(ValueError):
            self.ctx.image(4, 4, "D32_SFLOAT")
        with self.assertRaises(ValueError):
            self.ctx.image(4, 4, depth=2, layers=2)
        img = self.ctx.image(4, 4, "R32_SFLOAT")
        with self.assertRaises(ValueError):
            img.upload(np.zeros(15, np.float32))
        with self.assertRaises(ValueError):
            self.ctx.upload_image(np.zeros((4, 4), np.float32), format="R32G32B32A32_SFLOAT")

    def test_format_layouts(self):
        layout = sk._shoonyakasha._format_layout
        self.assertEqual(layout("R32G32B32A32_SFLOAT"), ("<f4", 4))
        self.assertEqual(layout("R16G16_SFLOAT"), ("<f2", 2))
        self.assertEqual(layout("R8G8B8A8_SRGB"), ("|u1", 4))
        self.assertEqual(layout("R32_SINT"), ("<i4", 1))
        self.assertEqual(layout("B10G11R11_UFLOAT_PACK32"), ("<u4", 1))
        self.assertIsNone(layout("D24_UNORM_S8_UINT"))


FILL_LEVEL = """#version 450
layout(local_size_x = 8, local_size_y = 8) in;
layout(binding = 0, r32f) uniform writeonly image2D level;
layout(push_constant) uniform Push { float value; } pc;
void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(p, imageSize(level)))) return;
    imageStore(level, p, vec4(pc.value));
}
"""

SAMPLE_LODS = """#version 450
layout(local_size_x = 8) in;
layout(binding = 0) uniform sampler2D tex;
layout(std430, binding = 1) writeonly buffer Out { float values[]; } outv;
void main() { outv.values[gl_LocalInvocationID.x] = textureLod(tex, vec2(0.5), float(gl_LocalInvocationID.x) * 0.5).r; }
"""


def srgb_to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


@unittest.skipUnless(sk.extension_available(), "runs in a fresh interpreter instead (see InstalledPackage)")
class ComputeMipsAndFiles(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        Compute.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        Compute.tearDownClass.__func__(cls)

    kernel = Compute.kernel

    def test_generated_mips_average_level_zero(self):
        data = np.random.default_rng(2).random((8, 8), dtype=np.float32)
        img = self.ctx.upload_image(data, mips=0)
        self.assertEqual(img.mips, 4)
        self.assertEqual([img.level_shape(m) for m in range(4)], [(8, 8), (4, 4), (2, 2), (1, 1)])
        np.testing.assert_array_equal(img.to_numpy(0), data)
        expected = data
        for mip in range(1, 4):
            expected = expected.reshape(expected.shape[0] // 2, 2, expected.shape[1] // 2, 2).mean(axis=(1, 3))
            np.testing.assert_allclose(img.to_numpy(mip), expected, rtol=1e-5, atol=1e-6, err_msg="mip %d" % mip)

    def test_3d_mip_shapes(self):
        img = self.ctx.image(8, 4, "R16G16B16A16_SFLOAT", depth=2, mips=0)
        self.assertEqual(img.mips, 4)
        self.assertEqual(img.level_shape(1), (1, 2, 4, 4))
        self.assertEqual(img.level_shape(3), (1, 1, 1, 4))
        self.assertEqual(img.level_nbytes(1), 1 * 2 * 4 * 8)
        with self.assertRaises(ValueError):
            self.ctx.image(8, 8, mips=5)

    def test_each_level_written_then_sampled_by_lod(self):
        fill = self.kernel("fill.comp", FILL_LEVEL)
        img = self.ctx.image(8, 8, "R32_SFLOAT", mips=0)
        for mip in range(img.mips):
            size = img.level_shape(mip)
            self.ctx.run(fill, [img.level(mip)], threads=(size[1], size[0]), push=np.float32(10 * (mip + 1)).tobytes())
        for mip in range(img.mips):
            np.testing.assert_array_equal(img.to_numpy(mip), 10 * (mip + 1))
        sample = self.kernel("lods.comp", SAMPLE_LODS)
        out = np.zeros(8, dtype=np.float32)
        self.ctx.run(sample, [img, out], groups=1)          # all levels: lod 0, 0.5, 1, ... 3.5
        if self.linear_filtering(img):
            np.testing.assert_allclose(out, [10, 15, 20, 25, 30, 35, 40, 40], rtol=1e-5)
        else:
            self.assertEqual(out[0], 10)
            self.assertEqual(out[6], 40)
        self.ctx.run(sample, [img.level(2), out], groups=1)  # only level 2, whatever the lod
        np.testing.assert_array_equal(out, 30)
        with self.assertRaises(IndexError):
            img.level(4)

    def linear_filtering(self, img):
        """Whether the device filters R32_SFLOAT linearly, judged by sampling midway between two texels."""
        tex = self.ctx.upload_image(np.array([[0.0, 1.0]], dtype=np.float32))
        sample = self.kernel("sample2d.comp", SAMPLE_2D)
        out = np.zeros((1, 4), dtype=np.float32)
        self.ctx.run(sample, [tex, np.array([[0.5, 0.5]], np.float32), out], threads=1, push=struct.pack("<I", 1))
        return abs(out[0, 0] - 0.5) < 1e-3

    def test_save_image_files(self):
        rgba = np.random.default_rng(3).random((5, 7, 4), dtype=np.float32) * 4
        img = self.ctx.upload_image(rgba)
        img.save(self.tmp / "a.npy")
        np.testing.assert_array_equal(np.load(self.tmp / "a.npy"), rgba)
        for half, size in ((True, 2), (False, 4)):
            path = self.tmp / ("a_%d.exr" % size)
            img.save(path, half=half)
            data = path.read_bytes()
            self.assertEqual(data[:4], b"\x76\x2f\x31\x01")
            self.assertTrue(data.endswith(self.exr_last_row(rgba, size)), "last scanline of %s" % path.name)
        img.save(self.tmp / "a.hdr")
        self.assertTrue((self.tmp / "a.hdr").read_bytes().startswith(b"#?RADIANCE"))

    @staticmethod
    def exr_last_row(rgba, size):
        """The values of the last scanline as saveExr stores them: channels A, B, G, R in turn."""
        row = rgba[-1]
        dtype = "<f2" if size == 2 else "<f4"
        return b"".join(row[:, c].astype(dtype).tobytes() for c in (3, 2, 1, 0))

    def test_save_layers_slices_and_mips(self):
        layers = np.stack([np.full((4, 4), v, np.float32) for v in (1, 2, 3)])
        img = self.ctx.upload_image(layers, kind="array", mips=0)
        img.save(self.tmp / "l.npy", layer=2)
        np.testing.assert_array_equal(np.load(self.tmp / "l.npy"), np.full((4, 4, 1), 3, np.float32))
        img.save(self.tmp / "m.npy", mip=2, layer=1)
        np.testing.assert_allclose(np.load(self.tmp / "m.npy"), np.full((1, 1, 1), 2, np.float32))
        with self.assertRaises(ValueError):
            img.save(self.tmp / "x.npy", layer=3)
        with self.assertRaises(ValueError):
            img.save(self.tmp / "x.png")
        with self.assertRaises(ValueError):
            self.ctx.image(2, 2, "R32_UINT").save(self.tmp / "u.exr")

    def test_srgb_is_decoded(self):
        texels = np.array([[[0, 64, 128, 255], [255, 200, 30, 128]]], dtype=np.uint8)
        img = self.ctx.upload_image(texels, format="R8G8B8A8_SRGB")
        img.save(self.tmp / "s.npy")
        saved = np.load(self.tmp / "s.npy")
        c = texels / 255.0
        np.testing.assert_allclose(saved[..., :3], srgb_to_linear(c[..., :3]), atol=1e-6)
        np.testing.assert_allclose(saved[..., 3], c[..., 3], atol=1e-6)

    def test_save_arrays(self):
        grey = np.linspace(0, 8, 12, dtype=np.float32).reshape(3, 4)
        sk.save_exr(self.tmp / "g.exr", grey, half=False)
        self.assertTrue((self.tmp / "g.exr").read_bytes().endswith(grey[-1].astype("<f4").tobytes()))
        sk.save_hdr(self.tmp / "g.hdr", grey)
        self.assertIn(b"-Y 3 +X 4\n", (self.tmp / "g.hdr").read_bytes())
        with self.assertRaises(ValueError):
            sk.save_exr(self.tmp / "bad.exr", np.zeros((2, 2, 5), np.float32))


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

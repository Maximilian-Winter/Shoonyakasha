"""
The split-sum table of the GGX specular reflection (brdf_lut.comp), for
the planet pipeline's "brdfLut" external image: SIZE x SIZE texels, a
cosine between the view and the normal across and a roughness down, each
the scale and bias of the Fresnel reflectance at normal incidence.

`table(ctx=None)` gives it as float32 (SIZE, SIZE, 2), computed on the GPU
once and then read from CACHE (named by a hash of the kernel and SIZE).
`ctx` is a ComputeContext (the engine's, `engine.compute`); without one a
context of its own is made and shut down. `rgba16(table)` gives it as
float16 RGBA texels (scale, bias, 0, 1) for R16G16B16A16_SFLOAT.
"""

import os
import zlib
from pathlib import Path

import numpy as np

SIZE = 32
KERNEL = Path(__file__).resolve().parent.parent / "pipelines" / "planet" / "shaders" / "brdf_lut.comp.spv"


def _cache_directory():
    base = os.environ.get("LOCALAPPDATA") if os.name == "nt" else None
    base = base or os.environ.get("XDG_CACHE_HOME") or os.path.join(os.path.expanduser("~"), ".cache")
    return os.path.join(base, "shoonyakasha", "brdf_lut")


CACHE = _cache_directory()


def _compute(ctx):
    import shoonyakasha as sk
    own = ctx is None
    if own:
        ctx = sk.ComputeContext()
    try:
        kernel = ctx.kernel(KERNEL)
        out = np.zeros((SIZE * SIZE, 2), np.float32)
        ctx.run(kernel, [out], threads=(SIZE, SIZE), push=np.array([SIZE], np.uint32).tobytes())
        return out.reshape(SIZE, SIZE, 2)
    finally:
        if own:
            ctx.shutdown()


def table(ctx=None):
    key = "%08x" % zlib.crc32(KERNEL.read_bytes(), zlib.crc32(np.array([SIZE], np.int32).tobytes()))
    path = os.path.join(CACHE, key + ".npy")
    if os.path.exists(path):
        return np.load(path, allow_pickle=False)
    result = _compute(ctx)
    os.makedirs(CACHE, exist_ok=True)
    np.save(path, result)
    return result


def rgba16(values):
    out = np.zeros(values.shape[:2] + (4,), np.float16)
    out[..., :2] = values
    out[..., 3] = 1.0
    return np.ascontiguousarray(out)

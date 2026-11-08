"""
The trees' octahedral impostors (impostor_oct.glsl): each near tree kind
drawn from VIEWS x VIEWS directions over the upper hemisphere, TEXELS
texels square each, into a block of BLOCK texels of an atlas COLUMNS
blocks across, on the GPU (impostor_bake.comp).

`sphere(vertices)` gives a tree's bounding sphere in its units: (the
centre's height, the radius), the centre on its axis halfway up its
points, the radius reaching its furthest point with RADIUS_MARGIN more.

`bake(trees, atlas, bark, ctx=None)` takes the trees' near meshes
(vegetation.trees()), the foliage atlas and the bark tile
(vegetation_textures) and gives (albedo, normals, rects, spheres):

    albedo   uint8 RGBA (sRGB): rgb the hits' colour (the foliage card's
             or the bark's, times its shade), a the share of rays that hit
    normals  uint8 RGBA: rgb the hits' normal in the tree's frame (x right,
             y up, z ahead) as n * 0.5 + 0.5; a 255 where most hits were
             foliage (tinted by the plant's colour), else 0
    rects    each tree's block (u0, v0, u1, v1) in the atlas, v down
    spheres  each tree's sphere

A view's texels nothing covers take the mean colour of those something
covers and an upward foliage normal, so mip levels blend towards them.
`ctx` is a ComputeContext (the engine's, `engine.compute`); without one a
context of its own is made and shut down. `cached_bake(trees, atlas,
bark, ctx=None)` keeps each bake in CACHE (CACHE_KEEP of them, the
newest), named by a hash of its inputs, and gives it back from there.
"""

import os
import zlib
from pathlib import Path

import numpy as np

from .vegetation_textures import _linear_to_srgb

VIEWS = 6
TEXELS = 96
BLOCK = VIEWS * TEXELS
COLUMNS = 5
RADIUS_MARGIN = 1.02
KERNEL = Path(__file__).resolve().parent.parent / "pipelines" / "planet" / "shaders" / "impostor_bake.comp.spv"
CACHE_KEEP = 4


def _cache_directory():
    base = os.environ.get("LOCALAPPDATA") if os.name == "nt" else None
    base = base or os.environ.get("XDG_CACHE_HOME") or os.path.join(os.path.expanduser("~"), ".cache")
    return os.path.join(base, "shoonyakasha", "impostors")


CACHE = _cache_directory()


def sphere(vertices):
    y = vertices[:, 1].astype(np.float64)
    centre = 0.5 * (y.min() + y.max())
    reach = np.sqrt(vertices[:, 0].astype(np.float64) ** 2 + (y - centre) ** 2 + vertices[:, 2].astype(np.float64) ** 2)
    return float(centre), float(reach.max() * RADIUS_MARGIN)


def layout(names):
    """Each name's block (u0, v0, u1, v1) and the atlas's (width, height) in texels."""
    rows = (len(names) + COLUMNS - 1) // COLUMNS
    width, height = COLUMNS * BLOCK, rows * BLOCK
    rects = {}
    for k, name in enumerate(names):
        y, x = divmod(k, COLUMNS)
        rects[name] = (x * BLOCK / width, y * BLOCK / height, (x + 1) * BLOCK / width, (y + 1) * BLOCK / height)
    return rects, (width, height)


def _fill_empty(colour, coverage, normal, foliage):
    """Each view's uncovered texels: the covered ones' mean colour and an upward foliage normal."""
    for vy in range(VIEWS):
        for vx in range(VIEWS):
            cell = (slice(vy * TEXELS, (vy + 1) * TEXELS), slice(vx * TEXELS, (vx + 1) * TEXELS))
            empty = coverage[cell] <= 0.0
            covered = ~empty
            mean = colour[cell][covered].mean(axis=0) if covered.any() else np.full(3, 0.2)
            colour[cell][empty] = mean
            normal[cell][empty] = (0.0, 1.0, 0.0)
            foliage[cell][empty] = 1.0


def bake(trees, atlas, bark, ctx=None):
    import shoonyakasha as sk
    own = ctx is None
    if own:
        ctx = sk.ComputeContext()
    try:
        kernel = ctx.kernel(KERNEL)
        atlas_image = ctx.upload_image(np.ascontiguousarray(atlas, np.uint8))
        bark_image = ctx.upload_image(np.ascontiguousarray(bark, np.uint8))
        names = list(trees)
        rects, (width, height) = layout(names)
        albedo = np.zeros((height, width, 4), np.uint8)
        normals = np.zeros((height, width, 4), np.uint8)
        spheres = {}
        for k, name in enumerate(names):
            vertices, indices = trees[name]
            centre, radius = sphere(vertices)
            spheres[name] = (centre, radius)
            colour_out = np.zeros((BLOCK * BLOCK, 4), np.float32)
            normal_out = np.zeros((BLOCK * BLOCK, 4), np.float32)
            push = np.array([0, len(indices)], np.uint32).tobytes() + np.array([centre, radius], np.float32).tobytes()
            ctx.run(kernel, [np.ascontiguousarray(vertices, np.float32), np.ascontiguousarray(indices, np.uint32),
                             atlas_image, bark_image, colour_out, normal_out],
                    threads=(BLOCK, BLOCK), push=push)
            colour = colour_out[:, :3].reshape(BLOCK, BLOCK, 3).astype(np.float64)
            coverage = colour_out[:, 3].reshape(BLOCK, BLOCK).astype(np.float64)
            normal = normal_out[:, :3].reshape(BLOCK, BLOCK, 3).astype(np.float64)
            foliage = normal_out[:, 3].reshape(BLOCK, BLOCK).astype(np.float64)
            _fill_empty(colour, coverage, normal, foliage)
            y, x = divmod(k, COLUMNS)
            block = (slice(y * BLOCK, (y + 1) * BLOCK), slice(x * BLOCK, (x + 1) * BLOCK))
            albedo[block] = np.clip(np.concatenate([_linear_to_srgb(np.clip(colour, 0.0, 1.0)), coverage[..., None]],
                                                   -1) * 255.0 + 0.5, 0, 255)
            normals[block] = np.clip(np.concatenate([normal * 0.5 + 0.5, foliage[..., None]], -1) * 255.0 + 0.5, 0, 255)
        return albedo, normals, rects, spheres
    finally:
        if own:
            ctx.shutdown()


def _key(trees, atlas, bark):
    h = zlib.crc32(np.array([VIEWS, TEXELS, COLUMNS], np.int32).tobytes())
    h = zlib.crc32(KERNEL.read_bytes(), h)
    for name, (vertices, indices) in trees.items():
        h = zlib.crc32(name.encode(), h)
        h = zlib.crc32(np.ascontiguousarray(vertices).tobytes(), h)
        h = zlib.crc32(np.ascontiguousarray(indices).tobytes(), h)
    h = zlib.crc32(np.ascontiguousarray(atlas).tobytes(), h)
    return "%08x%08x" % (zlib.crc32(np.ascontiguousarray(bark).tobytes(), h), h)


def cached_bake(trees, atlas, bark, ctx=None):
    path = os.path.join(CACHE, _key(trees, atlas, bark) + ".npz")
    if os.path.exists(path):
        with np.load(path, allow_pickle=False) as data:
            names = [str(n) for n in data["names"]]
            rects = {n: tuple(r) for n, r in zip(names, data["rects"].tolist())}
            spheres = {n: tuple(s) for n, s in zip(names, data["spheres"].tolist())}
            return data["albedo"], data["normals"], rects, spheres
    albedo, normals, rects, spheres = bake(trees, atlas, bark, ctx)
    os.makedirs(CACHE, exist_ok=True)
    names = list(rects)
    np.savez_compressed(path, albedo=albedo, normals=normals, names=np.array(names),
                        rects=np.array([rects[n] for n in names]), spheres=np.array([spheres[n] for n in names]))
    kept = sorted((os.path.join(CACHE, f) for f in os.listdir(CACHE) if f.endswith(".npz")), key=os.path.getmtime)
    for old in kept[:-CACHE_KEEP]:
        os.remove(old)
    return albedo, normals, rects, spheres

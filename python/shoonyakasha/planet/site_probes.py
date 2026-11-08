"""
The sites' sky visibility (site_probes.comp): for a site's static
triangles in its frame (x east, y up, z north, metres; y = 0 the ground at
its centre), a grid of COUNTS probes from GRID_MIN, SPACING apart, each the
share of the sky it sees in each direction as L1 spherical harmonics
(hits on the site's triangles below GROUND_LEVEL of site_probes.comp, on
the ground like a pad, leave a direction seen). The
planet pipeline's models and terrain take their sky light times it
(site_probes.glsl).

`bake(triangles, ctx=None)` takes the triangles as (N, 3, 3) and gives the
grid as float32 (COUNTS y * COUNTS z, COUNTS x, 4): row y * COUNTS z + z,
column x, each probe's (L00, L1 x, y, z), zeros for a probe inside the
geometry. `ctx` is a ComputeContext (the engine's, `engine.compute`);
without one a context of its own is made and shut down. `cached_bake(
triangles, ctx=None)` keeps each bake in CACHE (CACHE_KEEP of them, the
newest), named by a hash of the triangles and the grid, and gives it back
from there. `box_triangles(centre, size)` gives an axis-aligned box's 12
triangles in the site's frame, outward.
"""

import os
import zlib
from pathlib import Path

import numpy as np

COUNTS = (32, 12, 32)
GRID_MIN = (-64.0, 0.0, -64.0)
SPACING = (4.0, 3.2, 4.0)
CACHE_KEEP = 16
KERNEL = Path(__file__).resolve().parent.parent / "pipelines" / "planet" / "shaders" / "site_probes.comp.spv"


def _cache_directory():
    base = os.environ.get("LOCALAPPDATA") if os.name == "nt" else None
    base = base or os.environ.get("XDG_CACHE_HOME") or os.path.join(os.path.expanduser("~"), ".cache")
    return os.path.join(base, "shoonyakasha", "site_probes")


CACHE = _cache_directory()


def _push(count):
    return (np.array(COUNTS + (count,), np.uint32).tobytes() + np.array(GRID_MIN + (0.0,), np.float32).tobytes()
            + np.array(SPACING + (0.0,), np.float32).tobytes())


def bake(triangles, ctx=None):
    import shoonyakasha as sk
    tris = np.asarray(triangles, np.float32).reshape(-1, 3, 3)
    count = len(tris)
    corners = np.zeros((max(count, 1) * 3, 4), np.float32)
    corners[:count * 3, :3] = tris.reshape(-1, 3)
    normals = np.zeros((max(count, 1), 4), np.float32)
    if count:
        cross = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
        normals[:count, :3] = cross / np.maximum(np.linalg.norm(cross, axis=1, keepdims=True), 1e-12)
    nx, ny, nz = COUNTS
    out = np.zeros((ny * nz * nx, 4), np.float32)
    own = ctx is None
    if own:
        ctx = sk.ComputeContext()
    try:
        kernel = ctx.kernel(KERNEL)
        ctx.run(kernel, [corners, normals, out], threads=(nx, ny * nz), push=_push(count))
    finally:
        if own:
            ctx.shutdown()
    return out.reshape(ny * nz, nx, 4)


def _key(triangles):
    h = zlib.crc32(np.array(COUNTS, np.int32).tobytes() + np.array(GRID_MIN + SPACING, np.float32).tobytes())
    h = zlib.crc32(KERNEL.read_bytes(), h)
    data = np.ascontiguousarray(np.asarray(triangles, np.float32)).tobytes()
    return "%08x%08x" % (zlib.crc32(data, h), h)


def cached_bake(triangles, ctx=None):
    path = os.path.join(CACHE, _key(triangles) + ".npy")
    if os.path.exists(path):
        return np.load(path, allow_pickle=False)
    grid = bake(triangles, ctx)
    os.makedirs(CACHE, exist_ok=True)
    np.save(path, grid)
    kept = sorted((os.path.join(CACHE, f) for f in os.listdir(CACHE) if f.endswith(".npy")), key=os.path.getmtime)
    for old in kept[:-CACHE_KEEP]:
        os.remove(old)
    return grid


def box_triangles(centre, size):
    c, h = np.asarray(centre, np.float32), 0.5 * np.asarray(size, np.float32)
    corners = np.array([[x, y, z] for x in (-1, 1) for y in (-1, 1) for z in (-1, 1)], np.float32) * h + c
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    out = []
    for a, b, d, e in faces:
        quad = corners[[a, b, d, e]]
        for tri in (quad[[0, 1, 2]], quad[[0, 2, 3]]):
            if np.dot(np.cross(tri[1] - tri[0], tri[2] - tri[0]), tri.mean(axis=0) - c) < 0.0:
                tri = tri[::-1]
            out.append(tri)
    return np.array(out, np.float32)

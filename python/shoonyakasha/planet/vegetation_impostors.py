"""
The trees' impostors: each tree of vegetation.TREES drawn from the side
(looking along -z, orthographically) into a cell of an atlas, for the far
kinds' camera-facing quads.

`bake(trees, atlas, bark)` takes the trees' near meshes (vegetation.trees()),
the foliage atlas and the bark tile (vegetation_textures) and gives
(albedo, normals, rects):

    albedo   uint8 RGBA (sRGB): rgb the foliage's card colour times its
             shade, or the bark's colour times its shade; a its coverage
    normals  uint8 RGBA: rgb the surface's normal (x right, y up, z towards
             the viewer) as n * 0.5 + 0.5; a 0 where the albedo is mostly
             bark, else 255: foliage, tinted by the plant's colour (texels
             nothing covers are foliage, so mip levels blend towards it)
    rects    each tree's (u0, v0, u1, v1) in the atlas, v down

A cell is CELL texels square and spans the tree's width
(vegetation.impostor_width) across and its unit height up. Each is drawn at SUPERSAMPLE times the cell's size with
a depth buffer, a card's texel kept where the atlas's alpha is at least
1/2, then averaged down; texels nothing covers take the cell's mean colour.
"""

import numpy as np

from . import vegetation
from .vegetation_textures import _linear_to_srgb, _srgb_to_linear

CELL = 256
COLUMNS = 4
SUPERSAMPLE = 2


def _sample(image, u, v):
    """Nearest texels of `image` (H x W x C float) at (u, v) arrays, wrapping."""
    height, width = image.shape[:2]
    x = np.mod(np.floor(u * width).astype(np.int64), width)
    y = np.mod(np.floor(v * height).astype(np.int64), height)
    return image[y, x]


def _draw(vertices, indices, width_units, size, atlas, bark):
    """One tree drawn at `size` texels square: (colour (linear), alpha,
    normal, foliage) arrays."""
    colour = np.zeros((size, size, 3))
    normal = np.zeros((size, size, 3))
    alpha = np.zeros((size, size))
    foliage = np.zeros((size, size))
    depth = np.full((size, size), -np.inf)
    px = (vertices[:, 0] / width_units + 0.5) * size
    py = (1.0 - vertices[:, 1]) * size
    pz = vertices[:, 2]
    for a, b, c in indices.reshape(-1, 3):
        xs, ys = px[[a, b, c]], py[[a, b, c]]
        x0, x1 = max(int(np.floor(xs.min())), 0), min(int(np.ceil(xs.max())), size - 1)
        y0, y1 = max(int(np.floor(ys.min())), 0), min(int(np.ceil(ys.max())), size - 1)
        if x0 > x1 or y0 > y1:
            continue
        area = (xs[1] - xs[0]) * (ys[2] - ys[0]) - (xs[2] - xs[0]) * (ys[1] - ys[0])
        if abs(area) < 1e-9:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        w1 = ((gx - xs[0]) * (ys[2] - ys[0]) - (xs[2] - xs[0]) * (gy - ys[0])) / area
        w2 = ((xs[1] - xs[0]) * (gy - ys[0]) - (gx - xs[0]) * (ys[1] - ys[0])) / area
        w0 = 1.0 - w1 - w2
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not inside.any():
            continue
        w = np.stack([w0, w1, w2], -1)[inside]
        tri = vertices[[a, b, c]]
        z = w @ pz[[a, b, c]]
        uv = w @ tri[:, [3, 11]]
        n = w @ tri[:, 4:7]
        shade = w @ tri[:, 8]
        part = tri[0, 7]
        if part == vegetation.PART_BARK:
            texel = _sample(bark, uv[:, 0], uv[:, 1])
            keep = np.ones(len(z), bool)
        else:
            texel = _sample(atlas, uv[:, 0], uv[:, 1])
            keep = texel[:, 3] >= 0.5
        yy, xx = np.nonzero(inside)
        yy, xx = yy + y0, xx + x0
        nearer = keep & (z > depth[yy, xx])
        yy, xx = yy[nearer], xx[nearer]
        depth[yy, xx] = z[nearer]
        colour[yy, xx] = texel[nearer, :3] * shade[nearer, None]
        n = n[nearer]
        normal[yy, xx] = n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-6)
        alpha[yy, xx] = 1.0
        foliage[yy, xx] = 0.0 if part == vegetation.PART_BARK else 1.0
    return colour, alpha, normal, foliage


def _down(image, factor):
    h, w = image.shape[:2]
    return image.reshape(h // factor, factor, w // factor, factor, *image.shape[2:]).mean(axis=(1, 3))


def bake(trees, atlas, bark):
    """(albedo, normals, rects) for the trees (name: (vertices, indices))."""
    atlas_linear = np.concatenate([_srgb_to_linear(atlas[..., :3] / 255.0), atlas[..., 3:] / 255.0], -1)
    bark_linear = np.concatenate([_srgb_to_linear(bark[..., :3] / 255.0), bark[..., 3:] / 255.0], -1)
    names = list(trees)
    rows = (len(names) + COLUMNS - 1) // COLUMNS
    albedo = np.zeros((rows * CELL, COLUMNS * CELL, 4), np.uint8)
    normals = np.zeros((rows * CELL, COLUMNS * CELL, 4), np.uint8)
    rects = {}
    big = CELL * SUPERSAMPLE
    for index, name in enumerate(names):
        vertices, indices = trees[name]
        colour, alpha, normal, foliage = _draw(vertices, indices, vegetation.impostor_width(vertices), big,
                                               atlas_linear, bark_linear)
        coverage = _down(alpha, SUPERSAMPLE)
        weight = np.maximum(coverage, 1e-6)[..., None]
        colour = _down(colour * alpha[..., None], SUPERSAMPLE) / weight
        normal = _down(normal * alpha[..., None], SUPERSAMPLE)
        normal = normal / np.maximum(np.linalg.norm(normal, axis=-1, keepdims=True), 1e-6)
        bare = _down((1.0 - foliage) * alpha, SUPERSAMPLE)      # the share of each texel that is bark
        leafy = 1.0 - bare / weight[..., 0]
        covered = coverage > 0.0
        leafy = np.where(covered, leafy, 1.0)
        mean = colour[covered].mean(axis=0) if covered.any() else np.full(3, 0.5)
        colour = np.where(covered[..., None], colour, mean)
        normal = np.where(covered[..., None], normal, np.array([0.0, 0.0, 1.0]))
        y, x = divmod(index, COLUMNS)
        cell = (slice(y * CELL, (y + 1) * CELL), slice(x * CELL, (x + 1) * CELL))
        albedo[cell] = np.clip(np.concatenate([_linear_to_srgb(colour), coverage[..., None]], -1) * 255 + 0.5, 0, 255)
        normals[cell] = np.clip(np.concatenate([normal * 0.5 + 0.5, (leafy > 0.5)[..., None]], -1) * 255 + 0.5, 0, 255)
        width, height = COLUMNS * CELL, rows * CELL
        rects[name] = ((x * CELL + 0.5) / width, (y * CELL + 0.5) / height, ((x + 1) * CELL - 0.5) / width,
                       ((y + 1) * CELL - 0.5) / height)
    return albedo, normals, rects

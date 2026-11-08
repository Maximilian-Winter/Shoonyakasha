"""
The vegetation's meshes: plants one unit tall, y up, their base at the
origin, built of bark-textured trunks and branches and alpha-tested
foliage cards (quads showing a card of vegetation_textures' atlas), in
the kinds vegetation_scatter.comp and grass_scatter.comp place:

    conifer_a, conifer_b      a spruce (whorls of drooping branches to the
                              top) and a pine (a bare trunk under a
                              rounded crown)
    broadleaf_a, broadleaf_b  a round crown and a tall oval one
    acacia                    a flat-topped crown on a forked trunk
    fir                       a narrow fir, level branches of flat sprays
                              (vegetation_textures' fir cards) to the top
    each tree grown by tree_generator from its species (SPECIES) as variant
    0 of VARIANTS; each also "far": one camera-facing square about its
    bounding sphere (vegetation_impostors.sphere) showing its octahedral
    impostor
    bush                      a mound of cluster cards
    shrub                     a few upright cards of bare twigs and leaves
    cactus                    a ribbed column with an arm or two
    rock, boulder             a lumpy, smooth-shaded stone (an icosphere
                              pushed in and out by smooth lobes, squashed),
                              reaching ROCK_SINK below its base into the
                              ground; rock_points(name) gives its points for
                              a convex hull, rock_triangles(name) its
                              triangles for a mesh of its own
    grass                     three crossed cards of a tuft
    then each kind of scans.SCANS, a prepared scan (scan_mesh)
    then each tree's variants 1 to VARIANTS - 1 ("conifer_a.1", near then
    far each), from VARIANT_FIRST

A vertex is three vec4: position (xyz) and the texture's u; the normal
(xyz) and the part (w: PART_BARK, PART_CARD, PART_GRASS, PART_IMPOSTOR,
PART_PLAIN, or a scan's part); and (r) a shade (1 in the open, darker
deep in a crown or low down), (g) its sway in the wind (0 on a trunk, 1
at the tip of the outermost branches and on cards; 0 on plain parts) and
(b) its phase in the wind (0 .. 1), or a scan's tangent (rgb), and the
texture's v. A scan's part is PART_SCAN + 4 x its material's atlas cell + 2 when the
material is cut out by its alpha + 1 when its bitangent is the tangent's
cross the normal turned round (scan_part); its (u, v) are its material's
own, which the shaders map into its cell. Bark's (u, v) are in tiles of the bark
texture around and up the trunk (in its height's units); cards' are in
their atlas. An impostor's vertex is a corner of the square about its
tree's sphere (x the corner's sign times the radius, y the centre's height
plus its sign times the radius), its normal (0, 0, 1), its colour.rgb the
corner's signs (x, y) and the radius, and its (u, v) its tree's block's
first corner in the impostors' atlas (vegetation.glsl's
vegetationImpostor). A card's normal points out from its crown's
centre, leaning towards the card's own, so a crown is lit as a whole.

`forest(seed)` grows every variant of every tree from the world's
`seed` (each from the seed, its species and its variant), the same for the
same seed: `Forest` with `meshes` (each near tree kind's (vertices,
indices)) and `trunks` (each near and far tree kind's trunk radius and the
height its first branch leaves it, as shares of its height).
`species_of(kind)` gives a tree kind's species (its variant 0's name),
`is_tree(kind)` whether a kind is a tree's, near or far.

`meshes(impostor_rects=None, scan_cells=None, seed=None)` gives every kind's
vertices in one array, the indices in one array (into the whole vertex
array) and each kind's (first index, index count); `impostor_rects` maps
a near tree kind to its impostor's block (u0, v0, u1, v1) (from (0, 0)
without it), `scan_cells` maps (scan kind, material) to its atlas cell
(scans.atlas); a scan not prepared, or without `scan_cells`, has no
triangles; the trees are `forest(seed)`'s (seed (0, 0, 0) when None).
`trees(seed=None)` gives each near tree kind's mesh by name, for baking
impostors. `instance_firsts(capacity, grass_capacity)` gives each kind's
first instance: a tree kind has `capacity` / VARIANTS, grass
`grass_capacity`, every other kind `capacity`.
`draw_template(ranges, firsts)` gives the kinds' indexed indirect draws
with no instances.
"""

import functools
import math
import zlib
from dataclasses import dataclass

import numpy as np

from . import scans, tree_generator, vegetation_textures
from .vegetation_impostors import sphere

TREES = ("conifer_a", "conifer_b", "broadleaf_a", "broadleaf_b", "acacia", "fir")
VARIANTS = 4                 # grown trees of each species
KINDS = (tuple(k for tree in TREES for k in (tree, tree + "_far")) + ("bush", "shrub", "cactus", "rock", "boulder",
                                                                       "grass") + scans.NAMES
         + tuple(k for tree in TREES for v in range(1, VARIANTS) for k in ("%s.%d" % (tree, v), "%s.%d_far" % (tree, v))))
GRASS = KINDS.index("grass")
SCAN_FIRST = GRASS + 1
VARIANT_FIRST = SCAN_FIRST + len(scans.NAMES)
# name: (seed, height over width before scaling, the icosphere's subdivisions)
ROCKS = {"rock": (21, 0.55, 1), "boulder": (22, 0.7, 2)}
ROCK_SINK = 0.25             # units a rock reaches below its base

PART_BARK, PART_CARD, PART_GRASS, PART_IMPOSTOR, PART_PLAIN, PART_SCAN = range(6)
CROWN_NORMAL = 0.75          # of a card's normal: the share pointing out from the crown's centre
GRASS_UP = 0.65              # of a grass card's normal: the share that is straight up


class _Builder:
    def __init__(self):
        self.vertices = []
        self.indices = []

    def vertex(self, p, n, part, shade, uv, sway=None, phase=None):
        """A vertex; its sway 1 for cards and grass and 0 otherwise when None, its phase from its position."""
        if sway is None:
            sway = 1.0 if part in (PART_CARD, PART_GRASS) else 0.0
        if phase is None:
            phase = (p[0] * 12.9898 + p[1] * 78.233 + p[2] * 37.719) % 1.0
        self.vertices.append((p[0], p[1], p[2], uv[0], n[0], n[1], n[2], float(part), shade, sway, phase, uv[1]))
        return len(self.vertices) - 1

    def quad(self, corners, normals, part, shades, uvs, sway=None, phase=None):
        """Corners counter-clockwise from the bottom left, seen from the front."""
        k = [self.vertex(c, n, part, s, uv, sway, phase) for c, n, s, uv in zip(corners, normals, shades, uvs)]
        self.indices += [k[0], k[1], k[2], k[0], k[2], k[3]]

    def tube(self, a, b, ra, rb, sides, shade, part=PART_BARK, v_scale=None):
        """An open tube from `a` (radius `ra`) to `b` (radius `rb`), bark
        tiles around it once and up it by its circumference at `a`."""
        a, b = np.asarray(a, float), np.asarray(b, float)
        axis = b - a
        length = np.linalg.norm(axis)
        axis = axis / length
        side = np.cross(axis, [0.0, 0.0, 1.0] if abs(axis[2]) < 0.9 else [1.0, 0.0, 0.0])
        side /= np.linalg.norm(side)
        other = np.cross(axis, side)
        v_scale = v_scale or 1.0 / (2.0 * math.pi * max(ra, 1e-3))
        ring = []
        for k in range(sides + 1):
            t = 2.0 * math.pi * k / sides
            out = math.cos(t) * side + math.sin(t) * other
            ring.append((out, k / sides))
        base = len(self.vertices)
        for out, u in ring:
            self.vertex(a + ra * out, out, part, shade[0], (u, 0.0))
            self.vertex(b + rb * out, out, part, shade[1], (u, length * v_scale))
        for k in range(sides):
            i0, i1, j0, j1 = base + 2 * k, base + 2 * k + 1, base + 2 * k + 2, base + 2 * k + 3
            self.indices += [i0, j0, j1, i0, j1, i1]

    def card(self, base, up, facing, width, length, rect, centre, shade_base, shade_tip, part=PART_CARD,
             crown_normal=CROWN_NORMAL, sway=None, phase=None):
        """A card from `base` along `up` for `length`, `width` across,
        facing `facing`, showing the atlas rect (bottom of the card's image
        at `base`); normals out from `centre`, leaning to `facing`."""
        up, facing = _unit(up), _unit(facing)
        across = _unit(np.cross(up, facing))
        facing = np.cross(across, up)
        base = np.asarray(base, float)
        corners = [base - across * width / 2, base + across * width / 2,
                   base + across * width / 2 + up * length, base - across * width / 2 + up * length]
        normals = []
        for c in corners:
            out = c - np.asarray(centre, float)
            n = crown_normal * _unit(out) + (1.0 - crown_normal) * facing
            normals.append(_unit(n))
        u0, v0, u1, v1 = rect
        uvs = [(u0, v1), (u1, v1), (u1, v0), (u0, v0)]
        self.quad(corners, normals, part, (shade_base, shade_base, shade_tip, shade_tip), uvs, sway, phase)

    def arrays(self):
        """(vertices, indices), scaled about the base so the highest point is at 1."""
        vertices = np.array(self.vertices, np.float64).reshape(-1, 12)
        vertices[:, :3] /= max(vertices[:, 1].max(), 1e-6)
        return vertices.astype(np.float32), np.array(self.indices, np.uint32)


def _unit(v):
    v = np.asarray(v, float)
    n = np.linalg.norm(v)
    return v / n if n > 0 else v


def _random_unit(rng):
    v = rng.normal(size=3)
    return _unit(v)


def _crown_shade(p, centre, radii):
    """1 at a crown's top outside, darker inside and low down."""
    q = (np.asarray(p) - centre) / radii
    outside = min(np.linalg.norm(q), 1.0)
    return float(np.clip(0.45 + 0.35 * outside + 0.2 * (q[1] * 0.5 + 0.5), 0.35, 1.0))


def _crown(b, rng, centre, radii, cards, size, rect_names):
    """Cards spread through an ellipsoid crown, their bases inside it and
    their tips out, so the crown's surface is foliage; none reaches below
    the ground."""
    centre, radii = np.asarray(centre, float), np.asarray(radii, float)
    for _ in range(cards):
        d = _random_unit(rng)
        d[1] = d[1] * 0.9 + 0.1
        d = _unit(d)
        depth = rng.uniform(0.1, 0.8)
        base = centre + d * radii * depth
        out = _unit(d * radii + rng.normal(0.0, 0.25, 3) * radii)
        facing = _unit(_random_unit(rng) + d * 0.5)
        length = size * rng.uniform(0.8, 1.2)
        if base[1] + out[1] * length < 0.0:          # a card that would reach into the ground points up
            out[1] = abs(out[1])
        rect = vegetation_textures.card_rect(rect_names[int(rng.integers(len(rect_names)))])
        tip = base + out * length
        b.card(base, out, facing, length, length, rect, centre, _crown_shade(base, centre, radii),
               _crown_shade(tip, centre, radii))


def _bush():
    rng = np.random.default_rng(11)
    b = _Builder()
    _crown(b, rng, (0.0, 0.42, 0.0), (0.55, 0.42, 0.55), 64, 0.26, ("bush",))
    return b


def _shrub():
    rng = np.random.default_rng(12)
    b = _Builder()
    for k in range(5):
        a = math.pi * k / 5 + rng.uniform(-0.2, 0.2)
        facing = np.array([math.cos(a), 0.0, math.sin(a)])
        up = _unit(np.array([0.0, 1.0, 0.0]) + facing * rng.uniform(-0.25, 0.25))   # tilted forward or back only
        b.card((0, -0.02, 0), up, facing, 1.0, 1.02, vegetation_textures.card_rect("shrub"), (0, 0.35, 0), 0.55, 1.0,
               crown_normal=0.5)
    return b


def _cactus():
    b = _Builder()
    ribs = 12
    green = (0.7, 1.0)
    b.tube((0, -0.05, 0), (0, 0.95, 0), 0.1, 0.085, ribs, green, PART_PLAIN, v_scale=1.0)
    b.tube((0, 0.95, 0), (0, 1.0, 0), 0.085, 0.02, ribs, (1.0, 1.0), PART_PLAIN, v_scale=1.0)
    for side, height in ((1, 0.42), (-1, 0.55)):
        elbow = (side * 0.24, height + 0.05, 0.0)
        b.tube((side * 0.06, height, 0), elbow, 0.055, 0.05, ribs, green, PART_PLAIN, v_scale=1.0)
        top = (side * 0.25, height + 0.33, 0.0)
        b.tube(elbow, top, 0.05, 0.045, ribs, green, PART_PLAIN, v_scale=1.0)
        b.tube(top, (side * 0.25, height + 0.37, 0.0), 0.045, 0.01, ribs, (1.0, 1.0), PART_PLAIN, v_scale=1.0)
    return b


def _icosphere(subdivisions):
    """The unit directions and triangles of an icosahedron split `subdivisions` times."""
    t = (1.0 + math.sqrt(5.0)) / 2.0
    points = [_unit(p) for p in ((-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t),
                                 (0, -1, -t), (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1))]
    faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2),
             (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11),
             (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    for _ in range(subdivisions):
        middles, split = {}, []

        def middle(a, b):
            key = (min(a, b), max(a, b))
            if key not in middles:
                points.append(_unit(points[a] + points[b]))
                middles[key] = len(points) - 1
            return middles[key]

        for a, b, c in faces:
            ab, bc, ca = middle(a, b), middle(b, c), middle(c, a)
            split += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        faces = split
    return np.array(points), faces


def _rock(seed, flat, subdivisions):
    """A lumpy stone: an icosphere split `subdivisions` times pushed in and
    out by a few smooth lobes, squashed to `flat` of its width, one unit
    tall above its base and reaching ROCK_SINK below it, centred on its
    axis, each corner's normal the mean of its faces' (by area), darker
    towards the bottom."""
    rng = np.random.default_rng(seed)
    directions, faces = _icosphere(subdivisions)
    radius = np.ones(len(directions))
    for _ in range(5):
        axis = _random_unit(rng)
        radius += rng.uniform(0.06, 0.14) * np.sin(rng.uniform(2.0, 4.0) * directions @ axis + rng.uniform(0, 6.3))
    p = directions * radius[:, None]
    p[:, 1] *= flat
    low, high = p[:, 1].min(), p[:, 1].max()
    scale = (1.0 + ROCK_SINK) / (high - low)
    p = p * scale
    p[:, 1] += -ROCK_SINK - low * scale
    for axis in (0, 2):
        p[:, axis] -= 0.5 * (p[:, axis].min() + p[:, axis].max())
    outward = []
    smooth = np.zeros_like(p)
    for a, c, d in faces:
        face = [a, c, d]
        corners = p[face]
        cross = np.cross(corners[1] - corners[0], corners[2] - corners[0])
        if np.dot(cross, corners.mean(axis=0) - (0.0, 0.5 - 0.5 * ROCK_SINK, 0.0)) < 0.0:
            face, cross = face[::-1], -cross
        outward.append(face)
        smooth[face] += cross
    smooth /= np.linalg.norm(smooth, axis=1, keepdims=True)
    b = _Builder()
    for face in outward:
        k = [b.vertex(p[i], smooth[i], PART_PLAIN, float(np.clip(0.65 + 0.35 * (p[i][1] + ROCK_SINK), 0.6, 1.0)),
                      (0.0, 0.0)) for i in face]
        b.indices += k
    return b


def rock_points(name):
    """A rock kind's distinct points, (N x 3), for its convex hull."""
    vertices, _ = _rock(*ROCKS[name]).arrays()
    return np.unique(np.round(vertices[:, :3], 6), axis=0)


def rock_triangles(name):
    """A rock kind's triangles as drawn: positions, normals and shades
    (N x 3 each, three rows per triangle)."""
    vertices, indices = _rock(*ROCKS[name]).arrays()
    v = vertices[indices]
    return v[:, 0:3], v[:, 4:7], np.repeat(v[:, 8:9], 3, axis=1)


def scan_part(cell, cutout, flipped):
    return PART_SCAN + 4 * cell + (2 if cutout else 0) + (1 if flipped else 0)


def scan_mesh(scan, cells):
    """A prepared scan's (vertices N x 12, indices), one unit tall above
    its base less its kind's sink, which reaches below it; triangles of a
    material without a cell are left out."""
    if scan is None or cells is None:
        return np.zeros((0, 12), np.float32), np.zeros(0, np.uint32)
    height = scan.height
    p = scan.positions / height
    p[:, 1] -= scan.kind.sink
    cell = np.array([cells.get((scan.kind.name, int(m)), -1) for m in range(int(scan.materials.max()) + 1)])
    cutout = np.array([bool(t.get("cutout")) for t in scan.textures] + [False] * len(cell))
    material = scan.materials.astype(np.int64)
    part = PART_SCAN + 4 * cell[material] + 2 * cutout[material] + (scan.tangents[:, 3] < 0.0)
    vertices = np.concatenate([p, scan.uvs[:, :1], scan.normals, part[:, None], scan.tangents[:, :3],
                               scan.uvs[:, 1:2]], axis=1).astype(np.float32)
    triangles = scan.indices.reshape(-1, 3)
    kept = (cell[material[triangles]] >= 0).all(axis=1)
    return vertices, triangles[kept].reshape(-1).astype(np.uint32)


def _grass():
    b = _Builder()
    rect = vegetation_textures.card_rect("grass")
    for k in range(3):
        a = math.pi * k / 3 + 0.2
        facing = np.array([math.cos(a), 0.0, math.sin(a)])
        corners_up = np.array([0.0, 1.0, 0.0])
        across = _unit(np.cross(corners_up, facing))
        corners = [-across * 0.5, across * 0.5, across * 0.5 + corners_up, -across * 0.5 + corners_up]
        normal = _unit((1.0 - GRASS_UP) * facing + GRASS_UP * corners_up)
        u0, v0, u1, v1 = rect
        b.quad(corners, [normal] * 4, PART_GRASS, (0.45, 0.45, 1.0, 1.0), [(u0, v1), (u1, v1), (u1, v0), (u0, v0)])
    return b


def _impostor(centre, radius, rect):
    """(vertices, indices) of the square about a tree's sphere, showing its block from `rect`'s first corner."""
    u0, v0 = rect[0], rect[1]
    corners = ((-1.0, -1.0), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0))
    vertices = np.array([(x * radius, centre + y * radius, 0.0, u0, 0.0, 0.0, 1.0, float(PART_IMPOSTOR), x, y, radius, v0)
                         for x, y in corners], np.float32)
    return vertices, np.array([0, 1, 2, 0, 2, 3], np.uint32)


def species_of(kind):
    """The species of a tree kind, near or far: its variant 0's name."""
    name = kind[:-4] if kind.endswith("_far") else kind
    return name.split(".")[0]


def is_tree(kind):
    return species_of(kind) in TREES


def near_trees():
    """Every near tree kind's name."""
    return [k for k in KINDS if is_tree(k) and not k.endswith("_far")]


def variant_seed(seed, name):
    """The seed a near tree kind grows from, from the world's `seed`."""
    return zlib.crc32(repr((tuple(float(x) for x in seed), name)).encode())


@dataclass
class Forest:
    meshes: dict                 # near tree kind: (vertices, indices)
    trunks: dict                 # near and far tree kind: (trunk radius, trunk height) as shares of its height


@functools.lru_cache(maxsize=2)
def _forest(seed):
    meshes, trunks = {}, {}
    for name in near_trees():
        vertices, indices, trunk = tree_generator.grow(tree_generator.SPECIES[species_of(name)],
                                                       variant_seed(seed, name))
        meshes[name] = (vertices, indices)
        trunks[name] = trunks[name + "_far"] = trunk
    return Forest(meshes, trunks)


def forest(seed=None):
    return _forest(tuple(float(x) for x in (seed or (0.0, 0.0, 0.0))[:3]))


def trees(seed=None):
    """Each near tree kind's mesh by name: (vertices, indices)."""
    return forest(seed).meshes


def meshes(impostor_rects=None, scan_cells=None, seed=None):
    """(vertices (N x 12 float32), indices (uint32, into the whole vertex
    array), [(first index, index count)] per kind)."""
    parts, ranges, first_vertex, first_index = [], [], 0, 0
    all_indices = []
    grown = trees(seed)
    for name in KINDS:
        if name in scans.NAMES:
            vertices, indices = scan_mesh(scans.prepared(name) if scan_cells is not None else None, scan_cells)
            parts.append(vertices)
            all_indices.append(indices + first_vertex)
            ranges.append((first_index, len(indices)))
            first_vertex += len(vertices)
            first_index += len(indices)
            continue
        if is_tree(name):
            if name.endswith("_far"):
                tree = name[:-4]
                rect = (impostor_rects or {}).get(tree, (0.0, 0.0, 1.0, 1.0))
                vertices, indices = _impostor(*sphere(grown[tree][0]), rect)
            else:
                vertices, indices = grown[name]
            parts.append(vertices)
            all_indices.append(indices + first_vertex)
            ranges.append((first_index, len(indices)))
            first_vertex += len(vertices)
            first_index += len(indices)
            continue
        if name in ROCKS:
            builder = _rock(*ROCKS[name])
        else:
            builder = {"bush": _bush, "shrub": _shrub, "cactus": _cactus, "grass": _grass}[name]()
        vertices, indices = builder.arrays()
        parts.append(vertices)
        all_indices.append(indices + first_vertex)
        ranges.append((first_index, len(indices)))
        first_vertex += len(vertices)
        first_index += len(indices)
    return np.concatenate(parts), np.concatenate(all_indices).astype(np.uint32), ranges


def kind_capacity(kind, capacity, grass_capacity):
    """The instances kind index `kind` holds."""
    if kind == GRASS:
        return grass_capacity
    return capacity // VARIANTS if is_tree(KINDS[kind]) else capacity


def instance_firsts(capacity, grass_capacity):
    """Each kind's first instance in VegetationInstances, the kinds one
    after another in their order, each holding kind_capacity."""
    firsts, at = [], 0
    for kind in range(len(KINDS)):
        firsts.append(at)
        at += kind_capacity(kind, capacity, grass_capacity)
    return firsts


def draw_template(ranges, firsts):
    """The kinds' indexed indirect draws with no instances yet: (index count,
    0, first index, 0, the kind's first instance from `firsts`) each, as
    uint32."""
    words = []
    for (first, count), first_instance in zip(ranges, firsts):
        words += [count, 0, first, 0, first_instance]
    return np.array(words, dtype=np.uint32)

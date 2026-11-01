"""
The vegetation's meshes: plants one unit tall, y up, their base at the
origin, built of bark-textured trunks and branches and alpha-tested
foliage cards (quads showing a card of vegetation_textures' atlas), in
the kinds vegetation_scatter.comp and grass_scatter.comp place:

    conifer_a, conifer_b      a spruce (whorls of drooping branch cards to
                              the top) and a pine (a bare trunk under a
                              rounded crown of branch cards)
    broadleaf_a, broadleaf_b  a round crown and a tall oval one of twig
                              cards on a trunk with branches
    acacia                    a flat-topped crown on a forked trunk
    each of these also "far": one camera-facing quad showing its impostor
    (vegetation_impostors), as wide as the tree
    bush                      a mound of cluster cards
    shrub                     a few upright cards of bare twigs and leaves
    cactus                    a ribbed column with an arm or two
    rock, boulder             a lumpy, flat-shaded stone (an icosphere
                              pushed in and out by smooth lobes, squashed),
                              reaching ROCK_SINK below its base into the
                              ground; rock_points(name) gives its points for
                              a convex hull, rock_triangles(name) its
                              triangles for a mesh of its own
    grass                     three crossed cards of a tuft

A vertex is three vec4: position (xyz) and the texture's u; the normal
(xyz) and the part (w: PART_BARK, PART_CARD, PART_GRASS, PART_IMPOSTOR,
PART_PLAIN); and a shade (rgb: 1 in the open, darker deep in a crown or
low down) and the texture's v. Bark's (u, v) are in tiles of the bark
texture around and up the trunk (in its height's units); cards' and
impostors' are in their atlas. A card's normal points out from its crown's
centre, leaning towards the card's own, so a crown is lit as a whole.

`meshes(impostor_rects=None)` gives every kind's vertices in one array,
the indices in one array (into the whole vertex array) and each kind's
(first index, index count); `impostor_rects` maps a tree's name to its
impostor's (u0, v0, u1, v1) (the whole image without it). `trees()` gives
each tree's near mesh by name, for baking impostors.
`draw_template(ranges, firsts)` gives the kinds' indexed indirect draws
with no instances.
"""

import math

import numpy as np

from . import vegetation_textures

TREES = ("conifer_a", "conifer_b", "broadleaf_a", "broadleaf_b", "acacia")
KINDS = tuple(k for tree in TREES for k in (tree, tree + "_far")) + ("bush", "shrub", "cactus", "rock", "boulder",
                                                                      "grass")
GRASS = KINDS.index("grass")
# name: (seed, height over width before scaling, the icosphere's subdivisions)
ROCKS = {"rock": (21, 0.55, 1), "boulder": (22, 0.7, 2)}
ROCK_SINK = 0.25             # units a rock reaches below its base

PART_BARK, PART_CARD, PART_GRASS, PART_IMPOSTOR, PART_PLAIN = range(5)
CROWN_NORMAL = 0.75          # of a card's normal: the share pointing out from the crown's centre
GRASS_UP = 0.65              # of a grass card's normal: the share that is straight up


def impostor_width(vertices):
    """The width of a tree's impostor: twice its furthest point from its axis."""
    return float(2.0 * np.sqrt(vertices[:, 0] ** 2 + vertices[:, 2] ** 2).max())


class _Builder:
    def __init__(self):
        self.vertices = []
        self.indices = []

    def vertex(self, p, n, part, shade, uv):
        self.vertices.append((p[0], p[1], p[2], uv[0], n[0], n[1], n[2], float(part), shade, shade, shade, uv[1]))
        return len(self.vertices) - 1

    def quad(self, corners, normals, part, shades, uvs):
        """Corners counter-clockwise from the bottom left, seen from the front."""
        k = [self.vertex(c, n, part, s, uv) for c, n, s, uv in zip(corners, normals, shades, uvs)]
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
             crown_normal=CROWN_NORMAL):
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
        self.quad(corners, normals, part, (shade_base, shade_base, shade_tip, shade_tip), uvs)

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


def _tree(name):
    rng = np.random.default_rng(sum(map(ord, name)))
    b = _Builder()
    wood = (0.8, 0.9)
    if name == "conifer_a":
        b.tube((0, -0.05, 0), (0, 1.0, 0), 0.03, 0.004, 7, wood)
        whorls = 11
        for i in range(whorls):
            t = i / (whorls - 1)
            y = 0.24 + 0.71 * t
            reach = 0.3 * (1.0 - t) + 0.05
            for k in range(6):
                a = 2.0 * math.pi * (k / 6 + rng.uniform(-0.05, 0.05) + 0.37 * i)
                out = np.array([math.cos(a), -0.35 - 0.2 * rng.random(), math.sin(a)])
                facing = np.array([0.0, 1.0, 0.0]) + 0.3 * _random_unit(rng)
                shade = 0.55 + 0.45 * t
                b.card((0, y, 0), out, facing, reach * 1.1, reach * 1.25,
                       vegetation_textures.card_rect("conifer_a" if k % 2 else "conifer_b"), (0, y + 0.1, 0),
                       shade * 0.7, shade, crown_normal=0.6)
        b.card((0, 0.82, 0), (0, 1, 0), (1, 0, 0), 0.12, 0.2, vegetation_textures.card_rect("conifer_a"),
               (0, 0.7, 0), 0.9, 1.0)
        b.card((0, 0.82, 0), (0, 1, 0), (0, 0, 1), 0.12, 0.2, vegetation_textures.card_rect("conifer_b"),
               (0, 0.7, 0), 0.9, 1.0)
    elif name == "conifer_b":
        b.tube((0, -0.05, 0), (0, 0.85, 0), 0.035, 0.012, 7, wood)
        for k in range(4):
            a = 2.0 * math.pi * k / 4 + rng.uniform(-0.3, 0.3)
            end = (0.18 * math.cos(a), 0.8 + rng.uniform(-0.05, 0.05), 0.18 * math.sin(a))
            b.tube((0, 0.62, 0), end, 0.012, 0.005, 5, wood)
        _crown(b, rng, (0.0, 0.8, 0.0), (0.3, 0.17, 0.3), 46, 0.2, ("conifer_a", "conifer_b"))
    elif name in ("broadleaf_a", "broadleaf_b"):
        tall = name == "broadleaf_b"
        b.tube((0, -0.05, 0), (0, 0.5, 0), 0.045, 0.03, 8, wood)
        centre = (0.0, 0.66, 0.0) if tall else (0.0, 0.62, 0.0)
        radii = (0.24, 0.33, 0.24) if tall else (0.36, 0.3, 0.36)
        for k in range(5):
            a = 2.0 * math.pi * k / 5 + rng.uniform(-0.3, 0.3)
            out = np.array([radii[0] * 0.7 * math.cos(a), rng.uniform(0.0, 0.2), radii[2] * 0.7 * math.sin(a)])
            b.tube((0, 0.42 + 0.05 * k / 5, 0), np.asarray(centre) + out, 0.022, 0.008, 5, wood)
        _crown(b, rng, centre, radii, 110 if tall else 140, 0.26, ("broadleaf_a", "broadleaf_b"))
    elif name == "acacia":
        b.tube((0, -0.05, 0), (0.02, 0.45, 0), 0.04, 0.03, 7, wood)
        for k in range(4):
            a = 2.0 * math.pi * k / 4 + rng.uniform(-0.3, 0.3)
            end = (0.4 * math.cos(a), 0.78 + rng.uniform(-0.04, 0.04), 0.4 * math.sin(a))
            b.tube((0.02, 0.43, 0), end, 0.02, 0.008, 5, wood)
        centre, radii = np.array([0.0, 0.84, 0.0]), np.array([0.6, 0.12, 0.6])
        for _ in range(90):
            r, a = math.sqrt(rng.random()) * 0.52, rng.uniform(0, 2 * math.pi)
            base = centre + np.array([r * math.cos(a), rng.uniform(-0.08, 0.04), r * math.sin(a)])
            out = _unit(np.array([math.cos(a + rng.uniform(-1, 1)), rng.uniform(0.2, 0.6),
                                  math.sin(a + rng.uniform(-1, 1))]))
            facing = _unit(np.array([0.0, 1.0, 0.0]) + 0.8 * _random_unit(rng))
            b.card(base, out, facing, 0.28, 0.22, vegetation_textures.card_rect("acacia"), centre,
                   _crown_shade(base, centre, radii), 1.0, crown_normal=0.5)
    return b


def _bush():
    rng = np.random.default_rng(11)
    b = _Builder()
    _crown(b, rng, (0.0, 0.42, 0.0), (0.55, 0.42, 0.55), 26, 0.42, ("bush",))
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
    axis, flat-shaded, darker towards the bottom."""
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
    b = _Builder()
    for a, c, d in faces:
        corners = p[[a, c, d]]
        normal = _unit(np.cross(corners[1] - corners[0], corners[2] - corners[0]))
        if np.dot(normal, corners.mean(axis=0) - (0.0, 0.5 - 0.5 * ROCK_SINK, 0.0)) < 0.0:
            corners, normal = corners[::-1], -normal
        k = [b.vertex(q, normal, PART_PLAIN, float(np.clip(0.65 + 0.35 * (q[1] + ROCK_SINK), 0.6, 1.0)), (0.0, 0.0))
             for q in corners]
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
    return v[:, 0:3], v[:, 4:7], v[:, 8:11]


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


def _impostor(width, rect):
    b = _Builder()
    u0, v0, u1, v1 = rect
    corners = [(-width / 2, 0.0, 0.0), (width / 2, 0.0, 0.0), (width / 2, 1.0, 0.0), (-width / 2, 1.0, 0.0)]
    b.quad(corners, [(0.0, 0.0, 1.0)] * 4, PART_IMPOSTOR, (1.0, 1.0, 1.0, 1.0), [(u0, v1), (u1, v1), (u1, v0), (u0, v0)])
    return b


def trees():
    """Each tree's near mesh by name: (vertices, indices)."""
    return {name: _tree(name).arrays() for name in TREES}


def meshes(impostor_rects=None):
    """(vertices (N x 12 float32), indices (uint32, into the whole vertex
    array), [(first index, index count)] per kind)."""
    parts, ranges, first_vertex, first_index = [], [], 0, 0
    all_indices = []
    for name in KINDS:
        if name.endswith("_far"):
            tree = name[:-4]
            rect = (impostor_rects or {}).get(tree, (0.0, 0.0, 1.0, 1.0))
            builder = _impostor(impostor_width(_tree(tree).arrays()[0]), rect)
        elif name in TREES:
            builder = _tree(name)
        elif name in ROCKS:
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


def draw_template(ranges, firsts):
    """The kinds' indexed indirect draws with no instances yet: (index count,
    0, first index, 0, the kind's first instance from `firsts`) each, as
    uint32."""
    words = []
    for (first, count), first_instance in zip(ranges, firsts):
        words += [count, 0, first, 0, first_instance]
    return np.array(words, dtype=np.uint32)

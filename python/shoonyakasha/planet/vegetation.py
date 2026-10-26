"""
The vegetation's meshes: low-poly plants one unit tall, y up, their base at
the origin, flat-shaded (every triangle its own three vertices with its
face's normal), in the kinds vegetation_scatter.comp places:

    0  conifer        a hexagonal trunk under three stacked eight-sided cones
    1  conifer, far   one five-sided cone
    2  broadleaf      a hexagonal trunk under a lumpy icosphere crown
    3  broadleaf, far an octahedron crown on a thin trunk
    4  bush           a squashed, lumpy icosphere

A vertex is three vec4: position (xyz), normal (xyz; w 1 for foliage, 0
for wood) and colour (linear albedo). `meshes()` gives them all in one
vertex array and one index array, with each kind's (first index, index
count); `draw_template(capacity)` the five indexed indirect draws with no
instances, the kind k's first instance at k * capacity.
"""

import math

import numpy as np

KINDS = ("conifer", "conifer far", "broadleaf", "broadleaf far", "bush")
WOOD = (0.2, 0.12, 0.065)
CONIFER = (0.05, 0.12, 0.055)
BROADLEAF = (0.09, 0.18, 0.05)
BUSH = (0.11, 0.19, 0.05)


class _Builder:
    def __init__(self):
        self.vertices = []

    def triangle(self, a, b, c, colour, foliage):
        """A triangle a, b, c, counter-clockwise seen from where its normal points."""
        ab = np.subtract(b, a)
        ac = np.subtract(c, a)
        n = np.cross(ab, ac)
        n = n / (np.linalg.norm(n) or 1.0)
        for p in (a, b, c):
            self.vertices.append((p[0], p[1], p[2], 1.0, n[0], n[1], n[2], 1.0 if foliage else 0.0,
                                  colour[0], colour[1], colour[2], 1.0))

    def prism(self, sides, radius, bottom, top, colour):
        """An open prism around the y axis."""
        for k in range(sides):
            a0, a1 = 2.0 * math.pi * k / sides, 2.0 * math.pi * (k + 1) / sides
            p0 = (radius * math.cos(a0), bottom, radius * math.sin(a0))
            p1 = (radius * math.cos(a1), bottom, radius * math.sin(a1))
            q0 = (radius * math.cos(a0), top, radius * math.sin(a0))
            q1 = (radius * math.cos(a1), top, radius * math.sin(a1))
            self.triangle(p0, q1, p1, colour, False)
            self.triangle(p0, q0, q1, colour, False)

    def cone(self, sides, radius, bottom, top, colour, twist=0.0):
        """A cone around the y axis with its base closed."""
        apex = (0.0, top, 0.0)
        centre = (0.0, bottom, 0.0)
        for k in range(sides):
            a0 = 2.0 * math.pi * k / sides + twist
            a1 = 2.0 * math.pi * (k + 1) / sides + twist
            p0 = (radius * math.cos(a0), bottom, radius * math.sin(a0))
            p1 = (radius * math.cos(a1), bottom, radius * math.sin(a1))
            self.triangle(p0, apex, p1, colour, True)
            self.triangle(p0, p1, centre, colour, True)

    def blob(self, subdivisions, centre, radii, colour, lumps, seed):
        """An icosphere (`subdivisions` times split) scaled by `radii`
        around `centre`, each vertex moved in or out by up to `lumps` of
        its radius, the same for the same vertex."""
        vertices, faces = _icosphere(subdivisions)
        rng = np.random.default_rng(seed)
        scales = 1.0 + lumps * (rng.random(len(vertices)) * 2.0 - 1.0)
        points = [tuple(centre[k] + radii[k] * v[k] * s for k in range(3)) for v, s in zip(vertices, scales)]
        for a, b, c in faces:
            self.triangle(points[a], points[b], points[c], colour, True)

    def array(self):
        return np.array(self.vertices, dtype=np.float32).reshape(-1, 12)


def _icosphere(subdivisions):
    """Unit icosphere: (vertices, triangles counter-clockwise from outside)."""
    t = (1.0 + math.sqrt(5.0)) / 2.0
    vertices = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t),
                (0, -1, -t), (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    vertices = [tuple(c / math.sqrt(sum(x * x for x in v)) for c in v) for v in vertices]
    faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2),
             (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5),
             (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    for _ in range(subdivisions):
        middles = {}

        def middle(a, b):
            key = (min(a, b), max(a, b))
            if key not in middles:
                m = tuple((vertices[a][k] + vertices[b][k]) / 2.0 for k in range(3))
                n = math.sqrt(sum(c * c for c in m))
                vertices.append(tuple(c / n for c in m))
                middles[key] = len(vertices) - 1
            return middles[key]

        split = []
        for a, b, c in faces:
            ab, bc, ca = middle(a, b), middle(b, c), middle(c, a)
            split += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        faces = split
    return vertices, faces


def _kind(name):
    b = _Builder()
    if name == "conifer":
        b.prism(6, 0.035, -0.05, 0.25, WOOD)
        for bottom, top, radius, shade, twist in ((0.16, 0.62, 0.27, 1.0, 0.0), (0.38, 0.82, 0.21, 1.08, 0.4),
                                                  (0.58, 1.0, 0.14, 1.16, 0.8)):
            b.cone(8, radius, bottom, top, tuple(c * shade for c in CONIFER), twist)
    elif name == "conifer far":
        b.cone(5, 0.25, 0.12, 1.0, CONIFER)
    elif name == "broadleaf":
        b.prism(6, 0.05, -0.05, 0.5, WOOD)
        b.blob(1, (0.0, 0.66, 0.0), (0.4, 0.34, 0.4), BROADLEAF, 0.12, 7)
    elif name == "broadleaf far":
        b.prism(3, 0.05, -0.05, 0.4, WOOD)
        b.blob(0, (0.0, 0.64, 0.0), (0.4, 0.36, 0.4), BROADLEAF, 0.0, 0)
    elif name == "bush":
        b.blob(1, (0.0, 0.42, 0.0), (0.6, 0.45, 0.6), BUSH, 0.1, 11)
    return b.array()


def meshes():
    """(vertices (N x 12 float32), indices (uint32), [(first index, index count)] per kind)."""
    parts, ranges, first = [], [], 0
    for name in KINDS:
        part = _kind(name)
        parts.append(part)
        ranges.append((first, len(part)))
        first += len(part)
    vertices = np.concatenate(parts)
    return vertices, np.arange(len(vertices), dtype=np.uint32), ranges


def draw_template(ranges, capacity):
    """The kinds' indexed indirect draws with no instances yet: (index count,
    0, first index, 0, kind * capacity) each, as uint32."""
    words = []
    for kind, (first, count) in enumerate(ranges):
        words += [count, 0, first, 0, kind * capacity]
    return np.array(words, dtype=np.uint32)

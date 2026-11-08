"""
The outposts' parts, built from boxes, cylinders, cones, domes and mounds.

A part (`Part`) is built in its own frame: x to its right, y up, z ahead,
its footprint a rectangle of half extents `half` (x, z) about the origin,
standing on y = 0. It is made of pieces (`Piece`), each rigid: its solid
triangles (vertex coloured), its lamps' glow (`lamp`: a fixture in
LAMP_FIXTURE among the solid triangles and its glow in LAMP around it,
drawn at night) and its screens' (in SCREEN), and its hulls, point sets for convex hulls in
the piece's frame, which sits at `position` in the part's. A dynamic piece
(containers, barriers) has a mass and moves on its own in the physics; the
rest are static. A part may have a beacon (`beacon`, a point in its frame).

`Geometry` collects flat-shaded triangles with their normals and colours:
`box`, `cylinder` (a cone or a frustum with `radius_top`), `dome` (a
hemisphere), `mound` (a heap with seeded bumps) and `add` (another
geometry turned and moved); `arrays()` gives (positions, normals,
colours), float32 N x 3 each. `box_points` and `cylinder_points` give a
box's corners and a cylinder's rim points for hulls.

`PARTS` maps each part's name to its builder, `build(name, rng, **params)`:
pad (`size`, `colour`), habitat (`stack` 1 or 2), dome (`radius`),
container_stack (`count` 1 to 3), tank, tank_farm (`count`), pipe
(`length`), ore_line (`length`, `tint`: a heap, a conveyor and a hopper),
mast (`height`), solar (`tilt`, degrees towards its ahead), antenna,
barrier, terminal and ore_bin (its inside `BIN_INSIDE`, half extents x,
z and the height range, in its frame).
"""

import math
from dataclasses import dataclass, field

import numpy as np

WHITE = (0.70, 0.69, 0.64)       # worn off-white panels
YELLOW = (0.80, 0.56, 0.10)      # handles and stripes, the car's
FRAME = (0.13, 0.14, 0.16)       # dark grey frames
CONCRETE = (0.42, 0.42, 0.41)
SOLAR = (0.04, 0.07, 0.15)
PAD = (0.33, 0.34, 0.37)
CONTAINERS = ((0.55, 0.22, 0.11), (0.11, 0.33, 0.37), (0.76, 0.54, 0.11), (0.38, 0.40, 0.43), (0.21, 0.29, 0.19))
LAMP = (1.0, 0.82, 0.58)         # the lamps' emissive colour
LAMP_FIXTURE = (0.75, 0.72, 0.64)   # a lamp's colour unlit
LAMP_OVER = 0.03                 # m a lamp's glow stands out from its fixture on every side
SCREEN = (0.22, 0.80, 1.0)       # the screens'
BEACON = (1.0, 0.16, 0.08)       # the beacons'
CONTAINER = (6.0, 2.6, 2.4)      # m: length, height, width
CONTAINER_MASS = 2_200.0         # kg
BARRIER = (4.0, 1.0, 0.5)        # m: length, height, depth
BARRIER_MASS = 400.0             # kg
BIN_INSIDE = (1.35, 0.9, 0.15, 1.6)   # the ore bin's inside: half x, half z, from and to y


# A box's faces as corner indices (box_points' order: x, then y, then z), each going round its face.
BOX_FACES = ((0, 1, 3, 2), (4, 5, 7, 6), (0, 1, 5, 4), (2, 3, 7, 6), (0, 2, 6, 4), (1, 3, 7, 5))


class Geometry:
    def __init__(self):
        self.positions, self.normals, self.colours = [], [], []

    def __len__(self):
        return sum(len(p) for p in self.positions)

    def triangles(self, points, colour):
        """Triangles of `points` (three per triangle, counter-clockwise from outside), flat shaded; those
        without area are left out."""
        p = np.asarray(points, np.float64).reshape(-1, 3, 3)
        n = np.cross(p[:, 1] - p[:, 0], p[:, 2] - p[:, 0])
        size = np.linalg.norm(n, axis=1)
        keep = size > 1e-10
        p, n = p[keep].reshape(-1, 3), n[keep] / size[keep, None]
        self.positions.append(p)
        self.normals.append(np.repeat(n, 3, axis=0))
        self.colours.append(np.tile(np.asarray(colour, np.float64), (len(p), 1)))

    def quads(self, quads, colour):
        """Quads (four corners each, counter-clockwise from outside) as two triangles each."""
        q = np.asarray(quads, np.float64).reshape(-1, 4, 3)
        self.triangles(q[:, [0, 1, 2, 0, 2, 3]].reshape(-1, 3), colour)

    def box(self, centre, size, colour, rotation=None):
        corners = box_points(centre, size, rotation)
        middle = corners.mean(axis=0)
        quads = []
        for f in BOX_FACES:
            q = corners[list(f)]
            if np.dot(np.cross(q[1] - q[0], q[2] - q[0]), q.mean(axis=0) - middle) < 0.0:
                q = q[::-1]
            quads.append(q)
        self.quads(quads, colour)

    def cylinder(self, base, axis, radius, length, colour, segments=12, radius_top=None, caps=True):
        """Along unit `axis` from `base`, `radius` there and `radius_top` (the same when None) at the far end."""
        top_radius = radius if radius_top is None else radius_top
        base, axis = np.asarray(base, np.float64), np.asarray(axis, np.float64)
        u, v = perpendiculars(axis)
        angles = np.linspace(0.0, 2.0 * math.pi, segments + 1)
        rim = np.outer(np.cos(angles), u) + np.outer(np.sin(angles), v)
        low, high = base + radius * rim, base + axis * length + top_radius * rim
        sides = [(low[k], low[k + 1], high[k + 1], high[k]) for k in range(segments)]
        if top_radius <= 0.0:
            self.triangles([(low[k], low[k + 1], high[k]) for k in range(segments)], colour)
        else:
            self.quads(sides, colour)
        if caps:
            top = base + axis * length
            if top_radius > 0.0:
                self.triangles([(top, high[k], high[k + 1]) for k in range(segments)], colour)
            self.triangles([(base, low[k + 1], low[k]) for k in range(segments)], colour)

    def dome(self, base, radius, colour, rings=6, segments=18):
        base = np.asarray(base, np.float64)
        theta = np.linspace(0.0, 0.5 * math.pi, rings + 1)
        phi = np.linspace(0.0, 2.0 * math.pi, segments + 1)

        def at(i, j):
            return base + radius * np.array([math.cos(theta[i]) * math.sin(phi[j]), math.sin(theta[i]),
                                             math.cos(theta[i]) * math.cos(phi[j])])
        quads = []
        for i in range(rings):
            for j in range(segments):
                quads.append((at(i, j), at(i, j + 1), at(i + 1, j + 1), at(i + 1, j)))
        self.quads(quads, colour)

    def mound(self, base, radius, height, colour, rng, rings=4, segments=14):
        """A heap: rings of points falling from its top, each pushed in or out a little (seeded by `rng`)."""
        base = np.asarray(base, np.float64)
        phi = np.linspace(0.0, 2.0 * math.pi, segments, endpoint=False)
        grid = []
        for i in range(rings + 1):
            t = i / rings
            ring = []
            for j in range(segments):
                r = radius * t * (1.0 + (0.18 * (rng.random() - 0.5) if 0 < i else 0.0))
                y = height * (1.0 - t * t) * (1.0 + (0.12 * (rng.random() - 0.5) if 0 < i < rings else 0.0))
                ring.append(base + np.array([r * math.sin(phi[j]), y if i < rings else 0.0, r * math.cos(phi[j])]))
            grid.append(ring)
        quads = []
        for i in range(rings):
            for j in range(segments):
                k = (j + 1) % segments
                quads.append((grid[i][j], grid[i + 1][j], grid[i + 1][k], grid[i][k]))
        self.quads(quads, colour)
        return np.array([p for ring in grid[1:] for p in ring[::2]] + [grid[0][0]])

    def add(self, other, rotation=None, offset=(0.0, 0.0, 0.0)):
        r = np.eye(3) if rotation is None else np.asarray(rotation, np.float64)
        o = np.asarray(offset, np.float64)
        for p, n, c in zip(other.positions, other.normals, other.colours):
            self.positions.append(p @ r.T + o)
            self.normals.append(n @ r.T)
            self.colours.append(c)

    def arrays(self):
        if not self.positions:
            empty = np.zeros((0, 3), np.float32)
            return empty, empty, empty
        return tuple(np.concatenate(a).astype(np.float32) for a in (self.positions, self.normals, self.colours))


def perpendiculars(axis):
    a = np.asarray(axis, np.float64)
    helper = np.array([1.0, 0.0, 0.0]) if abs(a[0]) < 0.9 else np.array([0.0, 0.0, 1.0])
    u = np.cross(a, helper)
    u /= np.linalg.norm(u)
    return u, np.cross(a, u)


def yaw(angle):
    """The rotation by `angle` radians about y: z (ahead) to (sin, 0, cos)."""
    c, s = math.cos(angle), math.sin(angle)
    return np.array([[c, 0.0, s], [0.0, 1.0, 0.0], [-s, 0.0, c]])


def pitch(angle):
    """The rotation by `angle` radians about x: y (up) to (0, cos, sin)."""
    c, s = math.cos(angle), math.sin(angle)
    return np.array([[1.0, 0.0, 0.0], [0.0, c, -s], [0.0, s, c]])


def lamp(piece, centre, size, rotation=None):
    """A lamp: its fixture in the piece's solid triangles, and its glow, LAMP_OVER larger, in its lamps'."""
    piece.solid.box(centre, size, LAMP_FIXTURE, rotation)
    piece.lamps.box(centre, tuple(a + 2.0 * LAMP_OVER for a in size), LAMP, rotation)


def box_points(centre, size, rotation=None):
    signs = np.array([[x, y, z] for x in (-0.5, 0.5) for y in (-0.5, 0.5) for z in (-0.5, 0.5)])
    corners = signs * np.asarray(size, np.float64)
    if rotation is not None:
        corners = corners @ np.asarray(rotation, np.float64).T
    return corners + np.asarray(centre, np.float64)


def cylinder_points(base, radius, height, segments=12):
    """An upright cylinder's rim points, at `base` and `height` above it."""
    angles = np.linspace(0.0, 2.0 * math.pi, segments, endpoint=False)
    ring = np.stack([radius * np.sin(angles), np.zeros(segments), radius * np.cos(angles)], axis=1)
    base = np.asarray(base, np.float64)
    return np.concatenate([ring + base, ring + base + (0.0, height, 0.0)])


@dataclass
class Piece:
    solid: Geometry = field(default_factory=Geometry)
    lamps: Geometry = field(default_factory=Geometry)
    screens: Geometry = field(default_factory=Geometry)
    hulls: list = field(default_factory=list)
    position: tuple = (0.0, 0.0, 0.0)
    dynamic: bool = False
    mass: float = 0.0


@dataclass
class Part:
    name: str
    pieces: list
    half: tuple
    beacon: tuple = None


def pad(rng, size=30.0, colour=PAD):
    p = Piece()
    p.solid.box((0.0, 0.1, 0.0), (size, 0.2, size), colour)
    p.hulls.append(box_points((0.0, 0.1, 0.0), (size, 0.2, size)))
    mark = 0.23
    for x in (-3.0, 3.0):
        p.solid.box((x, mark, 0.0), (1.2, 0.06, 10.0), YELLOW)
    p.solid.box((0.0, mark, 0.0), (4.8, 0.06, 1.2), YELLOW)
    edge = 0.5 * size - 0.4
    for k in range(7):
        t = -edge + 2.0 * edge * k / 6
        for x, z in ((t, edge), (t, -edge), (edge, t), (-edge, t)):
            lamp(p, (x, 0.3, z), (0.3, 0.2, 0.3))
    return Part("pad", [p], (0.5 * size, 0.5 * size))


def habitat(rng, stack=1):
    p = Piece()
    for k in range(stack):
        y = 1.5 + 3.0 * k
        p.solid.box((0.0, y, 0.0), (6.0, 2.9, 2.9), WHITE)
        for x in (-3.0, 3.0):
            p.solid.box((x, y, 0.0), (0.3, 3.0, 3.06), FRAME)
        p.solid.box((0.0, y - 1.0, 0.0), (5.7, 0.25, 2.96), YELLOW)
        for x in (-1.6, 0.0, 1.6):
            for z in (1.47, -1.47):
                lamp(p, (x, y + 0.45, z), (0.8, 0.5, 0.04))
    p.solid.box((-1.9, 1.15, 1.47), (1.1, 2.1, 0.06), FRAME)
    p.solid.box((-1.25, 1.1, 1.52), (0.1, 0.5, 0.06), YELLOW)
    p.hulls.append(box_points((0.0, 1.5 * stack, 0.0), (6.3, 3.0 * stack, 3.0)))
    return Part("habitat", [p], (3.3, 1.7))


def dome(rng, radius=5.0):
    p = Piece()
    p.solid.cylinder((0.0, 0.0, 0.0), (0.0, 1.0, 0.0), radius + 0.3, 0.6, FRAME, segments=18)
    p.solid.dome((0.0, 0.6, 0.0), radius, WHITE)
    for k in range(10):
        a = 2.0 * math.pi * k / 10
        e = math.radians(18.0)
        r = radius * 0.985
        lamp(p, (r * math.cos(e) * math.sin(a), 0.6 + r * math.sin(e), r * math.cos(e) * math.cos(a)),
             (0.7, 0.45, 0.7), yaw(a))
    z = radius + 0.4
    p.solid.box((0.0, 1.3, z), (2.2, 2.6, 2.0), WHITE)
    p.solid.box((0.0, 1.15, z + 1.0), (1.1, 2.1, 0.06), FRAME)
    p.solid.box((0.0, 2.5, z), (2.3, 0.2, 2.06), YELLOW)
    angles = np.linspace(0.0, 2.0 * math.pi, 12, endpoint=False)
    points = [cylinder_points((0.0, 0.0, 0.0), radius + 0.3, 0.6)]
    for e in (0.35, 0.8, 1.2):
        ring = np.stack([np.sin(angles), np.zeros(12), np.cos(angles)], axis=1) * radius * math.cos(e)
        points.append(ring + (0.0, 0.6 + radius * math.sin(e), 0.0))
    points.append(np.array([[0.0, 0.6 + radius, 0.0]]))
    p.hulls.append(np.concatenate(points))
    p.hulls.append(box_points((0.0, 1.3, z), (2.2, 2.6, 2.0)))
    return Part("dome", [p], (radius + 0.4, radius + 1.5))


def container_piece(colour, y, x=0.0):
    p = Piece(position=(x, y, 0.0), dynamic=True, mass=CONTAINER_MASS)
    length, height, width = CONTAINER
    p.solid.box((0.0, 0.0, 0.0), (length, height, width), colour)
    for e in (-0.5 * length, 0.5 * length):
        p.solid.box((e, 0.0, 0.0), (0.16, height + 0.04, width + 0.04), FRAME)
    for t in (-1.5, 0.0, 1.5):
        p.solid.box((t, 0.0, 0.0), (0.08, height - 0.2, width + 0.04), tuple(0.8 * c for c in colour))
    p.hulls.append(box_points((0.0, 0.0, 0.0), (length, height, width)))
    return p


def container_stack(rng, count=2):
    colour = CONTAINERS[rng.randrange(len(CONTAINERS))]
    height = CONTAINER[1]
    pieces = [container_piece(colour if k == 0 or rng.random() < 0.6 else CONTAINERS[rng.randrange(len(CONTAINERS))],
                              0.5 * height + k * (height + 0.02), rng.uniform(-0.3, 0.3) if k else 0.0)
              for k in range(count)]
    return Part("container_stack", pieces, (0.5 * CONTAINER[0] + 0.3, 0.5 * CONTAINER[2] + 0.1))


def tank_geometry(p, x, z):
    for dx in (-1.3, 1.3):
        for dz in (-1.3, 1.3):
            p.solid.box((x + dx, 0.8, z + dz), (0.3, 1.6, 0.3), FRAME)
    p.solid.cylinder((x, 1.5, z), (0.0, 1.0, 0.0), 2.0, 8.0, WHITE, segments=16)
    p.solid.cylinder((x, 5.0, z), (0.0, 1.0, 0.0), 2.04, 0.5, YELLOW, segments=16, caps=False)
    lamp(p, (x, 9.6, z), (0.3, 0.2, 0.3))
    p.hulls.append(cylinder_points((x, 0.0, z), 2.0, 9.5))


def tank(rng):
    p = Piece()
    tank_geometry(p, 0.0, 0.0)
    return Part("tank", [p], (2.1, 2.1))


def tank_farm(rng, count=3):
    p = Piece()
    span = 5.5 * (count - 1)
    for k in range(count):
        tank_geometry(p, -0.5 * span + 5.5 * k, -0.6)
    p.solid.cylinder((-0.5 * span - 0.5, 0.9, 2.0), (1.0, 0.0, 0.0), 0.3, span + 1.0, YELLOW)
    for k in range(count):
        x = -0.5 * span + 5.5 * k
        p.solid.cylinder((x, 0.9, 1.3), (0.0, 0.0, 1.0), 0.18, 0.75, FRAME, segments=8)
        p.solid.box((x + 2.0, 0.4, 2.0), (0.3, 0.8, 0.3), FRAME)
    return Part("tank_farm", [p], (0.5 * span + 2.2, 2.8))


def pipe(rng, length=10.0):
    p = Piece()
    p.solid.cylinder((0.0, 0.9, -0.5 * length), (0.0, 0.0, 1.0), 0.3, length, YELLOW)
    supports = max(1, int(length // 4.0))
    for k in range(supports + 1):
        p.solid.box((0.0, 0.4, -0.5 * length + length * k / supports), (0.5, 0.8, 0.3), FRAME)
    p.hulls.append(box_points((0.0, 0.6, 0.0), (0.6, 1.2, length)))
    return Part("pipe", [p], (0.4, 0.5 * length))


def ore_line(rng, length=16.0, tint=(0.40, 0.34, 0.28)):
    p = Piece()
    heap_z, hopper_z = -0.5 * length + 1.0, 0.5 * length - 1.5
    p.hulls.append(p.solid.mound((0.0, 0.0, heap_z), 4.0, 3.2, tint, rng))
    p.hulls.append(p.solid.mound((3.2, 0.0, heap_z - 1.0), 2.4, 1.6, tint, rng))
    for dx in (-1.2, 1.2):
        for dz in (-1.2, 1.2):
            p.solid.box((dx, 1.6, hopper_z + dz), (0.25, 3.2, 0.25), FRAME)
    p.solid.box((0.0, 3.2, hopper_z), (3.0, 0.3, 3.0), FRAME)
    p.solid.cylinder((0.0, 3.35, hopper_z), (0.0, 1.0, 0.0), 0.8, 2.0, YELLOW, segments=4, radius_top=2.0)
    p.hulls.append(box_points((0.0, 2.6, hopper_z), (3.0, 5.2, 3.0)))
    start, end = np.array([0.0, 0.6, heap_z + 3.0]), np.array([0.0, 5.2, hopper_z - 1.2])
    along = end - start
    run = float(np.linalg.norm(along))
    rotation = pitch(-math.atan2(along[1], along[2]))
    middle = 0.5 * (start + end)
    p.solid.box(middle, (1.4, 0.2, run), FRAME, rotation)
    p.solid.box(middle + rotation @ (0.0, 0.14, 0.0), (1.1, 0.08, run), (0.08, 0.08, 0.09), rotation)
    for side in (-0.75, 0.75):
        p.solid.box(middle + rotation @ (side, 0.25, 0.0), (0.1, 0.3, run), YELLOW, rotation)
    for k in range(1, 4):
        q = start + along * k / 4
        p.solid.box((0.0, 0.5 * q[1], q[2]), (1.2, q[1], 0.2), FRAME)
    p.hulls.append(box_points(middle, (1.6, 0.6, run), rotation))
    return Part("ore_line", [p], (5.8, 0.5 * length + 2.6))


def mast(rng, height=25.0):
    p = Piece()
    p.solid.box((0.0, 0.25, 0.0), (1.8, 0.5, 1.8), CONCRETE)
    bands = max(2, int(height // 5.0))
    for k in range(bands):
        p.solid.box((0.0, 0.5 + (k + 0.5) * (height - 0.5) / bands, 0.0), (0.7, (height - 0.5) / bands, 0.7),
                    WHITE if k % 2 else (0.82, 0.30, 0.14))
    p.solid.cylinder((0.0, 0.75 * height, 0.38), (0.0, 0.0, 1.0), 0.15, 0.6, FRAME, radius_top=1.1, segments=12)
    lamp(p, (0.0, height + 0.15, 0.0), (0.4, 0.3, 0.4))
    p.hulls.append(box_points((0.0, 0.5 * height, 0.0), (0.8, height, 0.8)))
    return Part("mast", [p], (1.1, 1.3), beacon=(0.0, height + 0.6, 0.0))


def solar(rng, tilt=25.0):
    p = Piece()
    rotation = pitch(-math.radians(tilt))
    centre = (0.0, 2.2, 0.0)
    p.solid.box(centre, (8.0, 0.1, 4.0), SOLAR, rotation)
    p.solid.box(np.asarray(centre) + rotation @ (0.0, -0.08, 0.0), (8.1, 0.06, 4.1), FRAME, rotation)
    for x in (-2.5, 2.5):
        p.solid.box((x, 1.1, 0.0), (0.2, 2.2, 0.2), FRAME)
    p.hulls.append(box_points(centre, (8.1, 0.2, 4.1), rotation))
    p.hulls.append(box_points((0.0, 1.1, 0.0), (5.2, 2.2, 0.2)))
    return Part("solar", [p], (4.2, 2.2))


def antenna(rng, height=10.0):
    p = Piece()
    p.solid.box((0.0, 0.2, 0.0), (1.0, 0.4, 1.0), CONCRETE)
    p.solid.box((0.0, 0.5 * height, 0.0), (0.22, height, 0.22), FRAME)
    up = np.array([0.0, math.sin(math.radians(50.0)), math.cos(math.radians(50.0))])
    p.solid.cylinder((0.0, height - 1.0, 0.1), up, 0.1, 0.7, WHITE, radius_top=1.3, segments=14)
    lamp(p, (0.0, height + 0.1, 0.0), (0.25, 0.2, 0.25))
    p.hulls.append(box_points((0.0, 0.5 * height, 0.0), (0.3, height, 0.3)))
    return Part("antenna", [p], (1.3, 1.3))


def barrier(rng):
    length, height, depth = BARRIER
    p = Piece(position=(0.0, 0.5 * height, 0.0), dynamic=True, mass=BARRIER_MASS)
    p.solid.box((0.0, 0.0, 0.0), (length, height, depth), CONCRETE)
    p.solid.box((0.0, 0.25 * height, 0.0), (length + 0.02, 0.15, depth + 0.02), YELLOW)
    p.hulls.append(box_points((0.0, 0.0, 0.0), (length, height, depth)))
    return Part("barrier", [p], (0.5 * length, 0.5 * depth))


def terminal(rng):
    p = Piece()
    p.solid.box((0.0, 1.0, 0.0), (1.0, 2.0, 0.6), FRAME)
    p.screens.box((0.0, 1.45, 0.31), (0.7, 0.5, 0.03), SCREEN)
    p.solid.box((0.0, 0.95, 0.33), (0.6, 0.08, 0.08), YELLOW)
    p.hulls.append(box_points((0.0, 1.0, 0.0), (1.0, 2.0, 0.6)))
    return Part("terminal", [p], (0.6, 0.4))


def ore_bin(rng):
    hx, hz, low, high = BIN_INSIDE
    wall = 0.12
    p = Piece()
    p.solid.box((0.0, 0.5 * low, 0.0), (2.0 * hx + 2.0 * wall, low, 2.0 * hz + 2.0 * wall), FRAME)
    p.hulls.append(box_points((0.0, 0.5 * low, 0.0), (2.0 * hx + 2.0 * wall, low, 2.0 * hz + 2.0 * wall)))
    top = 1.0
    for centre, size in (((hx + 0.5 * wall, 0.5 * top, 0.0), (wall, top, 2.0 * hz + 2.0 * wall)),
                         ((-hx - 0.5 * wall, 0.5 * top, 0.0), (wall, top, 2.0 * hz + 2.0 * wall)),
                         ((0.0, 0.5 * top, hz + 0.5 * wall), (2.0 * hx, top, wall)),
                         ((0.0, 0.5 * top, -hz - 0.5 * wall), (2.0 * hx, top, wall))):
        p.solid.box(centre, size, FRAME)
        p.solid.box((centre[0], top, centre[2]), (size[0] + 0.02, 0.1, size[2] + 0.02), YELLOW)
        p.hulls.append(box_points(centre, size))
    return Part("ore_bin", [p], (hx + 0.2, hz + 0.2))


PARTS = {"pad": pad, "habitat": habitat, "dome": dome, "container_stack": container_stack, "tank": tank,
         "tank_farm": tank_farm, "pipe": pipe, "ore_line": ore_line, "mast": mast, "solar": solar,
         "antenna": antenna, "barrier": barrier, "terminal": terminal, "ore_bin": ore_bin}


def build(name, rng, **params):
    return PARTS[name](rng, **params)

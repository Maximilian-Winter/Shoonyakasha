"""
Cube-sphere geometry in Python floats (64-bit).

Each of the six cube faces has a frame (normal, u axis, v axis) with
cross(u, v) == normal. A point of a face is normal + u * u_axis + v * v_axis
with u, v in [-1, 1]; `spherify` maps it onto the unit sphere.

A chunk is the square of a face at quadtree `level` with integer
coordinates (x, y), 0 .. 2**level - 1 along u and v.
"""

import math

# (normal, u axis, v axis) per face: +X, -X, +Y, -Y, +Z, -Z.
FACES = (
    ((1.0, 0.0, 0.0), (0.0, 0.0, -1.0), (0.0, 1.0, 0.0)),
    ((-1.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, 1.0, 0.0)),
    ((0.0, 1.0, 0.0), (1.0, 0.0, 0.0), (0.0, 0.0, -1.0)),
    ((0.0, -1.0, 0.0), (1.0, 0.0, 0.0), (0.0, 0.0, 1.0)),
    ((0.0, 0.0, 1.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)),
    ((0.0, 0.0, -1.0), (-1.0, 0.0, 0.0), (0.0, 1.0, 0.0)),
)


def spherify(p):
    """The point `p` of the cube [-1, 1]^3 surface mapped onto the unit
    sphere, spacing points more evenly than normalising."""
    x, y, z = p
    x2, y2, z2 = x * x, y * y, z * z
    return (x * math.sqrt(1.0 - 0.5 * y2 - 0.5 * z2 + y2 * z2 / 3.0),
            y * math.sqrt(1.0 - 0.5 * z2 - 0.5 * x2 + z2 * x2 / 3.0),
            z * math.sqrt(1.0 - 0.5 * x2 - 0.5 * y2 + x2 * y2 / 3.0))


def cube_point(face, u, v):
    n, a, b = FACES[face]
    return (n[0] + u * a[0] + v * b[0], n[1] + u * a[1] + v * b[1], n[2] + u * a[2] + v * b[2])


def direction(face, u, v):
    """Unit vector from the planet's centre through face coordinates (u, v)."""
    return spherify(cube_point(face, u, v))


def chunk_bounds(level, x, y):
    """(u0, v0, size) of a chunk in face coordinates."""
    size = 2.0 / (1 << level)
    return -1.0 + x * size, -1.0 + y * size, size


def chunk_uv(level, x, y, i, j, quads):
    """Face coordinates of vertex (i, j) of a chunk with `quads` quads a side.
    Vertices on a chunk's edge come out the same for both chunks sharing it."""
    cells = quads << level                       # quads across the whole face
    return -1.0 + 2.0 * (x * quads + i) / cells, -1.0 + 2.0 * (y * quads + j) / cells


def chunk_mesh(face, level, x, y, quads, radius):
    """One chunk of the sphere of `radius`. Returns its centre (planet
    space) and, relative to that centre, its positions, plus normals (the
    directions from the planet's centre), face (u, v) per vertex, and
    indices, counter-clockwise seen from outside."""
    u0, v0, size = chunk_bounds(level, x, y)
    c = direction(face, u0 + 0.5 * size, v0 + 0.5 * size)
    centre = (c[0] * radius, c[1] * radius, c[2] * radius)

    positions, normals, uvs, indices = [], [], [], []
    for j in range(quads + 1):
        for i in range(quads + 1):
            u, v = chunk_uv(level, x, y, i, j, quads)
            d = direction(face, u, v)
            positions.append((d[0] * radius - centre[0], d[1] * radius - centre[1], d[2] * radius - centre[2]))
            normals.append(d)
            uvs.append((u, v))
    row = quads + 1
    for j in range(quads):
        for i in range(quads):
            a = j * row + i             # +i is along u, +j along v, and cross(u, v) points outward
            b, c2, d2 = a + 1, a + row + 1, a + row
            indices += [a, b, c2, a, c2, d2]
    return centre, positions, normals, uvs, indices

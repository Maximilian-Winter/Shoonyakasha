"""
Cube-sphere geometry in Python floats (64-bit).

Each of the six cube faces has a frame (normal, u axis, v axis) with
cross(u, v) == normal. A point of a face is normal + u * u_axis + v * v_axis
with u, v in [-1, 1]; `spherify` maps it onto the unit sphere.

A chunk is the square of a face at quadtree `level` with integer
coordinates (x, y), 0 .. 2**level - 1 along u and v. `chunk_mesh` needs
numpy; the rest is plain Python.
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


def face_uv(d):
    """(face, u, v) of the unit direction `d`: the face whose normal is
    nearest to it, and the face coordinates `direction` maps onto it,
    found by Newton's method from where `d` meets the cube."""
    face = max(range(6), key=lambda f: sum(FACES[f][0][k] * d[k] for k in range(3)))
    n, a, b = FACES[face]
    along = sum(n[k] * d[k] for k in range(3))
    u = sum(a[k] * d[k] for k in range(3)) / along
    v = sum(b[k] * d[k] for k in range(3)) / along
    h = 1e-7
    for _ in range(8):
        p = direction(face, u, v)
        pu, pv = direction(face, u + h, v), direction(face, u, v + h)
        du = [(pu[k] - p[k]) / h for k in range(3)]
        dv = [(pv[k] - p[k]) / h for k in range(3)]
        e = [d[k] - p[k] for k in range(3)]
        # Least squares: [du dv] (su, sv) = e.
        a11 = sum(c * c for c in du)
        a12 = sum(du[k] * dv[k] for k in range(3))
        a22 = sum(c * c for c in dv)
        b1 = sum(du[k] * e[k] for k in range(3))
        b2 = sum(dv[k] * e[k] for k in range(3))
        det = a11 * a22 - a12 * a12
        su, sv = (a22 * b1 - a12 * b2) / det, (a11 * b2 - a12 * b1) / det
        u, v = u + su, v + sv
        if abs(su) + abs(sv) < 1e-15:
            break
    return face, u, v


def chunk_bounds(level, x, y):
    """(u0, v0, size) of a chunk in face coordinates."""
    size = 2.0 / (1 << level)
    return -1.0 + x * size, -1.0 + y * size, size


def chunk_uv(level, x, y, i, j, quads):
    """Face coordinates of vertex (i, j) of a chunk with `quads` quads a side.
    Vertices on a chunk's edge come out the same for both chunks sharing it."""
    cells = quads << level                       # quads across the whole face
    return -1.0 + 2.0 * (x * quads + i) / cells, -1.0 + 2.0 * (y * quads + j) / cells


def chunk_centre(face, level, x, y, radius):
    """The point of the sphere of `radius` at the middle of a chunk's face
    coordinates."""
    u0, v0, size = chunk_bounds(level, x, y)
    c = direction(face, u0 + 0.5 * size, v0 + 0.5 * size)
    return (c[0] * radius, c[1] * radius, c[2] * radius)


def chunk_mesh(face, level, x, y, quads, radius, skirt_depth=0.0):
    """One chunk of the sphere of `radius`, built with numpy. Returns its
    centre (planet space, a tuple) and numpy arrays: positions relative to
    that centre (N x 3), normals (the directions from the planet's centre),
    face (u, v) per vertex, and indices (uint32), counter-clockwise seen
    from outside.

    The first (quads + 1)**2 vertices are the grid, row by row along u, then
    rows along v. With `skirt_depth` > 0 each edge vertex has a copy that
    much nearer the centre, after the grid, and walls join each edge to its
    copies, drawn from both sides. The directions are the same, to the bit,
    as `direction` gives for `chunk_uv`."""
    import numpy as np

    n, a, b = (np.array(axis) for axis in FACES[face])
    cells = quads << level
    k = np.arange(quads + 1, dtype=np.float64)
    u = -1.0 + 2.0 * (x * quads + k) / cells
    v = -1.0 + 2.0 * (y * quads + k) / cells
    uu, vv = np.meshgrid(u, v)                   # [j, i]: u from i, v from j
    p = n + uu[..., None] * a + vv[..., None] * b
    px, py, pz = p[..., 0], p[..., 1], p[..., 2]
    x2, y2, z2 = px * px, py * py, pz * pz
    dirs = np.stack([px * np.sqrt(1.0 - 0.5 * y2 - 0.5 * z2 + y2 * z2 / 3.0),
                     py * np.sqrt(1.0 - 0.5 * z2 - 0.5 * x2 + z2 * x2 / 3.0),
                     pz * np.sqrt(1.0 - 0.5 * x2 - 0.5 * y2 + x2 * y2 / 3.0)], axis=-1).reshape(-1, 3)
    uvs = np.stack([uu, vv], axis=-1).reshape(-1, 2)

    centre = chunk_centre(face, level, x, y, radius)
    c = np.array(centre)
    row = quads + 1
    grid = np.arange(row * row).reshape(row, row)
    quad_a = grid[:-1, :-1].ravel()              # +i is along u, +j along v, and cross(u, v) points outward
    quad_b, quad_c, quad_d = quad_a + 1, quad_a + row + 1, quad_a + row
    indices = [np.stack([quad_a, quad_b, quad_c, quad_a, quad_c, quad_d], axis=-1).ravel()]
    positions = [dirs * radius - c]
    normals, uv_list = [dirs], [uvs]

    if skirt_depth > 0.0:
        base = row * row
        for edge in (grid[0, :], grid[:, -1], grid[-1, ::-1], grid[::-1, 0]):
            skirt = base + np.arange(row)
            positions.append(dirs[edge] * (radius - skirt_depth) - c)
            normals.append(dirs[edge])
            uv_list.append(uvs[edge])
            e0, e1, s0, s1 = edge[:-1], edge[1:], skirt[:-1], skirt[1:]
            indices.append(np.stack([e0, s0, s1, e0, s1, e1,          # one side
                                     e0, s1, s0, e0, e1, s1], axis=-1).ravel())   # the other
            base += row

    return (centre, np.concatenate(positions), np.concatenate(normals), np.concatenate(uv_list),
            np.concatenate(indices).astype(np.uint32))

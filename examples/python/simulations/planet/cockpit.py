"""
The ship's cockpit, built from boxes in the camera's space: metres, the eye
at the origin, forward -Z, up +Y, right +X. The cockpit is drawn with the
camera's rotation and no translation, so it moves with the view.

`cockpit_mesh()` gives triangles (positions, normals, colours) with flat
normals. `SCREENS` places the instrument displays: their centre, their
right and up directions, and their size.
"""

import math

DASH = (0.045, 0.048, 0.055)
FRAME = (0.07, 0.075, 0.085)
CONSOLE = (0.035, 0.037, 0.042)


def _add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _normalize(a):
    n = math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])
    return (a[0] / n, a[1] / n, a[2] / n)


def oriented_box(centre, x_axis, y_axis, size, colour, out):
    """A box at `centre` with its edges along the unit vectors `x_axis`,
    `y_axis` and their cross product, `size` long along each; triangles
    counter-clockwise seen from outside, appended to `out`."""
    z_axis = _cross(x_axis, y_axis)
    axes = (x_axis, y_axis, z_axis)
    half = [s * 0.5 for s in size]
    for a in range(3):
        for sign in (1.0, -1.0):
            n = _scale(axes[a], sign)
            u, v = axes[(a + 1) % 3], axes[(a + 2) % 3]      # u x v == axes[a]
            hu, hv = half[(a + 1) % 3], half[(a + 2) % 3]
            if sign < 0.0:
                u, hu, v, hv = v, hv, u, hu                  # keep u x v == n
            face = _add(centre, _scale(n, half[a]))
            corners = [_add(face, _add(_scale(u, su * hu), _scale(v, sv * hv)))
                       for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
            for k in (0, 1, 2, 0, 2, 3):
                out[0].append(corners[k])
                out[1].append(n)
                out[2].append(colour)


def box(centre, size, colour, out):
    oriented_box(centre, (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), size, colour, out)


def beam(start, end, width, colour, out):
    """A square beam `width` thick from `start` to `end`."""
    along = _sub(end, start)
    length = math.sqrt(sum(c * c for c in along))
    axis = _scale(along, 1.0 / length)
    helper = (0.0, 1.0, 0.0) if abs(axis[1]) < 0.9 else (1.0, 0.0, 0.0)
    side = _normalize(_cross(helper, axis))
    up = _cross(axis, side)
    # oriented_box's third axis is side x up, which is `axis`: the length runs along the beam.
    oriented_box(_scale(_add(start, end), 0.5), side, up, (width, width, length), colour, out)


def cockpit_mesh():
    out = ([], [], [])
    # The dashboard under the view, and the consoles beside the seat.
    box((0.0, -0.52, -0.78), (1.6, 0.25, 0.5), DASH, out)
    for side in (-1.0, 1.0):
        box((side * 0.66, -0.62, -0.15), (0.3, 0.32, 0.95), CONSOLE, out)
    # The canopy frame: pillars from the dashboard's corners to the roof, the
    # bow across the top, rails back along the roof and sills along the sides.
    for side in (-1.0, 1.0):
        beam((side * 0.78, -0.40, -0.98), (side * 0.44, 0.44, -0.56), 0.05, FRAME, out)
        beam((side * 0.44, 0.44, -0.56), (side * 0.52, 0.36, 0.45), 0.05, FRAME, out)
        beam((side * 0.80, -0.40, -0.98), (side * 0.82, -0.36, 0.45), 0.06, FRAME, out)
    beam((-0.44, 0.44, -0.56), (0.44, 0.44, -0.56), 0.05, FRAME, out)
    # The displays' bezels, behind where SCREENS puts the displays.
    for centre, right, up, (w, h) in SCREENS:
        normal = _cross(right, up)
        oriented_box(_sub(centre, _scale(normal, 0.012)), right, up, (w + 0.03, h + 0.03, 0.02), CONSOLE, out)
    return out


def _tilted_up(degrees):
    """Up leaning `degrees` forward (towards -Z), so that a display with
    right +X faces up towards the eye."""
    t = math.radians(degrees)
    return (0.0, math.cos(t), -math.sin(t))


# (centre, right, up, (width, height)) of each display, metres, camera space.
SCREENS = [
    ((-0.24, -0.28, -0.68), (1.0, 0.0, 0.0), _tilted_up(25.0), (0.225, 0.22)),
    ((0.24, -0.28, -0.68), (1.0, 0.0, 0.0), _tilted_up(25.0), (0.225, 0.22)),
]

"""
Cascaded shadow maps for the sun, fitted in Python floats (64-bit).

Each cascade covers a slice of the view, between two distances along the
view direction, with an orthographic projection along the sun's
direction: a square window around the slice's bounding sphere (its size
depends only on the slice's distances, the field of view and the aspect
ratio, so turning the camera does not change it), reaching
`caster_reach` metres further towards the sun so that what stands between
the slice and the sun casts into it.

The engine's camera stays at the origin and everything is drawn relative
to it, so a cascade's matrix maps a position relative to the camera to
the shadow map's clip space (depth 0 nearest the sun, 1 furthest). The
window is snapped to whole texels in planet space: a fixed point of the
planet keeps its place within its texel as the camera moves, so shadow
edges do not shimmer.
"""

import math
from dataclasses import dataclass


def _add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _normalize(a):
    return _scale(a, 1.0 / math.sqrt(_dot(a, a)))


@dataclass
class Cascade:
    near: float                  # metres along the view where the cascade starts
    far: float                   # and ends
    matrix: tuple                # four columns of four: camera-relative position to clip space
    texel: float                 # metres across one texel


def light_basis(towards_sun):
    """(right, up, back) of the sun's view: back points towards the sun."""
    back = _normalize(towards_sun)
    helper = (0.0, 1.0, 0.0) if abs(back[1]) < 0.99 else (1.0, 0.0, 0.0)
    right = _normalize(_cross(helper, back))
    return right, _cross(back, right), back


def slice_sphere(near, far, fov_y_degrees, aspect):
    """(distance along the view to the centre, radius) of the smallest
    sphere centred on the view axis that holds the view's slice between
    `near` and `far`."""
    k = math.tan(math.radians(fov_y_degrees) * 0.5) * math.sqrt(1.0 + aspect * aspect)
    centre = 0.5 * (near + far) * (1.0 + k * k)
    if centre >= far:
        return far, max(far * k, math.hypot(far - near, near * k))
    return centre, math.hypot(centre - near, near * k)


def fit_cascades(camera, forward, towards_sun, fov_y_degrees, aspect, splits, resolution, caster_reach):
    """The cascades between consecutive distances of `splits` for a camera
    at `camera` (planet space) looking along the unit `forward`."""
    right, up, back = light_basis(towards_sun)
    cascades = []
    for near, far in zip(splits, splits[1:]):
        along, radius = slice_sphere(near, far, fov_y_degrees, aspect)
        radius = math.ceil(radius * 16.0) / 16.0           # so rounding cannot change the texel size
        texel = 2.0 * radius / resolution
        # The window's centre in planet space, snapped to whole texels across the sun's view.
        centre = _add(camera, _scale(forward, along))
        x = math.floor(_dot(centre, right) / texel) * texel
        y = math.floor(_dot(centre, up) / texel) * texel
        snapped = _add(_add(_scale(right, x), _scale(up, y)), _scale(back, _dot(centre, back)))
        relative = _sub(snapped, camera)                    # small: subtracted in double
        depth = 2.0 * radius + caster_reach
        # clip.x = dot(p - relative, right) / radius; clip.y likewise with up;
        # clip.z = (radius + caster_reach - dot(p - relative, back)) / depth.
        sx, sy, sz = 1.0 / radius, 1.0 / radius, -1.0 / depth
        tx = -_dot(relative, right) * sx
        ty = -_dot(relative, up) * sy
        tz = (radius + caster_reach + _dot(relative, back)) / depth
        matrix = ((right[0] * sx, up[0] * sy, back[0] * sz, 0.0),
                  (right[1] * sx, up[1] * sy, back[1] * sz, 0.0),
                  (right[2] * sx, up[2] * sy, back[2] * sz, 0.0),
                  (tx, ty, tz, 1.0))
        cascades.append(Cascade(near, far, matrix, texel))
    return cascades


def transform(matrix, point):
    """`matrix` (four columns of four) applied to `point` with w = 1."""
    return tuple(sum(matrix[c][r] * (point[c] if c < 3 else 1.0) for c in range(4)) for r in range(4))

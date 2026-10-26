"""
Vectors as tuples of Python floats (64-bit), and the frames built from them.
"""

import math


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def length(a):
    return math.sqrt(dot(a, a))


def normalize(a):
    return scale(a, 1.0 / length(a))


def rotate(v, axis, angle):
    """`v` turned by `angle` radians about the unit vector `axis` (Rodrigues)."""
    c, s = math.cos(angle), math.sin(angle)
    return add(add(scale(v, c), scale(cross(axis, v), s)), scale(axis, dot(axis, v) * (1.0 - c)))


def local_frame(up):
    """(east, north) at the point of the sphere whose direction is `up`."""
    east = cross((0.0, 1.0, 0.0), up)
    if length(east) < 1e-9:
        east = (1.0, 0.0, 0.0)
    east = normalize(east)
    return east, cross(up, east)


def euler_from_basis(right, up, forward):
    """The engine's Euler rotation (x pitch, y yaw, z roll; applied Y, then
    X, then Z) whose rotation matrix has columns right, up and -forward."""
    back = scale(forward, -1.0)
    pitch = math.asin(max(-1.0, min(1.0, -back[1])))
    yaw = math.atan2(back[0], back[2])
    roll = math.atan2(right[1], up[1])
    return (pitch, yaw, roll)

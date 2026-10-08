"""Small vector and camera helpers on plain tuples.

    from shoonyakasha import mathutil as mu

    engine.scene.set_rotation(camera, mu.look_rotation(eye, target))
    eye = mu.orbit(target, distance=6.0, azimuth=35.0, elevation=10.0)

Vectors are tuples of floats. Rotations are the engine's Euler angles in
radians, (pitch, yaw, roll), as taken by Scene.set_rotation: an entity with
rotation (0, 0, 0) faces -Z, positive pitch turns it up and positive yaw
turns it from -Z towards -X.
"""

import math

__all__ = [
    "lerp", "lerp3", "smoothstep", "add", "scale", "normalize",
    "rotation_facing", "look_rotation", "yaw_point", "orbit",
]


def lerp(a, b, t):
    """a at t = 0, b at t = 1, linear in between and beyond."""
    return a + (b - a) * t


def lerp3(a, b, t):
    """lerp() applied to each component of two vectors."""
    return tuple(lerp(x, y, t) for x, y in zip(a, b))


def smoothstep(t):
    """Eases t from 0 to 1 with zero slope at both ends; t is clamped to [0, 1]."""
    t = min(max(t, 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def scale(v, s):
    return tuple(x * s for x in v)


def normalize(v):
    """v scaled to length 1; a zero vector is returned unchanged."""
    n = math.sqrt(sum(x * x for x in v)) or 1.0
    return tuple(x / n for x in v)


def rotation_facing(direction):
    """Euler rotation (pitch, yaw, 0) whose forward is `direction`.

    Matches TransformComponent::rotationFacing in include/ECS/Core.h. Straight up or
    down gives pitch +-pi/2 with an arbitrary yaw; a zero direction gives a
    finite but arbitrary rotation.
    """
    x, y, z = normalize(direction)
    return (math.asin(max(-1.0, min(1.0, y))), math.atan2(-x, -z), 0.0)


def look_rotation(eye, target):
    """Euler rotation that points an entity at `eye` towards `target`."""
    return rotation_facing(tuple(t - e for t, e in zip(target, eye)))


def yaw_point(p, degrees):
    """p rotated about +Y the way the engine's yaw turns: +Z towards +X for positive angles."""
    a = math.radians(degrees)
    c, s = math.cos(a), math.sin(a)
    return (c * p[0] + s * p[2], p[1], -s * p[0] + c * p[2])


def orbit(target, distance, azimuth, elevation):
    """A point `distance` from target; azimuth 0 is towards +Z, 90 towards +X; degrees."""
    a, e = math.radians(azimuth), math.radians(elevation)
    return add(target, (distance * math.cos(e) * math.sin(a), distance * math.sin(e),
                        distance * math.cos(e) * math.cos(a)))

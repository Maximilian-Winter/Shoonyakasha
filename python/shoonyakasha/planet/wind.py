"""
The wind over the planet (PlanetWorld.wind_at): a prevailing wind that
turns slowly with the world's clock, stronger with height, with gusts.

`prevailing(time)` is the prevailing direction, a unit vector in planet
space: BASE_DIRECTION turned about TURN_AXIS once every TURN_PERIOD
seconds. At a point whose unit up is `up` the wind blows along that
direction's part along the ground, so it is weaker towards the two
points where the direction is straight up or down and still there; the
clouds drift with the same direction (PlanetWorld.blow_clouds).

`profile(height)` is the share of the wind's speed at `height` metres
above the ground: ln(h / ROUGHNESS) / ln(REFERENCE_HEIGHT / ROUGHNESS)
with h at least MIN_HEIGHT, so 0.25 at 1 m, 0.5 at 10 m and 1 at 1 km.

`gust(point, time, along, speed)` is GUST_SHARE of `speed` times value
noise (`noise3`, -1 to 1) over GUST_SCALE metres, the noise carried
along the unit `along` at `speed`, so the gusts travel with the wind.

`wind_at(point, up, time, speed, height)` is the wind's velocity (m/s,
planet space) at `point` (metres from the planet's centre) with unit
`up`, `height` metres above the ground, for a wind of `speed` m/s at
REFERENCE_HEIGHT: along the ground's part of the prevailing direction,
at `speed` times its length times the profile (the steady speed), plus
the gust of that speed along it.
"""

import math

from shoonyakasha.planet.vec import dot, length, normalize, rotate, scale, sub

BASE_DIRECTION = (0.0, 0.8, 0.6)
TURN_AXIS = (1.0, 0.0, 0.0)        # perpendicular to BASE_DIRECTION
TURN_PERIOD = 6.0 * 3600.0         # s of the world's clock for one turn
ROUGHNESS = 0.1                    # m
REFERENCE_HEIGHT = 1_000.0         # m at which the wind blows at its speed
MIN_HEIGHT = 1.0                   # m: lower heights count as this
GUST_SHARE = 0.25
GUST_SCALE = 200.0                 # m across a gust


def prevailing(time):
    return normalize(rotate(BASE_DIRECTION, TURN_AXIS, 2.0 * math.pi * time / TURN_PERIOD))


def profile(height):
    h = max(MIN_HEIGHT, height)
    return math.log(h / ROUGHNESS) / math.log(REFERENCE_HEIGHT / ROUGHNESS)


def _hash(i, j, k):
    """A value from -1 to 1 for the lattice point (i, j, k)."""
    h = (i * 73856093) ^ (j * 19349663) ^ (k * 83492791)
    h &= 0xFFFFFFFF
    h ^= h >> 13
    h = (h * 1274126177) & 0xFFFFFFFF
    h ^= h >> 16
    return h / 2147483647.5 - 1.0


def _smooth(t):
    return t * t * (3.0 - 2.0 * t)


def noise3(x, y, z):
    """Value noise from -1 to 1, smooth between the integer lattice's points."""
    i, j, k = math.floor(x), math.floor(y), math.floor(z)
    fx, fy, fz = _smooth(x - i), _smooth(y - j), _smooth(z - k)

    def lerp(a, b, t):
        return a + (b - a) * t

    x00 = lerp(_hash(i, j, k), _hash(i + 1, j, k), fx)
    x10 = lerp(_hash(i, j + 1, k), _hash(i + 1, j + 1, k), fx)
    x01 = lerp(_hash(i, j, k + 1), _hash(i + 1, j, k + 1), fx)
    x11 = lerp(_hash(i, j + 1, k + 1), _hash(i + 1, j + 1, k + 1), fx)
    return lerp(lerp(x00, x10, fy), lerp(x01, x11, fy), fz)


def gust(point, time, along, speed):
    p = sub(point, scale(along, speed * time))
    return GUST_SHARE * speed * noise3(p[0] / GUST_SCALE, p[1] / GUST_SCALE, p[2] / GUST_SCALE)


def wind_at(point, up, time, speed, height):
    direction = prevailing(time)
    tangent = sub(direction, scale(up, dot(direction, up)))
    share = length(tangent)
    if share < 1e-9 or speed <= 0.0:
        return (0.0, 0.0, 0.0)
    along = scale(tangent, 1.0 / share)
    steady = speed * share * profile(height)
    return scale(along, steady + gust(point, time, along, steady))

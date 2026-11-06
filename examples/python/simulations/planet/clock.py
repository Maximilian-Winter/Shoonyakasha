"""
The day (planet.py): the planet does not turn, so the physics and the
orbits stay as they are; the sun goes round it instead, about an axis
tilted AXIS_TILT from the site's north towards its up, once every
DAY_LENGTH seconds of the world's clock.

`sun_direction(time, up, east, north)` is the unit direction towards the
sun in planet space, for the site's frame: noon at the site at time 0
(and every DAY_LENGTH on), the sun rising in the east and setting in the
west, at most 90 - AXIS_TILT degrees up at the site. `site_angles` gives a
direction's elevation and azimuth seen from the site, as
PlanetWorld.sun_elevation and sun_azimuth take them. `hour(time)` is the
site's hour (0 to 24, noon at 12), `local_hour(direction, time, ...)` the
hour anywhere on the planet, `day(time)` the day's index (turning at
06:00 at the site), and `time_of(hour, now)` the time nearest `now` at
which the site has that hour.

`exposure_scale(elevation)` is the exposure's factor with the sun at
`elevation` (degrees) seen from the camera: 1 at DAY_ELEVATION and above,
NIGHT_SCALE at NIGHT_ELEVATION and below, eased in between in its
logarithm. `set_sun(world, time, camera)` puts a PlanetWorld's sun and
exposure there; `unset_sun(world)` leaves its exposure unscaled.
"""

import math

from shoonyakasha.planet.vec import add, cross, dot, length, normalize, rotate, scale, sub

DAY_LENGTH = 40 * 60.0           # s of the world's clock
AXIS_TILT = math.radians(18.0)
DAY_ELEVATION, NIGHT_ELEVATION = 0.0, -10.0   # degrees
NIGHT_SCALE = 6.0


def sun_direction(time, up, east, north):
    axis = normalize(add(scale(north, math.cos(AXIS_TILT)), scale(up, math.sin(AXIS_TILT))))
    noon = normalize(sub(up, scale(axis, dot(up, axis))))
    return rotate(noon, axis, -2.0 * math.pi * time / DAY_LENGTH)


def site_angles(direction, up, east, north):
    """(elevation, azimuth) in degrees of a unit direction seen from the
    site, the azimuth from east towards north, 0 to 360."""
    elevation = math.degrees(math.asin(max(-1.0, min(1.0, dot(direction, up)))))
    azimuth = math.degrees(math.atan2(dot(direction, north), dot(direction, east))) % 360.0
    return elevation, azimuth


def hour(time):
    return (12.0 + 24.0 * time / DAY_LENGTH) % 24.0


def local_hour(direction, time, up, east, north):
    """The hour (0 to 24, noon at 12) at the unit `direction` from the
    planet's centre at `time`, for the site's frame: how far the sun has
    turned about the axis past that place's noon."""
    axis = normalize(add(scale(north, math.cos(AXIS_TILT)), scale(up, math.sin(AXIS_TILT))))
    sun = sun_direction(time, up, east, north)
    a = sub(direction, scale(axis, dot(direction, axis)))
    s = sub(sun, scale(axis, dot(sun, axis)))
    if length(a) < 1e-9 or length(s) < 1e-9:
        return hour(time)
    angle = math.atan2(dot(cross(a, s), axis), dot(a, s))
    return (12.0 - 24.0 * angle / (2.0 * math.pi)) % 24.0


def day(time):
    """The day's index at `time`: it turns at dawn, 06:00 at the site (day 0 from -DAY_LENGTH / 4)."""
    return math.floor((time + 0.25 * DAY_LENGTH) / DAY_LENGTH)


def time_of(hour_wanted, now):
    """The time nearest `now` at which the site's hour is `hour_wanted`."""
    shift = ((hour_wanted - hour(now) + 12.0) % 24.0 - 12.0) * DAY_LENGTH / 24.0
    return now + shift


def exposure_scale(elevation):
    x = max(0.0, min(1.0, (DAY_ELEVATION - elevation) / (DAY_ELEVATION - NIGHT_ELEVATION)))
    return math.exp(math.log(NIGHT_SCALE) * x * x * (3.0 - 2.0 * x))


def set_sun(world, time, camera):
    """A PlanetWorld's sun (sun_elevation and sun_azimuth, seen from its
    site) where sun_direction has it at `time`, and its exposure_scale for
    the sun's elevation seen from `camera` (planet space); uploaded."""
    _, up, east, north = world.site_frame()
    sun = sun_direction(time, up, east, north)
    world.sun_elevation, world.sun_azimuth = site_angles(sun, up, east, north)
    radial = normalize(world.body_under(camera).relative(camera))
    world.exposure_scale = exposure_scale(math.degrees(math.asin(max(-1.0, min(1.0, dot(sun, radial))))))
    world.upload_sky()


def unset_sun(world):
    """A PlanetWorld's exposure unscaled, uploaded when it was not."""
    if world.exposure_scale != 1.0:
        world.exposure_scale = 1.0
        world.upload_sky()

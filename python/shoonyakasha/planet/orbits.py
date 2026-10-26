"""
Bodies, their gravity and air, and the orbits around them, in Python floats
(64-bit) with the planet's centre at the origin.

`Planet` is the central body: gravity falling off with the square of the
distance from its centre, and air whose density falls off exponentially
with height. A `Moon` circles it on rails and pulls too; `Planet.time`
places it. Orbits are reckoned around the moon inside its sphere of
influence and around the planet outside it (`reference`). `orbit` gives a
path's periapsis and apoapsis, `orbit_path` points along it.
"""

import math
from dataclasses import dataclass

from .vec import add, cross, dot, length, normalize, scale, sub


# ── Bodies ─────────────────────────────────────────────────────────────────

@dataclass
class Planet:
    radius: float = 100_000.0           # m
    surface_gravity: float = 4.905      # m/s^2 at the radius
    air_density: float = 1.225          # kg/m^3 at the radius
    scale_height: float = 2_500.0       # m; the atmosphere's Rayleigh scale height
    density_factor: float = 1.0         # times the air's density
    moon: object = None                 # a Moon, which pulls too, where it is at `time`
    time: float = 0.0                   # seconds of flight, for the moon's place
    moon_pulls: bool = True             # whether the moon's gravity counts

    @property
    def gm(self):
        return self.surface_gravity * self.radius * self.radius

    def gravity(self, position):
        r = length(position)
        g = scale(position, -self.gm / (r * r * r))
        if self.moon is not None and self.moon_pulls:
            g = add(g, self.moon.gravity(position, self.time))
        return g

    def surface_motion(self, position, dt=0.0):
        """(velocity, acceleration, displacement over the next `dt` seconds)
        of the surface of the body `position` is reckoned around
        (reference): the moon's within its sphere of influence, else the
        planet's, which are 0."""
        moon = self.moon
        if moon is not None and length(sub(position, moon.position(self.time))) < moon.influence:
            return (moon.velocity(self.time), moon.acceleration(self.time),
                    sub(moon.position(self.time + dt), moon.position(self.time)))
        return (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)

    def density(self, position):
        height = max(length(position) - self.radius, -2_000.0)
        return self.air_density * self.density_factor * math.exp(-height / self.scale_height)


@dataclass
class Moon:
    """A moon on a circular orbit around the planet's centre: at `time` t
    it is `distance` from it at the angle phase + rate * t in the plane of
    the unit vectors `u` (where it is at angle 0) and `v` (a quarter turn
    on, the way it moves). Everything is reckoned from the planet's centre,
    which the moon pulls as well: the moon circles it at the rate for both
    their masses, and its pull on the ship is less its pull on the planet
    (the frame's own acceleration). No air."""
    radius: float = 25_000.0
    surface_gravity: float = 0.981
    distance: float = 600_000.0
    phase: float = 0.0
    u: tuple = (1.0, 0.0, 0.0)
    v: tuple = (0.0, 1.0, 0.0)
    primary_gm: float = 4.905 * 100_000.0 ** 2

    @property
    def gm(self):
        return self.surface_gravity * self.radius * self.radius

    @property
    def rate(self):
        """Radians a second along its orbit around the planet."""
        return math.sqrt((self.primary_gm + self.gm) / self.distance ** 3)

    def position(self, time):
        angle = self.phase + self.rate * time
        return add(scale(self.u, self.distance * math.cos(angle)), scale(self.v, self.distance * math.sin(angle)))

    def velocity(self, time):
        angle = self.phase + self.rate * time
        speed = self.distance * self.rate
        return add(scale(self.u, -speed * math.sin(angle)), scale(self.v, speed * math.cos(angle)))

    def acceleration(self, time):
        """Its centre's acceleration: rate^2 * distance towards the planet's centre."""
        return scale(self.position(time), -self.rate * self.rate)

    def gravity(self, position, time):
        """Its pull at `position` relative to the planet's centre: towards
        the moon, less the planet's own fall towards it."""
        centre = self.position(time)
        offset = sub(position, centre)
        r = length(offset)
        d = length(centre)
        return sub(scale(offset, -self.gm / (r * r * r)), scale(centre, self.gm / (d * d * d)))

    @property
    def influence(self):
        """Radius of its sphere of influence: distance * (its GM / the planet's) ** 0.4."""
        return self.distance * (self.gm / self.primary_gm) ** 0.4


def reference(planet, position, velocity):
    """(body, position, velocity) relative to the body whose gravity the
    ship's orbit is reckoned around: the moon inside its sphere of
    influence, else the planet."""
    moon = planet.moon
    if moon is not None:
        centre = moon.position(planet.time)
        if length(sub(position, centre)) < moon.influence:
            return moon, sub(position, centre), sub(velocity, moon.velocity(planet.time))
    return planet, position, velocity


# ── Orbits ─────────────────────────────────────────────────────────────────

def orbit(planet, position, velocity):
    """(periapsis, apoapsis) heights above the radius of the path the ship
    would coast along around the planet (or any body with `gm` and
    `radius`, given the position and velocity relative to it); apoapsis is
    inf when it would escape."""
    r = length(position)
    v2 = dot(velocity, velocity)
    energy = 0.5 * v2 - planet.gm / r
    h = length(cross(position, velocity))
    e = math.sqrt(max(0.0, 1.0 + 2.0 * energy * h * h / (planet.gm * planet.gm)))
    if energy >= 0.0:
        periapsis = h * h / (planet.gm * (1.0 + e))
        return periapsis - planet.radius, math.inf
    a = -planet.gm / (2.0 * energy)
    return a * (1.0 - e) - planet.radius, a * (1.0 + e) - planet.radius


def orbit_path(planet, position, velocity, count=256, reach=10.0):
    """`count` points along the path a body would coast along, from its
    position onwards in the direction it moves: a whole ellipse, evenly
    spaced in eccentric anomaly, when its energy is negative; otherwise the
    open branch out to `reach` times its present distance from the centre. An empty list when
    it moves straight towards or away from the centre (no plane to draw
    in)."""
    mu = planet.gm
    r = length(position)
    h = cross(position, velocity)
    if length(h) < 1e-9 * r * max(length(velocity), 1e-9):
        return []
    eccentricity_vector = sub(scale(cross(velocity, h), 1.0 / mu), scale(position, 1.0 / r))
    e = length(eccentricity_vector)
    # Perifocal frame: P towards periapsis (or the position on a circle), Q a quarter turn on.
    along = normalize(eccentricity_vector) if e > 1e-9 else scale(position, 1.0 / r)
    normal = normalize(h)
    across = cross(normal, along)
    p = dot(h, h) / mu                                   # the conic's semi-latus rectum
    nu0 = math.atan2(dot(position, across), dot(position, along))   # the ship's true anomaly
    points = []
    energy = 0.5 * dot(velocity, velocity) - mu / r
    if energy < 0.0:
        # Bound: an ellipse, also where rounding puts e at 1 or just above for a nearly radial
        # motion. The semi-axes from the energy and p, and the ship's eccentric anomaly from
        # its distance and radial speed, which keep their precision for thin ellipses.
        e = min(e, 1.0 - 1e-15)
        a = -mu / (2.0 * energy)
        b = math.sqrt(a * p)
        e0 = math.atan2(dot(position, velocity) / math.sqrt(mu * a), 1.0 - r / a)
        for i in range(count):
            angle = e0 + 2.0 * math.pi * i / (count - 1)
            x, y = a * (math.cos(angle) - e), b * math.sin(angle)
            points.append(add(scale(along, x), scale(across, y)))
        return points
    # Open: from the ship's true anomaly to where the distance reaches `reach` times r.
    limit = math.acos(max(-1.0, min(1.0, (p / (reach * r) - 1.0) / e)))
    end = max(limit, nu0)
    for i in range(count):
        nu = nu0 + (end - nu0) * i / (count - 1)
        radius = p / (1.0 + e * math.cos(nu))
        points.append(add(scale(along, radius * math.cos(nu)), scale(across, radius * math.sin(nu))))
    return points


def circular_speed(planet, height):
    return math.sqrt(planet.gm / (planet.radius + height))

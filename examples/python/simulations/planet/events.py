"""
Meteor showers and storms (walker.py): when the next shower comes, where
its meteors strike, the paths they fly in, the craters kept and how hot a
meteorite still is; and when the next storm comes and where it goes.

`schedule(now, rng, player, radius, start=None)` plans a Shower on a body
of `radius` (m). It starts at `start`, or after a gap drawn from an
exponential of mean MEAN_GAP after `now`, at least MIN_GAP. Its centre
lies AREA_NEAR to AREA_FAR along the ground from the unit direction
`player`, at a bearing drawn uniformly. Its impacts (Impact), sorted by
time, are drawn over SPREAD_SECONDS from its start:
- one large one at the centre: a meteorite of LARGE_MASS kg and a crater
  whose radius grows with that mass over CRATER_RADIUS, DEPTH_SHARE of it
  deep, with a rim RIM_SHARE of it high;
- SMALL_IMPACTS small ones within a radius drawn from SPREAD of the
  centre, each a rock of SMALL_MASS kg and no crater.
Every meteor comes in from about the same bearing (the shower's, give or
take RADIANT_JITTER degrees), at SPEED and ANGLE degrees below the
horizon as it strikes; each has its grade drawn from GRADE.

`velocity(impact, up)` is the meteor's velocity as it strikes at the unit
`up`. `Path(point, velocity, gm)` flies it back from where it strikes
(`point`, from the body's centre) under the body's gravity (`gm`, m^3/s^2),
in steps of PATH_STEP seconds, until it is ENTRY_ALTITUDE above the body's
`radius`: `duration` is its flight's seconds, and `at(before)` its
position `before` seconds before it strikes, between the steps' points.

`keep(craters, crater, time, here, radius)` is the craters ((Crater,
time) pairs) with a new one dug at `time`. When MAX_CRATERS are kept
already, the oldest whose centre lies farther than DROP_FAR along the
ground from the unit direction `here` goes, or the oldest of all when
none does. `crater_record` and `crater_from_record` turn a crater and its
time into the save's record and back.

`schedule_storm(now, rng, player, radius, wind, start=None)` plans a
storm (shoonyakasha.planet.wind.Storm) on a body of `radius`: it starts
at `start`, or after a gap drawn from an exponential of mean
STORM_MEAN_GAP after `now`, at least STORM_MIN_GAP, and lasts a time
drawn from STORM_LIFETIME. Its centre starts STORM_AWAY along the ground
from the unit direction `player`, against the unit `wind` (the wind's
direction along the ground there; a bearing drawn at random when None)
give or take STORM_SPREAD degrees, and moves straight towards where the
player was at STORM_SPEED, so it comes over it; its radius is drawn
from STORM_RADIUS. `storm_record` and `storm_from_record` turn it into
the save's record and back.

`heat(age, cooling)` is how hot a meteorite or a rock is `age` seconds
after it struck: 1 then, falling evenly to 0 over `cooling` seconds
(LARGE_COOLING for the meteorite, SMALL_COOLING for the rocks); one with
heat above 0 is hot.
"""

import math
from dataclasses import asdict, dataclass, field

from shoonyakasha.planet.vec import add, cross, dot, length, local_frame, normalize, scale
from shoonyakasha.planet.wind import Storm

MEAN_GAP = 25.0 * 60.0           # s of the world's clock between showers, on average
MIN_GAP = 5.0 * 60.0             # s, at least
WARNING_SECONDS = 60.0           # s before a shower's start it is announced
AREA_NEAR, AREA_FAR = 3_000.0, 10_000.0   # m along the ground from the player to a shower's centre
SPREAD = (1_000.0, 2_000.0)      # m: the radius around the centre its small impacts land within
SPREAD_SECONDS = 40.0            # s from a shower's start its impacts are drawn over
SMALL_IMPACTS = (4, 10)
LARGE_MASS = (2_000.0, 6_000.0)  # kg
SMALL_MASS = (15.0, 60.0)        # kg
CRATER_RADIUS = (20.0, 40.0)     # m, for the lightest and the heaviest meteorite
DEPTH_SHARE = 0.18               # of a crater's radius it is deep
RIM_SHARE = 0.06                 # of a crater's radius its rim is high
GRADE = (0.4, 0.8)
SPEED = (2_000.0, 4_000.0)       # m/s as it strikes
ANGLE = (25.0, 60.0)             # degrees below the horizon as it strikes
RADIANT_JITTER = 10.0            # degrees each meteor's bearing differs from its shower's, at most
ENTRY_ALTITUDE = 30_000.0        # m above the body's radius a meteor's path begins
PATH_STEP = 0.05                 # s
MAX_CRATERS = 16
DROP_FAR = 20_000.0              # m along the ground from the player a crater must be to be dropped first
LARGE_COOLING = 180.0            # s a meteorite takes to cool
SMALL_COOLING = 60.0             # s a small rock takes
STORM_MEAN_GAP = 40.0 * 60.0     # s of the world's clock between storms, on average
STORM_MIN_GAP = 10.0 * 60.0      # s, at least
STORM_LIFETIME = (40.0 * 60.0, 80.0 * 60.0)   # s
STORM_AWAY = (20_000.0, 35_000.0)   # m upwind of the player its centre starts
STORM_SPREAD = 30.0              # degrees off straight upwind, at most
STORM_SPEED = (6.0, 14.0)        # m/s
STORM_RADIUS = (8_000.0, 15_000.0)   # m


@dataclass
class Impact:
    time: float                  # the world's clock when it strikes
    direction: tuple             # unit, from the body's centre
    mass: float                  # kg of its meteorite or rock
    grade: float
    speed: float                 # m/s as it strikes
    angle: float                 # degrees below the horizon as it strikes
    bearing: float               # degrees clockwise from north it travels towards
    crater: tuple = None         # (radius, depth, rim) in m for the large one, else None

    @property
    def large(self):
        return self.crater is not None


@dataclass
class Shower:
    start: float                 # the world's clock its impacts are drawn from
    centre: tuple                # unit direction of its centre from the body's centre
    impacts: list = field(default_factory=list)


def along_ground(direction, bearing, distance, radius):
    """The unit direction `distance` m along the ground from the unit
    `direction` towards `bearing` (degrees clockwise from north)."""
    east, north = local_frame(direction)
    b = math.radians(bearing)
    heading = add(scale(north, math.cos(b)), scale(east, math.sin(b)))
    a = distance / radius
    return normalize(add(scale(direction, math.cos(a)), scale(heading, math.sin(a))))


def ground_distance(a, b, radius):
    """m along the ground between the unit directions `a` and `b`."""
    return radius * math.acos(max(-1.0, min(1.0, dot(a, b))))


def crater_of(mass):
    """(radius, depth, rim) of the crater a meteorite of `mass` kg digs."""
    low, high = LARGE_MASS
    t = max(0.0, min(1.0, (mass - low) / (high - low)))
    radius = CRATER_RADIUS[0] + t * (CRATER_RADIUS[1] - CRATER_RADIUS[0])
    return radius, DEPTH_SHARE * radius, RIM_SHARE * radius


def schedule(now, rng, player, radius, start=None):
    """The next shower (see the module's docstring); `rng` a random.Random."""
    if start is None:
        start = now + max(MIN_GAP, rng.expovariate(1.0 / MEAN_GAP))
    centre = along_ground(player, rng.uniform(0.0, 360.0), rng.uniform(AREA_NEAR, AREA_FAR), radius)
    radiant = rng.uniform(0.0, 360.0)
    spread = rng.uniform(*SPREAD)

    def meteor(time, direction, mass, crater=None):
        return Impact(time, direction, mass, rng.uniform(*GRADE), rng.uniform(*SPEED), rng.uniform(*ANGLE),
                      (radiant + rng.uniform(-RADIANT_JITTER, RADIANT_JITTER)) % 360.0, crater)

    mass = rng.uniform(*LARGE_MASS)
    impacts = [meteor(start + rng.uniform(0.0, SPREAD_SECONDS), centre, mass, crater_of(mass))]
    for _ in range(rng.randint(*SMALL_IMPACTS)):
        direction = along_ground(centre, rng.uniform(0.0, 360.0), spread * math.sqrt(rng.random()), radius)
        impacts.append(meteor(start + rng.uniform(0.0, SPREAD_SECONDS), direction, rng.uniform(*SMALL_MASS)))
    impacts.sort(key=lambda i: i.time)
    return Shower(start, centre, impacts)


def velocity(impact, up):
    """The meteor's velocity (m/s) as it strikes at the unit `up`."""
    east, north = local_frame(up)
    b, a = math.radians(impact.bearing), math.radians(impact.angle)
    heading = add(scale(north, math.cos(b)), scale(east, math.sin(b)))
    return scale(add(scale(heading, math.cos(a)), scale(up, -math.sin(a))), impact.speed)


class Path:
    def __init__(self, point, velocity, gm, radius):
        """The path flown back from `point` (m from the body's centre) at
        `velocity` under `gm` until ENTRY_ALTITUDE above `radius`."""
        points = [tuple(point)]
        p, v = tuple(point), tuple(velocity)
        h = -PATH_STEP
        top = radius + ENTRY_ALTITUDE
        while length(p) < top and len(points) < 100_000:
            r = length(p)
            v = add(v, scale(p, -gm / (r * r * r) * h))
            p = add(p, scale(v, h))
            points.append(p)
        self.points = points            # from where it strikes back to its entry, PATH_STEP apart
        self.duration = (len(points) - 1) * PATH_STEP

    def at(self, before):
        """Its position `before` seconds before it strikes."""
        s = max(0.0, min(self.duration, before)) / PATH_STEP
        k = min(int(s), len(self.points) - 2)
        if k < 0:
            return self.points[0]
        f = s - k
        a, b = self.points[k], self.points[k + 1]
        return add(scale(a, 1.0 - f), scale(b, f))


def keep(craters, crater, time, here, radius):
    """`craters` ((Crater, time)) with `crater` dug at `time` (see the module's docstring)."""
    kept = list(craters)
    if len(kept) >= MAX_CRATERS:
        far = [c for c in kept if ground_distance(normalize(c[0].direction), here, radius) > DROP_FAR]
        kept.remove(min(far or kept, key=lambda c: c[1]))
    return kept + [(crater, time)]


def crater_record(crater, time, body_name):
    """The save's record of `crater` (world.Crater) dug at `time` on the body named `body_name`."""
    return {"type": "crater", "body": body_name, "direction": list(crater.direction), "radius": crater.radius,
            "depth": crater.depth, "rim": crater.rim, "time": time}


def crater_from_record(record, body_index, make):
    """(crater, time) from a save's record; `make(body, direction, radius, depth, rim)` the crater."""
    return (make(body_index, tuple(record["direction"]), record["radius"], record["depth"], record["rim"]),
            record["time"])


def bearing_of(up, direction):
    """Degrees clockwise from north of `direction` along the ground at the unit `up`."""
    east, north = local_frame(up)
    return math.degrees(math.atan2(dot(direction, east), dot(direction, north))) % 360.0


def schedule_storm(now, rng, player, radius, wind, start=None):
    """The next storm (see the module's docstring); `rng` a random.Random."""
    if start is None:
        start = now + max(STORM_MIN_GAP, rng.expovariate(1.0 / STORM_MEAN_GAP))
    downwind = bearing_of(player, wind) if wind is not None else rng.uniform(0.0, 360.0)
    upwind = downwind + 180.0 + rng.uniform(-STORM_SPREAD, STORM_SPREAD)
    centre = along_ground(player, upwind, rng.uniform(*STORM_AWAY), radius)
    axis = normalize(cross(centre, player))
    return Storm(centre, axis, rng.uniform(*STORM_SPEED), rng.uniform(*STORM_RADIUS), start,
                 start + rng.uniform(*STORM_LIFETIME), radius)


def storm_record(storm):
    r = asdict(storm)
    r["centre"], r["axis"] = list(storm.centre), list(storm.axis)
    return r


def storm_from_record(r):
    return Storm(**dict(r, centre=tuple(r["centre"]), axis=tuple(r["axis"])))


def heat(age, cooling):
    return max(0.0, min(1.0, 1.0 - age / cooling)) if age >= 0.0 else 1.0

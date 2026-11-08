"""
The outposts' layouts: what each outpost is for and where its parts
(shoonyakasha.planet.outpost_kit) stand on its flat site.

`purposes(seed, bodies, fixed)` gives each outpost its purpose, one of
PURPOSES: `bodies` maps a body's name to its outposts' names in order,
and each body's outposts take the purposes in a seeded order (from `seed`
and the body's name), so those on one body differ while there are
purposes left; `fixed` maps an outpost's name to a purpose it always has.

`layout(seed, name, purpose, ...)` places an outpost's parts, the same
for the same `seed` and `name`, as `Placed` values: the part's name, its
centre (x east, z north, metres from the pad's centre), its heading
(radians from north towards east: its ahead) and the builder's params.
The pad (PAD_SIZE across) stands at the centre, its approach a seeded
heading; the terminal and the ore bin beside it, off the approach; then
the purpose's centre parts near the pad and its ring parts between INNER
and OUTER, facing the centre, each footprint GAP clear of the others and
of the approach corridor (CORRIDOR wide, from the centre out along the
approach); a mast near the edge; and barrier segments in arcs along
OUTER + BARRIER_OUT, with gaps and none across the corridor. Solar arrays
lean towards `equator` (a heading). An ore line's heaps are `tint`.

`site_model(placed, body_radius, seed)` builds the parts and puts them
together in the site's frame (x east, y up, z north; y = 0 the flat
site's height at the pad), each part lowered by the body's curvature at
its distance from the centre: `SiteModel` with `solid` (the static
pieces' triangles), `loose` (each dynamic piece's: `Loose` with its
triangles in its own frame, its position and heading), `lamps`,
`screens` (positions, normals, colours each), `bodies` (`Body`: a hull's
points in its frame, the frame's position and heading, dynamic or not,
its mass, and for a dynamic one its index in `loose`), `beacons`
(points) and `bins` (each ore bin's centre and heading).

`overlaps(a, b, gap)` tells whether two footprints (centre, heading, half
extents) come within `gap` of each other.
"""

import math
import random
import zlib
from dataclasses import dataclass, field

import numpy as np

from shoonyakasha.planet import outpost_kit as kit

PURPOSES = ("mining", "fuel", "research", "trade")
PAD_SIZE = 30.0                  # m
INNER = 22.0                     # m from the centre the ring starts
OUTER = 50.0                     # m from the centre the ring ends
CORRIDOR = 36.0                  # m wide, kept clear along the approach
GAP = 2.0                        # m between footprints, at least
BARRIER_OUT = 5.0                # m beyond OUTER the barrier stands
BARRIER_ARC, BARRIER_SKIP = 6, 3   # segments in an arc, and left out between arcs
TRIES = 80                       # spots tried for a ring part

# Each purpose's centre parts and ring parts: (name, how many (low, high), params).
CONTENTS = {
    "mining": ([("ore_line", (1, 2), {})],
               [("container_stack", (3, 5), {}), ("habitat", (2, 3), {}), ("tank", (1, 2), {}),
                ("solar", (0, 2), {})]),
    "fuel": ([("tank_farm", (1, 1), {})],
             [("pad", (1, 1), {"size": 20.0}), ("habitat", (2, 3), {}), ("container_stack", (2, 3), {}),
              ("tank", (1, 2), {}), ("solar", (0, 1), {})]),
    "research": ([("dome", (1, 1), {}), ("antenna", (2, 3), {})],
                 [("solar", (3, 5), {}), ("habitat", (2, 3), {}), ("container_stack", (1, 2), {})]),
    "trade": ([("pad", (1, 1), {"size": 20.0}), ("container_stack", (4, 6), {})],
              [("habitat", (2, 3), {}), ("dome", (1, 1), {"radius": 4.0}), ("tank", (1, 1), {}),
               ("solar", (1, 2), {})]),
}


@dataclass
class Placed:
    name: str
    x: float
    z: float
    heading: float
    params: dict = field(default_factory=dict)

    def footprint(self, half):
        return (self.x, self.z), self.heading, half


def seeded(*key):
    return random.Random(zlib.crc32(repr(key).encode()))


def purposes(seed, bodies, fixed=None):
    fixed = fixed or {}
    out = {}
    for body, names in bodies.items():
        order = list(PURPOSES)
        seeded("purposes", seed, body).shuffle(order)
        free = [name for name in names if name not in fixed]
        left = [p for p in order if p not in {fixed[n] for n in names if n in fixed}] or order
        for k, name in enumerate(free):
            out[name] = left[k % len(left)]
        out.update({name: fixed[name] for name in names if name in fixed})
    return out


def corners(centre, heading, half):
    c, s = math.cos(heading), math.sin(heading)
    right, ahead = np.array([c, -s]), np.array([s, c])
    return np.array([np.asarray(centre) + right * x * half[0] + ahead * z * half[1]
                     for x, z in ((-1, -1), (1, -1), (1, 1), (-1, 1))])


def overlaps(a, b, gap=0.0):
    """Whether footprints `a` and `b` (centre, heading, half) come within `gap` of each other."""
    pa = corners(*a)
    pb = corners(*b)
    for heading in (a[1], b[1]):
        for axis in (np.array([math.cos(heading), -math.sin(heading)]), np.array([math.sin(heading), math.cos(heading)])):
            ra, rb = pa @ axis, pb @ axis
            if ra.max() + gap <= rb.min() or rb.max() + gap <= ra.min():
                return False
    return True


def ahead(heading, distance):
    return math.sin(heading) * distance, math.cos(heading) * distance


def corridor(approach):
    x, z = ahead(approach, 0.5 * (OUTER + BARRIER_OUT + 10.0))
    return (x, z), approach, (0.5 * CORRIDOR, 0.5 * (OUTER + BARRIER_OUT + 10.0))


def half_of(name, params, rng):
    """A part's footprint half extents (its builder's, on a throwaway rng)."""
    return kit.build(name, random.Random(0), **params).half


class Site:
    def __init__(self, approach):
        self.placed = []             # Placed
        self.footprints = []         # (centre, heading, half)
        self.clear = corridor(approach)

    def fits(self, footprint):
        if overlaps(footprint, self.clear, GAP):
            return False
        return not any(overlaps(footprint, f, GAP) for f in self.footprints)

    def put(self, placed, half):
        self.placed.append(placed)
        self.footprints.append(placed.footprint(half))
        return placed

    def spot(self, name, params, rng, inner, outer, jitter=math.radians(10.0)):
        half = half_of(name, params, rng)
        reach = math.hypot(*half)
        if inner + reach > outer - reach:
            return None
        for _ in range(TRIES):
            angle = rng.uniform(0.0, 2.0 * math.pi)
            r = math.sqrt(rng.uniform((inner + reach) ** 2, (outer - reach) ** 2))
            x, z = ahead(angle, r)
            heading = math.atan2(-x, -z) + rng.uniform(-jitter, jitter)
            placed = Placed(name, x, z, heading, dict(params))
            if self.fits(placed.footprint(half)):
                return self.put(placed, half)
        return None


def layout(seed, name, purpose, pad_size=PAD_SIZE, pad_colour=kit.PAD, equator=math.pi, tint=(0.40, 0.34, 0.28)):
    rng = seeded("layout", seed, name)
    approach = rng.uniform(0.0, 2.0 * math.pi)
    site = Site(approach)
    site.put(Placed("pad", 0.0, 0.0, approach, {"size": pad_size, "colour": tuple(pad_colour)}),
             (0.5 * pad_size, 0.5 * pad_size))
    side = approach + 0.5 * math.pi * (1 if rng.random() < 0.5 else -1)
    for part, along, out in (("terminal", -3.0, 2.2), ("ore_bin", -6.5, 2.6)):
        x, z = ahead(side, 0.5 * pad_size + out)
        dx, dz = ahead(approach, along)
        heading = math.atan2(-x, -z)
        placed = Placed(part, x + dx, z + dz, heading)
        site.put(placed, half_of(part, {}, rng))
    centre, ring = CONTENTS[purpose]
    params_of = {"ore_line": lambda: {"length": rng.uniform(14.0, 20.0), "tint": tuple(tint)},
                 "tank_farm": lambda: {"count": rng.randint(3, 5)},
                 "container_stack": lambda: {"count": rng.randint(1, 3)},
                 "habitat": lambda: {"stack": 2 if rng.random() < 0.3 else 1},
                 "dome": lambda: {"radius": rng.uniform(5.0, 6.0)},
                 "antenna": lambda: {"height": rng.uniform(8.0, 12.0)},
                 "solar": lambda: {"tilt": rng.uniform(20.0, 35.0)}}
    for group, (inner, outer) in ((centre, (INNER, INNER + 30.0)), (ring, (INNER, OUTER))):
        for part, (low, high), fixed in group:
            for _ in range(rng.randint(low, high)):
                params = dict(params_of.get(part, dict)())
                params.update(fixed)
                if part == "solar":
                    placed = solar_spot(site, params, rng, equator)
                elif part == "tank_farm":
                    placed = tank_farm_spot(site, params, rng, pad_size)
                else:
                    placed = site.spot(part, params, rng, inner, outer)
                if placed is None and group is centre:
                    site.spot(part, params, rng, INNER, OUTER)
    mast = {"height": rng.uniform(20.0, 35.0)}
    if site.spot("mast", mast, rng, OUTER - 10.0, OUTER) is None:
        site.spot("mast", mast, rng, INNER, OUTER)
    barriers(site, rng)
    return site.placed


def solar_spot(site, params, rng, equator):
    """A solar array whose ahead (the way it leans) is towards `equator`, give or take 10 degrees."""
    half = half_of("solar", params, rng)
    reach = math.hypot(*half)
    for _ in range(TRIES):
        angle = rng.uniform(0.0, 2.0 * math.pi)
        r = math.sqrt(rng.uniform((INNER + reach) ** 2, (OUTER - reach) ** 2))
        x, z = ahead(angle, r)
        placed = Placed("solar", x, z, equator + rng.uniform(-0.17, 0.17), dict(params))
        if site.fits(placed.footprint(half)):
            return site.put(placed, half)
    return None


def tank_farm_spot(site, params, rng, pad_size):
    """A tank farm facing the pad, with a pipe from its manifold to the pad's edge."""
    half = half_of("tank_farm", params, rng)
    for _ in range(TRIES):
        angle = rng.uniform(0.0, 2.0 * math.pi)
        r = 0.5 * pad_size * 1.42 + half[1] + rng.uniform(6.0, 14.0)
        x, z = ahead(angle, r)
        heading = math.atan2(-x, -z)
        farm = Placed("tank_farm", x, z, heading, dict(params))
        start = r - half[1] + 0.3
        end = 0.5 * pad_size + 0.5
        length = start - end
        px, pz = ahead(angle, 0.5 * (start + end))
        line = Placed("pipe", px, pz, heading, {"length": length})
        line_half = half_of("pipe", line.params, rng)
        if site.fits(farm.footprint(half)) and not overlaps(line.footprint(line_half), site.clear, GAP) and not any(
                overlaps(line.footprint(line_half), f, 0.0) for f in site.footprints):
            site.put(farm, half)
            site.put(line, line_half)
            return farm
    return None


def barriers(site, rng):
    radius = OUTER + BARRIER_OUT
    length = kit.BARRIER[0] + 0.4
    count = int(2.0 * math.pi * radius / length)
    start = rng.randrange(BARRIER_ARC + BARRIER_SKIP)
    half = half_of("barrier", {}, rng)
    for k in range(count):
        if (k + start) % (BARRIER_ARC + BARRIER_SKIP) >= BARRIER_ARC:
            continue
        angle = 2.0 * math.pi * k / count
        x, z = ahead(angle, radius)
        placed = Placed("barrier", x, z, angle + rng.uniform(-0.04, 0.04))
        if site.fits(placed.footprint(half)):
            site.put(placed, half)


@dataclass
class Body:
    hull: np.ndarray             # points in the frame
    position: tuple              # the frame's origin, site frame
    heading: float               # radians about up
    dynamic: bool = False
    mass: float = 0.0
    loose: int = -1              # a dynamic body's index in SiteModel.loose


@dataclass
class Loose:
    triangles: tuple             # (positions, normals, colours) in its own frame
    position: tuple
    heading: float


@dataclass
class SiteModel:
    solid: tuple
    loose: list
    lamps: tuple
    screens: tuple
    bodies: list
    beacons: list
    bins: list


def site_model(placed, body_radius, seed=0):
    solid, lamps, screens = kit.Geometry(), kit.Geometry(), kit.Geometry()
    loose, bodies, beacons, bins = [], [], [], []
    for k, p in enumerate(placed):
        part = kit.build(p.name, seeded("part", seed, k, p.name), **p.params)
        drop = (p.x * p.x + p.z * p.z) / (2.0 * body_radius)
        origin = np.array([p.x, -drop, p.z])
        rotation = kit.yaw(p.heading)
        for piece in part.pieces:
            at = origin + rotation @ np.asarray(piece.position, np.float64)
            if piece.dynamic:
                index = len(loose)
                loose.append(Loose(piece.solid.arrays(), tuple(at), p.heading))
                bodies += [Body(np.asarray(h, np.float32), tuple(at), p.heading, True, piece.mass, index)
                           for h in piece.hulls]
            else:
                solid.add(piece.solid, rotation, at)
                bodies += [Body(np.asarray(h, np.float32), tuple(at), p.heading) for h in piece.hulls]
            lamps.add(piece.lamps, rotation, at)
            screens.add(piece.screens, rotation, at)
        if part.beacon is not None:
            beacons.append(tuple(origin + rotation @ np.asarray(part.beacon, np.float64)))
        if p.name == "ore_bin":
            bins.append((tuple(origin), p.heading))
    return SiteModel(solid.arrays(), loose, lamps.arrays(), screens.arrays(), bodies, beacons, bins)


def merged(loose):
    """The loose pieces' triangles put together in the site's frame."""
    g = kit.Geometry()
    for piece in loose:
        positions, normals, colours = piece.triangles
        rotation = kit.yaw(piece.heading)
        g.positions.append(positions.astype(np.float64) @ rotation.T + np.asarray(piece.position))
        g.normals.append(normals.astype(np.float64) @ rotation.T)
        g.colours.append(colours.astype(np.float64))
    return g.arrays()

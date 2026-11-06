"""
Outposts: landing pads where the ship trades (trade.py), one at the
blocks (the home outpost, on the site's pad) and more on flat, dry ground
of the planet and the moon, found by a survey.

`Survey(bodies, seed)` gives, for each body, CANDIDATES seeded directions
(from the body's name and, for a body with terrain parameters, the
planet, `seed`; the moon's are always the same) and for each the points to measure the
terrain at: the candidate and RING points around it at its flat radius
(`points`, (direction, body) pairs for world.start_survey). `choose(heights,
taken)` keeps the candidates whose points are all above DRY on the planet
(off the sea) and, on the planet, between FROM_HOME's distances along the
ground from the home outpost; takes those with the least relief (highest
point less lowest) first, each at least SPACING from the outposts already
there and those taken before it; and makes COUNT of them per body into
outposts at the mean of their heights. Each outpost there is a flat site
(world.Site) of FLAT_RADIUS and BLEND_RADIUS.

`Pads` draws each outpost but the home one: a pad PAD_SIZE across, a mast
and two buildings beside it, placed each frame relative to the camera
like the blocks. `Outpost.at(ship_position)` tells whether a point is over
its pad: within PAD_REACH of its centre along the ground and REACH_UP
above or below it.
"""

import math
import random
import zlib
from dataclasses import dataclass, field

from shoonyakasha.planet.vec import add, dot, euler_from_basis, length, local_frame, normalize, scale, sub
from shoonyakasha.planet.world import Site

COUNT = {"planet": 3, "moon": 2}
NAMES = {"planet": ("Kestrel", "Halden", "Varga"), "moon": ("Lowrim", "Selene Deep")}
HOME_NAME = "Base"
CANDIDATES = {"planet": 64, "moon": 24}
RING = 8
FLAT_RADIUS = {"planet": 150.0, "moon": 100.0}       # m
BLEND_RADIUS = {"planet": 600.0, "moon": 400.0}      # m
SPACING = {"planet": 30_000.0, "moon": 12_000.0}     # m along the ground between outposts, at least
FROM_HOME = (20_000.0, 150_000.0)                    # m along the ground from the home outpost, on the planet
DRY = 10.0                                           # m above sea level, on the planet
FUEL_PRICE = {"home": 0.8, "planet": 1.0, "moon": 3.0}   # credits per kg
PAD_SIZE = 30.0                                      # m
PAD_REACH = 25.0                                     # m along the ground from a pad's centre
REACH_UP = 15.0                                      # m above or below it
PAD_COLOUR = {"planet": (0.33, 0.34, 0.37), "moon": (0.46, 0.46, 0.49)}


def ground_distance(radius, a, b):
    return radius * math.acos(max(-1.0, min(1.0, dot(a, b))))


@dataclass
class Outpost:
    name: str
    body: object                 # the world body it stands on
    direction: tuple             # unit, from the body's centre
    height: float                # m above the body's radius
    fuel_price: float
    flat_radius: float = 0.0     # m; 0 for the home outpost, which stands on the site's pad
    blend_radius: float = 0.0
    supply: dict = field(default_factory=dict)   # body name: (kg of its ore bought, world time), trade.py
    reputation: float = 0.0      # the player's standing here (the walker's contracts); raises its prices

    def site(self):
        return Site(self.body.index, tuple(self.direction), self.height, self.flat_radius, self.blend_radius)

    def point(self):
        """The pad's centre on the ground, from the planet's centre."""
        return add(self.body.centre(), scale(self.direction, self.body.radius + self.height))

    def at(self, position):
        """Whether `position` (from the planet's centre) is over the pad."""
        relative = sub(position, self.body.centre())
        up = dot(relative, self.direction) - (self.body.radius + self.height)
        along = ground_distance(self.body.radius, self.direction, normalize(relative))
        return along <= PAD_REACH and abs(up) <= REACH_UP


def home_outpost(world):
    """The outpost at the blocks, on the site's pad."""
    _, up, _, _ = world.site_frame()
    return Outpost(HOME_NAME, world.planet, up, world.site_height or 0.0, FUEL_PRICE["home"])


def ring(direction, radius, distance, count=RING):
    """`count` unit directions `distance` metres along the ground around `direction`."""
    east, north = local_frame(direction)
    angle = distance / radius
    out = []
    for k in range(count):
        theta = 2.0 * math.pi * k / count
        side = add(scale(east, math.cos(theta)), scale(north, math.sin(theta)))
        out.append(normalize(add(scale(direction, math.cos(angle)), scale(side, math.sin(angle)))))
    return out


class Survey:
    def __init__(self, bodies, seed):
        """`bodies`: the world bodies to search; `seed`: anything with a
        repr, mixed into the candidates of each body with terrain parameters."""
        self.candidates = []         # (body, direction)
        self.points = []             # (direction, body), RING + 1 per candidate
        for body in bodies:
            mixed = seed if body.terrain is not None else None
            rng = random.Random(zlib.crc32(repr((body.name, mixed)).encode()))
            for _ in range(CANDIDATES[body.name]):
                direction = normalize((rng.gauss(0.0, 1.0), rng.gauss(0.0, 1.0), rng.gauss(0.0, 1.0)))
                self.candidates.append((body, direction))
                self.points.append((direction, body))
                self.points += [(d, body) for d in ring(direction, body.radius, FLAT_RADIUS[body.name])]

    def choose(self, heights, taken=(), home=None):
        """The new outposts from the points' `heights` (in `points`' order):
        COUNT per body. `taken`: outposts already there, which the new ones
        keep SPACING from; `home`: the home outpost, which the planet's
        keep FROM_HOME's distances from."""
        per = RING + 1
        kept = []
        for k, (body, direction) in enumerate(self.candidates):
            h = heights[k * per:(k + 1) * per]
            if body.name == "planet":
                if min(h) < DRY:
                    continue
                if home is not None:
                    distance = ground_distance(body.radius, home.direction, direction)
                    if not FROM_HOME[0] <= distance <= FROM_HOME[1]:
                        continue
            kept.append((max(h) - min(h), k, body, direction, sum(h) / len(h)))
        kept.sort(key=lambda c: (c[0], c[1]))
        chosen = list(taken) + ([home] if home is not None else [])
        new = []
        for _, _, body, direction, height in kept:
            name = body.name
            mine = [o for o in new if o.body is body]
            if len(mine) >= COUNT[name]:
                continue
            if any(o.body is body and ground_distance(body.radius, o.direction, direction) < SPACING[name]
                   for o in chosen + new):
                continue
            new.append(Outpost(NAMES[name][len(mine)], body, direction, height, FUEL_PRICE[name],
                               FLAT_RADIUS[name], BLEND_RADIUS[name]))
        return new


class Pads:
    """The outposts' pads, masts and buildings."""

    # (east, up, north) centre, (east, up, north) size, colour; None for the pad's own colour
    PIECES = (
        ((0.0, 0.1, 0.0), (PAD_SIZE, 0.1, PAD_SIZE), None),
        ((13.5, 10.15, -13.5), (0.4, 20.0, 0.4), (0.92, 0.32, 0.15)),
        ((-26.0, 2.05, 0.0), (6.0, 4.0, 9.0), (0.78, 0.76, 0.70)),
        ((-26.0, 1.55, 9.0), (3.0, 3.0, 3.0), (0.85, 0.62, 0.20)),
    ) + tuple(((x * 14.5, 0.25, z * 14.5), (0.5, 0.3, 0.5), (1.0, 0.75, 0.25)) for x in (-1, 1) for z in (-1, 1))

    def __init__(self, engine, mesh):
        """`mesh`: (positions, normals) of a unit cube centred on the origin."""
        self.engine = engine
        self.mesh = mesh
        self.placed = []             # (entity, body, offset from the body's centre)

    def build(self, outposts):
        """Pads for `outposts` (all but those with no flat radius), replacing any there were."""
        self.clear()
        positions, normals = self.mesh
        for outpost in outposts:
            if outpost.flat_radius <= 0.0:
                continue
            up = outpost.direction
            east, north = local_frame(up)
            rotation = euler_from_basis(east, up, scale(north, -1.0))
            centre = scale(up, outpost.body.radius + outpost.height)
            for (e, u, n), size, colour in self.PIECES:
                entity = self.engine.create_mesh(positions, normals=normals, name="Pad")
                self.engine.scene.set_rotation(entity, rotation)
                self.engine.scene.set_scale(entity, size)
                colour = colour or PAD_COLOUR[outpost.body.name]
                self.engine.scene.set_material_vec4(entity, "baseColorFactor", colour + (1.0,))
                self.engine.scene.set_material_float(entity, "gridStrength", 0.0)
                self.engine.scene.set_render_layer_mask(entity, 1)
                offset = add(centre, add(scale(east, e), add(scale(up, u), scale(north, n))))
                self.placed.append((entity, outpost.body, offset))

    def clear(self):
        for entity, _, _ in self.placed:
            self.engine.scene.destroy_entity(entity)
        self.placed = []

    def draw(self, camera):
        for entity, body, offset in self.placed:
            self.engine.scene.set_position(entity, sub(add(body.centre(), offset), camera))


def nearest(outposts, position):
    """The outpost nearest `position` (from the planet's centre), or None."""
    return min(outposts, key=lambda o: length(sub(o.point(), position)), default=None)

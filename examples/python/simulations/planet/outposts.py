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

`Pads` builds and draws each outpost but the home one from the outposts'
kit (shoonyakasha.planet.outpost_kit, outpost_layout): `build(outposts,
seed)` gives each its purpose (`Outpost.purpose`, seeded by `seed`;
REFINERY's always the fuel depot) and lays its parts out round its pad
(PAD_SIZE across), the same each time for the same seed; an ore line's
heaps take the tint of a resource of its body (seeded). Each outpost is
drawn by a few mesh entities placed each frame relative to the camera
like the blocks: its static parts, its loose parts (containers and
barriers, merged; `Pads.loose_shown` hides them), its lamps and its
screens, all within HIDE_DISTANCE, and its beacons. The lamps' glow
(LAMP_GLOW) is drawn while the sun at the site is below LAMPS_ON degrees. A
beacon blinks (on BEACON_ON s of every BEACON_PERIOD, its phase per
outpost; lit throughout with `Pads.steady`) within BEACON_RANGE, or from
anywhere while its site's lamps are on, at least BEACON_ANGLE radians
across. `Built.to_frame` turns
the site frame's points (x east, y up, z north) into the planet's axes,
and `Built.point` a site point into planet space. `Outpost.at(ship_position)` tells whether a point is over
its pad: within PAD_REACH of its centre along the ground and REACH_UP
above or below it. `Pads.register_probes(world)` hands each built
outpost's static triangles and frame to world.add_site_probes, and
`base_probes(world, pieces, frame)` the home outpost's blocks (each piece's
centre east, up and north of the site's point on the ground and its size). The outpost named REFINERY has a refinery
(`Outpost.refinery`): it turns ice into fuel, REFINE_ICE kg of ice to a
kg of fuel (the walker's Outpost tab).
"""

import math
import random
import zlib
from dataclasses import dataclass, field

import numpy as np

from shoonyakasha.planet import outpost_kit, outpost_layout, site_probes
from shoonyakasha.planet.resources import AMBER, CUPRITE, EVAPORITE, FERRITE, ICE, ILMENITE, QUARTZ
from shoonyakasha.planet.rocks import ORE_TINTS
from shoonyakasha.planet.vec import add, dot, length, local_frame, normalize, scale, sub
from shoonyakasha.planet.world import Site

COUNT = {"planet": 3, "moon": 2}
REFINERY = "Selene Deep"         # the outpost with a refinery
REFINE_ICE = 3.0                 # kg of ice to a kg of fuel
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
HEAP_RESOURCES = {"planet": (FERRITE, CUPRITE, QUARTZ, AMBER, EVAPORITE), "moon": (ICE, ILMENITE)}
HIDE_DISTANCE = 8_000.0                              # m: an outpost's parts are drawn within this
LAMPS_ON = 3.0                                       # degrees: the sun's elevation at a site below which its lamps glow
LAMP_GLOW, SCREEN_GLOW, BEACON_GLOW = 2.0, 1.2, 24.0  # emissive strengths
BEACON_RANGE = 30_000.0                              # m
BEACON_PERIOD, BEACON_ON = 2.0, 0.3                  # s
BEACON_SIZE, BEACON_ANGLE = 0.6, 0.006               # m across at least; radians across at least


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
    purpose: str = ""            # one of outpost_layout.PURPOSES, from Pads.build

    @property
    def refinery(self):
        return self.name == REFINERY

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


class Built:
    """An outpost's site model (outpost_layout.SiteModel) and its entities."""

    def __init__(self, outpost, model):
        self.outpost = outpost
        self.model = model
        east, north = local_frame(outpost.direction)
        self.axes = np.array([east, outpost.direction, north])
        self.centre = scale(outpost.direction, outpost.body.radius + outpost.height)   # from the body's centre
        self.phase = (zlib.crc32(outpost.name.encode()) % 1000) / 1000.0 * BEACON_PERIOD
        self.entities = {}           # "solid", "loose", "lamps", "screens": entity
        self.beacons = []            # entity per beacon
        self.shown = {}              # entity: visible

    def to_frame(self, points):
        """Site-frame points (x east, y up, z north) along the planet's axes."""
        return np.asarray(points, np.float64) @ self.axes

    def point(self, local):
        """A site-frame point in planet space."""
        return tuple(np.asarray(add(self.outpost.body.centre(), self.centre)) + self.to_frame(local))

    def triangles(self, arrays):
        """(positions, normals, colours) of the site frame along the planet's axes, each triangle's
        corners reversed, since the site frame's axes are mirrored."""
        positions, normals, colours = arrays
        flip = lambda a: np.asarray(a).reshape(-1, 3, 3)[:, ::-1].reshape(-1, 3)
        return (flip(self.to_frame(positions)).astype(np.float32), flip(self.to_frame(normals)).astype(np.float32),
                flip(colours).astype(np.float32))


class Pads:
    """The outposts but the home one, built from the kit."""

    def __init__(self, engine, mesh):
        """`mesh`: (positions, normals) of a unit cube centred on the origin, the beacons'."""
        self.engine = engine
        self.mesh = mesh
        self.built = []              # Built
        self.loose_shown = {}        # outpost name: whether its loose parts' merged mesh is drawn (True when absent)
        self.steady = False          # the beacons lit throughout instead of blinking

    def build(self, outposts, seed):
        """`outposts` (all but those with no flat radius) laid out and given entities, replacing any there were."""
        self.clear()
        sites = [o for o in outposts if o.flat_radius > 0.0]
        names = {}
        for o in sites:
            names.setdefault(o.body.name, []).append(o.name)
        purposes = outpost_layout.purposes(seed, names, {REFINERY: "fuel"})
        for o in sites:
            o.purpose = purposes[o.name]
            rng = random.Random(zlib.crc32(repr(("heap", seed, o.name)).encode()))
            resources = HEAP_RESOURCES.get(o.body.name, (FERRITE,))
            tint = ORE_TINTS[resources[rng.randrange(len(resources))]]
            equator = math.pi if o.direction[1] >= 0.0 else 0.0
            placed = outpost_layout.layout(seed, o.name, o.purpose, PAD_SIZE, PAD_COLOUR.get(o.body.name, (0.4, 0.4, 0.4)),
                                           equator, tint)
            built = Built(o, outpost_layout.site_model(placed, o.body.radius, seed))
            self.make_entities(built)
            self.built.append(built)

    def register_probes(self, world):
        """Each built outpost's static triangles and frame to world.add_site_probes."""
        for built in self.built:
            triangles = np.asarray(built.model.solid[0], np.float32).reshape(-1, 3, 3)
            world.add_site_probes(("outpost", built.outpost.name), triangles,
                                  lambda b=built: (b.point((0.0, 0.0, 0.0)), tuple(b.axes[0]), tuple(b.axes[1]),
                                                   tuple(b.axes[2])))

    def make_entities(self, built):
        from viewmodel import white_texture
        scene = self.engine.scene
        m = built.model
        for key, arrays, glow in (("solid", m.solid, None), ("loose", outpost_layout.merged(m.loose), None),
                                  ("lamps", m.lamps, tuple(c * LAMP_GLOW for c in outpost_kit.LAMP)),
                                  ("screens", m.screens, tuple(c * SCREEN_GLOW for c in outpost_kit.SCREEN))):
            if len(arrays[0]) == 0:
                continue
            positions, normals, colours = built.triangles(arrays)
            entity = self.engine.create_mesh(positions, normals=normals, colors=colours, name="Outpost")
            scene.set_material_float(entity, "gridStrength", 0.0)
            if glow is None:
                scene.set_material_vec4(entity, "baseColorFactor", (1.0, 1.0, 1.0, 1.0))
                scene.set_render_layer_mask(entity, 1)
            else:
                scene.set_material_vec4(entity, "baseColorFactor", (0.05, 0.05, 0.05, 1.0))
                scene.set_material_texture(entity, "emissiveMap", white_texture())
                scene.set_material_vec4(entity, "emissiveFactor", glow + (0.0,))
                scene.set_render_layer_mask(entity, 2)
            built.entities[key] = entity
        cube = self.mesh
        for _ in m.beacons:
            entity = self.engine.create_mesh(cube[0], normals=cube[1], name="Beacon")
            scene.set_material_float(entity, "gridStrength", 0.0)
            scene.set_material_vec4(entity, "baseColorFactor", (0.05, 0.05, 0.05, 1.0))
            scene.set_material_texture(entity, "emissiveMap", white_texture())
            scene.set_material_vec4(entity, "emissiveFactor", tuple(c * BEACON_GLOW for c in outpost_kit.BEACON) + (0.0,))
            scene.set_render_layer_mask(entity, 2)
            built.beacons.append(entity)

    def clear(self):
        for built in self.built:
            for entity in list(built.entities.values()) + built.beacons:
                self.engine.scene.destroy_entity(entity)
        self.built = []

    def find(self, outpost):
        return next((b for b in self.built if b.outpost is outpost), None)

    def show(self, built, entity, on):
        if built.shown.get(entity) != on:
            self.engine.scene.set_visible(entity, on)
            built.shown[entity] = on

    def draw(self, camera, sun, time):
        """Each outpost placed relative to `camera` (planet space); `sun` the
        direction towards the sun, `time` the world's clock (s)."""
        scene = self.engine.scene
        for built in self.built:
            o = built.outpost
            at = add(o.body.centre(), built.centre)
            relative = sub(at, camera)
            distance = length(relative)
            near = distance < HIDE_DISTANCE
            dark = dot(sun, o.direction) < math.sin(math.radians(LAMPS_ON))
            for key, entity in built.entities.items():
                on = near and (key != "loose" or self.loose_shown.get(o.name, True)) and (key != "lamps" or dark)
                self.show(built, entity, on)
                if on:
                    scene.set_position(entity, relative)
            blink = self.steady or (time + built.phase) % BEACON_PERIOD < BEACON_ON
            for entity, local in zip(built.beacons, built.model.beacons):
                point = built.point(local)
                d = length(sub(point, camera))
                on = blink and (d < BEACON_RANGE or dark)
                self.show(built, entity, on)
                if on:
                    scene.set_position(entity, sub(point, camera))
                    size = max(BEACON_SIZE, d * BEACON_ANGLE)
                    scene.set_scale(entity, (size, size, size))



def nearest(outposts, position):
    """The outpost nearest `position` (from the planet's centre), or None."""
    return min(outposts, key=lambda o: length(sub(o.point(), position)), default=None)


def base_probes(world, pieces, frame):
    """The home outpost's blocks (((east, up, north) of their centres, size) each) to world.add_site_probes,
    with `frame` giving its point on the ground and east, up, north."""
    triangles = np.concatenate([site_probes.box_triangles((e, u, n), size) for (e, u, n), size in pieces])
    world.add_site_probes(("base",), triangles, frame)

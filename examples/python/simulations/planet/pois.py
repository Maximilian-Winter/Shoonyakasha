"""
Points of interest (walker.py): places on a body found from a hash, each
of a kind with something to do there. Plain Python, no engine.

Placement: each face of the body's cube is cut into CELLS by CELLS cells
(about 2.5 km across on the planet); each cell has one candidate
(`candidate`): a direction hashed inside the cell (away from its edges)
and a roll, 0 to 1, from a hash of the body, the face, the cell and the
seed. `cells_near(direction, reach, radius)` are the cells whose
candidates may lie within `reach` metres along the ground.

Kinds (KINDS, in order: key, name, chance, where): once the ground at a
candidate is measured, `kind_for(roll, height, climate, ore)` walks the
kinds whose `where` holds there, adding up their chances, and gives the
first whose sum passes the roll (None for most candidates). `where` sees
the height (m above the sea), the climate ((temperature, moisture) or
None) and the ore richness:
    pod       a crashed cargo pod: everywhere
    beacon    an old survey beacon: everywhere
    wreck     a broken ship's hull: everywhere, twice as likely above
              HIGH ground
    rig       an abandoned rig: where the ore is above RIG_ORE
    geode     a field of rich ore rocks: in the cold (below COLD) or the
              dry (moisture below DRY)
    arch      a rock arch: in the dry or on HIGH ground
A candidate is valid on land (above MIN_HEIGHT) and out of the water.

`Poi` keeps a candidate's key (body index, face, cell x, cell y), its
direction and roll, and once measured its height, climate, ore,
resource, kind (or None) and whether it is valid; `discovered`, `looted`
and `done` (the spots of it used) are what the save keeps (`record`,
`restore`). `Pois` holds them: `want(body, direction, reach, seed)` adds
the candidates near a place to be measured; `to_measure(count)` gives
those still to measure; `measured(key, height, climate, ore, resource,
water)` sets one's ground and kind.

Rewards: `parts_for(poi, spot)` the part a wreck's or a rig's spot gives
(PARTS, by a hash); `survey_value(poi)` the survey data (credits) a
beacon or an arch gives, SURVEY_VALUE apart by its roll.

LAYOUTS draws each kind as boxes about its point on the ground, and
SPOTS says where it is used, how near and for how long the use key is
held: a beacon activated reveals the map within REVEAL and gives survey
data; a wreck's three spots, each salvaged by holding the key, give a
part each; a rig's two crates, opened, give RIG_ROCKS ore rocks of the
local resource and a part; an arch scanned gives survey data. A cargo
pod is a crate prop (POD_SIZE, POD_MASS) that fetches POD_VALUE resting
on an outpost's pad; a geode field is GEODE_ROCKS loose ore rocks of the
local resource at GEODE_GRADE, spread over GEODE_SPREAD.
"""

import math
import zlib
from dataclasses import dataclass, field

from shoonyakasha.planet import cube_sphere
from shoonyakasha.planet.vec import dot

CELLS = 64
MIN_HEIGHT = 2.0                 # m above the sea
HIGH = 1_200.0                   # m above the sea
COLD = 2.0                       # degrees C
DRY = 0.3
RIG_ORE = 0.3
PARTS = ("coil", "injector", "nav_core")
PART_NAMES = {"coil": "Coil", "injector": "Injector", "nav_core": "Nav core"}
SURVEY_VALUE = (50.0, 200.0)     # credits, from a beacon or an arch


def _everywhere(height, climate, ore):
    return True


def _rig(height, climate, ore):
    return ore >= RIG_ORE


def _geode(height, climate, ore):
    return climate is not None and (climate[0] < COLD or climate[1] < DRY)


def _arch(height, climate, ore):
    return height > HIGH or (climate is not None and climate[1] < DRY)


KINDS = [
    ("pod", "Cargo pod", 0.06, _everywhere),
    ("beacon", "Survey beacon", 0.04, _everywhere),
    ("wreck", "Wreck", 0.03, _everywhere),
    ("rig", "Abandoned rig", 0.02, _rig),
    ("geode", "Geode field", 0.03, _geode),
    ("arch", "Rock arch", 0.02, _arch),
]
NAMES = {key: name for key, name, _, _ in KINDS}


def _hash(*values):
    return zlib.crc32(repr(values).encode())


def candidate(body, face, cx, cy, seed):
    """(unit direction, roll) of the cell's candidate."""
    h = _hash(body, face, cx, cy, seed)
    a, b, c = (h & 0x3FF) / 1023.0, ((h >> 10) & 0x3FF) / 1023.0, ((h >> 20) & 0xFFF) / 4095.0
    u = (cx + 0.15 + 0.7 * a) / CELLS * 2.0 - 1.0
    v = (cy + 0.15 + 0.7 * b) / CELLS * 2.0 - 1.0
    return cube_sphere.direction(face, u, v), c


def cells_near(direction, reach, radius):
    """(face, cx, cy) of the cells whose candidates may lie within `reach`
    metres along the ground of the unit `direction` on a body of `radius`."""
    cell_angle = 2.0 / CELLS * 1.3          # a cell's span at most, as an angle (radians), with room
    limit = reach / radius + cell_angle
    face, u, v = cube_sphere.face_uv(direction)
    span = int(math.ceil(limit / (2.0 / CELLS) * 1.5)) + 1
    ci = int((u + 1.0) * 0.5 * CELLS)
    cj = int((v + 1.0) * 0.5 * CELLS)
    found = []
    for f in range(6):
        if f != face and dot(cube_sphere.direction(f, 0.0, 0.0), direction) < math.cos(math.pi / 4 + limit + 0.2):
            continue
        for cx in range(CELLS):
            if f == face and abs(cx - ci) > span:
                continue
            for cy in range(CELLS):
                if f == face and abs(cy - cj) > span:
                    continue
                centre = cube_sphere.direction(f, (cx + 0.5) / CELLS * 2.0 - 1.0, (cy + 0.5) / CELLS * 2.0 - 1.0)
                if dot(centre, direction) >= math.cos(limit):
                    found.append((f, cx, cy))
    return found


def kind_for(roll, height, climate, ore):
    total = 0.0
    for key, _, chance, where in KINDS:
        if not where(height, climate, ore):
            continue
        total += chance * (2.0 if key == "wreck" and height > HIGH else 1.0)
        if roll < total:
            return key
    return None


@dataclass
class Poi:
    key: tuple                   # (body index, face, cell x, cell y)
    direction: tuple
    roll: float
    measured: bool = False
    height: float = 0.0          # m above the body's radius
    climate: tuple = None
    ore: float = 0.0
    resource: int = 0
    kind: str = None
    valid: bool = False
    discovered: bool = False
    looted: bool = False
    done: list = field(default_factory=list)    # the spots used

    @property
    def name(self):
        return NAMES.get(self.kind, "Signal")


class Pois:
    def __init__(self):
        self.pois = {}           # key: Poi
        self.queue = []          # keys to measure, nearest first

    def want(self, body_index, direction, reach, radius, seed):
        """The candidates within `reach` of `direction` known, those not
        measured queued, nearest first."""
        for face, cx, cy in cells_near(direction, reach, radius):
            key = (body_index, face, cx, cy)
            if key in self.pois:
                continue
            d, roll = candidate(body_index, face, cx, cy, seed)
            if radius * math.acos(max(-1.0, min(1.0, dot(d, direction)))) > reach:
                continue
            self.pois[key] = Poi(key, d, roll)
            self.queue.append(key)
        self.queue.sort(key=lambda k: -dot(self.pois[k].direction, direction))

    def to_measure(self, count):
        return [self.pois[k] for k in self.queue[:count]]

    def measured(self, key, height, climate, ore, resource, water):
        poi = self.pois[key]
        poi.measured = True
        poi.height, poi.climate, poi.ore, poi.resource = height, climate, ore, resource
        poi.valid = height > MIN_HEIGHT and water is None
        poi.kind = kind_for(poi.roll, height, climate, ore) if poi.valid else None
        if key in self.queue:
            self.queue.remove(key)

    def found(self):
        """The measured points of interest with a kind."""
        return [p for p in self.pois.values() if p.measured and p.kind is not None]

    def record(self):
        return [{"key": list(p.key), "discovered": p.discovered, "looted": p.looted, "done": list(p.done)}
                for p in self.pois.values() if p.discovered or p.looted or p.done]

    def restore(self, records, seed):
        """The saved states put back on their candidates (made again from
        `seed`; their ground is measured again when wanted)."""
        self.pois, self.queue = {}, []
        for r in records:
            key = tuple(r["key"])
            d, roll = candidate(*key, seed)
            self.pois[key] = Poi(key, d, roll, discovered=r["discovered"], looted=r["looted"], done=list(r["done"]))
            self.queue.append(key)


# How each kind is drawn: boxes of (offset (east, up, north) from its point on the ground, size, colour),
# in metres, sunk SINK into the ground; and where it is used: spots (offset, reach in metres, seconds the
# use key is held). A pod and a geode field are props (POD, GEODE) besides.
SINK = 0.3
HULL, RUST, STEEL, STONE, BEACON = (0.32, 0.33, 0.36), (0.55, 0.30, 0.18), (0.6, 0.62, 0.66), (0.52, 0.45, 0.38), \
    (0.3, 0.85, 1.0)
LAYOUTS = {
    "pod": [((1.6, 2.0, 0.0), (0.15, 4.0, 0.15), STEEL), ((1.6, 4.1, 0.0), (0.4, 0.4, 0.4), BEACON)],
    "beacon": [((0.0, 4.0, 0.0), (0.5, 8.0, 0.5), STEEL), ((0.0, 8.4, 0.0), (1.2, 1.0, 1.2), BEACON),
               ((0.0, 0.4, 0.0), (2.0, 0.8, 2.0), HULL)],
    "wreck": [((0.0, 1.2, 0.0), (4.0, 2.4, 12.0), HULL), ((3.5, 0.6, 2.0), (3.0, 0.5, 5.0), HULL),
              ((-3.0, 0.4, -3.0), (2.0, 0.4, 3.0), HULL), ((0.0, 2.6, 4.5), (0.4, 1.8, 0.4), RUST)],
    "rig": [((x, 3.0, z), (0.3, 6.0, 0.3), RUST) for x in (-4.0, 4.0) for z in (-4.0, 4.0)]
           + [((0.0, 6.1, z), (8.3, 0.3, 0.3), RUST) for z in (-4.0, 4.0)]
           + [((-2.0, 0.6, 1.5), (1.2, 1.2, 1.2), RUST), ((2.0, 0.6, -1.5), (1.2, 1.2, 1.2), RUST)],
    "geode": [],
    "arch": [((-6.0, 7.0, 0.0), (3.0, 14.0, 3.5), STONE), ((6.0, 7.0, 0.0), (3.0, 14.0, 3.5), STONE),
             ((0.0, 15.0, 0.0), (16.0, 3.0, 4.0), STONE)],
}
SPOTS = {
    "beacon": [((0.0, 0.0, 0.0), 5.0, 0.0)],
    "wreck": [((2.6, 0.0, -3.0), 2.5, 3.0), ((-2.6, 0.0, 1.0), 2.5, 3.0), ((0.0, 0.0, 7.0), 2.5, 3.0)],
    "rig": [((-2.0, 0.0, 1.5), 2.5, 0.0), ((2.0, 0.0, -1.5), 2.5, 0.0)],
    "arch": [((0.0, 0.0, 0.0), 25.0, 0.0)],
}
USE_WORDS = {"beacon": "Activate", "wreck": "Salvage", "rig": "Open", "arch": "Scan"}
# Metres around each kind's point cleared of plants and rocks (the world's clearings), and the edge over
# which they come back.
CLEAR_RADIUS = {"pod": 5.0, "beacon": 6.0, "wreck": 14.0, "rig": 9.0, "geode": 8.0, "arch": 14.0}
CLEAR_EDGE = 6.0
POD_SIZE, POD_MASS, POD_VALUE = 1.2, 300.0, 600.0     # m, kg, credits at an outpost's pad
POD_COLOUR = (0.95, 0.55, 0.15)
GEODE_ROCKS, GEODE_GRADE, GEODE_SPREAD = 6, 0.95, 4.0  # rocks, their grade, m around the point
RIG_ROCKS, RIG_GRADE = 2, 0.75                         # ore rocks a rig's crate holds, their grade
REVEAL = 1_500.0                                       # m around a beacon its map reveals


def parts_for(poi, spot):
    return PARTS[_hash(poi.key, spot, "part") % len(PARTS)]


def survey_value(poi):
    low, high = SURVEY_VALUE
    return low + (high - low) * ((_hash(poi.key, "survey") % 1000) / 999.0)

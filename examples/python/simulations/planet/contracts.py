"""
Contracts offered at the outposts (walker.py). Plain Python, no engine.

`offers(outpost, day, seed, outposts, radius, reputation)` are OFFERS
contracts of the outpost (a name and a unit direction) for `day` (the
day's index; a day turns at dawn), drawn from a random.Random seeded with
zlib.crc32 of the outpost's name, the day and the world's seed, so they
are the same all day and after a reload. Each is one of KINDS (by
WEIGHTS):
    delivery  bring `quantity` kg of a planet resource at `min_grade` or
              better to this outpost; reward its value at that grade
              times DELIVERY_MARKUP plus DELIVERY_BONUS
    courier   carry a fragile crate from this outpost to `destination`,
              another of the planet's; reward COURIER_BASE plus
              COURIER_PER_KM of the way
    recovery  bring the cargo pod at `target` (RECOVERY_REACH from the
              outpost) back here; reward RECOVERY_BASE plus
              RECOVERY_PER_KM of the way out
    survey    explore SURVEY_SHARE of the map's cells within SURVEY_RADIUS
              of `target` (SURVEY_REACH out); reward SURVEY_REWARD
each reward times 1 + REWARD_PER_REPUTATION times the outpost's
reputation, rounded to 10 credits, due within DEADLINE_DAYS days.

`Contract` holds its kind, `origin` (the outpost that offered it),
`destination`, `reward`, `deadline` (the last day), its task's fields
and its progress (`delivered` kg, `damage`, the survey's `explored`
share), and `state`: "offered", "active", "completed", "failed" or
"expired". `task(c)` is a line of what to do, `progress(c)` its share
done. `wants(c, item, outpost)` whether a delivery takes an ore item sold
at an outpost. `strike(c, speed)` adds a fragile crate's damage, of
(speed - SAFE_SPEED)^2 times DAMAGE_PER, and says whether it broke
(BREAK_DAMAGE). `record(c)` / `restore(record)` for the save. The
courier's crate is COURIER_SIZE and COURIER_MASS, a recovery's pod
POD_SIZE and POD_MASS.
"""

import math
import random
import zlib
from dataclasses import asdict, dataclass

from shoonyakasha.planet import resources
from shoonyakasha.planet.vec import add, cross, dot, normalize, rotate, scale

import trade

KINDS = ("delivery", "courier", "recovery", "survey")
WEIGHTS = (3, 2, 2, 2)
OFFERS = 3
MAX_ACTIVE = 3
DEADLINE_DAYS = 2
QUANTITIES = (200.0, 400.0, 800.0)     # kg
GRADES = (0.3, 0.5, 0.7)
DELIVERY_MARKUP, DELIVERY_BONUS = 1.4, 100.0
COURIER_BASE, COURIER_PER_KM = 150.0, 4.0
RECOVERY_REACH = (1_000.0, 4_000.0)    # m
RECOVERY_BASE, RECOVERY_PER_KM = 400.0, 60.0
SURVEY_REACH = (1_000.0, 3_000.0)      # m
SURVEY_RADIUS, SURVEY_SHARE, SURVEY_REWARD = 300.0, 0.8, 300.0
REWARD_PER_REPUTATION = 0.1
SAFE_SPEED, DAMAGE_PER, BREAK_DAMAGE = 2.0, 1.0 / 40.0, 1.0
COURIER_SIZE, COURIER_MASS = 0.8, 60.0       # m, kg: the fragile crate
COURIER_COLOUR = (0.92, 0.92, 0.95)
POD_SIZE, POD_MASS = 0.9, 200.0              # m, kg: a recovery's cargo pod
POD_COLOUR = (1.0, 0.8, 0.2)


@dataclass
class Contract:
    id: str
    kind: str
    origin: str
    destination: str
    reward: float
    deadline: int                      # the last day it may be done on
    resource: int = 0                  # delivery
    quantity: float = 0.0
    min_grade: float = 0.0
    target: tuple = None               # recovery, survey: a unit direction from the planet's centre
    delivered: float = 0.0             # kg
    damage: float = 0.0                # courier
    explored: float = 0.0              # survey: the share explored
    state: str = "offered"


def _away(direction, distance, bearing, radius):
    """The unit direction `distance` metres along the ground from `direction` at `bearing` (radians from east)."""
    east = normalize(cross((0.0, 1.0, 0.0), direction)) if abs(direction[1]) < 0.99 else (1.0, 0.0, 0.0)
    north = cross(direction, east)
    axis = normalize(add(scale(east, -math.sin(bearing)), scale(north, math.cos(bearing))))
    return normalize(rotate(direction, axis, distance / radius))


def ground_km(a, b, radius):
    return radius * math.acos(max(-1.0, min(1.0, dot(a, b)))) / 1_000.0


def offers(outpost, day, seed, outposts, radius, reputation=0.0):
    """See the module's docstring. `outpost` and each of `outposts` are (name, unit direction)."""
    name, here = outpost
    rng = random.Random(zlib.crc32(("%s:%d:%r" % (name, day, tuple(seed))).encode()))
    others = [o for o in outposts if o[0] != name]
    scale_reward = 1.0 + REWARD_PER_REPUTATION * reputation
    out = []
    for k in range(OFFERS):
        kind = rng.choices(KINDS, weights=WEIGHTS)[0]
        if kind == "courier" and not others:
            kind = "delivery"
        c = Contract("%s/%d/%d" % (name, day, k), kind, name, name, 0.0, day + DEADLINE_DAYS)
        if kind == "delivery":
            c.resource = rng.randint(resources.FERRITE, resources.EVAPORITE)
            c.quantity, c.min_grade = rng.choice(QUANTITIES), rng.choice(GRADES)
            value = trade.VALUES[c.resource] * c.min_grade ** trade.GRADE_POWER * c.quantity
            reward = value * DELIVERY_MARKUP + DELIVERY_BONUS
        elif kind == "courier":
            c.destination, there = rng.choice(others)
            reward = COURIER_BASE + COURIER_PER_KM * ground_km(here, there, radius)
        elif kind == "recovery":
            distance = rng.uniform(*RECOVERY_REACH)
            c.target = _away(here, distance, rng.uniform(0.0, 2.0 * math.pi), radius)
            reward = RECOVERY_BASE + RECOVERY_PER_KM * distance / 1_000.0
        else:
            c.target = _away(here, rng.uniform(*SURVEY_REACH), rng.uniform(0.0, 2.0 * math.pi), radius)
            reward = SURVEY_REWARD
        c.reward = round(reward * scale_reward, -1)
        out.append(c)
    return out


def task(c):
    if c.kind == "delivery":
        return "Bring %.0f kg of %s, grade %.1f or better, to %s" % (c.quantity, resources.name(c.resource).lower(),
                                                                   c.min_grade, c.destination)
    if c.kind == "courier":
        return "Carry a fragile crate from %s to %s" % (c.origin, c.destination)
    if c.kind == "recovery":
        return "Bring the cargo pod at the marker back to %s" % c.destination
    return "Explore %d %% of the ground within %.0f m of the marker" % (round(100 * SURVEY_SHARE), SURVEY_RADIUS)


def progress(c):
    if c.kind == "delivery":
        return min(1.0, c.delivered / c.quantity)
    if c.kind == "survey":
        return min(1.0, c.explored / SURVEY_SHARE)
    return 1.0 if c.state == "completed" else 0.0


def wants(c, item, outpost_name):
    return (c.state == "active" and c.kind == "delivery" and outpost_name == c.destination
            and item.get("resource") == c.resource and item.get("grade", 0.0) >= c.min_grade
            and c.delivered < c.quantity)


def strike(c, speed):
    c.damage += max(0.0, speed - SAFE_SPEED) ** 2 * DAMAGE_PER
    return c.damage >= BREAK_DAMAGE


def record(c):
    r = asdict(c)
    r["target"] = list(c.target) if c.target is not None else None
    return r


def restore(r):
    r = dict(r)
    r["target"] = tuple(r["target"]) if r.get("target") is not None else None
    return Contract(**r)

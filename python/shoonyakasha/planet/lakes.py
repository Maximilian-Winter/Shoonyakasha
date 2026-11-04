"""
Lakes found by a survey (world.Lake): seeded candidates on the planet
around a centre, each measured at its middle and around its shore, the
best made into lakes.

`LakeSurvey(body, centre, seed)` gives CANDIDATES directions between
REACH's distances along the ground from the unit `centre` (evenly by
area, from a random generator seeded with `seed`'s repr), each with a
shore radius between RADII, and the points to measure the terrain at
(`points`, (direction, body) pairs for world.start_survey): the candidate,
then RING points on its shore.

`choose(heights, avoid=())` takes the points' heights in `points`' order
and keeps the candidates whose shore points all lie DRY or more above
sea level and whose relief (highest shore point less lowest) is at most
RELIEF times their radius, and whose reach (`reach`: the radius times 1 +
LAKE_WOBBLE_REACH times WOBBLE, plus the bank) stays clear of every `avoid`
circle ((unit direction, radius in metres) pairs). It takes the deepest
hollows first (the shore points' mean less the middle; ties by index),
each at least SPACING times the sum of the two reaches from those taken
before it, up to COUNT. Each becomes a Lake with its water FREEBOARD below
its lowest shore point, DEPTH_SHARE of its radius deep (within DEPTH), a
bank BANK_SHARE of its radius wide and WOBBLE.
"""

import math
import random

from .vec import add, dot, local_frame, normalize, scale
from .world import LAKE_WOBBLE_REACH, MAX_LAKES, Lake

CANDIDATES = 240
RING = 8
REACH = (1_500.0, 40_000.0)          # m along the ground from the centre
RADII = (100.0, 400.0)               # m, a lake's shore radius
DRY = 8.0                            # m above sea level every shore point lies, at least
RELIEF = 0.15                        # of the radius: the shore's relief, at most
SPACING = 1.5                        # times the sum of two lakes' reaches, at least
COUNT = 24
FREEBOARD = 0.5                      # m the water lies below the lowest shore point
DEPTH_SHARE = 0.05
DEPTH = (3.0, 25.0)                  # m
BANK_SHARE = 0.6
WOBBLE = 0.25

assert COUNT <= MAX_LAKES


def towards(direction, radius, bearing, distance):
    """The unit direction `distance` metres along a sphere of `radius` from
    the unit `direction` towards `bearing` (radians from north towards east)."""
    east, north = local_frame(direction)
    tangent = add(scale(north, math.cos(bearing)), scale(east, math.sin(bearing)))
    angle = distance / radius
    return normalize(add(scale(direction, math.cos(angle)), scale(tangent, math.sin(angle))))


def ground_distance(radius, a, b):
    return radius * math.acos(max(-1.0, min(1.0, dot(a, b))))


def reach(radius):
    """Metres from a lake's centre its ground can change out to, for a lake of shore `radius`."""
    return radius * (1.0 + LAKE_WOBBLE_REACH * WOBBLE) + BANK_SHARE * radius


class LakeSurvey:
    def __init__(self, body, centre, seed):
        """`body`: the planet's world body; `centre`: unit direction the
        candidates lie around; `seed`: anything with a repr."""
        self.body = body
        rng = random.Random(repr(("lakes", seed)))
        self.candidates = []         # (direction, radius)
        self.points = []             # (direction, body), RING + 1 per candidate
        near2, far2 = REACH[0] ** 2, REACH[1] ** 2
        for _ in range(CANDIDATES):
            bearing = rng.uniform(0.0, 2.0 * math.pi)
            distance = math.sqrt(rng.uniform(near2, far2))
            radius = rng.uniform(*RADII)
            direction = towards(normalize(centre), body.radius, bearing, distance)
            self.candidates.append((direction, radius))
            self.points.append((direction, body))
            self.points += [(towards(direction, body.radius, 2.0 * math.pi * k / RING, radius), body)
                            for k in range(RING)]

    def choose(self, heights, avoid=()):
        """The lakes from the points' `heights` (in `points`' order), kept
        clear of the `avoid` circles: (unit direction, radius in metres)."""
        per = RING + 1
        planet_radius = self.body.radius
        kept = []
        for k, (direction, radius) in enumerate(self.candidates):
            middle, shore = heights[k * per], heights[k * per + 1:(k + 1) * per]
            if min(shore) < DRY or max(shore) - min(shore) > RELIEF * radius:
                continue
            if any(ground_distance(planet_radius, direction, d) < reach(radius) + r for d, r in avoid):
                continue
            hollow = sum(shore) / len(shore) - middle
            kept.append((-hollow, k, direction, radius, min(shore)))
        kept.sort(key=lambda c: (c[0], c[1]))
        lakes, taken = [], []
        for _, _, direction, radius, lowest in kept:
            if len(lakes) >= COUNT:
                break
            if any(ground_distance(planet_radius, direction, d) < SPACING * (reach(radius) + reach(r))
                   for d, r in taken):
                continue
            taken.append((direction, radius))
            depth = min(max(DEPTH_SHARE * radius, DEPTH[0]), DEPTH[1])
            lakes.append(Lake(direction, lowest - FREEBOARD, radius, depth, BANK_SHARE * radius, WOBBLE))
        return lakes

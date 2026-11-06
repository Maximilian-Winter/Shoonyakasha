"""
Trading at the outposts (outposts.py): selling ore and buying fuel for
credits.

Cargo items are dicts: "body" (the name of the body the ore was mined
on), "direction" (where, a unit vector from that body's centre), "mass"
(kg), "grade" (its ore richness, 0 to 1) and "resource" (its id in
shoonyakasha.planet.resources; an item without one is ferrite).

An outpost pays for a kilogram of ore its resource's VALUES (0 for a
resource not there) times its grade to the power GRADE_POWER, times
1 + REPUTATION_PRICE times its `reputation` (0 without one), times
OTHER_BODY when the ore comes from the other body, or else 1 plus
DISTANCE_PREMIUM for every 100 km along the ground from where it was
mined, and divided by 1 + supply / SUPPLY_HALF, where `supply` is the
kilograms of that body's ore the outpost has bought, falling back towards
0 with the time constant SUPPLY_RECOVERY (seconds of the world's clock).
Selling m kilograms from supply s pays the price integrated over the
supply as it rises: base SUPPLY_HALF ln((1 + (s + m) / SUPPLY_HALF) /
(1 + s / SUPPLY_HALF)), so a load sold at once fetches what its pieces
sold one after another do.

Fuel costs the outpost's `fuel_price` per kilogram. Away from an
outpost fuel can be called from the nearest (`fuel_call`): at its price
times CALL_FACTOR on the ground or CALL_ORBIT_FACTOR in orbit, for a fee
of CALL_FEE plus CALL_FEE_PER_KM per km along the ground, arriving
CALL_DELAY plus the distance at CALL_SPEED later. `Market` holds the
credits, sells a ship's cargo (`sell`), fills its tanks as far as the
credits go (`buy_fuel`, and `call_fuel` for called fuel with its fee),
sells survey data (`sell_survey`, priced by `survey_sale` as ore is,
falling with the outpost's supply of it under SURVEY), and gives and takes
its state as a record for a save file.
"""

import math

from shoonyakasha.planet.resources import AMBER, CUPRITE, EVAPORITE, FERRITE, ICE, ILMENITE, QUARTZ
from shoonyakasha.planet.vec import dot

# Credits per kg at grade 1 by resource.
VALUES = {FERRITE: 2.0, CUPRITE: 3.0, QUARTZ: 4.67, AMBER: 6.0, EVAPORITE: 1.67, ICE: 1.33, ILMENITE: 10.0}
GRADE_POWER = 1.5
REPUTATION_PRICE = 0.02          # added to 1 per point of the outpost's reputation, as a factor of its price
OTHER_BODY = 1.8                 # times the price for ore from the other body
DISTANCE_PREMIUM = 0.5           # added to 1 per 100 km along the ground from where the ore was mined
SUPPLY_HALF = 4_000.0            # kg of a body's ore bought that halves an outpost's price for it
SUPPLY_RECOVERY = 900.0          # s: the supply's time constant falling back to 0
START_CREDITS = 2_000.0
# Fuel called away from an outpost: the nearest outpost's fuel price times CALL_FACTOR on the ground
# (CALL_ORBIT_FACTOR in orbit), a fee of CALL_FEE plus CALL_FEE_PER_KM per km along the ground from it,
# and arriving CALL_DELAY plus the distance at CALL_SPEED later.
CALL_FACTOR = 3.0
CALL_ORBIT_FACTOR = 6.0
CALL_FEE = 200.0                 # credits
CALL_FEE_PER_KM = 2.0            # credits
CALL_DELAY = 20.0                # s
CALL_SPEED = 400.0               # m/s
SURVEY = "survey"                # an outpost's supply key for survey data
SURVEY_HALF = 2_000.0            # credits of survey data bought that halve an outpost's price for it


def ground_distance(radius, a, b):
    """Metres along a sphere of `radius` between unit directions `a` and `b`."""
    return radius * math.acos(max(-1.0, min(1.0, dot(a, b))))


def base_price(outpost, item):
    """Credits per kg the outpost pays for `item` with no supply."""
    price = (VALUES.get(item.get("resource", FERRITE), 0.0) * item["grade"] ** GRADE_POWER
             * (1.0 + REPUTATION_PRICE * getattr(outpost, "reputation", 0.0)))
    if item["body"] != outpost.body.name:
        return price * OTHER_BODY
    distance = ground_distance(outpost.body.radius, outpost.direction, item["direction"])
    return price * (1.0 + DISTANCE_PREMIUM * distance / 100_000.0)


def supply(outpost, body, time):
    """kg of `body`'s ore (a name) the outpost has bought, at `time`."""
    kg, since = outpost.supply.get(body, (0.0, time))
    return kg * math.exp(-max(time - since, 0.0) / SUPPLY_RECOVERY)


def price(outpost, item, time):
    """Credits per kg the outpost pays now for the first kilogram of `item`."""
    return base_price(outpost, item) / (1.0 + supply(outpost, item["body"], time) / SUPPLY_HALF)


def sale(outpost, items, time):
    """Credits `items` fetch sold at once, and the outpost's supply after
    (body name: (kg, time)), without changing it."""
    supplies = {}
    credits = 0.0
    for item in items:
        body = item["body"]
        s = supplies[body] if body in supplies else supply(outpost, body, time)
        m = item["mass"]
        credits += base_price(outpost, item) * SUPPLY_HALF * math.log((1.0 + (s + m) / SUPPLY_HALF)
                                                                       / (1.0 + s / SUPPLY_HALF))
        supplies[body] = s + m
    return credits, {body: (kg, time) for body, kg in supplies.items()}


def survey_sale(outpost, value, time):
    """Credits survey data worth `value` (credits with no supply) fetches at
    the outpost, its price divided by 1 + supply / SURVEY_HALF as ore's,
    the supply the data it bought (under SURVEY, falling back as ore's), and
    the outpost's supply after, without changing it."""
    s = supply(outpost, SURVEY, time)
    credits = SURVEY_HALF * math.log((1.0 + (s + value) / SURVEY_HALF) / (1.0 + s / SURVEY_HALF))
    return credits, {SURVEY: (s + value, time)}


def fuel_call(outpost, distance, in_orbit):
    """A fuel call from `outpost` `distance` metres along the ground away,
    to a ship in orbit or not: (credits per kg, the fee, seconds until it
    arrives)."""
    factor = CALL_ORBIT_FACTOR if in_orbit else CALL_FACTOR
    return (outpost.fuel_price * factor, CALL_FEE + CALL_FEE_PER_KM * distance / 1_000.0,
            CALL_DELAY + distance / CALL_SPEED)


class Market:
    def __init__(self, credits=START_CREDITS):
        self.credits = credits

    def value(self, outpost, items, time):
        """Credits `items` would fetch at the outpost now."""
        return sale(outpost, items, time)[0]

    def sell(self, ship, outpost, time):
        """The ship's whole cargo sold at the outpost: the credits gained,
        the outpost's supply raised. Returns (kg sold, credits)."""
        items = list(ship.cargo)
        if not items:
            return 0.0, 0.0
        credits, supplies = sale(outpost, items, time)
        outpost.supply.update(supplies)
        ship.cargo.clear()
        self.credits += credits
        return sum(item["mass"] for item in items), credits

    def buy_fuel(self, ship, outpost):
        """The ship's tanks filled at the outpost as far as the credits go.
        Returns (kg bought, credits paid)."""
        room = ship.params.fuel_capacity - ship.fuel
        kg = max(min(room, self.credits / outpost.fuel_price), 0.0)
        cost = kg * outpost.fuel_price
        ship.fuel += kg
        self.credits -= cost
        return kg, cost

    def sell_survey(self, outpost, value, time):
        """Survey data worth `value` sold at the outpost (survey_sale): the
        credits gained, the supply raised. Returns the credits."""
        if value <= 0.0:
            return 0.0
        credits, supplies = survey_sale(outpost, value, time)
        outpost.supply.update(supplies)
        self.credits += credits
        return credits

    def call_fuel(self, ship, price, fee):
        """Called fuel delivered: the ship's tanks filled at `price` per kg as
        far as the credits left after `fee` go, the fee paid only when some
        fuel comes. Returns (kg delivered, credits paid)."""
        room = ship.params.fuel_capacity - ship.fuel
        kg = max(min(room, (self.credits - fee) / price), 0.0)
        if kg <= 0.0:
            return 0.0, 0.0
        cost = fee + kg * price
        ship.fuel += kg
        self.credits -= cost
        return kg, cost

    def record(self, outposts):
        """The credits and each outpost's supply, as plain values."""
        return {"credits": self.credits,
                "supply": {o.name: {body: list(value) for body, value in o.supply.items()} for o in outposts}}

    def restore(self, record, outposts):
        self.credits = record["credits"]
        by_name = {o.name: o for o in outposts}
        for name, supplies in record["supply"].items():
            if name in by_name:
                by_name[name].supply = {body: tuple(value) for body, value in supplies.items()}

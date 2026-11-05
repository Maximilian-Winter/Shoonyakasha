"""Tests for the planet example's trading (examples/python/simulations/
planet/trade.py), loaded from its file: an outpost pays its resource's
value times the grade to GRADE_POWER (ferrite for an item without a
resource, nothing for none), more from farther along the ground or from
the other body; its
supply lowers its price and falls back with time; a load sold at once
fetches what its pieces sold one after another do; a sale and a purchase
of fuel move the credits, the cargo, the fuel and the supply as they
should; a fuel call is priced by its factor, fee and delay and pays its
fee only with fuel, as far as the credits go; and the market's record
restores it.

    python -m unittest discover -s tests/python
"""

import importlib.util
import math
import unittest
from pathlib import Path

from shoonyakasha.planet.resources import AMBER, FERRITE, ICE, ILMENITE, NONE

REPO = Path(__file__).resolve().parents[2]
EXAMPLE = REPO / "examples" / "python" / "simulations" / "planet"
_SPEC = importlib.util.spec_from_file_location("planet_trade", EXAMPLE / "trade.py")
trade = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(trade)


class Body:
    def __init__(self, name, radius):
        self.name, self.radius = name, radius


class Outpost:
    def __init__(self, body, direction=(0.0, 0.0, 1.0), fuel_price=1.0):
        self.body, self.direction, self.fuel_price = body, direction, fuel_price
        self.name = "Test"
        self.supply = {}


class Params:
    fuel_capacity = 6_000.0


class Ship:
    def __init__(self, cargo=(), fuel=1_000.0):
        self.cargo = list(cargo)
        self.fuel = fuel
        self.params = Params()


PLANET, MOON = Body("planet", 100_000.0), Body("moon", 25_000.0)


def ore(body, mass, grade=1.0, direction=(0.0, 0.0, 1.0), resource=FERRITE):
    return {"body": body, "direction": direction, "mass": mass, "grade": grade, "resource": resource}


def towards(angle):
    """A unit direction `angle` radians from +Z towards +X."""
    return (math.sin(angle), 0.0, math.cos(angle))


class Prices(unittest.TestCase):
    def test_grade_distance_and_the_other_body(self):
        outpost = Outpost(PLANET)
        here = trade.base_price(outpost, ore("planet", 1.0, 0.5))
        self.assertAlmostEqual(here, trade.VALUES[FERRITE] * 0.5 ** trade.GRADE_POWER)
        far = trade.base_price(outpost, ore("planet", 1.0, 0.5, towards(0.5)))      # 50 km along the ground
        self.assertAlmostEqual(far, here * (1.0 + 0.5 * trade.DISTANCE_PREMIUM))
        other = trade.base_price(outpost, ore("moon", 1.0, 0.5, towards(2.0)))
        self.assertAlmostEqual(other, here * trade.OTHER_BODY)

    def test_value_by_resource(self):
        outpost = Outpost(PLANET)
        for resource in (FERRITE, AMBER, ILMENITE):
            self.assertAlmostEqual(trade.base_price(outpost, ore("planet", 1.0, 0.64, resource=resource)),
                                   trade.VALUES[resource] * 0.512)
        self.assertGreater(trade.VALUES[ILMENITE], trade.VALUES[AMBER])
        self.assertLess(trade.VALUES[ICE], trade.VALUES[FERRITE])
        # An item without a resource is ferrite; one of no resource fetches nothing.
        legacy = ore("planet", 1.0, 0.64)
        del legacy["resource"]
        self.assertEqual(trade.base_price(outpost, legacy), trade.base_price(outpost, ore("planet", 1.0, 0.64)))
        self.assertEqual(trade.base_price(outpost, ore("planet", 1.0, 0.64, resource=NONE)), 0.0)

    def test_supply_lowers_the_price_and_falls_back(self):
        outpost = Outpost(PLANET)
        item = ore("planet", 1.0)
        base = trade.price(outpost, item, 0.0)
        outpost.supply["planet"] = (trade.SUPPLY_HALF, 100.0)
        self.assertAlmostEqual(trade.price(outpost, item, 100.0), base / 2.0)
        later = 100.0 + trade.SUPPLY_RECOVERY
        self.assertAlmostEqual(trade.supply(outpost, "planet", later), trade.SUPPLY_HALF / math.e)
        self.assertAlmostEqual(trade.price(outpost, item, later), base / (1.0 + 1.0 / math.e))
        self.assertAlmostEqual(trade.price(outpost, ore("moon", 1.0), 100.0),
                               trade.base_price(outpost, ore("moon", 1.0)))

    def test_a_load_fetches_what_its_pieces_do(self):
        outpost = Outpost(PLANET)
        pieces = [ore("planet", m, 0.7) for m in (300.0, 1_200.0, 2_500.0)]
        whole, supplies = trade.sale(outpost, [ore("planet", 4_000.0, 0.7)], 0.0)
        one_by_one = 0.0
        for piece in pieces:
            credits, after = trade.sale(outpost, [piece], 0.0)
            one_by_one += credits
            outpost.supply.update(after)
        self.assertAlmostEqual(one_by_one, whole, places=6)
        self.assertAlmostEqual(supplies["planet"][0], 4_000.0)
        # The integral: base H ln((1 + m / H) / 1).
        self.assertAlmostEqual(whole, trade.VALUES[FERRITE] * 0.7 ** trade.GRADE_POWER * trade.SUPPLY_HALF
                               * math.log(2.0), places=6)


class MarketTest(unittest.TestCase):
    def test_sell_moves_the_credits_cargo_and_supply(self):
        market = trade.Market(credits=100.0)
        outpost = Outpost(PLANET)
        ship = Ship([ore("planet", 1_000.0, 0.6), ore("moon", 500.0, 0.9)])
        expected = trade.sale(outpost, ship.cargo, 50.0)[0]
        kg, credits = market.sell(ship, outpost, 50.0)
        self.assertEqual((kg, ship.cargo), (1_500.0, []))
        self.assertAlmostEqual(credits, expected)
        self.assertAlmostEqual(market.credits, 100.0 + expected)
        self.assertEqual(outpost.supply, {"planet": (1_000.0, 50.0), "moon": (500.0, 50.0)})
        self.assertEqual(market.sell(ship, outpost, 60.0), (0.0, 0.0))

    def test_fuel_fills_as_far_as_the_credits_go(self):
        outpost = Outpost(MOON, fuel_price=3.0)
        market = trade.Market(credits=3_000.0)
        ship = Ship(fuel=1_000.0)
        self.assertEqual(market.buy_fuel(ship, outpost), (1_000.0, 3_000.0))
        self.assertEqual((ship.fuel, market.credits), (2_000.0, 0.0))
        market.credits = 1e6
        kg, cost = market.buy_fuel(ship, outpost)
        self.assertEqual((kg, cost, ship.fuel), (4_000.0, 12_000.0, 6_000.0))
        self.assertEqual(market.buy_fuel(ship, outpost), (0.0, 0.0))

    def test_a_fuel_call_costs_more_and_pays_its_fee_only_with_fuel(self):
        outpost = Outpost(PLANET, fuel_price=1.0)
        price, fee, delay = trade.fuel_call(outpost, 30_000.0, False)
        self.assertEqual(price, trade.CALL_FACTOR)
        self.assertAlmostEqual(fee, trade.CALL_FEE + 30.0 * trade.CALL_FEE_PER_KM)
        self.assertAlmostEqual(delay, trade.CALL_DELAY + 30_000.0 / trade.CALL_SPEED)
        self.assertEqual(trade.fuel_call(outpost, 30_000.0, True)[0], trade.CALL_ORBIT_FACTOR)
        # As far as the credits go after the fee.
        market = trade.Market(credits=1_000.0)
        ship = Ship(fuel=0.0)
        kg, cost = market.call_fuel(ship, price, fee)
        self.assertAlmostEqual(kg, (1_000.0 - fee) / price)
        self.assertAlmostEqual((cost, market.credits), (1_000.0, 0.0))
        self.assertAlmostEqual(ship.fuel, kg)
        # No fuel when the credits do not cover the fee, and then no fee.
        market.credits = fee - 1.0
        self.assertEqual(market.call_fuel(ship, price, fee), (0.0, 0.0))
        self.assertEqual(market.credits, fee - 1.0)
        # At most what fills the tanks.
        market.credits = 1e6
        ship.fuel = 5_500.0
        self.assertEqual(market.call_fuel(ship, price, fee), (500.0, fee + 500.0 * price))
        self.assertEqual(ship.fuel, 6_000.0)

    def test_survey_data_sells_as_ore_with_its_own_supply(self):
        outpost = Outpost(PLANET)
        market = trade.Market(credits=0.0)
        first = market.sell_survey(outpost, 500.0, 0.0)
        self.assertAlmostEqual(first, trade.SURVEY_HALF * math.log(1.0 + 500.0 / trade.SURVEY_HALF))
        self.assertLess(first, 500.0)
        self.assertEqual(outpost.supply, {trade.SURVEY: (500.0, 0.0)})
        self.assertLess(market.sell_survey(outpost, 500.0, 0.0), first)
        self.assertEqual(market.sell_survey(outpost, 0.0, 0.0), 0.0)
        self.assertAlmostEqual(trade.supply(outpost, "planet", 0.0), 0.0)     # ore's supply untouched

    def test_record_restores(self):
        a, b = Outpost(PLANET), Outpost(MOON)
        b.name = "Other"
        a.supply = {"moon": (250.0, 12.0)}
        market = trade.Market(credits=1_234.5)
        record = market.record([a, b])
        fresh_a, fresh_b = Outpost(PLANET), Outpost(MOON)
        fresh_b.name = "Other"
        restored = trade.Market()
        restored.restore(record, [fresh_a, fresh_b])
        self.assertEqual(restored.credits, 1_234.5)
        self.assertEqual(fresh_a.supply, {"moon": (250.0, 12.0)})
        self.assertEqual(fresh_b.supply, {})


if __name__ == "__main__":
    unittest.main()

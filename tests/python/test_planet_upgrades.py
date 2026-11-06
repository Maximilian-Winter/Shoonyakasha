"""Tests for the planet example's upgrades
(examples/python/simulations/planet/upgrades.py), loaded from its file:
every effect names a parameter of flight.ShipParams, hovercar.CarParams
or GunParams and is shown on the cards, every upgrade it needs is in the
table before it; the effective parameters multiply in the owned
upgrades' effects on the groups given (a game's) and leave the base as
it was; and an upgrade is refused when owned, when it
needs one not owned, or without the credits.

    python -m unittest discover -s tests/python
"""

import importlib.util
import sys
import unittest
from pathlib import Path

from shoonyakasha.planet.gravity_gun import GunParams

REPO = Path(__file__).resolve().parents[2]
EXAMPLE = REPO / "examples" / "python" / "simulations" / "planet"


def load(name):
    spec = importlib.util.spec_from_file_location("planet_" + name, EXAMPLE / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


flight = load("flight")
upgrades = load("upgrades")
sys.path.insert(0, str(EXAMPLE))
hovercar = load("hovercar")
suit = load("suit")
pois = load("pois")


def base():
    return {"ship": flight.ShipParams(), "gun": GunParams()}


def everything():
    return {"ship": flight.ShipParams(), "car": hovercar.CarParams(), "gun": GunParams(), "suit": suit.SuitParams()}


class Table(unittest.TestCase):
    def test_effects_name_parameters_and_needs_come_first(self):
        params = everything()
        seen = set()
        for upgrade in upgrades.UPGRADES:
            for path in upgrade.effects:
                group, attribute = path.split(".")
                self.assertTrue(hasattr(params[group], attribute), path)
                self.assertIn(path, upgrades.EFFECTS)
            for key in upgrade.requires:
                self.assertIn(key, seen, upgrade.key)
            for part in upgrade.parts:
                self.assertIn(part, pois.PARTS)
            seen.add(upgrade.key)
        self.assertEqual(set(upgrades.BY_KEY), seen)

    def test_each_game_offers_those_with_its_effects(self):
        ship = [u.key for u in upgrades.for_groups(("ship", "gun"))]
        walker = [u.key for u in upgrades.for_groups(("car", "gun", "suit"))]
        self.assertNotIn("suit_1", ship)
        self.assertIn("suit_1", walker)
        self.assertIn("engine_1", ship)


class Effective(unittest.TestCase):
    def test_owned_effects_multiply_and_the_base_stays(self):
        b = base()
        out = upgrades.effective(b, {"hold_1", "engine_1", "gun_force_1"})
        self.assertEqual(b["ship"].cargo_capacity, flight.ShipParams().cargo_capacity)
        self.assertAlmostEqual(out["ship"].cargo_capacity, 1.5 * b["ship"].cargo_capacity)
        self.assertAlmostEqual(out["ship"].hold_m3, 1.5 * b["ship"].hold_m3)
        self.assertAlmostEqual(out["ship"].isp_main, 1.15 * b["ship"].isp_main)
        self.assertEqual(out["ship"].fuel_capacity, b["ship"].fuel_capacity)
        self.assertAlmostEqual(out["gun"].hold_force, 1.5 * b["gun"].hold_force)
        self.assertEqual(out["gun"].charged_speed, b["gun"].charged_speed)
        self.assertEqual(upgrades.effective(b, set())["ship"], b["ship"])

    def test_a_game_applies_the_groups_it_has(self):
        car = hovercar.CarParams()
        out = upgrades.effective({"car": car, "gun": GunParams()}, {"hold_1", "tank_1", "engine_1"})
        self.assertEqual(set(out), {"car", "gun"})
        self.assertAlmostEqual(out["car"].cargo_capacity, 1.5 * car.cargo_capacity)
        self.assertAlmostEqual(out["car"].fuel_capacity, 1.5 * car.fuel_capacity)
        self.assertAlmostEqual(out["car"].drive_burn, car.drive_burn / 1.15)
        self.assertAlmostEqual(out["car"].space_isp, 1.15 * car.space_isp)

    def test_the_long_range_tank(self):
        car, ship = hovercar.CarParams(), flight.ShipParams()
        out = upgrades.effective({"car": car, "ship": ship}, {"tank_1", "engine_1", "longrange"})
        self.assertAlmostEqual(out["car"].fuel_capacity, 1.5 * 1.6 * car.fuel_capacity)
        self.assertAlmostEqual(out["ship"].fuel_capacity, 1.5 * 1.6 * ship.fuel_capacity)
        self.assertEqual(upgrades.BY_KEY["longrange"].requires, ("engine_1",))
        p = out["car"]
        self.assertGreater(hovercar.delta_v(p, p.fuel_capacity), 2.0 * hovercar.delta_v(car, car.fuel_capacity))
        self.assertEqual(set(upgrades.effects_for(upgrades.BY_KEY["hold_1"], {"car"})),
                         {"car.cargo_capacity", "car.hold_m3"})


class Additive(unittest.TestCase):
    def test_limits_added_and_reserve_multiplied(self):
        s = suit.SuitParams()
        out = upgrades.effective({"suit": s}, {"suit_1", "suit_2"})["suit"]
        self.assertAlmostEqual(out.cold_limit, s.cold_limit - 10.0)
        self.assertAlmostEqual(out.hot_limit, s.hot_limit + 10.0)
        self.assertAlmostEqual(out.reserve, s.reserve * 1.5 * 1.5)


class Refusal(unittest.TestCase):
    def test_parts_only_where_a_game_has_them(self):
        bank = upgrades.BY_KEY["gun_charge_1"]
        self.assertIsNone(upgrades.refusal(bank, {"gun_force_1"}, 1e6))
        self.assertEqual(upgrades.refusal(bank, {"gun_force_1"}, 1e6, {}), "needs 1 coil")
        self.assertIsNone(upgrades.refusal(bank, {"gun_force_1"}, 1e6, {"coil": 1}))
        engine = upgrades.BY_KEY["engine_1"]
        self.assertEqual(upgrades.refusal(engine, {"tank_1"}, 1e6, {"injector": 1}), "needs 2 injectors")

    def test_owned_needs_and_credits(self):
        engine = upgrades.BY_KEY["engine_1"]
        self.assertEqual(upgrades.refusal(engine, set(), 1e6), "needs Auxiliary tank")
        self.assertEqual(upgrades.refusal(engine, {"tank_1"}, engine.cost - 1.0), "not enough credits")
        self.assertIsNone(upgrades.refusal(engine, {"tank_1"}, engine.cost))
        self.assertEqual(upgrades.refusal(engine, {"tank_1", "engine_1"}, 1e6), "owned")


if __name__ == "__main__":
    unittest.main()

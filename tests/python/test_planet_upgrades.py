"""Tests for the planet example's upgrades
(examples/python/simulations/planet/upgrades.py), loaded from its file:
every effect names a parameter of flight.ShipParams or GunParams and is
shown on the cards, every upgrade it needs is in the table before it;
the effective parameters multiply in the owned upgrades' effects and
leave the base as it was; and an upgrade is refused when owned, when it
needs one not owned, or without the credits.

    python -m unittest discover -s tests/python
"""

import importlib.util
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


def base():
    return {"ship": flight.ShipParams(), "gun": GunParams()}


class Table(unittest.TestCase):
    def test_effects_name_parameters_and_needs_come_first(self):
        params = base()
        seen = set()
        for upgrade in upgrades.UPGRADES:
            for path in upgrade.effects:
                group, attribute = path.split(".")
                self.assertTrue(hasattr(params[group], attribute), path)
                self.assertIn(path, upgrades.EFFECTS)
            for key in upgrade.requires:
                self.assertIn(key, seen, upgrade.key)
            seen.add(upgrade.key)
        self.assertEqual(set(upgrades.BY_KEY), seen)


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


class Refusal(unittest.TestCase):
    def test_owned_needs_and_credits(self):
        engine = upgrades.BY_KEY["engine_1"]
        self.assertEqual(upgrades.refusal(engine, set(), 1e6), "needs Auxiliary tank")
        self.assertEqual(upgrades.refusal(engine, {"tank_1"}, engine.cost - 1.0), "not enough credits")
        self.assertIsNone(upgrades.refusal(engine, {"tank_1"}, engine.cost))
        self.assertEqual(upgrades.refusal(engine, {"tank_1", "engine_1"}, 1e6), "owned")


if __name__ == "__main__":
    unittest.main()

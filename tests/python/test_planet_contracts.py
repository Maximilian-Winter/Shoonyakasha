"""Tests for the walker's contracts (examples/python/simulations/planet/
contracts.py), loaded from its folder: an outpost's offers are the same
for the same day and seed and others on another day, each of a kind with
its task's fields, its reward and its deadline; reputation raises the
rewards; recovery and survey markers lie within their reach; a delivery
takes only matching ore at its outpost; a fragile crate breaks from hard
strikes, not soft ones; and a contract's record, with the time its
deadline was held back, restores it. Plain
Python: no engine needed.

    python -m unittest discover -s tests/python
"""

import math
import sys
import unittest
from pathlib import Path

PLANET_DIR = Path(__file__).resolve().parents[2] / "examples" / "python" / "simulations" / "planet"
sys.path.insert(0, str(PLANET_DIR))

import contracts  # noqa: E402
import trade  # noqa: E402
from shoonyakasha.planet import resources  # noqa: E402
from shoonyakasha.planet.vec import normalize  # noqa: E402

RADIUS = 100_000.0
SEED = (0.0, 0.0, 0.0, 0.0)
BASE = ("Base", (0.0, 0.0, 1.0))
HALDEN = ("Halden", normalize((0.5, 0.0, 1.0)))
OUTPOSTS = [BASE, HALDEN]


def all_offers(days=60, reputation=0.0):
    return [c for d in range(days) for c in contracts.offers(BASE, d, SEED, OUTPOSTS, RADIUS, reputation)]


class Offers(unittest.TestCase):
    def test_same_day_same_offers_and_others_another_day(self):
        a = contracts.offers(BASE, 3, SEED, OUTPOSTS, RADIUS)
        self.assertEqual(len(a), contracts.OFFERS)
        self.assertEqual(a, contracts.offers(BASE, 3, SEED, OUTPOSTS, RADIUS))
        self.assertNotEqual([c.id for c in a], [c.id for c in contracts.offers(BASE, 4, SEED, OUTPOSTS, RADIUS)])

    def test_each_kind_with_its_fields(self):
        found = all_offers()
        self.assertEqual({c.kind for c in found}, set(contracts.KINDS))
        for c in found:
            self.assertEqual((c.origin, c.state), ("Base", "offered"))
            self.assertGreater(c.reward, 0.0)
            self.assertEqual(c.reward % 10.0, 0.0)
            if c.kind == "delivery":
                self.assertTrue(resources.FERRITE <= c.resource <= resources.EVAPORITE)
                self.assertIn(c.quantity, contracts.QUANTITIES)
                self.assertIn(c.min_grade, contracts.GRADES)
                value = trade.VALUES[c.resource] * c.min_grade ** trade.GRADE_POWER * c.quantity
                self.assertAlmostEqual(c.reward, round(value * contracts.DELIVERY_MARKUP + contracts.DELIVERY_BONUS, -1))
            elif c.kind == "courier":
                self.assertEqual(c.destination, "Halden")
            else:
                km = contracts.ground_km(BASE[1], c.target, RADIUS) * 1_000.0
                low, high = contracts.RECOVERY_REACH if c.kind == "recovery" else contracts.SURVEY_REACH
                self.assertTrue(low - 1.0 <= km <= high + 1.0, km)
                self.assertEqual(c.destination, "Base")

    def test_reputation_raises_rewards(self):
        plain, liked = all_offers(10), all_offers(10, reputation=3.0)
        for a, b in zip(plain, liked):
            self.assertAlmostEqual(b.reward, a.reward * 1.3, delta=10.0)


class Tasks(unittest.TestCase):
    def test_short_task_names_each_kind(self):
        c = contracts.Contract("x", "delivery", "Base", "Halden", 500.0, 5, resource=resources.AMBER, quantity=400.0,
                               min_grade=0.5)
        self.assertEqual(contracts.short_task(c), "400 kg amber, grade 0.5+, to Halden")
        for c in all_offers(10):
            self.assertTrue(contracts.short_task(c))
            self.assertLess(len(contracts.short_task(c)), len(contracts.task(c)))

    def test_delivery_takes_matching_ore_at_its_outpost(self):
        c = contracts.Contract("x", "delivery", "Base", "Base", 500.0, 5, resource=resources.AMBER, quantity=400.0,
                               min_grade=0.5, state="active")
        good = {"resource": resources.AMBER, "grade": 0.6, "mass": 100.0}
        self.assertTrue(contracts.wants(c, good, "Base"))
        self.assertFalse(contracts.wants(c, good, "Halden"))
        self.assertFalse(contracts.wants(c, dict(good, grade=0.4), "Base"))
        self.assertFalse(contracts.wants(c, dict(good, resource=resources.QUARTZ), "Base"))
        c.delivered = 300.0
        self.assertAlmostEqual(contracts.progress(c), 0.75)

    def test_fragile_crate_breaks_from_hard_strikes(self):
        c = contracts.Contract("x", "courier", "Base", "Halden", 300.0, 5, state="active")
        self.assertFalse(contracts.strike(c, contracts.SAFE_SPEED))
        self.assertEqual(c.damage, 0.0)
        self.assertFalse(contracts.strike(c, 5.0))
        self.assertTrue(contracts.strike(c, contracts.SAFE_SPEED + math.sqrt(contracts.BREAK_DAMAGE
                                                                              / contracts.DAMAGE_PER)))

    def test_record_restores(self):
        c = [o for o in all_offers() if o.kind == "survey"][0]
        c.state, c.explored, c.paused = "active", 0.4, 123.0
        back = contracts.restore(contracts.record(c))
        self.assertEqual(back, c)
        old = contracts.record(c)
        del old["paused"]
        self.assertEqual(contracts.restore(old).paused, 0.0)


if __name__ == "__main__":
    unittest.main()

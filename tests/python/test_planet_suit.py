"""Tests for the walker's suit (examples/python/simulations/planet/suit.py),
loaded from its file: the air's temperature warmest mid-afternoon with a
daily swing larger where it is dry, wind chill only in the cold; the
moon's from shade to sun; the reserve unspent within the limits, drained
by the degrees beyond them, recharged in shelter with the health
recovering only at an outpost, the health draining once the reserve is
empty until it runs out; and the tint and the slowdown as the reserve
falls. Plain Python: no engine needed.

    python -m unittest discover -s tests/python
"""

import importlib.util
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
_SPEC = importlib.util.spec_from_file_location(
    "planet_suit", REPO / "examples" / "python" / "simulations" / "planet" / "suit.py")
suit = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(suit)


class Temperature(unittest.TestCase):
    def test_daily_swing_and_wind_chill(self):
        warmest = suit.local_temperature(20.0, 0.0, suit.WARMEST_HOUR, 0.0)
        coldest = suit.local_temperature(20.0, 0.0, (suit.WARMEST_HOUR + 12.0) % 24.0, 0.0)
        self.assertAlmostEqual(warmest - 20.0, suit.SWING_WET + suit.SWING_DRY)
        self.assertAlmostEqual(20.0 - coldest, suit.SWING_WET + suit.SWING_DRY)
        wet = suit.local_temperature(20.0, 1.0, suit.WARMEST_HOUR, 0.0)
        self.assertAlmostEqual(wet - 20.0, suit.SWING_WET)
        self.assertEqual(suit.local_temperature(20.0, 0.5, 9.0, 10.0), suit.local_temperature(20.0, 0.5, 9.0, 0.0))
        self.assertAlmostEqual(suit.local_temperature(0.0, 0.5, 9.0, 10.0),
                               suit.local_temperature(0.0, 0.5, 9.0, 0.0) - 10.0 * suit.WIND_CHILL)

    def test_moon_from_shade_to_sun(self):
        self.assertEqual(suit.moon_temperature(-0.5), suit.MOON_SHADE)
        self.assertEqual(suit.moon_temperature(1.0), suit.MOON_SUN)
        self.assertAlmostEqual(suit.moon_temperature(0.5 * suit.MOON_FULL_SUN),
                               0.5 * (suit.MOON_SUN + suit.MOON_SHADE))


class Reserve(unittest.TestCase):
    def test_within_the_limits_nothing_is_spent(self):
        p, s = suit.SuitParams(), suit.SuitState()
        self.assertFalse(suit.step_suit(s, p, 20.0, False, False, 100.0))
        self.assertEqual((s.reserve, s.health), (1.0, 1.0))

    def test_drained_by_the_degrees_beyond_then_the_health(self):
        p, s = suit.SuitParams(), suit.SuitState()
        suit.step_suit(s, p, p.cold_limit - 10.0, False, False, 10.0)
        self.assertAlmostEqual(s.reserve, 1.0 - 10.0 * p.drain * 10.0)
        s.reserve = 0.01
        suit.step_suit(s, p, p.hot_limit + 20.0, False, False, 1.0)             # 0.016 wanted, 0.01 there
        self.assertEqual(s.reserve, 0.0)
        self.assertAlmostEqual(s.health, 1.0 - suit.HEALTH_DRAIN * 0.006)
        self.assertTrue(suit.step_suit(s, p, -200.0, False, False, 1_000.0))
        self.assertEqual(s.health, 0.0)

    def test_shelter_recharges_and_an_outpost_heals(self):
        p = suit.SuitParams()
        s = suit.SuitState(reserve=0.2, health=0.5)
        suit.step_suit(s, p, -40.0, True, False, 4.0)                          # in the car: no drain
        self.assertAlmostEqual(s.reserve, 0.2 + 4.0 * p.recharge)
        self.assertEqual(s.health, 0.5)
        suit.step_suit(s, p, -40.0, True, True, 20.0)
        self.assertEqual(s.reserve, p.reserve)
        self.assertAlmostEqual(s.health, 0.5 + 20.0 * p.heal)


class Effects(unittest.TestCase):
    def test_tint_and_slowdown(self):
        p = suit.SuitParams()
        (colour, alpha), factor = suit.effects(suit.SuitState(reserve=0.8), p, -30.0)
        self.assertEqual((alpha, factor), (0.0, 1.0))
        (colour, alpha), factor = suit.effects(suit.SuitState(reserve=0.1), p, -30.0)
        self.assertEqual(colour, suit.FROST)
        self.assertAlmostEqual(alpha, suit.TINT_ALPHA * 0.8)
        self.assertEqual(factor, suit.SLOW_FACTOR)
        (colour, _), _ = suit.effects(suit.SuitState(reserve=0.1), p, 50.0)
        self.assertEqual(colour, suit.HEAT)


if __name__ == "__main__":
    unittest.main()

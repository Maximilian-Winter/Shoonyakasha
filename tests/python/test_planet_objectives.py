"""Tests for the walker's objectives
(examples/python/simulations/planet/objectives.py), loaded from its file:
the goals met in order (several in one update when their numbers are
reached together, none out of turn), the progress and its text, and the
ore that sells: on a pad of its own body, at rest and not held.

    python -m unittest discover -s tests/python
"""

import importlib.util
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
_SPEC = importlib.util.spec_from_file_location(
    "planet_objectives", REPO / "examples" / "python" / "simulations" / "planet" / "objectives.py")
objectives = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(objectives)


def numbers(**given):
    snapshot = {goal.key: 0 for goal in objectives.GOALS}
    snapshot.update(given)
    return snapshot


class Body:
    def __init__(self, name):
        self.name = name

    def centre(self):
        return (0.0, 0.0, 0.0)


class Pad:
    def __init__(self, body, x):
        self.body, self.x, self.name = body, x, "pad"

    def at(self, position):
        return abs(position[0] - self.x) < 10.0


class Prop:
    def __init__(self, mass, grade):
        self.mass, self.grade = mass, grade


class ObjectivesTest(unittest.TestCase):
    def test_goals_met_in_order(self):
        o = objectives.Objectives()
        first = objectives.GOALS[0]
        self.assertIs(o.current(), first)
        self.assertEqual(o.update(numbers(cells=100, ore_seen=1)), [])   # a later goal waits its turn
        self.assertEqual(o.progress(numbers(cells=100)), (100, first.target))
        self.assertEqual(o.update(numbers(cells=first.target, waypoints=1, ore_seen=1)),
                         list(objectives.GOALS[:3]))
        self.assertEqual(o.current().key, "ore_chunks")
        everything = numbers(**{goal.key: goal.target for goal in objectives.GOALS})
        self.assertEqual(len(o.update(everything)), len(objectives.GOALS) - 3)
        self.assertIsNone(o.current())
        self.assertIsNone(o.progress(everything))
        self.assertEqual(o.update(everything), [])

    def test_progress_text(self):
        goals = {goal.key: goal for goal in objectives.GOALS}
        self.assertEqual(objectives.progress_text(goals["cells"], 212, 400), "212 / 400 cells")
        self.assertEqual(objectives.progress_text(goals["earned"], 1234.6, 2000), "1,234 / 2,000 cr")
        self.assertEqual(objectives.progress_text(goals["ore_seen"], 0, 1), "not yet")
        self.assertEqual(objectives.progress_text(goals["ore_seen"], 1, 1), "done")


class SaleTest(unittest.TestCase):
    def test_ore_at_rest_on_a_pad_of_its_body_sells(self):
        planet, moon = Body("planet"), Body("moon")
        pads = [Pad(planet, 0.0), Pad(moon, 100.0)]
        seen = ("planet", (0.0, 0.0, 1.0))
        resting, moving, held, elsewhere, wrong_body = (Prop(100.0, 0.5) for _ in range(5))
        ore = [(resting, planet, (2.0, 0.0, 0.0), 0.1, seen),
               (moving, planet, (2.0, 0.0, 0.0), 3.0, seen),
               (held, planet, (2.0, 0.0, 0.0), 0.0, seen),
               (elsewhere, planet, (50.0, 0.0, 0.0), 0.0, seen),
               (wrong_body, planet, (100.0, 0.0, 0.0), 0.0, seen)]
        sold = objectives.sale_items(ore, pads, held)
        self.assertEqual(len(sold), 1)
        pad, prop, item = sold[0]
        self.assertIs(pad, pads[0])
        self.assertIs(prop, resting)
        self.assertEqual(item, {"body": "planet", "direction": (0.0, 0.0, 1.0), "mass": 100.0, "grade": 0.5})


if __name__ == "__main__":
    unittest.main()

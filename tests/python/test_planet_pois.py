"""Tests for the walker's points of interest
(examples/python/simulations/planet/pois.py), loaded from its file: a
cell's candidate lies inside it and is the same for the same seed and
another for another; the cells near a place include its own and every
candidate within the reach; the kinds by their chances and where they
may lie; a candidate measured under the water or at sea level has no
kind; and the saved states come back onto their candidates. Plain
Python: no engine needed.

    python -m unittest discover -s tests/python
"""

import importlib.util
import math
import unittest
from pathlib import Path

from shoonyakasha.planet import cube_sphere
from shoonyakasha.planet.vec import dot

REPO = Path(__file__).resolve().parents[2]
_SPEC = importlib.util.spec_from_file_location(
    "planet_pois", REPO / "examples" / "python" / "simulations" / "planet" / "pois.py")
pois = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(pois)

RADIUS = 100_000.0
SEED = (0.0, 0.0, 0.0, 0.0)


class Placement(unittest.TestCase):
    def test_candidate_inside_its_cell_and_seeded(self):
        d, roll = pois.candidate(0, 4, 31, 32, SEED)
        face, u, v = cube_sphere.face_uv(d)
        self.assertEqual(face, 4)
        self.assertEqual(int((u + 1.0) * 0.5 * pois.CELLS), 31)
        self.assertEqual(int((v + 1.0) * 0.5 * pois.CELLS), 32)
        self.assertTrue(0.0 <= roll <= 1.0)
        self.assertEqual(pois.candidate(0, 4, 31, 32, SEED), (d, roll))
        self.assertNotEqual(pois.candidate(0, 4, 31, 32, (1.0, 0.0, 0.0, 0.0)), (d, roll))

    def test_cells_near_hold_every_candidate_within_reach(self):
        here = cube_sphere.direction(4, 0.98, 0.2)          # near an edge of the face
        reach = 6_000.0
        near = set(pois.cells_near(here, reach, RADIUS))
        face, u, v = cube_sphere.face_uv(here)
        self.assertIn((face, int((u + 1.0) * 0.5 * pois.CELLS), int((v + 1.0) * 0.5 * pois.CELLS)), near)
        for f in range(6):
            for cx in range(pois.CELLS):
                for cy in range(pois.CELLS):
                    d, _ = pois.candidate(0, f, cx, cy, SEED)
                    if RADIUS * math.acos(max(-1.0, min(1.0, dot(d, here)))) <= reach:
                        self.assertIn((f, cx, cy), near)
        self.assertGreater(len({c[0] for c in near}), 1)       # across the edge too


class Kinds(unittest.TestCase):
    def test_chances_and_where(self):
        anywhere = (500.0, (15.0, 0.6), 0.0)                   # mild, wet, no ore, low
        rolls = [k / 10_000.0 for k in range(10_000)]
        share = lambda kind, ground: sum(pois.kind_for(r, *ground) == kind for r in rolls) / len(rolls)  # noqa: E731
        self.assertAlmostEqual(share("pod", anywhere), 0.06, places=2)
        self.assertAlmostEqual(share("beacon", anywhere), 0.04, places=2)
        self.assertAlmostEqual(share("wreck", anywhere), 0.03, places=2)
        for kind in ("rig", "geode", "arch"):
            self.assertEqual(share(kind, anywhere), 0.0)
        self.assertAlmostEqual(share("wreck", (2_000.0, (15.0, 0.6), 0.0)), 0.06, places=2)
        self.assertAlmostEqual(share("rig", (500.0, (15.0, 0.6), 0.5)), 0.02, places=2)
        self.assertAlmostEqual(share("geode", (500.0, (-5.0, 0.6), 0.0)), 0.03, places=2)
        self.assertAlmostEqual(share("arch", (500.0, (25.0, 0.1), 0.0)), 0.02, places=2)

    def test_water_and_the_sea_have_none(self):
        p = pois.Pois()
        here = cube_sphere.direction(4, 0.0, 0.0)
        p.want(0, here, 6_000.0, RADIUS, SEED)
        self.assertTrue(p.queue)
        keys = list(p.queue)
        p.measured(keys[0], 300.0, (15.0, 0.5), 0.0, 0, 299.0)    # under a lake
        p.measured(keys[1], 1.0, (15.0, 0.5), 0.0, 0, None)      # at sea level
        self.assertEqual((p.pois[keys[0]].kind, p.pois[keys[1]].kind), (None, None))
        self.assertFalse(p.pois[keys[0]].valid)
        self.assertNotIn(keys[0], p.queue)


class Saving(unittest.TestCase):
    def test_states_come_back(self):
        p = pois.Pois()
        here = cube_sphere.direction(4, 0.1, -0.2)
        p.want(0, here, 6_000.0, RADIUS, SEED)
        key = p.queue[0]
        poi = p.pois[key]
        poi.discovered, poi.done = True, [0, 2]
        records = p.record()
        q = pois.Pois()
        q.restore(records, SEED)
        back = q.pois[key]
        self.assertEqual((back.discovered, back.looted, back.done, back.direction), (True, False, [0, 2], poi.direction))
        self.assertIn(key, q.queue)                              # measured again when wanted


if __name__ == "__main__":
    unittest.main()

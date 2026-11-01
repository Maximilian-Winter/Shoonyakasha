"""Tests for the planet example's outposts (examples/python/simulations/
planet/outposts.py), loaded from its file, with heights made up for the
survey's points: a survey asks for each candidate and a ring at its flat
radius, the same for the same seed; the outposts chosen are the flattest
candidates kept apart, off the sea, within the planet's distances from
home, as many per body as COUNT allows, at the mean of their heights; a
point counts as over a pad within its reach; and an outpost's site is its
flat site.

    python -m unittest discover -s tests/python
"""

import importlib.util
import math
import unittest
from pathlib import Path

from shoonyakasha.planet.vec import dot, length, normalize, sub

REPO = Path(__file__).resolve().parents[2]
EXAMPLE = REPO / "examples" / "python" / "simulations" / "planet"
_SPEC = importlib.util.spec_from_file_location("planet_outposts", EXAMPLE / "outposts.py")
outposts = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(outposts)


class Body:
    def __init__(self, index, name, radius, terrain=None, centre=(0.0, 0.0, 0.0)):
        self.index, self.name, self.radius, self.terrain = index, name, radius, terrain
        self._centre = centre

    def centre(self):
        return self._centre


PLANET = Body(0, "planet", 100_000.0, terrain={})
MOON = Body(1, "moon", 25_000.0, centre=(600_000.0, 0.0, 0.0))
PER = outposts.RING + 1


def flat_heights(survey, relief=lambda k: 1.0 + k, base=200.0):
    """Heights for each candidate's points: `base` at its centre and its
    ring spread over `relief(k)` metres."""
    heights = []
    for k in range(len(survey.candidates)):
        heights.append(base)
        heights += [base + relief(k) * (j % 2) for j in range(outposts.RING)]
    return heights


def ground(body, a, b):
    return body.radius * math.acos(max(-1.0, min(1.0, dot(a, b))))


class SurveyTest(unittest.TestCase):
    def test_points_ring_each_candidate(self):
        survey = outposts.Survey([PLANET, MOON], (1.0, 2.0, 3.0))
        counts = outposts.CANDIDATES
        self.assertEqual(len(survey.candidates), counts["planet"] + counts["moon"])
        self.assertEqual(len(survey.points), len(survey.candidates) * PER)
        for k, (body, direction) in enumerate(survey.candidates):
            centre, *ring = survey.points[k * PER:(k + 1) * PER]
            self.assertEqual(centre, (direction, body))
            for d, b in ring:
                self.assertIs(b, body)
                self.assertAlmostEqual(ground(body, direction, d), outposts.FLAT_RADIUS[body.name], places=6)

    def test_same_seed_same_candidates(self):
        a = outposts.Survey([PLANET], (1.0, 2.0, 3.0))
        b = outposts.Survey([PLANET], (1.0, 2.0, 3.0))
        c = outposts.Survey([PLANET], (4.0, 2.0, 3.0))
        self.assertEqual(a.candidates, b.candidates)
        self.assertNotEqual(a.candidates, c.candidates)

    def test_the_moons_candidates_ignore_the_seed(self):
        a = outposts.Survey([MOON], (1.0, 2.0, 3.0))
        b = outposts.Survey([MOON], (4.0, 2.0, 3.0))
        self.assertEqual(a.candidates, b.candidates)


class ChooseTest(unittest.TestCase):
    def test_flattest_apart_dry_and_counted(self):
        survey = outposts.Survey([PLANET, MOON], (0.0, 0.0, 0.0))
        heights = flat_heights(survey)
        # A sea candidate, flatter than all, is left out.
        sea = next(k for k, (b, _) in enumerate(survey.candidates) if b is PLANET)
        heights[sea * PER:(sea + 1) * PER] = [-5.0] * PER
        chosen = survey.choose(heights)
        for name in ("planet", "moon"):
            mine = [o for o in chosen if o.body.name == name]
            self.assertEqual(len(mine), outposts.COUNT[name])
            self.assertEqual([o.name for o in mine], list(outposts.NAMES[name][:len(mine)]))
            for i, a in enumerate(mine):
                for b in mine[i + 1:]:
                    self.assertGreaterEqual(ground(a.body, a.direction, b.direction), outposts.SPACING[name])
        candidates = [d for _, d in survey.candidates]
        self.assertNotIn(sea, [candidates.index(o.direction) for o in chosen])
        # The first chosen on each body is its flattest candidate: the lowest index (relief 1 + k).
        for name in ("planet", "moon"):
            first = next(k for k, (b, _) in enumerate(survey.candidates) if b.name == name and k != sea)
            self.assertEqual(next(o for o in chosen if o.body.name == name).direction, candidates[first])
        o = chosen[0]
        k = candidates.index(o.direction)
        self.assertAlmostEqual(o.height, sum(heights[k * PER:(k + 1) * PER]) / PER)
        self.assertEqual((o.flat_radius, o.blend_radius, o.fuel_price),
                         (outposts.FLAT_RADIUS[o.body.name], outposts.BLEND_RADIUS[o.body.name],
                          outposts.FUEL_PRICE[o.body.name]))

    def test_home_distances_and_those_taken(self):
        survey = outposts.Survey([PLANET], (0.0, 0.0, 0.0))
        heights = flat_heights(survey)
        home_direction = survey.candidates[0][1]
        home = outposts.Outpost("Base", PLANET, home_direction, 200.0, 1.0)
        taken = [outposts.Outpost("Old", PLANET, survey.candidates[1][1], 200.0, 1.0, 150.0, 600.0)]
        chosen = survey.choose(heights, taken=taken, home=home)
        low, high = outposts.FROM_HOME
        for o in chosen:
            self.assertTrue(low <= ground(PLANET, home_direction, o.direction) <= high)
            self.assertGreaterEqual(ground(PLANET, taken[0].direction, o.direction), outposts.SPACING["planet"])


class OutpostTest(unittest.TestCase):
    def test_over_the_pad(self):
        up = normalize((0.0, 0.3, 1.0))
        o = outposts.Outpost("Pad", MOON, up, -40.0, 3.0, 100.0, 400.0)
        centre = o.point()
        self.assertAlmostEqual(length(sub(centre, MOON.centre())), MOON.radius - 40.0)
        self.assertTrue(o.at(centre))
        east = outposts.ring(up, MOON.radius, outposts.PAD_REACH - 1.0, 1)[0]
        near = tuple(c + d * (MOON.radius - 40.0 + 2.0) for c, d in zip(MOON.centre(), east))
        self.assertTrue(o.at(near))
        out = outposts.ring(up, MOON.radius, outposts.PAD_REACH + 1.0, 1)[0]
        self.assertFalse(o.at(tuple(c + d * (MOON.radius - 40.0) for c, d in zip(MOON.centre(), out))))
        high = tuple(c + u * (MOON.radius - 40.0 + outposts.REACH_UP + 1.0) for c, u in zip(MOON.centre(), up))
        self.assertFalse(o.at(high))

    def test_site(self):
        o = outposts.Outpost("Pad", MOON, (0.0, 1.0, 0.0), -40.0, 3.0, 100.0, 400.0)
        site = o.site()
        self.assertEqual((site.body, site.direction, site.height, site.flat_radius, site.blend_radius),
                         (1, (0.0, 1.0, 0.0), -40.0, 100.0, 400.0))


if __name__ == "__main__":
    unittest.main()

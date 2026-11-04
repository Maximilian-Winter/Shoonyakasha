"""Tests for the lakes' survey (shoonyakasha.planet.lakes), with heights
made up for its points: the candidates lie within the survey's reach of
its centre with their shore points on their radius, the same for the same
seed; the lakes chosen keep off the sea, off steep shores and away from
the circles to avoid, take the deepest hollows first, keep their spacing,
and lie FREEBOARD below their lowest shore point with their depth, bank
and wobble.

    python -m unittest discover -s tests/python
"""

import unittest

from shoonyakasha.planet import lakes
from shoonyakasha.planet.lakes import ground_distance
from shoonyakasha.planet.world import RADIUS

CENTRE = (0.0, 0.0, 1.0)
PER = lakes.RING + 1


class Body:
    radius = RADIUS


class Survey(unittest.TestCase):
    def setUp(self):
        self.body = Body()
        self.survey = lakes.LakeSurvey(self.body, CENTRE, (1.0, 2.0, 3.0))

    def flat(self, height=100.0):
        return [height] * len(self.survey.points)

    def test_candidates_and_their_points(self):
        survey = self.survey
        self.assertEqual(len(survey.candidates), lakes.CANDIDATES)
        self.assertEqual(len(survey.points), lakes.CANDIDATES * PER)
        for k, (direction, radius) in enumerate(survey.candidates):
            self.assertTrue(lakes.RADII[0] <= radius <= lakes.RADII[1])
            distance = ground_distance(RADIUS, CENTRE, direction)
            self.assertTrue(lakes.REACH[0] - 1e-6 <= distance <= lakes.REACH[1] + 1e-6)
            self.assertEqual(survey.points[k * PER], (direction, self.body))
            for d, body in survey.points[k * PER + 1:(k + 1) * PER]:
                self.assertIs(body, self.body)
                self.assertAlmostEqual(ground_distance(RADIUS, direction, d), radius, delta=1e-3)

    def test_the_same_seed_gives_the_same_candidates(self):
        again = lakes.LakeSurvey(self.body, CENTRE, (1.0, 2.0, 3.0))
        other = lakes.LakeSurvey(self.body, CENTRE, (1.0, 2.0, 4.0))
        self.assertEqual(again.candidates, self.survey.candidates)
        self.assertNotEqual(other.candidates, self.survey.candidates)

    def test_lakes_on_flat_ground(self):
        chosen = self.survey.choose(self.flat())
        self.assertTrue(0 < len(chosen) <= lakes.COUNT)
        for lake in chosen:
            self.assertEqual(lake.level, 100.0 - lakes.FREEBOARD)
            self.assertTrue(lakes.DEPTH[0] <= lake.depth <= lakes.DEPTH[1])
            self.assertAlmostEqual(lake.depth, min(max(lakes.DEPTH_SHARE * lake.radius, lakes.DEPTH[0]), lakes.DEPTH[1]))
            self.assertAlmostEqual(lake.bank, lakes.BANK_SHARE * lake.radius)
            self.assertEqual(lake.wobble, lakes.WOBBLE)
        for i, a in enumerate(chosen):
            for b in chosen[i + 1:]:
                gap = ground_distance(RADIUS, a.direction, b.direction)
                self.assertGreaterEqual(gap, lakes.SPACING * (lakes.reach(a.radius) + lakes.reach(b.radius)))

    def candidate_lake(self, chosen, k):
        direction = self.survey.candidates[k][0]
        return [lake for lake in chosen if lake.direction == direction]

    def test_the_deepest_hollow_first(self):
        heights = self.flat()
        k = 17
        heights[k * PER] = 60.0
        chosen = self.survey.choose(heights)
        self.assertEqual(chosen[0].direction, self.survey.candidates[k][0])

    def test_wet_steep_and_avoided_candidates_are_left_out(self):
        survey = self.survey
        wet, steep, avoided = 3, 5, 7
        heights = self.flat()
        for k in (wet, steep, avoided):
            heights[k * PER] = 40.0                        # each the deepest hollow but for its fault
        for j in range(PER):
            heights[wet * PER + j] = lakes.DRY - 1.0       # a level shore just under DRY
        heights[wet * PER] = lakes.DRY - 30.0
        heights[steep * PER + 1] = 100.0 + lakes.RELIEF * survey.candidates[steep][1] + 1.0
        avoid = [(survey.candidates[avoided][0], 10.0)]
        chosen = survey.choose(heights, avoid=avoid)
        for k in (wet, steep, avoided):
            self.assertEqual(self.candidate_lake(chosen, k), [])
        for lake in chosen:
            self.assertGreaterEqual(ground_distance(RADIUS, lake.direction, avoid[0][0]),
                                    lakes.reach(lake.radius) + 10.0)

    def test_the_water_lies_below_the_lowest_shore_point(self):
        heights = self.flat()
        k = 11
        heights[k * PER] = 20.0
        heights[k * PER + 4] = 97.0
        lake = self.candidate_lake(self.survey.choose(heights), k)[0]
        self.assertEqual(lake.level, 97.0 - lakes.FREEBOARD)


if __name__ == "__main__":
    unittest.main()

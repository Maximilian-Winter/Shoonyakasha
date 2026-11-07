"""Tests for the walker's settings and save
(examples/python/simulations/planet/config.py and save.py): the settings'
defaults, a file with missing, mistyped or out-of-range values, a file
that cannot be parsed, writing only when changed; the save's round trip
with the explored map's measured cells, a missing save, another version
(an older one too), grids of unknown bodies left out, and the line about
a save. Plain
Python: no engine needed.

    python -m unittest discover -s tests/python
"""

import json
import math
import os
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

PLANET_DIR = Path(__file__).resolve().parents[2] / "examples" / "python" / "simulations" / "planet"
sys.path.insert(0, str(PLANET_DIR))

import config  # noqa: E402
import save  # noqa: E402
from explored_map import Grid  # noqa: E402


class Body:
    def __init__(self, name):
        self.name, self.radius, self.ocean = name, 100_000.0, True


class Explored:
    def __init__(self):
        self.grids, self.asked, self.asking = {}, ["probe"], ["cell"]


class ConfigTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.dir.name, "walker_config.json")

    def tearDown(self):
        self.dir.cleanup()

    def write(self, values):
        with open(self.path, "w", encoding="utf-8") as f:
            f.write(values if isinstance(values, str) else json.dumps(values))

    def test_defaults_without_a_file(self):
        settings = config.load(self.path)
        self.assertEqual(settings.values, config.DEFAULTS)
        self.assertFalse(settings.save())
        self.assertFalse(os.path.exists(self.path))

    def test_bad_values_take_defaults_or_ranges(self):
        self.write({"mouse_sensitivity": 9.0, "invert_y": "yes", "fov": "wide", "window": [800, 600],
                    "hud_scale": 1, "head_bob": False, "unknown": 1})
        settings = config.load(self.path)
        self.assertEqual(settings["mouse_sensitivity"], config.RANGES["mouse_sensitivity"][1])
        self.assertIs(settings["invert_y"], False)
        self.assertEqual(settings["fov"], config.DEFAULTS["fov"])
        self.assertEqual(settings["window"], config.DEFAULTS["window"])
        self.assertEqual(settings["hud_scale"], 1.0)
        self.assertIs(settings["head_bob"], False)
        self.assertNotIn("unknown", settings.values)
        settings.set("fov", float("nan"))
        self.assertEqual(settings["fov"], config.DEFAULTS["fov"])

    def test_unreadable_file_gives_defaults(self):
        self.write("{not json")
        self.assertEqual(config.load(self.path).values, config.DEFAULTS)
        self.write("[1, 2]")
        self.assertEqual(config.load(self.path).values, config.DEFAULTS)

    def test_written_when_changed(self):
        settings = config.load(self.path)
        settings.set("fov", 80.0)
        settings.set("window", (1920, 1080))
        self.assertTrue(settings.save())
        self.assertFalse(settings.save())
        again = config.load(self.path)
        self.assertEqual(again["fov"], 80.0)
        self.assertEqual(again["window"], [1920, 1080])
        self.assertFalse(os.path.exists(self.path + ".tmp"))


class SaveTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.base = os.path.join(self.dir.name, "walker_save")
        self.planet = Body("planet")
        self.grid = Grid(self.planet, (0.0, 0.0, 1.0), size=33)
        self.grid.set(3, 4, 120.5, 0.25)
        self.grid.set(16, 16, -40.0, 0.0)

    def tearDown(self):
        self.dir.cleanup()

    def test_round_trip(self):
        data = {"world": {"time": 12.5}, "economy": {"market": {"credits": 1240.0}}, "progress": {"objective": 2}}
        written = save.write(self.base, data, {"planet": self.grid})
        self.assertEqual(written["version"], save.VERSION)
        self.assertTrue(save.exists(self.base))
        read, arrays = save.read(self.base)
        self.assertEqual(read["world"]["time"], 12.5)
        self.assertEqual(len(arrays["planet/index"]), 2)
        explored = Explored()
        save.restore_grids(explored, arrays, {"planet": self.planet}, Grid)
        grid = explored.grids["planet"]
        self.assertIsNone(explored.asked)
        self.assertEqual(explored.asking, [])
        self.assertEqual(grid.measured, 2)
        self.assertEqual(float(grid.heights[4, 3]), 120.5)
        self.assertEqual(float(grid.ores[4, 3]), 0.25)
        self.assertTrue(math.isnan(float(grid.heights[0, 0])))
        self.assertEqual(grid.size, 33)
        np.testing.assert_allclose(grid.origin, (0.0, 0.0, 1.0))
        self.assertIn("1,240 cr", save.describe(read))
        self.assertIn("2 objectives done", save.describe(read))

    def test_missing_and_other_version(self):
        with self.assertRaises(save.SaveError):
            save.read(self.base)
        save.write(self.base, {}, {})
        for version in (save.VERSION + 1, save.VERSION - 1):
            with open(self.base + ".json", "w", encoding="utf-8") as f:
                json.dump({"version": version}, f)
            with self.assertRaises(save.SaveError):
                save.read(self.base)

    def test_grids_of_unknown_bodies_left_out(self):
        moon = Grid(Body("moon"), (1.0, 0.0, 0.0), size=17)
        moon.set(1, 1, 10.0, 0.0)
        save.write(self.base, {}, {"planet": self.grid, "moon": moon})
        _, arrays = save.read(self.base)
        explored = Explored()
        save.restore_grids(explored, arrays, {"planet": self.planet}, Grid)
        self.assertEqual(list(explored.grids), ["planet"])


if __name__ == "__main__":
    unittest.main()

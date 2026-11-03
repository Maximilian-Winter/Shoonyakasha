"""Tests for the animation controller builder and the Animation binding's
declarations; they need no compiled extension.

    python -m unittest discover -s tests/python
"""

import json
import re
import tempfile
import unittest
from pathlib import Path

from shoonyakasha.animation_controller import AnimationController

REPO = Path(__file__).resolve().parents[2]
HEADER = (REPO / "include/Facade/AnimationAPI.h").read_text(encoding="utf-8")
PXD = (REPO / "python/shoonyakasha/_engine_api.pxd").read_text(encoding="utf-8")


def locomotion():
    c = AnimationController()
    c.float("speed").bool("grounded", default=True).trigger("jump")
    base = c.layer("base")
    base.blend1d("locomotion", "speed", [("idle", 0), ("walk", 1), ("run", 3)])
    base.clip("jump", "jump", loop=False, events=[(0.0, "takeoff"), (0.5, "apex")])
    base.transition("locomotion", "jump", duration=0.2, when=[("jump",)])
    base.transition("jump", "locomotion", exit_time=0.9)
    base.transition("any", "fall", when=[("grounded", "==", False)])
    c.layer("upper", mask=["Spine"], weight=0.5, mode="additive").clip("wave", "wave", speed=2.0,
                                                                         speed_parameter="speed")
    return c


class BuilderTest(unittest.TestCase):
    def test_json_form(self):
        self.assertEqual(locomotion().to_dict(), {
            "parameters": {
                "speed": {"type": "float"},
                "grounded": {"type": "bool", "default": True},
                "jump": {"type": "trigger"},
            },
            "layers": [
                {
                    "name": "base",
                    "states": {
                        "locomotion": {"blend1d": {"parameter": "speed",
                                                   "clips": [["idle", 0], ["walk", 1], ["run", 3]]}},
                        "jump": {"clip": "jump", "loop": False, "events": [[0.0, "takeoff"], [0.5, "apex"]]},
                    },
                    "transitions": [
                        {"from": "locomotion", "to": "jump", "duration": 0.2, "when": [["jump"]]},
                        {"from": "jump", "to": "locomotion", "exit_time": 0.9},
                        {"from": "any", "to": "fall", "when": [["grounded", "==", False]]},
                    ],
                },
                {
                    "name": "upper", "weight": 0.5, "mode": "additive", "mask": ["Spine"],
                    "states": {"wave": {"clip": "wave", "speed": 2.0, "speed_parameter": "speed"}},
                },
            ],
        })

    def test_blend2d_and_default(self):
        c = AnimationController()
        c.float("vx").float("vz")
        layer = c.layer("move", default="strafe")
        layer.clip("idle", "idle")
        layer.blend2d("strafe", ("vx", "vz"), [("idle", 0, 0), ("left", -1, 0), ("forward", 0, 1)])
        d = c.to_dict()["layers"][0]
        self.assertEqual(d["default"], "strafe")
        self.assertEqual(d["states"]["strafe"], {
            "blend2d": {"parameters": ["vx", "vz"], "clips": [["idle", 0, 0], ["left", -1, 0], ["forward", 0, 1]]}})

    def test_duplicates_and_mode_are_rejected(self):
        c = AnimationController()
        c.float("speed")
        with self.assertRaises(ValueError):
            c.float("speed")
        layer = c.layer("base")
        with self.assertRaises(ValueError):
            c.layer("base")
        layer.clip("idle", "idle")
        with self.assertRaises(ValueError):
            layer.clip("idle", "other")
        with self.assertRaises(ValueError):
            c.layer("bad", mode="multiply")

    def test_save_writes_json(self):
        c = locomotion()
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "locomotion.json"
            c.save(path)
            self.assertEqual(json.loads(path.read_text(encoding="utf-8")), c.to_dict())


class BindingTest(unittest.TestCase):
    def test_every_facade_method_is_declared(self):
        public = HEADER.split("class AnimationAPI {", 1)[1].split("private:", 1)[0]
        public = re.sub(r"//[^\n]*", "", public)
        methods = set(re.findall(r"\b(\w+)\(", public)) - {"AnimationAPI", "vec3", "AnimationAPI&"}
        declared = PXD.split('cdef cppclass CppAnimationAPI', 1)[1].split("# ═", 1)[0]
        missing = sorted(m for m in methods if not re.search(r"\b" + m + r"\(", declared))
        self.assertEqual(missing, [])


if __name__ == "__main__":
    unittest.main()

"""Tests for shoonyakasha.character with a stand-in engine; they need no
compiled extension.

    python -m unittest discover -s tests/python
"""

import unittest
from pathlib import Path

from shoonyakasha.character import load_humanoid


class Result:
    success = True
    error = ""
    root_entities = [7, 9]


class Animation:
    def __init__(self):
        self.calls = []
        self.motion = (0.5, 0.0, -0.25)

    def __getattr__(self, name):
        def record(*args):
            self.calls.append((name,) + args)
            if name == "create_animator":
                return args[0] == 9
            if name == "humanoid_mapping":
                return {"Hips": "mixamorig_Hips", "RightHand": "mixamorig_RightHand"}
            if name == "load_humanoid_clips":
                return [args[2] or "clip"]
            if name == "take_root_motion":
                return self.motion
            return None
        return record


class Scene:
    def __init__(self):
        self.positions = {}

    def get_position(self, entity):
        return self.positions.get(entity, (0.0, 0.0, 0.0))

    def set_position(self, entity, position):
        self.positions[entity] = tuple(position)


class Engine:
    def __init__(self):
        self.animation = Animation()
        self.scene = Scene()
        self.loaded = []

    def load_gltf_scene(self, path, **options):
        self.loaded.append((path, options))
        return Result()


class LoadHumanoidTest(unittest.TestCase):
    def test_loads_maps_retargets_and_sets_controller(self):
        engine = Engine()
        hero = load_humanoid(engine, Path("models/hero.gltf"),
                             animations=["anims/a.gltf", ("anims/walk.gltf", "walk")],
                             controller={"layers": []}, mapping={"Chest": ""}, position=(1, 0, 2),
                             root_motion=True)
        self.assertEqual(hero.root, 9)   # the first root with a skinned mesh
        names = [c[0] for c in engine.animation.calls]
        self.assertEqual(names, ["create_animator", "create_animator", "make_humanoid", "load_humanoid_clips",
                                 "load_humanoid_clips", "set_controller", "humanoid_mapping", "set_root_motion"])
        calls = engine.animation.calls
        self.assertEqual(calls[2], ("make_humanoid", 9, {"Chest": ""}))
        self.assertEqual(calls[4], ("load_humanoid_clips", 9, "anims/walk.gltf", "walk", None))
        self.assertEqual(calls[7], ("set_root_motion", 9, True, "mixamorig_Hips"))
        self.assertEqual(engine.scene.positions[9], (1, 0, 2))

    def test_bones_resolve_to_joints(self):
        engine = Engine()
        hero = load_humanoid(engine, "hero.gltf")
        hero.attach(42, "RightHand", offset_position=(0, 8, 0))
        hero.attach(43, "custom_joint")
        calls = [c for c in engine.animation.calls if c[0] == "attach"]
        self.assertEqual(calls[0], ("attach", 42, 9, "mixamorig_RightHand", (0, 8, 0), (0.0, 0.0, 0.0)))
        self.assertEqual(calls[1][3], "custom_joint")

    def test_apply_root_motion_moves_the_root(self):
        engine = Engine()
        hero = load_humanoid(engine, "hero.gltf", position=(1.0, 0.0, 1.0))
        self.assertEqual(hero.apply_root_motion(), (0.5, 0.0, -0.25))
        self.assertEqual(engine.scene.positions[9], (1.5, 0.0, 0.75))

    def test_no_skinned_mesh_raises(self):
        engine = Engine()
        engine.animation.create_animator = lambda root: False
        with self.assertRaises(RuntimeError):
            load_humanoid(engine, "prop.gltf")


if __name__ == "__main__":
    unittest.main()

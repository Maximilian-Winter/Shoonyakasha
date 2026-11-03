"""
Shoonyakasha: humanoid retargeting

Characters with different skeletons share each other's animations. Each is
loaded as a humanoid (shoonyakasha.character), its joints mapped onto the
standard humanoid bones, and every character's clip is retargeted onto every
character: the rumba of the Mixamo dancer, Y Bot's rumba, the capoeira (with
its travel as root motion), and CesiumMan's walk when the glTF sample models
are next to the repository.

Models: assets/models/rumba-dancing-mixamo/gltf/Rumba-Dancing.gltf,
assets/models/rumba-dancing-mixamo/gltf-Y-Bot/Rumba-Dancing-Y-Bot.gltf,
assets/models/capoeira-root-motion/gltf/Capoeira.gltf, and optionally
glTF-Sample-Assets/Models/CesiumMan/glTF-Binary/CesiumMan.glb.

Keys:
    1 .. 4         every character: cross-fade to clip 1 .. 4 (printed at start)
    M              root motion on or off (the characters travel, or dance in place)
    SPACE          pause or resume
    W/A/S/D/Q/E    move the camera; right mouse looks around
"""

from pathlib import Path

import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.character import load_humanoid

REPO = Path(__file__).resolve().parents[4]
CESIUM = REPO / "glTF-Sample-Assets/Models/CesiumMan/glTF-Binary/CesiumMan.glb"

# (label, model, clip label, position)
CHARACTERS = [
    ("dancer", "models/rumba-dancing-mixamo/gltf/Rumba-Dancing.gltf", "rumba", (-1.8, 0.0, 0.0)),
    ("ybot", "models/rumba-dancing-mixamo/gltf-Y-Bot/Rumba-Dancing-Y-Bot.gltf", "rumba_ybot", (-0.6, 0.0, 0.0)),
    ("capoeira", "models/capoeira-root-motion/gltf/Capoeira.gltf", "capoeira", (0.6, 0.0, 0.0)),
]

# CesiumMan's joints are numbered, so its bones are named here.
CESIUM_MAPPING = {
    "Hips": "Skeleton_torso_joint_1", "Spine": "Skeleton_torso_joint_2", "Chest": "torso_joint_3",
    "Neck": "Skeleton_neck_joint_1", "Head": "Skeleton_neck_joint_2",
    "LeftUpperArm": "Skeleton_arm_joint_L__4_", "LeftLowerArm": "Skeleton_arm_joint_L__3_",
    "LeftHand": "Skeleton_arm_joint_L__2_", "RightUpperArm": "Skeleton_arm_joint_R",
    "RightLowerArm": "Skeleton_arm_joint_R__2_", "RightHand": "Skeleton_arm_joint_R__3_",
    "LeftUpperLeg": "leg_joint_L_1", "LeftLowerLeg": "leg_joint_L_2", "LeftFoot": "leg_joint_L_3",
    "LeftToes": "leg_joint_L_5", "RightUpperLeg": "leg_joint_R_1", "RightLowerLeg": "leg_joint_R_2",
    "RightFoot": "leg_joint_R_3", "RightToes": "leg_joint_R_5",
}

FADE = 0.4

engine = sk.Engine(
    title="Shoonyakasha - Humanoid retargeting",
    width=1600, height=900,
    hdr_environment_path="env/charolettenbrunn_park_1k.hdr",
)

state = {"heroes": {}, "homes": {}, "clips": [], "root_motion": False, "paused": False}


def on_init():
    engine.create_camera(pos=(0.0, 1.3, 5.0), fov=50.0, speed=3.0, near_plane=0.05, far_plane=200.0)
    engine.create_directional_light(direction=(-0.4, -1.0, -0.5), color=(1.0, 0.96, 0.9), intensity=3.0)

    heroes, sources = {}, []
    for label, path, clip, position in CHARACTERS:
        heroes[label] = load_humanoid(engine, path, position=position)
        sources.append((label, clip))
    if CESIUM.exists():
        heroes["cesium"] = load_humanoid(engine, CESIUM, mapping=CESIUM_MAPPING, position=(1.8, 0.0, 0.0))
        sources.append(("cesium", "walk"))

    # Each character's own first clip, retargeted onto every character
    # (itself included), under one name per source.
    anim = engine.animation
    originals = {label: heroes[label].clips[0] for label, _ in sources}
    for target in heroes.values():
        for label, clip in sources:
            anim.retarget_clips(target.root, heroes[label].root, [originals[label]], clip)
    state["heroes"] = heroes
    state["homes"] = {label: engine.scene.get_position(h.root) for label, h in heroes.items()}
    state["clips"] = [clip for _, clip in sources]
    play(0)

    for label, hero in heroes.items():
        print(f"{label}: {len(hero.mapping)} humanoid bones, clips {hero.clips}")
    print("Clips by key:", {i + 1: clip for i, clip in enumerate(state["clips"])})
    print(__doc__[__doc__.index("Keys:"):])


def play(index):
    clip = state["clips"][index]
    for hero in state["heroes"].values():
        hero.play(clip, fade=FADE)
    print("Playing", clip)


def on_update(dt):
    if state["root_motion"]:
        for hero in state["heroes"].values():
            hero.apply_root_motion()


def on_key_pressed(key):
    if not state["heroes"]:
        return
    if keys.NUM_1 <= key <= keys.NUM_9 and key - keys.NUM_1 < len(state["clips"]):
        play(key - keys.NUM_1)
    elif key == ord("M"):
        state["root_motion"] = not state["root_motion"]
        for label, hero in state["heroes"].items():
            hero.set_root_motion(state["root_motion"])
            engine.scene.set_position(hero.root, state["homes"][label])
        print("Root motion:", "on" if state["root_motion"] else "off")
    elif key == keys.SPACE:
        state["paused"] = not state["paused"]
        for hero in state["heroes"].values():
            engine.animation.set_speed(hero.root, 0.0 if state["paused"] else 1.0)


engine.set_on_init(on_init)
engine.set_on_update(on_update)
engine.set_on_key_pressed(on_key_pressed)
engine.run()

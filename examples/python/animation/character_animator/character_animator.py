"""
Shoonyakasha: per-model animators

A Mixamo dancer and the bundled Fox, each driven by one animator on its
model's root entity (engine.animation). The dancer's two skinned meshes share
one skeleton and one evaluation. The Fox switches clips with a cross-fade. A
glowing cube is attached to the dancer's right hand and follows it.

Models: assets/models/rumba-dancing-mixamo/gltf/Rumba-Dancing.gltf and
assets/models/Fox.glb.

Keys:
    1 / 2 / 3      Fox: cross-fade to Survey / Walk / Run
    R              dancer: rest pose, or dance again
    SPACE          pause or resume both
    = / -          faster / slower
    W/A/S/D/Q/E    move the camera; right mouse looks around
"""

import shoonyakasha as sk
from shoonyakasha import keys

DANCER = "models/rumba-dancing-mixamo/gltf/Rumba-Dancing.gltf"
FOX = "models/Fox.glb"
FADE = 0.4
FOX_CLIPS = {keys.NUM_1: "Survey", keys.NUM_2: "Walk", keys.NUM_3: "Run"}

engine = sk.Engine(
    title="Shoonyakasha - Character animator",
    width=1600, height=900,
    hdr_environment_path="env/charolettenbrunn_park_1k.hdr",
)

state = {"dancer": None, "fox": None, "speed": 1.0, "paused": False, "dancing": True}


def cube(half):
    """Positions and indices of an axis-aligned cube, `half` on each side of 0."""
    positions, indices = [], []
    for axis in range(3):
        for sign in (-1.0, 1.0):
            u, v = (axis + 1) % 3, (axis + 2) % 3
            base = len(positions)
            for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                p = [0.0, 0.0, 0.0]
                p[axis], p[u], p[v] = sign * half, a * half, b * half * sign
                positions.append(tuple(p))
            indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    return positions, indices


def load_model(path, position, scale=None):
    result = engine.load_gltf_scene(path, load_skins=True, load_animations=True)
    if not result.success:
        raise SystemExit(f"cannot load {path}: {result.error}")
    # The animator goes on the first top-level node with a skinned mesh below it.
    root = next((r for r in result.root_entities if engine.animation.create_animator(r)), None)
    if root is None:
        raise SystemExit(f"{path} has no skinned mesh")
    engine.scene.set_position(root, position)
    if scale is not None:
        engine.scene.set_scale(root, (scale, scale, scale))
    return root


def on_init():
    engine.create_camera(pos=(0.6, 1.2, 4.0), fov=50.0, speed=3.0, near_plane=0.05, far_plane=200.0)
    engine.create_directional_light(direction=(-0.4, -1.0, -0.5), color=(1.0, 0.96, 0.9), intensity=3.0)

    anim = engine.animation
    dancer = load_model(DANCER, (0.0, 0.0, 0.0))     # its root node scales centimetres to metres
    fox = load_model(FOX, (1.6, 0.0, 0.0), 0.012)
    state["dancer"], state["fox"] = dancer, fox

    anim.play(dancer, anim.clip_names(dancer)[0])
    anim.play(fox, "Survey")

    # An attached entity is placed in the joint's space, which carries the
    # dancer's centimetre scale: the cube is 8 cm wide, 8 cm past the hand.
    positions, indices = cube(4.0)
    gem = engine.create_mesh(positions, indices, name="hand_gem")
    engine.scene.set_material_vec4(gem, "emissiveFactor", (4.0, 1.2, 0.3, 0.0))
    anim.attach(gem, dancer, "mixamorig_RightHand", offset_position=(0.0, 8.0, 0.0))

    print("Dancer clips:", anim.clip_names(dancer))
    print("Fox clips:   ", anim.clip_names(fox))
    print("Dancer joints:", len(anim.joint_names(dancer)))
    print(__doc__[__doc__.index("Keys:"):])


def set_speed(speed):
    state["speed"] = speed
    effective = 0.0 if state["paused"] else speed
    for model in (state["dancer"], state["fox"]):
        engine.animation.set_speed(model, effective)
    print(f"speed {speed:.2f}x" + (" (paused)" if state["paused"] else ""))


def on_key_pressed(key):
    anim = engine.animation
    dancer, fox = state["dancer"], state["fox"]
    if dancer is None:
        return
    if key in FOX_CLIPS:
        anim.play(fox, FOX_CLIPS[key], fade=FADE)
        print("Fox:", FOX_CLIPS[key])
    elif key == ord("R"):
        state["dancing"] = not state["dancing"]
        if state["dancing"]:
            anim.play(dancer, anim.clip_names(dancer)[0], fade=FADE)
        else:
            anim.stop(dancer)
        print("Dancer:", anim.current_clip(dancer) or "rest pose")
    elif key == keys.SPACE:
        state["paused"] = not state["paused"]
        set_speed(state["speed"])
    elif key == keys.EQUAL:
        set_speed(min(state["speed"] * 1.5, 8.0))
    elif key == keys.MINUS:
        set_speed(max(state["speed"] / 1.5, 0.1))


engine.set_on_init(on_init)
engine.set_on_key_pressed(on_key_pressed)
engine.run()

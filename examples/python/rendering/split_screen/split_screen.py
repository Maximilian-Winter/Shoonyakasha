"""
Shoonyakasha split screen

Two cameras on one screen, on the pipeline in pipeline/: the player's view,
flown with the default camera controls, and a map from above, drawn by an
orthographic camera named "Map" that follows the player. The pipeline reads
the map camera as scene.cameras.Map.* and culls the map's pass against it
("view": "cameras.Map"); each pass draws into a viewport set from Python.
An arrow marks the player on the map, on a render layer the player's view
leaves out.

Keys:
    WASD/Q/E + right mouse   fly the camera
    M                        side by side, or the map in a corner
    P                        screenshot into split_screen_captures/
"""

import math
import os
import random
import time

import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.mathutil import rotation_facing

HERE = os.path.dirname(os.path.abspath(__file__))
PIPELINE = os.path.join(HERE, "pipeline", "pipeline.json")
CAPTURES = os.path.join(HERE, "split_screen_captures")

sk.shaders.compile_dir(os.path.join(HERE, "pipeline", "shaders"))

engine = sk.Engine(title="Shoonyakasha - split screen", width=1600, height=800,
                   pipeline_json_path=PIPELINE)

TOWN = 40.0          # the town is TOWN x TOWN metres
MAP_SPAN = 36.0      # metres the map shows top to bottom
MAP_HEIGHT = 80.0    # the map camera's height above the player
LAYOUTS = ("side by side", "map in the corner")
state = {"layout": 0}


# ── Shapes ─────────────────────────────────────────────────────────────────

def cube():
    """A unit cube on the ground, as triangles without indices: each face
    gets its own vertices, so the computed normals are flat."""
    corners = [(x, y, z) for x in (-0.5, 0.5) for y in (0.0, 1.0) for z in (-0.5, 0.5)]
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    return [corners[i] for face in faces for i in (face[0], face[1], face[2], face[0], face[2], face[3])]


def ground(cells=40):
    """The town's ground, a checkerboard of `cells` x `cells` squares in
    vertex colours."""
    positions, colors, indices = [], [], []
    size = TOWN / cells
    for i in range(cells):
        for j in range(cells):
            x, z = -TOWN / 2 + i * size, -TOWN / 2 + j * size
            shade = (0.42, 0.45, 0.40) if (i + j) % 2 else (0.36, 0.39, 0.35)
            base = len(positions)
            positions += [(x, 0.0, z), (x, 0.0, z + size), (x + size, 0.0, z), (x + size, 0.0, z + size)]
            colors += [shade] * 4
            indices += [base, base + 1, base + 2, base + 2, base + 1, base + 3]
    return positions, indices, colors


def arrow():
    """A flat arrow pointing along -Z, the way an unrotated entity faces."""
    tip, left, right, notch = (0.0, 0.0, -1.6), (-1.0, 0.0, 1.0), (1.0, 0.0, 1.0), (0.0, 0.0, 0.4)
    return [tip, left, notch, tip, notch, right]


# ── Scene ──────────────────────────────────────────────────────────────────

def build_town():
    positions, indices, colors = ground()
    engine.create_mesh(positions, indices, colors=colors, name="Ground")
    block = cube()
    rng = random.Random(7)
    for i in range(-3, 4):
        for j in range(-3, 4):
            height = rng.uniform(1.5, 7.0)
            entity = engine.create_mesh(block, name="Block")
            engine.scene.set_position(entity, (i * 5.0, 0.0, j * 5.0))
            engine.scene.set_scale(entity, (3.0, height, 3.0))
            hue = rng.random()
            color = (0.55 + 0.35 * math.cos(6.283 * hue), 0.55 + 0.35 * math.cos(6.283 * (hue - 0.33)),
                     0.55 + 0.35 * math.cos(6.283 * (hue - 0.67)), 1.0)
            engine.scene.set_material_vec4(entity, "baseColorFactor", color)


def on_init():
    build_town()
    # At a crossing of two streets, looking north (-Z).
    state["player"] = engine.create_camera((2.5, 1.7, 12.5), fov=60.0, speed=6.0)

    # The map: an orthographic camera looking straight down, north up.
    map_camera = engine.scene.create_entity("Map")
    engine.scene.add_component(map_camera, "Camera")
    engine.scene.set_camera_type(map_camera, sk.CAMERA_ORTHOGRAPHIC)
    engine.scene.set_camera_ortho_size(map_camera, MAP_SPAN)
    engine.scene.set_camera_near(map_camera, 1.0)
    engine.scene.set_camera_far(map_camera, MAP_HEIGHT + 10.0)
    engine.scene.set_rotation(map_camera, (-math.pi / 2, 0.0, 0.0))
    state["map"] = map_camera

    # The player on the map: layer 2, which the player's view (renderLayerMask
    # 1 in pipeline.json) leaves out.
    marker = engine.create_mesh(arrow(), name="Marker")
    engine.scene.set_material_vec4(marker, "baseColorFactor", (1.0, 0.85, 0.1, 1.0))
    engine.scene.set_render_layer_mask(marker, 2)
    state["marker"] = marker

    engine.ecs.add_system("SplitScreen", follow, priority=-4)
    print(__doc__.split("Keys:")[1].rstrip(), flush=True)


def layout(width, height):
    """Each pass's viewport, (x, y, width, height) as fractions of the
    screen from its top left, and each camera's aspect."""
    if state["layout"] == 0:
        half = (width / 2) / height
        return (0.0, 0.0, 0.5, 1.0), half, (0.5, 0.0, 0.5, 1.0), half
    side, margin = 0.32 * height, 16.0
    corner = (1.0 - (side + margin) / width, margin / height, side / width, side / height)
    return (0.0, 0.0, 1.0, 1.0), 0.0, corner, 1.0


def follow(dt):
    width, height = engine.framebuffer_size
    if width == 0 or height == 0:
        return True
    player_view, player_aspect, map_view, map_aspect = layout(width, height)
    engine.set_custom_vec4("split.player", player_view)
    engine.set_custom_vec4("split.map", map_view)
    engine.scene.set_camera_aspect(state["player"], player_aspect)
    engine.scene.set_camera_aspect(state["map"], map_aspect)

    x, _, z = engine.scene.get_position(state["player"])
    engine.scene.set_position(state["map"], (x, MAP_HEIGHT, z))
    engine.scene.set_position(state["marker"], (x, 8.0, z))
    forward = engine.scene.get_forward(state["player"])
    if abs(forward[0]) + abs(forward[2]) > 1e-4:
        engine.scene.set_rotation(state["marker"], rotation_facing((forward[0], 0.0, forward[2])))
    return True


def on_key(key):
    if key == keys.M:
        state["layout"] = 1 - state["layout"]
        print("layout:", LAYOUTS[state["layout"]], flush=True)
    elif key == keys.P:
        os.makedirs(CAPTURES, exist_ok=True)
        path = os.path.join(CAPTURES, time.strftime("split_screen_%Y%m%d_%H%M%S.png"))
        if engine.capture_screenshot(path):
            print("saved", path, flush=True)


engine.set_on_init(on_init)
engine.set_on_key_pressed(on_key)
engine.run()

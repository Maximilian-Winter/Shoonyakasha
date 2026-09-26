"""
Shoonyakasha Japanese Shrine

A glTF shrine rendered with deferred PBR: a G-buffer pass writes the glTF
metallic-roughness material (base colour, tangent-space normal map, roughness
in G and metallic in B), then shaders/shrine_lighting.frag shades it with
image-based light from an HDR environment plus a low sun, and draws the
environment behind it. The camera circles the shrine.

The sun casts shadows. ShadowPass renders depth from the sun into a 2048x2048
map through an orthographic projection that this script builds and hands to
the shaders as a custom mat4; the lighting pass compares against it through a
comparison sampler with a 5x5 kernel of filtered lookups.

Keys:
    SPACE   stop or resume the orbit; while stopped, WASD/Q/E and the right
            mouse button fly the camera
    O       turn sun shadows off or on (disables ShadowPass)
    P       save a screenshot to shrine_screenshot.png

Usage:
    python shrine.py

Requirements:
    - the shoonyakasha package installed (pip install .)
    - run from this directory, so shrine_pipeline.json and shaders/ resolve
    - models/japanese_shrine.glb in the shared asset root; see assets/README.md

Model: "Japanese Shrine - Traditional Temple" (https://skfb.ly/pMEO8) by
aumiella, licensed under Creative Commons Attribution 4.0
(http://creativecommons.org/licenses/by/4.0/).
"""

import math
import sys

import shoonyakasha as sk
from shoonyakasha import keys

MODEL = "models/japanese_shrine.glb"
GROUND = "models/Box.gltf"

if not sk.assets.exists(MODEL):
    print(f"{MODEL} is not in the asset root. Download it as described in assets/README.md.")
    sys.exit(1)

sk.shaders.compile_dir("shaders")

engine = sk.Engine(
    title="Shoonyakasha - Japanese Shrine",
    width=1280, height=720,
    hdr_environment_path="env/farm_sunset_1k.hdr",
    pipeline_json_path="shrine_pipeline.json",
)

# The shrine stands on y = -0.13 and rises to about y = 2.2.
GROUND_TOP = -0.13
ORBIT_TARGET = (0.0, 0.85, 0.0)
ORBIT_RADIUS = 5.2
ORBIT_HEIGHT = 1.7
ORBIT_SPEED = 0.07      # radians/sec
ORBIT_START = 0.65      # radians; 0 looks straight at the front steps

# Direction the sunlight travels, shared by the light and its shadow map.
SUN_DIRECTION = (-0.55, -0.5, 0.67)
# The shadow map covers a sphere around the shrine; receivers outside the map
# are treated as lit.
SHADOW_CENTER = (0.0, 1.0, 0.0)
SHADOW_RADIUS = 2.6
SHADOW_DISTANCE = 12.0  # sun eye distance from SHADOW_CENTER
SHADOW_FAR = 30.0       # deep enough to reach ground in the shrine's long shadow
# Normal offset, constant depth bias, slope bias, sky light kept in shadow.
# ShadowPass also applies hardware slope bias (its "depthBias" block), so the
# shader-side slope term stays at 0.
SHADOW_PARAMS = (0.02, 0.0003, 0.0, 0.55)


def normalize(v):
    length = math.sqrt(sum(c * c for c in v))
    return tuple(c / length for c in v)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def matmul(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def sun_view_projection():
    """Row-major 4x4 world -> sun clip matrix, depth mapped to [0, 1]."""
    forward = normalize(SUN_DIRECTION)
    eye = tuple(c - f * SHADOW_DISTANCE for c, f in zip(SHADOW_CENTER, forward))
    up = (0.0, 0.0, 1.0) if abs(forward[1]) > 0.99 else (0.0, 1.0, 0.0)
    side = normalize(cross(forward, up))
    true_up = cross(side, forward)
    view = [
        [*side, -dot(side, eye)],
        [*true_up, -dot(true_up, eye)],
        [*(-f for f in forward), dot(forward, eye)],
        [0.0, 0.0, 0.0, 1.0],
    ]
    r, near, far = SHADOW_RADIUS, 0.1, SHADOW_FAR
    ortho = [
        [1.0 / r, 0.0, 0.0, 0.0],
        [0.0, 1.0 / r, 0.0, 0.0],
        [0.0, 0.0, -1.0 / (far - near), -near / (far - near)],
        [0.0, 0.0, 0.0, 1.0],
    ]
    return matmul(ortho, view)


def upload_sun_shadow():
    matrix = sun_view_projection()
    columns = tuple(tuple(matrix[row][column] for row in range(4)) for column in range(4))
    engine.set_custom_mat4("shadow.lightViewProj", columns)
    engine.set_custom_vec4("shadow.params", SHADOW_PARAMS)


def look_rotation(eye, target):
    """Euler rotation (pitch, yaw, 0) that points the camera from eye at target."""
    dx, dy, dz = (t - e for t, e in zip(target, eye))
    length = math.sqrt(dx * dx + dy * dy + dz * dz)
    return (math.asin(dy / length), math.atan2(-dx, -dz), 0.0)


class Orbit:
    def __init__(self):
        self.angle = ORBIT_START
        self.running = True

    def eye(self):
        return (ORBIT_TARGET[0] + ORBIT_RADIUS * math.sin(self.angle),
                ORBIT_HEIGHT,
                ORBIT_TARGET[2] + ORBIT_RADIUS * math.cos(self.angle))


orbit = Orbit()


def root_of(entity):
    parent = engine.scene.get_parent(entity)
    while parent != sk.NULL_ENTITY:
        entity, parent = parent, engine.scene.get_parent(parent)
    return entity


def build_ground():
    """A wide slab of stone made by flattening the bundled unit cube."""
    ground = engine.load_gltf_scene(GROUND)
    for root in {root_of(entity) for entity in ground.entities}:
        # Box.gltf's root node is turned 90 degrees about X; a cube looks the
        # same either way, and without it the scale below lines up with world axes.
        engine.scene.set_rotation(root, (0.0, 0.0, 0.0))
        engine.scene.set_scale(root, (40.0, 0.2, 40.0))
        engine.scene.set_position(root, (0.0, GROUND_TOP - 0.1, 0.0))
    for entity in ground.entities:
        engine.scene.set_material_vec4(entity, "baseColorFactor", (0.16, 0.13, 0.10, 1.0))
        engine.scene.set_material_float(entity, "metallicFactor", 0.0)
        engine.scene.set_material_float(entity, "roughnessFactor", 0.92)


def on_init():
    engine.set_custom_float("shrine.exposure", 0.8)
    engine.set_custom_float("shrine.iblIntensity", 1.0)
    engine.set_custom_float("shrine.skyBlur", 1.5)
    engine.set_custom_float("shrine.fogDensity", 0.14)
    engine.set_custom_float("shrine.fogStart", 7.0)

    engine.create_camera(pos=orbit.eye(), fov=50.0, speed=3.0, near_plane=0.05, far_plane=200.0)
    engine.create_directional_light(direction=SUN_DIRECTION, color=(1.0, 0.82, 0.62), intensity=3.0)
    upload_sun_shadow()

    result = engine.load_gltf_scene(MODEL)
    print(f"[shrine] {len(result.entities)} entities, {result.total_vertices} vertices, "
          f"{result.total_textures} textures")
    build_ground()

    # Runs before TransformSystem (priority 0), so the camera's world matrix
    # reflects this frame's orbit position.
    def orbit_system(dt):
        if orbit.running:
            orbit.angle += ORBIT_SPEED * dt
            eye = orbit.eye()
            camera = engine.camera_entity
            engine.scene.set_position(camera, eye)
            engine.scene.set_rotation(camera, look_rotation(eye, ORBIT_TARGET))
        return True

    engine.ecs.add_system("Orbit", orbit_system, priority=-5)


class Controls:
    def __init__(self):
        # is_key_down reports the current state, so the previous state is kept
        # to act once per press.
        self._was_down = {}

    def pressed(self, key):
        down = engine.input.is_key_down(key)
        was_down = self._was_down.get(key, False)
        self._was_down[key] = down
        return down and not was_down

    def update(self, dt):
        if self.pressed(keys.SPACE):
            orbit.running = not orbit.running
        if self.pressed(keys.O):
            # A disabled pass still clears its depth target, to 1.0 here, so
            # every receiver passes the comparison and the sun lights everything.
            enabled = not engine.is_pass_enabled("ShadowPass")
            engine.set_pass_enabled("ShadowPass", enabled)
            print("sun shadows", "on" if enabled else "off")
        if self.pressed(keys.P):
            path = "shrine_screenshot.png"
            print("screenshot ->", path if engine.capture_screenshot(path) else "failed")


controls = Controls()
engine.set_on_init(on_init)
engine.set_on_update(controls.update)

if __name__ == "__main__":
    engine.run()

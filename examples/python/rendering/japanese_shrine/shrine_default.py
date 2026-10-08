"""
Shoonyakasha Japanese Shrine, on the default pipeline

The same shrine, ground, sun and orbit as shrine.py, rendered by the
pipeline the engine ships (python/shoonyakasha/pipelines/default/) instead of
this directory's shrine_pipeline.json: no pipeline path, no shaders and no
shadow matrices of its own. The sun's cascades are fitted by the engine; the
pipeline adds contact shadows, ambient occlusion, bloom and automatic
exposure. Its settings are scene.custom.default.* values, a few of which are
set in on_init below.

Where the device has ray queries, the sun's shadows are ray traced (the
pipeline's "raytraced" preset): sharp where the shrine meets the ground,
softening with distance, and averaged over frames by TAA. Elsewhere the
cascades are used, as in the "high" preset.

shrine.py stays as the example of writing a pipeline yourself.

Keys:
    SPACE   stop or resume the orbit; while stopped, WASD/Q/E and the right
            mouse button fly the camera
    R       ray-traced or cascaded sun shadows, where the device has ray queries
    O       sun shadows off or on
    G       ambient occlusion off or on
    B       bloom off or on
    X       automatic exposure off or on (off: fixed exposure 1)
    0-4     debug view: 0 off, 1 shadow cascades, 2 shadow mask, 3 normals,
            4 ambient occlusion
    P       save a screenshot to shrine_default_screenshot.png

Usage:
    python shrine_default.py

Requirements:
    - the shoonyakasha package installed (pip install .)
    - models/japanese_shrine.glb in the shared asset root; see assets/README.md

Model: "Japanese Shrine - Traditional Temple" (https://skfb.ly/pMEO8) by
aumiella, licensed under Creative Commons Attribution 4.0
(http://creativecommons.org/licenses/by/4.0/).
"""

import math
import sys

import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.mathutil import look_rotation

MODEL = "models/japanese_shrine.glb"
GROUND = "models/Box.gltf"

if not sk.assets.exists(MODEL):
    print(f"{MODEL} is not in the asset root. Download it as described in assets/README.md.")
    sys.exit(1)

# No pipeline_json_path: the default pipeline.
engine = sk.Engine(
    title="Shoonyakasha - Japanese Shrine (default pipeline)",
    width=1280, height=720,
    hdr_environment_path="env/farm_sunset_1k.hdr",
)

# The shrine stands on y = -0.13 and rises to about y = 2.2.
GROUND_TOP = -0.13
ORBIT_TARGET = (0.0, 0.85, 0.0)
ORBIT_RADIUS = 5.2
ORBIT_HEIGHT = 1.7
ORBIT_SPEED = 0.07      # radians/sec
ORBIT_START = 0.65      # radians; 0 looks straight at the front steps

# Direction the sunlight travels.
SUN_DIRECTION = (-0.55, -0.5, 0.67)

SETTINGS = {
    "exposure": 1.0,             # compensation on top of automatic exposure
    "skyBlur": 1.5,
    "aoRadius": 0.5,
    "contactShadowLength": 0.15,
    "sunAngle": 0.4,             # degrees; how quickly ray-traced shadows soften
}


class Orbit:
    def __init__(self):
        self.angle = ORBIT_START
        self.running = True

    def eye(self):
        return (ORBIT_TARGET[0] + ORBIT_RADIUS * math.sin(self.angle),
                ORBIT_HEIGHT,
                ORBIT_TARGET[2] + ORBIT_RADIUS * math.cos(self.angle))


orbit = Orbit()
sun = [sk.NULL_ENTITY]


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
    for name, value in SETTINGS.items():
        engine.set_custom_float("default." + name, value)

    engine.create_camera(pos=orbit.eye(), fov=50.0, speed=3.0, near_plane=0.05, far_plane=200.0)
    sun[0] = engine.create_directional_light(direction=SUN_DIRECTION, color=(1.0, 0.82, 0.62),
                                             intensity=3.0)
    # The first directional light with its shadow flag set is the sun. The
    # cascades cover 30 units of view depth: the shrine and its surroundings.
    engine.scene.set_light_cast_shadows(sun[0], True)
    engine.set_sun_shadows(cascades=4, max_distance=30.0, split_lambda=0.75, resolution=2048)
    # Applied once the pipeline has loaded. The cascades still carry the
    # shadows of alpha-tested and skinned casters, which the rays do not see.
    controls.ray_traced = engine.ray_query_supported()
    if controls.ray_traced:
        engine.apply_pipeline_preset("raytraced")
    print("sun shadows:", "ray traced" if controls.ray_traced else "cascaded (no ray queries)")

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
        self.pressed = keys.KeyEdges(engine.input).pressed
        self.shadows = True
        self.ao = True
        self.bloom = True
        self.auto_exposure = True
        self.ray_traced = False     # set in on_init, once the device exists

    def update(self, dt):
        if self.pressed(keys.SPACE):
            orbit.running = not orbit.running
        if self.pressed(keys.R):
            if not engine.ray_query_supported():
                print("this device has no ray queries; the shadows stay cascaded")
            else:
                self.ray_traced = not self.ray_traced
                engine.apply_pipeline_preset("raytraced" if self.ray_traced else "high")
                print("sun shadows:", "ray traced" if self.ray_traced else "cascaded")
        if self.pressed(keys.O):
            # Without a shadow-casting sun the pipeline treats everything as lit.
            self.shadows = not self.shadows
            engine.scene.set_light_cast_shadows(sun[0], self.shadows)
            print("sun shadows", "on" if self.shadows else "off")
        if self.pressed(keys.G):
            # A disabled pass still clears its target, to "unoccluded" here.
            self.ao = not self.ao
            engine.set_pass_enabled("GTAO", self.ao)
            print("ambient occlusion", "on" if self.ao else "off")
        if self.pressed(keys.B):
            self.bloom = not self.bloom
            engine.set_custom_float("default.bloomIntensity", 0.04 if self.bloom else 0.0)
            print("bloom", "on" if self.bloom else "off")
        if self.pressed(keys.X):
            self.auto_exposure = not self.auto_exposure
            engine.set_custom_uint("default.autoExposure", 1 if self.auto_exposure else 0)
            print("automatic exposure", "on" if self.auto_exposure else "off")
        for view in range(5):
            if self.pressed(keys.NUM_0 + view):
                engine.set_custom_uint("default.debugView", view)
                print("debug view", view)
        if self.pressed(keys.P):
            path = "shrine_default_screenshot.png"
            print("screenshot ->", path if engine.capture_screenshot(path) else "failed")


controls = Controls()
engine.set_on_init(on_init)
engine.set_on_update(controls.update)

if __name__ == "__main__":
    engine.run()

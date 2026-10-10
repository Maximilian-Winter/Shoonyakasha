"""
Shoonyakasha Sponza, on the default pipeline

Intel's Sponza atrium rendered by the pipeline the engine ships, with no
pipeline or shaders of its own: sun light falling through the open roof in
cascaded shadows, two warm lamps, ambient occlusion in the arcades, bloom and
automatic exposure. Sponza is a large download; without it the example
builds a small colonnade of boxes so it still runs, and still shows the
shadows and occlusion.

Keys:
    WASD/Q/E + right mouse   fly the camera
    1 / 2 / 3                quality preset low / medium / high (the default)
    4                        ray-traced sun shadows, where the device has ray queries
    V                        cycle the debug views: final image, shadow
                             cascades, shadow mask, normals, ambient occlusion,
                             lights per cluster, motion vectors
    X                        automatic exposure off or on
    P                        save a screenshot to sponza_screenshot.png

Usage:
    python sponza.py

Requirements:
    - the shoonyakasha package installed (pip install .)
    - optionally models/NewSponza_Main_glTF_003.gltf in the shared asset root;
      `python tools/fetch_assets.py sponza` says where to get it

Sponza 2022 Scene, commissioned by Frank Meinl, sponsored by Anton Kaplanyan.
Intel Sample Library. See assets/README.md about its licence.
"""

import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.mathutil import look_rotation

SPONZA = "models/NewSponza_Main_glTF_003.gltf"
BOX = "models/Box.gltf"

# No pipeline_json_path: the default pipeline.
engine = sk.Engine(
    title="Shoonyakasha - Sponza (default pipeline)",
    width=1600, height=900,
    hdr_environment_path="env/kloofendal_misty_1k.hdr",
)

EYE = (-10.5, 1.8, -0.6)
TARGET = (4.0, 2.6, 0.4)
# Direction the sunlight travels: steeply down through the open roof.
SUN_DIRECTION = (0.3, -1.0, 0.35)
PRESETS = {keys.NUM_1: "low", keys.NUM_2: "medium", keys.NUM_3: "high", keys.NUM_4: "raytraced"}
DEBUG_VIEWS = ("final image", "shadow cascades", "shadow mask", "normals", "ambient occlusion",
               "lights per cluster", "motion vectors")


def root_of(entity):
    parent = engine.scene.get_parent(entity)
    while parent != sk.NULL_ENTITY:
        entity, parent = parent, engine.scene.get_parent(parent)
    return entity


def box(center, size, color, roughness=0.85):
    """A box of the given size centred on `center`, from the bundled unit cube."""
    result = engine.load_gltf_scene(BOX)
    for root in {root_of(entity) for entity in result.entities}:
        # Box.gltf's root node is turned 90 degrees about X; without it the
        # scale lines up with world axes.
        engine.scene.set_rotation(root, (0.0, 0.0, 0.0))
        engine.scene.set_scale(root, size)
        engine.scene.set_position(root, center)
    for entity in result.entities:
        engine.scene.set_material_vec4(entity, "baseColorFactor", color)
        engine.scene.set_material_float(entity, "metallicFactor", 0.0)
        engine.scene.set_material_float(entity, "roughnessFactor", roughness)


def build_colonnade():
    """Stand-in for Sponza: a floor, two rows of pillars under beams, an end wall."""
    stone = (0.55, 0.5, 0.42, 1.0)
    box((0.0, -0.1, 0.0), (30.0, 0.2, 14.0), (0.45, 0.42, 0.38, 1.0))
    for side in (-3.5, 3.5):
        for x in range(-10, 11, 4):
            box((float(x), 2.5, side), (0.7, 5.0, 0.7), stone)
        box((0.0, 5.3, side), (22.0, 0.6, 1.2), stone)
        box((0.0, 3.0, side * 1.9), (30.0, 6.0, 0.4), (0.62, 0.55, 0.46, 1.0))
    box((12.5, 3.0, 0.0), (0.4, 6.0, 14.0), (0.62, 0.55, 0.46, 1.0))
    box((1.0, 0.5, 0.8), (1.0, 1.0, 1.0), (0.7, 0.2, 0.15, 1.0), roughness=0.5)


def on_init():
    camera = engine.create_camera(pos=EYE, fov=60.0, speed=4.0, near_plane=0.05, far_plane=300.0)
    engine.scene.set_rotation(camera, look_rotation(EYE, TARGET))

    sun = engine.create_directional_light(direction=SUN_DIRECTION, color=(1.0, 0.93, 0.82),
                                          intensity=6.0)
    engine.scene.set_light_cast_shadows(sun, True)
    engine.set_sun_shadows(cascades=4, max_distance=40.0, split_lambda=0.7, resolution=2048)
    engine.create_point_light(pos=(-4.0, 2.2, 2.8), color=(1.0, 0.62, 0.3), intensity=4.0, range=8.0)
    engine.create_point_light(pos=(4.0, 2.2, -2.8), color=(1.0, 0.62, 0.3), intensity=4.0, range=8.0)

    if sk.assets.exists(SPONZA):
        result = engine.load_gltf_scene(SPONZA, max_texture_size=2048)
        print(f"[sponza] {len(result.entities)} entities, {result.total_vertices} vertices, "
              f"{result.total_textures} textures")
    else:
        print(f"[sponza] {SPONZA} is not in the asset root; showing a colonnade of boxes instead.")
        build_colonnade()


class Controls:
    def __init__(self):
        self.pressed = keys.KeyEdges(engine.input).pressed
        self.debug_view = 0
        self.auto_exposure = True

    def update(self, dt):
        for key, preset in PRESETS.items():
            if self.pressed(key):
                if preset == "raytraced" and not engine.ray_query_supported():
                    print("this device has no ray queries; keeping the current preset")
                    continue
                engine.apply_pipeline_preset(preset)
                print("preset", preset)
        if self.pressed(keys.V):
            self.debug_view = (self.debug_view + 1) % len(DEBUG_VIEWS)
            engine.set_custom_uint("default.debugView", self.debug_view)
            print("debug view:", DEBUG_VIEWS[self.debug_view])
        if self.pressed(keys.X):
            self.auto_exposure = not self.auto_exposure
            engine.set_custom_uint("default.autoExposure", 1 if self.auto_exposure else 0)
            print("automatic exposure", "on" if self.auto_exposure else "off")
        if self.pressed(keys.P):
            path = "sponza_screenshot.png"
            print("screenshot ->", path if engine.capture_screenshot(path) else "failed")


controls = Controls()
engine.set_on_init(on_init)
engine.set_on_update(controls.update)

if __name__ == "__main__":
    engine.run()

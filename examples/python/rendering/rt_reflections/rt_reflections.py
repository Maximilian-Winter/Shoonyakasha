"""
Shoonyakasha ray-traced reflections

The ray-traced reflections of Embark's kajiya renderer in Crytek Sponza, on
top of its diffuse global illumination (the ReSTIR GI example's). At half
resolution, each pixel traces one ray a frame drawn from its surface's GGX
lobe; reservoirs reuse the best of those rays over time, and are traced
again to see whether the light changed. A ratio estimator over a kernel
shaped like the lobe resolves them to full resolution, and a temporal
filter that follows both the surface and the reflected image smooths them.
The scene, lights and camera are the reference path tracer's
(../reference_pt), so the two can be held side by side.

Keys:
    WASD/Q/E + right mouse   fly the camera
    1 / 2 / 3 / 4 / 5        image / indirect diffuse / irradiance cache / direct light / reflections
    T                        ReSTIR and filters on or off (off: the raw candidates)
    F                        freeze the irradiance cache
    F2                       hide or show the panel
    P                        save a screenshot to rt_reflections_captures/

Usage:
    python rt_reflections.py
    python rt_reflections.py --frames 300 --screenshot rr.png   # run 300 frames, save, quit
    python rt_reflections.py --view 4 --exposure 8              # the reflections alone, brighter
    python rt_reflections.py --walk 1.5                         # walk the camera down the nave

Requirements:
    - the shoonyakasha package installed (pip install .)
    - a GPU with ray queries
    - optionally glTF-Sample-Assets in the repository root, for Sponza;
      without it a colonnade of boxes stands in
"""

import argparse
import math
import os
import time

import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.mathutil import look_rotation

HERE = os.path.dirname(os.path.abspath(__file__))
PIPELINE = os.path.join(HERE, "pipeline", "pipeline.json")
REPOSITORY = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
SPONZA = os.path.join(REPOSITORY, "glTF-Sample-Assets", "Models", "Sponza", "glTF", "Sponza.gltf")
BOX = "models/Box.gltf"

EYE = (-11.0, 2.0, -0.5)
TARGET = (8.0, 3.5, 0.5)
SUN_DIRECTION = (0.3, -1.0, 0.35)
LAMPS = ((-4.0, 2.2, 1.2), (4.0, 2.2, -1.2))
VIEWS = ("Image", "Diffuse", "Cache", "Direct", "Reflect")
ALL_STAGES = 127   # every stage of the diffuse light and of the reflections

parser = argparse.ArgumentParser(description="Ray-traced reflections",
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
parser.add_argument("--scene", choices=("sponza", "colonnade"), default="sponza",
                    help="Sponza (when glTF-Sample-Assets is there), or a colonnade of boxes")
parser.add_argument("--view", type=int, choices=(0, 1, 2, 3, 4), default=0,
                    help="0 image, 1 indirect diffuse light, 2 irradiance cache, 3 direct light only, "
                         "4 reflections")
parser.add_argument("--stages", type=int, default=ALL_STAGES,
                    help="the stages that run, a sum of: 1 temporal resampling, 2 spatial resampling, "
                         "4 near-field split, 8 temporal filter, 16 spatial filter; for the reflections, 32 temporal "
                         "resampling and validation, 64 filters (default 127)")
parser.add_argument("--sky", type=float, default=1.0, help="intensity of the sky's light")
parser.add_argument("--exposure", type=float, default=1.0, help="multiplies the image before the tone curve")
parser.add_argument("--walk", type=float, default=0.0, metavar="SPEED",
                    help="walk the camera along the nave at SPEED metres a second")
parser.add_argument("--frames", type=int, default=300, help="frames before --screenshot")
parser.add_argument("--screenshot", metavar="FILE", help="save a screenshot after --frames frames, then quit")
parser.add_argument("--width", type=int, default=1600)
parser.add_argument("--height", type=int, default=900)
args = parser.parse_args()

sk.shaders.compile_dir(os.path.join(HERE, "pipeline", "shaders"))
engine = sk.Engine(
    title="Shoonyakasha - Ray-traced reflections",
    width=args.width, height=args.height,
    hdr_environment_path="env/kloofendal_misty_1k.hdr",
    pipeline_json_path=PIPELINE,
)


# ── The scene: the reference path tracer's ───────────────────────────────

def root_of(entity):
    parent = engine.scene.get_parent(entity)
    while parent != sk.NULL_ENTITY:
        entity, parent = parent, engine.scene.get_parent(parent)
    return entity


def box(center, size, color, roughness=0.85):
    """A box of the given size centred on `center`, from the bundled unit cube."""
    result = engine.load_gltf_scene(BOX)
    for root in {root_of(entity) for entity in result.entities}:
        engine.scene.set_rotation(root, (0.0, 0.0, 0.0))
        engine.scene.set_scale(root, size)
        engine.scene.set_position(root, center)
    for entity in result.entities:
        engine.scene.set_material_vec4(entity, "baseColorFactor", color)
        engine.scene.set_material_float(entity, "metallicFactor", 0.0)
        engine.scene.set_material_float(entity, "roughnessFactor", roughness)


def build_colonnade():
    """Stand-in for Sponza: a floor, two rows of pillars under beams, walls."""
    stone = (0.55, 0.5, 0.42, 1.0)
    box((0.0, -0.1, 0.0), (30.0, 0.2, 14.0), (0.45, 0.42, 0.38, 1.0))
    for side in (-3.5, 3.5):
        for x in range(-10, 11, 4):
            box((float(x), 2.5, side), (0.7, 5.0, 0.7), stone)
        box((0.0, 5.3, side), (22.0, 0.6, 1.2), stone)
        box((0.0, 3.0, side * 1.9), (30.0, 6.0, 0.4), (0.62, 0.55, 0.46, 1.0))
    box((12.5, 3.0, 0.0), (0.4, 6.0, 14.0), (0.62, 0.55, 0.46, 1.0))
    box((1.0, 0.5, 0.8), (1.0, 1.0, 1.0), (0.7, 0.2, 0.15, 1.0), roughness=0.5)


def build_scene():
    camera = engine.create_camera(pos=EYE, fov=60.0, speed=4.0, near_plane=0.05, far_plane=300.0)
    engine.scene.set_rotation(camera, look_rotation(EYE, TARGET))
    sun = engine.create_directional_light(direction=SUN_DIRECTION, color=(1.0, 0.93, 0.82), intensity=6.0)
    engine.scene.set_light_cast_shadows(sun, True)
    for position in LAMPS:
        lamp = engine.create_point_light(pos=position, color=(1.0, 0.62, 0.3), intensity=4.0, range=8.0)
        engine.scene.set_light_cast_shadows(lamp, True)
    if args.scene == "sponza" and os.path.isfile(SPONZA):
        result = engine.load_gltf_scene(SPONZA, max_texture_size=2048)
        print("[rt_reflections] %d entities, %d textures" % (len(result.entities), result.total_textures))
    else:
        if args.scene == "sponza":
            print("[rt_reflections] Sponza is not there (%s); a colonnade of boxes stands in." % SPONZA)
        build_colonnade()


# ── Settings and the panel ───────────────────────────────────────────────

GOLD = (0.95, 0.85, 0.55, 1.0)
MUTED = (0.62, 0.66, 0.72, 1.0)
FRAME = (0.07, 0.075, 0.09, 0.88)
SURFACE = (0.22, 0.24, 0.30, 1.0)
ACCENT = (0.30, 0.52, 0.85, 1.0)


class Settings:
    """The scene.custom.rg.* values the pipeline reads."""

    def __init__(self):
        self.view = args.view
        self.stages = args.stages
        self.freeze = False
        self.exposure = args.exposure
        self.environment = args.sky

    def apply(self):
        engine.set_custom_uint("rr.view", self.view)
        engine.set_custom_uint("rr.stages", self.stages)
        engine.set_custom_uint("rr.freeze", 1 if self.freeze else 0)
        engine.set_custom_float("rr.exposure", self.exposure)
        engine.set_custom_float("rr.environmentIntensity", self.environment)


class Panel:
    WIDTH = 400

    def __init__(self, settings):
        self.settings = settings
        self.visible = True
        self.texts = {}

    def build(self):
        ui = engine.ui
        self.canvas = ui.create_canvas(reference_size=(1920, 1080))
        frame = ui.create_panel(self.canvas, color=FRAME)
        ui.set_rect(frame, anchor_min=(0, 1), anchor_max=(0, 1), pivot=(0, 1), position=(20, -20),
                    size=(self.WIDTH, 400))
        column = ui.create_vertical_layout(frame, spacing=8, padding=(20, 16, 20, 16))
        ui.set_rect(column, anchor_min=(0, 0), anchor_max=(1, 1), size=(0, 0))
        ui.set_layout_child_sizing(column, expand_width=True)
        ui.create_text(column, "Ray-traced reflections", size=28, color=GOLD, font=ui.load_font("fonts/PlayfairDisplay.ttf"))
        ui.create_text(column, "After kajiya (Embark). F2 hides this panel.", size=15, color=MUTED)
        self.stats = ui.create_text(column, "", size=16, color=MUTED)
        self.exposure_label = ui.create_text(column, "", size=18)
        self.exposure = ui.create_slider(column, min=-4.0, max=4.0, value=math.log2(self.settings.exposure),
                                         size=(0, 22))
        ui.set_accessible(self.exposure, "Exposure")
        self.environment_label = ui.create_text(column, "", size=18)
        self.environment = ui.create_slider(column, min=0.0, max=4.0, value=self.settings.environment,
                                            size=(0, 22))
        ui.set_accessible(self.environment, "Sky intensity")
        row = ui.create_horizontal_layout(column, size=(0, 34), spacing=6)
        ui.set_layout_child_sizing(row, expand_width=True)
        self.views = []
        for value, label in enumerate(VIEWS):
            button = ui.create_button(row, label, size=(60, 34))
            ui.set_font_size(button, 15)
            self.views.append((value, button))
        self.restir = ui.create_toggle(column, "ReSTIR and filters", is_on=self.settings.stages != 0, size=(0, 28))
        self.freeze = ui.create_toggle(column, "Freeze the irradiance cache", is_on=self.settings.freeze,
                                       size=(0, 28))

    def set_text(self, element, text):
        if self.texts.get(element) != text:
            self.texts[element] = text
            engine.ui.set_text(element, text)

    def update(self, stats):
        ui, s = engine.ui, self.settings
        if ui.value_changed(self.exposure):
            s.exposure = 2.0 ** ui.slider_value(self.exposure)
        if ui.value_changed(self.environment):
            s.environment = ui.slider_value(self.environment)
        for value, button in self.views:
            if ui.was_clicked(button):
                s.view = value
        if ui.value_changed(self.restir):
            s.stages = ALL_STAGES if ui.toggle_value(self.restir) else 0
        if ui.value_changed(self.freeze):
            s.freeze = ui.toggle_value(self.freeze)
        for toggle, on in ((self.restir, s.stages != 0), (self.freeze, s.freeze)):
            if ui.toggle_value(toggle) != on:
                ui.set_toggle_value(toggle, on)
        for value, button in self.views:
            ui.set_color(button, ACCENT if value == s.view else SURFACE)
        self.set_text(self.stats, stats)
        self.set_text(self.exposure_label, "Exposure   %.2f" % s.exposure)
        self.set_text(self.environment_label, "Sky   %.2f" % s.environment)

    def toggle(self):
        self.visible = not self.visible
        engine.ui.set_visible(self.canvas, self.visible)


class Controls:
    def __init__(self):
        self.pressed = keys.KeyEdges(engine.input).pressed
        self.settings = Settings()
        self.panel = Panel(self.settings)
        self.walked = 0.0
        self.frames = 0
        self.wait = 0.0
        self.stats = ""

    def start(self):
        self.settings.apply()
        engine.enable_render_stats()
        if args.screenshot is None:
            self.panel.build()

    def update(self, dt):
        for key, view in ((keys.NUM_1, 0), (keys.NUM_2, 1), (keys.NUM_3, 2), (keys.NUM_4, 3), (keys.NUM_5, 4)):
            if self.pressed(key):
                self.settings.view = view
        if self.pressed(keys.T):
            self.settings.stages = 0 if self.settings.stages else ALL_STAGES
        if self.pressed(keys.F):
            self.settings.freeze = not self.settings.freeze
        if self.pressed(keys.F2) and args.screenshot is None:
            self.panel.toggle()
        if self.pressed(keys.P):
            save_screenshot(capture_path())
        if args.screenshot is None:
            self.wait -= dt
            if self.wait <= 0.0:
                self.wait = 0.5
                stats = engine.render_stats
                if stats and stats.get("gpu_ms") is not None:
                    self.stats = "GPU %.2f ms   %.0f FPS" % (stats["gpu_ms"], stats["fps"])
            self.panel.update(self.stats)
        self.settings.apply()

        if args.walk:
            self.walked += args.walk * dt
            camera = engine.scene.get_main_camera()
            engine.scene.set_position(camera, (EYE[0] + self.walked, EYE[1], EYE[2]))

        self.frames += 1
        if args.screenshot and self.frames >= args.frames:
            stats = engine.render_stats
            if stats and stats.get("gpu_ms") is not None:
                print("[rt_reflections] GPU %.2f ms a frame" % stats["gpu_ms"], flush=True)
            save_screenshot(args.screenshot)
            os._exit(0)        # the engine has no quit call


def capture_path():
    directory = os.path.join(os.getcwd(), "rt_reflections_captures")
    os.makedirs(directory, exist_ok=True)
    return os.path.join(directory, time.strftime("rt_reflections_%Y%m%d_%H%M%S.png"))


def save_screenshot(path):
    print("[rt_reflections] screenshot ->", path if engine.capture_screenshot(os.path.abspath(path)) else "failed",
          flush=True)


controls = Controls()


def on_init():
    if not engine.ray_query_supported():
        print("[rt_reflections] this device has no ray queries, which the GI needs.")
        os._exit(1)
    build_scene()
    controls.start()


engine.set_on_init(on_init)
engine.set_on_update(controls.update)

if __name__ == "__main__":
    engine.run()

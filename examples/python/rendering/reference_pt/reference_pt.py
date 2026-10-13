"""
Shoonyakasha reference path tracer

Crytek Sponza path traced: one path per pixel per frame, averaged while the
camera stands still, so the image converges to the light transport of the
scene: the sun through the open roof, two warm lamps, the sky, and light
bouncing between the walls. The paths are traced in a compute shader with ray
queries; at every hit the shader reads the surface (vertices, material,
textures) through the scene's ray instances and texture array. The lights
are the default pipeline's light model, so the same scene on the default
pipeline (--pipeline default) can be held up against it.

Keys:
    WASD/Q/E + right mouse   fly the camera; moving starts the average over
    1 / 2 / 3                everything / indirect light only / white furnace
    R                        start the average over
    F2                       hide or show the panel
    P                        save a screenshot to reference_pt_captures/

Usage:
    python reference_pt.py
    python reference_pt.py --samples 1024 --screenshot out.png   # converge, save, quit
    python reference_pt.py --pipeline default --screenshot raster.png

Requirements:
    - the shoonyakasha package installed (pip install .)
    - a GPU with ray queries
    - optionally glTF-Sample-Assets in the repository root, for Sponza;
      without it a colonnade of boxes stands in
"""

import argparse
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
# Two warm lamps in the open nave, clear of the pillars and curtains.
LAMPS = ((-4.0, 2.2, 1.2), (4.0, 2.2, -1.2))
MODES = ("Everything", "Indirect only", "White furnace")

parser = argparse.ArgumentParser(description="Reference path tracer",
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
parser.add_argument("--pipeline", choices=("pt", "default"), default="pt",
                    help="pt: the path tracer; default: the same scene on the default pipeline")
parser.add_argument("--scene", choices=("sponza", "colonnade"), default="sponza",
                    help="Sponza (when glTF-Sample-Assets is there), or a colonnade of boxes")
parser.add_argument("--bounces", type=int, default=8, help="bounces after the first hit (1 to 16)")
parser.add_argument("--mode", type=int, choices=(0, 1, 2), default=0,
                    help="0 everything, 1 indirect light only, 2 white furnace")
parser.add_argument("--sky", type=float, default=1.0, help="intensity of the sky's light (both pipelines)")
parser.add_argument("--samples", type=int, default=0,
                    help="stop averaging at this many paths per pixel (0: never)")
parser.add_argument("--screenshot", metavar="FILE",
                    help="save a screenshot once --samples paths are in (without --samples, after --settle "
                         "seconds), then quit")
parser.add_argument("--settle", type=float, default=3.0,
                    help="seconds before --screenshot on the default pipeline")
parser.add_argument("--width", type=int, default=1600)
parser.add_argument("--height", type=int, default=900)
args = parser.parse_args()

PATH_TRACED = args.pipeline == "pt"
if PATH_TRACED:
    sk.shaders.compile_dir(os.path.join(HERE, "pipeline", "shaders"))
engine = sk.Engine(
    title="Shoonyakasha - Reference path tracer" if PATH_TRACED else "Shoonyakasha - Sponza, default pipeline",
    width=args.width, height=args.height,
    hdr_environment_path="env/kloofendal_misty_1k.hdr",
    **({"pipeline_json_path": PIPELINE} if PATH_TRACED else {}),
)


# ── The scene ────────────────────────────────────────────────────────────

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
    engine.set_sun_shadows(cascades=4, max_distance=40.0, split_lambda=0.7, resolution=2048)
    for position in LAMPS:
        lamp = engine.create_point_light(pos=position, color=(1.0, 0.62, 0.3), intensity=4.0, range=8.0)
        engine.scene.set_light_cast_shadows(lamp, True)
    if args.scene == "sponza" and os.path.isfile(SPONZA):
        result = engine.load_gltf_scene(SPONZA, max_texture_size=2048)
        print("[reference_pt] %d entities, %d textures" % (len(result.entities), result.total_textures))
    else:
        if args.scene == "sponza":
            print("[reference_pt] Sponza is not there (%s); a colonnade of boxes stands in." % SPONZA)
        build_colonnade()


# ── The panel ────────────────────────────────────────────────────────────

GOLD = (0.95, 0.85, 0.55, 1.0)
MUTED = (0.62, 0.66, 0.72, 1.0)
FRAME = (0.07, 0.075, 0.09, 0.88)
SURFACE = (0.22, 0.24, 0.30, 1.0)
ACCENT = (0.30, 0.52, 0.85, 1.0)


class Settings:
    """The scene.custom.pt.* values the pipeline reads."""

    def __init__(self):
        self.bounces = max(1, min(args.bounces, 16))
        self.mode = args.mode
        self.exposure = 1.0
        self.environment = args.sky

    def apply(self):
        engine.set_custom_uint("pt.maxBounces", self.bounces)
        engine.set_custom_uint("pt.mode", self.mode)
        engine.set_custom_float("pt.exposure", self.exposure)
        engine.set_custom_float("pt.environmentIntensity", self.environment)
        engine.set_custom_float("pt.maxSamples", float(args.samples))


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
                    size=(self.WIDTH, 430))
        column = ui.create_vertical_layout(frame, spacing=8, padding=(20, 16, 20, 16))
        ui.set_rect(column, anchor_min=(0, 0), anchor_max=(1, 1), size=(0, 0))
        ui.set_layout_child_sizing(column, expand_width=True)
        ui.create_text(column, "Reference path tracer", size=28, color=GOLD,
                       font=ui.load_font("fonts/PlayfairDisplay.ttf"))
        ui.create_text(column, "F2 hides this panel.", size=15, color=MUTED)
        self.samples = ui.create_text(column, "", size=18)
        self.bounces_label = ui.create_text(column, "", size=18)
        self.bounces = ui.create_slider(column, min=1, max=16, value=self.settings.bounces, size=(0, 22))
        ui.set_slider_range(self.bounces, 1, 16, True)
        ui.set_accessible(self.bounces, "Bounces")
        self.exposure_label = ui.create_text(column, "", size=18)
        self.exposure = ui.create_slider(column, min=-4.0, max=4.0, value=0.0, size=(0, 22))
        ui.set_accessible(self.exposure, "Exposure")
        self.environment_label = ui.create_text(column, "", size=18)
        self.environment = ui.create_slider(column, min=0.0, max=4.0, value=self.settings.environment,
                                            size=(0, 22))
        ui.set_accessible(self.environment, "Sky intensity")
        row = ui.create_horizontal_layout(column, size=(0, 34), spacing=6)
        ui.set_layout_child_sizing(row, expand_width=True)
        self.modes = []
        for value, label in enumerate(MODES):
            button = ui.create_button(row, label, size=(60, 34))
            ui.set_font_size(button, 15)
            self.modes.append((value, button))
        self.restart = ui.create_button(column, "Start over", size=(0, 34))
        ui.set_font_size(self.restart, 16)

    def set_text(self, element, text):
        if self.texts.get(element) != text:
            self.texts[element] = text
            engine.ui.set_text(element, text)

    def update(self, samples, rate):
        """Read the controls; True when a setting the image depends on changed."""
        ui, s = engine.ui, self.settings
        changed = False
        if ui.value_changed(self.bounces):
            s.bounces = int(round(ui.slider_value(self.bounces)))
            changed = True
        if ui.value_changed(self.exposure):
            s.exposure = 2.0 ** ui.slider_value(self.exposure)   # display only
        if ui.value_changed(self.environment):
            s.environment = ui.slider_value(self.environment)
            changed = True
        for value, button in self.modes:
            if ui.was_clicked(button) and value != s.mode:
                s.mode = value
                changed = True
        restart = ui.was_clicked(self.restart)

        if abs(ui.slider_value(self.bounces) - s.bounces) > 1e-3:
            ui.set_slider_value(self.bounces, s.bounces)
        for value, button in self.modes:
            ui.set_color(button, ACCENT if value == s.mode else SURFACE)
        self.set_text(self.samples, "%d paths per pixel   %.0f per second" % (samples, rate))
        self.set_text(self.bounces_label, "Bounces   %d" % s.bounces)
        self.set_text(self.exposure_label, "Exposure   %.2f" % s.exposure)
        self.set_text(self.environment_label, "Sky   %.2f" % s.environment)
        return changed or restart

    def toggle(self):
        self.visible = not self.visible
        engine.ui.set_visible(self.canvas, self.visible)


# ── Accumulation ─────────────────────────────────────────────────────────

class Controls:
    """Starts the average over whenever the camera or a setting changes:
    pt.reset is 1 in that frame. Counts the paths per pixel since."""

    def __init__(self):
        self.pressed = keys.KeyEdges(engine.input).pressed
        self.settings = Settings()
        self.panel = Panel(self.settings)
        self.pose = None
        self.frames = 0
        self.samples = 0
        self.clock = 0.0
        self.since_reset = 0.0

    def start(self):
        self.settings.apply()
        if args.screenshot is None:
            self.panel.build()

    def camera_pose(self):
        camera = engine.scene.get_main_camera()
        return engine.scene.get_position(camera), engine.scene.get_rotation(camera)

    def update(self, dt):
        self.clock += dt
        changed = False
        for key, mode in ((keys.NUM_1, 0), (keys.NUM_2, 1), (keys.NUM_3, 2)):
            if self.pressed(key) and self.settings.mode != mode:
                self.settings.mode = mode
                changed = True
        if self.pressed(keys.R):
            changed = True
        if self.pressed(keys.F2) and args.screenshot is None:
            self.panel.toggle()
        if self.pressed(keys.P):
            save_screenshot(capture_path())
        if args.screenshot is None:
            rate = self.samples / self.since_reset if self.since_reset > 0.0 else 0.0
            changed = self.panel.update(self.samples, rate) or changed
        self.settings.apply()

        pose = self.camera_pose()
        moved = pose != self.pose
        self.pose = pose
        reset = changed or moved
        engine.set_custom_uint("pt.reset", 1 if reset else 0)
        if reset:
            self.frames, self.since_reset = 0, 0.0
        self.frames += 1
        self.since_reset += dt
        self.samples = min(self.frames, args.samples) if args.samples else self.frames

        # A few frames past the last path, so the presented frame holds it.
        if args.screenshot and (self.frames >= args.samples + 4 if args.samples else self.clock >= args.settle):
            save_screenshot(args.screenshot)
            os._exit(0)        # the engine has no quit call


def capture_path():
    directory = os.path.join(os.getcwd(), "reference_pt_captures")
    os.makedirs(directory, exist_ok=True)
    return os.path.join(directory, time.strftime("reference_pt_%Y%m%d_%H%M%S.png"))


def save_screenshot(path):
    print("[reference_pt] screenshot ->", path if engine.capture_screenshot(os.path.abspath(path)) else "failed",
          flush=True)


controls = Controls()


def on_init():
    if PATH_TRACED and not engine.ray_query_supported():
        print("[reference_pt] this device has no ray queries, which the path tracer needs.")
        os._exit(1)
    build_scene()
    if PATH_TRACED:
        controls.start()
    else:
        # The path tracer's exposure and tone curve, without bloom, for comparison.
        engine.set_custom_uint("default.autoExposure", 0)
        engine.set_custom_float("default.exposure", 1.0)
        engine.set_custom_float("default.bloomIntensity", 0.0)
        engine.set_custom_float("default.iblIntensity", args.sky)


def on_update(dt):
    if PATH_TRACED:
        controls.update(dt)
    elif args.screenshot:
        controls.clock += dt
        if controls.clock >= args.settle:
            save_screenshot(args.screenshot)
            os._exit(0)


engine.set_on_init(on_init)
engine.set_on_update(on_update)

if __name__ == "__main__":
    engine.run()

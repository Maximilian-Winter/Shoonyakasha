"""
Shoonyakasha Showroom: cars and starships on a turntable

A model showroom on the showroom pipeline (pipeline/), a copy of the default
pipeline with:
- the sun's shadows from a virtual shadow map: 8 clipmap levels of 4096 x
  4096 virtual texels, of which only the 128 x 128 pages that visible pixels
  need are rendered, into a cached pool. Close up, a texel is a millimetre.
- softboxes as rectangular area lights, by linearly transformed cosines:
  highlights with the softbox's shape, stretched along curved paint as a
  real panel's are, and soft shadows from rays towards random points of the
  panel, where the GPU has ray queries. Spotlights are glowing spheres.

Models come from Sketchfab (see README.md for their credits). Fetch them once
with `python tools/fetch_assets.py showroom` (needs a Sketchfab API token);
without them the showroom shows the bundled Fox. Any glTF can be added with
--extra.

Run interactively, it has two canvas UIs (engine.ui):
- a settings panel on the screen, at the left: the lighting, the light,
  environment and exposure levels, shadows, reflections, camera, lens and
  tone mapper, paint and clear coat, the floor and the softboxes; it
  scrolls (wheel, drag or scrollbar), and Tab and the arrow keys move
  through it once a control has focus (Escape leaves it)
- an info stand in the scene beside the turntable: a world canvas on a post
  with the model's name, length and credit, and its own buttons for the
  model, the paint and the turntable. The model in front of it stops the
  pointer, and it keeps its brightness under every lighting's exposure

Keys:
    LEFT / RIGHT   previous / next model
    L              next lighting: studio, hard light, night, neon, stained glass
    C              next camera: turntable, cinematic, orbit (drag with the left
                   mouse button, scroll to zoom), free (WASD/Q/E, right mouse)
    G              next shadow technique: VSM + ray-traced lights, all ray traced,
                   VSM with shadow maps, cascades
    R              ray-traced reflections on or off (with ray-traced lights)
    T              turntable on or off
    K              next paint colour (cars)
    X              clear coat on or off (the lacquer over car paint)
    J              softboxes as rectangles or as spheres (before and after)
    U              softbox diffusers: grid, plain fabric, even
    M              next tone mapper: AgX, PBR Neutral, ACES
    B              depth of field: off, f/2.8, f/0.8 (more than a real lens),
                   focused where the camera looks
    Y              turn the model 90 degrees (prints the catalogue yaw)
    F              floor: dark gloss or white
    V              debug views (VSM levels and pages, shadow mask, normals ...)
    H              hide or show the credits and status
    O              render stats: frame rate, GPU time per pass, draw calls
    F2             settings panel on or off
    P              screenshot into showroom_captures/
    I              high-quality still into showroom_captures/: the view held
                   while --samples frames are averaged
    F9             start or stop recording a video into showroom_captures/
    F1             help on screen

Automatic capture, for posting (the credits stay on screen unless
--no-overlay). Stills average --samples frames each: no aliasing, no
ray-tracing noise. Video renders --motion-blur sub-frames for each frame and
averages them, motion-blurred over a --shutter of the frame interval, and
renders at --supersample times the window's size (2 for recordings):
    python showroom.py --screenshots shots/            stills of every model
    python showroom.py --record tour.mp4               a cinematic tour
    python showroom.py --record reel.mp4 --vertical --models alfa_gtv6
    python showroom.py --record tour.mp4 --dof 1.4     with depth of field
Recording prints its progress every few seconds; Esc or Ctrl+C stops it
early and keeps what is recorded.
"""

import argparse
import json
import math
import os
import signal
import sys
import time

import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.mathutil import (add, lerp, look_rotation, normalize, orbit, rotation_facing,
                                   scale, smoothstep, yaw_point)

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import catalogue  # noqa: E402
import studio  # noqa: E402

PIPELINE = os.path.join(HERE, "pipeline", "pipeline.json")

# settings.toneMapper in the pipeline's tonemap.frag
TONE_MAPPERS = {"agx": (2, "AgX"), "neutral": (1, "PBR Neutral"), "aces": (0, "ACES")}

# Key U: what the softboxes' diffusers look like, in the panels and in the
# light they cast (the pipeline's softbox.glsl): the grid's cells across a
# panel's short side, 0 for plain fabric (its hotspot alone), -1 for an
# evenly lit panel.
DIFFUSERS = {"grid": (4, "grid"), "fabric": (0, "plain fabric"), "even": (-1, "even panels")}

# Orbit camera: degrees per pixel dragged, and the zoom per notch of the
# wheel.
ORBIT_DEGREES_PER_PIXEL = 0.25
ORBIT_ZOOM_STEP = 0.88

# Key B: depth of field off, then at these f-numbers. Whole-car shots need
# more than a real lens to show much blur.
DOF_STOPS = (0.0, 2.8, 0.8)

# Body paint whose file declares no clear coat gets this one: a full coat,
# polished almost to a mirror.
PAINT_COAT = (1.0, 0.03)
FONT = "fonts/Roboto-Regular.ttf"


def parse_args():
    parser = argparse.ArgumentParser(description="Shoonyakasha showroom",
                                     formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    parser.add_argument("--models", help="comma-separated catalogue names to show, in order "
                        "(default: every fetched model)")
    parser.add_argument("--extra", action="append", default=[], metavar="GLTF",
                        help="also show this glTF file (repeatable); LENGTH= in front scales it, "
                             "e.g. 4.5=path/car.gltf")
    parser.add_argument("--lighting", choices=["studio", "hardlight", "night", "neon", "window"],
                        help="lighting to open with (default: each model's own)")
    parser.add_argument("--camera", choices=["turntable", "cinematic", "orbit", "free"], default="turntable")
    parser.add_argument("--shadows", choices=["hybrid", "vsm", "raytraced", "cascades"],
                        help="shadow technique: hybrid is the virtual shadow map for the sun with ray-traced "
                             "light shadows (the default where the GPU has ray queries), vsm the map with "
                             "shadow-mapped lights (the default elsewhere), raytraced rays for everything, "
                             "cascades the default pipeline's cascaded shadow maps")
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--vertical", action="store_true", help="1080 x 1920, for phone-shaped video")
    parser.add_argument("--hdri", help="environment map instead of the generated studio")
    parser.add_argument("--texture-size", type=int, default=4096, help="largest texture side to load")
    parser.add_argument("--no-overlay", action="store_true", help="no credits or status on screen")
    parser.add_argument("--no-reflections", action="store_true", help="no ray-traced reflections")
    parser.add_argument("--tonemapper", choices=list(TONE_MAPPERS), default="agx",
                        help="tone curve: AgX, Khronos PBR Neutral or ACES")
    parser.add_argument("--no-clearcoat", action="store_true", help="no clear coat over the paint")
    parser.add_argument("--softbox", choices=list(DIFFUSERS), default="grid",
                        help="softbox diffusers: a fabric grid, plain fabric (a hotspot), or evenly lit")
    parser.add_argument("--light-image", metavar="IMAGE",
                        help="picture the stained-glass window shines with (PNG, JPEG ...), stretched to "
                             "its 1:2 shape; default a generated window")
    parser.add_argument("--sphere-lights", action="store_true",
                        help="shade softboxes as spheres, not as their rectangles")
    parser.add_argument("--screenshots", metavar="DIR", help="save stills of every model and exit")
    parser.add_argument("--record", metavar="FILE", help="record a cinematic tour (.mp4/.mkv) and exit")
    parser.add_argument("--seconds", type=float, default=0.0,
                        help="seconds per model when recording (default: one pass of the cinematic shots)")
    parser.add_argument("--fps", type=int, default=30, help="frame rate of recordings")
    parser.add_argument("--settle", type=int, default=48,
                        help="frames to let TAA, ray-traced shadows and exposure settle before a still")
    parser.add_argument("--samples", type=int, default=64,
                        help="frames averaged into each still (1: none)")
    parser.add_argument("--motion-blur", type=int, default=8,
                        help="sub-frames averaged into each recorded frame (1: no motion blur)")
    parser.add_argument("--shutter", type=float, default=0.5,
                        help="fraction of the frame interval the motion blur spans (0.5: a 180 degree shutter)")
    parser.add_argument("--vignette", type=float, default=0.0,
                        help="darken the corners by up to this much (0.15 is subtle)")
    parser.add_argument("--grain", type=float, default=0.0,
                        help="film grain strength (0.02 is subtle; helps video compression)")
    parser.add_argument("--dof", type=float, default=0.0, metavar="F",
                        help="depth of field at f-number F, focused where the camera looks (0: off; "
                             "below 1 is more than a real lens)")
    parser.add_argument("--focus", type=float, default=0.0, metavar="METRES",
                        help="focus distance for --dof (default: where the camera looks)")
    parser.add_argument("--supersample", type=float, default=None, metavar="SCALE",
                        help="render at SCALE times the window's size and scale down: crisper edges "
                             "and detail at SCALE^2 the cost (default: 2 with --record, 1 otherwise)")
    args = parser.parse_args()
    if args.vertical:
        args.width, args.height = 1080, 1920
    return args


args = parse_args()
ROOT = sk.assets.root()
if ROOT is None:
    print("No asset root found; run from inside the repository or set SHOONYAKASHA_ASSET_DIR.")
    sys.exit(1)
STUDIO = studio.ensure(os.path.join(str(ROOT), "showroom", "studio"))
WHITE = os.path.join(STUDIO, "white.png")
LIGHT_IMAGE = os.path.abspath(args.light_image) if args.light_image else os.path.join(STUDIO, "stained_glass.png")


# ── The engine ───────────────────────────────────────────────────────────

sk.shaders.compile_dir(os.path.join(HERE, "pipeline", "shaders"))
engine = sk.Engine(
    title="Shoonyakasha - Showroom",
    width=args.width, height=args.height,
    hdr_environment_path=args.hdri or os.path.join(STUDIO, "studio.hdr"),
    pipeline_json_path=PIPELINE,
)


class _Scene:
    """engine.scene, which exists once the engine runs."""

    def __getattr__(self, name):
        return getattr(engine.scene, name)


scene = _Scene()


def root_of(entity):
    while True:
        parent = scene.get_parent(entity)
        if parent == sk.NULL_ENTITY:
            return entity
        entity = parent


def roots_of(entities):
    return list(dict.fromkeys(root_of(e) for e in entities))


def holder(name):
    """An empty entity with a transform, to parent things to."""
    entity = scene.create_entity(name)
    if not scene.has_component(entity, "Transform"):
        scene.add_component(entity, "Transform")
    return entity


def by_suffix(entities, name):
    """The loaded entities whose node is `name` (entities are named
    "<prefix>_<node>")."""
    return [e for e in entities if scene.get_name(e).endswith("_" + name)]


# ── Models ───────────────────────────────────────────────────────────────

TURNTABLE_HEIGHT = 0.06


class Model:
    """A model on the turntable: loaded on first show, then hidden and shown."""

    def __init__(self, entry, path, bounds, credit):
        self.entry = entry
        self.path = path
        self.bounds = bounds
        self.credit = credit
        self.loaded = False
        self.entities = []
        self.paint = []
        self.paint_original = {}
        self.coat = {}   # entity -> (clearcoatFactor, clearcoatRoughnessFactor) when coated
        self.fit = sk.NULL_ENTITY
        self.yaw = entry.yaw
        self.size = (1.0, 1.0, 1.0)
        self.scale = 1.0

    def load(self, parent):
        started = time.time()
        result = engine.load_gltf_scene(self.path, max_texture_size=args.texture_size, name_prefix="m",
                                        load_skins=True, load_animations=True)
        if not result.success:
            raise RuntimeError("could not load %s: %s" % (self.path, result.error))
        self.entities = list(result.entities)
        self.fit = holder("model_fit")
        scene.set_parent(self.fit, parent)
        for root in roots_of(self.entities):
            scene.set_parent(root, self.fit)
        for i, _ in enumerate(result.animation_clips):
            for e in self.entities:
                scene.play_animation(e, i)
            break
        if self.entry.paint:
            for e in self.entities:
                material = scene.get_name(e)[2:].rsplit("#", 1)[0]
                if self.entry.paint.search(material):
                    self.paint.append(e)
                    self.paint_original[e] = scene.get_material_vec4(e, "baseColorFactor", (1, 1, 1, 1))
        # Clear coat: what the file declares (KHR_materials_clearcoat), and on
        # body paint that declares none.
        for e in self.entities:
            if scene.has_material_param(e, "clearcoatFactor"):
                self.coat[e] = (scene.get_material_float(e, "clearcoatFactor", 0.0),
                                scene.get_material_float(e, "clearcoatRoughnessFactor", 0.0))
        for e in self.paint:
            self.coat.setdefault(e, PAINT_COAT)
        self.apply_coat(showroom.clearcoat)
        self.loaded = True
        self.place()
        lo, hi = self.bounds
        print("[showroom] %s: %d entities, %d vertices, %.1fs%s; %.2f x %.2f x %.2f in the file, shown at x%.3g" % (
            self.entry.name, len(self.entities), result.total_vertices, time.time() - started,
            (", %d paint" % len(self.paint) if self.paint else "") +
            (", %d clear-coated" % len(self.coat) if self.coat else ""),
            hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], self.scale))

    def apply_coat(self, on):
        for e, (factor, roughness) in self.coat.items():
            scene.set_material_float(e, "clearcoatFactor", factor if on else 0.0)
            scene.set_material_float(e, "clearcoatRoughnessFactor", roughness)

    def place(self):
        """Scale to the catalogue length, turn, centre on the turntable, and
        stand on it (or hover above it)."""
        lo, hi = self.bounds
        extent = [b - a for a, b in zip(lo, hi)]
        auto_turn = 90.0 if extent[0] > extent[2] else 0.0    # the long side along Z
        length = max(extent[0], extent[2]) or 1.0
        self.scale = (self.entry.length / length) if self.entry.length else 1.0
        yaw = self.yaw + auto_turn
        centre = ((lo[0] + hi[0]) / 2.0, lo[1], (lo[2] + hi[2]) / 2.0)
        offset = yaw_point(scale(centre, -self.scale), yaw)
        scene.set_scale(self.fit, (self.scale,) * 3)
        scene.set_rotation(self.fit, (0.0, math.radians(yaw), 0.0))
        scene.set_position(self.fit, add(offset, (0.0, self.base_height(), 0.0)))
        width, length = (extent[2], extent[0]) if auto_turn else (extent[0], extent[2])
        self.size = (width * self.scale, extent[1] * self.scale, length * self.scale)

    def base_height(self):
        return TURNTABLE_HEIGHT + self.entry.plinth + self.entry.hover

    def centre(self):
        return (0.0, self.base_height() + self.size[1] / 2.0, 0.0)

    def radius(self):
        return 0.5 * math.sqrt(sum(s * s for s in self.size))

    def turntable_radius(self):
        if self.entry.plinth:
            return max(0.9, 0.75 * self.entry.plinth)
        return max(1.0, 0.5 * math.hypot(self.size[0], self.size[2]) * 1.06)

    def show(self, visible):
        for e in self.entities:
            scene.set_visible(e, visible)


def find_models():
    """The catalogue's fetched models, then --extra files; the bundled Fox if
    there is nothing else."""
    wanted = args.models.split(",") if args.models else [e.name for e in catalogue.CATALOGUE]
    models = []
    for name in wanted:
        entry = catalogue.BY_NAME.get(name)
        if entry is None:
            print("[showroom] unknown model '%s'; known: %s" % (name, ", ".join(catalogue.BY_NAME)))
            continue
        directory = os.path.join(str(ROOT), "showroom", name)
        path = os.path.join(directory, "model.gltf")
        if not os.path.exists(path):
            continue
        with open(os.path.join(directory, "showroom.json"), encoding="utf-8") as f:
            info = json.load(f)
        models.append(Model(entry, path, (info["boundsMin"], info["boundsMax"]), info["credit"]))
    for extra in args.extra:
        length, _, path = extra.rpartition("=") if "=" in extra and not os.path.exists(extra) else ("", "", extra)
        path = os.path.abspath(path)
        name = os.path.splitext(os.path.basename(path))[0]
        if name in ("model", "scene"):
            name = os.path.basename(os.path.dirname(path))      # a converted model's directory
        entry = catalogue.Entry(name, name.replace("_", " "), "", "car",
                                float(length) if length else None, paint=catalogue.CAR_PAINT)
        info = os.path.join(os.path.dirname(path), "showroom.json")
        if os.path.exists(info):        # converted by tools/sketchfab.py: bounds without stray parts
            with open(info, encoding="utf-8") as f:
                info = json.load(f)
            models.append(Model(entry, path, (info["boundsMin"], info["boundsMax"]), info.get("credit", "")))
        else:
            models.append(Model(entry, path, catalogue.gltf_bounds(path), ""))
    if not models:
        print("[showroom] No showroom models found. Fetch them once with\n"
              "    python tools/fetch_assets.py showroom\n"
              "(needs a Sketchfab API token, see README.md). Showing the bundled Fox meanwhile.")
        path = str(ROOT / "models" / "Fox.glb")
        entry = catalogue.Entry("fox", "Fox", "Khronos glTF sample", "miniature", 1.1, yaw=-90.0, rig="studio")
        models.append(Model(entry, path, catalogue.gltf_bounds(path),
                            '"Fox" by PixelMannen (CC0), rigged by tomkranis (CC BY 4.0), '
                            'from the Khronos glTF Sample Assets'))
    return models


# ── Lights ───────────────────────────────────────────────────────────────

class LightSpec:
    """One light of a rig. `direction` points from the model's centre towards
    the light, in the stage's frame (+Z front, +X right), `distance` in units
    of the model's size; softbox (width, height) in metres draws a panel,
    which with `image` shines with a picture, like a coloured glass window."""

    def __init__(self, direction, distance, color, intensity, radius=0.3, cone=(35.0, 70.0),
                 softbox=None, shadows=True, aim_height=0.0, image=False):
        self.direction = normalize(direction)
        self.distance = distance
        self.color = color
        self.intensity = intensity
        self.radius = radius
        self.cone = cone
        self.softbox = softbox
        self.shadows = shadows
        self.aim_height = aim_height
        self.image = image          # the panel shines with the pipeline's lightImage


class Rig:
    def __init__(self, name, label, lights, sun=None, ibl=0.5, floor=(0.16, 0.16, 0.17), floor_roughness=0.32,
                 cove=(0.5, 0.5, 0.52), rim=(0.0, 0.0, 0.0), exposure=1.0, min_ev=-4.0,
                 turntable=((0.05, 0.05, 0.055), 0.85, 0.28)):
        self.name = name
        self.label = label
        self.lights = lights
        self.sun = sun              # (direction the light travels, colour, intensity) or None
        self.ibl = ibl
        self.floor = floor
        self.floor_roughness = floor_roughness
        self.cove = cove
        self.rim = rim              # the turntable's light strip
        self.exposure = exposure
        self.min_ev = min_ev
        self.turntable = turntable  # top: colour, metallic, roughness


RIGS = [
    Rig("studio", "Studio softboxes", [
        LightSpec((-0.72, 0.6, 0.55), 1.9, (1.0, 0.97, 0.92), 55.0, radius=0.75, cone=(30.0, 62.0), softbox=(2.2, 1.4)),
        LightSpec((0.88, 0.32, 0.36), 2.0, (0.86, 0.92, 1.0), 14.0, radius=0.55, cone=(30.0, 62.0), softbox=(1.6, 1.1)),
        LightSpec((0.05, 0.5, -0.86), 1.9, (1.0, 0.98, 0.95), 45.0, radius=0.5, cone=(30.0, 60.0), softbox=(2.2, 0.8)),
        LightSpec((0.0, 1.0, 0.0), 1.55, (1.0, 1.0, 1.0), 38.0, radius=0.8, cone=(35.0, 62.0), softbox=(1.2, 3.6)),
    ], ibl=0.45, floor=(0.07, 0.07, 0.075), floor_roughness=0.28, cove=(0.3, 0.3, 0.31),
        rim=(0.25, 0.25, 0.28), exposure=0.95),
    Rig("hardlight", "Hard light (virtual shadow map)", [
        LightSpec((0.85, 0.3, 0.4), 2.2, (0.8, 0.88, 1.0), 10.0, radius=0.6, cone=(45.0, 80.0), softbox=(1.6, 1.1)),
    ], sun=((0.55, -0.52, -0.65), (1.0, 0.93, 0.84), 7.0), ibl=0.3, floor=(0.32, 0.31, 0.3),
        floor_roughness=0.6, cove=(0.62, 0.6, 0.58), rim=(0.0, 0.0, 0.0), exposure=0.9,
        turntable=((0.42, 0.42, 0.41), 0.0, 0.7)),
    Rig("night", "Night: spotlights", [
        LightSpec((0.0, 1.0, 0.12), 1.6, (1.0, 0.92, 0.82), 160.0, radius=0.12, cone=(16.0, 24.0)),
        LightSpec((-0.8, 0.75, -0.6), 2.0, (0.45, 0.6, 1.0), 90.0, radius=0.1, cone=(14.0, 22.0)),
        LightSpec((0.8, 0.75, -0.6), 2.0, (0.45, 0.6, 1.0), 90.0, radius=0.1, cone=(14.0, 22.0)),
    ], ibl=0.04, floor=(0.03, 0.03, 0.035), floor_roughness=0.18, cove=(0.05, 0.05, 0.06),
        rim=(0.1, 0.45, 1.6), exposure=1.0, min_ev=-1.5),
    Rig("neon", "Neon", [
        LightSpec((-0.95, 0.22, 0.25), 1.8, (1.0, 0.12, 0.65), 32.0, radius=0.35, cone=(35.0, 60.0), softbox=(0.3, 2.4)),
        LightSpec((0.9, 0.35, -0.45), 1.8, (0.1, 0.85, 1.0), 32.0, radius=0.35, cone=(35.0, 60.0), softbox=(0.3, 2.4)),
        LightSpec((0.0, 1.0, -0.2), 1.6, (0.55, 0.3, 1.0), 16.0, radius=0.6, cone=(40.0, 70.0), softbox=(1.0, 3.0)),
    ], ibl=0.03, floor=(0.02, 0.02, 0.025), floor_roughness=0.14, cove=(0.03, 0.025, 0.04),
        rim=(1.4, 0.1, 0.9), exposure=0.8, min_ev=0.0),
    Rig("window", "Stained glass", [
        LightSpec((-0.9, 0.45, -0.35), 2.0, (1.0, 1.0, 1.0), 90.0, radius=0.6, cone=(40.0, 55.0),
                  softbox=(1.5, 3.0), image=True),
        LightSpec((0.7, 0.6, 0.6), 2.2, (0.75, 0.8, 1.0), 6.0, radius=0.8, cone=(40.0, 70.0), softbox=(1.4, 1.0)),
    ], ibl=0.05, floor=(0.02, 0.02, 0.022), floor_roughness=0.05, cove=(0.05, 0.045, 0.04),
        rim=(0.0, 0.0, 0.0), exposure=1.0, min_ev=1.5),
]
RIG_BY_NAME = {r.name: r for r in RIGS}
MAX_LIGHTS = max(len(r.lights) for r in RIGS)


class Stage:
    """The cove, the turntable, the lights and their softboxes."""

    def __init__(self):
        self.spin = sk.NULL_ENTITY
        self.angle = 0.0
        self.lights = []
        self.softboxes = []     # (holder, diffuser entities)
        self.sun = sk.NULL_ENTITY
        self.rig = None
        self.diffuser = args.softbox
        self.size = 1.0
        # The settings panel's levels, over every rig's own.
        self.brightness = 1.0       # the lights, their softboxes' glow and the sun
        self.environment = 1.0      # the image-based light
        self.exposure_ev = 0.0      # stops over the rig's exposure

    def build(self):
        self.spin = holder("turntable_spin")
        result = engine.load_gltf_scene(os.path.join(STUDIO, "stage.gltf"), name_prefix="stage")
        self.stage_root = holder("stage")
        for root in roots_of(result.entities):
            scene.set_parent(root, self.stage_root)
        self.stage_scale = 1.0
        self.floor = by_suffix(result.entities, "floor")
        self.cove = by_suffix(result.entities, "cove")
        for e in result.entities:
            scene.set_cast_shadows(e, False)       # receivers only: the sun shines through the cove
        result = engine.load_gltf_scene(os.path.join(STUDIO, "turntable.gltf"), name_prefix="turntable")
        self.turntable = holder("turntable")
        scene.set_parent(self.turntable, self.spin)
        for root in roots_of(result.entities):
            scene.set_parent(root, self.turntable)
        self.rim = by_suffix(result.entities, "turntable_rim")
        self.top = by_suffix(result.entities, "turntable_top")
        result = engine.load_gltf_scene(os.path.join(STUDIO, "plinth.gltf"), name_prefix="plinth")
        self.plinth = holder("plinth")
        scene.set_parent(self.plinth, self.spin)
        for root in roots_of(result.entities):
            scene.set_parent(root, self.plinth)
        self.plinth_entities = result.entities

        for i in range(MAX_LIGHTS):
            light = engine.create_point_light(pos=(0.0, 4.0, 0.0), color=(1.0, 1.0, 1.0), intensity=0.0, range=20.0)
            scene.set_light_type(light, sk.LIGHT_SPOT)
            self.lights.append(light)
            box = holder("softbox%d" % i)
            parts = engine.load_gltf_scene(os.path.join(STUDIO, "softbox.gltf"), name_prefix="softbox%d" % i)
            for root in roots_of(parts.entities):
                scene.set_parent(root, box)
            for e in parts.entities:
                scene.set_cast_shadows(e, False)   # the light shines from inside it
            self.softboxes.append((box, parts.entities, by_suffix(parts.entities, "softbox_diffuser")))
        self.sun = engine.create_directional_light(direction=(0.4, -0.7, -0.5), intensity=0.0)

    def stand(self, model):
        """Size the turntable and plinth for a model, and the cove so the
        farthest camera of any shot stays inside it."""
        aspect = args.width / args.height
        farthest = max(framing_distance(model.radius(), 24.0, aspect) * 1.15,
                       framing_distance(model.radius(), 40.0, aspect) * 1.3)
        self.stage_scale = max(1.0, (farthest + 1.5) / studio.FLOOR_RADIUS)
        scene.set_scale(self.stage_root, (self.stage_scale,) * 3)
        r = model.turntable_radius()
        scene.set_scale(self.turntable, (r, 1.0, r))
        plinth = model.entry.plinth
        for e in self.plinth_entities:
            scene.set_visible(e, plinth > 0.0)
        scene.set_position(self.plinth, (0.0, TURNTABLE_HEIGHT, 0.0))
        scene.set_scale(self.plinth, (r * 1.1, max(plinth, 0.01), r * 1.1))

    def light(self, rig, model):
        """Place and switch the rig's lights around a model; lights scale
        with it, so a bomber is lit like a coupe."""
        self.rig = rig
        size = self.size = max(model.radius() / 2.3, 0.45)
        target = model.centre()
        for i, light in enumerate(self.lights):
            box, parts, diffusers = self.softboxes[i]
            if i >= len(rig.lights):
                scene.set_light_intensity(light, 0.0)
                scene.set_light_cast_shadows(light, False)
                for e in parts:
                    scene.set_visible(e, False)
                continue
            spec = rig.lights[i]
            distance = size * (spec.distance * 2.3 + 1.2)
            position = add(target, scale(spec.direction, distance))
            aim = add(target, (0.0, spec.aim_height * size, 0.0))
            rotation = look_rotation(position, aim)
            scene.set_position(light, position)
            scene.set_rotation(light, rotation)
            scene.set_light_color(light, spec.color)
            scene.set_light_intensity(light, spec.intensity * size * size * self.brightness)
            scene.set_light_range(light, distance * 2.5)
            scene.set_light_cone(light, spec.cone[0], spec.cone[1])
            scene.set_light_source_radius(light, spec.radius * size)
            scene.set_light_cast_shadows(light, spec.shadows)
            # A softbox lights as its panel: the pipeline shades the
            # rectangle, the sphere stays for showroom.areaLights = 0.
            panel = (spec.softbox[0] * size, spec.softbox[1] * size) if spec.softbox else (0.0, 0.0)
            scene.set_light_source_size(light, *panel)
            scene.set_light_source_image(light, 1 if spec.image else 0)
            visible = spec.softbox is not None
            for e in parts:
                scene.set_visible(e, visible)
            if visible:
                w, h = spec.softbox
                scene.set_position(box, position)
                scene.set_rotation(box, rotation)
                scene.set_scale(box, (w * size, h * size, size))
                for e in diffusers:
                    scene.set_material_vec4(e, "emissiveFactor", self.diffuser_emission(spec))
                    scene.set_material_texture(e, "emissiveMap", LIGHT_IMAGE if spec.image else WHITE)
        if rig.sun:
            direction, color, intensity = rig.sun
            scene.set_rotation(self.sun, rotation_facing(direction))
            scene.set_light_color(self.sun, color)
            scene.set_light_intensity(self.sun, intensity * self.brightness)
            scene.set_light_cast_shadows(self.sun, True)
        else:
            scene.set_light_intensity(self.sun, 0.0)
            scene.set_light_cast_shadows(self.sun, False)
        for e in self.floor:
            scene.set_material_vec4(e, "baseColorFactor", rig.floor + (1.0,))
            scene.set_material_float(e, "roughnessFactor", rig.floor_roughness)
        for e in self.cove:
            scene.set_material_vec4(e, "baseColorFactor", rig.cove + (1.0,))
        for e in self.rim:
            scene.set_material_vec4(e, "emissiveFactor", rig.rim + (0.0,))
        color, metallic, roughness = rig.turntable
        for e in self.top:
            scene.set_material_vec4(e, "baseColorFactor", color + (1.0,))
            scene.set_material_float(e, "metallicFactor", metallic)
            scene.set_material_float(e, "roughnessFactor", roughness)
        self.set_environment(self.environment)
        self.set_exposure(self.exposure_ev)
        engine.set_custom_float("default.exposureMinEV", rig.min_ev)

    def diffuser_emission(self, spec):
        """emissiveFactor of a softbox's diffuser."""
        # A picture keeps its colours deep: it glows less than a diffuser,
        # which is white and may burn out.
        glow = scale(spec.color, (5.0 if spec.image else 3.0 + spec.intensity / 15.0) * self.brightness)
        # emissiveFactor.a: 2 + the grid's cells marks a diffuser for the
        # pipeline's softbox pattern, 1 is an ordinary emitter.
        grid = DIFFUSERS[self.diffuser][0]
        if spec.image:
            grid = -1               # the picture, not a diffuser's pattern
        return glow + (2.0 + grid if grid >= 0 else 1.0,)

    def set_brightness(self, brightness):
        """Scale the rig's lights, their softboxes' glow and the sun."""
        self.brightness = brightness
        for light, spec, (_, _, diffusers) in zip(self.lights, self.rig.lights, self.softboxes):
            scene.set_light_intensity(light, spec.intensity * self.size * self.size * brightness)
            if spec.softbox is not None:
                for e in diffusers:
                    scene.set_material_vec4(e, "emissiveFactor", self.diffuser_emission(spec))
        if self.rig.sun:
            scene.set_light_intensity(self.sun, self.rig.sun[2] * brightness)

    def set_environment(self, environment):
        self.environment = environment
        engine.set_custom_float("default.iblIntensity", self.rig.ibl * environment)

    def set_exposure(self, ev):
        self.exposure_ev = ev
        engine.set_custom_float("default.exposure", self.rig.exposure * 2.0 ** ev)

    def turn(self, degrees):
        self.angle = (self.angle + degrees) % 360.0
        scene.set_rotation(self.spin, (0.0, math.radians(self.angle), 0.0))


# ── Cameras ──────────────────────────────────────────────────────────────

class Shot:
    def __init__(self, name, seconds, pose):
        self.name = name
        self.seconds = seconds
        self.pose = pose            # u in [0, 1] -> (eye, target, fov)


def framing_distance(radius, fov, aspect, margin=0.92):
    """Camera distance at which a sphere of `radius` fits the view (the
    margin is under 1: a car fills far less of its bounding sphere)."""
    v = math.radians(fov) / 2.0
    h = math.atan(math.tan(v) * aspect)
    return radius * margin / math.sin(min(v, h))


def cinematic_shots(model, aspect):
    """Shots around a model whose nose points to +Z."""
    w, h, l = (s / 2.0 for s in model.size)
    base = model.base_height()
    c = model.centre()
    r = model.radius()
    ship = model.entry.kind in ("ship", "miniature")
    d = framing_distance(r, 40.0, aspect)

    def front_three_quarter(u):
        u = smoothstep(u)
        eye = orbit(c, lerp(d * 1.25, d * 0.95, u), lerp(42.0, 30.0, u), lerp(9.0, 6.0, u))
        return eye, add(c, (0.0, -h * 0.15, 0.0)), 40.0

    def side_track(u):
        u = smoothstep(u)
        x = w + max(r * 1.25, 1.6)
        eye = (x, base + h * 0.45, lerp(l * 0.9, -l * 0.9, u))
        target = (0.0, base + h * 0.4, lerp(l * 0.55, -l * 0.55, u))
        return eye, target, 45.0

    def crane(u):
        u = smoothstep(u)
        eye = orbit(c, d * 1.1, lerp(150.0, 230.0, u), lerp(58.0, 34.0, u))
        return eye, c, 40.0

    def detail(u):
        u = smoothstep(u)
        # A front corner: a wheel on a car, the nose or a wingtip on a ship.
        target = (w * 0.75, base + h * (0.45 if ship else 0.22), l * 0.62)
        away = normalize((0.9, 0.25, 0.7))
        eye = add(target, scale(away, lerp(r * 0.75, r * 0.45, u)))
        return eye, target, 38.0

    def rear_pull(u):
        u = smoothstep(u)
        eye = orbit(c, lerp(d * 0.8, d * 1.2, u), lerp(205.0, 215.0, u), lerp(5.0, 12.0, u))
        return eye, c, 40.0

    def hero(u):
        u = smoothstep(u)
        d_tele = framing_distance(r, 24.0, aspect)
        eye = orbit(add(c, (0.0, -h * 0.25, 0.0)), lerp(d_tele * 0.95, d_tele * 1.1, u), lerp(-8.0, 8.0, u), 3.0)
        return eye, add(c, (0.0, -h * 0.1, 0.0)), 24.0

    return [Shot("front three-quarter", 6.0, front_three_quarter), Shot("side", 7.0, side_track),
            Shot("crane", 6.0, crane), Shot("detail", 6.0, detail), Shot("rear", 6.0, rear_pull),
            Shot("hero", 6.0, hero)]


def still_poses(model, aspect):
    """Fixed views for stills: (name, eye, target, fov)."""
    c = model.centre()
    r = model.radius()
    d = framing_distance(r, 40.0, aspect)
    d_tele = framing_distance(r, 24.0, aspect)
    h = model.size[1]
    return [
        ("front34", orbit(c, d, 35.0, 8.0), add(c, (0.0, -h * 0.12, 0.0)), 40.0),
        ("side", orbit(c, d * 1.05, 90.0, 4.0), c, 40.0),
        ("rear34", orbit(c, d, 215.0, 10.0), c, 40.0),
        ("hero", orbit(add(c, (0.0, -h * 0.25, 0.0)), d_tele, 0.0, 2.0), add(c, (0.0, -h * 0.1, 0.0)), 24.0),
        ("top", orbit(c, d * 1.05, 120.0, 55.0), c, 40.0),
        ("detail",) + cinematic_shots(model, aspect)[3].pose(0.6),
    ]


# ── Overlay ──────────────────────────────────────────────────────────────

class Overlay:
    """The model's name and credit at the bottom, the showroom's state at the
    top right, help on F1."""

    def __init__(self):
        self.visible = not args.no_overlay
        self.help_visible = False

    def build(self):
        k = args.height / 1080.0 if not args.vertical else args.width / 1080.0
        self.k = k
        bottom = sk.UI_ANCHOR_BOTTOM_LEFT
        self.panel = engine.create_ui_panel(sk.UI_ANCHOR_BOTTOM_CENTER, (0.0, -78.0 * k),
                                            (args.width * 2.0, 156.0 * k), color=(0.0, 0.0, 0.0, 0.45))
        scene.set_sort_key(self.panel, 0)
        self.title = engine.create_text("", bottom, (48.0 * k, -96.0 * k), FONT, font_size=44.0 * k)
        self.subtitle = engine.create_text("", bottom, (50.0 * k, -64.0 * k), FONT, font_size=22.0 * k,
                                           color=(0.85, 0.85, 0.85, 1.0))
        self.credit = engine.create_text("", bottom, (50.0 * k, -30.0 * k), FONT, font_size=17.0 * k,
                                         color=(0.75, 0.75, 0.75, 1.0))
        self.status = engine.create_text("", sk.UI_ANCHOR_TOP_LEFT, (28.0 * k, 34.0 * k), FONT,
                                         font_size=17.0 * k, color=(0.8, 0.8, 0.8, 0.9))
        lines = [line.strip() for line in __doc__.split("Keys:")[1].split("Automatic")[0].strip().splitlines()]
        self.help = []
        for i, line in enumerate(lines):
            label = engine.create_text(catalogue.ascii_text(line), sk.UI_ANCHOR_TOP_LEFT,
                                       (28.0 * k, (80.0 + 24.0 * i) * k), FONT, font_size=18.0 * k)
            self.help.append(label)
        for label in [self.title, self.subtitle, self.credit, self.status] + self.help:
            scene.set_text_sort_key(label, 1)
        self.apply()

    def show_model(self, model):
        e = model.entry
        scene.set_text(self.title, catalogue.ascii_text(e.title.upper()))
        scene.set_text(self.subtitle, catalogue.ascii_text(e.year))
        scene.set_text(self.credit, "Model: " + catalogue.short_credit(model.credit) if model.credit else "")

    def set_status(self, text):
        scene.set_text(self.status, catalogue.ascii_text(text))

    def apply(self):
        scene.set_visible(self.panel, self.visible)
        for label in (self.title, self.subtitle, self.credit, self.status):
            scene.set_text_visible(label, self.visible)
        for label in self.help:
            scene.set_text_visible(label, self.help_visible)


# ── Render stats ─────────────────────────────────────────────────────────

class StatsPanel:
    """Frame rate, CPU and GPU time, draw calls and the most expensive passes
    at the top right, on O. The engine collects them only while it is shown."""

    ROWS = 10
    REFRESH = 0.25   # seconds; the engine's figures cover the last whole second

    def __init__(self):
        self.visible = False
        self.wait = 0.0

    def build(self, k):
        anchor = sk.UI_ANCHOR_TOP_RIGHT
        margin, width, line = 16.0 * k, 470.0 * k, 22.0 * k
        lines = 4 + self.ROWS
        height = 22.0 * k + lines * line
        self.panel = engine.create_ui_panel(anchor, (-margin - width / 2.0, margin + height / 2.0),
                                            (width, height), color=(0.0, 0.0, 0.0, 0.6))
        scene.set_sort_key(self.panel, 0)
        left = -margin - width + 16.0 * k
        right = -margin - 16.0 * k
        time_column = right - 90.0 * k
        y = lambda i: margin + 30.0 * k + i * line

        def label(x, i, size=17.0, color=(0.9, 0.9, 0.9, 1.0), align=None):
            text = engine.create_text("", anchor, (x, y(i)), FONT, font_size=size * k, color=color)
            if align is not None:
                scene.set_text_align(text, align)
            scene.set_text_sort_key(text, 1)
            return text

        self.header = [label(left, 0, 20.0, (1.0, 1.0, 1.0, 1.0)), label(left, 1), label(left, 2)]
        grey = (0.6, 0.6, 0.6, 1.0)
        self.columns = (label(left, 3, 15.0, grey), label(time_column, 3, 15.0, grey, sk.TEXT_ALIGN_RIGHT),
                        label(right, 3, 15.0, grey, sk.TEXT_ALIGN_RIGHT))
        self.rows = [(label(left, 4 + i, 16.0), label(time_column, 4 + i, 16.0, align=sk.TEXT_ALIGN_RIGHT),
                      label(right, 4 + i, 16.0, align=sk.TEXT_ALIGN_RIGHT)) for i in range(self.ROWS)]
        self.apply()

    def labels(self):
        return self.header + list(self.columns) + [t for row in self.rows for t in row]

    def toggle(self):
        self.visible = not self.visible
        if self.visible:
            engine.enable_render_stats()
            self.wait = 0.0
        else:
            engine.disable_render_stats()
        self.apply()

    def apply(self):
        scene.set_visible(self.panel, self.visible)
        for text in self.labels():
            scene.set_text_visible(text, self.visible)
        if self.visible:
            for text in self.labels():
                scene.set_text(text, "")
            scene.set_text(self.header[0], "Collecting render stats ...")

    def update(self, dt):
        if not self.visible:
            return
        self.wait -= dt
        if self.wait > 0.0:
            return
        self.wait = self.REFRESH
        stats = engine.render_stats
        if not stats or stats["fps"] == 0.0:
            return

        gpu = stats["gpu_ms"] is not None
        scene.set_text(self.header[0], "%.0f FPS   %.2f ms   (worst %.2f ms)"
                       % (stats["fps"], stats["frame_time_ms"], stats["frame_time_max_ms"]))
        scene.set_text(self.header[1], "GPU %s   CPU record %.2f ms%s"
                       % ("%.2f ms" % stats["gpu_ms"] if gpu else "n/a", stats["cpu_record_ms"],
                          "   (validation layers on)" if stats["validation_layers"] else ""))
        scene.set_text(self.header[2], "%d draws   %d dispatches   %s vertices"
                       % (stats["draw_calls"], stats["dispatches"], count(stats["vertices"])))

        cost = "gpu_ms" if gpu else "cpu_ms"
        scene.set_text(self.columns[0], "pass")
        scene.set_text(self.columns[1], "GPU ms" if gpu else "CPU ms")
        scene.set_text(self.columns[2], "draws")
        groups = group_passes(stats["passes"], cost)
        for i, row in enumerate(self.rows):
            if i < len(groups):
                name, n, ms, draws = groups[i]
                scene.set_text(row[0], name if n == 1 else "%s  x%d" % (name, n))
                scene.set_text(row[1], "%.3f" % ms)
                scene.set_text(row[2], str(draws))
            else:
                for text in row:
                    scene.set_text(text, "")


def group_passes(passes, cost):
    """The instances of a repeated pass (Shadow0, Shadow1, ...) as one row,
    most expensive first: (name, instances, ms, draws)."""
    groups = {}
    for p in passes:
        name = p["name"].rstrip("0123456789") or p["name"]
        n, ms, draws = groups.get(name, (0, 0.0, 0))
        groups[name] = (n + 1, ms + (p[cost] or 0.0), draws + p["draw_calls"])
    return sorted(((name,) + g for name, g in groups.items()), key=lambda g: -g[2])


def count(n):
    return "%.1fM" % (n / 1e6) if n >= 1e6 else "%.1fk" % (n / 1e3) if n >= 1e4 else str(n)


# ── Canvas UI: the settings panel and the info stand ─────────────────────

INK = (0.92, 0.93, 0.95, 1.0)
MUTED = (0.62, 0.66, 0.72, 1.0)
GOLD = (0.95, 0.85, 0.55, 1.0)
FRAME = (0.07, 0.075, 0.09, 0.9)
SURFACE = (0.22, 0.24, 0.30, 1.0)
ACCENT = (0.30, 0.52, 0.85, 1.0)


def srgb(linear):
    """A linear colour (a material's factor) as the sRGB the UI takes."""
    return tuple(12.92 * c if c <= 0.0031308 else 1.055 * c ** (1.0 / 2.4) - 0.055 for c in linear[:3]) + (1.0,)


class Swatches:
    """A row of paint swatches: buttons in the paints' colours, the first,
    labelled "Own", for the model's own paint. The chosen one is framed in
    white."""

    def __init__(self, parent, size, spacing):
        ui = engine.ui
        self.row = ui.create_horizontal_layout(parent, size=(0, size), spacing=spacing)
        ui.set_layout_alignment(self.row, (0, 0.5))
        self.frames, self.buttons = [], []
        for i in range(len(PAINTS)):
            frame = ui.create_panel(self.row, size=(size, size))
            ui.set_raycast_target(frame, False)
            button = ui.create_button(frame, "" if PAINTS[i] else "Own", size=(size, size))
            ui.set_rect(button, anchor_min=(0, 0), anchor_max=(1, 1), size=(-8, -8))
            ui.set_font_size(button, size * 0.34)
            if PAINTS[i]:
                ui.set_color(button, srgb(PAINTS[i]))
            ui.set_accessible(button, "Original paint" if i == 0 else "Paint %d" % i)
            self.frames.append(frame)
            self.buttons.append(button)

    def show_model(self, model):
        for button in self.buttons:
            engine.ui.set_interactable(button, bool(model.paint))

    def clicked(self):
        """The index of the swatch clicked this frame, or None."""
        for i, button in enumerate(self.buttons):
            if engine.ui.was_clicked(button):
                return i
        return None

    def sync(self, index):
        for i, frame in enumerate(self.frames):
            engine.ui.set_color(frame, (1, 1, 1, 0.9) if i == index else (1, 1, 1, 0.12))


class SettingsPanel:
    """A screen canvas at the left: everything the keys change, as buttons,
    toggles and sliders in a scroll view. Each frame it reads its controls
    first, then shows the showroom's state, so changes made with the keys
    appear on it too."""

    WIDTH = 430

    def __init__(self):
        self.visible = True
        self.built = False
        self.sliders = {}           # slider -> (its label, name, format of its value)
        self.texts = {}             # element -> the text last set, to set only changes

    def build(self, fonts):
        ui = engine.ui
        self.built = True
        reference = (1080, 1920) if args.vertical else (1920, 1080)
        self.canvas = ui.create_canvas(reference_size=reference)
        # Between the status line at the top and the credits at the bottom.
        top, bottom = 72.0, 176.0
        self.frame = ui.create_panel(self.canvas, color=FRAME)
        ui.set_rect(self.frame, anchor_min=(0, 0), anchor_max=(0, 1), pivot=(0, 0.5),
                    position=(24, (top - bottom) / 2.0), size=(self.WIDTH, -(top + bottom)))
        column = ui.create_vertical_layout(self.frame, spacing=6, padding=(20, 14, 12, 14))
        ui.set_rect(column, anchor_min=(0, 0), anchor_max=(1, 1), size=(0, 0))
        ui.set_layout_child_sizing(column, expand_width=True)
        ui.create_text(column, "Showroom", size=30, color=GOLD, font=fonts["serif"])
        ui.create_text(column, "F2 hides this panel. Once a control has focus, Tab and the arrow keys "
                               "move between them; Escape lets go.", size=15, color=MUTED)
        view = ui.create_scroll_view(column, size=(self.WIDTH - 32, 0))   # less the column's padding
        ui.set_layout_element(view, flexible=(-1, 1))
        ui.set_accessible(view, "Showroom settings")
        page = ui.create_vertical_layout(ui.scroll_content(view), spacing=8, padding=(0, 4, 18, 12))
        ui.set_layout_child_sizing(page, expand_width=True)
        self.page = page

        self.section("Model")
        self.model_buttons = self.choices({"previous": "Previous", "next": "Next", "turn": "Turn 90°"}, 3)
        self.swatches = Swatches(page, 40, 6)
        self.clearcoat = ui.create_toggle(page, "Clear coat over the paint", size=(0, 28))
        self.spin = ui.create_toggle(page, "Turntable turning", size=(0, 28))

        self.section("Lighting")
        self.rigs = self.choices({"studio": "Studio", "hardlight": "Hard light", "night": "Night",
                                  "neon": "Neon", "window": "Stained glass"}, 3)
        self.brightness = self.slider("Lights", 0.0, 2.0, 1.0, "x%.2f")
        self.environment = self.slider("Environment light", 0.0, 2.0, 1.0, "x%.2f")
        self.exposure = self.slider("Exposure", -3.0, 3.0, 0.0, "%+.1f EV")
        self.area_lights = ui.create_toggle(page, "Softboxes lit as rectangles", size=(0, 28))
        ui.create_text(page, "Softbox diffusers", size=16, color=MUTED)
        self.diffusers = self.choices({"grid": "Grid", "fabric": "Plain fabric", "even": "Even"}, 3)
        self.floor = ui.create_toggle(page, "White floor", size=(0, 28))

        self.section("Shadows and reflections")
        names = {"hybrid": "VSM + traced lights", "raytraced": "All ray traced", "high": "Virtual shadow map",
                 "cascades": "Cascades"}
        self.shadows = self.choices({p: names[p] for p in showroom.shadow_presets()}, 2)
        self.reflections = ui.create_toggle(page, "Ray-traced reflections", size=(0, 28))

        self.section("Camera and lens")
        self.cameras = self.choices(CAMERA_MODES, 4)
        self.dof = ui.create_toggle(page, "Depth of field", size=(0, 28))
        self.f_number = self.slider("Aperture", 0.8, 8.0, 2.8, "f/%.1f")
        ui.create_text(page, "Tone mapper", size=16, color=MUTED)
        self.tonemappers = self.choices({k: v[1] for k, v in TONE_MAPPERS.items()}, 3)
        self.bloom = self.slider("Bloom", 0.0, 0.3, 0.05, "%.3f")
        self.vignette = self.slider("Vignette", 0.0, 0.5, args.vignette, "%.2f")
        self.grain = self.slider("Film grain", 0.0, 0.08, args.grain, "%.3f")

        self.section("On screen and in the scene")
        self.stand = ui.create_toggle(page, "Info stand beside the turntable", is_on=True, size=(0, 28))
        self.credits = ui.create_toggle(page, "Credits and status", size=(0, 28))
        self.stats = ui.create_toggle(page, "Render stats", size=(0, 28))
        self.apply()

    def show_model(self, model):
        if self.built:
            self.swatches.show_model(model)

    def section(self, title):
        ui = engine.ui
        ui.create_element(self.page, (0, 6))
        ui.create_text(self.page, title, size=20, color=GOLD)

    def choices(self, labels, per_row):
        """Buttons for {key: label}, per_row to a row, sharing its width."""
        ui = engine.ui
        buttons = {}
        for i, (key, label) in enumerate(labels.items()):
            if i % per_row == 0:
                row = ui.create_horizontal_layout(self.page, size=(0, 34), spacing=6)
                ui.set_layout_child_sizing(row, expand_width=True)
            buttons[key] = ui.create_button(row, label, size=(60, 34))
            ui.set_font_size(buttons[key], 16)
        return buttons

    def slider(self, name, lo, hi, value, value_format):
        """A label over a slider; returns the slider. The label shows its
        name and its value in value_format."""
        label = engine.ui.create_text(self.page, "", size=17)
        slider = engine.ui.create_slider(self.page, min=lo, max=hi, value=value, size=(0, 22))
        engine.ui.set_accessible(slider, name)
        self.sliders[slider] = (label, name, value_format)
        return slider

    def apply(self):
        if self.built:
            engine.ui.set_visible(self.canvas, self.visible and not showroom.overlay.help_visible)

    def set_text(self, element, text):
        if self.texts.get(element) != text:
            self.texts[element] = text
            engine.ui.set_text(element, text)

    def update(self):
        if not self.built:
            return
        ui, s = engine.ui, showroom
        clicked = lambda buttons: next((k for k, b in buttons.items() if ui.was_clicked(b)), None)
        changed = ui.value_changed

        # The controls, as of last frame's pointer and keys.
        action = clicked(self.model_buttons)
        if action == "previous":
            s.select(s.index - 1)
        elif action == "next":
            s.select(s.index + 1)
        elif action == "turn":
            s.turn_model()
        paint = self.swatches.clicked()
        if paint is not None:
            s.set_paint(paint)
        if changed(self.clearcoat):
            s.set_clearcoat(ui.toggle_value(self.clearcoat))
        if changed(self.spin):
            s.spinning = ui.toggle_value(self.spin)
        rig = clicked(self.rigs)
        if rig:
            s.set_rig(RIG_BY_NAME[rig])
        if changed(self.brightness):
            s.stage.set_brightness(ui.slider_value(self.brightness))
        if changed(self.environment):
            s.stage.set_environment(ui.slider_value(self.environment))
        if changed(self.exposure):
            s.stage.set_exposure(ui.slider_value(self.exposure))
        if changed(self.area_lights):
            s.set_area_lights(ui.toggle_value(self.area_lights))
        diffuser = clicked(self.diffusers)
        if diffuser:
            s.set_diffuser(diffuser)
        if changed(self.floor):
            s.floor_white = ui.toggle_value(self.floor)
            s.apply_floor()
        preset = clicked(self.shadows)
        if preset:
            s.set_shadows(preset)
        if changed(self.reflections):
            s.set_reflections(ui.toggle_value(self.reflections))
        camera = clicked(self.cameras)
        if camera:
            s.set_camera(camera)
        if changed(self.dof) or (changed(self.f_number) and s.dof > 0.0):
            s.set_dof(ui.slider_value(self.f_number) if ui.toggle_value(self.dof) else 0.0)
        tonemapper = clicked(self.tonemappers)
        if tonemapper:
            s.set_tonemapper(tonemapper)
        if changed(self.bloom):
            engine.set_custom_float("default.bloomIntensity", ui.slider_value(self.bloom))
        if changed(self.vignette):
            engine.set_custom_float("showroom.vignette", ui.slider_value(self.vignette))
        if changed(self.grain):
            engine.set_custom_float("showroom.grain", ui.slider_value(self.grain))
        if changed(self.stand):
            s.info_stand.set_shown(ui.toggle_value(self.stand))
        if changed(self.credits):
            s.overlay.visible = ui.toggle_value(self.credits)
            s.overlay.apply()
        if changed(self.stats):
            s.stats.toggle()

        # The showroom's state, whichever way it was changed.
        self.swatches.sync(s.paint)
        for toggle, on in ((self.clearcoat, s.clearcoat), (self.spin, s.spinning),
                           (self.area_lights, s.area_lights), (self.floor, s.floor_white),
                           (self.reflections, s.reflections), (self.dof, s.dof > 0.0),
                           (self.credits, s.overlay.visible), (self.stats, s.stats.visible)):
            if ui.toggle_value(toggle) != on:
                ui.set_toggle_value(toggle, on)
        for slider, value in ((self.brightness, s.stage.brightness), (self.environment, s.stage.environment),
                              (self.exposure, s.stage.exposure_ev), (self.f_number, s.dof)):
            if value > 0.0 or slider is not self.f_number:
                if abs(ui.slider_value(slider) - value) > 1e-4:
                    ui.set_slider_value(slider, value)
        for buttons, chosen in ((self.rigs, s.rig.name), (self.diffusers, s.stage.diffuser),
                                (self.shadows, s.preset), (self.cameras, s.camera_mode),
                                (self.tonemappers, s.tonemapper)):
            for key, button in buttons.items():
                ui.set_color(button, ACCENT if key == chosen else SURFACE)
        # Reflections are traced only with the ray-traced lights' presets.
        ui.set_interactable(self.reflections, s.preset in ("hybrid", "raytraced"))
        for slider, (label, name, value_format) in self.sliders.items():
            self.set_text(label, "%s   %s" % (name, value_format % ui.slider_value(slider)))


class InfoStand:
    """A placard on a post beside the turntable: a world canvas with the
    model's name, length and credit, and buttons for the model, its paint
    and the turntable. It stands at the right of the turntable camera's view,
    turned towards it, and grows with the model like the lights. The model
    stops the pointer where it is in front of the stand (the pipeline's "ui"
    block), and the placard keeps its brightness under every lighting's
    exposure (exposureCompensation, which world canvases set)."""

    PIXELS = (960, 640)
    SIZE = (1.5, 1.0)       # metres, at a model scale of 1
    HEIGHT = 1.25           # the placard's centre above the floor

    def __init__(self):
        self.built = False
        self.shown = True

    def build(self, fonts):
        ui = engine.ui
        self.built = True
        self.root = holder("info_stand")
        mount = holder("info_stand_mount")
        scene.set_parent(mount, self.root)
        scene.set_position(mount, (0.0, self.HEIGHT, 0.0))
        scene.set_rotation(mount, (-0.2, 0.0, 0.0))      # leaning back, towards a camera above it
        self.canvas = ui.create_world_canvas(pixel_size=self.PIXELS, world_size=self.SIZE, emission=1.4)
        scene.set_parent(self.canvas, mount)
        ui.set_canvas_clear_color(self.canvas, (0.025, 0.028, 0.035, 1.0))
        metal = ((0.04, 0.04, 0.045), 0.8, 0.35)
        self.parts = (self.box(mount, (0.0, 0.0, -0.03), (self.SIZE[0] + 0.06, self.SIZE[1] + 0.06, 0.04), metal)
                      + self.box(self.root, (0.0, self.HEIGHT / 2.0, -0.08), (0.06, self.HEIGHT, 0.06), metal)
                      + self.box(self.root, (0.0, 0.012, -0.08), (0.5, 0.024, 0.36), metal))

        # A column from the top: a title of two lines moves the rest down.
        # The buttons are placed from the bottom corners.
        column = ui.create_vertical_layout(self.canvas, spacing=6)
        ui.set_rect(column, anchor_min=(0, 0), anchor_max=(1, 0), pivot=(0.5, 0), position=(0, 36),
                    size=(-96, 400))
        ui.set_layout_child_sizing(column, expand_width=True)
        self.year = ui.create_text(column, "", size=34, color=MUTED)
        self.title = ui.create_text(column, "", size=72, color=GOLD, font=fonts["serif"])
        self.length = ui.create_text(column, "", size=34)
        self.credit = ui.create_text(column, "", size=24, color=MUTED)
        ui.create_element(column, (0, 10))
        self.swatches = Swatches(column, 76, 16)
        self.previous = ui.create_button(self.canvas, "Previous", size=(250, 88))
        self.next = ui.create_button(self.canvas, "Next", size=(250, 88))
        self.spin = ui.create_toggle(self.canvas, "Turntable", is_on=True, size=(320, 52))
        for element, x in ((self.previous, 48), (self.next, 314)):
            ui.set_anchor(element, (0, 1))
            ui.set_position(element, (x, -36))
            ui.set_font_size(element, 36)
        ui.set_anchor(self.spin, (1, 1))
        ui.set_position(self.spin, (-48, -54))
        ui.set_font_size(self.spin, 34)

    @staticmethod
    def box(parent, position, size, material):
        """The unit Box.gltf, scaled to `size` at `position` under parent."""
        meshes = engine.load_gltf_scene(str(ROOT / "models" / "Box.gltf"), name_prefix="info_stand").entities
        root = root_of(meshes[0])
        scene.set_parent(root, parent)
        scene.set_rotation(root, (0.0, 0.0, 0.0))     # the file turns the cube about X
        scene.set_scale(root, size)
        scene.set_position(root, position)
        color, metallic, roughness = material
        for e in meshes:
            scene.set_material_vec4(e, "baseColorFactor", color + (1.0,))
            scene.set_material_float(e, "metallicFactor", metallic)
            scene.set_material_float(e, "roughnessFactor", roughness)
        return list(meshes)

    def show_model(self, model):
        """Fill the placard in, and stand it beside the model's turntable."""
        if not self.built:
            return
        ui, e = engine.ui, model.entry
        ui.set_text(self.year, e.year)
        ui.set_text(self.title, e.title)
        ui.set_text(self.length, "Shown %.2f m long" % model.size[2])
        ui.set_text(self.credit, catalogue.short_credit(model.credit) if model.credit else "")
        ui.set_visible(self.swatches.row, bool(model.paint))
        self.swatches.show_model(model)

        # At the right of the turntable camera's view (the settings panel is
        # at the left), clear of the turntable and a little behind the model,
        # turned to face that camera.
        size = max(model.radius() / 2.3, 0.45)
        c = model.centre()
        eye = orbit(c, framing_distance(model.radius(), 40.0, args.width / args.height), 35.0, 9.0)
        a = math.radians(35.0)
        right, back = (math.cos(a), 0.0, -math.sin(a)), (-math.sin(a), 0.0, -math.cos(a))
        place = add(scale(right, model.turntable_radius() + 0.85 * size), scale(back, 0.6 * size))
        scene.set_scale(self.root, (size,) * 3)
        scene.set_position(self.root, place)
        scene.set_rotation(self.root, (0.0, math.atan2(eye[0] - place[0], eye[2] - place[2]), 0.0))

    def set_shown(self, shown):
        self.shown = shown
        if self.built:
            engine.ui.set_visible(self.canvas, shown)      # nor does it take the pointer
            scene.set_visible(self.canvas, shown)
            for e in self.parts:
                scene.set_visible(e, shown)
            showroom.bump()        # it casts a shadow into the virtual shadow map

    def update(self):
        if not self.built or not self.shown:
            return
        ui, s = engine.ui, showroom
        if ui.was_clicked(self.previous):
            s.select(s.index - 1)
        if ui.was_clicked(self.next):
            s.select(s.index + 1)
        paint = self.swatches.clicked()
        if paint is not None:
            s.set_paint(paint)
        if ui.value_changed(self.spin):
            s.spinning = ui.toggle_value(self.spin)
        self.swatches.sync(s.paint)
        if ui.toggle_value(self.spin) != s.spinning:
            ui.set_toggle_value(self.spin, s.spinning)


# ── The showroom ─────────────────────────────────────────────────────────

CAMERA_MODES = {"turntable": "Turntable", "cinematic": "Cinematic", "orbit": "Orbit", "free": "Free"}
SHADOW_LABELS = {"hybrid": "Virtual shadow map + ray-traced lights", "high": "Virtual shadow map",
                 "raytraced": "Ray-traced sun and lights", "cascades": "Cascaded shadow maps"}
DEBUG_VIEWS = ("final image", "VSM levels and pages", "shadow mask", "normals", "ambient occlusion",
               "lights per cluster", "motion vectors", "VSM pages drawn this frame (red) or cached (green)")
PAINTS = [None, (0.42, 0.012, 0.012), (0.012, 0.012, 0.014), (0.8, 0.8, 0.78), (0.02, 0.12, 0.06),
          (0.35, 0.36, 0.38), (0.75, 0.32, 0.02)]


class Showroom:
    def __init__(self):
        self.models = find_models()
        self.stage = Stage()
        self.overlay = Overlay()
        self.stats = StatsPanel()
        self.settings = SettingsPanel()
        self.info_stand = InfoStand()
        self.index = 0
        self.rig = None
        self.camera_mode = args.camera
        self.spinning = True
        self.shots = []
        self.shot_time = 0.0
        self.epoch = 0
        self.preset = "high"
        self.paint = 0
        self.clearcoat = not args.no_clearcoat
        self.area_lights = not args.sphere_lights
        self.orbit_goal = None      # azimuth, elevation (degrees), distance the orbit camera eases to
        self.orbit_now = None
        self.orbit_drag = None      # while the left button is down: whether the press turns the camera
        self.tonemapper = args.tonemapper
        self.floor_white = False
        self.time = 0.0
        self.debug_view = 0
        self.dof = args.dof
        self.focus = 10.0         # metres to what the camera looks at

    @property
    def model(self):
        return self.models[self.index]

    def start(self):
        # The picture rectangle lights with image slot 1 shine with: the
        # pipeline's "lightImage", prefiltered there for blurred reflections.
        if not engine.set_pipeline_image("lightImage", LIGHT_IMAGE):
            print("[showroom] could not load the window's image", LIGHT_IMAGE)
        self.stage.build()
        self.overlay.build()
        self.apply_tonemapper()
        self.stats.build(self.overlay.k)
        rt = engine.ray_query_supported()
        self.preset = {"hybrid": "hybrid", "vsm": "high", "raytraced": "raytraced", "cascades": "cascades",
                       None: None}[args.shadows]
        if self.preset is None:
            self.preset = "hybrid" if rt else "high"
        if self.preset in ("hybrid", "raytraced") and not rt:
            print("[showroom] this GPU has no ray queries; using the virtual shadow map")
            self.preset = "high"
        engine.apply_pipeline_preset(self.preset)
        engine.set_custom_float("default.contactShadowLength", 0.12)
        engine.set_custom_float("default.aoRadius", 0.45)
        engine.set_custom_float("default.bloomIntensity", 0.05)
        engine.set_custom_float("default.skyBlur", 2.0)
        engine.set_custom_float("default.sunAngle", 0.35)
        self.reflections = not args.no_reflections
        engine.set_custom_float("showroom.reflections", 1.0 if self.reflections else 0.0)
        engine.set_custom_float("showroom.areaLights", 1.0 if self.area_lights else 0.0)
        engine.set_custom_float("showroom.softboxGrid", float(DIFFUSERS[self.stage.diffuser][0]))
        if not (args.screenshots or args.record):
            fonts = {"serif": engine.ui.load_font("fonts/PlayfairDisplay.ttf")}
            self.settings.build(fonts)
            self.info_stand.build(fonts)
        self.select(0)

    def select(self, index):
        old = self.model if self.model.loaded else None
        self.index = index % len(self.models)
        model = self.model
        if old is not None and old is not model:
            old.show(False)
        if not model.loaded:
            model.load(self.stage.spin)
        model.show(True)
        self.stage.stand(model)
        self.settings.show_model(model)
        self.info_stand.show_model(model)
        self.orbit_goal = None
        self.set_rig(RIG_BY_NAME[args.lighting] if args.lighting else RIG_BY_NAME[model.entry.rig])
        self.paint = 0
        self.overlay.show_model(model)
        self.shots = cinematic_shots(model, args.width / args.height)
        self.shot_time = 0.0
        self.bump()
        print("[showroom] %s" % " ".join(p for p in (model.entry.year, model.entry.title) if p))
        if model.credit:
            print("           %s" % model.credit)

    def set_rig(self, rig):
        self.rig = rig
        self.stage.light(rig, self.model)
        self.apply_floor()
        self.bump()
        self.update_status()

    def bump(self):
        """Everything in the virtual shadow map is out of date."""
        self.epoch += 1
        engine.set_custom_uint("showroom.vsmEpoch", self.epoch)

    def update_status(self):
        self.overlay.set_status("%s  |  %s  |  %s%s%s%s%s" % (
            self.rig.label, SHADOW_LABELS.get(self.preset, self.preset), TONE_MAPPERS[self.tonemapper][1],
            "" if self.clearcoat else "  |  no clear coat",
            "" if self.area_lights else "  |  sphere lights",
            "" if self.stage.diffuser == "grid" else "  |  " + DIFFUSERS[self.stage.diffuser][1],
            "  |  f/%g" % self.dof if self.dof > 0.0 else ""))

    def next_dof(self):
        stops = list(DOF_STOPS)
        if self.dof not in stops:
            stops.insert(1, self.dof)
        self.set_dof(stops[(stops.index(self.dof) + 1) % len(stops)])
        print("[showroom] depth of field:", "f/%g" % self.dof if self.dof > 0.0 else "off")

    def set_dof(self, f_number):
        """Depth of field at f_number; 0 turns it off."""
        self.dof = f_number
        self.update_status()

    def apply_dof(self):
        """Focus where the camera looks: the shot's target, or in the free
        camera the model."""
        engine.set_custom_uint("showroom.dof", 1 if self.dof > 0.0 else 0)
        if self.dof <= 0.0:
            return
        if self.camera_mode == "free":
            eye = scene.get_world_position(engine.camera_entity)
            c = self.model.centre()
            self.focus = math.sqrt(sum((c[i] - eye[i]) ** 2 for i in range(3)))
        engine.set_custom_float("showroom.dofFocus", args.focus if args.focus > 0.0 else self.focus)
        engine.set_custom_float("showroom.dofFStop", self.dof)

    def apply_tonemapper(self):
        engine.set_custom_uint("default.toneMapper", TONE_MAPPERS[self.tonemapper][0])

    def next_tonemapper(self):
        names = list(TONE_MAPPERS)
        self.set_tonemapper(names[(names.index(self.tonemapper) + 1) % len(names)])

    def set_tonemapper(self, name):
        self.tonemapper = name
        self.apply_tonemapper()
        self.update_status()
        print("[showroom] tone mapper:", TONE_MAPPERS[self.tonemapper][1])

    def toggle_clearcoat(self):
        self.set_clearcoat(not self.clearcoat)

    def set_clearcoat(self, on):
        self.clearcoat = on
        for model in self.models:
            if model.loaded:
                model.apply_coat(self.clearcoat)
        self.update_status()
        print("[showroom] clear coat", "on" if self.clearcoat else "off")

    def next_diffuser(self):
        names = list(DIFFUSERS)
        self.set_diffuser(names[(names.index(self.stage.diffuser) + 1) % len(names)])

    def set_diffuser(self, name):
        self.stage.diffuser = name
        engine.set_custom_float("showroom.softboxGrid", float(DIFFUSERS[self.stage.diffuser][0]))
        self.stage.light(self.rig, self.model)
        self.apply_floor()
        self.update_status()
        print("[showroom] softbox diffusers:", DIFFUSERS[self.stage.diffuser][1])

    def toggle_area_lights(self):
        self.set_area_lights(not self.area_lights)

    def set_area_lights(self, on):
        self.area_lights = on
        engine.set_custom_float("showroom.areaLights", 1.0 if self.area_lights else 0.0)
        self.update_status()
        print("[showroom] softboxes lit as", "rectangles" if self.area_lights else "spheres")

    def set_reflections(self, on):
        self.reflections = on
        engine.set_custom_float("showroom.reflections", 1.0 if on else 0.0)
        print("[showroom] reflections", "on" if on else "off",
              "" if self.preset in ("hybrid", "raytraced") else "(they need ray-traced lights: G)")

    def set_camera(self, mode):
        self.camera_mode = mode
        self.shot_time = 0.0
        self.orbit_goal = None
        print("[showroom] camera:", mode)

    # Frame by frame ------------------------------------------------------

    def update(self, dt):
        self.time += dt
        model = self.model
        if self.camera_mode == "cinematic":
            self.stage.turn(0.0)
        elif self.spinning:
            self.stage.turn(model.entry.spin * dt)
        moving = self.spinning and self.camera_mode != "cinematic"
        if model.entry.hover:
            bob = 0.04 * math.sin(self.time * 1.7) * min(1.0, model.radius() / 2.0)
            scene.set_position(self.stage.spin, (0.0, bob, 0.0))
            moving = True
        # Pages of the virtual shadow map under what moves are drawn again
        # every frame; the rest of the map stays cached.
        if moving:
            c = model.centre()
            r = max(model.radius(), model.turntable_radius()) * 1.05
            engine.set_custom_vec4("showroom.vsmDirty0", (c[0], c[1], c[2], r))
        else:
            engine.set_custom_vec4("showroom.vsmDirty0", (0.0, 0.0, 0.0, 0.0))
        self.update_camera(dt)
        self.apply_dof()

    def update_camera(self, dt):
        model = self.model
        aspect = args.width / args.height
        if self.camera_mode == "turntable":
            c = model.centre()
            d = framing_distance(model.radius(), 40.0, aspect)
            eye = orbit(c, d, 35.0 + 6.0 * math.sin(self.time * 0.07), 9.0)
            self.pose(eye, add(c, (0.0, -model.size[1] * 0.12, 0.0)), 40.0)
        elif self.camera_mode == "orbit":
            self.orbit_camera(dt)
        elif self.camera_mode == "cinematic":
            self.shot_time += dt
            total = sum(s.seconds for s in self.shots)
            t = self.shot_time % total
            for shot in self.shots:
                if t < shot.seconds:
                    eye, target, fov = shot.pose(t / shot.seconds)
                    self.pose(eye, target, fov)
                    break
                t -= shot.seconds

    def orbit_camera(self, dt):
        """Turn around the model with the left mouse button held, and come
        closer or go further with the wheel. The camera always looks at the
        model's centre, and eases to where it is sent."""
        model = self.model
        target = add(model.centre(), (0.0, -model.size[1] * 0.12, 0.0))
        nearest = model.radius() * 1.1
        farthest = studio.FLOOR_RADIUS * self.stage.stage_scale - 0.6
        if self.orbit_goal is None:
            # Start where the camera is, so switching to it does not jump.
            eye = scene.get_world_position(engine.camera_entity)
            offset = tuple(eye[i] - target[i] for i in range(3))
            distance = max(math.sqrt(sum(v * v for v in offset)), 1e-3)
            self.orbit_goal = [math.degrees(math.atan2(offset[0], offset[2])),
                               math.degrees(math.asin(max(-1.0, min(1.0, offset[1] / distance)))), distance]
            self.orbit_now = None
        goal = self.orbit_goal
        # A press on the settings panel or the info stand works the UI for as
        # long as the button is held, and the wheel over them scrolls them.
        if not engine.input.is_mouse_button_down(keys.MOUSE_LEFT):
            self.orbit_drag = None
        elif self.orbit_drag is None:
            self.orbit_drag = not engine.ui.pointer_over_ui
        if self.orbit_drag:
            dx, dy = engine.input.get_mouse_delta()
            goal[0] -= dx * ORBIT_DEGREES_PER_PIXEL
            goal[1] += dy * ORBIT_DEGREES_PER_PIXEL
        scroll = engine.input.get_scroll_delta()[1]
        if scroll and not engine.ui.pointer_over_ui:
            goal[2] *= ORBIT_ZOOM_STEP ** scroll
        goal[1] = max(-3.0, min(80.0, goal[1]))
        goal[2] = max(nearest, min(max(farthest, nearest), goal[2]))
        if self.orbit_now is None:
            self.orbit_now = list(goal)
        ease = 1.0 - math.exp(-14.0 * dt)
        now = self.orbit_now
        for i in range(3):
            now[i] += (goal[i] - now[i]) * ease
        self.pose(orbit(target, now[2], now[0], now[1]), target, 40.0)

    def pose(self, eye, target, fov):
        camera = engine.camera_entity
        # Inside the cove, and above its floor, whatever the shot asks for.
        limit = studio.FLOOR_RADIUS * self.stage.stage_scale - 0.5
        horizontal = math.hypot(eye[0], eye[2])
        if horizontal > limit:
            eye = (eye[0] * limit / horizontal, eye[1], eye[2] * limit / horizontal)
        eye = (eye[0], max(eye[1], 0.15), eye[2])
        self.focus = math.sqrt(sum((target[i] - eye[i]) ** 2 for i in range(3)))
        scene.set_position(camera, eye)
        scene.set_rotation(camera, look_rotation(eye, target))
        scene.set_camera_fov(camera, fov)

    # Keys ----------------------------------------------------------------

    def next_rig(self):
        i = RIGS.index(self.rig)
        self.set_rig(RIGS[(i + 1) % len(RIGS)])
        print("[showroom] lighting:", self.rig.label)

    def shadow_presets(self):
        return (["hybrid", "raytraced", "high", "cascades"] if engine.ray_query_supported()
                else ["high", "cascades"])

    def next_shadows(self):
        order = self.shadow_presets()
        self.set_shadows(order[(order.index(self.preset) + 1) % len(order)] if self.preset in order else "high")

    def set_shadows(self, preset):
        self.preset = preset
        engine.apply_pipeline_preset(self.preset)
        engine.set_custom_float("default.contactShadowLength", 0.12)
        self.update_status()
        print("[showroom] shadows:", SHADOW_LABELS[self.preset])

    def next_paint(self):
        if not self.model.paint:
            print("[showroom] no paint materials found on this model (catalogue 'paint' pattern)")
            return
        self.set_paint((self.paint + 1) % len(PAINTS))

    def set_paint(self, index):
        """PAINTS[index] on the model's body paint; 0 is the file's own."""
        model = self.model
        self.paint = index
        for e in model.paint:
            color = PAINTS[self.paint]
            scene.set_material_vec4(e, "baseColorFactor",
                                    model.paint_original[e] if color is None else color + (1.0,))

    def turn_model(self):
        model = self.model
        model.yaw = (model.yaw + 90.0) % 360.0
        model.place()
        self.info_stand.show_model(model)
        self.shots = cinematic_shots(model, args.width / args.height)
        self.bump()
        print("[showroom] %s: yaw=%g in catalogue.py" % (model.entry.name, model.yaw))

    def toggle_floor(self):
        self.floor_white = not self.floor_white
        self.apply_floor()

    def apply_floor(self):
        """The white floor and cove, or the rig's own."""
        floor = (0.75, 0.75, 0.74) if self.floor_white else self.rig.floor
        cove = (0.85, 0.85, 0.84) if self.floor_white else self.rig.cove
        for e in self.stage.floor:
            scene.set_material_vec4(e, "baseColorFactor", floor + (1.0,))
        for e in self.stage.cove:
            scene.set_material_vec4(e, "baseColorFactor", cove + (1.0,))


showroom = Showroom()


# ── Interactive and automatic runs ───────────────────────────────────────

class Accumulation:
    """Capture mode: frames averaged in the pipeline's accumColor (pass
    Accumulate, shown by Tonemap while it is on)."""

    RAW, TAA = 1, 0

    def __init__(self):
        self.on = False

    def begin(self, source):
        engine.set_pass_enabled("Accumulate", True)
        engine.set_custom_uint("showroom.accumSource", source)
        engine.set_custom_uint("showroom.accumulate", 1)
        engine.set_custom_uint("showroom.accumFrame", 0)
        self.on = True

    def frame(self, index):
        """This frame is sample `index` of the average; 0 starts it over."""
        engine.set_custom_uint("showroom.accumFrame", index)

    def end(self):
        engine.set_custom_uint("showroom.accumulate", 0)
        engine.set_pass_enabled("Accumulate", False)
        self.on = False


accumulation = Accumulation()


class StillCapture:
    """Key I: hold the view still, average args.samples frames of it, and
    save the result."""

    def __init__(self):
        self.frames = -1

    @property
    def active(self):
        return self.frames >= 0

    def start(self):
        if self.active:
            return
        self.frames = 0
        self.path = capture_path(".png")
        accumulation.begin(Accumulation.RAW)
        print("[showroom] averaging %d frames ..." % max(args.samples, 1))

    def update(self):
        samples = max(args.samples, 1)
        if self.frames < samples:
            accumulation.frame(self.frames)
        else:
            # The frame presented last holds the full average.
            ok = engine.capture_screenshot(self.path)
            print("[showroom] still ->", self.path if ok else "failed")
            accumulation.end()
            self.frames = -1
            return
        self.frames += 1


still_capture = StillCapture()


def capture_path(suffix):
    directory = os.path.join(os.getcwd(), "showroom_captures")
    os.makedirs(directory, exist_ok=True)
    return os.path.join(directory, time.strftime("showroom_%Y%m%d_%H%M%S") + suffix)


class Controls:
    def __init__(self):
        self.pressed = keys.KeyEdges(engine.input).pressed

    def update(self, dt):
        s = showroom
        # While a control of the canvas UI has focus, the arrow keys move
        # between controls instead.
        ui_focused = engine.ui.focused_element != sk.NULL_ENTITY
        if self.pressed(keys.RIGHT) and not ui_focused:
            s.select(s.index + 1)
        if self.pressed(keys.LEFT) and not ui_focused:
            s.select(s.index - 1)
        if self.pressed(keys.L):
            s.next_rig()
        if self.pressed(keys.C):
            modes = list(CAMERA_MODES)
            s.set_camera(modes[(modes.index(s.camera_mode) + 1) % len(modes)])
        if self.pressed(keys.G):
            s.next_shadows()
        if self.pressed(keys.T):
            s.spinning = not s.spinning
        if self.pressed(keys.R):
            s.set_reflections(not s.reflections)
        if self.pressed(keys.K):
            s.next_paint()
        if self.pressed(keys.Y):
            s.turn_model()
        if self.pressed(keys.F):
            s.toggle_floor()
        if self.pressed(keys.V):
            s.debug_view = (s.debug_view + 1) % len(DEBUG_VIEWS)
            engine.set_custom_uint("default.debugView", s.debug_view)
            print("[showroom] debug view:", DEBUG_VIEWS[s.debug_view])
        if self.pressed(keys.H):
            s.overlay.visible = not s.overlay.visible
            s.overlay.apply()
        if self.pressed(keys.F1):
            s.overlay.help_visible = not s.overlay.help_visible
            s.overlay.apply()
            s.settings.apply()
        if self.pressed(keys.F2):
            s.settings.visible = not s.settings.visible
            s.settings.apply()
        if self.pressed(keys.O):
            s.stats.toggle()
        if self.pressed(keys.M):
            s.next_tonemapper()
        if self.pressed(keys.X):
            s.toggle_clearcoat()
        if self.pressed(keys.J):
            s.toggle_area_lights()
        if self.pressed(keys.U):
            s.next_diffuser()
        if self.pressed(keys.B):
            s.next_dof()
        s.settings.update()
        s.info_stand.update()
        s.stats.update(dt)
        if self.pressed(keys.I):
            still_capture.start()
        if self.pressed(keys.P):
            path = capture_path(".png")
            print("[showroom] screenshot ->", path if engine.capture_screenshot(path) else "failed")
        if self.pressed(keys.F9):
            if engine.is_recording:
                engine.stop_recording()
                print("[showroom] recording stopped")
            else:
                path = capture_path(".mp4")
                ok = engine.start_recording(path, fps=args.fps)
                print("[showroom] recording ->", path if ok else "failed (see the error above)")


class Stills:
    """--screenshots: every model, in its lighting and the studio's, from
    every still pose; each frame held until TAA, the ray-traced shadows and
    exposure have settled."""

    def __init__(self, directory):
        self.directory = directory
        os.makedirs(directory, exist_ok=True)
        self.jobs = []
        for i, model in enumerate(showroom.models):
            rigs = [model.entry.rig] if args.lighting is None else [args.lighting]
            if args.lighting is None and "studio" not in rigs:
                rigs.append("studio")
            for rig in rigs:
                for pose in range(len(still_poses(model, args.width / args.height))):
                    self.jobs.append((i, rig, pose))
        self.job = -1
        self.frames = 0
        showroom.camera_mode = "still"       # poses are set here, not by the turntable camera
        showroom.spinning = False

    def update(self, dt):
        if engine.input.is_key_down(keys.ESCAPE):
            finish("stopped")
        samples = max(args.samples, 1)
        if self.job >= 0:
            self.frames += 1
            # After settling, average `samples` frames, then save the last.
            if samples > 1 and args.settle <= self.frames < args.settle + samples:
                if self.frames == args.settle:
                    accumulation.begin(Accumulation.RAW)
                accumulation.frame(self.frames - args.settle)
            if self.frames == args.settle + (samples if samples > 1 else 0):
                i, rig, pose = self.jobs[self.job]
                model = showroom.models[i]
                name = still_poses(model, args.width / args.height)[pose][0]
                path = os.path.join(self.directory, "%s_%s_%s.png" % (model.entry.name, rig, name))
                engine.capture_screenshot(path)
                print("[showroom] still ->", path)
                if accumulation.on:
                    accumulation.end()
        if self.job < 0 or self.frames >= args.settle + (samples if samples > 1 else 0) + 1:
            self.job += 1
            self.frames = 0
            if self.job >= len(self.jobs):
                print("[showroom] %d stills in %s" % (len(self.jobs), self.directory))
                os._exit(0)        # the engine has no quit call
            i, rig, pose = self.jobs[self.job]
            if i != showroom.index or not showroom.model.loaded:
                showroom.select(i)
            if showroom.rig.name != rig:
                showroom.set_rig(RIG_BY_NAME[rig])
            model = showroom.model
            _, eye, target, fov = still_poses(model, args.width / args.height)[pose]
            showroom.pose(eye, target, fov)
            engine.set_custom_float("default.exposureAdaptSpeed", 12.0)


class Tour:
    """--record: the cinematic shots of every model in turn, at a fixed
    frame rate whatever the rendering speed, fading through black between
    models."""

    def __init__(self, path):
        self.path = path
        self.step = 1.0 / args.fps
        self.model_time = 0.0
        self.started = False
        self.frames = 0
        self.sub = 0      # sub-frame of the recorded frame being rendered
        # Every model gets the same shots, so the length is known up front.
        self.total = int(round(len(showroom.models) * self.seconds() * args.fps))
        self.start_time = None
        self.last_report = 0.0

    def seconds(self):
        return args.seconds or sum(s.seconds for s in showroom.shots)

    def report(self):
        """Every few seconds: frames written, and an estimate of the time left."""
        now = time.monotonic()
        if now - self.last_report < 5.0:
            return
        self.last_report = now
        done = engine.recorded_frame_count
        elapsed = now - self.start_time
        line = "[showroom] recording: %d / %d frames (%d%%)" % (done, self.total, 100 * done // max(self.total, 1))
        if done > 0:
            left = elapsed / done * max(self.total - done, 0)
            line += ", %s left" % format_duration(left)
        print(line + "  (Esc or Ctrl+C stops and keeps what is recorded)", flush=True)

    def update(self, dt):
        if not self.started:
            # Let the first model settle before rolling.
            self.frames += 1
            if self.frames < args.settle:
                showroom.update(0.0)
                return
            if not engine.start_recording(self.path, fps=args.fps):
                print("[showroom] could not start recording (see the error above)")
                os._exit(1)
            print("[showroom] recording ->", self.path)
            self.started = True
            self.start_time = time.monotonic()
            if args.motion_blur > 1:
                accumulation.begin(Accumulation.TAA)

        # Each recorded frame is the average of `blur` sub-frames spread over
        # the shutter; only the last is written, holding the average.
        blur = max(args.motion_blur, 1)
        sub_step = self.step * min(max(args.shutter, 0.0), 1.0) / blur
        if engine.input.is_key_down(keys.ESCAPE):
            finish("stopped")
        if self.sub == 0:
            self.report()
            self.model_time += self.step
            if self.model_time >= self.seconds():
                if showroom.index + 1 >= len(showroom.models):
                    engine.stop_recording()
                    print("[showroom] tour recorded: %s" % self.path)
                    os._exit(0)
                showroom.select(showroom.index + 1)
                self.model_time = 0.0
            length = self.seconds()
            fade = min(1.0, self.model_time / 0.6, (length - self.model_time) / 0.6)
            engine.set_custom_float("default.exposure", showroom.rig.exposure * max(fade, 0.0) ** 2)
        if blur > 1:
            accumulation.frame(self.sub)
            engine.recording_paused = self.sub != blur - 1
        showroom.update(self.step - (blur - 1) * sub_step if self.sub == 0 else sub_step)
        self.sub = (self.sub + 1) % blur


def format_duration(seconds):
    seconds = int(seconds)
    if seconds >= 3600:
        return "%d:%02d:%02d" % (seconds // 3600, seconds // 60 % 60, seconds % 60)
    return "%d:%02d" % (seconds // 60, seconds % 60)


def finish(reason):
    """Finish any recording, so the file plays, and quit."""
    if engine.is_recording:
        frames = engine.recorded_frame_count
        ok = engine.stop_recording()
        print("[showroom] recording %s after %d frames (%s): %s" % (
            reason, frames, format_duration(frames / max(args.fps, 1)),
            "saved" if ok else "could not be finished, see the error above"), flush=True)
    os._exit(0)        # the engine has no quit call


# Ctrl+C: Python only notices it when a callback next runs, so the handler
# just asks, and the next frame finishes the recording and quits. (Raising
# KeyboardInterrupt instead would end that one callback and leave the
# engine running.)
quit_requested = False


def request_quit(signum, frame):
    global quit_requested
    quit_requested = True


signal.signal(signal.SIGINT, request_quit)

controls = Controls()
automation = None


def on_init():
    global automation
    camera = engine.create_camera(pos=(0.0, 1.5, 8.0), fov=40.0, speed=4.0, near_plane=0.03, far_plane=300.0)
    scene.set_rotation(camera, look_rotation((0.0, 1.5, 8.0), (0.0, 0.6, 0.0)))
    engine.set_custom_float("default.exposureAdaptSpeed", 2.5)
    engine.set_custom_float("showroom.vignette", args.vignette)
    engine.set_custom_float("showroom.grain", args.grain)
    supersample = args.supersample if args.supersample is not None else (2.0 if args.record else 1.0)
    if supersample != 1.0:
        engine.render_scale = supersample
        # The shadow map's pages are picked per scene pixel; keep the texels
        # per window pixel, and the pages a frame needs, as without it.
        engine.set_custom_float("showroom.vsmLevelBias", math.log2(engine.render_scale))
        print("[showroom] rendering at %gx the window's size" % engine.render_scale)
    showroom.start()
    if args.screenshots:
        automation = Stills(args.screenshots)
    elif args.record:
        showroom.camera_mode = "cinematic"
        automation = Tour(os.path.abspath(args.record))

    def animate(dt):
        if quit_requested:
            finish("stopped")
        if automation is None and still_capture.active:
            showroom.update(0.0)        # held still while it is averaged
            still_capture.update()
        elif automation is None:
            showroom.update(dt)
        elif isinstance(automation, Stills):
            showroom.update(0.0)
            automation.update(dt)
        else:
            automation.update(dt)
        return True

    # Before the transform system (priority 0), so this frame's poses render.
    engine.ecs.add_system("Showroom", animate, priority=-5)


engine.set_on_init(on_init)
engine.set_on_update(lambda dt: automation is None and controls.update(dt))

if __name__ == "__main__":
    engine.run()

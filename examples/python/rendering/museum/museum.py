"""
Shoonyakasha Museum: a small digital car museum

The showroom's cars, in the order they were made, each on the turntable
beside a placard: its year, maker and name, what kind of car it is, its
figures and a few lines about it, with a timeline of the collection along
the bottom. It renders with the showroom (../showroom): its pipeline, studio
and models, so fetch those once with `python tools/fetch_assets.py showroom`
(see ../showroom/README.md). The placards are in exhibits.py.

Keys:
    LEFT / RIGHT   previous / next exhibit
    L              next lighting: studio, hard light, night, neon
    C              next camera: turntable, cinematic, free (WASD/Q/E, right mouse)
    G              next shadow technique: VSM + ray-traced lights, all ray traced,
                   VSM with shadow maps, cascades
    R              ray-traced reflections on or off (with ray-traced lights)
    T              turntable on or off
    LEFT MOUSE     drag to turn the turntable; it coasts on when let go
    , / .          turn the turntable while held
    - / =          turntable slower / faster
    K              next paint colour (cars)
    X              clear coat on or off (the lacquer over car paint)
    M              next tone mapper: AgX, PBR Neutral, ACES
    B              depth of field: off, f/2.8, f/0.8 (more than a real lens),
                   focused where the camera looks
    Y              turn the model 90 degrees (prints the catalogue yaw)
    F              floor: dark gloss or white
    V              debug views (VSM levels and pages, shadow mask, normals ...)
    H              hide or show the placard (the credit stays)
    O              render stats: frame rate, GPU time per pass, draw calls
    P              screenshot into museum_captures/, as on screen
    I              high-quality still into museum_captures/: the view held
                   while --samples frames are averaged
    F9             start or stop recording a video into museum_captures/
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
import struct
import sys
import time

import shoonyakasha as sk
from shoonyakasha import keys

HERE = os.path.dirname(os.path.abspath(__file__))
SHOWROOM = os.path.join(os.path.dirname(HERE), "showroom")
sys.path.insert(0, HERE)
sys.path.insert(1, SHOWROOM)
import catalogue  # noqa: E402  (the showroom's)
import exhibits  # noqa: E402
import studio  # noqa: E402  (the showroom's)

PIPELINE = os.path.join(SHOWROOM, "pipeline", "pipeline.json")

# settings.toneMapper in the pipeline's tonemap.frag
TONE_MAPPERS = {"agx": (2, "AgX"), "neutral": (1, "PBR Neutral"), "aces": (0, "ACES")}

# Key B: depth of field off, then at these f-numbers. Whole-car shots need
# more than a real lens to show much blur.
DOF_STOPS = (0.0, 2.8, 0.8)

# Body paint whose file declares no clear coat gets this one: a full coat,
# polished almost to a mirror.
PAINT_COAT = (1.0, 0.03)
FONT = "fonts/Roboto-Regular.ttf"
DISPLAY_FONT = "fonts/PlayfairDisplay.ttf"     # the placard's year and name

# The placard's colours
GOLD = (0.83, 0.69, 0.45, 1.0)
IVORY = (0.96, 0.93, 0.86, 1.0)
GREY = (0.62, 0.62, 0.64, 1.0)
BODY = (0.82, 0.81, 0.79, 1.0)


def parse_args():
    parser = argparse.ArgumentParser(description="Shoonyakasha museum",
                                     formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    parser.add_argument("--models", help="comma-separated catalogue names to show (default: every car, by year) "
                        "(default: every fetched model)")
    parser.add_argument("--extra", action="append", default=[], metavar="GLTF",
                        help="also show this glTF file (repeatable); LENGTH= in front scales it, "
                             "e.g. 4.5=path/car.gltf")
    parser.add_argument("--lighting", choices=["studio", "hardlight", "night", "neon"],
                        help="lighting to open with (default: each model's own)")
    parser.add_argument("--camera", choices=["turntable", "cinematic", "free"], default="turntable")
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


# ── Maths ────────────────────────────────────────────────────────────────

def lerp(a, b, t):
    return a + (b - a) * t


def lerp3(a, b, t):
    return tuple(lerp(x, y, t) for x, y in zip(a, b))


def smooth(t):
    t = min(max(t, 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def scale(a, s):
    return tuple(x * s for x in a)


def normalize(v):
    n = math.sqrt(sum(x * x for x in v)) or 1.0
    return tuple(x / n for x in v)


def rotation_facing(direction):
    """Euler rotation (pitch, yaw, 0) whose forward is `direction`."""
    x, y, z = normalize(direction)
    return (math.asin(max(-1.0, min(1.0, y))), math.atan2(-x, -z), 0.0)


def look_rotation(eye, target):
    return rotation_facing(tuple(t - e for t, e in zip(target, eye)))


def yaw_point(p, degrees):
    """Rotate p about +Y (the engine's yaw: +Z towards +X for positive angles)."""
    a = math.radians(degrees)
    c, s = math.cos(a), math.sin(a)
    return (c * p[0] + s * p[2], p[1], -s * p[0] + c * p[2])


def orbit(target, distance, azimuth, elevation):
    """A point `distance` from target, azimuth 0 in front (+Z), 90 to the right (+X)."""
    a, e = math.radians(azimuth), math.radians(elevation)
    return add(target, (distance * math.cos(e) * math.sin(a), distance * math.sin(e),
                        distance * math.cos(e) * math.cos(a)))


# ── glTF bounds, for models that come without a showroom.json ────────────

def gltf_bounds(path):
    """World-space bounds of a glTF or GLB's meshes, from their accessors'
    min/max (which glTF requires for positions)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] == b"glTF":
        length = struct.unpack_from("<I", data, 12)[0]
        gltf = json.loads(data[20:20 + length].decode("utf-8"))
    else:
        gltf = json.loads(data.decode("utf-8"))
    nodes = gltf.get("nodes", [])
    lo, hi = [math.inf] * 3, [-math.inf] * 3

    def matmul(a, b):
        return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]

    def local(node):
        if "matrix" in node:
            m = node["matrix"]
            return [[m[c * 4 + r] for c in range(4)] for r in range(4)]
        t = node.get("translation", [0, 0, 0])
        x, y, z, w = node.get("rotation", [0, 0, 0, 1])
        s = node.get("scale", [1, 1, 1])
        r = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
             [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
             [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
        return [[r[i][0] * s[0], r[i][1] * s[1], r[i][2] * s[2], t[i]] for i in range(3)] + [[0, 0, 0, 1]]

    def walk(index, parent):
        node = nodes[index]
        m = matmul(parent, local(node))
        if "mesh" in node:
            for primitive in gltf["meshes"][node["mesh"]]["primitives"]:
                a = gltf["accessors"][primitive["attributes"]["POSITION"]]
                if "min" not in a:
                    continue
                for x in (a["min"][0], a["max"][0]):
                    for y in (a["min"][1], a["max"][1]):
                        for z in (a["min"][2], a["max"][2]):
                            p = [sum(m[i][k] * v for k, v in enumerate((x, y, z, 1.0))) for i in range(3)]
                            for i in range(3):
                                lo[i] = min(lo[i], p[i])
                                hi[i] = max(hi[i], p[i])
        for child in node.get("children", []):
            walk(child, m)

    identity = [[1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]
    scene = gltf.get("scenes", [{}])[gltf.get("scene", 0)] if gltf.get("scenes") else {"nodes": list(range(len(nodes)))}
    for root in scene.get("nodes", []):
        walk(root, identity)
    if lo[0] == math.inf:
        return [-0.5, 0.0, -0.5], [0.5, 1.0, 0.5]
    return lo, hi


# ── The engine ───────────────────────────────────────────────────────────

sk.shaders.compile_dir(os.path.join(SHOWROOM, "pipeline", "shaders"))
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
        print("[museum] %s: %d entities, %d vertices, %.1fs%s; %.2f x %.2f x %.2f in the file, shown at x%.3g" % (
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


def exhibit_year(entry):
    exhibit = exhibits.EXHIBITS.get(entry.name)
    if exhibit:
        return exhibit.year
    return int(entry.year) if entry.year.isdigit() else 9999


def find_models():
    """The catalogue's fetched models, then --extra files; the bundled Fox if
    there is nothing else."""
    # Every car of the catalogue, oldest first; --models picks others, in its order.
    if args.models:
        wanted = args.models.split(",")
    else:
        cars = [e for e in catalogue.CATALOGUE if e.kind == "car"]
        wanted = [e.name for e in sorted(cars, key=lambda e: exhibit_year(e))]
    models, missing = [], []
    for name in wanted:
        entry = catalogue.BY_NAME.get(name)
        if entry is None:
            print("[museum] unknown model '%s'; known: %s" % (name, ", ".join(catalogue.BY_NAME)))
            continue
        directory = os.path.join(str(ROOT), "showroom", name)
        path = os.path.join(directory, "model.gltf")
        if not os.path.exists(path):
            missing.append(name)
            continue
        with open(os.path.join(directory, "showroom.json"), encoding="utf-8") as f:
            info = json.load(f)
        models.append(Model(entry, path, (info["boundsMin"], info["boundsMax"]), info["credit"]))
    if missing and models:
        print("[museum] Not fetched yet, so not shown: %s\n"
              "    looked in %s\n"
              "    fetch them with: python tools/fetch_assets.py %s"
              % (", ".join(missing), os.path.join(str(ROOT), "showroom"),
                 " ".join("showroom/" + n for n in missing)))
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
            models.append(Model(entry, path, gltf_bounds(path), ""))
    if not models:
        print("[museum] No showroom models found. Fetch them once with\n"
              "    python tools/fetch_assets.py showroom\n"
              "(needs a Sketchfab API token, see README.md). Showing the bundled Fox meanwhile.")
        path = str(ROOT / "models" / "Fox.glb")
        entry = catalogue.Entry("fox", "Fox", "Khronos glTF sample", "miniature", 1.1, yaw=-90.0, rig="studio")
        models.append(Model(entry, path, gltf_bounds(path),
                            '"Fox" by PixelMannen (CC0), rigged by tomkranis (CC BY 4.0), '
                            'from the Khronos glTF Sample Assets'))
    return models


# ── Lights ───────────────────────────────────────────────────────────────

class LightSpec:
    """One light of a rig. `direction` points from the model's centre towards
    the light, in the stage's frame (+Z front, +X right), `distance` in units
    of the model's size; softbox (width, height) in metres draws a panel."""

    def __init__(self, direction, distance, color, intensity, radius=0.3, cone=(35.0, 70.0),
                 softbox=None, shadows=True, aim_height=0.0):
        self.direction = normalize(direction)
        self.distance = distance
        self.color = color
        self.intensity = intensity
        self.radius = radius
        self.cone = cone
        self.softbox = softbox
        self.shadows = shadows
        self.aim_height = aim_height


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
        size = max(model.radius() / 2.3, 0.45)
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
            scene.set_light_intensity(light, spec.intensity * size * size)
            scene.set_light_range(light, distance * 2.5)
            scene.set_light_cone(light, spec.cone[0], spec.cone[1])
            scene.set_light_source_radius(light, spec.radius * size)
            scene.set_light_cast_shadows(light, spec.shadows)
            visible = spec.softbox is not None
            for e in parts:
                scene.set_visible(e, visible)
            if visible:
                w, h = spec.softbox
                scene.set_position(box, position)
                scene.set_rotation(box, rotation)
                scene.set_scale(box, (w * size, h * size, size))
                glow = scale(spec.color, 3.0 + spec.intensity / 15.0)
                for e in diffusers:
                    scene.set_material_vec4(e, "emissiveFactor", glow + (0.0,))
        if rig.sun:
            direction, color, intensity = rig.sun
            scene.set_rotation(self.sun, rotation_facing(direction))
            scene.set_light_color(self.sun, color)
            scene.set_light_intensity(self.sun, intensity)
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
        engine.set_custom_float("default.iblIntensity", rig.ibl)
        engine.set_custom_float("default.exposure", rig.exposure)
        engine.set_custom_float("default.exposureMinEV", rig.min_ev)

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
        u = smooth(u)
        eye = orbit(c, lerp(d * 1.25, d * 0.95, u), lerp(42.0, 30.0, u), lerp(9.0, 6.0, u))
        return eye, add(c, (0.0, -h * 0.15, 0.0)), 40.0

    def side_track(u):
        u = smooth(u)
        x = w + max(r * 1.25, 1.6)
        eye = (x, base + h * 0.45, lerp(l * 0.9, -l * 0.9, u))
        target = (0.0, base + h * 0.4, lerp(l * 0.55, -l * 0.55, u))
        return eye, target, 45.0

    def crane(u):
        u = smooth(u)
        eye = orbit(c, d * 1.1, lerp(150.0, 230.0, u), lerp(58.0, 34.0, u))
        return eye, c, 40.0

    def detail(u):
        u = smooth(u)
        # A front corner: a wheel on a car, the nose or a wingtip on a ship.
        target = (w * 0.75, base + h * (0.45 if ship else 0.22), l * 0.62)
        away = normalize((0.9, 0.25, 0.7))
        eye = add(target, scale(away, lerp(r * 0.75, r * 0.45, u)))
        return eye, target, 38.0

    def rear_pull(u):
        u = smooth(u)
        eye = orbit(c, lerp(d * 0.8, d * 1.2, u), lerp(205.0, 215.0, u), lerp(5.0, 12.0, u))
        return eye, c, 40.0

    def hero(u):
        u = smooth(u)
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

class Text:
    """A label with a soft shadow under it, so it reads over a bright
    softbox as well as a dark wall."""

    def __init__(self, anchor, x, y, size, color=IVORY, font=FONT, align=None, shadow=True):
        k = placard_scale()
        self.shadow = None
        if shadow:
            self.shadow = engine.create_text("", anchor, (x + 1.5 * k, y + 2.0 * k), font, font_size=size,
                                             color=(0.0, 0.0, 0.0, 0.55 * color[3]))
            scene.set_text_sort_key(self.shadow, 1)
        self.label = engine.create_text("", anchor, (x, y), font, font_size=size, color=color)
        scene.set_text_sort_key(self.label, 2)
        if align is not None:
            for label in self.labels():
                scene.set_text_align(label, align)

    def labels(self):
        return [self.label] + ([self.shadow] if self.shadow else [])

    def set(self, text):
        for label in self.labels():
            scene.set_text(label, catalogue.ascii_text(text))

    def show(self, visible):
        for label in self.labels():
            scene.set_text_visible(label, visible)

    def color(self, color):
        scene.set_text_color(self.label, color)


def placard_scale():
    return args.height / 1080.0 if not args.vertical else args.width / 1080.0


STAT_ROWS = 6
TEXT_LINES = 6


class Overlay:
    """The placard: a dark column at the left (at the bottom in --vertical)
    with the exhibit's year, name, kind, figures and text; a timeline of the
    collection; the model's credit; the museum's state at the top right; help
    on F1."""

    def __init__(self):
        self.visible = not args.no_overlay
        self.help_visible = False

    def build(self):
        k = self.k = placard_scale()
        tl = sk.UI_ANCHOR_TOP_LEFT
        self.panels = []

        def panel(x, y, w, h, color):
            p = engine.create_ui_panel(tl, (x + w / 2.0, y + h / 2.0), (w, h), color=color)
            scene.set_sort_key(p, 0)
            self.panels.append(p)
            return p

        if args.vertical:
            # The lower half of a phone-shaped frame; the car stands above it.
            x0, top = 64.0 * k, args.height * 0.52
            panel(0.0, top, args.width, args.height - top, (0.02, 0.02, 0.025, 0.66))
            panel(0.0, top, args.width, 1.5 * k, GOLD[:3] + (0.8,))
            y0 = top - 70.0 * k
            column = args.width - 2 * x0
        else:
            x0, y0 = 80.0 * k, 0.0
            column = 560.0 * k
            panel(0.0, 0.0, 680.0 * k, args.height, (0.02, 0.02, 0.025, 0.62))
            panel(680.0 * k, 0.0, 1.5 * k, args.height, GOLD[:3] + (0.8,))
        self.column = column

        self.number = Text(tl, x0, y0 + 150.0 * k, 16.0 * k, GOLD)
        self.year = Text(tl, x0 - 4.0 * k, y0 + 285.0 * k, 128.0 * k, IVORY, DISPLAY_FONT)
        self.maker = Text(tl, x0, y0 + 340.0 * k, 20.0 * k, GOLD)
        self.name = Text(tl, x0 - 2.0 * k, y0 + 400.0 * k, 54.0 * k, IVORY, DISPLAY_FONT)
        self.kind = Text(tl, x0, y0 + 440.0 * k, 19.0 * k, GREY)
        self.rule = panel(x0, y0 + 470.0 * k, min(column, 480.0 * k), 1.5 * k, GOLD[:3] + (0.7,))
        self.stats = []
        for i in range(STAT_ROWS):
            y = y0 + (525.0 + 38.0 * i) * k
            self.stats.append((Text(tl, x0, y, 14.0 * k, GREY), Text(tl, x0 + 150.0 * k, y, 20.0 * k, IVORY)))
        self.text_top = y0 + (525.0 + 38.0 * STAT_ROWS + 30.0) * k
        self.text = [Text(tl, x0, self.text_top + 29.0 * k * i, 18.0 * k, BODY) for i in range(TEXT_LINES)]
        self.chars_per_line = int(column / (9.0 * k))

        # The credit: always on screen with the model, which its licence asks.
        self.credit = Text(sk.UI_ANCHOR_BOTTOM_LEFT, x0, -34.0 * k, 14.0 * k, (0.7, 0.7, 0.7, 1.0))
        self.status = Text(sk.UI_ANCHOR_TOP_RIGHT, -32.0 * k, 40.0 * k, 16.0 * k, (0.85, 0.85, 0.85, 0.9),
                           align=sk.TEXT_ALIGN_RIGHT)

        # The timeline: one stop per exhibit, along the bottom (the top in --vertical)
        n = len(showroom.models)
        if args.vertical:
            left, right, y = x0, args.width - x0, 96.0 * k
        else:
            left, right, y = 780.0 * k, args.width - 100.0 * k, args.height - 92.0 * k
        self.timeline_line = panel(left, y, right - left, 1.0 * k, (1.0, 1.0, 1.0, 0.35))
        self.stops = []
        for i, model in enumerate(showroom.models):
            x = left + (right - left) * (i + 0.5) / n
            dot = engine.create_ui_panel(tl, (x, y + 0.5 * k), (9.0 * k, 9.0 * k), color=(1.0, 1.0, 1.0, 0.6))
            scene.set_sort_key(dot, 1)
            exhibit = exhibits.EXHIBITS.get(model.entry.name)
            year = Text(tl, x, y + 34.0 * k, 22.0 * k, GREY, DISPLAY_FONT, align=sk.TEXT_ALIGN_CENTER)
            year.set(str(exhibit.year) if exhibit else model.entry.year)
            name = Text(tl, x, y + 56.0 * k, 13.0 * k, GREY, align=sk.TEXT_ALIGN_CENTER)
            name.set((exhibit.name if exhibit else model.entry.title).upper())
            self.stops.append((dot, year, name))

        # Short notes: a capture saved, the turntable's speed
        self.toast_label = Text(sk.UI_ANCHOR_TOP_RIGHT, -32.0 * k, 70.0 * k, 18.0 * k, GOLD,
                                align=sk.TEXT_ALIGN_RIGHT)
        self.toast_until = 0.0

        lines = [line.strip() for line in __doc__.split("Keys:")[1].split("Automatic")[0].strip().splitlines()]
        self.help = []
        for i, line in enumerate(lines):
            label = Text(tl, (720.0 if not args.vertical else 40.0) * k, (80.0 + 24.0 * i) * k, 18.0 * k)
            label.set(line)
            self.help.append(label)
        self.apply()

    def show_model(self, model):
        e = model.entry
        exhibit = exhibits.EXHIBITS.get(e.name)
        index = showroom.models.index(model)
        self.number.set("EXHIBIT %d OF %d" % (index + 1, len(showroom.models)))
        if exhibit:
            self.year.set(str(exhibit.year))
            self.maker.set(exhibit.maker.upper())
            self.name.set(exhibit.name)
            self.kind.set("%s   /   %s" % (exhibit.kind, exhibit.body))
            rows = exhibit.stats
            paragraph = exhibits.wrap(exhibit.text, self.chars_per_line)
        else:
            # Not in exhibits.py: what the catalogue knows
            self.year.set(e.year)
            self.maker.set("")
            self.name.set(e.title)
            self.kind.set(e.kind.capitalize())
            rows, paragraph = [], []
        for i, (label, value) in enumerate(self.stats):
            label.set(rows[i][0].upper() if i < len(rows) else "")
            value.set(rows[i][1] if i < len(rows) else "")
        for i, line in enumerate(self.text):
            line.set(paragraph[i] if i < len(paragraph) else "")
        self.credit.set("Model: " + catalogue.short_credit(model.credit) if model.credit else "")
        for i, (dot, year, name) in enumerate(self.stops):
            current = i == index
            scene.set_sprite_color(dot, GOLD if current else (1.0, 1.0, 1.0, 0.6))
            year.color(GOLD if current else GREY)
            name.color(IVORY if current else GREY)

    def set_status(self, text):
        self.status.set(text)

    def toast(self, text, seconds=3.0):
        """Show `text` at the top right for a few seconds; None hides it."""
        self.toast_label.set(text or "")
        self.toast_label.show(bool(text))
        self.toast_until = time.monotonic() + seconds if text else 0.0

    def update_toast(self):
        if self.toast_until and time.monotonic() > self.toast_until:
            self.toast(None)

    def labels(self):
        out = [self.number, self.year, self.maker, self.name, self.kind, self.status] + self.text
        for label, value in self.stats:
            out += [label, value]
        for _, year, name in self.stops:
            out += [year, name]
        return out

    def apply(self):
        for p in self.panels:
            scene.set_visible(p, self.visible)
        for _dot, _, _ in self.stops:
            scene.set_visible(_dot, self.visible)
        for label in self.labels():
            label.show(self.visible)
        self.credit.show(True)
        for label in self.help:
            label.show(self.help_visible)

    def framing(self):
        """Where the car goes in the frame, beside the placard: a shift of the
        view, in fractions of its half-width (x) and half-height (y)."""
        if not self.visible:
            return 0.0, 0.0
        return (0.0, -0.42) if args.vertical else (-0.34, 0.0)


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


# ── The showroom ─────────────────────────────────────────────────────────

SHADOW_LABELS = {"hybrid": "Virtual shadow map + ray-traced lights", "high": "Virtual shadow map",
                 "raytraced": "Ray-traced sun and lights", "cascades": "Cascaded shadow maps"}
DEBUG_VIEWS = ("final image", "VSM levels and pages", "shadow mask", "normals", "ambient occlusion",
               "lights per cluster", "motion vectors", "VSM pages drawn this frame (red) or cached (green)")
PAINTS = [None, (0.42, 0.012, 0.012), (0.012, 0.012, 0.014), (0.8, 0.8, 0.78), (0.02, 0.12, 0.06),
          (0.35, 0.36, 0.38), (0.75, 0.32, 0.02)]


class Plate:
    """The turntable by hand: drag it with the left mouse button and it
    coasts on when let go; hold , or . to turn it; - and = set how fast it
    turns by itself."""

    DRAG = 0.25          # degrees per pixel dragged
    KEYS = 45.0          # degrees per second while , or . is held
    FRICTION = 2.5       # how fast a coasting turn dies down, per second

    def __init__(self):
        self.speed = 1.0     # times the catalogue's spin
        self.velocity = 0.0  # degrees per second, while coasting
        self.last_mouse = None

    def faster(self, factor):
        self.speed = min(max(self.speed * factor, 0.125), 8.0)
        return self.speed

    def update(self, dt, auto_speed):
        """Degrees to turn the turntable by this frame, or None to leave it
        to the automatic spin."""
        if dt <= 0.0:
            self.last_mouse = None
            return None
        inp = engine.input
        turn = None
        if inp.is_mouse_button_down(keys.MOUSE_LEFT):
            x, _ = inp.get_mouse_position()
            if self.last_mouse is not None:
                turn = (x - self.last_mouse) * self.DRAG
                # The last moments of the drag set the coast
                self.velocity = 0.7 * self.velocity + 0.3 * turn / dt
            self.last_mouse = x
            return turn if turn is not None else 0.0
        self.last_mouse = None
        held = (1.0 if inp.is_key_down(keys.PERIOD) else 0.0) - (1.0 if inp.is_key_down(keys.COMMA) else 0.0)
        if held:
            self.velocity = 0.0
            return held * self.KEYS * dt
        if abs(self.velocity) > abs(auto_speed) + 1.0:
            self.velocity *= math.exp(-self.FRICTION * dt)
            return self.velocity * dt
        self.velocity = 0.0
        return None


class Showroom:
    def __init__(self):
        self.models = find_models()
        self.stage = Stage()
        self.overlay = Overlay()
        self.stats = StatsPanel()
        self.index = 0
        self.rig = None
        self.camera_mode = args.camera
        self.spinning = True
        self.plate = Plate()
        self.shots = []
        self.shot_time = 0.0
        self.epoch = 0
        self.preset = "high"
        self.paint = 0
        self.clearcoat = not args.no_clearcoat
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
            print("[museum] this GPU has no ray queries; using the virtual shadow map")
            self.preset = "high"
        engine.apply_pipeline_preset(self.preset)
        engine.set_custom_float("default.contactShadowLength", 0.12)
        engine.set_custom_float("default.aoRadius", 0.45)
        engine.set_custom_float("default.bloomIntensity", 0.05)
        engine.set_custom_float("default.skyBlur", 2.0)
        engine.set_custom_float("default.sunAngle", 0.35)
        self.reflections = not args.no_reflections
        engine.set_custom_float("showroom.reflections", 1.0 if self.reflections else 0.0)
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
        self.set_rig(RIG_BY_NAME[args.lighting] if args.lighting else RIG_BY_NAME[model.entry.rig])
        self.paint = 0
        self.overlay.show_model(model)
        self.shots = cinematic_shots(model, args.width / args.height)
        self.shot_time = 0.0
        self.bump()
        print("[museum] %s" % " ".join(p for p in (model.entry.year, model.entry.title) if p))
        if model.credit:
            print("           %s" % model.credit)

    def set_rig(self, rig):
        self.rig = rig
        self.stage.light(rig, self.model)
        self.bump()
        self.update_status()

    def bump(self):
        """Everything in the virtual shadow map is out of date."""
        self.epoch += 1
        engine.set_custom_uint("showroom.vsmEpoch", self.epoch)

    def update_status(self):
        self.overlay.set_status("%s  |  %s  |  %s%s%s" % (
            self.rig.label, SHADOW_LABELS.get(self.preset, self.preset), TONE_MAPPERS[self.tonemapper][1],
            "" if self.clearcoat else "  |  no clear coat",
            "  |  f/%g" % self.dof if self.dof > 0.0 else ""))

    def next_dof(self):
        stops = list(DOF_STOPS)
        if self.dof not in stops:
            stops.insert(1, self.dof)
        self.dof = stops[(stops.index(self.dof) + 1) % len(stops)]
        self.update_status()
        print("[museum] depth of field:", "f/%g" % self.dof if self.dof > 0.0 else "off")

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
        self.tonemapper = names[(names.index(self.tonemapper) + 1) % len(names)]
        self.apply_tonemapper()
        self.update_status()
        print("[museum] tone mapper:", TONE_MAPPERS[self.tonemapper][1])

    def toggle_clearcoat(self):
        self.clearcoat = not self.clearcoat
        for model in self.models:
            if model.loaded:
                model.apply_coat(self.clearcoat)
        self.update_status()
        print("[museum] clear coat", "on" if self.clearcoat else "off")

    # Frame by frame ------------------------------------------------------

    def update(self, dt):
        self.time += dt
        model = self.model
        auto = model.entry.spin * self.plate.speed if self.spinning else 0.0
        manual = self.plate.update(dt, auto) if self.camera_mode != "cinematic" else None
        if self.camera_mode == "cinematic":
            self.stage.turn(0.0)
        elif manual is not None:
            self.stage.turn(manual)
        elif self.spinning:
            self.stage.turn(auto * dt)
        moving = (self.spinning or manual is not None) and self.camera_mode != "cinematic"
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

    def pose(self, eye, target, fov):
        camera = engine.camera_entity
        eye, target = self.beside_placard(eye, target, fov)
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

    def beside_placard(self, eye, target, fov):
        """Move the camera sideways (up and down in --vertical) so the shot's
        subject sits beside the placard, and back a little, so it still fits
        in what the placard leaves free."""
        sx, sy = self.overlay.framing()
        if sx == 0.0 and sy == 0.0:
            return eye, target
        forward = normalize(tuple(target[i] - eye[i] for i in range(3)))
        right = normalize((-forward[2], 0.0, forward[0]))          # forward x up
        up = (right[1] * forward[2] - right[2] * forward[1],
              right[2] * forward[0] - right[0] * forward[2],
              right[0] * forward[1] - right[1] * forward[0])
        distance = math.sqrt(sum((target[i] - eye[i]) ** 2 for i in range(3))) * 1.12
        half_h = distance * math.tan(math.radians(fov) * 0.5)
        half_w = half_h * args.width / args.height
        shift = add(scale(right, sx * half_w), scale(up, sy * half_h))
        eye = add(add(target, scale(forward, -distance)), shift)
        return eye, add(target, shift)

    # Keys ----------------------------------------------------------------

    def next_rig(self):
        i = RIGS.index(self.rig)
        self.set_rig(RIGS[(i + 1) % len(RIGS)])
        print("[museum] lighting:", self.rig.label)

    def next_shadows(self):
        order = (["hybrid", "raytraced", "high", "cascades"] if engine.ray_query_supported()
                 else ["high", "cascades"])
        self.preset = order[(order.index(self.preset) + 1) % len(order)] if self.preset in order else "high"
        engine.apply_pipeline_preset(self.preset)
        engine.set_custom_float("default.contactShadowLength", 0.12)
        self.update_status()
        print("[museum] shadows:", SHADOW_LABELS[self.preset])

    def next_paint(self):
        model = self.model
        if not model.paint:
            print("[museum] no paint materials found on this model (catalogue 'paint' pattern)")
            return
        self.paint = (self.paint + 1) % len(PAINTS)
        for e in model.paint:
            color = PAINTS[self.paint]
            scene.set_material_vec4(e, "baseColorFactor",
                                    model.paint_original[e] if color is None else color + (1.0,))

    def turn_model(self):
        model = self.model
        model.yaw = (model.yaw + 90.0) % 360.0
        model.place()
        self.shots = cinematic_shots(model, args.width / args.height)
        self.bump()
        print("[museum] %s: yaw=%g in catalogue.py" % (model.entry.name, model.yaw))

    def toggle_floor(self):
        self.floor_white = not self.floor_white
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
        print("[museum] averaging %d frames ..." % max(args.samples, 1))

    def update(self):
        samples = max(args.samples, 1)
        if self.frames < samples:
            accumulation.frame(self.frames)
        else:
            # The frame presented last holds the full average.
            ok = engine.capture_screenshot(self.path)
            showroom.overlay.toast(("Saved " + os.path.basename(self.path)) if ok else "Still failed")
            print("[museum] still ->", self.path if ok else "failed")
            accumulation.end()
            self.frames = -1
            return
        self.frames += 1


still_capture = StillCapture()


class Screenshot:
    """Key P: the frame as on screen, without the toast saying a capture
    was saved: the toast is hidden, and the frame after next is captured."""

    def __init__(self):
        self.wait = -1

    def request(self):
        showroom.overlay.toast(None)
        self.wait = 2

    def update(self):
        if self.wait < 0:
            return
        self.wait -= 1
        if self.wait == 0:
            self.wait = -1
            path = capture_path(".png")
            ok = engine.capture_screenshot(path)
            print("[museum] screenshot ->", path if ok else "failed")
            showroom.overlay.toast(("Saved " + os.path.basename(path)) if ok else "Screenshot failed")


screenshot = Screenshot()


def capture_path(suffix):
    directory = os.path.join(os.getcwd(), "museum_captures")
    os.makedirs(directory, exist_ok=True)
    return os.path.join(directory, time.strftime("showroom_%Y%m%d_%H%M%S") + suffix)


class Controls:
    def __init__(self):
        self.down = {}

    def pressed(self, key):
        now = engine.input.is_key_down(key)
        was = self.down.get(key, False)
        self.down[key] = now
        return now and not was

    def update(self, dt):
        s = showroom
        if self.pressed(keys.RIGHT):
            s.select(s.index + 1)
        if self.pressed(keys.LEFT):
            s.select(s.index - 1)
        if self.pressed(keys.L):
            s.next_rig()
        if self.pressed(keys.C):
            modes = ["turntable", "cinematic", "free"]
            s.camera_mode = modes[(modes.index(s.camera_mode) + 1) % len(modes)]
            s.shot_time = 0.0
            print("[museum] camera:", s.camera_mode)
        if self.pressed(keys.G):
            s.next_shadows()
        if self.pressed(keys.T):
            s.spinning = not s.spinning
        if self.pressed(keys.R):
            s.reflections = not s.reflections
            engine.set_custom_float("showroom.reflections", 1.0 if s.reflections else 0.0)
            print("[museum] reflections", "on" if s.reflections else "off",
                  "" if s.preset in ("hybrid", "raytraced") else "(they need ray-traced lights: G)")
        if self.pressed(keys.K):
            s.next_paint()
        if self.pressed(keys.Y):
            s.turn_model()
        if self.pressed(keys.F):
            s.toggle_floor()
        if self.pressed(keys.V):
            s.debug_view = (s.debug_view + 1) % len(DEBUG_VIEWS)
            engine.set_custom_uint("default.debugView", s.debug_view)
            print("[museum] debug view:", DEBUG_VIEWS[s.debug_view])
        if self.pressed(keys.H):
            s.overlay.visible = not s.overlay.visible
            s.overlay.apply()
        if self.pressed(keys.F1):
            s.overlay.help_visible = not s.overlay.help_visible
            s.overlay.apply()
        if self.pressed(keys.O):
            s.stats.toggle()
        if self.pressed(keys.M):
            s.next_tonemapper()
        if self.pressed(keys.X):
            s.toggle_clearcoat()
        if self.pressed(keys.B):
            s.next_dof()
        s.stats.update(dt)
        slower, faster = self.pressed(keys.MINUS), self.pressed(keys.EQUAL)
        if slower or faster:
            speed = s.plate.faster(1.5 if faster else 1.0 / 1.5)
            s.overlay.toast("Turntable %.2gx" % speed + ("" if s.spinning else " (off: T)"))
        if self.pressed(keys.I):
            s.overlay.toast(None)
            still_capture.start()
        if self.pressed(keys.P):
            screenshot.request()
        if self.pressed(keys.F9):
            if engine.is_recording:
                engine.stop_recording()
                print("[museum] recording stopped")
            else:
                path = capture_path(".mp4")
                ok = engine.start_recording(path, fps=args.fps)
                print("[museum] recording ->", path if ok else "failed (see the error above)")


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
                print("[museum] still ->", path)
                if accumulation.on:
                    accumulation.end()
        if self.job < 0 or self.frames >= args.settle + (samples if samples > 1 else 0) + 1:
            self.job += 1
            self.frames = 0
            if self.job >= len(self.jobs):
                print("[museum] %d stills in %s" % (len(self.jobs), self.directory))
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
        line = "[museum] recording: %d / %d frames (%d%%)" % (done, self.total, 100 * done // max(self.total, 1))
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
                print("[museum] could not start recording (see the error above)")
                os._exit(1)
            print("[museum] recording ->", self.path)
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
                    print("[museum] tour recorded: %s" % self.path)
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
        print("[museum] recording %s after %d frames (%s): %s" % (
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
        print("[museum] rendering at %gx the window's size" % engine.render_scale)
    showroom.start()
    if args.screenshots:
        automation = Stills(args.screenshots)
    elif args.record:
        showroom.camera_mode = "cinematic"
        automation = Tour(os.path.abspath(args.record))

    def animate(dt):
        if quit_requested:
            finish("stopped")
        showroom.overlay.update_toast()
        screenshot.update()
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

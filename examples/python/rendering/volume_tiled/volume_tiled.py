"""
Shoonyakasha Volume Tiled Forward Shading

Jeremiah van Oosten's Volume Tiled Forward Shading (2015) on its own
pipeline (pipeline/), with up to a million point lights moving through
Crytek Sponza, the scene the paper measured in. Each frame:
- a depth prepass, and the clusters (volume tiles, 64 x 64 pixels across,
  sliced in depth so each is about a cube) that visible pixels and
  transparent fragments lie in are marked and gathered into a list;
- the lights are given 30-bit Morton codes, sorted (a radix sort of chunks
  of 256, then Merge Path merges) and built into a 32-ary BVH;
- one workgroup per active cluster walks the BVH and lists the lights that
  reach it; with the BVH off, it tests every light, the paper's method
  without it;
- the scene is shaded forward, each pixel with its cluster's lights.

A panel at the left sets the lights and shows each stage's GPU time.

Keys:
    WASD/Q/E + right mouse   fly the camera
    + / -                    twice or half the lights
    B                        light assignment through the BVH, or testing
                             every light
    V                        next view: image, lights per cluster, clusters,
                             reference (every light at every pixel, no
                             clusters: slow, for a few thousand lights)
    M                        lights moving or still
    F2                       the panel on or off
    P                        screenshot into volume_tiled_captures/

Benchmark, the paper's measurement, at a fixed view and density (each
light's range shrinks as their number grows, so about the same number reach
each point), with and without the BVH:
    python volume_tiled.py --benchmark results.md

Sponza is Khronos's glTF sample of Crytek Sponza: clone
https://github.com/KhronosGroup/glTF-Sample-Assets into the repository root
(it is ignored by git), or pass --scene with any glTF and --box with the
volume to fill with lights. Without either, a stand-in colonnade of boxes.
"""

import argparse
import json
import math
import os
import struct
import sys
import time

import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.mathutil import look_rotation

HERE = os.path.dirname(os.path.abspath(__file__))
PIPELINE = os.path.join(HERE, "pipeline", "pipeline.json")
REPOSITORY = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
SPONZA = os.path.join(REPOSITORY, "glTF-Sample-Assets", "Models", "Sponza", "glTF", "Sponza.gltf")
BOX = "models/Box.gltf"

# Capacity of the light buffers: VTFS_MAX_LIGHTS in pipeline/shaders/vtfs.glsl.
MAX_LIGHTS = 1 << 20
MIN_LIGHTS = 1 << 8

# Where the lights go in Sponza: the atrium and its arcades, above the floor.
SPONZA_BOX = ((-14.0, 0.2, -6.0), (13.5, 10.5, 6.0))
STAND_IN_BOX = ((-14.0, 0.2, -6.0), (12.0, 6.0, 6.0))

# Camera views: (eye, target). The first is where the window opens and the
# benchmark measures.
VIEWS = [((-11.0, 2.0, -0.5), (8.0, 3.5, 0.5)),
         ((9.0, 0.6, 2.5), (-6.0, 9.0, -3.0))]

# Lights that reach a point, on average, at the range --benchmark and the
# panel's "Range for density" choose.
OVERLAP = 8.0

DEBUG_VIEWS = [(0, "Image"), (5, "Lights per cluster"), (7, "Clusters"), (8, "Reference")]

# GPU time by stage of the technique: pass names, by prefix.
STAGES = [
    ("Depth prepass", ("DepthPrepass",)),
    ("Light update", ("VtfsUpdateLights",)),
    ("Light bounds", ("VtfsReduce",)),
    ("Morton codes", ("VtfsMorton",)),
    ("Sort", ("VtfsRadixSort", "VtfsMergeSort")),
    ("BVH build", ("VtfsBvh",)),
    ("Cluster boxes", ("VtfsClusterAABBs",)),
    ("Mark clusters", ("VtfsReset", "VtfsMark")),
    ("Find unique", ("VtfsFindUnique",)),
    ("Assign lights", ("VtfsAssign",)),
    ("Forward shading", ("Sky", "Forward", "Transparent")),
]


def parse_args():
    parser = argparse.ArgumentParser(description="Volume Tiled Forward Shading",
                                     formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    parser.add_argument("--lights", type=int, default=16384, help="lights to start with (256 to 1048576)")
    parser.add_argument("--range", type=float, default=0.0,
                        help="each light's range in metres (default: about %g lights reach each point)" % OVERLAP)
    parser.add_argument("--intensity", type=float, default=1.0, help="each light's intensity")
    parser.add_argument("--no-bvh", action="store_true", help="assign lights by testing every one")
    parser.add_argument("--scene", help="glTF to light instead of Sponza")
    parser.add_argument("--box", type=float, nargs=6, metavar=("X0", "Y0", "Z0", "X1", "Y1", "Z1"),
                        help="the volume the lights fill (default: Sponza's interior, or the scene's bounds)")
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--benchmark", metavar="FILE",
                        help="measure every light count with and without the BVH, write a Markdown table, quit")
    parser.add_argument("--counts", type=int, nargs="+",
                        default=[1024, 4096, 16384, 65536, 262144, 1048576],
                        help="light counts for --benchmark")
    parser.add_argument("--all-up-to", type=int, default=65536,
                        help="largest light count --benchmark measures without the BVH (it slows down fast)")
    args = parser.parse_args()
    args.lights = max(MIN_LIGHTS, min(MAX_LIGHTS, args.lights))
    return args


args = parse_args()


# ── glTF bounds, for a scene given with --scene ──────────────────────────

def gltf_bounds(path):
    """World-space bounds of a glTF or GLB's meshes, from their accessors'
    min/max (which glTF requires for positions)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] == b"glTF":
        gltf = json.loads(data[20:20 + struct.unpack_from("<I", data, 12)[0]].decode("utf-8"))
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
                for corner in range(8):
                    p = [a["max"][i] if corner >> i & 1 else a["min"][i] for i in range(3)]
                    q = [sum(m[i][k] * v for k, v in enumerate(p + [1.0])) for i in range(3)]
                    for i in range(3):
                        lo[i], hi[i] = min(lo[i], q[i]), max(hi[i], q[i])
        for child in node.get("children", []):
            walk(child, m)

    identity = [[1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]
    scenes = gltf.get("scenes")
    roots = scenes[gltf.get("scene", 0)]["nodes"] if scenes else range(len(nodes))
    for root in roots:
        walk(root, identity)
    return tuple(lo), tuple(hi)


# ── The engine ───────────────────────────────────────────────────────────

sk.shaders.compile_dir(os.path.join(HERE, "pipeline", "shaders"))
engine = sk.Engine(
    title="Shoonyakasha - Volume Tiled Forward Shading",
    width=args.width, height=args.height,
    pipeline_json_path=PIPELINE,
    environment_color=(0.02, 0.025, 0.035),
)


def root_of(entity):
    while engine.scene.get_parent(entity) != sk.NULL_ENTITY:
        entity = engine.scene.get_parent(entity)
    return entity


def box(center, size, color):
    """A box of the given size centred on `center`, from the bundled unit cube."""
    result = engine.load_gltf_scene(BOX)
    for root in {root_of(e) for e in result.entities}:
        engine.scene.set_rotation(root, (0.0, 0.0, 0.0))     # the file turns the cube about X
        engine.scene.set_scale(root, size)
        engine.scene.set_position(root, center)
    for e in result.entities:
        engine.scene.set_material_vec4(e, "baseColorFactor", color)
        engine.scene.set_material_float(e, "metallicFactor", 0.0)
        engine.scene.set_material_float(e, "roughnessFactor", 0.8)


def build_colonnade():
    """Stand-in for Sponza: a floor, two rows of pillars under beams, walls."""
    stone = (0.55, 0.5, 0.42, 1.0)
    wall = (0.62, 0.55, 0.46, 1.0)
    box((0.0, -0.1, 0.0), (30.0, 0.2, 14.0), (0.45, 0.42, 0.38, 1.0))
    for side in (-3.5, 3.5):
        for x in range(-12, 12, 3):
            box((float(x), 2.5, side), (0.7, 5.0, 0.7), stone)
        box((0.0, 5.3, side), (26.0, 0.6, 1.2), stone)
        box((0.0, 3.0, side * 1.9), (30.0, 6.0, 0.4), wall)
    box((13.0, 3.0, 0.0), (0.4, 6.0, 14.0), wall)


def load_scene():
    """The scene, and the box its lights fill."""
    path = os.path.abspath(args.scene) if args.scene else SPONZA
    if os.path.isfile(path):
        result = engine.load_gltf_scene(path, max_texture_size=2048)
        if not result.success:
            print("[volume_tiled] could not load %s: %s" % (path, result.error))
            sys.exit(1)
        if args.box:
            return tuple(args.box[:3]), tuple(args.box[3:])
        return SPONZA_BOX if path == SPONZA else gltf_bounds(path)
    if args.scene:
        print("[volume_tiled] no such scene:", path)
        sys.exit(1)
    print("[volume_tiled] Sponza is not there (%s). Clone\n"
          "    https://github.com/KhronosGroup/glTF-Sample-Assets\n"
          "into the repository root for it; meanwhile a stand-in colonnade." % SPONZA)
    build_colonnade()
    return (tuple(args.box[:3]), tuple(args.box[3:])) if args.box else STAND_IN_BOX


# ── The lights ───────────────────────────────────────────────────────────

class Lights:
    """What the pipeline's VTFS passes read from scene.custom.vtfs.*: how
    many lights, how far they reach, how bright, where, and whether they
    move. The lights themselves are made on the GPU (vtfs_update_lights.comp)."""

    def __init__(self):
        self.count = args.lights
        self.box = SPONZA_BOX
        self.range = args.range
        self.intensity = args.intensity
        self.moving = True
        self.bvh = not args.no_bvh

    def volume(self):
        lo, hi = self.box
        return max((hi[0] - lo[0]) * (hi[1] - lo[1]) * (hi[2] - lo[2]), 1e-6)

    def range_for_density(self, count=None):
        """The range at which about OVERLAP lights reach each point."""
        return (3.0 * OVERLAP * self.volume() / (4.0 * math.pi * (count or self.count))) ** (1.0 / 3.0)

    def apply(self):
        lo, hi = self.box
        if self.range <= 0.0:
            self.range = self.range_for_density()
        engine.set_custom_uint("vtfs.lightCount", self.count)
        engine.set_custom_float("vtfs.lightRange", self.range)
        # Brighter as they get smaller, so the scene keeps its brightness.
        engine.set_custom_float("vtfs.lightIntensity", self.intensity * self.range * self.range)
        engine.set_custom_vec4("vtfs.boxMin", tuple(lo) + (0.0,))
        engine.set_custom_vec4("vtfs.boxMax", tuple(hi) + (0.0,))
        engine.set_custom_float("vtfs.motion", 0.5 * min(self.range, 1.0) if self.moving else 0.0)
        engine.set_custom_float("vtfs.heatScale", 4.0 * OVERLAP)
        engine.apply_pipeline_preset("bvh" if self.bvh else "all")


lights = Lights()


# ── Stats ────────────────────────────────────────────────────────────────

def stage_times(stats):
    """GPU milliseconds per entry of STAGES, and the frame's total."""
    times = {name: 0.0 for name, _ in STAGES}
    for p in stats["passes"]:
        for name, prefixes in STAGES:
            if p["name"].startswith(prefixes):
                times[name] += p["gpu_ms"] or 0.0
                break
    return times, stats["gpu_ms"] or 0.0


def count_text(n):
    return "{:,}".format(n)


# ── The panel ────────────────────────────────────────────────────────────

INK = (0.92, 0.93, 0.95, 1.0)
MUTED = (0.62, 0.66, 0.72, 1.0)
GOLD = (0.95, 0.85, 0.55, 1.0)
FRAME = (0.07, 0.075, 0.09, 0.88)
SURFACE = (0.22, 0.24, 0.30, 1.0)
ACCENT = (0.30, 0.52, 0.85, 1.0)


class Panel:
    """A screen canvas at the left: the lights, the light assignment, the
    view, and each stage's GPU time, refreshed twice a second."""

    WIDTH = 440
    REFRESH = 0.5

    def __init__(self):
        self.visible = True
        self.wait = 0.0
        self.texts = {}

    def build(self):
        ui = engine.ui
        self.canvas = ui.create_canvas(reference_size=(1920, 1080))
        frame = ui.create_panel(self.canvas, color=FRAME)
        ui.set_rect(frame, anchor_min=(0, 0), anchor_max=(0, 1), pivot=(0, 0.5), position=(20, 0),
                    size=(self.WIDTH, -40))
        column = ui.create_vertical_layout(frame, spacing=8, padding=(20, 16, 20, 16))
        ui.set_rect(column, anchor_min=(0, 0), anchor_max=(1, 1), size=(0, 0))
        ui.set_layout_child_sizing(column, expand_width=True)
        serif = ui.load_font("fonts/PlayfairDisplay.ttf")
        ui.create_text(column, "Volume Tiled Forward Shading", size=28, color=GOLD, font=serif)
        ui.create_text(column, "van Oosten 2015. F2 hides this panel.", size=15, color=MUTED)

        self.count_label = ui.create_text(column, "", size=18)
        self.count = ui.create_slider(column, min=math.log2(MIN_LIGHTS), max=math.log2(MAX_LIGHTS),
                                      value=math.log2(lights.count), size=(0, 22))
        ui.set_slider_range(self.count, math.log2(MIN_LIGHTS), math.log2(MAX_LIGHTS), True)
        ui.set_accessible(self.count, "Lights")
        self.range_label = ui.create_text(column, "", size=18)
        self.range = ui.create_slider(column, min=0.05, max=4.0, value=lights.range, size=(0, 22))
        ui.set_accessible(self.range, "Light range")
        self.density = ui.create_button(column, "Range for about %g lights per point" % OVERLAP, size=(0, 34))
        ui.set_font_size(self.density, 16)
        self.intensity_label = ui.create_text(column, "", size=18)
        self.intensity = ui.create_slider(column, min=0.0, max=4.0, value=lights.intensity, size=(0, 22))
        ui.set_accessible(self.intensity, "Light intensity")
        self.moving = ui.create_toggle(column, "Lights moving", is_on=lights.moving, size=(0, 28))
        self.bvh = ui.create_toggle(column, "Light BVH (off: every light tested)", is_on=lights.bvh,
                                    size=(0, 28))

        ui.create_text(column, "View", size=16, color=MUTED)
        row = ui.create_horizontal_layout(column, size=(0, 34), spacing=6)
        ui.set_layout_child_sizing(row, expand_width=True)
        self.views = []
        for value, label in DEBUG_VIEWS:
            button = ui.create_button(row, label.split(" ")[0] if value == 5 else label, size=(60, 34))
            ui.set_font_size(button, 15)
            self.views.append((value, button))

        ui.create_element(column, (0, 6))
        self.header = ui.create_text(column, "", size=18, color=GOLD)
        self.summary = ui.create_text(column, "", size=15, color=MUTED)
        self.rows = []
        for name, _ in STAGES + [("Everything else", ())]:
            row = ui.create_horizontal_layout(column, size=(0, 22))
            ui.set_layout_child_sizing(row, expand_width=True)
            ui.create_text(row, name, size=16)
            right = ui.create_text(row, "", size=16)
            ui.set_text_align(right, sk.TEXT_ALIGN_RIGHT)
            self.rows.append((name, right))
        self.apply()

    def apply(self):
        engine.ui.set_visible(self.canvas, self.visible)

    def set_text(self, element, text):
        if self.texts.get(element) != text:
            self.texts[element] = text
            engine.ui.set_text(element, text)

    def update(self, dt, view):
        ui = engine.ui
        changed = False
        if ui.value_changed(self.count):
            lights.count = 1 << int(round(ui.slider_value(self.count)))
            changed = True
        if ui.value_changed(self.range):
            lights.range = ui.slider_value(self.range)
            changed = True
        if ui.was_clicked(self.density):
            lights.range = lights.range_for_density()
            changed = True
        if ui.value_changed(self.intensity):
            lights.intensity = ui.slider_value(self.intensity)
            changed = True
        if ui.value_changed(self.moving):
            lights.moving = ui.toggle_value(self.moving)
            changed = True
        if ui.value_changed(self.bvh):
            lights.bvh = ui.toggle_value(self.bvh)
            changed = True
        for value, button in self.views:
            if ui.was_clicked(button):
                view = value
        if changed:
            lights.apply()

        # The state, whichever way it changed.
        log2 = math.log2(lights.count)
        if abs(ui.slider_value(self.count) - log2) > 1e-3:
            ui.set_slider_value(self.count, log2)
        if abs(ui.slider_value(self.range) - lights.range) > 1e-4:
            ui.set_slider_value(self.range, lights.range)
        for toggle, on in ((self.moving, lights.moving), (self.bvh, lights.bvh)):
            if ui.toggle_value(toggle) != on:
                ui.set_toggle_value(toggle, on)
        for value, button in self.views:
            ui.set_color(button, ACCENT if value == view else SURFACE)
        self.set_text(self.count_label, "Lights   %s" % count_text(lights.count))
        self.set_text(self.range_label, "Range   %.2f m" % lights.range)
        self.set_text(self.intensity_label, "Intensity   %.2f" % lights.intensity)

        self.wait -= dt
        if self.wait <= 0.0:
            self.wait = self.REFRESH
            self.show_stats()
        return view

    def show_stats(self):
        stats = engine.render_stats
        if not stats or stats["fps"] == 0.0 or stats["gpu_ms"] is None:
            self.set_text(self.header, "Measuring ...")
            return
        times, total = stage_times(stats)
        self.set_text(self.header, "GPU %.2f ms   %.0f FPS" % (total, stats["fps"]))
        self.set_text(self.summary, "%s, %s   %d x %d" % (
            count_text(lights.count) + " lights", "BVH" if lights.bvh else "every light tested",
            args.width, args.height))
        rest = total - sum(times.values())
        for name, right in self.rows:
            ms = times.get(name, rest)
            self.set_text(right, "%.3f ms" % ms)


# ── Interactive and benchmark runs ───────────────────────────────────────

def capture_path():
    directory = os.path.join(os.getcwd(), "volume_tiled_captures")
    os.makedirs(directory, exist_ok=True)
    return os.path.join(directory, time.strftime("volume_tiled_%Y%m%d_%H%M%S.png"))


class Controls:
    def __init__(self):
        self.pressed = keys.KeyEdges(engine.input).pressed
        self.panel = Panel()
        self.view = 0

    def set_view(self, view):
        if view != self.view:
            self.view = view
            engine.set_custom_uint("default.debugView", view)
            print("[volume_tiled] view:", dict(DEBUG_VIEWS)[view])

    def update(self, dt):
        if self.pressed(keys.EQUAL) or self.pressed(keys.KP_ADD):
            lights.count = min(lights.count * 2, MAX_LIGHTS)
            lights.apply()
        if self.pressed(keys.MINUS) or self.pressed(keys.KP_SUBTRACT):
            lights.count = max(lights.count // 2, MIN_LIGHTS)
            lights.apply()
        if self.pressed(keys.B):
            lights.bvh = not lights.bvh
            lights.apply()
        if self.pressed(keys.M):
            lights.moving = not lights.moving
            lights.apply()
        view = self.view
        if self.pressed(keys.V):
            values = [v for v, _ in DEBUG_VIEWS]
            view = values[(values.index(view) + 1) % len(values)]
        if self.pressed(keys.F2):
            self.panel.visible = not self.panel.visible
            self.panel.apply()
        if self.pressed(keys.P):
            path = capture_path()
            print("[volume_tiled] screenshot ->", path if engine.capture_screenshot(path) else "failed")
        self.set_view(self.panel.update(dt, view))


class Benchmark:
    """--benchmark: each light count, with the BVH and (up to --all-up-to)
    without, at the first view, the range set for about OVERLAP lights per
    point. Each runs SETTLE seconds, then the GPU times of MEASURE seconds of
    frames are averaged."""

    SETTLE = 1.5
    MEASURE = 3.0

    def __init__(self, path):
        self.path = os.path.abspath(path)
        self.runs = [(n, bvh) for n in args.counts for bvh in (True, False)
                     if bvh or n <= args.all_up_to]
        self.run = -1
        self.clock = 0.0
        self.samples = []
        self.results = []
        self.next_sample = 0.0

    def start(self, index):
        self.run = index
        count, bvh = self.runs[index]
        lights.count = max(1, min(count, MAX_LIGHTS))
        lights.range = lights.range_for_density()
        lights.bvh = bvh
        lights.apply()
        self.clock = 0.0
        self.samples = []
        self.next_sample = self.SETTLE + 1.05
        print("[volume_tiled] %s lights, %s, range %.3f m ..." % (
            count_text(lights.count), "BVH" if bvh else "every light", lights.range), flush=True)

    def update(self, dt):
        if self.run < 0:
            self.start(0)
            return
        self.clock += dt
        # render_stats covers the last whole second: one sample a second.
        if self.clock >= self.next_sample:
            stats = engine.render_stats
            if stats and stats["gpu_ms"] is not None:
                self.samples.append(stage_times(stats))
            self.next_sample += 1.0
        if self.clock >= self.SETTLE + self.MEASURE + 1.1:
            self.finish_run()

    def finish_run(self):
        count, bvh = self.runs[self.run]
        if self.samples:
            n = len(self.samples)
            times = {name: sum(s[0][name] for s in self.samples) / n for name, _ in STAGES}
            total = sum(s[1] for s in self.samples) / n
            self.results.append((count, bvh, lights.range, times, total))
            print("    GPU %.2f ms (assign %.3f, sort %.3f, shading %.3f)" % (
                total, times["Assign lights"], times["Sort"], times["Forward shading"]), flush=True)
        if self.run + 1 < len(self.runs):
            self.start(self.run + 1)
        else:
            self.write()
            os._exit(0)        # the engine has no quit call

    def write(self):
        columns = [name for name, _ in STAGES]
        lines = ["# Volume Tiled Forward Shading benchmark", "",
                 "%d x %d, Crytek Sponza (first view), about %g lights reaching each point; "
                 "GPU milliseconds per stage, averaged over %g s." % (
                     args.width, args.height, OVERLAP, self.MEASURE), "",
                 "| Lights | Assignment | Range (m) | " + " | ".join(columns) + " | Frame |",
                 "|---:|---|---:|" + "---:|" * (len(columns) + 1)]
        for count, bvh, light_range, times, total in self.results:
            lines.append("| %s | %s | %.3f | %s | %.2f |" % (
                count_text(count), "BVH" if bvh else "every light", light_range,
                " | ".join("%.3f" % times[c] for c in columns), total))
        with open(self.path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        print("[volume_tiled] results ->", self.path)


controls = Controls()
benchmark = Benchmark(args.benchmark) if args.benchmark else None


def on_init():
    lights.box = load_scene()
    eye, target = VIEWS[0]
    camera = engine.create_camera(pos=eye, fov=60.0, speed=4.0, near_plane=0.1, far_plane=100.0)
    engine.scene.set_rotation(camera, look_rotation(eye, target))
    # Night: a dim sky, so the lights carry the scene.
    engine.set_custom_float("default.iblIntensity", 0.05)
    lights.apply()
    engine.enable_render_stats()
    if benchmark is None:
        controls.panel.build()
    print("[volume_tiled] %s lights in %s .. %s, range %.2f m" % (
        count_text(lights.count), lights.box[0], lights.box[1], lights.range))


def on_update(dt):
    if benchmark is not None:
        benchmark.update(dt)
    else:
        controls.update(dt)


engine.set_on_init(on_init)
engine.set_on_update(on_update)

if __name__ == "__main__":
    engine.run()

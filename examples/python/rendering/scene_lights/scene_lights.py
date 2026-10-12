"""
Shoonyakasha scene lights: thousands of lights on the default pipeline

Up to 4096 point lights scattered through Crytek Sponza, every one a scene
entity like any other, plus four spot lights with shadows sweeping the
arcades. The default pipeline reads the scene's lights from a storage
buffer the engine refills every frame ("SceneLights", up to 8192), so the
scene is no longer held to 128; its light clustering is unchanged, and its
limit of 63 lights per cluster shows in the lights-per-cluster view.

The lights are made in one call, engine.create_point_lights, and the panel
shows how long that took, the frame rate, and the GPU time of the passes the
lights cost.

Keys:
    WASD/Q/E + right mouse   fly the camera
    + / -                    twice or half the lights (128 to 4096)
    V                        next debug view (final image, cascades, shadow
                             mask, normals, ambient occlusion, lights per
                             cluster, motion vectors)
    P                        screenshot into scene_lights_captures/

Benchmark: each light count, at the first view, the GPU and frame times:
    python scene_lights.py --benchmark results.md

Sponza is Khronos's glTF sample of Crytek Sponza: clone
https://github.com/KhronosGroup/glTF-Sample-Assets into the repository root
(it is ignored by git). Without it, a stand-in colonnade of boxes.
"""

import argparse
import colorsys
import math
import os
import random
import time

import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.mathutil import look_rotation

HERE = os.path.dirname(os.path.abspath(__file__))
REPOSITORY = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
SPONZA = os.path.join(REPOSITORY, "glTF-Sample-Assets", "Models", "Sponza", "glTF", "Sponza.gltf")
BOX = "models/Box.gltf"

# The most point lights the example makes; the default pipeline's
# "SceneLights" buffer holds 8192 lights.
MAX_LIGHTS = 4096
MIN_LIGHTS = 128

# Where the lights go: Sponza's atrium and arcades, above the floor.
LIGHT_BOX = ((-14.0, 0.2, -6.0), (13.5, 10.0, 6.0))
EYE, TARGET = (-11.0, 2.0, -0.5), (8.0, 3.5, 0.5)

# Lights reaching a point on average, which sets each light's range.
OVERLAP = 4.0

DEBUG_VIEWS = ["final image", "cascades", "shadow mask", "normals", "ambient occlusion",
               "lights per cluster", "motion vectors"]

# The passes whose GPU time the lights drive.
LIGHT_PASSES = ("LightClusters", "Lighting", "SpotShadow", "PointShadow")


def parse_args():
    parser = argparse.ArgumentParser(description="Thousands of scene lights on the default pipeline",
                                     formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    parser.add_argument("--lights", type=int, default=2048, help="point lights to start with (128 to 4096)")
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--benchmark", metavar="FILE",
                        help="measure every light count, write a Markdown table, quit")
    parser.add_argument("--counts", type=int, nargs="+", default=[128, 256, 512, 1024, 2048, 4096],
                        help="light counts for --benchmark")
    args = parser.parse_args()
    args.lights = max(MIN_LIGHTS, min(MAX_LIGHTS, args.lights))
    return args


args = parse_args()

engine = sk.Engine(title="Shoonyakasha - scene lights", width=args.width, height=args.height,
                   environment_color=(0.02, 0.025, 0.035))


def root_of(entity):
    while engine.scene.get_parent(entity) != sk.NULL_ENTITY:
        entity = engine.scene.get_parent(entity)
    return entity


def box(center, size, color):
    result = engine.load_gltf_scene(BOX)
    for root in {root_of(e) for e in result.entities}:
        engine.scene.set_rotation(root, (0.0, 0.0, 0.0))     # the file turns the cube about X
        engine.scene.set_scale(root, size)
        engine.scene.set_position(root, center)
    for e in result.entities:
        engine.scene.set_material_vec4(e, "baseColorFactor", color)
        engine.scene.set_material_float(e, "roughnessFactor", 0.8)


def load_scene():
    if os.path.isfile(SPONZA):
        engine.load_gltf_scene(SPONZA, max_texture_size=2048)
        return
    print("[scene_lights] Sponza is not there (%s). Clone https://github.com/KhronosGroup/glTF-Sample-Assets "
          "into the repository root for it; meanwhile a stand-in colonnade." % SPONZA)
    stone, wall = (0.55, 0.5, 0.42, 1.0), (0.62, 0.55, 0.46, 1.0)
    box((0.0, -0.1, 0.0), (30.0, 0.2, 14.0), (0.45, 0.42, 0.38, 1.0))
    for side in (-3.5, 3.5):
        for x in range(-12, 12, 3):
            box((float(x), 2.5, side), (0.7, 5.0, 0.7), stone)
        box((0.0, 5.3, side), (26.0, 0.6, 1.2), stone)
        box((0.0, 3.0, side * 1.9), (30.0, 6.0, 0.4), wall)


# ── The lights ───────────────────────────────────────────────────────────

class Lights:
    """The point lights, made and destroyed as their number changes, and four
    shadowed spot lights that turn."""

    def __init__(self):
        self.points = []
        self.spots = []
        self.created_ms = 0.0
        self.time = 0.0

    def range_for(self, count):
        lo, hi = LIGHT_BOX
        volume = (hi[0] - lo[0]) * (hi[1] - lo[1]) * (hi[2] - lo[2])
        return (3.0 * OVERLAP * volume / (4.0 * math.pi * count)) ** (1.0 / 3.0)

    def set_count(self, count):
        for e in self.points:
            engine.scene.destroy_entity(e)
        rng = random.Random(7)
        lo, hi = LIGHT_BOX
        positions = [tuple(rng.uniform(lo[i], hi[i]) for i in range(3)) for _ in range(count)]
        colors = [colorsys.hsv_to_rgb(rng.random(), 0.75, 1.0) for _ in range(count)]
        light_range = self.range_for(count)
        started = time.perf_counter()
        # Brighter as they get smaller, so the scene keeps its brightness.
        self.points = engine.create_point_lights(positions, colors, intensity=1.5 * light_range ** 2,
                                                 range=light_range)
        self.created_ms = (time.perf_counter() - started) * 1000.0
        print("[scene_lights] %d point lights, range %.2f m, made in %.1f ms" % (count, light_range,
                                                                                 self.created_ms))

    def make_spots(self):
        for i in range(4):
            e = engine.create_point_light(pos=(-9.0 + 6.0 * i, 9.0, 0.0), color=(1.0, 0.9, 0.75),
                                          intensity=120.0, range=18.0)
            engine.scene.set_light_type(e, sk.LIGHT_SPOT)
            engine.scene.set_light_cone(e, 18.0, 28.0)
            engine.scene.set_light_cast_shadows(e, True)
            self.spots.append(e)

    def update(self, dt):
        self.time += dt
        for i, e in enumerate(self.spots):
            a = self.time * 0.5 + i * 1.7
            eye = (-9.0 + 6.0 * i, 9.0, 0.0)
            engine.scene.set_rotation(e, look_rotation(eye, (eye[0] + 3.0 * math.cos(a), 0.0,
                                                             4.0 * math.sin(a))))


lights = Lights()


# ── The panel ────────────────────────────────────────────────────────────

class Panel:
    """A few lines of text at the top left: the lights, and what they cost."""

    def build(self):
        ui = engine.ui
        canvas = ui.create_canvas(reference_size=(1920, 1080))
        frame = ui.create_panel(canvas, size=(520, 230), color=(0.07, 0.075, 0.09, 0.85))
        ui.set_anchor(frame, (0, 0))
        ui.set_position(frame, (20, 20))
        self.text = ui.create_text(frame, "", size=20)
        ui.set_rect(self.text, anchor_min=(0, 0), anchor_max=(1, 1), size=(-36, -28))
        self.wait = 0.0

    def update(self, dt):
        self.wait -= dt
        if self.wait > 0.0:
            return
        self.wait = 0.5
        lines = ["%d point lights + 4 shadowed spots (made in %.1f ms)" % (len(lights.points), lights.created_ms),
                 "+ / - lights,  V view: %s" % DEBUG_VIEWS[controls.view]]
        stats = engine.render_stats
        if stats and stats["fps"] > 0.0 and stats["gpu_ms"] is not None:
            lines.append("%.0f FPS   frame %.2f ms   GPU %.2f ms" % (stats["fps"], stats["frame_time_ms"],
                                                                    stats["gpu_ms"]))
            times = pass_times(stats)
            lines += ["%s  %.3f ms" % (name, times[name]) for name in LIGHT_PASSES]
        engine.ui.set_text(self.text, "\n".join(lines))


def pass_times(stats):
    times = {name: 0.0 for name in LIGHT_PASSES}
    for p in stats["passes"]:
        for name in LIGHT_PASSES:
            if p["name"].startswith(name):
                times[name] += p["gpu_ms"] or 0.0
    return times


# ── Interactive and benchmark runs ───────────────────────────────────────

class Controls:
    def __init__(self):
        self.pressed = keys.KeyEdges(engine.input).pressed
        self.view = 0

    def update(self, dt):
        count = len(lights.points)
        if self.pressed(keys.EQUAL) or self.pressed(keys.KP_ADD):
            if count < MAX_LIGHTS:
                lights.set_count(min(count * 2, MAX_LIGHTS))
        if self.pressed(keys.MINUS) or self.pressed(keys.KP_SUBTRACT):
            if count > MIN_LIGHTS:
                lights.set_count(max(count // 2, MIN_LIGHTS))
        if self.pressed(keys.V):
            self.view = (self.view + 1) % len(DEBUG_VIEWS)
            engine.set_custom_uint("default.debugView", self.view)
        if self.pressed(keys.P):
            directory = os.path.join(os.getcwd(), "scene_lights_captures")
            os.makedirs(directory, exist_ok=True)
            path = os.path.join(directory, time.strftime("scene_lights_%Y%m%d_%H%M%S.png"))
            print("[scene_lights] screenshot ->", path if engine.capture_screenshot(path) else "failed")


class Benchmark:
    """--benchmark: each light count settles for SETTLE seconds, then the
    render stats of MEASURE seconds are averaged."""

    SETTLE = 1.5
    MEASURE = 3.0

    def __init__(self, path):
        self.path = os.path.abspath(path)
        self.counts = [max(1, min(n, MAX_LIGHTS)) for n in args.counts]
        self.run = -1
        self.results = []

    def start(self, index):
        self.run = index
        lights.set_count(self.counts[index])
        self.clock = 0.0
        self.next_sample = self.SETTLE + 1.05
        self.samples = []

    def update(self, dt):
        if self.run < 0:
            self.start(0)
            return
        self.clock += dt
        if self.clock >= self.next_sample:
            stats = engine.render_stats
            if stats and stats["gpu_ms"] is not None:
                self.samples.append((stats["frame_time_ms"], stats["gpu_ms"], stats["cpu_record_ms"],
                                     pass_times(stats)))
            self.next_sample += 1.0
        if self.clock < self.SETTLE + self.MEASURE + 1.1:
            return
        if self.samples:
            n = len(self.samples)
            mean = lambda k: sum(s[k] for s in self.samples) / n
            passes = {name: sum(s[3][name] for s in self.samples) / n for name in LIGHT_PASSES}
            self.results.append((self.counts[self.run], lights.created_ms, mean(0), mean(1), mean(2), passes))
            print("    frame %.2f ms, GPU %.2f ms" % (mean(0), mean(1)), flush=True)
        if self.run + 1 < len(self.counts):
            self.start(self.run + 1)
            return
        lines = ["# Scene lights benchmark", "",
                 "%d x %d, the default pipeline, Crytek Sponza (first view), the point lights' range set for about "
                 "%g reaching each point, 4 shadowed spot lights; milliseconds, averaged over %g s." % (
                     args.width, args.height, OVERLAP, self.MEASURE), "",
                 "| Point lights | Made in | Frame | GPU | CPU record | " + " | ".join(LIGHT_PASSES) + " |",
                 "|---:|---:|---:|---:|---:|" + "---:|" * len(LIGHT_PASSES)]
        for count, made, frame, gpu, cpu, passes in self.results:
            lines.append("| %d | %.1f | %.2f | %.2f | %.2f | %s |" % (
                count, made, frame, gpu, cpu, " | ".join("%.3f" % passes[p] for p in LIGHT_PASSES)))
        with open(self.path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        print("[scene_lights] results ->", self.path)
        os._exit(0)        # the engine has no quit call


controls = Controls()
panel = Panel()
benchmark = Benchmark(args.benchmark) if args.benchmark else None


def on_init():
    load_scene()
    camera = engine.create_camera(pos=EYE, fov=60.0, speed=4.0, near_plane=0.1, far_plane=100.0)
    engine.scene.set_rotation(camera, look_rotation(EYE, TARGET))
    engine.set_custom_float("default.iblIntensity", 0.05)     # night: the lights carry the scene
    lights.make_spots()
    engine.enable_render_stats()
    if benchmark is None:
        lights.set_count(args.lights)
        panel.build()


def on_update(dt):
    lights.update(dt)
    if benchmark is not None:
        benchmark.update(dt)
    else:
        controls.update(dt)
        panel.update(dt)


engine.set_on_init(on_init)
engine.set_on_update(on_update)

if __name__ == "__main__":
    engine.run()

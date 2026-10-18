"""
Shoonyakasha - Why gold is gold

Four polished spheres of gold, copper, silver and aluminium, path traced with
the wave optics example's spectral pipeline from each metal's measured
complex refractive index n + ik. A panel plots each metal's reflectance at
normal incidence, and the n and k of one of them, from the same tables the
renderer reads, with a wavelength cursor that reads off n, k and the
reflectance of each metal there. The single wavelength view renders the
light at the cursor's wavelength alone, in grey.

Keys:
    WASD/Q/E + right mouse   fly the camera; moving starts the average over
    Left / Right             move the wavelength cursor by 5 nm
    V                        the full spectrum / the cursor's wavelength alone
    M                        plot n and k of the next metal
    R                        start the average over
    F2                       hide or show the panel
    P                        save a screenshot to gold_captures/

Usage:
    python gold.py
    python gold.py --metal copper --wavelength 560
    python gold.py --single --wavelength 450           # the light at 450 nm alone
    python gold.py --samples 512 --screenshot out.png   # converge, save, quit

Requirements:
    - the shoonyakasha package installed (pip install .)
    - a GPU with ray queries
"""

import argparse
import json
import math
import os
import re
import struct
import tempfile
import time
import zlib

import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.mathutil import look_rotation

HERE = os.path.dirname(os.path.abspath(__file__))
WAVE_OPTICS = os.path.join(HERE, "..", "wave_optics")
PIPELINE = os.path.join(WAVE_OPTICS, "pipeline", "pipeline.json")
SHADERS = os.path.join(WAVE_OPTICS, "pipeline", "shaders")
BOX = "models/Box.gltf"

# The metals in the order of metals.glsl's tables, with their names, the
# colour of their curves in the plots, and a line on what the plot shows.
METALS = ("GOLD", "SILVER", "COPPER", "ALUMINIUM")
NAMES = {"GOLD": "Gold", "SILVER": "Silver", "COPPER": "Copper", "ALUMINIUM": "Aluminium"}
CURVE = {"GOLD": (0.96, 0.76, 0.26), "COPPER": (0.92, 0.46, 0.30), "SILVER": (0.88, 0.90, 0.95),
         "ALUMINIUM": (0.50, 0.68, 0.98)}
CAPTION = {
    "GOLD": "Below about 500 nm, light lifts electrons out of gold's filled d band into its conduction band and "
            "is absorbed: reflectance falls to about 0.4. Above it, the free electrons reflect nearly all of it. "
            "Relativistic effects on gold's 6s electrons bring that band edge down into the visible.",
    "COPPER": "Copper's d band edge lies lower in energy than gold's: absorption sets in below about 590 nm. "
              "More of the green is lost, and copper looks redder than gold.",
    "SILVER": "Silver's d band edge lies in the ultraviolet, near 320 nm. Across the visible range its free "
              "electrons reflect about 98% at every wavelength, so it looks white.",
    "ALUMINIUM": "Aluminium has an absorption band near 800 nm, just past the red end. Its reflectance falls "
                 "slightly towards the red, from 0.92 to 0.88, so it looks faintly bluish white.",
}
# Left to right in the scene.
ORDER = ("GOLD", "COPPER", "SILVER", "ALUMINIUM")
LAMBDA_MIN, LAMBDA_MAX = 380.0, 780.0

# Material type and metal codes of the wave optics pipeline's Materials buffer.
CONDUCTOR = 1

RADIUS = 0.28
SPACING = 0.72
EYE = (0.82, 0.78, 3.7)
TARGET = (0.82, 0.30, 0.0)
FOV = 40.0
LAMP = (1.6, 2.6, 2.4)

parser = argparse.ArgumentParser(description="Why gold is gold", formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog=__doc__)
parser.add_argument("--metal", choices=[m.lower() for m in METALS], default="gold",
                    help="the metal whose n and k the panel plots")
parser.add_argument("--wavelength", type=float, default=450.0, help="the cursor's wavelength in nm (380 to 780)")
parser.add_argument("--single", action="store_true",
                    help="render the light at the cursor's wavelength alone, in grey")
parser.add_argument("--roughness", type=float, default=0.04, help="GGX roughness of the spheres")
parser.add_argument("--sky", type=float, default=0.4, help="intensity of the sky's light")
parser.add_argument("--exposure", type=float, default=1.0, help="multiplies the image before the tone curve")
parser.add_argument("--samples", type=int, default=0, help="stop averaging at this many paths per pixel (0: never)")
parser.add_argument("--screenshot", metavar="FILE", help="save a screenshot once --samples paths are in, then quit")
parser.add_argument("--width", type=int, default=1600)
parser.add_argument("--height", type=int, default=900)
args = parser.parse_args()
if args.screenshot and not args.samples:
    parser.error("--screenshot needs --samples")


# ── The metals' data ─────────────────────────────────────────────────────

def read_metals():
    """{name: [(n, k)] at 380, 400, ... 780 nm}, from metals.glsl."""
    with open(os.path.join(SHADERS, "metals.glsl"), encoding="utf-8") as f:
        source = f.read()
    tables = {}
    for name, body in re.findall(r"const vec2 (\w+)_IOR\[21\] = vec2\[21\]\((.*?)\);", source, re.S):
        tables[name] = [(float(n), float(k)) for n, k in re.findall(r"vec2\(([-\d.]+), ([-\d.]+)\)", body)]
    return tables


TABLES = read_metals()


def ior(metal, wavelength):
    """n, k at a wavelength, interpolated linearly as the shader does."""
    x = (min(max(wavelength, LAMBDA_MIN), LAMBDA_MAX) - LAMBDA_MIN) / 20.0
    i = min(int(x), 19)
    t = x - i
    (n0, k0), (n1, k1) = TABLES[metal][i], TABLES[metal][i + 1]
    return n0 + (n1 - n0) * t, k0 + (k1 - k0) * t


def reflectance(n, k):
    """Reflectance at normal incidence from air onto n + ik."""
    return ((n - 1.0) ** 2 + k * k) / ((n + 1.0) ** 2 + k * k)


def cie_xyz(wavelength):
    """The CIE 1931 colour matching functions, as spectral.glsl fits them."""
    def lobe(x, mu, s1, s2):
        t = (x - mu) / (s1 if x < mu else s2)
        return math.exp(-0.5 * t * t)
    x = (1.056 * lobe(wavelength, 599.8, 37.9, 31.0) + 0.362 * lobe(wavelength, 442.0, 16.0, 26.7)
         - 0.065 * lobe(wavelength, 501.1, 20.4, 26.2))
    y = 0.821 * lobe(wavelength, 568.8, 46.9, 40.5) + 0.286 * lobe(wavelength, 530.9, 16.3, 31.1)
    z = 1.217 * lobe(wavelength, 437.0, 11.8, 36.0) + 0.681 * lobe(wavelength, 459.0, 26.0, 13.8)
    return x, y, z


def xyz_to_linear_rgb(x, y, z):
    return (3.2406 * x - 1.5372 * y - 0.4986 * z, -0.9689 * x + 1.8758 * y + 0.0415 * z,
            0.0557 * x - 0.2040 * y + 1.0570 * z)


def encode_srgb(c):
    c = min(max(c, 0.0), 1.0)
    return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1.0 / 2.4) - 0.055


def metal_colour(metal):
    """sRGB of the metal's normal reflectance under an equal-energy light,
    with the flat spectrum white, as the renderer's colour conversion has it."""
    total, white = [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]
    for step in range(401):
        wavelength = LAMBDA_MIN + step
        r = reflectance(*ior(metal, wavelength))
        rgb = xyz_to_linear_rgb(*cie_xyz(wavelength))
        for c in range(3):
            total[c] += r * rgb[c]
            white[c] += rgb[c]
    return tuple(encode_srgb(t / w) for t, w in zip(total, white))


def wavelength_colour(wavelength):
    """An sRGB colour for a wavelength: the hue of the colour matching
    functions at full saturation (that of 420 or 660 nm beyond them), dimmed
    towards the ends of the visible range."""
    rgb = [max(c, 0.0) for c in xyz_to_linear_rgb(*cie_xyz(min(max(wavelength, 420.0), 660.0)))]
    peak = max(rgb) or 1.0
    fade = (1.0 - 0.7 * min(1.0, max(0.0, (420.0 - wavelength) / 40.0))
            - 0.7 * min(1.0, max(0.0, (wavelength - 660.0) / 120.0)))
    return tuple(encode_srgb(c / peak * fade) for c in rgb)


# ── Plots, drawn into PNG files ──────────────────────────────────────────

class Raster:
    """An RGBA image with antialiased curves, saved as PNG."""

    def __init__(self, width, height):
        self.width, self.height = width, height
        self.pixels = [[0.0, 0.0, 0.0, 0.0] for _ in range(width * height)]

    def blend(self, x, y, colour, alpha):
        if 0 <= x < self.width and 0 <= y < self.height and alpha > 0.0:
            p = self.pixels[y * self.width + x]
            a = alpha + p[3] * (1.0 - alpha)
            for c in range(3):
                p[c] = (colour[c] * alpha + p[c] * p[3] * (1.0 - alpha)) / a
            p[3] = a

    def rect(self, x0, y0, x1, y1, colour, alpha=1.0):
        for y in range(int(y0), int(math.ceil(y1))):
            for x in range(int(x0), int(math.ceil(x1))):
                self.blend(x, y, colour, alpha)

    def curve(self, points, colour, width, dash=0.0):
        """A polyline `width` pixels wide; dash > 0 draws dashes of that length."""
        coverage = {}
        radius = 0.5 * width
        travelled = 0.0
        for (xa, ya), (xb, yb) in zip(points, points[1:]):
            length = math.hypot(xb - xa, yb - ya)
            steps = max(1, int(length / 0.3))
            for s in range(steps):
                t = s / steps
                travelled += length / steps
                if dash > 0.0 and (travelled // dash) % 2 == 1:
                    continue
                px, py = xa + (xb - xa) * t, ya + (yb - ya) * t
                for y in range(int(py - radius - 1), int(py + radius + 2)):
                    for x in range(int(px - radius - 1), int(px + radius + 2)):
                        d = math.hypot(x + 0.5 - px, y + 0.5 - py)
                        a = min(1.0, max(0.0, radius + 0.5 - d))
                        if a > coverage.get((x, y), 0.0):
                            coverage[(x, y)] = a
        for (x, y), a in coverage.items():
            self.blend(x, y, colour, a)

    def save(self, path):
        rows = bytearray()
        for y in range(self.height):
            rows.append(0)
            for p in self.pixels[y * self.width:(y + 1) * self.width]:
                rows.extend(int(round(min(max(c, 0.0), 1.0) * 255.0)) for c in p)

        def chunk(kind, data):
            return (struct.pack(">I", len(data)) + kind + data
                    + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))
        header = struct.pack(">IIBBBBB", self.width, self.height, 8, 6, 0, 0, 0)
        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(rows), 9))
                    + chunk(b"IEND", b""))


class Axes:
    """Where values go in a plot of `size` canvas units, drawn at `scale`
    pixels per unit: wavelength across, `top` at the top of the y axis."""

    LEFT, RIGHT, TOP, BOTTOM = 46.0, 12.0, 30.0, 40.0
    STRIP = 9.0

    def __init__(self, size, top, scale=2):
        self.width, self.height = size
        self.top = top
        self.scale = scale

    def x(self, wavelength):
        span = self.width - self.LEFT - self.RIGHT
        return self.LEFT + span * (wavelength - LAMBDA_MIN) / (LAMBDA_MAX - LAMBDA_MIN)

    def y(self, value):
        span = self.height - self.TOP - self.BOTTOM
        return self.TOP + span * (1.0 - value / self.top)

    def draw_frame(self, raster, ticks):
        s = self.scale
        grid = (1.0, 1.0, 1.0)
        for value in ticks:
            y = self.y(value) * s
            raster.rect(self.LEFT * s, y - 0.5 * s, (self.width - self.RIGHT) * s, y + 0.5 * s, grid, 0.12)
        for wavelength in range(400, 800, 100):
            x = self.x(wavelength) * s
            raster.rect(x - 0.5 * s, self.TOP * s, x + 0.5 * s, self.y(0.0) * s, grid, 0.12)
        # The visible spectrum under the wavelength axis.
        y0 = (self.y(0.0) + 3.0) * s
        for px in range(int(self.x(LAMBDA_MIN) * s), int(self.x(LAMBDA_MAX) * s)):
            wavelength = LAMBDA_MIN + (px / s - self.LEFT) / (self.width - self.LEFT - self.RIGHT) * 400.0
            raster.rect(px, y0, px + 1, y0 + self.STRIP * s, wavelength_colour(wavelength))

    def polyline(self, values):
        """Pixel points of a curve given as a function of wavelength."""
        return [(self.x(w) * self.scale, self.y(values(w)) * self.scale)
                for w in [LAMBDA_MIN + i for i in range(0, 401, 2)]]


REFLECTANCE_SIZE = (600.0, 250.0)
NK_SIZE = (600.0, 210.0)


def nk_top(metal):
    return float(math.ceil(max(max(n, k) for n, k in TABLES[metal]) + 0.2))


def draw_plots(directory):
    """The reflectance plot and one n, k plot per metal; returns their paths."""
    paths = {}
    axes = Axes(REFLECTANCE_SIZE, 1.0)
    raster = Raster(int(REFLECTANCE_SIZE[0] * axes.scale), int(REFLECTANCE_SIZE[1] * axes.scale))
    axes.draw_frame(raster, [0.25 * i for i in range(5)])
    for metal in ("ALUMINIUM", "SILVER", "COPPER", "GOLD"):
        raster.curve(axes.polyline(lambda w, m=metal: reflectance(*ior(m, w))), CURVE[metal], 2.0 * axes.scale)
    paths["reflectance"] = os.path.join(directory, "reflectance.png")
    raster.save(paths["reflectance"])
    for metal in METALS:
        axes = Axes(NK_SIZE, nk_top(metal))
        raster = Raster(int(NK_SIZE[0] * axes.scale), int(NK_SIZE[1] * axes.scale))
        axes.draw_frame(raster, nk_ticks(metal))
        raster.curve(axes.polyline(lambda w, m=metal: ior(m, w)[0]), CURVE[metal], 2.0 * axes.scale)
        raster.curve(axes.polyline(lambda w, m=metal: ior(m, w)[1]), CURVE[metal], 2.0 * axes.scale,
                     dash=7.0 * axes.scale)
        paths[metal] = os.path.join(directory, "nk_%s.png" % metal.lower())
        raster.save(paths[metal])
    return paths


def nk_ticks(metal):
    top = nk_top(metal)
    step = 1.0 if top <= 6.0 else 2.0
    return [step * i for i in range(int(top / step) + 1)]


# ── Shapes, written as glTF ──────────────────────────────────────────────

def write_sphere(path, rings=48, segments=96):
    """A sphere of radius 1 about the origin, its buffer next to it."""
    positions, uvs, indices = [], [], []
    for r in range(rings + 1):
        theta = math.pi * r / rings
        for s in range(segments + 1):
            phi = 2.0 * math.pi * s / segments
            positions.append((math.sin(theta) * math.cos(phi), math.cos(theta), math.sin(theta) * math.sin(phi)))
            uvs.append((s / segments, r / rings))
    for r in range(rings):
        for s in range(segments):
            a = r * (segments + 1) + s
            b = a + segments + 1
            indices += [a, a + 1, b, a + 1, b + 1, b]
    blob = bytearray()
    views, accessors = [], []

    def add(data, target, accessor):
        while len(blob) % 4:
            blob.append(0)
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), "target": target})
        blob.extend(data)
        accessor["bufferView"] = len(views) - 1
        accessors.append(accessor)
        return len(accessors) - 1

    def floats(values, kind):
        flat = [c for v in values for c in v]
        accessor = {"componentType": 5126, "count": len(values), "type": kind}
        if kind == "VEC3":
            accessor["min"] = [min(v[i] for v in values) for i in range(3)]
            accessor["max"] = [max(v[i] for v in values) for i in range(3)]
        return add(struct.pack("<%df" % len(flat), *flat), 34962, accessor)

    index = add(struct.pack("<%dI" % len(indices), *indices), 34963,
                {"componentType": 5125, "count": len(indices), "type": "SCALAR"})
    attributes = {"POSITION": floats(positions, "VEC3"), "NORMAL": floats(positions, "VEC3"),
                  "TEXCOORD_0": floats(uvs, "VEC2")}
    gltf = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
            "nodes": [{"name": "sphere", "mesh": 0}],
            "meshes": [{"primitives": [{"attributes": attributes, "indices": index, "material": 0}]}],
            "materials": [{"pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1], "metallicFactor": 0.0,
                                                    "roughnessFactor": 0.5}}],
            "accessors": accessors, "bufferViews": views,
            "buffers": [{"uri": "sphere.bin", "byteLength": len(blob)}]}
    with open(os.path.join(os.path.dirname(path), "sphere.bin"), "wb") as f:
        f.write(blob)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(gltf, f)


FILES = tempfile.mkdtemp(prefix="gold_")
SPHERE = os.path.join(FILES, "sphere.gltf")
write_sphere(SPHERE)
PLOTS = draw_plots(FILES)

sk.shaders.compile_dir(SHADERS)
engine = sk.Engine(title="Shoonyakasha - Why gold is gold", width=args.width, height=args.height,
                   hdr_environment_path="env/kloofendal_misty_1k.hdr", pipeline_json_path=PIPELINE)


# ── The scene ────────────────────────────────────────────────────────────

def root_of(entity):
    parent = engine.scene.get_parent(entity)
    while parent != sk.NULL_ENTITY:
        entity, parent = parent, engine.scene.get_parent(parent)
    return entity


def place(path, position, scale, color=(0.8, 0.8, 0.8), roughness=0.8):
    """A glTF model placed in the scene; returns its entities."""
    result = engine.load_gltf_scene(path)
    for root in {root_of(entity) for entity in result.entities}:
        engine.scene.set_rotation(root, (0.0, 0.0, 0.0))
        engine.scene.set_scale(root, scale)
        engine.scene.set_position(root, position)
    for entity in result.entities:
        engine.scene.set_material_vec4(entity, "baseColorFactor", tuple(color) + (1.0,))
        engine.scene.set_material_float(entity, "metallicFactor", 0.0)
        engine.scene.set_material_float(entity, "roughnessFactor", roughness)
    return list(result.entities)


def sphere_centre(index):
    return ((index - 1.5) * SPACING, RADIUS, 0.0)


def build_scene():
    camera = engine.create_camera(pos=EYE, fov=FOV, speed=2.0, near_plane=0.02, far_plane=100.0)
    engine.scene.set_rotation(camera, look_rotation(EYE, TARGET))
    lamp = engine.create_point_light(pos=LAMP, color=(1.0, 1.0, 1.0), intensity=12.0, range=100.0)
    engine.scene.set_light_source_radius(lamp, 0.4)
    place(BOX, (0.0, -0.05, 0.0), (10.0, 0.1, 10.0), color=(0.06, 0.06, 0.065))
    slots = []
    for index, metal in enumerate(ORDER):
        for entity in place(SPHERE, sphere_centre(index), (RADIUS,) * 3):
            slots.append(((entity, CONDUCTOR, METALS.index(metal) + 1, args.roughness), (1.0, 0.0, 1.0, 0.0),
                          (0.0, 0.0, 0.0, 1.0)))
    values = [v for slot in slots[:8] for v in slot]
    values += [(0.0, 0.0, 0.0, 0.0)] * (24 - len(values))
    for i, value in enumerate(values):
        engine.set_custom_vec4("wo.slot[%d]" % i, tuple(float(c) for c in value))
    engine.set_custom_uint("wo.maxBounces", 6)
    engine.set_custom_uint("wo.view", 0)
    engine.set_custom_uint("wo.lampSpectrum", 0)
    engine.set_custom_float("wo.exposure", args.exposure)
    engine.set_custom_float("wo.environmentIntensity", args.sky)
    engine.set_custom_float("wo.polariser", -1000.0)
    engine.set_custom_float("wo.maxSamples", float(args.samples))
    engine.set_custom_float("wo.indirectAngle", 5.0)


def project(point):
    """Canvas units (1920 x 1080) of a point seen from the starting camera."""
    forward = [t - e for t, e in zip(TARGET, EYE)]
    length = math.sqrt(sum(c * c for c in forward))
    forward = [c / length for c in forward]
    right = [-forward[2], 0.0, forward[0]]
    length = math.sqrt(sum(c * c for c in right))
    right = [c / length for c in right]
    up = [right[1] * forward[2] - right[2] * forward[1], right[2] * forward[0] - right[0] * forward[2],
          right[0] * forward[1] - right[1] * forward[0]]
    v = [p - e for p, e in zip(point, EYE)]
    depth = sum(a * b for a, b in zip(v, forward))
    half = math.tan(math.radians(FOV) * 0.5)
    x = sum(a * b for a, b in zip(v, right)) / (depth * half * 1920.0 / 1080.0)
    y = sum(a * b for a, b in zip(v, up)) / (depth * half)
    return (0.5 + 0.5 * x) * 1920.0, (0.5 - 0.5 * y) * 1080.0


# ── The panel ────────────────────────────────────────────────────────────

GOLDEN = (0.95, 0.85, 0.55, 1.0)
MUTED = (0.62, 0.66, 0.72, 1.0)
FRAME = (0.07, 0.075, 0.09, 0.9)
SURFACE = (0.22, 0.24, 0.30, 1.0)
ACCENT = (0.30, 0.52, 0.85, 1.0)


def hex_colour(rgb):
    return "#%02X%02X%02X" % tuple(int(round(c * 255.0)) for c in rgb)


class Panel:
    WIDTH = 660

    def __init__(self):
        self.metal = args.metal.upper()
        self.wavelength = min(max(args.wavelength, LAMBDA_MIN), LAMBDA_MAX)
        self.single = args.single
        self.texts = {}
        self.visible = True

    def text_at(self, parent, text, position, size=15.0, color=MUTED, width=60.0, align=sk.TEXT_ALIGN_CENTER):
        """Text of `width` units in parent with its top at `position`: centred
        on it, or starting or ending there when aligned left or right."""
        ui = engine.ui
        element = ui.create_text(parent, text, size=size, color=color)
        pivot = {sk.TEXT_ALIGN_LEFT: 0.0, sk.TEXT_ALIGN_RIGHT: 1.0}.get(align, 0.5)
        ui.set_rect(element, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(pivot, 0.0), position=position,
                    size=(width, size + 6.0))
        ui.set_text_align(element, align)
        return element

    def plot(self, column, size, texture, top, ticks, label):
        """A plot image with its tick labels and a cursor line; returns (holder, image, cursor, axes)."""
        ui = engine.ui
        holder = ui.create_element(column, size=size)
        ui.set_layout_element(holder, min=size, preferred=size)
        image = ui.create_image(holder, size=size, texture=texture)
        ui.set_rect(image, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0), position=(0, 0), size=size)
        axes = Axes(size, top)
        for value in ticks:
            self.text_at(holder, ("%.2f" % value) if top <= 1.0 else "%g" % value,
                         (axes.LEFT - 6.0, axes.y(value) - 10.0), width=40.0, align=sk.TEXT_ALIGN_RIGHT)
        for wavelength in range(400, 800, 100):
            self.text_at(holder, "%d" % wavelength, (axes.x(wavelength), axes.y(0.0) + axes.STRIP + 6.0))
        self.text_at(holder, label, (axes.LEFT, 2.0), size=16.0, color=(0.85, 0.87, 0.9, 1.0), width=300.0,
                     align=sk.TEXT_ALIGN_LEFT)
        cursor = ui.create_panel(holder, size=(2, 2), color=(1.0, 1.0, 1.0, 0.7))
        return holder, image, cursor, axes

    def build(self):
        ui = engine.ui
        self.canvas = ui.create_canvas(reference_size=(1920, 1080))
        frame = ui.create_panel(self.canvas, color=FRAME)
        ui.set_rect(frame, anchor_min=(1, 0), anchor_max=(1, 1), pivot=(1, 0), position=(-20, 20),
                    size=(self.WIDTH, -40))
        column = ui.create_vertical_layout(frame, spacing=8, padding=(22, 16, 22, 16))
        ui.set_rect(column, anchor_min=(0, 0), anchor_max=(1, 1), size=(0, 0))
        ui.set_layout_child_sizing(column, expand_width=True)
        ui.create_text(column, "Why gold is gold", size=30, color=GOLDEN,
                       font=ui.load_font("fonts/PlayfairDisplay.ttf"))
        ui.create_text(column, "From each metal's measured refractive index n + ik. F2 hides this panel.",
                       size=15, color=MUTED)

        self.reflectance = self.plot(column, REFLECTANCE_SIZE, PLOTS["reflectance"], 1.0,
                                     [0.25 * i for i in range(5)], "Reflectance at normal incidence")
        self.reflectance_dots = {}
        for metal in METALS:
            self.reflectance_dots[metal] = ui.create_panel(self.reflectance[0], size=(9, 9),
                                                           color=CURVE[metal] + (1.0,))

        row = ui.create_horizontal_layout(column, size=(0, 34), spacing=6)
        ui.set_layout_child_sizing(row, expand_width=True)
        self.buttons = {}
        for metal in ORDER:
            button = ui.create_button(row, NAMES[metal], size=(60, 34))
            ui.set_font_size(button, 15)
            swatch = ui.create_image(button, size=(16, 16), color=metal_colour(metal) + (1.0,))
            ui.set_rect(swatch, anchor_min=(0, 0.5), anchor_max=(0, 0.5), pivot=(0, 0.5), position=(9, 0),
                        size=(16, 16))
            self.buttons[metal] = button

        self.nk = {}
        for metal in METALS:
            self.nk[metal] = self.plot(column, NK_SIZE, PLOTS[metal], nk_top(metal), nk_ticks(metal),
                                       "Refractive index of %s: n (solid), k (dashed)" % NAMES[metal].lower())
        self.nk_dots = [ui.create_panel(self.nk[m][0], size=(9, 9), color=(1, 1, 1, 1)) for m in METALS
                        for _ in range(2)]
        ui.create_text(column, "R = ((n - 1)² + k²) / ((n + 1)² + k²)", size=16,
                       color=(0.85, 0.87, 0.9, 1.0))

        row = ui.create_horizontal_layout(column, size=(0, 32), spacing=10)
        ui.set_layout_child_sizing(row, expand_width=True)
        self.wavelength_label = ui.create_text(row, "", size=18)
        ui.set_rich_text(self.wavelength_label)
        ui.set_text_align(self.wavelength_label, sk.TEXT_ALIGN_LEFT, sk.TEXT_ALIGN_MIDDLE)
        self.single_button = ui.create_button(row, "Single wavelength", size=(190, 32))
        ui.set_font_size(self.single_button, 15)
        ui.set_layout_element(self.single_button, min=(190, 32), preferred=(190, 32), flexible=(0, 0))
        self.slider = ui.create_slider(column, min=LAMBDA_MIN, max=LAMBDA_MAX, value=self.wavelength, size=(0, 22))
        ui.set_accessible(self.slider, "Wavelength")
        self.readout = {}
        for metal in ORDER:
            row = ui.create_horizontal_layout(column, size=(0, 22))
            cells = []
            for width in (150, 100, 100, 100):
                cell = ui.create_text(row, "", size=17, color=CURVE[metal] + (1.0,))
                ui.set_rich_text(cell)
                ui.set_layout_element(cell, min=(width, 22), preferred=(width, 22))
                cells.append(cell)
            self.readout[metal] = cells
        self.caption = ui.create_text(column, "", size=17, color=(0.85, 0.87, 0.9, 1.0))
        ui.set_text_wrap(self.caption, True)
        ui.set_layout_element(self.caption, min=(0, 90), preferred=(0, 90))

        # Names under the spheres, where the starting camera sees them.
        self.labels = []
        for index, metal in enumerate(ORDER):
            x, y = project(tuple(c + d for c, d in zip(sphere_centre(index), (0.0, -RADIUS, 0.0))))
            self.labels.append(self.text_at(self.canvas, NAMES[metal], (x, y + 40.0), size=22.0,
                                            color=(0.93, 0.93, 0.95, 1.0), width=200.0))

    def set_text(self, element, text):
        if self.texts.get(element) != text:
            self.texts[element] = text
            engine.ui.set_text(element, text)

    def place_dot(self, dot, axes, wavelength, value):
        engine.ui.set_rect(dot, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0.5, 0.5),
                           position=(axes.x(wavelength), axes.y(value)), size=(9, 9))

    def update(self, camera_moved):
        ui = engine.ui
        if ui.value_changed(self.slider):
            self.wavelength = ui.slider_value(self.slider)
        for metal, button in self.buttons.items():
            if ui.was_clicked(button):
                self.metal = metal
        if ui.was_clicked(self.single_button):
            self.toggle_single()
        w = self.wavelength
        ui.set_slider_value(self.slider, w)
        ui.set_color(self.single_button, ACCENT if self.single else SURFACE)

        holder, _, cursor, axes = self.reflectance
        ui.set_rect(cursor, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0.5, 0), position=(axes.x(w), axes.TOP),
                    size=(2, axes.y(0.0) - axes.TOP))
        for metal, dot in self.reflectance_dots.items():
            self.place_dot(dot, axes, w, reflectance(*ior(metal, w)))
        for index, metal in enumerate(METALS):
            holder, _, cursor, axes = self.nk[metal]
            ui.set_visible(holder, metal == self.metal)
            ui.set_rect(cursor, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0.5, 0),
                        position=(axes.x(w), axes.TOP), size=(2, axes.y(0.0) - axes.TOP))
            n, k = ior(metal, w)
            self.place_dot(self.nk_dots[2 * index], axes, w, n)
            self.place_dot(self.nk_dots[2 * index + 1], axes, w, k)
            ui.set_color(self.nk_dots[2 * index], CURVE[metal] + (1.0,))
        for metal, button in self.buttons.items():
            ui.set_color(button, ACCENT if metal == self.metal else SURFACE)

        self.set_text(self.wavelength_label, "Wavelength   <b>%.0f nm</b>   %.2f eV" % (w, 1239.84 / w))
        hue = wavelength_colour(w) + (1.0,)
        ui.set_color(self.reflectance[2], hue)
        for metal in METALS:
            ui.set_color(self.nk[metal][2], hue)
        for metal, cells in self.readout.items():
            n, k = ior(metal, w)
            texts = (NAMES[metal], "n  %.2f" % n, "k  %.2f" % k, "R  %.2f" % reflectance(n, k))
            for cell, text in zip(cells, texts):
                self.set_text(cell, "<b>%s</b>" % text if metal == self.metal else text)
        self.set_text(self.caption, CAPTION[self.metal])
        for label in self.labels:
            ui.set_visible(label, not camera_moved)

    def toggle_single(self):
        self.single = not self.single

    def key(self):
        """What the image depends on: the wavelength while it is shown alone."""
        return (self.single, self.wavelength) if self.single else (False,)

    def step(self, nanometres):
        self.wavelength = min(max(self.wavelength + nanometres, LAMBDA_MIN), LAMBDA_MAX)

    def next_metal(self):
        self.metal = ORDER[(ORDER.index(self.metal) + 1) % len(ORDER)]

    def toggle(self):
        self.visible = not self.visible
        engine.ui.set_visible(self.canvas, self.visible)


# ── Accumulation ─────────────────────────────────────────────────────────

class Controls:
    """Starts the average over whenever the camera moves: wo.reset is 1 in
    that frame. Counts the paths per pixel since."""

    def __init__(self):
        self.pressed = keys.KeyEdges(engine.input).pressed
        self.panel = Panel()
        self.state = None
        self.start_state = None
        self.frames = 0

    def start(self):
        build_scene()
        self.panel.build()

    def update(self, dt):
        if self.pressed(keys.LEFT):
            self.panel.step(-5.0)
        if self.pressed(keys.RIGHT):
            self.panel.step(5.0)
        if self.pressed(keys.M):
            self.panel.next_metal()
        if self.pressed(keys.V):
            self.panel.toggle_single()
        if self.pressed(keys.F2):
            self.panel.toggle()
        if self.pressed(keys.P):
            save_screenshot(capture_path())
        restart = self.pressed(keys.R)

        camera = engine.scene.get_main_camera()
        state = (engine.scene.get_position(camera), engine.scene.get_rotation(camera))
        if self.start_state is None:
            self.start_state = state
        self.panel.update(state != self.start_state)
        engine.set_custom_float("wo.wavelength", self.panel.wavelength if self.panel.single else 0.0)
        state += self.panel.key()
        reset = restart or state != self.state
        self.state = state
        engine.set_custom_uint("wo.reset", 1 if reset else 0)
        self.frames = 1 if reset else self.frames + 1

        # A few frames past the last path, so the presented frame holds it.
        if args.screenshot and self.frames >= args.samples + 4:
            save_screenshot(args.screenshot)
            os._exit(0)        # the engine has no quit call


def capture_path():
    directory = os.path.join(os.getcwd(), "gold_captures")
    os.makedirs(directory, exist_ok=True)
    return os.path.join(directory, time.strftime("gold_%Y%m%d_%H%M%S.png"))


def save_screenshot(path):
    print("[gold] screenshot ->", path if engine.capture_screenshot(os.path.abspath(path)) else "failed", flush=True)


controls = Controls()


def on_init():
    if not engine.ray_query_supported():
        print("[gold] this device has no ray queries, which the path tracer needs.")
        os._exit(1)
    controls.start()


engine.set_on_init(on_init)
engine.set_on_update(controls.update)

if __name__ == "__main__":
    engine.run()

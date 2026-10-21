"""
Shared parts of the optics episodes (gold.py, brewster.py): the wave optics
pipeline's paths and settings, glTF shapes, plots drawn into RGBA images,
panel helpers and the accumulation of paths.
"""

import json
import math
import os
import struct
import time

import shoonyakasha as sk

HERE = os.path.dirname(os.path.abspath(__file__))
WAVE_OPTICS = os.path.join(HERE, "..", "wave_optics")
PIPELINE = os.path.join(WAVE_OPTICS, "pipeline", "pipeline.json")
SHADERS = os.path.join(WAVE_OPTICS, "pipeline", "shaders")
BOX = "models/Box.gltf"
ENVIRONMENT = "env/kloofendal_misty_1k.hdr"

# Material types of the wave optics pipeline's Materials buffer.
CONDUCTOR, GRATING, FILM = 1, 2, 3

GOLDEN = (0.95, 0.85, 0.55, 1.0)
MUTED = (0.62, 0.66, 0.72, 1.0)
TEXT = (0.85, 0.87, 0.9, 1.0)
FRAME = (0.07, 0.075, 0.09, 0.9)
SURFACE = (0.22, 0.24, 0.30, 1.0)
ACCENT = (0.30, 0.52, 0.85, 1.0)

LAMBDA_MIN, LAMBDA_MAX = 380.0, 780.0


# ── Colour ───────────────────────────────────────────────────────────────

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


def wavelength_colour(wavelength):
    """An sRGB colour for a wavelength: the hue of the colour matching
    functions at full saturation (that of 420 or 660 nm beyond them), dimmed
    towards the ends of the visible range."""
    rgb = [max(c, 0.0) for c in xyz_to_linear_rgb(*cie_xyz(min(max(wavelength, 420.0), 660.0)))]
    peak = max(rgb) or 1.0
    fade = (1.0 - 0.7 * min(1.0, max(0.0, (420.0 - wavelength) / 40.0))
            - 0.7 * min(1.0, max(0.0, (wavelength - 660.0) / 120.0)))
    return tuple(encode_srgb(c / peak * fade) for c in rgb)


# ── Plots, drawn into RGBA images ────────────────────────────────────────

class Raster:
    """An RGBA image with antialiased curves, for a UI image."""

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

    def curve(self, points, colour, width, dash=0.0, alpha=1.0):
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
            self.blend(x, y, colour, a * alpha)

    def rgba(self):
        """The texels as 8-bit sRGB RGBA bytes, top row first."""
        return bytes(int(round(min(max(c, 0.0), 1.0) * 255.0)) for p in self.pixels for c in p)

    def show(self, ui, image):
        """Make this the texture of UI image `image`."""
        ui.set_texture_data(image, self.rgba(), self.width, self.height, "R8G8B8A8_SRGB")


class Axes:
    """Where values go in a plot of `size` canvas units drawn at `scale`
    pixels per unit: x from x_range[0] to x_range[1] across, y from 0 to
    `top`. With `spectrum`, x is a wavelength in nm and a strip of the
    visible spectrum lies under the axis."""

    LEFT, RIGHT, TOP, BOTTOM = 46.0, 12.0, 30.0, 40.0
    STRIP = 9.0

    def __init__(self, size, x_range, top, x_ticks, y_ticks, spectrum=False, scale=2):
        self.width, self.height = size
        self.x0, self.x1 = x_range
        self.top = top
        self.x_ticks, self.y_ticks = x_ticks, y_ticks
        self.spectrum = spectrum
        self.scale = scale

    def x(self, value):
        span = self.width - self.LEFT - self.RIGHT
        return self.LEFT + span * (value - self.x0) / (self.x1 - self.x0)

    def y(self, value):
        span = self.height - self.TOP - self.BOTTOM
        return self.TOP + span * (1.0 - value / self.top)

    def raster(self):
        """A raster for the plot with its grid drawn."""
        s = self.scale
        raster = Raster(int(self.width * s), int(self.height * s))
        grid = (1.0, 1.0, 1.0)
        for value in self.y_ticks:
            y = self.y(value) * s
            raster.rect(self.LEFT * s, y - 0.5 * s, (self.width - self.RIGHT) * s, y + 0.5 * s, grid, 0.12)
        for value in self.x_ticks:
            x = self.x(value) * s
            raster.rect(x - 0.5 * s, self.TOP * s, x + 0.5 * s, self.y(0.0) * s, grid, 0.12)
        if self.spectrum:
            y0 = (self.y(0.0) + 3.0) * s
            for px in range(int(self.x(self.x0) * s), int(self.x(self.x1) * s)):
                value = self.x0 + (px / s - self.LEFT) / (self.width - self.LEFT - self.RIGHT) * (self.x1 - self.x0)
                raster.rect(px, y0, px + 1, y0 + self.STRIP * s, wavelength_colour(value))
        return raster

    def polyline(self, function, step=None):
        """Pixel points of `function` over the x range."""
        step = step or (self.x1 - self.x0) / 200.0
        count = int(round((self.x1 - self.x0) / step))
        return [(self.x(self.x0 + i * step) * self.scale, self.y(function(self.x0 + i * step)) * self.scale)
                for i in range(count + 1)]

    def vertical(self, value):
        """Pixel points of a vertical line at x = value across the plot."""
        return [(self.x(value) * self.scale, self.TOP * self.scale),
                (self.x(value) * self.scale, self.y(0.0) * self.scale)]


# ── Shapes, written as glTF ──────────────────────────────────────────────

def write_gltf(path, positions, normals, uvs, indices):
    """One mesh of one primitive, its buffer next to it, under a single node."""
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
    attributes = {"POSITION": floats(positions, "VEC3"), "NORMAL": floats(normals, "VEC3"),
                  "TEXCOORD_0": floats(uvs, "VEC2")}
    base = os.path.splitext(os.path.basename(path))[0]
    gltf = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
            "nodes": [{"name": base, "mesh": 0}],
            "meshes": [{"primitives": [{"attributes": attributes, "indices": index, "material": 0}]}],
            "materials": [{"pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1], "metallicFactor": 0.0,
                                                    "roughnessFactor": 0.5}}],
            "accessors": accessors, "bufferViews": views,
            "buffers": [{"uri": base + ".bin", "byteLength": len(blob)}]}
    with open(os.path.join(os.path.dirname(path), base + ".bin"), "wb") as f:
        f.write(blob)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(gltf, f)


def sphere(rings=48, segments=96):
    """A sphere of radius 1 about the origin: positions, normals, uvs, indices."""
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
    return positions, list(positions), uvs, indices


# ── The scene ────────────────────────────────────────────────────────────

def root_of(engine, entity):
    parent = engine.scene.get_parent(entity)
    while parent != sk.NULL_ENTITY:
        entity, parent = parent, engine.scene.get_parent(parent)
    return entity


def place(engine, path, position, scale, color=(0.8, 0.8, 0.8), roughness=0.8):
    """A glTF model placed in the scene, unrotated; returns its entities."""
    result = engine.load_gltf_scene(path)
    for root in {root_of(engine, entity) for entity in result.entities}:
        engine.scene.set_rotation(root, (0.0, 0.0, 0.0))
        engine.scene.set_scale(root, scale)
        engine.scene.set_position(root, position)
    for entity in result.entities:
        engine.scene.set_material_vec4(entity, "baseColorFactor", tuple(color) + (1.0,))
        engine.scene.set_material_float(entity, "metallicFactor", 0.0)
        engine.scene.set_material_float(entity, "roughnessFactor", roughness)
    return list(result.entities)


def set_material_slots(engine, slots):
    """The pipeline's material slots: up to eight of three vec4s each."""
    values = [v for slot in slots[:8] for v in slot]
    values += [(0.0, 0.0, 0.0, 0.0)] * (24 - len(values))
    for i, value in enumerate(values):
        engine.set_custom_vec4("wo.slot[%d]" % i, tuple(float(c) for c in value))


def set_settings(engine, samples, sky, exposure, polariser=None, wavelength=0.0):
    """The pipeline's settings: the image view, six bounces, the lamp's own
    RGB colour; polariser in degrees from the image's horizontal, or None."""
    engine.set_custom_uint("wo.maxBounces", 6)
    engine.set_custom_uint("wo.view", 0)
    engine.set_custom_uint("wo.lampSpectrum", 0)
    engine.set_custom_float("wo.exposure", exposure)
    engine.set_custom_float("wo.environmentIntensity", sky)
    engine.set_custom_float("wo.polariser", -1000.0 if polariser is None else polariser)
    engine.set_custom_float("wo.maxSamples", float(samples))
    engine.set_custom_float("wo.indirectAngle", 5.0)
    engine.set_custom_float("wo.wavelength", wavelength)


def project(eye, target, fov, point, canvas=(1920.0, 1080.0)):
    """Canvas units of a point seen from a camera at eye looking at target,
    with vertical field of view fov in degrees and no roll."""
    forward = [t - e for t, e in zip(target, eye)]
    length = math.sqrt(sum(c * c for c in forward))
    forward = [c / length for c in forward]
    right = [-forward[2], 0.0, forward[0]]
    length = math.sqrt(sum(c * c for c in right))
    right = [c / length for c in right]
    up = [right[1] * forward[2] - right[2] * forward[1], right[2] * forward[0] - right[0] * forward[2],
          right[0] * forward[1] - right[1] * forward[0]]
    v = [p - e for p, e in zip(point, eye)]
    depth = sum(a * b for a, b in zip(v, forward))
    half = math.tan(math.radians(fov) * 0.5)
    x = sum(a * b for a, b in zip(v, right)) / (depth * half * canvas[0] / canvas[1])
    y = sum(a * b for a, b in zip(v, up)) / (depth * half)
    return (0.5 + 0.5 * x) * canvas[0], (0.5 - 0.5 * y) * canvas[1]


# ── The panel ────────────────────────────────────────────────────────────

def hex_colour(rgb):
    return "#%02X%02X%02X" % tuple(int(round(c * 255.0)) for c in rgb)


def text_at(ui, parent, text, position, size=15.0, color=MUTED, width=60.0, align=sk.TEXT_ALIGN_CENTER):
    """Text of `width` units in parent with its top at `position`: centred
    on it, or starting or ending there when aligned left or right."""
    element = ui.create_text(parent, text, size=size, color=color)
    pivot = {sk.TEXT_ALIGN_LEFT: 0.0, sk.TEXT_ALIGN_RIGHT: 1.0}.get(align, 0.5)
    ui.set_rect(element, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(pivot, 0.0), position=position,
                size=(width, size + 6.0))
    ui.set_text_align(element, align)
    return element


class Plot:
    """A plot image in a panel column, with its tick labels, a title and a
    cursor line; `axes` places values on it."""

    def __init__(self, ui, column, axes, raster, label, x_format="%g", y_format="%g"):
        self.ui, self.axes = ui, axes
        size = (axes.width, axes.height)
        self.holder = ui.create_element(column, size=size)
        ui.set_layout_element(self.holder, min=size, preferred=size)
        self.image = ui.create_image(self.holder, size=size)
        raster.show(ui, self.image)
        ui.set_rect(self.image, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0), position=(0, 0), size=size)
        for value in axes.y_ticks:
            text_at(ui, self.holder, y_format % value, (axes.LEFT - 6.0, axes.y(value) - 10.0), width=40.0,
                    align=sk.TEXT_ALIGN_RIGHT)
        below = axes.y(0.0) + (axes.STRIP + 6.0 if axes.spectrum else 4.0)
        for value in axes.x_ticks:
            text_at(ui, self.holder, x_format % value, (axes.x(value), below))
        self.title = text_at(ui, self.holder, label, (axes.LEFT, 2.0), size=16.0, color=TEXT, width=480.0,
                             align=sk.TEXT_ALIGN_LEFT)
        ui.set_rich_text(self.title)
        self.cursor = ui.create_panel(self.holder, size=(2, 2), color=(1.0, 1.0, 1.0, 0.7))

    def dot(self, color):
        return self.ui.create_panel(self.holder, size=(9, 9), color=color)

    def place_dot(self, dot, x, y):
        self.ui.set_rect(dot, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0.5, 0.5),
                         position=(self.axes.x(x), self.axes.y(y)), size=(9, 9))

    def place_cursor(self, x, color=None):
        a = self.axes
        self.ui.set_rect(self.cursor, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0.5, 0), position=(a.x(x), a.TOP),
                         size=(2, a.y(0.0) - a.TOP))
        if color is not None:
            self.ui.set_color(self.cursor, color)


def panel_frame(ui, width, title):
    """A screen canvas with a full-height panel on the right and its title;
    returns (canvas, column)."""
    canvas = ui.create_canvas(reference_size=(1920, 1080))
    frame = ui.create_panel(canvas, color=FRAME)
    ui.set_rect(frame, anchor_min=(1, 0), anchor_max=(1, 1), pivot=(1, 0), position=(-20, 20), size=(width, -40))
    column = ui.create_vertical_layout(frame, spacing=8, padding=(22, 16, 22, 16))
    ui.set_rect(column, anchor_min=(0, 0), anchor_max=(1, 1), size=(0, 0))
    ui.set_layout_child_sizing(column, expand_width=True)
    ui.create_text(column, title, size=30, color=GOLDEN, font=ui.load_font("fonts/PlayfairDisplay.ttf"))
    return canvas, column


def button_row(ui, column, labels, height=34):
    """A row of buttons sharing the column's width; returns them in order."""
    row = ui.create_horizontal_layout(column, size=(0, height), spacing=6)
    ui.set_layout_child_sizing(row, expand_width=True)
    buttons = []
    for label in labels:
        button = ui.create_button(row, label, size=(60, height))
        ui.set_font_size(button, 15)
        buttons.append(button)
    return buttons


class TextCache:
    """Sets an element's text only when it changes."""

    def __init__(self, ui):
        self.ui, self.texts = ui, {}

    def set(self, element, text):
        if self.texts.get(element) != text:
            self.texts[element] = text
            self.ui.set_text(element, text)


# ── Accumulation ─────────────────────────────────────────────────────────

class Accumulation:
    """Starts the average over whenever `state` changes (wo.reset is 1 in
    that frame) and, with a screenshot path, saves it a few frames after
    `samples` paths per pixel and quits."""

    def __init__(self, engine, samples, screenshot, name):
        self.engine, self.samples, self.screenshot, self.name = engine, samples, screenshot, name
        self.state = None
        self.frames = 0

    def update(self, state, restart=False):
        reset = restart or state != self.state
        self.state = state
        self.engine.set_custom_uint("wo.reset", 1 if reset else 0)
        self.frames = 1 if reset else self.frames + 1
        # A few frames past the last path, so the presented frame holds it.
        if self.screenshot and self.frames >= self.samples + 4:
            self.save(self.screenshot)
            os._exit(0)        # the engine has no quit call

    def capture(self):
        """A screenshot into <name>_captures/ in the working directory."""
        directory = os.path.join(os.getcwd(), self.name + "_captures")
        os.makedirs(directory, exist_ok=True)
        self.save(os.path.join(directory, time.strftime(self.name + "_%Y%m%d_%H%M%S.png")))

    def save(self, path):
        saved = self.engine.capture_screenshot(os.path.abspath(path))
        print("[%s] screenshot ->" % self.name, path if saved else "failed", flush=True)

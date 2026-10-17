"""
Shoonyakasha wave optics

A spectral, polarised path tracer in which a compact disc's grooves split the
light into rainbows and a thin film on a sphere shows interference colours,
each as far as the light reaching it is coherent. The light's coherence
comes from the size of its source as seen from the surface: a small, distant
lamp gives crisp rainbows, a large, close one blurs them. The lamp can shine
with the spectrum of a light bulb, a sodium or mercury lamp or an RGB LED:
the disc splits its lines into separate coloured spots, and sodium light
shows rings on films too thick to show colours under white light. Gold and silver spheres reflect with
their measured refractive indices, and a polariser in front of the camera
shows the polarisation of what reaches it. --scene puts one of Khronos's
glTF sample models with thin films (KHR_materials_iridescence) in their
place.

Keys:
    WASD/Q/E + right mouse   fly the camera; moving starts the average over
    1 / 2                    the image / the coherence width of the lamp's light
    L                        the next lamp spectrum
    R                        start the average over
    F2                       hide or show the panel
    P                        save a screenshot to wave_optics_captures/

Usage:
    python wave_optics.py
    python wave_optics.py --lamp-radius 0.2 --lamp-distance 1.0
    python wave_optics.py --lamp sodium --film 5
    python wave_optics.py --scene abalone      # needs glTF-Sample-Assets in the repository root
    python wave_optics.py --polariser 90 --samples 512 --screenshot out.png   # converge, save, quit

Requirements:
    - the shoonyakasha package installed (pip install .)
    - a GPU with ray queries
"""

import argparse
import json
import math
import os
import struct
import tempfile
import time

import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.mathutil import look_rotation

HERE = os.path.dirname(os.path.abspath(__file__))
PIPELINE = os.path.join(HERE, "pipeline", "pipeline.json")
REPOSITORY = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
SAMPLES = os.path.join(REPOSITORY, "glTF-Sample-Assets", "Models")
BOX = "models/Box.gltf"

EYE = (0.0, 1.35, 3.6)
TARGET = (0.0, 0.55, 0.0)
DISC_CENTER = (0.0, 0.62, -0.35)
# Khronos glTF sample models with thin films (KHR_materials_iridescence):
# the model's file, its scale, where its origin goes, the camera's eye and target.
MODELS = {
    "abalone": ("IridescenceAbalone/glTF/IridescenceAbalone.gltf", 5.0, (0.0, 0.4, 0.0),
                (0.0, 1.15, 2.1), (0.0, 0.35, 0.0)),
    "suzanne": ("IridescenceSuzanne/glTF/IridescenceSuzanne.gltf", 0.3, (0.0, 0.3, 0.0),
                (0.0, 1.0, 3.2), (0.0, 0.3, 0.0)),
    "metal-spheres": ("IridescenceMetallicSpheres/glTF/IridescenceMetallicSpheres.gltf", 0.07, (0.0, 0.82, 0.0),
                      (0.0, 0.95, 3.6), (0.0, 0.8, 0.0)),
}
SCENES = ("spheres",) + tuple(MODELS)
DISC_TILT = math.radians(62.0)          # from lying flat, towards the camera
LAMP_TOWARDS = (0.35, 0.8, 1.0)         # from the disc's centre towards the lamp
LAMP_IRRADIANCE = 3.0                   # at the disc's centre
SUN_DIRECTION = (-0.6, -1.0, -0.4)
VIEWS = ("Image", "Coherence")
# The lamp's spectra, in the order of lamps.glsl's LAMP_* values.
SPECTRA = ("white", "bulb", "sodium", "mercury", "led")
SPECTRUM_LABELS = ("White", "Bulb", "Sodium", "Mercury", "LED")

# Material types of the pipeline's Materials buffer.
CONDUCTOR, GRATING, FILM = 1, 2, 3
GOLD, SILVER, COPPER, ALUMINIUM = 0, 1, 2, 3

parser = argparse.ArgumentParser(description="Wave optics", formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog=__doc__)
parser.add_argument("--scene", choices=SCENES, default="spheres",
                    help="spheres: the disc, the film sphere and the metals; abalone, suzanne, metal-spheres: a "
                         "Khronos sample model with thin films, from glTF-Sample-Assets")
parser.add_argument("--lamp-radius", type=float, default=0.02, help="radius of the lamp in metres (0.002 to 0.4)")
parser.add_argument("--lamp-distance", type=float, default=2.0,
                    help="distance from the centre of the scene (the disc or the model) to the lamp in metres "
                         "(0.6 to 8)")
parser.add_argument("--lamp", choices=SPECTRA, default="white",
                    help="the lamp's spectrum: white (its RGB colour), bulb (2700 K), sodium, mercury, led (RGB)")
parser.add_argument("--film", type=float, default=0.45,
                    help="thickness of the sphere's film in micrometres (0.1 to 30)")
parser.add_argument("--pitch", type=float, default=1.6, help="spacing of the disc's grooves in micrometres")
parser.add_argument("--polariser", type=float, default=None,
                    help="angle in degrees of a polariser in front of the camera, from the image's horizontal")
parser.add_argument("--view", type=int, choices=(0, 1), default=0, help="0 the image, 1 the lamp's coherence width")
parser.add_argument("--indirect", type=float, default=5.0,
                    help="angular radius in degrees of the sources of light that does not come from a scene light")
parser.add_argument("--bounces", type=int, default=6, help="bounces after the first hit (1 to 12)")
parser.add_argument("--sky", type=float, default=0.15, help="intensity of the sky's light")
parser.add_argument("--exposure", type=float, default=1.0, help="multiplies the image before the tone curve")
parser.add_argument("--samples", type=int, default=0, help="stop averaging at this many paths per pixel (0: never)")
parser.add_argument("--screenshot", metavar="FILE", help="save a screenshot once --samples paths are in, then quit")
parser.add_argument("--width", type=int, default=1600)
parser.add_argument("--height", type=int, default=900)
args = parser.parse_args()
if args.screenshot and not args.samples:
    parser.error("--screenshot needs --samples")

sk.shaders.compile_dir(os.path.join(HERE, "pipeline", "shaders"))
engine = sk.Engine(title="Shoonyakasha - Wave optics", width=args.width, height=args.height,
                   hdr_environment_path="env/kloofendal_misty_1k.hdr", pipeline_json_path=PIPELINE)


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
                                                    "roughnessFactor": 0.5}, "doubleSided": True}],
            "accessors": accessors, "bufferViews": views,
            "buffers": [{"uri": base + ".bin", "byteLength": len(blob)}]}
    with open(os.path.join(os.path.dirname(path), base + ".bin"), "wb") as f:
        f.write(blob)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(gltf, f)


def sphere(rings=48, segments=96):
    """A sphere of radius 1 about the origin."""
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


def annulus(inner=0.2, segments=192):
    """A flat ring in the xz plane facing +y, from radius `inner` to 1."""
    positions, normals, uvs, indices = [], [], [], []
    for s in range(segments + 1):
        phi = 2.0 * math.pi * s / segments
        c, n = math.cos(phi), math.sin(phi)
        for r in (inner, 1.0):
            positions.append((r * c, 0.0, r * n))
            normals.append((0.0, 1.0, 0.0))
            uvs.append((0.5 + 0.5 * r * c, 0.5 + 0.5 * r * n))
    for s in range(segments):
        a = 2 * s
        indices += [a, a + 2, a + 1, a + 1, a + 2, a + 3]
    return positions, normals, uvs, indices


SHAPES = tempfile.mkdtemp(prefix="wave_optics_")
SPHERE = os.path.join(SHAPES, "sphere.gltf")
DISC = os.path.join(SHAPES, "disc.gltf")
write_gltf(SPHERE, *sphere())
write_gltf(DISC, *annulus())


# ── The scene ────────────────────────────────────────────────────────────

def root_of(entity):
    parent = engine.scene.get_parent(entity)
    while parent != sk.NULL_ENTITY:
        entity, parent = parent, engine.scene.get_parent(parent)
    return entity


def place(path, position, scale, rotation=(0.0, 0.0, 0.0), color=(0.8, 0.8, 0.8)):
    """A glTF model placed in the scene; returns its entities."""
    result = engine.load_gltf_scene(path)
    for root in {root_of(entity) for entity in result.entities}:
        engine.scene.set_rotation(root, rotation)
        engine.scene.set_scale(root, scale)
        engine.scene.set_position(root, position)
    for entity in result.entities:
        engine.scene.set_material_vec4(entity, "baseColorFactor", tuple(color) + (1.0,))
        engine.scene.set_material_float(entity, "metallicFactor", 0.0)
        engine.scene.set_material_float(entity, "roughnessFactor", 0.8)
    return list(result.entities)


def place_model(path, scale, origin):
    """A glTF model with its own materials, scaled about `origin`; returns its entities."""
    result = engine.load_gltf_scene(path)
    for root in {root_of(entity) for entity in result.entities}:
        position = engine.scene.get_position(root)
        engine.scene.set_position(root, tuple(o + scale * p for o, p in zip(origin, position)))
        engine.scene.set_scale(root, tuple(scale * s for s in engine.scene.get_scale(root)))
    return list(result.entities)


def normalized(v):
    length = math.sqrt(sum(c * c for c in v))
    return tuple(c / length for c in v)


class Scene:
    """The room, the objects with wave-optical materials, and the lamp."""

    def build(self):
        model = MODELS.get(args.scene)
        eye, target = (model[3], model[4]) if model else (EYE, TARGET)
        camera = engine.create_camera(pos=eye, fov=50.0, speed=2.0, near_plane=0.02, far_plane=100.0)
        engine.scene.set_rotation(camera, look_rotation(eye, target))
        self.lamp = engine.create_point_light(pos=(0.0, 2.0, 0.0), color=(1.0, 0.97, 0.92), intensity=1.0,
                                              range=100.0)
        engine.create_directional_light(direction=SUN_DIRECTION, color=(1.0, 0.95, 0.88), intensity=0.3)
        self.lamp_shape = place(SPHERE, (0.0, 2.0, 0.0), (1.0, 1.0, 1.0))
        # The lamp's sphere glows with the lamp's spectrum.
        engine.set_custom_float("wo.lampEntity", float(self.lamp_shape[0]))

        place(BOX, (0.0, -0.05, 0.0), (8.0, 0.1, 8.0), color=(0.2, 0.2, 0.22))
        place(BOX, (0.0, 2.0, -2.2), (8.0, 4.0, 0.1), color=(0.12, 0.13, 0.16))
        self.disc, self.film, self.gold, self.silver = [], [], [], []
        if model:
            path = os.path.join(SAMPLES, model[0])
            if not os.path.isfile(path):
                print("[wave_optics] %s is not there; clone glTF-Sample-Assets into the repository root." % path)
                os._exit(1)
            place_model(path, model[1], model[2])
            self.center = model[2]
            return
        self.center = DISC_CENTER
        self.disc = place(DISC, DISC_CENTER, (0.6, 0.6, 0.6), rotation=(DISC_TILT, 0.0, 0.0))
        self.film = place(SPHERE, (-1.25, 0.42, 0.25), (0.42, 0.42, 0.42))
        self.gold = place(SPHERE, (1.2, 0.36, 0.3), (0.36, 0.36, 0.36))
        self.silver = place(SPHERE, (0.55, 0.2, 0.95), (0.2, 0.2, 0.2))
        place(BOX, (-0.55, 0.15, 1.0), (0.3, 0.3, 0.3), color=(0.75, 0.2, 0.15))

    def apply(self, s):
        """The lamp and the materials for settings `s`."""
        state = (s.lamp_radius, s.lamp_distance, s.film, s.pitch)
        if state == getattr(self, "state", None):
            return
        self.state = state
        offset = normalized(LAMP_TOWARDS)
        position = tuple(c + o * s.lamp_distance for c, o in zip(self.center, offset))
        engine.scene.set_position(self.lamp, position)
        engine.scene.set_light_source_radius(self.lamp, s.lamp_radius)
        # As bright at the disc from any distance; its surface has the
        # light's radiance, I / (pi r^2).
        intensity = LAMP_IRRADIANCE * s.lamp_distance * s.lamp_distance
        engine.scene.set_light_intensity(self.lamp, intensity)
        glow = intensity / (math.pi * s.lamp_radius * s.lamp_radius)
        for entity in self.lamp_shape:
            engine.scene.set_position(root_of(entity), position)
            engine.scene.set_scale(root_of(entity), (s.lamp_radius,) * 3)
            engine.scene.set_material_vec4(entity, "emissiveFactor", (glow, glow * 0.97, glow * 0.92, 0.0))

        slots = []
        for entity in self.disc:
            slots.append(((entity, GRATING, ALUMINIUM + 1, 0.0), (s.pitch, 0.1, 1.0, 0.0), DISC_CENTER + (1.0,)))
        for entity in self.film:
            slots.append(((entity, FILM, ALUMINIUM + 1, 0.12), (1.0, 0.0, 1.8, s.film), (0.0, 0.0, 0.0, 1.0)))
        for entity in self.gold:
            slots.append(((entity, CONDUCTOR, GOLD + 1, 0.12), (1.0, 0.0, 1.0, 0.0), (0.0, 0.0, 0.0, 1.0)))
        for entity in self.silver:
            slots.append(((entity, CONDUCTOR, SILVER + 1, 0.06), (1.0, 0.0, 1.0, 0.0), (0.0, 0.0, 0.0, 1.0)))
        values = [v for slot in slots[:8] for v in slot]
        values += [(0.0, 0.0, 0.0, 0.0)] * (24 - len(values))
        for i, value in enumerate(values):
            engine.set_custom_vec4("wo.slot[%d]" % i, tuple(float(c) for c in value))


# ── The panel ────────────────────────────────────────────────────────────

GOLDEN = (0.95, 0.85, 0.55, 1.0)
MUTED = (0.62, 0.66, 0.72, 1.0)
FRAME = (0.07, 0.075, 0.09, 0.88)
SURFACE = (0.22, 0.24, 0.30, 1.0)
ACCENT = (0.30, 0.52, 0.85, 1.0)


class Settings:
    """What the image depends on; apply() hands it to the pipeline and the scene."""

    def __init__(self):
        self.lamp_radius = min(max(args.lamp_radius, 0.002), 0.4)
        self.lamp_distance = min(max(args.lamp_distance, 0.6), 8.0)
        self.spectrum = SPECTRA.index(args.lamp)
        self.film = min(max(args.film, 0.1), 30.0)
        self.pitch = args.pitch
        self.polariser = args.polariser
        self.view = args.view
        self.bounces = max(1, min(args.bounces, 12))
        self.exposure = args.exposure
        self.environment = args.sky

    def key(self):
        return (self.lamp_radius, self.lamp_distance, self.spectrum, self.film, self.pitch, self.polariser,
                self.view, self.bounces, self.environment)

    def apply(self, scene):
        engine.set_custom_uint("wo.maxBounces", self.bounces)
        engine.set_custom_uint("wo.view", self.view)
        engine.set_custom_float("wo.exposure", self.exposure)
        engine.set_custom_float("wo.environmentIntensity", self.environment)
        engine.set_custom_float("wo.polariser", -1000.0 if self.polariser is None else self.polariser)
        engine.set_custom_float("wo.maxSamples", float(args.samples))
        engine.set_custom_float("wo.indirectAngle", args.indirect)
        engine.set_custom_uint("wo.lampSpectrum", self.spectrum)
        scene.apply(self)


def coherence_width(radius, distance, wavelength=0.55):
    """The lamp's coherence width in micrometres at `distance`, as the shader has it."""
    return wavelength / (math.pi * math.asin(min(radius / distance, 1.0)))


class Panel:
    WIDTH = 420

    def __init__(self, settings):
        self.settings = settings
        self.visible = True
        self.texts = {}

    def slider(self, column, name, low, high, value):
        label = engine.ui.create_text(column, "", size=18)
        slider = engine.ui.create_slider(column, min=low, max=high, value=value, size=(0, 22))
        engine.ui.set_accessible(slider, name)
        return label, slider

    def build(self):
        ui, s = engine.ui, self.settings
        self.canvas = ui.create_canvas(reference_size=(1920, 1080))
        frame = ui.create_panel(self.canvas, color=FRAME)
        ui.set_rect(frame, anchor_min=(0, 1), anchor_max=(0, 1), pivot=(0, 1), position=(20, -20),
                    size=(self.WIDTH, 590))
        column = ui.create_vertical_layout(frame, spacing=7, padding=(20, 16, 20, 16))
        ui.set_rect(column, anchor_min=(0, 0), anchor_max=(1, 1), size=(0, 0))
        ui.set_layout_child_sizing(column, expand_width=True)
        ui.create_text(column, "Wave optics", size=28, color=GOLDEN, font=ui.load_font("fonts/PlayfairDisplay.ttf"))
        ui.create_text(column, "F2 hides this panel.", size=15, color=MUTED)
        self.samples = ui.create_text(column, "", size=17)
        self.coherence = ui.create_text(column, "", size=17, color=MUTED)
        self.radius_label, self.radius = self.slider(column, "Lamp radius", math.log10(0.002), math.log10(0.4),
                                                     math.log10(s.lamp_radius))
        self.distance_label, self.distance = self.slider(column, "Lamp distance", 0.6, 8.0, s.lamp_distance)
        self.film_label, self.film = self.slider(column, "Film thickness", math.log10(0.1), math.log10(30.0),
                                                 math.log10(s.film))
        self.pitch_label, self.pitch = self.slider(column, "Groove spacing", 0.8, 4.0, s.pitch)
        self.polariser_label, self.polariser = self.slider(column, "Polariser angle", -90.0, 90.0,
                                                           s.polariser or 0.0)
        self.exposure_label, self.exposure = self.slider(column, "Exposure", -4.0, 4.0, math.log2(s.exposure))
        ui.create_text(column, "Lamp", size=18)
        row = ui.create_horizontal_layout(column, size=(0, 34), spacing=4)
        ui.set_layout_child_sizing(row, expand_width=True)
        self.spectra = []
        for value, label in enumerate(SPECTRUM_LABELS):
            button = ui.create_button(row, label, size=(60, 34))
            ui.set_font_size(button, 14)
            self.spectra.append((value, button))
        row = ui.create_horizontal_layout(column, size=(0, 34), spacing=6)
        ui.set_layout_child_sizing(row, expand_width=True)
        self.polariser_button = ui.create_button(row, "", size=(60, 34))
        ui.set_font_size(self.polariser_button, 15)
        self.views = []
        for value, label in enumerate(VIEWS):
            button = ui.create_button(row, label, size=(60, 34))
            ui.set_font_size(button, 15)
            self.views.append((value, button))

    def set_text(self, element, text):
        if self.texts.get(element) != text:
            self.texts[element] = text
            engine.ui.set_text(element, text)

    def update(self, samples, rate):
        ui, s = engine.ui, self.settings
        if ui.value_changed(self.radius):
            s.lamp_radius = 10.0 ** ui.slider_value(self.radius)
        if ui.value_changed(self.distance):
            s.lamp_distance = ui.slider_value(self.distance)
        if ui.value_changed(self.film):
            s.film = 10.0 ** ui.slider_value(self.film)
        if ui.value_changed(self.pitch):
            s.pitch = ui.slider_value(self.pitch)
        if ui.value_changed(self.polariser) and s.polariser is not None:
            s.polariser = ui.slider_value(self.polariser)
        if ui.value_changed(self.exposure):
            s.exposure = 2.0 ** ui.slider_value(self.exposure)
        if ui.was_clicked(self.polariser_button):
            s.polariser = None if s.polariser is not None else ui.slider_value(self.polariser)
        for value, button in self.views:
            if ui.was_clicked(button):
                s.view = value
        for value, button in self.spectra:
            if ui.was_clicked(button):
                s.spectrum = value

        for value, button in self.views:
            ui.set_color(button, ACCENT if value == s.view else SURFACE)
        for value, button in self.spectra:
            ui.set_color(button, ACCENT if value == s.spectrum else SURFACE)
        ui.set_color(self.polariser_button, ACCENT if s.polariser is not None else SURFACE)
        self.set_text(self.polariser_button, "Polariser")
        self.set_text(self.samples, "%d paths per pixel   %.0f per second" % (samples, rate))
        self.set_text(self.coherence, "The lamp's light at the centre: coherent over %.1f um"
                      % coherence_width(s.lamp_radius, s.lamp_distance))
        self.set_text(self.radius_label, "Lamp radius   %.1f cm" % (s.lamp_radius * 100.0))
        self.set_text(self.distance_label, "Lamp distance   %.2f m" % s.lamp_distance)
        self.set_text(self.film_label, "Film thickness   %.2f um" % s.film)
        self.set_text(self.pitch_label, "Groove spacing   %.2f um" % s.pitch)
        self.set_text(self.polariser_label, "Polariser   %s" % ("off" if s.polariser is None
                                                                 else "%.0f degrees" % s.polariser))
        self.set_text(self.exposure_label, "Exposure   %.2f" % s.exposure)

    def toggle(self):
        self.visible = not self.visible
        engine.ui.set_visible(self.canvas, self.visible)


# ── Accumulation ─────────────────────────────────────────────────────────

class Controls:
    """Starts the average over whenever the camera or a setting changes:
    wo.reset is 1 in that frame. Counts the paths per pixel since."""

    def __init__(self):
        self.pressed = keys.KeyEdges(engine.input).pressed
        self.settings = Settings()
        self.scene = Scene()
        self.panel = Panel(self.settings)
        self.state = None
        self.frames = 0
        self.samples = 0
        self.since_reset = 0.0

    def start(self):
        self.scene.build()
        self.settings.apply(self.scene)
        if args.screenshot is None:
            self.panel.build()

    def update(self, dt):
        for key, view in ((keys.NUM_1, 0), (keys.NUM_2, 1)):
            if self.pressed(key):
                self.settings.view = view
        if self.pressed(keys.L):
            self.settings.spectrum = (self.settings.spectrum + 1) % len(SPECTRA)
        restart = self.pressed(keys.R)
        if self.pressed(keys.F2) and args.screenshot is None:
            self.panel.toggle()
        if self.pressed(keys.P):
            save_screenshot(capture_path())
        if args.screenshot is None:
            rate = self.samples / self.since_reset if self.since_reset > 0.0 else 0.0
            self.panel.update(self.samples, rate)
        self.settings.apply(self.scene)

        camera = engine.scene.get_main_camera()
        state = (engine.scene.get_position(camera), engine.scene.get_rotation(camera), self.settings.key())
        reset = restart or state != self.state
        self.state = state
        engine.set_custom_uint("wo.reset", 1 if reset else 0)
        if reset:
            self.frames, self.since_reset = 0, 0.0
        self.frames += 1
        self.since_reset += dt
        self.samples = min(self.frames, args.samples) if args.samples else self.frames

        # A few frames past the last path, so the presented frame holds it.
        if args.screenshot and self.frames >= args.samples + 4:
            save_screenshot(args.screenshot)
            os._exit(0)        # the engine has no quit call


def capture_path():
    directory = os.path.join(os.getcwd(), "wave_optics_captures")
    os.makedirs(directory, exist_ok=True)
    return os.path.join(directory, time.strftime("wave_optics_%Y%m%d_%H%M%S.png"))


def save_screenshot(path):
    print("[wave_optics] screenshot ->", path if engine.capture_screenshot(os.path.abspath(path)) else "failed",
          flush=True)


controls = Controls()


def on_init():
    if not engine.ray_query_supported():
        print("[wave_optics] this device has no ray queries, which the path tracer needs.")
        os._exit(1)
    controls.start()


engine.set_on_init(on_init)
engine.set_on_update(controls.update)

if __name__ == "__main__":
    engine.run()

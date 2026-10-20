"""
Shoonyakasha Stage: a light show over a turntable

A car (or any showroom model) on a turntable on a stage deck, in a dark
hall full of haze and smoke, under a truss of moving heads, with a tribune
facing the stage. The stage pipeline (pipeline/) is the showroom's plus a
grid of fog that follows the camera:
- each cell of the grid holds the density of haze, ground fog and the smoke
  machines' plumes, and the light it scatters towards the eye from every
  spot light, with a ray to the light for its shadow, so beams show in the
  air and objects cut gaps into them;
- along each view ray the cells are summed from the eye outwards, and each
  pixel takes the fog up to its surface.
Four laser projectors draw fans, waves, tunnels of beams and sheets of
light into the same haze: each beam is traced to what it hits, and every
pixel adds the light the fog scatters from the beams its view ray passes,
and the dots and lines they leave on surfaces.
It needs a GPU with ray queries.

Models are the showroom's (python tools/fetch_assets.py showroom); without
them the stage shows the bundled Fox. The set itself is generated on first
start into assets/stage/set/.

Run interactively, a settings panel at the left sets the show (movement
programme, colours, speed, brightness, beam width), the smoke and the camera.

Keys:
    LEFT / RIGHT   previous / next model
    SPACE          next programme
    K              next colours
    L              next laser show
    J              next laser colours
    C              next camera: audience, stage, orbit (drag with the left
                   mouse button, scroll to zoom), free (WASD/Q/E, right mouse)
    T              turntable on or off
    G              fog on or off
    O              frame rate and the fog passes' GPU time in the status line
    F2             settings panel on or off
    P              screenshot into stage_captures/
    F9             start or stop recording a video into stage_captures/

Stills of every programme, for checking a change:
    python stage.py --screenshots shots/
"""

import argparse
import colorsys
import json
import math
import os
import signal
import sys
import time

import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.mathutil import add, look_rotation, normalize, orbit, rotation_facing, scale, yaw_point

HERE = os.path.dirname(os.path.abspath(__file__))
SHOWROOM = os.path.join(HERE, "..", "showroom")
sys.path.insert(0, HERE)
sys.path.insert(0, SHOWROOM)
import catalogue  # noqa: E402
import stage_set  # noqa: E402
import studio  # noqa: E402

PIPELINE = os.path.join(HERE, "pipeline", "pipeline.json")
FONT = "fonts/Roboto-Regular.ttf"

# Body paint whose file declares no clear coat gets this one, as in the
# showroom: a full coat, polished almost to a mirror.
PAINT_COAT = (1.0, 0.03)
TURNTABLE_HEIGHT = 0.06
# The model radius the set fits at a set scale of 1 (a car); larger models
# scale the whole set.
SET_RADIUS = 2.6

ORBIT_DEGREES_PER_PIXEL = 0.25
ORBIT_ZOOM_STEP = 0.88


def parse_args():
    parser = argparse.ArgumentParser(description="Shoonyakasha stage",
                                     formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    parser.add_argument("--models", help="comma-separated showroom catalogue names, in order "
                        "(default: every fetched model)")
    parser.add_argument("--programme", choices=list(PROGRAMMES), default="sweep", help="movement to open with")
    parser.add_argument("--colours", choices=list(PALETTES), default="ice", help="colours to open with")
    parser.add_argument("--lasers", choices=list(LASER_SHOWS), default="fans", help="laser show to open with")
    parser.add_argument("--laser-colours", choices=list(LASER_COLOURS), default="rgb",
                        help="laser colours to open with")
    parser.add_argument("--camera", choices=list(CAMERA_MODES), default="audience")
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--texture-size", type=int, default=4096, help="largest texture side to load")
    parser.add_argument("--screenshots", metavar="DIR", help="save a still of every programme and exit")
    parser.add_argument("--settle", type=int, default=150,
                        help="frames each still runs before it is saved (the fog and TAA settle)")
    parser.add_argument("--fps", type=int, default=30, help="frame rate of recordings")
    return parser.parse_args()


# ── The show: movement programmes and colours ────────────────────────────
# A programme gives each moving head a (pan, tilt) in degrees at time t (in
# beats of the speed slider). Tilt is measured from the head's rest, straight
# down when hung and straight up on the floor; pan 0 points it towards the
# audience (+Z), 90 towards +X.

def _sweep(t, i, n, hung):
    if hung:
        return 55.0 * math.sin(0.9 * t + 0.55 * i), 38.0
    return 90.0 * (1 if i % 2 else -1) + 40.0 * math.sin(0.9 * t + 1.3 * i), 28.0


def _fan(t, i, n, hung):
    spread = (i - (n - 1) / 2.0) / max(n - 1, 1)
    breathe = 0.5 + 0.5 * math.sin(0.7 * t)
    if hung:
        return 90.0 * (1 if spread >= 0 else -1), 8.0 + 46.0 * breathe * abs(spread) * 2.0
    return 90.0 * (1 if i % 2 else -1), 5.0 + 35.0 * breathe


def _circles(t, i, n, hung):
    return (110.0 * t + 90.0 * i) % 360.0, 24.0 + 10.0 * math.sin(0.5 * t + i)


def _columns(t, i, n, hung):
    return 0.0, 2.0 * math.sin(0.4 * t + i)


def _audience(t, i, n, hung):
    if hung:
        return 18.0 * math.sin(0.8 * t + 0.9 * i), 52.0 + 6.0 * math.sin(0.6 * t + i)
    return 0.0, 40.0 + 8.0 * math.sin(0.8 * t + i)


def _focus(t, i, n, hung):
    return None        # every head on the model: see Show.aim


PROGRAMMES = {"sweep": ("Sweep", _sweep), "fan": ("Fan", _fan), "circles": ("Circles", _circles),
              "columns": ("Columns", _columns), "audience": ("Audience", _audience), "focus": ("Focus", _focus)}


def _hue(h, s=1.0, v=1.0):
    return colorsys.hsv_to_rgb(h % 1.0, s, v)


PALETTES = {
    "white": ("White", lambda t, i, n: (1.0, 0.96, 0.9)),
    "ice": ("Ice", lambda t, i, n: (0.5, 0.75, 1.0) if i % 2 else (0.85, 0.93, 1.0)),
    "sunset": ("Sunset", lambda t, i, n: (1.0, 0.45, 0.08) if i % 2 else (1.0, 0.12, 0.45)),
    "rainbow": ("Rainbow", lambda t, i, n: _hue(i / float(n) + 0.04 * t)),
    "ultraviolet": ("Ultraviolet", lambda t, i, n: (0.42, 0.12, 1.0) if i % 2 else (0.12, 0.3, 1.0)),
}

# Laser shows: for each of the four projectors (two on the deck behind the
# model, then the truss's front and back centres), (pattern, beams, spread
# in degrees) or None. Patterns as in the pipeline's lasers.glsl: 1 a fan
# that tilts and rolls, 2 a fan whose beams wave, 3 a tunnel of beams on a
# turning cone, 4 a sheet of light.
LASER_SHOWS = {
    "off": ("Off", [None, None, None, None]),
    "fans": ("Fans", [(1, 12, 28.0), (1, 12, 28.0), (1, 10, 35.0), None]),
    "waves": ("Waves", [(2, 12, 30.0), (2, 12, 30.0), (2, 14, 40.0), None]),
    "tunnel": ("Tunnel", [(3, 12, 6.0), (3, 12, 6.0), (3, 16, 9.0), None]),
    "sky": ("Sky", [(1, 6, 20.0), (1, 6, 20.0), None, (4, 0, 38.0)]),
    "everything": ("Everything", [(2, 10, 30.0), (2, 10, 30.0), (3, 16, 9.0), (4, 0, 38.0)]),
}

LASER_COLOURS = {
    "green": ("Green", lambda t, j: (0.1, 1.0, 0.15)),
    "rgb": ("RGB", lambda t, j: ((1.0, 0.05, 0.05), (0.15, 0.25, 1.0), (0.1, 1.0, 0.15), (0.2, 0.9, 1.0))[j]),
    "cyan_magenta": ("Cyan/magenta", lambda t, j: (0.1, 0.9, 1.0) if j % 2 else (1.0, 0.1, 0.8)),
    "rainbow": ("Rainbow", lambda t, j: _hue(0.25 * j + 0.06 * t)),
    "heads": ("As the heads", None),
}

# Laser power at the brightness slider's 1, and how bright their dots are on
# surfaces.
LASER_BEAM_WATTS = 1.0
LASER_SHEET_WATTS = 6.0
LASER_DOT_GAIN = 40.0

CAMERA_MODES = {"audience": "Audience", "stage": "Stage", "orbit": "Orbit", "free": "Free"}


# ── The engine ───────────────────────────────────────────────────────────

args = parse_args()
ROOT = sk.assets.root()
if ROOT is None:
    print("No asset root found; run from inside the repository or set SHOONYAKASHA_ASSET_DIR.")
    sys.exit(1)
STUDIO = studio.ensure(os.path.join(str(ROOT), "showroom", "studio"))
SET = stage_set.ensure(os.path.join(str(ROOT), "stage", "set"))

sk.shaders.compile_dir(os.path.join(HERE, "pipeline", "shaders"))
engine = sk.Engine(
    title="Shoonyakasha - Stage",
    width=args.width, height=args.height,
    hdr_environment_path=os.path.join(SET, "hall.hdr"),
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


def load(path, prefix, parent):
    """A glTF file's entities, its roots parented to `parent`."""
    result = engine.load_gltf_scene(path, name_prefix=prefix)
    if not result.success:
        raise RuntimeError("could not load %s: %s" % (path, result.error))
    for root in roots_of(result.entities):
        scene.set_parent(root, parent)
    return list(result.entities)


# ── Models ───────────────────────────────────────────────────────────────

class Model:
    """A model on the turntable: loaded on first show, then hidden and shown."""

    def __init__(self, entry, path, bounds, credit):
        self.entry = entry
        self.path = path
        self.bounds = bounds
        self.credit = credit
        self.loaded = False
        self.entities = []
        self.fit = sk.NULL_ENTITY
        self.scale = 1.0
        self.yaw = entry.yaw
        lo, hi = bounds
        extent = [b - a for a, b in zip(lo, hi)]
        self.auto_turn = 90.0 if extent[0] > extent[2] else 0.0     # the long side along Z
        length = max(extent[0], extent[2]) or 1.0
        self.scale = (entry.length / length) if entry.length else 1.0
        width, length = (extent[2], extent[0]) if self.auto_turn else (extent[0], extent[2])
        self.size = (width * self.scale, extent[1] * self.scale, length * self.scale)
        self.base = 0.0

    def load(self, parent):
        result = engine.load_gltf_scene(self.path, max_texture_size=args.texture_size, name_prefix="m",
                                        load_skins=True, load_animations=True)
        if not result.success:
            raise RuntimeError("could not load %s: %s" % (self.path, result.error))
        self.entities = list(result.entities)
        self.fit = holder("model_fit")
        scene.set_parent(self.fit, parent)
        for root in roots_of(self.entities):
            scene.set_parent(root, self.fit)
        if result.animation_clips:
            for e in self.entities:
                scene.play_animation(e, 0)
        # Clear coat: what the file declares, and on body paint that declares none.
        for e in self.entities:
            material = scene.get_name(e)[2:].rsplit("#", 1)[0]
            painted = self.entry.paint is not None and self.entry.paint.search(material)
            if scene.has_material_param(e, "clearcoatFactor") or not painted:
                continue
            scene.set_material_float(e, "clearcoatFactor", PAINT_COAT[0])
            scene.set_material_float(e, "clearcoatRoughnessFactor", PAINT_COAT[1])
        self.loaded = True

    def place(self, base):
        """Turn, centre on the turntable and stand `base` metres up (plus the
        model's own plinth or hover)."""
        lo, hi = self.bounds
        self.base = base + self.entry.plinth + self.entry.hover
        yaw = self.yaw + self.auto_turn
        centre = ((lo[0] + hi[0]) / 2.0, lo[1], (lo[2] + hi[2]) / 2.0)
        offset = yaw_point(scale(centre, -self.scale), yaw)
        scene.set_scale(self.fit, (self.scale,) * 3)
        scene.set_rotation(self.fit, (0.0, math.radians(yaw), 0.0))
        scene.set_position(self.fit, add(offset, (0.0, self.base, 0.0)))

    def centre(self):
        return (0.0, self.base + self.size[1] / 2.0, 0.0)

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
    """The showroom catalogue's fetched models; the bundled Fox if there are none."""
    wanted = args.models.split(",") if args.models else [e.name for e in catalogue.CATALOGUE]
    models = []
    for name in wanted:
        entry = catalogue.BY_NAME.get(name)
        if entry is None:
            print("[stage] unknown model '%s'; known: %s" % (name, ", ".join(catalogue.BY_NAME)))
            continue
        directory = os.path.join(str(ROOT), "showroom", name)
        path = os.path.join(directory, "model.gltf")
        if not os.path.exists(path):
            continue
        with open(os.path.join(directory, "showroom.json"), encoding="utf-8") as f:
            info = json.load(f)
        models.append(Model(entry, path, (info["boundsMin"], info["boundsMax"]), info["credit"]))
    if not models:
        print("[stage] No showroom models found (python tools/fetch_assets.py showroom); showing the Fox.")
        path = str(ROOT / "models" / "Fox.glb")
        entry = catalogue.Entry("fox", "Fox", "Khronos glTF sample", "miniature", 1.1, yaw=-90.0)
        models.append(Model(entry, path, catalogue.gltf_bounds(path),
                            '"Fox" by PixelMannen (CC0), rigged by tomkranis (CC BY 4.0)'))
    return models


# ── The set ──────────────────────────────────────────────────────────────

# Moving heads, in set units: (x, z, hung). Hung ones hang under the truss's
# front and back beams; the others stand on the deck.
HEADS = ([(x, stage_set.TRUSS_Z1, True) for x in (-4.5, -1.5, 1.5, 4.5)]
         + [(x, stage_set.TRUSS_Z0, True) for x in (-4.5, -1.5, 1.5, 4.5)]
         + [(-5.4, -5.3, False), (5.4, -5.3, False), (-5.4, 3.2, False), (5.4, 3.2, False)])

# Smoke machines, in set units: (x, y, z, direction the jet leaves in). Two
# on the deck's back corners, two on the floor beside its front.
MACHINES = [(-5.9, stage_set.DECK[1], -5.6, (0.55, 0.0, 0.85)), (5.9, stage_set.DECK[1], -5.6, (-0.55, 0.0, 0.85)),
            (-8.5, 0.0, 5.0, (0.92, 0.0, -0.4)), (8.5, 0.0, 5.0, (-0.92, 0.0, -0.4))]

# Laser projectors, in set units: (x, y, z of the aperture, the direction it
# points). Two on the deck behind the model, aiming over the audience; one
# under the middle of the truss's front beam and one under its back beam.
_HUNG_LASER = stage_set.TRUSS_HEIGHT - stage_set.TRUSS_SECTION / 2.0 - 0.12
LASERS = [(-3.0, stage_set.DECK[1] + 0.1, -5.6, (0.0, 0.18, 1.0)),
          (3.0, stage_set.DECK[1] + 0.1, -5.6, (0.0, 0.18, 1.0)),
          (0.0, _HUNG_LASER, stage_set.TRUSS_Z1, (0.0, -0.25, 1.0)),
          (0.0, _HUNG_LASER, stage_set.TRUSS_Z0, (0.0, -0.05, 1.0))]


class MovingHead:
    """A moving head: base and yoke turning about the vertical, and the head
    aiming the spot light."""

    def __init__(self, index, x, z, hung):
        self.index = index
        self.spot = (x, z)
        self.hung = hung
        self.direction = (0.0, -1.0 if hung else 1.0, 0.0)
        self.centre = (0.0, 0.0, 0.0)

    def build(self):
        self.yoke = holder("head%d_yoke" % self.index)
        load(os.path.join(SET, "yoke_hung.gltf" if self.hung else "yoke_floor.gltf"), "head%d" % self.index,
             self.yoke)
        self.head = holder("head%d_head" % self.index)
        self.parts = load(os.path.join(SET, "head.gltf"), "head%d" % self.index, self.head)
        self.lens = by_suffix(self.parts, "head_lens")
        for e in self.parts:
            scene.set_cast_shadows(e, False)       # the light shines out of it
        self.light = engine.create_point_light(pos=(0.0, 4.0, 0.0), color=(1.0, 1.0, 1.0), intensity=0.0, range=60.0)
        scene.set_light_type(self.light, sk.LIGHT_SPOT)
        scene.set_light_cast_shadows(self.light, True)

    def place(self, s):
        x, z = self.spot
        if self.hung:
            y = stage_set.TRUSS_HEIGHT - stage_set.TRUSS_SECTION / 2.0 - stage_set.yoke_base_offset()
        else:
            y = stage_set.deck_top() + stage_set.yoke_base_offset()
        self.centre = (x * s, y * s, z * s)
        for e in (self.yoke, self.head):
            scene.set_position(e, self.centre)
            scene.set_scale(e, (s,) * 3)
        scene.set_light_range(self.light, 60.0 * s)
        scene.set_light_source_radius(self.light, 0.08 * s)

    def aim(self, direction, s):
        self.direction = direction
        rotation = rotation_facing(direction)
        scene.set_rotation(self.yoke, (0.0, rotation[1], 0.0))
        scene.set_rotation(self.head, rotation)
        scene.set_position(self.light, add(self.centre, scale(direction, 0.2 * s)))
        scene.set_rotation(self.light, rotation)

    def shine(self, color, intensity, half_angle, s):
        scene.set_light_color(self.light, color)
        scene.set_light_intensity(self.light, intensity)
        scene.set_light_cone(self.light, half_angle * 0.8, half_angle)
        glow = scale(color, 6.0 + 30.0 * min(intensity / max(800.0 * s * s, 1e-3), 2.0))
        for e in self.lens:
            scene.set_material_vec4(e, "emissiveFactor", glow + (1.0,))


class StageSet:
    """The hall, the turntable, the moving heads and the smoke machines."""

    def __init__(self):
        self.scale = 1.0
        self.angle = 0.0
        self.heads = [MovingHead(i, x, z, hung) for i, (x, z, hung) in enumerate(HEADS)]

    def build(self):
        self.root = holder("set")
        hall = load(os.path.join(SET, "hall.gltf"), "hall", self.root)
        self.strip = by_suffix(hall, "deck_strip")
        for e in hall:
            name = scene.get_name(e)
            if name.endswith("_hall_floor") or name.endswith("_hall_walls"):
                scene.set_cast_shadows(e, False)       # receivers only: the fog's rays need not test them
        self.spin = holder("turntable_spin")
        self.turntable = holder("turntable")
        scene.set_parent(self.turntable, self.spin)
        parts = load(os.path.join(STUDIO, "turntable.gltf"), "turntable", self.turntable)
        self.rim = by_suffix(parts, "turntable_rim")
        self.plinth = holder("plinth")
        scene.set_parent(self.plinth, self.spin)
        self.plinth_parts = load(os.path.join(STUDIO, "plinth.gltf"), "plinth", self.plinth)
        for head in self.heads:
            head.build()
        # Two key lights on the truss's front corners, on the model whatever
        # the moving heads do. Image slot 2 keeps them out of the fog.
        self.keys = []
        for _ in range(2):
            light = engine.create_point_light(pos=(0.0, 4.0, 0.0), color=(1.0, 0.95, 0.88), intensity=0.0,
                                              range=60.0)
            scene.set_light_type(light, sk.LIGHT_SPOT)
            scene.set_light_cast_shadows(light, True)
            scene.set_light_cone(light, 10.0, 18.0)
            scene.set_light_source_image(light, 2)
            self.keys.append(light)
        self.machines = []
        for i, _ in enumerate(MACHINES):
            machine = holder("smoke%d" % i)
            load(os.path.join(SET, "smoke_machine.gltf"), "smoke%d" % i, machine)
            self.machines.append(machine)
        self.lasers = []
        for i, _ in enumerate(LASERS):
            housing = holder("laser%d" % i)
            for e in load(os.path.join(SET, "laser.gltf"), "laser%d" % i, housing):
                scene.set_cast_shadows(e, False)       # the beams leave through it
            self.lasers.append(housing)

    def stand(self, model):
        """Scale the set to the model and stand it on the turntable."""
        s = self.scale = max(1.0, model.radius() / SET_RADIUS)
        scene.set_scale(self.root, (s,) * 3)
        deck = stage_set.deck_top() * s
        scene.set_position(self.spin, (0.0, deck, 0.0))
        r = model.turntable_radius()
        scene.set_scale(self.turntable, (r, 1.0, r))
        plinth = model.entry.plinth
        for e in self.plinth_parts:
            scene.set_visible(e, plinth > 0.0)
        scene.set_position(self.plinth, (0.0, TURNTABLE_HEIGHT, 0.0))
        scene.set_scale(self.plinth, (r * 1.1, max(plinth, 0.01), r * 1.1))
        model.place(TURNTABLE_HEIGHT)
        for head in self.heads:
            head.place(s)
        for light, side in zip(self.keys, (-1.0, 1.0)):
            position = (side * stage_set.TRUSS_X * s, (stage_set.TRUSS_HEIGHT - 0.3) * s, stage_set.TRUSS_Z1 * s)
            scene.set_position(light, position)
            scene.set_rotation(light, look_rotation(position, model.centre()))
            scene.set_light_range(light, 60.0 * s)
            scene.set_light_source_radius(light, 0.15 * s)
        for machine, (x, y, z, direction) in zip(self.machines, MACHINES):
            scene.set_position(machine, (x * s, y * s, z * s))
            scene.set_rotation(machine, rotation_facing(direction))
            scene.set_scale(machine, (s,) * 3)
        for housing, (origin, axis) in zip(self.lasers, self.apertures()):
            scene.set_position(housing, origin)
            scene.set_rotation(housing, rotation_facing(axis))
            scene.set_scale(housing, (s,) * 3)

    def apertures(self):
        """Each laser projector's aperture and the direction it points, in world space."""
        s = self.scale
        return [((x * s, y * s, z * s), normalize(direction)) for x, y, z, direction in LASERS]

    def nozzles(self):
        """Each smoke machine's nozzle and jet direction, in world space."""
        s = self.scale
        out = []
        for x, y, z, direction in MACHINES:
            d = normalize(direction)
            nozzle = add((x * s, y * s, z * s), add(scale(d, -stage_set.NOZZLE[2] * s),
                                                    (0.0, stage_set.NOZZLE[1] * s, 0.0)))
            out.append((nozzle, d))
        return out

    def turn(self, degrees):
        self.angle = (self.angle + degrees) % 360.0
        scene.set_rotation(self.spin, (0.0, math.radians(self.angle), 0.0))


# ── The show ─────────────────────────────────────────────────────────────

class Show:
    """Moves and colours the moving heads, and sets the fog."""

    def __init__(self):
        self.programme = args.programme
        self.palette = args.colours
        self.speed = 1.0
        self.brightness = 1.0
        self.beam = 7.0              # half-angle of the beams, degrees
        self.key = 1.0               # the key lights, times
        self.beats = 0.0
        self.fog_on = True
        self.haze = 0.007
        self.smoke = 1.0
        self.ground = 0.07
        self.forward = 0.6           # Henyey-Greenstein g
        self.glow = 1.8              # light the fog scatters, times
        self.aims = {}               # moving head -> its current direction, eased towards the programme's
        self.laser_show = args.lasers
        self.laser_palette = args.laser_colours
        self.laser_power = 1.0

    def target(self, head, i, n, model, s):
        """Where the programme points head i (of n of its kind) now."""
        _, programme = PROGRAMMES[self.programme]
        pan_tilt = programme(self.beats, i, n, head.hung)
        if pan_tilt is None:
            # On the model, each head on its own point around it.
            c = model.centre()
            r = model.radius() * 0.45
            a = self.beats * 0.4 + head.index * 1.7
            goal = add(c, (r * math.cos(a), -model.size[1] * 0.3, r * math.sin(a)))
            return normalize(tuple(g - p for g, p in zip(goal, head.centre)))
        pan, tilt = (math.radians(v) for v in pan_tilt)
        # Sweeping, the front row of the truss faces back over the deck, so
        # its beams cross the back row's above the model.
        if self.programme == "sweep" and head.hung and head.spot[1] > 0.0:
            tilt = -tilt
        up = -1.0 if head.hung else 1.0
        return normalize((math.sin(tilt) * math.sin(pan), up * math.cos(tilt), math.sin(tilt) * math.cos(pan)))

    def update(self, dt, stage, model):
        self.beats += dt * self.speed
        s = stage.scale
        hung = [h for h in stage.heads if h.hung]
        floor = [h for h in stage.heads if not h.hung]
        _, colour = PALETTES[self.palette]
        ease = 1.0 - math.exp(-6.0 * dt) if dt > 0.0 else 1.0
        for group in (hung, floor):
            for i, head in enumerate(group):
                goal = self.target(head, i, len(group), model, s)
                now = self.aims.get(head, goal)
                now = normalize(tuple(a + (b - a) * ease for a, b in zip(now, goal)))
                self.aims[head] = now
                head.aim(now, s)
        for head in stage.heads:
            color = colour(self.beats, head.index, len(stage.heads))
            head.shine(color, 500.0 * s * s * self.brightness, self.beam, s)
        for light in stage.keys:
            scene.set_light_intensity(light, 110.0 * s * s * self.key)
        strip = colour(self.beats, 0, len(stage.heads))
        for e in stage.strip:
            scene.set_material_vec4(e, "emissiveFactor", scale(strip, 2.0 * self.brightness) + (1.0,))
        for e in stage.rim:
            scene.set_material_vec4(e, "emissiveFactor", scale(strip, 1.2 * self.brightness) + (0.0,))
        self.apply_fog(stage)
        self.apply_lasers(stage, colour)

    def apply_lasers(self, stage, head_colour):
        """Each projector's pattern, aim and colour times power, for the
        pipeline's LaserTrace and Lasers passes."""
        s = stage.scale
        _, plan = LASER_SHOWS[self.laser_show]
        _, colour = LASER_COLOURS[self.laser_palette]
        for j, ((origin, axis), spec) in enumerate(zip(stage.apertures(), plan)):
            if spec is None or self.laser_power <= 0.0:
                engine.set_custom_vec4("laser.origin%d" % j, origin + (0.0,))
                engine.set_custom_vec4("laser.color%d" % j, (0.0, 0.0, 0.0, 0.0))
                continue
            pattern, beams, spread = spec
            c = colour(self.beats, j) if colour else head_colour(self.beats, 3 * j, len(stage.heads))
            # Watts per beam; a sheet spreads its power over its whole arc.
            power = (LASER_SHEET_WATTS if pattern == 4 else LASER_BEAM_WATTS) * self.laser_power * s
            engine.set_custom_vec4("laser.origin%d" % j, origin + (float(pattern),))
            engine.set_custom_vec4("laser.axis%d" % j, axis + (float(beams),))
            engine.set_custom_vec4("laser.up%d" % j, (0.0, 1.0, 0.0, math.radians(spread)))
            engine.set_custom_vec4("laser.color%d" % j, scale(c, power) + (1.3 * j,))
        engine.set_custom_vec4("laser.params", (self.beats, 0.003 * s, LASER_DOT_GAIN, 80.0 * s))

    def apply_fog(self, stage):
        s = stage.scale
        hx, hy, hz = stage_set.HALL
        engine.set_custom_vec4("fog.grid", (0.3, 55.0 * s, 0.9, 1.0 if self.fog_on else 0.0))
        engine.set_custom_vec4("fog.media", (self.haze, self.smoke, self.ground, self.forward))
        engine.set_custom_vec4("fog.motion", (showtime.seconds, self.glow, 0.5 * s, 1.0 / s))
        engine.set_custom_vec4("fog.wind", (0.12 * s, 0.0, 0.05 * s, 0.0))
        engine.set_custom_vec4("fog.hall", (hx * s, hy * s, hz * s, 0.0))
        for i, (nozzle, direction) in enumerate(stage.nozzles()):
            engine.set_custom_vec4("fog.machine%d" % i, nozzle + (1.0,))
            engine.set_custom_vec4("fog.machineDir%d" % i, direction + (2.2 * s,))

    def set_fog(self, on):
        self.fog_on = on
        for name in ("FogScatter", "FogHistory", "FogIntegrate", "FogApply"):
            engine.set_pass_enabled(name, on)


class ShowTime:
    """Seconds since the start, advanced by each frame's dt."""

    def __init__(self):
        self.seconds = 0.0


showtime = ShowTime()


# ── On screen ────────────────────────────────────────────────────────────

INK = (0.92, 0.93, 0.95, 1.0)
MUTED = (0.62, 0.66, 0.72, 1.0)
GOLD = (0.95, 0.85, 0.55, 1.0)
FRAME = (0.07, 0.075, 0.09, 0.9)
SURFACE = (0.22, 0.24, 0.30, 1.0)
ACCENT = (0.30, 0.52, 0.85, 1.0)


class StatusLine:
    """The model and the show at the top left; with O, the frame rate and
    the fog passes' GPU time."""

    def __init__(self):
        self.stats = False
        self.wait = 0.0
        self.text = ""

    def build(self):
        k = args.height / 1080.0
        self.label = engine.create_text("", sk.UI_ANCHOR_TOP_LEFT, (28.0 * k, 34.0 * k), FONT,
                                        font_size=17.0 * k, color=(0.8, 0.8, 0.8, 0.9))
        scene.set_text_sort_key(self.label, 1)

    def toggle_stats(self):
        self.stats = not self.stats
        if self.stats:
            engine.enable_render_stats()
        else:
            engine.disable_render_stats()

    def update(self, dt):
        s = stage
        model = s.model
        text = "%s  |  %s  |  %s  |  lasers: %s  |  %s camera" % (
            catalogue.ascii_text(model.entry.title), PROGRAMMES[s.show.programme][0],
            PALETTES[s.show.palette][0], LASER_SHOWS[s.show.laser_show][0], CAMERA_MODES[s.camera_mode])
        if self.stats:
            self.wait -= dt
            stats = engine.render_stats
            if stats and stats["fps"] > 0.0:
                fog = sum((p["gpu_ms"] or 0.0) for p in stats["passes"] if p["name"].startswith("Fog"))
                gpu = stats["gpu_ms"]
                self.extra = "  |  %.0f FPS, GPU %s, fog %.2f ms" % (
                    stats["fps"], "%.2f ms" % gpu if gpu is not None else "n/a", fog)
            text += getattr(self, "extra", "")
        if text != self.text:
            self.text = text
            scene.set_text(self.label, text)


class SettingsPanel:
    """A screen canvas at the left with the show's controls. Each frame it
    reads its controls first, then shows the stage's state, so changes made
    with the keys appear on it too."""

    WIDTH = 400

    def __init__(self):
        self.visible = True
        self.built = False
        self.sliders = {}           # slider -> (its label, name, format of its value)
        self.texts = {}

    def build(self):
        ui = engine.ui
        self.built = True
        self.canvas = ui.create_canvas(reference_size=(1920, 1080))
        top, bottom = 72.0, 40.0
        self.frame = ui.create_panel(self.canvas, color=FRAME)
        ui.set_rect(self.frame, anchor_min=(0, 0), anchor_max=(0, 1), pivot=(0, 0.5),
                    position=(24, (top - bottom) / 2.0), size=(self.WIDTH, -(top + bottom)))
        column = ui.create_vertical_layout(self.frame, spacing=6, padding=(20, 14, 12, 14))
        ui.set_rect(column, anchor_min=(0, 0), anchor_max=(1, 1), size=(0, 0))
        ui.set_layout_child_sizing(column, expand_width=True)
        ui.create_text(column, "Stage", size=30, color=GOLD)
        ui.create_text(column, "F2 hides this panel.", size=15, color=MUTED)
        view = ui.create_scroll_view(column, size=(self.WIDTH - 32, 0))
        ui.set_layout_element(view, flexible=(-1, 1))
        ui.set_accessible(view, "Stage settings")
        page = ui.create_vertical_layout(ui.scroll_content(view), spacing=8, padding=(0, 4, 18, 12))
        ui.set_layout_child_sizing(page, expand_width=True)
        self.page = page

        self.section("Model")
        self.model_buttons = self.choices({"previous": "Previous", "next": "Next"}, 2)
        self.spin = ui.create_toggle(page, "Turntable turning", size=(0, 28))

        self.section("Show")
        self.programmes = self.choices({k: v[0] for k, v in PROGRAMMES.items()}, 3)
        self.palettes = self.choices({k: v[0] for k, v in PALETTES.items()}, 3)
        self.speed = self.slider("Speed", 0.0, 3.0, 1.0, "x%.2f")
        self.brightness = self.slider("Brightness", 0.0, 2.0, 1.0, "x%.2f")
        self.beam = self.slider("Beam width", 2.0, 20.0, 7.0, "%.1f°")
        self.key = self.slider("Key light on the model", 0.0, 2.0, 1.0, "x%.2f")

        self.section("Lasers")
        self.laser_shows = self.choices({k: v[0] for k, v in LASER_SHOWS.items()}, 3)
        self.laser_palettes = self.choices({k: v[0] for k, v in LASER_COLOURS.items()}, 3)
        self.laser_power = self.slider("Laser power", 0.0, 3.0, 1.0, "x%.2f")

        self.section("Smoke")
        self.fog = ui.create_toggle(page, "Fog", is_on=True, size=(0, 28))
        self.haze = self.slider("Haze", 0.0, 0.05, 0.007, "%.3f")
        self.smoke = self.slider("Smoke machines", 0.0, 3.0, 1.0, "x%.2f")
        self.ground = self.slider("Ground fog", 0.0, 0.6, 0.07, "%.2f")
        self.glow = self.slider("Beam glow", 0.0, 3.0, 1.8, "x%.2f")
        self.forward = self.slider("Forward scattering", 0.0, 0.9, 0.6, "%.2f")

        self.section("Camera")
        self.cameras = self.choices(CAMERA_MODES, 4)
        self.exposure = self.slider("Exposure", -3.0, 3.0, 0.0, "%+.1f EV")
        self.stats = ui.create_toggle(page, "Frame rate and fog time", size=(0, 28))
        self.apply()

    def section(self, title):
        ui = engine.ui
        ui.create_element(self.page, (0, 6))
        ui.create_text(self.page, title, size=20, color=GOLD)

    def choices(self, labels, per_row):
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
        label = engine.ui.create_text(self.page, "", size=17)
        slider = engine.ui.create_slider(self.page, min=lo, max=hi, value=value, size=(0, 22))
        engine.ui.set_accessible(slider, name)
        self.sliders[slider] = (label, name, value_format)
        return slider

    def apply(self):
        if self.built:
            engine.ui.set_visible(self.canvas, self.visible)

    def set_text(self, element, text):
        if self.texts.get(element) != text:
            self.texts[element] = text
            engine.ui.set_text(element, text)

    def update(self):
        if not self.built:
            return
        ui, s = engine.ui, stage
        show = s.show
        clicked = lambda buttons: next((k for k, b in buttons.items() if ui.was_clicked(b)), None)
        changed = ui.value_changed

        action = clicked(self.model_buttons)
        if action == "previous":
            s.select(s.index - 1)
        elif action == "next":
            s.select(s.index + 1)
        if changed(self.spin):
            s.spinning = ui.toggle_value(self.spin)
        programme = clicked(self.programmes)
        if programme:
            show.programme = programme
        palette = clicked(self.palettes)
        if palette:
            show.palette = palette
        laser_show = clicked(self.laser_shows)
        if laser_show:
            show.laser_show = laser_show
        laser_palette = clicked(self.laser_palettes)
        if laser_palette:
            show.laser_palette = laser_palette
        for slider, attribute in ((self.speed, "speed"), (self.brightness, "brightness"), (self.beam, "beam"),
                                  (self.key, "key"), (self.laser_power, "laser_power"),
                                  (self.haze, "haze"), (self.smoke, "smoke"), (self.ground, "ground"),
                                  (self.glow, "glow"), (self.forward, "forward")):
            if changed(slider):
                setattr(show, attribute, ui.slider_value(slider))
        if changed(self.fog):
            show.set_fog(ui.toggle_value(self.fog))
        camera = clicked(self.cameras)
        if camera:
            s.set_camera(camera)
        if changed(self.exposure):
            s.set_exposure(ui.slider_value(self.exposure))
        if changed(self.stats):
            s.status.toggle_stats()

        for toggle, on in ((self.spin, s.spinning), (self.fog, show.fog_on), (self.stats, s.status.stats)):
            if ui.toggle_value(toggle) != on:
                ui.set_toggle_value(toggle, on)
        for buttons, chosen in ((self.programmes, show.programme), (self.palettes, show.palette),
                                (self.laser_shows, show.laser_show), (self.laser_palettes, show.laser_palette),
                                (self.cameras, s.camera_mode)):
            for key, button in buttons.items():
                ui.set_color(button, ACCENT if key == chosen else SURFACE)
        for slider, (label, name, value_format) in self.sliders.items():
            self.set_text(label, "%s   %s" % (name, value_format % ui.slider_value(slider)))


# ── The stage ────────────────────────────────────────────────────────────

class Stage:
    def __init__(self):
        self.models = find_models()
        self.set = StageSet()
        self.show = Show()
        self.status = StatusLine()
        self.settings = SettingsPanel()
        self.index = 0
        self.camera_mode = args.camera
        self.spinning = True
        self.time = 0.0
        self.exposure_ev = 0.0
        self.orbit_goal = None
        self.orbit_now = None
        self.orbit_drag = None

    @property
    def model(self):
        return self.models[self.index]

    def start(self):
        if not engine.ray_query_supported():
            print("[stage] this GPU has no ray queries, which the fog needs; the hall stays clear.")
        engine.apply_pipeline_preset("raytraced" if engine.ray_query_supported() else "high")
        engine.set_custom_float("showroom.reflections", 1.0)
        engine.set_custom_float("showroom.areaLights", 1.0)
        engine.set_custom_float("default.contactShadowLength", 0.12)
        engine.set_custom_float("default.aoRadius", 0.45)
        engine.set_custom_float("default.bloomIntensity", 0.06)
        engine.set_custom_float("default.iblIntensity", 0.3)
        engine.set_custom_float("default.exposureMinEV", -1.0)
        engine.set_custom_uint("default.toneMapper", 2)       # AgX
        self.set_exposure(0.0)
        self.set.build()
        self.status.build()
        if not args.screenshots:
            self.settings.build()
        self.select(0)

    def select(self, index):
        old = self.model if self.model.loaded else None
        self.index = index % len(self.models)
        model = self.model
        if old is not None and old is not model:
            old.show(False)
        if not model.loaded:
            model.load(self.set.spin)
        model.show(True)
        self.set.stand(model)
        self.show.aims.clear()
        self.orbit_goal = None
        print("[stage] %s" % " ".join(p for p in (model.entry.year, model.entry.title) if p))

    def set_camera(self, mode):
        self.camera_mode = mode
        self.orbit_goal = None
        print("[stage] camera:", mode)

    def set_exposure(self, ev):
        self.exposure_ev = ev
        engine.set_custom_float("default.exposure", 0.9 * 2.0 ** ev)

    def update(self, dt):
        self.time += dt
        showtime.seconds += dt
        if self.spinning:
            self.set.turn(self.model.entry.spin * dt)
        if self.model.entry.hover:
            bob = 0.04 * math.sin(self.time * 1.7) * min(1.0, self.model.radius() / 2.0)
            scene.set_position(self.set.spin, (0.0, stage_set.deck_top() * self.set.scale + bob, 0.0))
        self.show.update(dt, self.set, self.model)
        self.update_camera(dt)

    # Cameras -----------------------------------------------------------

    def audience_pose(self, u=0.0):
        """From the tribune's middle rows, looking at the model."""
        s = self.set.scale
        row = 3
        eye = ((math.sin(u) * 2.5) * s, (stage_set.TRIBUNE_RISE * (row + 1) + 1.2) * s,
               (stage_set.TRIBUNE_Z + stage_set.TRIBUNE_DEPTH * (row + 0.4)) * s)
        target = add(self.model.centre(), (0.0, 1.2 * s, 0.0))
        return eye, target, 55.0

    def stage_pose(self, u=0.0):
        """Slowly round the deck, low, near the model."""
        c = self.model.centre()
        d = self.model.radius() * 2.6
        return orbit(c, d, 35.0 + u * 8.0, 6.0), add(c, (0.0, 0.6 * self.set.scale, 0.0)), 50.0

    def update_camera(self, dt):
        if self.camera_mode == "audience":
            self.pose(*self.audience_pose(0.05 * self.time))
        elif self.camera_mode == "stage":
            self.pose(*self.stage_pose(self.time))
        elif self.camera_mode == "orbit":
            self.orbit_camera(dt)

    def orbit_camera(self, dt):
        model = self.model
        target = model.centre()
        nearest = model.radius() * 1.1
        farthest = stage_set.HALL[2] * self.set.scale - 1.0
        if self.orbit_goal is None:
            eye = scene.get_world_position(engine.camera_entity)
            offset = tuple(eye[i] - target[i] for i in range(3))
            distance = max(math.sqrt(sum(v * v for v in offset)), 1e-3)
            self.orbit_goal = [math.degrees(math.atan2(offset[0], offset[2])),
                               math.degrees(math.asin(max(-1.0, min(1.0, offset[1] / distance)))), distance]
            self.orbit_now = None
        goal = self.orbit_goal
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
        goal[1] = max(-5.0, min(80.0, goal[1]))
        goal[2] = max(nearest, min(max(farthest, nearest), goal[2]))
        if self.orbit_now is None:
            self.orbit_now = list(goal)
        ease = 1.0 - math.exp(-14.0 * dt)
        for i in range(3):
            self.orbit_now[i] += (goal[i] - self.orbit_now[i]) * ease
        now = self.orbit_now
        self.pose(orbit(target, now[2], now[0], now[1]), target, 45.0)

    def pose(self, eye, target, fov):
        camera = engine.camera_entity
        scene.set_position(camera, eye)
        scene.set_rotation(camera, look_rotation(eye, target))
        scene.set_camera_fov(camera, fov)


stage = Stage()


# ── Interactive and automatic runs ───────────────────────────────────────

def capture_path(suffix):
    directory = os.path.join(os.getcwd(), "stage_captures")
    os.makedirs(directory, exist_ok=True)
    return os.path.join(directory, time.strftime("stage_%Y%m%d_%H%M%S") + suffix)


class Controls:
    def __init__(self):
        self.pressed = keys.KeyEdges(engine.input).pressed

    def update(self, dt):
        s = stage
        ui_focused = engine.ui.focused_element != sk.NULL_ENTITY
        if self.pressed(keys.RIGHT) and not ui_focused:
            s.select(s.index + 1)
        if self.pressed(keys.LEFT) and not ui_focused:
            s.select(s.index - 1)
        if self.pressed(keys.SPACE) and not ui_focused:
            names = list(PROGRAMMES)
            s.show.programme = names[(names.index(s.show.programme) + 1) % len(names)]
        if self.pressed(keys.K):
            names = list(PALETTES)
            s.show.palette = names[(names.index(s.show.palette) + 1) % len(names)]
        if self.pressed(keys.L):
            names = list(LASER_SHOWS)
            s.show.laser_show = names[(names.index(s.show.laser_show) + 1) % len(names)]
        if self.pressed(keys.J):
            names = list(LASER_COLOURS)
            s.show.laser_palette = names[(names.index(s.show.laser_palette) + 1) % len(names)]
        if self.pressed(keys.C):
            modes = list(CAMERA_MODES)
            s.set_camera(modes[(modes.index(s.camera_mode) + 1) % len(modes)])
        if self.pressed(keys.T):
            s.spinning = not s.spinning
        if self.pressed(keys.G):
            s.show.set_fog(not s.show.fog_on)
        if self.pressed(keys.O):
            s.status.toggle_stats()
        if self.pressed(keys.F2):
            s.settings.visible = not s.settings.visible
            s.settings.apply()
        s.settings.update()
        s.status.update(dt)
        if self.pressed(keys.P):
            path = capture_path(".png")
            print("[stage] screenshot ->", path if engine.capture_screenshot(path) else "failed")
        if self.pressed(keys.F9):
            if engine.is_recording:
                engine.stop_recording()
                print("[stage] recording stopped")
            else:
                path = capture_path(".mp4")
                ok = engine.start_recording(path, fps=args.fps)
                print("[stage] recording ->", path if ok else "failed (see the error above)")


class Stills:
    """--screenshots: the first model in every programme, from the audience
    and from the stage, with the laser show --lasers; then every laser show
    from the audience. Each still after --settle frames of 1/60 s."""

    def __init__(self, directory):
        self.directory = directory
        os.makedirs(directory, exist_ok=True)
        self.jobs = ([(p, args.lasers, c) for p in PROGRAMMES for c in ("audience", "stage")]
                     + [(args.programme, l, "audience") for l in LASER_SHOWS if l != args.lasers])
        self.job = -1
        self.frames = 0
        engine.enable_render_stats()

    def update(self):
        if self.job >= 0:
            self.frames += 1
            if self.frames == args.settle:
                programme, lasers, camera = self.jobs[self.job]
                path = os.path.join(self.directory, "%s_%s_lasers-%s_%s.png" % (
                    stage.model.entry.name, programme, lasers, camera))
                engine.capture_screenshot(path)
                stats = engine.render_stats or {}
                passes = stats.get("passes", [])
                fog = sum((p["gpu_ms"] or 0.0) for p in passes if p["name"].startswith("Fog"))
                laser = sum((p["gpu_ms"] or 0.0) for p in passes if p["name"].startswith("Laser"))
                gpu = stats.get("gpu_ms")
                print("[stage] still -> %s  (GPU %s, fog %.2f ms, lasers %.2f ms)" % (
                    path, "%.2f ms" % gpu if gpu else "n/a", fog, laser), flush=True)
        if self.job < 0 or self.frames > args.settle:
            self.job += 1
            self.frames = 0
            if self.job >= len(self.jobs):
                print("[stage] %d stills in %s" % (len(self.jobs), self.directory), flush=True)
                os._exit(0)        # the engine has no quit call
            programme, lasers, camera = self.jobs[self.job]
            stage.show.programme = programme
            stage.show.laser_show = lasers
            stage.set_camera(camera)


quit_requested = False


def request_quit(signum, frame):
    global quit_requested
    quit_requested = True


signal.signal(signal.SIGINT, request_quit)

controls = Controls()
automation = None


def on_init():
    global automation
    camera = engine.create_camera(pos=(0.0, 3.0, 12.0), fov=50.0, speed=5.0, near_plane=0.05, far_plane=300.0)
    scene.set_rotation(camera, look_rotation((0.0, 3.0, 12.0), (0.0, 1.0, 0.0)))
    engine.set_custom_float("default.exposureAdaptSpeed", 2.5)
    stage.start()
    if args.screenshots:
        automation = Stills(args.screenshots)

    def animate(dt):
        if quit_requested:
            if engine.is_recording:
                engine.stop_recording()
            os._exit(0)
        if automation is not None:
            stage.update(1.0 / 60.0)
            automation.update()
        else:
            stage.update(dt)
        return True

    # Before the transform system (priority 0), so this frame's poses render.
    engine.ecs.add_system("Stage", animate, priority=-5)


engine.set_on_init(on_init)
engine.set_on_update(lambda dt: automation is None and controls.update(dt))

if __name__ == "__main__":
    engine.run()

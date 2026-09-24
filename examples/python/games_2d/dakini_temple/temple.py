"""
Shoonyakasha Dakini Temple

A top-down mandala palace, seen from above:

  - a ring of wisdom fire, a vajra fence and a ring of lotus petals
  - a palace with five-coloured walls and a gate in each direction, its
    ground split into the blue, yellow, red and green quarters
  - an eight-petalled lotus holding the deity's symbol at the centre
  - eight butter lamps and embers that spiral out into space

The palace can hold four deities, each with its own colours, centre symbol
and mantra:

  Vajrayogini   the red dharmodaya with a turning joy-swirl
  Green Tara    a blue utpala flower on a moon disc
  White Tara    an eye of wisdom on a moon disc
  Vajrapani     an upright vajra blazing in front of a sun disc

Every mandala layer is a single quad whose pattern is drawn procedurally by
shaders/mandala.frag. Python picks the layer through a "shape" material
parameter and the deity through the "deity" custom scene value; Python ECS
systems turn, breathe and flicker the quads.

Each deity's name and mantra are shown in Devanagari with a transliteration
beneath. The engine's text baking is ASCII-only, and Devanagari needs a
shaping layout (conjuncts such as ज्र, the ि matra drawn before its
consonant), so these are rendered with Pillow's raqm layout into PNGs under
generated/ and shown as screen-space panels. Without Pillow + raqm, or
without fonts/NotoSansDevanagari-Regular.ttf in the asset root, the
transliterations are shown as engine text instead.

Keys:
    LEFT / RIGHT   previous / next deity
    SPACE          next deity
    P              save a screenshot to dakini_temple.png

Usage:
    python temple.py

Requirements:
    - the shoonyakasha package installed (pip install .)
    - run from this directory, so temple_pipeline.json and shaders/ resolve
    - optional: Pillow with raqm support, for the Devanagari labels
"""

import hashlib
import math
import random
from pathlib import Path

import shoonyakasha as sk
from shoonyakasha import keys

sk.shaders.compile_dir("shaders")

WIDTH, HEIGHT = 1280, 720

engine = sk.Engine(
    title="Shoonyakasha - Dakini Temple",
    width=WIDTH, height=HEIGHT,
    pipeline_json_path="temple_pipeline.json",
)

# Render layers of temple_pipeline.json's passes.
LAYER_MANDALA = 1   # solid layers, premultiplied alpha
LAYER_GLOW = 2      # additive light
LAYER_OVERLAY = 4   # vignette
LAYER_TEXT = 8

# Layer ids understood by shaders/mandala.frag.
SKY, FIRE, VAJRA, PETALS, PALACE, LOTUS, CENTRE, GLOW, VIGNETTE, LAMP = range(10)

LAMP_RADIUS = 2.95
EMBER_COUNT = 90
EMBER_ESCAPE_RADIUS = 5.2

# Seconds to dim the mandala away, and again to bring the next one back.
FADE_SECONDS = 0.9
# How bright the sky stays while the mandala is dimmed.
SKY_AT_DARKEST = 0.2


class Deity:
    def __init__(self, name, mantra, sanskrit_name, sanskrit_mantra,
                 lamp, lamp_core, aura, bindu, embers, centre_spin):
        self.name = name              # ASCII, for the engine's text labels
        self.mantra = mantra          # ASCII transliteration
        self.sanskrit_name = sanskrit_name      # Devanagari
        self.sanskrit_mantra = sanskrit_mantra  # Devanagari
        self.lamp = lamp              # (r, g, b) of the lamp halos
        self.lamp_core = lamp_core    # (r, g, b) of the flame at each lamp's heart
        self.aura = aura              # (r, g, b, a) of the light around the centre
        self.bindu = bindu            # (r, g, b, a) of the bright point at the centre
        self.embers = embers          # (r, g, b) choices for new embers
        self.centre_spin = centre_spin  # radians/sec of the centre symbol


# The order matches the DEITY_* ids in shaders/mandala.frag.
DEITIES = [
    Deity("VAJRAYOGINI", "OM VAJRAYOGINI HUM PHAT",
          "वज्रयोगिनी", "ॐ वज्रयोगिनी हूं फट्",
          lamp=(1.0, 0.55, 0.18), lamp_core=(1.0, 0.90, 0.65),
          aura=(0.75, 0.05, 0.02, 0.35), bindu=(1.0, 0.85, 0.75, 0.55),
          embers=((1.0, 0.75, 0.35), (1.0, 0.35, 0.18), (1.0, 0.92, 0.80)),
          centre_spin=-0.12),
    Deity("GREEN TARA", "OM TARE TUTTARE TURE SOHA",
          "श्यामतारा", "ॐ तारे तुत्तारे तुरे स्वाहा",
          lamp=(0.35, 1.0, 0.50), lamp_core=(0.90, 1.0, 0.80),
          aura=(0.05, 0.60, 0.25, 0.35), bindu=(0.80, 1.0, 0.90, 0.50),
          embers=((0.40, 1.0, 0.55), (1.0, 0.85, 0.40), (0.85, 1.0, 0.90)),
          centre_spin=0.06),
    Deity("WHITE TARA", "OM TARE TUTTARE TURE MAMA AYUR PUNYE JNANA PUTRIM KURU SOHA",
          "सिततारा", "ॐ तारे तुत्तारे तुरे मम आयुः पुण्य ज्ञान पुष्टिं कुरु स्वाहा",
          lamp=(0.85, 0.90, 1.0), lamp_core=(1.0, 1.0, 1.0),
          aura=(0.60, 0.70, 1.0, 0.30), bindu=(1.0, 1.0, 1.0, 0.40),
          embers=((1.0, 1.0, 1.0), (0.70, 0.80, 1.0), (1.0, 0.90, 0.70)),
          centre_spin=0.0),
    Deity("VAJRAPANI", "OM VAJRAPANI HUM",
          "वज्रपाणि", "ॐ वज्रपाणि हूं",
          lamp=(0.30, 0.50, 1.0), lamp_core=(0.80, 0.90, 1.0),
          aura=(0.10, 0.20, 1.0, 0.22), bindu=(1.0, 0.80, 0.45, 0.15),
          embers=((0.35, 0.55, 1.0), (0.60, 0.95, 1.0), (1.0, 0.80, 0.40)),
          centre_spin=0.0),
]

rng = random.Random(108)


# ── Devanagari labels ──────────────────────────────────────────────

DEVANAGARI_FONT = "fonts/NotoSansDevanagari-Regular.ttf"
LATIN_FONT = "fonts/Roboto-Regular.ttf"
LABEL_DIR = Path("generated")
LABEL_GOLD = (1.0, 0.80, 0.42)
LABEL_MAX_WIDTH = WIDTH - 80


def devanagari_available():
    """Pillow with the raqm (HarfBuzz) layout, and the font in the asset root."""
    try:
        from PIL import features
    except ImportError:
        return False
    return bool(features.check("raqm")) and sk.assets.exists(DEVANAGARI_FONT)


def render_label(lines):
    """Render centred lines of white text over a soft black halo into a PNG.

    `lines` holds (text, font asset path, pixel size, opacity) tuples, drawn
    top to bottom. A line wider than LABEL_MAX_WIDTH is set smaller until it
    fits. The panel's tint multiplies the PNG, so the text takes the tint
    colour and the halo stays dark, keeping the text readable over the fire.
    Returns (absolute path, width, height); an existing PNG rendered from the
    same text, sizes and fonts is reused.
    """
    from PIL import Image, ImageDraw, ImageFilter, ImageFont

    fonts = {font for _, font, _, _ in lines}
    stamp = sorted((font, Path(sk.assets.locate(font)).stat().st_mtime_ns) for font in fonts)
    key = hashlib.sha1(repr((lines, LABEL_MAX_WIDTH, stamp)).encode("utf-8")).hexdigest()[:16]
    path = (LABEL_DIR / ("label_%s.png" % key)).resolve()
    if path.exists():
        with Image.open(path) as cached:
            return str(path), cached.width, cached.height

    laid_out = []
    for text, font_path, size, opacity in lines:
        while True:
            font = ImageFont.truetype(str(sk.assets.locate(font_path)), size,
                                      layout_engine=ImageFont.Layout.RAQM)
            left, top, right, bottom = font.getbbox(text)
            if right - left <= LABEL_MAX_WIDTH or size <= 8:
                break
            size = int(size * LABEL_MAX_WIDTH / (right - left))
        ascent, descent = font.getmetrics()
        laid_out.append((text, font, opacity, left, right - left, ascent + descent))

    pad = 14   # room for the halo's blur
    width = max(w for *_, w, _ in laid_out) + 2 * pad
    height = sum(h for *_, h in laid_out) + 2 * pad
    coverage = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(coverage)
    y = pad
    for text, font, opacity, left, w, h in laid_out:
        draw.text(((width - w) / 2 - left, y), text, font=font, fill=int(255 * opacity))
        y += h

    halo = coverage.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.GaussianBlur(5))
    halo = halo.point(lambda v: int(v * 0.75))
    black = Image.new("L", (width, height), 0)
    white = Image.new("L", (width, height), 255)
    label = Image.alpha_composite(Image.merge("RGBA", (black, black, black, halo)),
                                  Image.merge("RGBA", (white, white, white, coverage)))
    LABEL_DIR.mkdir(exist_ok=True)
    label.save(path)
    return str(path), width, height


def label_panel(lines, anchor, edge_offset):
    """A screen-space panel showing render_label(lines), `edge_offset` pixels
    from the anchored edge to the panel's nearest edge (negative from the bottom)."""
    path, w, h = render_label(lines)
    centre_y = edge_offset + (h / 2 if edge_offset >= 0 else -h / 2)
    panel = engine.create_ui_panel(anchor=anchor, offset_pixels=(0, centre_y),
                                   size_pixels=(w, h), texture_path=path,
                                   color=(*LABEL_GOLD, 0.0))
    engine.scene.set_render_layer_mask(panel, LAYER_TEXT)
    return panel


# ── Custom ECS components ──────────────────────────────────────────

class Spin:
    def __init__(self, rate):
        self.rate = rate              # radians/sec, positive = counter-clockwise
        self.angle = 0.0


class Breathe:
    def __init__(self, size, rate, amount, phase=0.0):
        self.size = size              # resting quad size (width, height)
        self.rate = rate              # breaths/sec
        self.amount = amount          # relative size change at the peak
        self.phase = phase


class Light:
    def __init__(self, color, flicker=False, size=0.0, seed=0.0):
        self.color = color            # (r, g, b, a) before fading
        self.flicker = flicker        # butter lamps waver in strength and size
        self.size = size
        self.seed = seed


class Ember:
    def __init__(self, deity):
        self.respawn(deity, first=True)

    def respawn(self, deity, first=False):
        self.angle = rng.uniform(0.0, math.tau)
        self.radius = rng.uniform(0.3, 4.5) if first else rng.uniform(0.2, 0.9)
        self.outward = rng.uniform(0.18, 0.45)     # world units/sec
        self.swirl = rng.uniform(0.15, 0.40)       # radians/sec
        self.size = rng.uniform(0.10, 0.24)
        self.color = rng.choice(deity.embers)


class Temple:
    """Which deity is shown, and the fade between one and the next."""

    def __init__(self):
        self.index = 0
        self.target = 0
        self.fade = 1.0               # 1 = mandala fully shown, 0 = dimmed away
        self.elapsed = 0.0
        self.sky = None
        self.solid_layers = []        # quads on LAYER_MANDALA and LAYER_OVERLAY
        self.centre = None
        self.lamps = []               # (halo, core) pairs
        self.aura = None
        self.bindu = None
        self.embers = []
        self.label_panels = []        # (title, mantra) Devanagari panels, one pair per deity
        self.title = None             # engine text labels, used when there are no panels
        self.mantra = None

    @property
    def deity(self):
        return DEITIES[self.index]

    def request(self, index):
        self.target = index % len(DEITIES)

    def show(self, index):
        """Swap the dimmed mandala over to another deity.

        Engine text labels are only retitled here, not faded: every change
        to a label re-bakes its glyph entities. The Devanagari panels fade
        in temple_system instead.
        """
        self.index = index
        deity = self.deity
        engine.set_custom_float("deity", float(index))
        if self.title is not None:
            engine.scene.set_text(self.title, deity.name)
            engine.scene.set_text(self.mantra, deity.mantra)
        spin = engine.ecs.get_component(self.centre, "Spin")
        spin.rate = deity.centre_spin
        spin.angle = 0.0
        for halo, core in self.lamps:
            engine.ecs.get_component(halo, "Light").color = (*deity.lamp, 1.0)
            engine.ecs.get_component(core, "Light").color = (*deity.lamp_core, 1.0)
        engine.ecs.get_component(self.aura, "Light").color = deity.aura
        engine.ecs.get_component(self.bindu, "Light").color = deity.bindu

    def step(self, dt):
        """Dims towards the requested deity, swaps at the darkest point, then returns."""
        self.elapsed += dt
        rate = dt / FADE_SECONDS
        if self.target != self.index:
            self.fade = max(0.0, self.fade - rate)
            if self.fade == 0.0:
                self.show(self.target)
        else:
            self.fade = min(1.0, self.fade + rate)

    @property
    def eased(self):
        return self.fade * self.fade * (3.0 - 2.0 * self.fade)


temple = Temple()


def mandala_quad(layer, size, render_layer=LAYER_MANDALA, sort_key=0,
                 pos=(0.0, 0.0, 0.0), tint=(1.0, 1.0, 1.0, 1.0)):
    """A quad painted by mandala.frag with the given layer id."""
    entity = engine.create_sprite(world_pos=pos, texture_path="", size=size, tint=tint)
    engine.scene.set_material_vec4(entity, "shape", (float(layer), size[0], size[1], 0.0))
    engine.scene.set_render_layer_mask(entity, render_layer)
    engine.scene.set_sort_key(entity, sort_key)
    return entity


def build_mandala():
    temple.sky = mandala_quad(SKY, (30.0, 18.0), sort_key=0)

    fire = mandala_quad(FIRE, (9.8, 9.8), sort_key=1)
    engine.ecs.set_component(fire, "Spin", Spin(0.03))

    vajra = mandala_quad(VAJRA, (8.0, 8.0), sort_key=2)
    engine.ecs.set_component(vajra, "Spin", Spin(-0.05))

    petals = mandala_quad(PETALS, (7.2, 7.2), sort_key=3)
    engine.ecs.set_component(petals, "Spin", Spin(0.04))

    palace = mandala_quad(PALACE, (5.4, 5.4), sort_key=4)

    lotus = mandala_quad(LOTUS, (3.6, 3.6), sort_key=5)
    engine.ecs.set_component(lotus, "Spin", Spin(0.08))
    engine.ecs.set_component(lotus, "Breathe", Breathe((3.6, 3.6), rate=0.18, amount=0.035))

    temple.centre = mandala_quad(CENTRE, (2.3, 2.3), sort_key=6)
    engine.ecs.set_component(temple.centre, "Spin", Spin(temple.deity.centre_spin))

    temple.solid_layers = [fire, vajra, petals, palace, lotus, temple.centre]

    for i in range(8):
        angle = math.tau * (i + 0.5) / 8.0
        pos = (LAMP_RADIUS * math.cos(angle), LAMP_RADIUS * math.sin(angle), 0.0)
        temple.solid_layers.append(mandala_quad(LAMP, (0.42, 0.42), sort_key=7, pos=pos))

        halo = mandala_quad(GLOW, (1.4, 1.4), render_layer=LAYER_GLOW, sort_key=1, pos=pos)
        engine.ecs.set_component(halo, "Light", Light((*temple.deity.lamp, 1.0),
                                                      flicker=True, size=1.4, seed=i * 1.7))
        core = mandala_quad(GLOW, (0.32, 0.32), render_layer=LAYER_GLOW, sort_key=2, pos=pos)
        engine.ecs.set_component(core, "Light", Light((*temple.deity.lamp_core, 1.0),
                                                      flicker=True, size=0.32, seed=i * 1.7))
        temple.lamps.append((halo, core))

    temple.aura = mandala_quad(GLOW, (4.6, 4.6), render_layer=LAYER_GLOW, sort_key=0)
    engine.ecs.set_component(temple.aura, "Light", Light(temple.deity.aura))
    engine.ecs.set_component(temple.aura, "Breathe", Breathe((4.6, 4.6), rate=0.18, amount=0.08))

    temple.bindu = mandala_quad(GLOW, (0.9, 0.9), render_layer=LAYER_GLOW, sort_key=0)
    engine.ecs.set_component(temple.bindu, "Light", Light(temple.deity.bindu))
    engine.ecs.set_component(temple.bindu, "Breathe", Breathe((0.9, 0.9), rate=0.18, amount=0.15))

    for _ in range(EMBER_COUNT):
        spark = mandala_quad(GLOW, (0.1, 0.1), render_layer=LAYER_GLOW, sort_key=3,
                             tint=(0.0, 0.0, 0.0, 0.0))
        engine.ecs.set_component(spark, "Ember", Ember(temple.deity))
        temple.embers.append(spark)

    mandala_quad(VIGNETTE, (22.0, 12.4), render_layer=LAYER_OVERLAY)


def build_text():
    if devanagari_available():
        for deity in DEITIES:
            title = label_panel([(deity.sanskrit_name, DEVANAGARI_FONT, 46, 1.0),
                                 (deity.name, LATIN_FONT, 17, 0.7)],
                                sk.UI_ANCHOR_TOP_CENTER, 0)
            mantra = label_panel([(deity.sanskrit_mantra, DEVANAGARI_FONT, 32, 1.0),
                                  (deity.mantra, LATIN_FONT, 15, 0.65)],
                                 sk.UI_ANCHOR_BOTTOM_CENTER, -14)
            temple.label_panels.append((title, mantra))
    else:
        build_ascii_labels()

    hint = engine.create_text(
        text="LEFT / RIGHT  change deity",
        anchor=sk.UI_ANCHOR_BOTTOM_RIGHT,
        offset_pixels=(-20, -16),
        font_path="fonts/Roboto-Regular.ttf",
        font_size=14.0,
        color=(1.0, 0.90, 0.75, 0.35),
    )
    engine.scene.set_text_align(hint, sk.TEXT_ALIGN_RIGHT)
    engine.scene.set_text_layer_mask(hint, LAYER_TEXT)


def build_ascii_labels():
    """Title and mantra as engine text, used when Devanagari cannot be rendered."""
    temple.title = engine.create_text(
        text=temple.deity.name,
        anchor=sk.UI_ANCHOR_TOP_CENTER,
        offset_pixels=(0, 44),
        font_path="fonts/Roboto-Regular.ttf",
        font_size=30.0,
        color=(1.0, 0.80, 0.42, 0.9),
    )
    temple.mantra = engine.create_text(
        text=temple.deity.mantra,
        anchor=sk.UI_ANCHOR_BOTTOM_CENTER,
        offset_pixels=(0, -34),
        font_path="fonts/Roboto-Regular.ttf",
        font_size=24.0,
        color=(1.0, 0.80, 0.42, 0.9),
    )
    for label in (temple.title, temple.mantra):
        engine.scene.set_text_align(label, sk.TEXT_ALIGN_CENTER)
        engine.scene.set_text_layer_mask(label, LAYER_TEXT)


def register_systems():
    # Priorities below 0 run before TransformSystem, so this frame's
    # rotations and scales reach this frame's world matrices.

    def temple_system(dt):
        temple.step(dt)
        fade = temple.eased
        for entity in temple.solid_layers:
            engine.scene.set_sprite_color(entity, (fade, fade, fade, fade))
        sky = SKY_AT_DARKEST + (1.0 - SKY_AT_DARKEST) * fade
        engine.scene.set_sprite_color(temple.sky, (sky, sky, sky, 1.0))
        # Only the current deity's labels show; they dim with the mandala.
        for index, panels in enumerate(temple.label_panels):
            alpha = 0.95 * fade if index == temple.index else 0.0
            for panel in panels:
                engine.scene.set_sprite_color(panel, (*LABEL_GOLD, alpha))
        return True

    engine.ecs.add_system("Temple", temple_system, priority=-10)

    def spin_system(dt):
        for entity in engine.ecs.find_entities_with_component("Spin"):
            spin = engine.ecs.get_component(entity, "Spin")
            spin.angle += spin.rate * dt
            engine.scene.set_rotation(entity, (0.0, 0.0, spin.angle))
        return True

    engine.ecs.add_system("Spin", spin_system, priority=-5)

    def breathe_system(dt):
        for entity in engine.ecs.find_entities_with_component("Breathe"):
            breath = engine.ecs.get_component(entity, "Breathe")
            s = 1.0 + breath.amount * math.sin(math.tau * breath.rate * temple.elapsed + breath.phase)
            engine.scene.set_scale(entity, (breath.size[0] * s, breath.size[1] * s, 1.0))
        return True

    engine.ecs.add_system("Breathe", breathe_system, priority=-5)

    def light_system(dt):
        fade = temple.eased
        for entity in engine.ecs.find_entities_with_component("Light"):
            light = engine.ecs.get_component(entity, "Light")
            r, g, b, a = light.color
            if light.flicker:
                t = temple.elapsed * 7.0 + light.seed
                wave = 0.5 * math.sin(t) + 0.3 * math.sin(t * 2.3 + 1.1) + 0.2 * math.sin(t * 5.7 + 2.9)
                a *= 0.78 + 0.22 * wave
                s = light.size * (0.94 + 0.06 * wave)
                engine.scene.set_scale(entity, (s, s, 1.0))
            engine.scene.set_sprite_color(entity, (r, g, b, a * fade))
        return True

    engine.ecs.add_system("Light", light_system, priority=-5)

    def ember_system(dt):
        for entity in temple.embers:
            ember = engine.ecs.get_component(entity, "Ember")
            ember.radius += ember.outward * dt
            ember.angle += ember.swirl * dt
            if ember.radius > EMBER_ESCAPE_RADIUS:
                ember.respawn(temple.deity)
            # Fade in near the centre and out towards the edge of space.
            life = ember.radius / EMBER_ESCAPE_RADIUS
            alpha = math.sin(math.pi * life) ** 1.5 * 0.9
            engine.scene.set_position(entity, (ember.radius * math.cos(ember.angle),
                                               ember.radius * math.sin(ember.angle), 0.0))
            engine.scene.set_scale(entity, (ember.size, ember.size, 1.0))
            engine.scene.set_sprite_color(entity, (*ember.color, alpha))
        return True

    engine.ecs.add_system("Embers", ember_system, priority=-5)


class Controls:
    def __init__(self):
        # is_key_down reports the current state, so the previous state is kept
        # to act once per press.
        self._was_down = {}

    def pressed(self, key):
        down = engine.input.is_key_down(key)
        was_down = self._was_down.get(key, False)
        self._was_down[key] = down
        return down and not was_down

    def update(self, dt):
        right, space = self.pressed(keys.RIGHT), self.pressed(keys.SPACE)
        if right or space:
            temple.request(temple.target + 1)
        if self.pressed(keys.LEFT):
            temple.request(temple.target - 1)
        if self.pressed(keys.P):
            path = "dakini_temple.png"
            print("screenshot ->", path if engine.capture_screenshot(path) else "failed")


def on_init():
    engine.set_custom_float("deity", 0.0)
    engine.create_camera(pos=(0.0, 0.0, 9.5), fov=60.0, speed=0.0)
    build_mandala()
    build_text()
    register_systems()


controls = Controls()
engine.set_on_init(on_init)
engine.set_on_update(controls.update)

if __name__ == "__main__":
    engine.run()

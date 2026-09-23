"""
Shoonyakasha Dakini Temple

A top-down mandala palace for Vajrayogini, seen from above:

  - a ring of wisdom fire, a vajra fence and a ring of lotus petals
  - a palace with five-coloured walls and a gate in each direction, its
    ground split into the blue, yellow, red and green quarters
  - an eight-petalled lotus holding the red dharmodaya, a pair of
    interlocking triangles with a turning joy-swirl at its centre
  - eight butter lamps and embers that spiral out into space

Every mandala layer is a single quad whose pattern is drawn procedurally by
shaders/mandala.frag. Python picks the layer through a "shape" material
parameter, and Python ECS systems turn, breathe and flicker the quads.

Keys:
    P    save a screenshot to dakini_temple.png

Usage:
    python temple.py

Requirements:
    - the shoonyakasha package installed (pip install .)
    - run from this directory, so temple_pipeline.json and shaders/ resolve
"""

import math
import random

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
SKY, FIRE, VAJRA, PETALS, PALACE, LOTUS, DHARMODAYA, GLOW, VIGNETTE, LAMP = range(10)

LAMP_RADIUS = 2.95
EMBER_COUNT = 90
EMBER_ESCAPE_RADIUS = 5.2

rng = random.Random(108)


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


class Flicker:
    def __init__(self, color, size, seed):
        self.color = color            # (r, g, b) of the flame light
        self.size = size
        self.seed = seed


class Ember:
    def __init__(self):
        self.respawn(first=True)

    def respawn(self, first=False):
        self.angle = rng.uniform(0.0, math.tau)
        self.radius = rng.uniform(0.3, 4.5) if first else rng.uniform(0.2, 0.9)
        self.outward = rng.uniform(0.18, 0.45)     # world units/sec
        self.swirl = rng.uniform(0.15, 0.40)       # radians/sec
        self.size = rng.uniform(0.10, 0.24)
        self.color = rng.choice(((1.0, 0.75, 0.35), (1.0, 0.35, 0.18), (1.0, 0.92, 0.80)))


elapsed = 0.0
embers = []


def mandala_quad(layer, size, render_layer=LAYER_MANDALA, sort_key=0,
                 pos=(0.0, 0.0, 0.0), tint=(1.0, 1.0, 1.0, 1.0)):
    """A quad painted by mandala.frag with the given layer id."""
    entity = engine.create_sprite(world_pos=pos, texture_path="", size=size, tint=tint)
    engine.scene.set_material_vec4(entity, "shape", (float(layer), size[0], size[1], 0.0))
    engine.scene.set_render_layer_mask(entity, render_layer)
    engine.scene.set_sort_key(entity, sort_key)
    return entity


def build_mandala():
    mandala_quad(SKY, (30.0, 18.0), sort_key=0)

    fire = mandala_quad(FIRE, (9.8, 9.8), sort_key=1)
    engine.ecs.set_component(fire, "Spin", Spin(0.03))

    vajra = mandala_quad(VAJRA, (8.0, 8.0), sort_key=2)
    engine.ecs.set_component(vajra, "Spin", Spin(-0.05))

    petals = mandala_quad(PETALS, (7.2, 7.2), sort_key=3)
    engine.ecs.set_component(petals, "Spin", Spin(0.04))

    mandala_quad(PALACE, (5.4, 5.4), sort_key=4)

    lotus = mandala_quad(LOTUS, (3.6, 3.6), sort_key=5)
    engine.ecs.set_component(lotus, "Spin", Spin(0.08))
    engine.ecs.set_component(lotus, "Breathe", Breathe((3.6, 3.6), rate=0.18, amount=0.035))

    dharmodaya = mandala_quad(DHARMODAYA, (2.3, 2.3), sort_key=6)
    engine.ecs.set_component(dharmodaya, "Spin", Spin(-0.12))

    for i in range(8):
        angle = math.tau * (i + 0.5) / 8.0
        pos = (LAMP_RADIUS * math.cos(angle), LAMP_RADIUS * math.sin(angle), 0.0)
        mandala_quad(LAMP, (0.42, 0.42), sort_key=7, pos=pos)

        light = mandala_quad(GLOW, (1.4, 1.4), render_layer=LAYER_GLOW, sort_key=1, pos=pos)
        engine.ecs.set_component(light, "Flicker", Flicker((1.0, 0.55, 0.18), 1.4, seed=i * 1.7))
        core = mandala_quad(GLOW, (0.32, 0.32), render_layer=LAYER_GLOW, sort_key=2, pos=pos)
        engine.ecs.set_component(core, "Flicker", Flicker((1.0, 0.90, 0.65), 0.32, seed=i * 1.7))

    aura = mandala_quad(GLOW, (4.6, 4.6), render_layer=LAYER_GLOW, sort_key=0,
                        tint=(0.75, 0.05, 0.02, 0.35))
    engine.ecs.set_component(aura, "Breathe", Breathe((4.6, 4.6), rate=0.18, amount=0.08))
    bindu = mandala_quad(GLOW, (0.9, 0.9), render_layer=LAYER_GLOW, sort_key=0,
                         tint=(1.0, 0.85, 0.75, 0.55))
    engine.ecs.set_component(bindu, "Breathe", Breathe((0.9, 0.9), rate=0.18, amount=0.15))

    for _ in range(EMBER_COUNT):
        spark = mandala_quad(GLOW, (0.1, 0.1), render_layer=LAYER_GLOW, sort_key=3,
                             tint=(0.0, 0.0, 0.0, 0.0))
        engine.ecs.set_component(spark, "Ember", Ember())
        embers.append(spark)

    mandala_quad(VIGNETTE, (22.0, 12.4), render_layer=LAYER_OVERLAY)


def build_text():
    mantra = engine.create_text(
        text="OM VAJRAYOGINI HUM PHAT",
        anchor=sk.UI_ANCHOR_BOTTOM_CENTER,
        offset_pixels=(0, -34),
        font_path="fonts/Roboto-Regular.ttf",
        font_size=26.0,
        color=(1.0, 0.80, 0.42, 0.9),
    )
    engine.scene.set_text_align(mantra, sk.TEXT_ALIGN_CENTER)
    engine.scene.set_text_layer_mask(mantra, LAYER_TEXT)


def register_systems():
    # Priorities below 0 run before TransformSystem, so this frame's
    # rotations and scales reach this frame's world matrices.

    def time_system(dt):
        global elapsed
        elapsed += dt
        return True

    engine.ecs.add_system("TempleTime", time_system, priority=-10)

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
            s = 1.0 + breath.amount * math.sin(math.tau * breath.rate * elapsed + breath.phase)
            engine.scene.set_scale(entity, (breath.size[0] * s, breath.size[1] * s, 1.0))
        return True

    engine.ecs.add_system("Breathe", breathe_system, priority=-5)

    def flicker_system(dt):
        for entity in engine.ecs.find_entities_with_component("Flicker"):
            flame = engine.ecs.get_component(entity, "Flicker")
            t = elapsed * 7.0 + flame.seed
            wave = 0.5 * math.sin(t) + 0.3 * math.sin(t * 2.3 + 1.1) + 0.2 * math.sin(t * 5.7 + 2.9)
            strength = 0.78 + 0.22 * wave
            engine.scene.set_sprite_color(entity, (*flame.color, strength))
            s = flame.size * (0.94 + 0.06 * wave)
            engine.scene.set_scale(entity, (s, s, 1.0))
        return True

    engine.ecs.add_system("Flicker", flicker_system, priority=-5)

    def ember_system(dt):
        for entity in embers:
            ember = engine.ecs.get_component(entity, "Ember")
            ember.radius += ember.outward * dt
            ember.angle += ember.swirl * dt
            if ember.radius > EMBER_ESCAPE_RADIUS:
                ember.respawn()
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
        # to save once per press.
        self._was_down = False

    def update(self, dt):
        down = engine.input.is_key_down(keys.P)
        if down and not self._was_down:
            path = "dakini_temple.png"
            print("screenshot ->", path if engine.capture_screenshot(path) else "failed")
        self._was_down = down


def on_init():
    engine.create_camera(pos=(0.0, 0.0, 9.5), fov=60.0, speed=0.0)
    build_mandala()
    build_text()
    register_systems()


controls = Controls()
engine.set_on_init(on_init)
engine.set_on_update(controls.update)

if __name__ == "__main__":
    engine.run()

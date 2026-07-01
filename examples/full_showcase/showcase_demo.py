"""
Shoonyakasha Full Showcase Demo

A single scene combining everything from the last few engine passes:
  - World-space sprites (orbiting "orbs") and screen-space UI (HUD panels
    + text), all through the sprite_geometry render path.
  - Three custom blend modes rendered in the SAME frame, routed per-entity
    via RenderableTagComponent's 8-bit render layer mask and three
    layer-filtered passes in showcase_pipeline.json:
      layer 1 -> AlphaPass         ("blending": "alpha")       - orbs, HUD
      layer 2 -> AdditivePass      ("blending": "additive")    - glow halos
      layer 4 -> PremultipliedPass ("blending": "custom", premultiplied) - vignette
  - Custom Python-defined ECS components (Orbit, Pulse, Health, Regen) -
    plain classes, no engine base class required.
  - Custom Python-defined ECS systems (engine.ecs.add_system), including
    one that deliberately fails every frame to demonstrate the
    auto-disable-after-N-consecutive-failures behavior.

Usage:
    python showcase_demo.py

Requirements:
    - Build with -DBUILD_PYTHON=ON
    - Compile shaders/sprite.vert and shaders/sprite.frag to .spv
      (glslc shaders/sprite.vert -o shaders/sprite.vert.spv, same for .frag)
    - Run from this directory (so showcase_pipeline.json and shaders/ resolve)
    - Have orb.png, glow.png, vignette.png, panel.png images available, or
      point the texture_path arguments below at your own art. A flat-colored
      fallback (no texture) still demonstrates the blend modes correctly.
"""

import math
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'python'))

import shoonyakasha as sk

engine = sk.Engine(
    title="Shoonyakasha Full Showcase",
    width=1280, height=720,
    pipeline_json_path="showcase_pipeline.json",
)

# Render layers used by showcase_pipeline.json's three passes.
LAYER_ALPHA = 1
LAYER_ADDITIVE = 2
LAYER_PREMULTIPLIED = 4


# ── Custom ECS components ──────────────────────────────────────────
# Plain Python classes - no Shoonyakasha base class required. Attached to
# entities via engine.ecs.set_component(entity, "TypeName", instance).

class Orbit:
    def __init__(self, center, radius, speed, angle, glow_entity):
        self.center = center          # (x, y, z) orbit center, world space
        self.radius = radius
        self.speed = speed            # radians/sec
        self.angle = angle
        self.glow_entity = glow_entity  # additive halo sprite that follows this orb


class Pulse:
    def __init__(self, rate, base_alpha=0.5, amplitude=0.5, phase=0.0):
        self.rate = rate              # cycles/sec
        self.base_alpha = base_alpha
        self.amplitude = amplitude
        self.phase = phase


class Health:
    def __init__(self, hp=100.0):
        self.hp = hp
        self.max_hp = hp


class Regen:
    def __init__(self, rate=4.0):
        self.rate = rate


orbs = []
hud_text_entity = None
status_text_entity = None
elapsed_time = 0.0


def make_orb(index, count):
    """One orbiting sprite (alpha layer) plus its additive glow halo."""
    angle = (2.0 * math.pi / count) * index
    center = (0.0, 0.0, 0.0)
    radius = 2.0 + 0.5 * index

    orb = engine.create_sprite(
        world_pos=(center[0] + radius * math.cos(angle), center[1] + radius * math.sin(angle), 0.0),
        texture_path="orb.png",
        size=(0.5, 0.5),
        tint=(1.0, 1.0, 1.0, 1.0),
    )
    engine.scene.set_render_layer_mask(orb, LAYER_ALPHA)

    glow = engine.create_sprite(
        world_pos=(center[0] + radius * math.cos(angle), center[1] + radius * math.sin(angle), 0.0),
        texture_path="glow.png",
        size=(1.2, 1.2),
        tint=(1.0, 0.8, 0.4, 0.6),
    )
    engine.scene.set_render_layer_mask(glow, LAYER_ADDITIVE)

    engine.ecs.set_component(orb, "Orbit", Orbit(center, radius, speed=0.4 + 0.1 * index,
                                                  angle=angle, glow_entity=glow))
    engine.ecs.set_component(orb, "Pulse", Pulse(rate=0.6 + 0.15 * index, phase=index * 0.7))
    engine.ecs.set_component(orb, "Health", Health(hp=60.0 + 10.0 * index))
    engine.ecs.set_component(orb, "Regen", Regen(rate=3.0))

    return orb


def on_init():
    global hud_text_entity, status_text_entity

    engine.create_camera(pos=(0.0, 0.0, 8.0), fov=60.0)

    for i in range(5):
        orbs.append(make_orb(i, 5))

    # Full-screen vignette using the custom premultiplied-alpha pass -
    # covers the whole viewport with a soft-edged overlay.
    vignette = engine.create_ui_panel(
        anchor=sk.UI_ANCHOR_MIDDLE_CENTER,
        offset_pixels=(0, 0),
        size_pixels=(1280, 720),
        texture_path="vignette.png",
        color=(0.0, 0.0, 0.05, 0.35),
    )
    engine.scene.set_render_layer_mask(vignette, LAYER_PREMULTIPLIED)

    # HUD: top-left panel + text (alpha layer, drawn after the vignette
    # since AlphaPass runs before PremultipliedPass would normally put it
    # underneath - reorder with sortKey if you want the HUD on top of the
    # vignette instead; passes already run Alpha -> Additive -> Premultiplied).
    hud_panel = engine.create_ui_panel(
        anchor=sk.UI_ANCHOR_TOP_LEFT,
        offset_pixels=(140, 45),
        size_pixels=(260, 70),
        texture_path="panel.png",
        color=(0.08, 0.08, 0.12, 0.85),
    )
    engine.scene.set_render_layer_mask(hud_panel, LAYER_ALPHA)

    hud_text_entity = engine.create_text(
        text=f"Orbs: {len(orbs)}",
        anchor=sk.UI_ANCHOR_TOP_LEFT,
        offset_pixels=(20, 40),
        font_path="font.ttf",
        font_size=22.0,
        color=(1.0, 1.0, 1.0, 1.0),
    )
    engine.scene.set_text_layer_mask(hud_text_entity, LAYER_ALPHA)

    status_text_entity = engine.create_text(
        text="Systems: OK",
        anchor=sk.UI_ANCHOR_BOTTOM_LEFT,
        offset_pixels=(90, -30),
        font_path="font.ttf",
        font_size=18.0,
        color=(0.6, 1.0, 0.6, 1.0),
    )
    engine.scene.set_text_layer_mask(status_text_entity, LAYER_ALPHA)

    register_systems()


def register_systems():
    # Runs before TransformSystem (priority 0) so this frame's moved
    # positions are reflected in the world matrices used for rendering.
    def orbit_system(dt):
        for orb in orbs:
            orbit = engine.ecs.get_component(orb, "Orbit")
            orbit.angle += orbit.speed * dt
            x = orbit.center[0] + orbit.radius * math.cos(orbit.angle)
            y = orbit.center[1] + orbit.radius * math.sin(orbit.angle)
            engine.scene.set_position(orb, (x, y, 0.0))
            engine.scene.set_position(orbit.glow_entity, (x, y, 0.0))
        return True

    engine.ecs.add_system("Orbit", orbit_system, priority=-5)

    def pulse_system(dt):
        global elapsed_time
        elapsed_time += dt
        for orb in orbs:
            orbit = engine.ecs.get_component(orb, "Orbit")
            pulse = engine.ecs.get_component(orb, "Pulse")
            alpha = pulse.base_alpha + pulse.amplitude * math.sin(
                2.0 * math.pi * pulse.rate * elapsed_time + pulse.phase)
            alpha = max(0.0, min(1.0, alpha))
            engine.scene.set_sprite_color(orbit.glow_entity, (1.0, 0.8, 0.4, alpha))
        return True

    engine.ecs.add_system("Pulse", pulse_system, priority=-4)

    def regen_system(dt):
        for entity in engine.ecs.find_entities_with_component("Health"):
            if not engine.ecs.has_component(entity, "Regen"):
                continue
            health = engine.ecs.get_component(entity, "Health")
            regen = engine.ecs.get_component(entity, "Regen")
            health.hp = min(health.max_hp, health.hp + regen.rate * dt)
        return True

    engine.ecs.add_system("Regen", regen_system, priority=50)

    def hud_system(dt):
        total_hp = sum(engine.ecs.get_component(o, "Health").hp for o in orbs)
        engine.scene.set_text(hud_text_entity, f"Orbs: {len(orbs)}  Total HP: {total_hp:.0f}")
        return True

    engine.ecs.add_system("Hud", hud_system, priority=70)

    # Deliberately broken system: raises every frame. Demonstrates that a
    # single misbehaving Python system doesn't crash the frame (the
    # exception is caught, its traceback printed) and auto-disables after
    # 3 consecutive failures rather than spamming stderr forever.
    def flaky_system(dt):
        raise RuntimeError("simulated failure in a custom system")

    engine.ecs.add_system("Flaky", flaky_system, priority=80, max_consecutive_failures=3)


def on_update(dt):
    if engine.ecs.has_system("Flaky") and not engine.ecs.is_system_enabled("Flaky"):
        failures = engine.ecs.get_system_failure_count("Flaky")
        print(f"[Python] 'Flaky' auto-disabled after {failures} consecutive failures")
        engine.ecs.remove_system("Flaky")
        engine.scene.set_text(status_text_entity, "Systems: OK ('Flaky' auto-disabled)")


engine.set_on_init(on_init)
engine.set_on_update(on_update)
engine.run()

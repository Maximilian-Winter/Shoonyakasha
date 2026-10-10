"""
Shoonyakasha Canvas UI Demo

A HUD and a terminal in the world, both built with engine.ui on the default
pipeline:

- top left, a title and a line of Unicode text;
- bottom left, settings: a toggle that spins the box, an exposure slider and
  a button that resets both;
- beside the box, a world canvas with a button that counts its clicks and a
  slider for the box's height;
- bottom right, whether the pointer is over the UI.

Usage:
    python ui_canvas_demo.py [--screenshot path.png]

With --screenshot it saves frame 120 to the path.

Controls: left mouse uses the widgets, on the screen and on the terminal.
WASD/Q/E and the right mouse button fly the camera; with the mouse captured
(ESC) the centre of the screen is the pointer.

Requirements:
    - the shoonyakasha package installed (pip install .)
"""

import math
import sys

import shoonyakasha as sk

engine = sk.Engine(title="Shoonyakasha Canvas UI", width=1280, height=720)

screenshot = sys.argv[sys.argv.index("--screenshot") + 1] if "--screenshot" in sys.argv else ""

ui_state = {}
frame = {"count": 0, "clicks": 0, "angle": 0.0}


def on_init():
    ui = engine.ui
    engine.create_camera(pos=(0.0, 1.2, 4.0), fov=60.0, speed=4.0, near_plane=0.05, far_plane=100.0)
    engine.create_directional_light(direction=(-0.4, -0.7, -0.5), intensity=3.0)
    box = engine.load_gltf_scene("models/Box.gltf")
    ui_state["box"] = box.entities[0]

    # ── HUD: a canvas laid out at 1280 x 720, scaled to the window ──
    hud = ui.create_canvas(reference_size=(1280, 720))
    title_font = ui.load_font("fonts/PlayfairDisplay.ttf")

    header = ui.create_panel(hud, size=(420, 110), color=(0.08, 0.09, 0.12, 0.82))
    ui.set_anchor(header, (0, 0))
    ui.set_position(header, (24, 24))
    title = ui.create_text(header, "Śūnyākāśa", size=36, color=(0.95, 0.85, 0.55, 1), font=title_font)
    ui.set_rect(title, anchor_min=(0, 0), anchor_max=(1, 0), pivot=(0, 0), position=(16, 12), size=(-32, 46))
    line = ui.create_text(header, "Canvas UI — Привет, мир", size=20)
    ui.set_rect(line, anchor_min=(0, 0), anchor_max=(1, 0), pivot=(0, 0), position=(16, 66), size=(-32, 28))

    settings = ui.create_panel(hud, size=(340, 170), color=(0.08, 0.09, 0.12, 0.82))
    ui.set_anchor(settings, (0, 1))
    ui.set_position(settings, (24, -24))
    spin = ui.create_toggle(settings, "Spin the box", is_on=True, size=(300, 28))
    ui.set_anchor(spin, (0, 0))
    ui.set_position(spin, (16, 16))
    exposure_label = ui.create_text(settings, "Exposure  0.0 EV", size=18)
    ui.set_rect(exposure_label, anchor_min=(0, 0), anchor_max=(1, 0), pivot=(0, 0), position=(16, 56), size=(-32, 22))
    exposure = ui.create_slider(settings, min=-2.0, max=2.0, value=0.0, size=(300, 24))
    ui.set_anchor(exposure, (0, 0))
    ui.set_position(exposure, (16, 82))
    reset = ui.create_button(settings, "Reset", size=(140, 40))
    ui.set_anchor(reset, (0, 0))
    ui.set_position(reset, (16, 118))

    status = ui.create_text(hud, "", size=20, color=(0.6, 0.95, 0.7, 1))
    ui.set_text_align(status, sk.TEXT_ALIGN_RIGHT, sk.TEXT_ALIGN_BOTTOM)
    ui.set_rect(status, anchor_min=(0, 1), anchor_max=(1, 1), pivot=(1, 1), position=(-24, -24), size=(-48, 30))

    # ── A terminal in the world, left of the box ──
    terminal = ui.create_world_canvas(pixel_size=(1024, 512), world_size=(1.6, 0.8), emission=2.0)
    engine.scene.set_position(terminal, (-1.5, 0.9, 0.3))
    engine.scene.set_rotation(terminal, (0.0, 0.45, 0.0))
    ui.set_canvas_clear_color(terminal, (0.03, 0.06, 0.1, 1.0))
    heading = ui.create_text(terminal, "World canvas", size=64, color=(0.95, 0.85, 0.55, 1), font=title_font)
    ui.set_rect(heading, anchor_min=(0, 0), anchor_max=(1, 0), pivot=(0, 0), position=(40, 30), size=(-80, 80))
    clicks = ui.create_text(terminal, "0 clicks", size=40, color=(0.6, 0.95, 0.7, 1))
    ui.set_rect(clicks, anchor_min=(0, 0), anchor_max=(1, 0), pivot=(0, 0), position=(40, 140), size=(-80, 50))
    counter = ui.create_button(terminal, "Click me", size=(300, 80))
    ui.set_anchor(counter, (0, 0))
    ui.set_position(counter, (40, 220))
    height_label = ui.create_text(terminal, "Box height", size=34)
    ui.set_rect(height_label, anchor_min=(0, 0), anchor_max=(1, 0), pivot=(0, 0), position=(40, 340), size=(-80, 40))
    height = ui.create_slider(terminal, min=0.0, max=1.0, value=0.0, size=(944, 60))
    ui.set_anchor(height, (0, 0))
    ui.set_position(height, (40, 400))

    ui_state.update(spin=spin, exposure=exposure, exposure_label=exposure_label, reset=reset,
                    status=status, clicks=clicks, counter=counter, height=height)


def on_update(dt):
    ui = engine.ui
    s = ui_state
    frame["count"] += 1

    # Edges are from the input pass of the frame before.
    if ui.was_clicked(s["reset"]):
        ui.set_toggle_value(s["spin"], False)
        ui.set_slider_value(s["exposure"], 0.0)
        frame["angle"] = 0.0
    if ui.was_clicked(s["counter"]):
        frame["clicks"] += 1
        ui.set_text(s["clicks"], "%d click%s" % (frame["clicks"], "" if frame["clicks"] == 1 else "s"))

    ev = ui.slider_value(s["exposure"])
    ui.set_text(s["exposure_label"], "Exposure  %+.1f EV" % ev)
    engine.set_custom_float("default.exposure", 2.0 ** ev)

    if ui.toggle_value(s["spin"]):
        frame["angle"] = (frame["angle"] + 0.8 * dt) % (2.0 * math.pi)
    engine.scene.set_rotation(s["box"], (0.0, frame["angle"], 0.0))
    engine.scene.set_position(s["box"], (0.0, ui.slider_value(s["height"]), 0.0))

    over = "over the UI" if ui.pointer_over_ui else "over the scene"
    ui.set_text(s["status"], "Pointer %s" % over)


def on_post_render():
    if screenshot and frame["count"] == 120:
        engine.capture_screenshot(screenshot)


engine.set_on_init(on_init)
engine.set_on_update(on_update)
engine.set_on_post_render(on_post_render)
engine.run()

"""
Shoonyakasha Canvas UI Showcase

A tour of engine.ui on the default pipeline, in one window:

- a sidebar with three tabs, each a page of elements shown and hidden with
  set_visible:
    Scene    toggles and sliders that drive the scene: spin, sun shadows,
             sun intensity, the box's colour and the exposure
    Text     the nine text alignments, three fonts, Unicode, a font size
             slider and wrapping in a clipped box
    Widgets  a click counter, a button enabled by a toggle, a stepped
             slider, and a log whose rows are created and destroyed at
             runtime, scrolled inside a clipping panel
- a readout of the pointer: which canvas, where on it, what it hovers
- an About dialog on a second canvas drawn on top, which blocks the
  pointer to everything under it while it is open
- a kiosk in the world, a canvas on a quad you can click in 3D, with its
  own counter, a slider for its glow and a button that changes its
  resolution

The sidebar and dialog frames are a 9-slice texture this script draws itself
(written to generated/frame.png), so panels of any size keep sharp corners.

Usage:
    python ui_showcase.py [--screenshot path.png] [--tab scene|text|widgets] [--about]

--screenshot saves frame 120 to the path, with six rows in the log; --tab
picks the page shown first; --about opens with the About dialog showing.

Controls: left mouse uses the UI, on the screen and on the kiosk. WASD/Q/E
and the right mouse button fly the camera; with the mouse captured (ESC),
the centre of the screen is the pointer.

Requirements:
    - the shoonyakasha package installed (pip install .)
"""

import colorsys
import math
import struct
import sys
import zlib
from pathlib import Path

import shoonyakasha as sk

HERE = Path(__file__).resolve().parent

# ─── Colours (sRGB, straight alpha) ───────────────────────────────

INK = (0.92, 0.93, 0.95, 1.0)
MUTED = (0.62, 0.66, 0.72, 1.0)
GOLD = (0.95, 0.85, 0.55, 1.0)
GREEN = (0.6, 0.95, 0.7, 1.0)
FRAME = (0.10, 0.11, 0.15, 0.94)
SURFACE = (0.22, 0.24, 0.30, 1.0)
ACCENT = (0.30, 0.52, 0.85, 1.0)


def arg(name, default=""):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


SCREENSHOT = arg("--screenshot")
FIRST_TAB = ("scene", "text", "widgets").index(arg("--tab", "scene"))


# ─── A 9-slice frame texture, drawn here ──────────────────────────

FRAME_SIZE, FRAME_RADIUS, FRAME_BORDER = 48, 12.0, 14


def write_frame_png(path):
    """A white rounded rectangle with a brighter 2-pixel rim, to be tinted.
    The corners are FRAME_BORDER pixels: the part a 9-slice keeps unstretched."""
    size, r = FRAME_SIZE, FRAME_RADIUS

    def pixel(x, y):
        px, py = x + 0.5, y + 0.5
        cx = min(max(px, r), size - r)
        cy = min(max(py, r), size - r)
        inside = r - math.hypot(px - cx, py - cy)          # distance in from the edge
        coverage = min(max(inside, 0.0), 1.0)
        level = 255 if inside < 2.5 else 196
        return level, level, level, round(255 * coverage)

    rows = b"".join(b"\x00" + bytes(c for x in range(size) for c in pixel(x, y)) for y in range(size))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(rows))
                     + chunk(b"IEND", b""))


FRAME_PNG = HERE / "generated" / "frame.png"
write_frame_png(FRAME_PNG)


# ─── The engine ───────────────────────────────────────────────────

engine = sk.Engine(title="Shoonyakasha Canvas UI Showcase", width=1280, height=720,
                   environment_color=(0.32, 0.36, 0.44))

ui_ = None          # engine.ui, from on_init
w = {}              # widgets by name
names = {}          # entity -> name, for the pointer readout
state = {"frame": 0, "tab": FIRST_TAB, "angle": 0.0, "clicks": 0, "kiosk_clicks": 0, "rows": []}


def named(name, entity):
    names[entity] = name
    w[name] = entity
    return entity


def place(element, x, y, width=None, height=None, anchor=(0, 0)):
    """Put `element` by the corner given by `anchor` at (x, y) from the same
    corner of its parent; give it a size if one is passed."""
    ui_.set_anchor(element, anchor)
    ui_.set_position(element, (x, y))
    if width is not None:
        ui_.set_size(element, (width, height))
    return element


def label(parent, text, x, y, width, height=24, size=18, color=INK, font=0):
    """Text in a box of its own at (x, y) from the parent's top-left."""
    t = ui_.create_text(parent, text, size=size, color=color, font=font)
    ui_.set_rect(t, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0), position=(x, y), size=(width, height))
    return t


def frame_panel(parent, width, height, color=FRAME):
    return ui_.create_panel(parent, size=(width, height), color=color, texture=str(FRAME_PNG),
                            border=(FRAME_BORDER,) * 4)


def root_of(entity):
    while engine.scene.get_parent(entity) != sk.NULL_ENTITY:
        entity = engine.scene.get_parent(entity)
    return entity


# ─── Building ─────────────────────────────────────────────────────

def on_init():
    global ui_
    ui_ = engine.ui
    fonts = {"serif": ui_.load_font("fonts/PlayfairDisplay.ttf")}

    build_scene()
    hud = named("HUD canvas", ui_.create_canvas(reference_size=(1280, 720)))
    build_sidebar(hud, fonts)
    build_pointer_readout(hud)
    build_dialog(fonts)
    build_kiosk(fonts)
    show_tab(state["tab"])
    ui_.set_visible(w["dialog"], "--about" in sys.argv)


def build_scene():
    engine.create_camera(pos=(0.0, 1.3, 4.6), fov=55.0, speed=4.0, near_plane=0.05, far_plane=100.0)
    w["sun"] = engine.create_directional_light(direction=(-0.5, -0.7, -0.45), intensity=3.0)
    engine.scene.set_light_cast_shadows(w["sun"], True)
    engine.set_sun_shadows(cascades=4, max_distance=20.0)

    # load_gltf_scene returns the mesh entities. Box.gltf's root node, their
    # parent, turns the mesh 90 degrees about X; placing and turning the
    # root instead keeps the unit cube upright.
    box = engine.load_gltf_scene("models/Box.gltf").entities
    w["box"], w["box_meshes"] = root_of(box[0]), box

    floor = engine.load_gltf_scene("models/Box.gltf").entities
    root = root_of(floor[0])
    engine.scene.set_rotation(root, (0.0, 0.0, 0.0))
    engine.scene.set_scale(root, (12.0, 0.2, 12.0))
    engine.scene.set_position(root, (0.0, -0.6, 0.0))
    for mesh in floor:
        engine.scene.set_material_vec4(mesh, "baseColorFactor", (0.42, 0.40, 0.37, 1.0))
        engine.scene.set_material_float(mesh, "roughnessFactor", 0.9)


def build_sidebar(hud, fonts):
    sidebar = named("Sidebar", frame_panel(hud, 380, 0))
    ui_.set_rect(sidebar, anchor_min=(0, 0), anchor_max=(0, 1), pivot=(0, 0.5), position=(20, 0), size=(380, -40))

    label(sidebar, "Canvas UI", 22, 16, 330, 44, size=34, color=GOLD, font=fonts["serif"])

    tabs = []
    for i, title in enumerate(("Scene", "Text", "Widgets")):
        tabs.append(named("%s tab" % title, place(ui_.create_button(sidebar, title, size=(108, 34)),
                                                  22 + i * 114, 70)))
    w["tabs"] = tabs

    pages = []
    for title in ("Scene", "Text", "Widgets"):
        page = named("%s page" % title, ui_.create_element(sidebar, (0, 0)))
        ui_.set_rect(page, anchor_min=(0, 0), anchor_max=(1, 1), pivot=(0.5, 1), position=(0, -16), size=(-44, -136))
        pages.append(page)
    w["pages"] = pages

    build_scene_page(pages[0])
    build_text_page(pages[1], fonts)
    build_widgets_page(pages[2])

    named("About button", place(ui_.create_button(hud, "About", size=(120, 40)), -24, -24, anchor=(1, 1)))


def build_scene_page(page):
    named("Spin toggle", place(ui_.create_toggle(page, "Spin the box", is_on=True, size=(330, 28)), 0, 0))
    named("Shadows toggle", place(ui_.create_toggle(page, "Sun shadows", is_on=True, size=(330, 28)), 0, 38))

    w["sun_label"] = label(page, "", 0, 86, 330)
    named("Sun slider", place(ui_.create_slider(page, min=0.0, max=8.0, value=3.0, size=(336, 24)), 0, 112))

    w["hue_label"] = label(page, "", 0, 150, 330)
    named("Hue slider", place(ui_.create_slider(page, min=0.0, max=1.0, value=0.0, size=(336, 24)), 0, 176))
    w["swatch"] = named("Colour swatch", place(ui_.create_image(page, (28, 28)), 0, 150, anchor=(1, 0)))

    w["ev_label"] = label(page, "", 0, 214, 330)
    named("Exposure slider", place(ui_.create_slider(page, min=-2.0, max=2.0, value=0.0, size=(336, 24)), 0, 240))

    named("Reset button", place(ui_.create_button(page, "Reset", size=(140, 40)), 0, 290))
    label(page, "The scene reads these each frame in on_update.", 0, 348, 336, 48, size=16, color=MUTED)


def build_text_page(page, fonts):
    label(page, "Alignment", 0, 0, 330, size=16, color=MUTED)
    horizontal = (sk.TEXT_ALIGN_LEFT, sk.TEXT_ALIGN_CENTER, sk.TEXT_ALIGN_RIGHT)
    vertical = (sk.TEXT_ALIGN_TOP, sk.TEXT_ALIGN_MIDDLE, sk.TEXT_ALIGN_BOTTOM)
    for row, v in enumerate(vertical):
        for column, h in enumerate(horizontal):
            cell = place(ui_.create_panel(page, (106, 44), color=SURFACE), column * 115, 24 + row * 50)
            ui_.set_raycast_target(cell, False)
            text = ui_.create_text(cell, "TMB"[row] + "LCR"[column], size=16, color=INK)
            ui_.set_rect(text, anchor_min=(0, 0), anchor_max=(1, 1), size=(-12, -8))
            ui_.set_text_align(text, h, v)

    label(page, "Fonts and Unicode", 0, 184, 330, size=16, color=MUTED)
    label(page, "Śūnyākāśa", 0, 206, 336, 40, size=32, color=GOLD, font=fonts["serif"])
    label(page, "Привет · Γειά σου · Ünïcödé", 0, 248, 336, 28, size=20)

    w["size_label"] = label(page, "", 0, 286, 230, size=16, color=MUTED)
    named("Size slider", place(ui_.create_slider(page, min=12.0, max=40.0, value=18.0, size=(336, 22)), 0, 310))
    named("Wrap toggle", place(ui_.create_toggle(page, "Wrap", is_on=True, size=(100, 24)), 0, 284, anchor=(1, 0)))

    box = named("Clipped text box", place(ui_.create_panel(page, (336, 110), color=(0.06, 0.07, 0.09, 1.0)), 0, 344))
    ui_.set_clip(box, True)
    w["paragraph"] = ui_.create_text(box, "Text is laid out from signed distance fields, so it stays sharp at "
                                          "any size. This box clips whatever overflows it.", size=18, color=INK)
    ui_.set_rect(w["paragraph"], anchor_min=(0, 0), anchor_max=(1, 1), size=(-20, -16))


def build_widgets_page(page):
    named("Click me", place(ui_.create_button(page, "Click me", size=(150, 40)), 0, 0))
    w["clicks_label"] = label(page, "0 clicks", 166, 8, 170, size=20, color=GREEN)

    named("Enable toggle", place(ui_.create_toggle(page, "Enable the button below", size=(330, 28)), 0, 56))
    named("Gated button", place(ui_.create_button(page, "Only when enabled", size=(230, 40)), 0, 92))
    set_gated(False)

    w["steps_label"] = label(page, "", 0, 148, 330)
    named("Steps slider", place(ui_.create_slider(page, min=0.0, max=10.0, value=5.0, size=(336, 24)), 0, 174))
    ui_.set_slider_range(w["Steps slider"], 0.0, 10.0, True)

    named("Add row", place(ui_.create_button(page, "Add row", size=(140, 36)), 0, 216))
    named("Clear rows", place(ui_.create_button(page, "Clear", size=(100, 36)), 150, 216))

    log = named("Log", place(ui_.create_panel(page, (336, 170), color=(0.06, 0.07, 0.09, 1.0)), 0, 262))
    ui_.set_clip(log, True)
    w["log_content"] = ui_.create_element(log, (0, 0))
    ui_.set_rect(w["log_content"], anchor_min=(0, 0), anchor_max=(1, 0), pivot=(0, 0), size=(0, 0))


def build_pointer_readout(hud):
    panel = named("Pointer panel", place(frame_panel(hud, 330, 120), -20, 20, anchor=(1, 0)))
    label(panel, "Pointer", 18, 12, 290, size=16, color=MUTED)
    w["pointer_text"] = label(panel, "", 18, 34, 294, 82, size=17)


def build_dialog(fonts):
    dialog_canvas = named("Dialog canvas", ui_.create_canvas(reference_size=(1280, 720), sort_order=10))
    # A dim layer over the whole screen: an image, so a raycast target, so
    # nothing below takes the pointer while the dialog is open.
    dim = named("Dialog backdrop", ui_.create_image(dialog_canvas, (0, 0), color=(0.0, 0.0, 0.0, 0.55)))
    ui_.set_rect(dim, anchor_min=(0, 0), anchor_max=(1, 1), size=(0, 0))
    box = named("Dialog", frame_panel(dim, 560, 280, color=FRAME[:3] + (1.0,)))
    label(box, "About this showcase", 30, 24, 500, 44, size=30, color=GOLD, font=fonts["serif"])
    label(box, "Everything here is built from Python with engine.ui: canvases, panels, text and "
               "widgets, on the screen and on a quad in the world. This dialog is a second "
               "canvas with a higher sort order; its backdrop stops clicks reaching the HUD.",
          30, 78, 500, 120, size=18)
    named("Close button", place(ui_.create_button(box, "Close", size=(130, 40)), -30, -24, anchor=(1, 1)))
    w["dialog"] = dim


def build_kiosk(fonts):
    kiosk = named("Kiosk canvas", ui_.create_world_canvas(pixel_size=(800, 600), world_size=(1.2, 0.9), emission=2.0))
    engine.scene.set_position(kiosk, (1.75, 0.75, 0.3))
    engine.scene.set_rotation(kiosk, (0.0, -0.5, 0.0))
    ui_.set_canvas_clear_color(kiosk, (0.04, 0.05, 0.08, 1.0))

    label(kiosk, "Kiosk", 40, 30, 720, 80, size=64, color=GOLD, font=fonts["serif"])
    label(kiosk, "A canvas on a quad in the scene. Click it like the screen.", 40, 120, 720, 40, size=28,
          color=MUTED)
    named("Touch me", place(ui_.create_button(kiosk, "Touch me", size=(320, 90)), 40, 190))
    w["kiosk_clicks"] = label(kiosk, "0 touches", 390, 215, 370, 50, size=40, color=GREEN)

    w["glow_label"] = label(kiosk, "", 40, 320, 720, 40, size=28)
    named("Glow slider", place(ui_.create_slider(kiosk, min=0.5, max=4.0, value=2.0, size=(720, 60)), 40, 366))
    named("Resolution button", place(ui_.create_button(kiosk, "", size=(420, 80)), 40, 470))
    w["kiosk"] = kiosk
    w["kiosk_pixels"] = (800, 600)


# ─── Each frame ───────────────────────────────────────────────────

def show_tab(index):
    state["tab"] = index
    for i, (tab, page) in enumerate(zip(w["tabs"], w["pages"])):
        ui_.set_visible(page, i == index)
        ui_.set_color(tab, ACCENT if i == index else SURFACE)


def update_scene_page(dt):
    if ui_.was_clicked(w["Reset button"]):
        ui_.set_toggle_value(w["Spin toggle"], True)
        ui_.set_toggle_value(w["Shadows toggle"], True)
        ui_.set_slider_value(w["Sun slider"], 3.0)
        ui_.set_slider_value(w["Hue slider"], 0.0)
        ui_.set_slider_value(w["Exposure slider"], 0.0)
        state["angle"] = 0.0

    if ui_.toggle_value(w["Spin toggle"]):
        state["angle"] = (state["angle"] + 0.7 * dt) % (2.0 * math.pi)
    engine.scene.set_rotation(w["box"], (0.0, state["angle"], 0.0))

    if ui_.value_changed(w["Shadows toggle"]):
        engine.scene.set_light_cast_shadows(w["sun"], ui_.toggle_value(w["Shadows toggle"]))

    sun = ui_.slider_value(w["Sun slider"])
    engine.scene.set_light_intensity(w["sun"], sun)
    ui_.set_text(w["sun_label"], "Sun intensity  %.1f" % sun)

    hue = ui_.slider_value(w["Hue slider"])
    colour = colorsys.hsv_to_rgb(hue, 0.75, 0.8) + (1.0,)
    for mesh in w["box_meshes"]:
        engine.scene.set_material_vec4(mesh, "baseColorFactor", colour)
    ui_.set_color(w["swatch"], colour)
    ui_.set_text(w["hue_label"], "Box colour  hue %.2f" % hue)

    ev = ui_.slider_value(w["Exposure slider"])
    engine.set_custom_float("default.exposure", 2.0 ** ev)
    ui_.set_text(w["ev_label"], "Exposure  %+.1f EV" % ev)


def update_text_page():
    size = ui_.slider_value(w["Size slider"])
    ui_.set_font_size(w["paragraph"], size)
    ui_.set_text_wrap(w["paragraph"], ui_.toggle_value(w["Wrap toggle"]))
    ui_.set_text(w["size_label"], "Font size  %d" % size)


def set_gated(enabled):
    """Enable or disable the gated button. Disabled, its own panel dims; its
    label, a child element, is dimmed here."""
    button = w["Gated button"]
    ui_.set_interactable(button, enabled)
    for child in engine.scene.get_children(button):
        ui_.set_color(child, INK if enabled else MUTED[:3] + (0.6,))


def add_row():
    rows = state["rows"]
    index = len(rows)
    tint = (0.16, 0.18, 0.23, 1.0) if index % 2 == 0 else (0.12, 0.13, 0.17, 1.0)
    row = ui_.create_panel(w["log_content"], (0, 26), color=tint)
    ui_.set_rect(row, anchor_min=(0, 0), anchor_max=(1, 0), pivot=(0, 0), position=(0, index * 28), size=(0, 26))
    ui_.set_raycast_target(row, False)
    text = ui_.create_text(row, "Row %d, added at frame %d" % (index + 1, state["frame"]), size=16)
    ui_.set_rect(text, anchor_min=(0, 0), anchor_max=(1, 1), size=(-16, 0))
    ui_.set_text_align(text, sk.TEXT_ALIGN_LEFT, sk.TEXT_ALIGN_MIDDLE)
    rows.append(row)


def update_widgets_page():
    if ui_.was_clicked(w["Click me"]):
        state["clicks"] += 1
        ui_.set_text(w["clicks_label"], "%d click%s" % (state["clicks"], "" if state["clicks"] == 1 else "s"))

    if ui_.value_changed(w["Enable toggle"]):
        set_gated(ui_.toggle_value(w["Enable toggle"]))
    if ui_.was_clicked(w["Gated button"]):
        add_row()

    ui_.set_text(w["steps_label"], "Steps (whole numbers)  %d" % ui_.slider_value(w["Steps slider"]))

    if ui_.was_clicked(w["Add row"]):
        add_row()
    if ui_.was_clicked(w["Clear rows"]):
        for row in state["rows"]:
            engine.scene.destroy_entity(row)     # its text goes with it
        state["rows"].clear()

    # Scroll the log so the newest row is at the bottom of the clipped panel.
    visible = ui_.get_rect(w["Log"])[3]
    total = len(state["rows"]) * 28
    ui_.set_size(w["log_content"], (0, total))
    ui_.set_position(w["log_content"], (0, min(0.0, visible - total)))


def update_pointer_readout():
    canvas = ui_.pointer_canvas
    hovered = ui_.hovered_element
    x, y = ui_.pointer_position
    lines = ["over the UI" if ui_.pointer_over_ui else "over the scene",
             "canvas  %s" % (names.get(canvas, "none") if canvas != sk.NULL_ENTITY else "none"),
             "at  (%.0f, %.0f)" % (x, y) if canvas != sk.NULL_ENTITY else "at  -",
             "hovering  %s" % (names.get(hovered, "a widget") if hovered != sk.NULL_ENTITY else "nothing")]
    ui_.set_text(w["pointer_text"], "\n".join(lines))


def update_kiosk():
    if ui_.was_clicked(w["Touch me"]):
        state["kiosk_clicks"] += 1
        n = state["kiosk_clicks"]
        ui_.set_text(w["kiosk_clicks"], "%d touch%s" % (n, "" if n == 1 else "es"))

    glow = ui_.slider_value(w["Glow slider"])
    engine.scene.set_material_vec4(w["kiosk"], "emissiveFactor", (glow, glow, glow, 0.0))
    ui_.set_text(w["glow_label"], "Glow  %.1f" % glow)

    if ui_.was_clicked(w["Resolution button"]):
        w["kiosk_pixels"] = (400, 300) if w["kiosk_pixels"] == (800, 600) else (800, 600)
        ui_.set_canvas_pixel_size(w["kiosk"], w["kiosk_pixels"])
    ui_.set_text(w["Resolution button"], "Resolution  %d x %d" % w["kiosk_pixels"])


def on_update(dt):
    state["frame"] += 1
    for i, tab in enumerate(w["tabs"]):
        if ui_.was_clicked(tab):
            show_tab(i)

    if ui_.was_clicked(w["About button"]):
        ui_.set_visible(w["dialog"], True)
    if ui_.was_clicked(w["Close button"]):
        ui_.set_visible(w["dialog"], False)

    update_scene_page(dt)
    update_text_page()
    update_widgets_page()
    update_pointer_readout()
    update_kiosk()

    if SCREENSHOT and state["frame"] == 30:
        for _ in range(6):
            add_row()       # something in the log for the screenshot


def on_post_render():
    if SCREENSHOT and state["frame"] == 120:
        engine.capture_screenshot(SCREENSHOT)


engine.set_on_init(on_init)
engine.set_on_update(on_update)
engine.set_on_post_render(on_post_render)
engine.run()

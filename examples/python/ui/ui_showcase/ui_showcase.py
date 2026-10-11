"""
Shoonyakasha Canvas UI Showcase

A tour of engine.ui on the default pipeline, in one window:

- a sidebar with three tabs, each a page of elements shown and hidden with
  set_visible, and laid out by layout groups: the pages are columns, their
  label-and-widget pairs rows, the alignment samples a grid:
    Scene    toggles and sliders that drive the scene: spin, sun shadows,
             sun intensity, the box's colour and the exposure
    Text     the nine text alignments, fonts, Unicode with shaped Devanagari
             and Tibetan from fallback fonts, a right-to-left line of Arabic
             and Hebrew with a number and a Latin word in it, rich text
             with a link that toggles wrapping, a font size
             slider and wrapping in a clipped box, Thai wrapping between
             words though it has no spaces
    Widgets  a click counter, a button enabled by a toggle, a toggle that
             lays the whole HUD out right to left, a stepped
             slider, a name field that greets in the log on Enter and,
             on Windows, shows what an input method composes in it, and a
             log whose rows are created and destroyed at runtime, in a
             scroll view (wheel, drag or scrollbar; a drag pulls past the
             ends and springs back), filtered by a search
             field as you type
- a readout of the pointer: which canvas, where on it, what it hovers
- an About dialog on a second canvas drawn on top, which blocks the
  pointer to everything under it while it is open, with a notes field of
  many lines
- a kiosk in the world, a canvas on a quad you can click in 3D, with its
  own counter, a slider for its glow and a button that changes its
  resolution. A pillar stands in front of it: where the pillar hides the
  kiosk, the pointer is over the scene, not the kiosk. The kiosk keeps its
  brightness when the exposure slider changes the rest of the scene

The sidebar and dialog frames are a 9-slice texture this script draws itself
(written to generated/frame.png), so panels of any size keep sharp corners.

Usage:
    python ui_showcase.py [--screenshot path.png] [--tab scene|text|widgets] [--about] [--rtl]

--screenshot saves frame 120 to the path, with twelve rows in the log, a
name typed and the log filtered; --tab picks the page shown first; --about
opens with the About dialog showing; --rtl starts with the HUD laid out
right to left.

Controls: left mouse uses the UI, on the screen and on the kiosk. WASD/Q/E
and the right mouse button fly the camera; with the mouse captured (ESC),
the centre of the screen is the pointer. While a text field has focus the
keys go to it; Tab moves on, ESC leaves it.

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
    # Characters Roboto and Playfair lack, Devanagari, Tibetan, Arabic, Hebrew
    # and Thai, come from these; HarfBuzz shapes their conjuncts, stacks and
    # joined letters.
    ui_.set_default_font_fallbacks([ui_.load_font("fonts/NotoSansDevanagari-Regular.ttf"),
                                    ui_.load_font("fonts/NotoSerifTibetan.ttf"),
                                    ui_.load_font("fonts/NotoSansArabic-Regular.ttf"),
                                    ui_.load_font("fonts/NotoSansHebrew-Regular.ttf"),
                                    ui_.load_font("fonts/NotoSansThai-Regular.ttf")])

    build_scene()
    hud = named("HUD canvas", ui_.create_canvas(reference_size=(1280, 720)))
    build_sidebar(hud, fonts)
    build_pointer_readout(hud)
    build_dialog(fonts)
    build_kiosk(fonts)
    show_tab(state["tab"])
    ui_.set_visible(w["dialog"], "--about" in sys.argv)
    if "--rtl" in sys.argv:
        ui_.set_toggle_value(w["RTL toggle"], True)
        set_right_to_left(True)


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

    # A pillar between the camera and the kiosk, hiding part of it.
    pillar = engine.load_gltf_scene("models/Box.gltf").entities
    root = named("Pillar", root_of(pillar[0]))
    engine.scene.set_rotation(root, (0.0, 0.0, 0.0))
    engine.scene.set_scale(root, (0.14, 1.6, 0.14))
    engine.scene.set_position(root, (1.3, 0.3, 1.3))
    for mesh in pillar:
        engine.scene.set_material_vec4(mesh, "baseColorFactor", (0.55, 0.52, 0.48, 1.0))


PAGE_WIDTH = 336


def row(parent, height, spacing=8):
    """A row across the whole page width, its children centred vertically."""
    r = ui_.create_horizontal_layout(parent, size=(PAGE_WIDTH, height), spacing=spacing)
    ui_.set_layout_element(r, preferred=(PAGE_WIDTH, -1))
    ui_.set_layout_alignment(r, (0, 0.5))
    return r


def stretch(element):
    """Make an element in a row take the row's spare width."""
    ui_.set_layout_element(element, flexible=(1, -1))
    return element


def build_sidebar(hud, fonts):
    # The frame is placed by its anchors; a column inside it, by its own,
    # holds the title, the tab row and the page area, one under the other.
    sidebar = named("Sidebar", frame_panel(hud, 380, 0))
    ui_.set_rect(sidebar, anchor_min=(0, 0), anchor_max=(0, 1), pivot=(0, 0.5), position=(20, 0), size=(380, -40))
    column = ui_.create_vertical_layout(sidebar, spacing=14, padding=(22, 16, 22, 16))
    ui_.set_rect(column, anchor_min=(0, 0), anchor_max=(1, 1), size=(0, 0))

    ui_.create_text(column, "Canvas UI", size=34, color=GOLD, font=fonts["serif"])

    tab_row = row(column, 34, spacing=6)
    w["tabs"] = [named("%s tab" % title, ui_.create_button(tab_row, title, size=(108, 34)))
                 for title in ("Scene", "Text", "Widgets")]

    # The page area takes the rest of the column's height; the three pages
    # overlap in it, and only one is visible.
    area = ui_.create_element(column, (PAGE_WIDTH, 0))
    ui_.set_layout_element(area, flexible=(-1, 1))
    pages = []
    for title in ("Scene", "Text", "Widgets"):
        page = named("%s page" % title, ui_.create_vertical_layout(area, spacing=10))
        ui_.set_rect(page, anchor_min=(0, 0), anchor_max=(1, 1), size=(0, 0))
        pages.append(page)
    w["pages"] = pages

    build_scene_page(pages[0])
    build_text_page(pages[1], fonts)
    build_widgets_page(pages[2])

    named("About button", place(ui_.create_button(hud, "About", size=(120, 40)), -24, -24, anchor=(1, 1)))


def build_scene_page(page):
    named("Spin toggle", ui_.create_toggle(page, "Spin the box", is_on=True, size=(PAGE_WIDTH, 28)))
    named("Shadows toggle", ui_.create_toggle(page, "Sun shadows", is_on=True, size=(PAGE_WIDTH, 28)))

    w["sun_label"] = ui_.create_text(page, "", size=18)
    named("Sun slider", ui_.create_slider(page, min=0.0, max=8.0, value=3.0, size=(PAGE_WIDTH, 24)))

    hue_row = row(page, 28)
    w["hue_label"] = stretch(ui_.create_text(hue_row, "", size=18))
    w["swatch"] = named("Colour swatch", ui_.create_image(hue_row, (28, 28)))
    named("Hue slider", ui_.create_slider(page, min=0.0, max=1.0, value=0.0, size=(PAGE_WIDTH, 24)))

    w["ev_label"] = ui_.create_text(page, "", size=18)
    named("Exposure slider", ui_.create_slider(page, min=-2.0, max=2.0, value=0.0, size=(PAGE_WIDTH, 24)))

    named("Reset button", ui_.create_button(page, "Reset", size=(140, 40)))
    ui_.create_text(page, "The scene reads these each frame in on_update. This page is a column of "
                          "rows placed by a vertical layout group.", size=16, color=MUTED)


def build_text_page(page, fonts):
    ui_.create_text(page, "Alignment", size=16, color=MUTED)
    grid = ui_.create_grid_layout(page, cell_size=(106, 44), columns=3, spacing=(9, 6))
    horizontal = (sk.TEXT_ALIGN_LEFT, sk.TEXT_ALIGN_CENTER, sk.TEXT_ALIGN_RIGHT)
    vertical = (sk.TEXT_ALIGN_TOP, sk.TEXT_ALIGN_MIDDLE, sk.TEXT_ALIGN_BOTTOM)
    for r, v in enumerate(vertical):
        for c, h in enumerate(horizontal):
            cell = ui_.create_panel(grid, color=SURFACE)
            ui_.set_raycast_target(cell, False)
            text = ui_.create_text(cell, "TMB"[r] + "LCR"[c], size=16, color=INK)
            ui_.set_rect(text, anchor_min=(0, 0), anchor_max=(1, 1), size=(-12, -8))
            ui_.set_text_align(text, h, v)

    ui_.create_text(page, "Fonts and Unicode", size=16, color=MUTED)
    ui_.create_text(page, "Śūnyākāśa", size=32, color=GOLD, font=fonts["serif"])
    ui_.create_text(page, "Привет · Γειά σου · Ünïcödé", size=20)
    ui_.create_text(page, "नमस्ते · क्षत्रिय · བཀྲ་ཤིས་བདེ་ལེགས།", size=22)
    # Arabic first, so the line reads right to left and START aligns it right;
    # the number and the Latin word inside still read left to right, and the
    # brackets around the Hebrew are mirrored.
    rtl = ui_.create_text(page, "مرحبا بالعالم 2026 · Canvas UI · (שלום עולם)", size=20)
    ui_.set_text_align(rtl, sk.TEXT_ALIGN_START)
    # Markup, read once set_rich_text is on. Roboto has no bold or italic
    # font loaded, so those are drawn thicker and slanted; the link, a
    # raycast target, takes the link colour under the pointer.
    w["rich"] = ui_.create_text(page, "Rich text: <b>bold</b>, <i>italic</i>, <color=#E8C468>colour</color>, "
                                      "<size=22>size</size>, <u>underlined</u>, <s>struck</s> and "
                                      "<link=wrap>a link that toggles wrapping</link>", size=16)
    ui_.set_rich_text(w["rich"])

    size_row = row(page, 26)
    w["size_label"] = stretch(ui_.create_text(size_row, "", size=16, color=MUTED))
    named("Wrap toggle", ui_.create_toggle(size_row, "Wrap", is_on=True, size=(100, 24)))
    named("Size slider", ui_.create_slider(page, min=12.0, max=40.0, value=18.0, size=(PAGE_WIDTH, 22)))

    box = named("Clipped text box", ui_.create_panel(page, (PAGE_WIDTH, 90), color=(0.06, 0.07, 0.09, 1.0)))
    ui_.set_clip(box, True)
    # Tibetan has no spaces between words: its lines break after the tsheg (་).
    # Nor does Thai, whose lines break between the words a word list finds.
    w["paragraph"] = ui_.create_text(box, "Text is laid out from signed distance fields, so it stays sharp at "
                                          "any size. This box clips whatever overflows it. "
                                          "བཀྲ་ཤིས་བདེ་ལེགས། ཐུགས་རྗེ་ཆེ། སེམས་ཅན་ཐམས་ཅད་བདེ་བར་གྱུར་ཅིག། "
                                          "ภาษาไทยเขียนติดกันโดยไม่มีช่องว่างระหว่างคำ",
                                     size=18, color=INK)
    ui_.set_rect(w["paragraph"], anchor_min=(0, 0), anchor_max=(1, 1), size=(-20, -16))


def build_widgets_page(page):
    click_row = row(page, 40, spacing=16)
    named("Click me", ui_.create_button(click_row, "Click me", size=(150, 40)))
    w["clicks_label"] = ui_.create_text(click_row, "0 clicks", size=20, color=GREEN)

    named("Enable toggle", ui_.create_toggle(page, "Enable the button below", size=(PAGE_WIDTH, 28)))
    named("Gated button", ui_.create_button(page, "Only when enabled", size=(230, 40)))
    set_gated(False)
    # Mirrors the whole HUD: the sidebar moves right, rows and sliders run
    # from the right, toggles put their box on the right.
    named("RTL toggle", ui_.create_toggle(page, "Lay out right to left", size=(PAGE_WIDTH, 28)))

    w["steps_label"] = ui_.create_text(page, "", size=18)
    named("Steps slider", ui_.create_slider(page, min=0.0, max=10.0, value=5.0, size=(PAGE_WIDTH, 24)))
    ui_.set_slider_range(w["Steps slider"], 0.0, 10.0, True)

    name_row = row(page, 36, spacing=10)
    named("Name field", stretch(ui_.create_text_field(name_row, "Your name, then Enter", size=(200, 36))))
    ui_.set_text_field_options(w["Name field"], max_length=24)
    named("Greet button", ui_.create_button(name_row, "Greet", size=(100, 36)))
    # While an input method composes in the name field, what it composes.
    w["composition_label"] = ui_.create_text(page, "", size=16)
    ui_.set_visible(w["composition_label"], False)

    button_row = row(page, 36, spacing=10)
    named("Add row", ui_.create_button(button_row, "Add row", size=(140, 36)))
    named("Clear rows", ui_.create_button(button_row, "Clear", size=(100, 36)))
    named("Search field", ui_.create_text_field(page, "Filter the log", size=(PAGE_WIDTH, 32)))

    # The log is a scroll view over a dark panel: wheel, drag or scrollbar.
    log = named("Log", ui_.create_panel(page, (PAGE_WIDTH, 150), color=(0.06, 0.07, 0.09, 1.0)))
    view = named("Log view", ui_.create_scroll_view(log))
    ui_.set_rect(view, anchor_min=(0, 0), anchor_max=(1, 1), size=(0, 0))
    ui_.set_scroll_elastic(view, True)   # a drag pulls past the ends and springs back
    w["log_view"] = view
    w["log_content"] = ui_.scroll_content(view)


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
    box = named("Dialog", frame_panel(dim, 560, 400, color=FRAME[:3] + (1.0,)))
    label(box, "About this showcase", 30, 24, 500, 44, size=30, color=GOLD, font=fonts["serif"])
    label(box, "Everything here is built from Python with engine.ui: canvases, panels, text and "
               "widgets, on the screen and on a quad in the world. This dialog is a second "
               "canvas with a higher sort order; its backdrop stops clicks reaching the HUD.",
          30, 78, 500, 90, size=18)
    # A field of many lines: Enter starts a line, Up and Down move between
    # them, the wheel scrolls it.
    label(box, "Notes", 30, 172, 500, 22, size=16, color=MUTED)
    notes = named("Notes field", place(ui_.create_text_field(box, "Write some notes", size=(500, 110)), 30, 198))
    ui_.set_text_field_multi_line(notes)
    ui_.set_font_size(notes, 18)   # a field's text starts at half its height, sized for one line
    ui_.set_text(notes, "Enter starts a new line, Ctrl+Enter would submit.\n"
                        "Up and Down move between lines and keep to their column.\n"
                        "Long lines wrap at the width of the field instead of scrolling sideways.")
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
    if ui_.clicked_link(w["rich"]) == "wrap":
        ui_.set_toggle_value(w["Wrap toggle"], not ui_.toggle_value(w["Wrap toggle"]))
    size = ui_.slider_value(w["Size slider"])
    ui_.set_font_size(w["paragraph"], size)
    ui_.set_text_wrap(w["paragraph"], ui_.toggle_value(w["Wrap toggle"]))
    ui_.set_text(w["size_label"], "Font size  %d" % size)


def set_right_to_left(on):
    ui_.set_layout_direction(w["HUD canvas"], sk.LAYOUT_DIRECTION_RTL if on else sk.LAYOUT_DIRECTION_LTR)


def set_gated(enabled):
    """Enable or disable the gated button. Disabled, its panel and its label
    take the disabled colour."""
    ui_.set_interactable(w["Gated button"], enabled)


def add_row(message=None):
    rows = state["rows"]
    index = len(rows)
    message = message or "Row %d, added at frame %d" % (index + 1, state["frame"])
    tint = (0.16, 0.18, 0.23, 1.0) if index % 2 == 0 else (0.12, 0.13, 0.17, 1.0)
    # The view's column places each new row under the last, across its width;
    # scrolling to the end shows it once the next layout has placed it.
    entry = ui_.create_panel(w["log_content"], (0, 26), color=tint)
    ui_.set_raycast_target(entry, False)
    text = ui_.create_text(entry, message, size=16)
    ui_.set_rect(text, anchor_min=(0, 0), anchor_max=(1, 1), size=(-16, 0))
    ui_.set_text_align(text, sk.TEXT_ALIGN_LEFT, sk.TEXT_ALIGN_MIDDLE)
    rows.append((entry, message))
    filter_rows()
    ui_.set_scroll_position(w["log_view"], (0, 1))


def filter_rows():
    """Show the log rows that contain the search text; hidden rows take no
    room in the column."""
    query = ui_.get_text(w["Search field"]).casefold()
    for entry, message in state["rows"]:
        ui_.set_visible(entry, query in message.casefold())


def greet():
    name = ui_.get_text(w["Name field"]).strip()
    if name:
        add_row("Hello, %s!" % name)


def update_widgets_page():
    if ui_.was_clicked(w["Click me"]):
        state["clicks"] += 1
        ui_.set_text(w["clicks_label"], "%d click%s" % (state["clicks"], "" if state["clicks"] == 1 else "s"))

    if ui_.value_changed(w["Enable toggle"]):
        set_gated(ui_.toggle_value(w["Enable toggle"]))
    if ui_.value_changed(w["RTL toggle"]):
        set_right_to_left(ui_.toggle_value(w["RTL toggle"]))
    if ui_.was_clicked(w["Gated button"]):
        add_row()

    ui_.set_text(w["steps_label"], "Steps (whole numbers)  %d" % ui_.slider_value(w["Steps slider"]))

    if ui_.text_submitted(w["Name field"]) or ui_.was_clicked(w["Greet button"]):
        greet()
    composing = ui_.get_text_field_composition(w["Name field"])
    ui_.set_text(w["composition_label"], "Composing: " + composing)
    ui_.set_visible(w["composition_label"], bool(composing))   # takes no room while hidden
    if ui_.text_changed(w["Search field"]):
        filter_rows()

    if ui_.was_clicked(w["Add row"]):
        add_row()
    if ui_.was_clicked(w["Clear rows"]):
        for entry, _ in state["rows"]:
            engine.scene.destroy_entity(entry)     # its text goes with it
        state["rows"].clear()


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
        for _ in range(12):
            add_row()       # more than the log shows, so it scrolls
        ui_.set_text(w["Name field"], "Dakini")
        greet()
        ui_.set_text(w["Search field"], "1")
        filter_rows()
        ui_.focus(w["Name field"])
        ui_.select_all(w["Name field"])


def on_post_render():
    if SCREENSHOT and state["frame"] == 120:
        engine.capture_screenshot(SCREENSHOT)


engine.set_on_init(on_init)
engine.set_on_update(on_update)
engine.set_on_post_render(on_post_render)
engine.run()

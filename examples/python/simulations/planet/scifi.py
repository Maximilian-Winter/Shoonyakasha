"""
The walker's full map's interface style (explored_map.py): thin cyan
lines on dark blue glass, panels with cut corners, corner brackets, faint
scan lines, amber for warnings and money. Its Layer, fonts and `measure`
also serve the Field Slate style (fieldslate.py, hud.py, slate.py).

Fonts: FONTS names the three faces, Rajdhani SemiBold for titles,
Rajdhani Medium for text and Share Tech Mono for numbers, loaded once a
ui (`font`); `add_faces` registers more. `measure.width(text, size, face)` gives a text's width in
canvas units from the same files with Pillow, or an estimate without it;
`wrap(text, size, width, face)` breaks a text between words into lines
no wider than `width`.

`Layer(ui, parent)` is a full-size element of a canvas that is drawn
afresh each frame: `begin()`, then shapes into `layer.mesh`
(shoonyakasha.toolkit.drawing.Mesh) and texts with `text(...)`, then
`end()`, which hands the shapes to the canvas and hides the texts not
used this frame. Texts draw over the layer's shapes; a layer made later
draws over one made before. Each text element is reused from frame to
frame and only what changed is handed to the canvas.

Shapes, in canvas units with y down: `cut_rect` (the points of a
rectangle with some corners cut at 45 degrees), `panel` (one filled, with
an edge and scan lines), `outline`, `line` (with a faint wide glow under
it), `brackets` (an L at each corner), `scanlines`, `segments` (a bar of
lit and unlit segments), `arc` (angles clockwise from up), `diamond` and
`chevron`. `caps(layer, hints, x, y)` draws key hints, each (keys,
label): every key in an outlined box, a gamepad's ("pad:A") filled, then
the label; `hints_width` measures a row of them.
"""

import math

from shoonyakasha.toolkit.drawing import Mesh

CYAN = (0.38, 0.88, 1.0, 1.0)
CYAN_SOFT = (0.38, 0.88, 1.0, 0.5)
CYAN_FAINT = (0.38, 0.88, 1.0, 0.14)
GLASS = (0.01, 0.04, 0.065, 0.94)
GLASS_DEEP = (0.005, 0.022, 0.038, 0.97)
WHITE = (0.88, 0.97, 1.0, 1.0)
MUTED = (0.6, 0.78, 0.86, 1.0)
AMBER = (1.0, 0.72, 0.28, 1.0)
RED = (1.0, 0.36, 0.32, 1.0)
GREEN = (0.45, 1.0, 0.72, 1.0)
MAGENTA = (1.0, 0.45, 0.85, 1.0)
SHADOW = (0.0, 0.0, 0.0, 0.55)
SCAN = (0.38, 0.88, 1.0, 0.035)
BACKDROP = (0.0, 0.012, 0.025, 0.6)
CUT = 10.0                       # canvas units cut off a panel's corners
SCAN_GAP = 3.0                   # canvas units between scan lines
GLOW = 0.16                      # a line's glow: this share of its alpha
GLOW_WIDTH = 4.0                 # canvas units the glow is wider than the line

FONTS = {"title": "fonts/Rajdhani-SemiBold.ttf", "body": "fonts/Rajdhani-Medium.ttf",
         "mono": "fonts/ShareTechMono-Regular.ttf"}
ESTIMATE = {"title": 0.5, "body": 0.47, "mono": 0.55}   # widths per size without Pillow
MEASURE_SIZE = 100               # the size Pillow measures each face at, scaled from there

CAP_SIZE = 13                    # the keys' text
CAP_LABEL_SIZE = 16
CAP_HEIGHT = 20
CAP_PAD = 6                      # left and right of a key's text
CAP_GAP = 4                      # between the keys of one hint
CAP_LABEL_GAP = 7                # between the keys and the label
HINT_GAP = 22                    # between hints
PAD_FILL = (0.38, 0.88, 1.0, 0.3)


def with_alpha(colour, alpha):
    """`colour` with its alpha times `alpha`."""
    return colour[:3] + (colour[3] * alpha,)


class Measure:
    """Widths of text in canvas units at a size, from FONTS."""

    def __init__(self):
        self.fonts = {}
        try:
            from PIL import ImageFont
            from shoonyakasha import assets
            self.locate = assets.locate
            self.image_font = ImageFont
        except Exception:
            self.image_font = None

    def width(self, text, size, face="body"):
        """The width of `text` drawn `size` high: the ui scales a face so
        that its ascent plus descent is the size."""
        if self.image_font is None:
            return ESTIMATE[face] * size * len(text)
        font = self.fonts.get(face)
        if font is None:
            try:
                font = self.image_font.truetype(str(self.locate(FONTS[face])), MEASURE_SIZE)
            except OSError:
                return ESTIMATE[face] * size * len(text)
            ascent, descent = font.getmetrics()
            font = self.fonts[face] = (font, ascent + descent)
        font, height = font
        return font.getlength(text) * size / height


measure = Measure()
_loaded = {}


def add_faces(faces, estimates):
    """More faces for `font`, `measure` and Layer.text: `faces` maps a face's name to its file (under the
    asset root), `estimates` to its width per size without Pillow."""
    FONTS.update(faces)
    ESTIMATE.update(estimates)


def font(ui, face):
    """The font id of `face` (a key of FONTS) for `ui`, loaded on first use; 0 (the default font) if it cannot be."""
    key = (id(ui), face)
    if key not in _loaded:
        _loaded[key] = ui.load_font(FONTS[face])
    return _loaded[key]


def wrap(text, size, width, face="body"):
    """`text` broken between words into lines no wider than `width` (a word wider stands alone)."""
    lines, current = [], ""
    for word in text.split():
        candidate = word if not current else current + " " + word
        if current and measure.width(candidate, size, face) > width:
            lines.append(current)
            current = word
        else:
            current = candidate
    if current:
        lines.append(current)
    return lines


ALIGN = {"left": 0.0, "center": 0.5, "right": 1.0}
HIDDEN = (None,) * 7             # a pooled text's state while hidden


class Layer:
    def __init__(self, ui, parent):
        import shoonyakasha as sk
        self.sk = sk
        self.ui = ui
        self.root = ui.create_element(parent)
        self.shapes = ui.create_element(self.root)
        for element in (self.root, self.shapes):
            ui.set_rect(element, anchor_min=(0, 0), anchor_max=(1, 1), pivot=(0, 0), position=(0, 0), size=(0, 0))
            ui.set_raycast_target(element, False)
        self.mesh = Mesh()
        self.pool = []            # [element, its state last handed to the canvas]
        self.used = 0

    def show(self, on):
        self.ui.set_visible(self.root, on)

    def begin(self):
        self.mesh = Mesh()
        self.used = 0

    def text(self, text, at, size, colour, face="body", align="left", shadow=True):
        """`text` at `at` (its left, middle or right by `align`, its middle
        across), `size` canvas units high, in `face`; with a dark copy a
        unit down and right behind it with `shadow`. Returns its width."""
        if not text:
            return 0.0
        width = measure.width(text, size, face)
        if shadow:
            self._put(text, (at[0] + 1.0, at[1] + 1.2), size, with_alpha(SHADOW, colour[3]), face, align, width)
        self._put(text, at, size, colour, face, align, width)
        return width

    def _put(self, text, at, size, colour, face, align, width):
        ui, sk = self.ui, self.sk
        if self.used == len(self.pool):
            element = ui.create_text(self.root, "", size=size, color=colour, font=font(ui, face))
            ui.set_raycast_target(element, False)
            self.pool.append([element, None])
        entry = self.pool[self.used]
        self.used += 1
        element, old = entry
        box = (round(width + 6.0), round(1.6 * size))
        new = (text, face, size, tuple(round(c, 3) for c in colour), align,
               (round(at[0], 1), round(at[1], 1)), box)
        if new == old:
            return
        if old is HIDDEN:
            ui.set_visible(element, True)
            old = None
        if old is None or old[0] != text:
            ui.set_text(element, text)
        if old is None or old[1] != face:
            ui.set_font(element, font(ui, face))
        if old is None or old[2] != size:
            ui.set_font_size(element, size)
        if old is None or old[3] != new[3]:
            ui.set_color(element, colour)
        if old is None or old[4] != align:
            ui.set_text_align(element, {"left": sk.TEXT_ALIGN_LEFT, "center": sk.TEXT_ALIGN_CENTER,
                                        "right": sk.TEXT_ALIGN_RIGHT}[align], sk.TEXT_ALIGN_MIDDLE)
        if old is None or old[5:] != new[5:]:
            ui.set_rect(element, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(ALIGN[align], 0.5),
                        position=(at[0], at[1]), size=box)
        entry[1] = new

    def end(self):
        for entry in self.pool[self.used:]:
            if entry[1] is None or entry[1][0] is not None:
                self.ui.set_visible(entry[0], False)
                entry[1] = HIDDEN
        mesh = self.mesh
        self.ui.set_geometry(self.shapes, mesh.positions, mesh.indices, mesh.colors)


# ── Shapes ─────────────────────────────────────────────────────────────────

def cut_rect(left, top, width, height, cut=CUT, corners=("tl", "br")):
    """The points, clockwise on screen from the top left, of a rectangle
    with `corners` (of "tl", "tr", "br", "bl") cut by `cut`."""
    right, bottom = left + width, top + height
    cut = min(cut, 0.5 * width, 0.5 * height)
    points = []
    for name, (x, y), (ax, ay), (bx, by) in (("tl", (left, top), (0, cut), (cut, 0)),
                                              ("tr", (right, top), (-cut, 0), (0, cut)),
                                              ("br", (right, bottom), (0, -cut), (-cut, 0)),
                                              ("bl", (left, bottom), (cut, 0), (0, -cut))):
        if name in corners and cut > 0.0:
            points += [(x + ax, y + ay), (x + bx, y + by)]
        else:
            points.append((x, y))
    return points


def outline(mesh, points, colour, width=1.0, closed=True):
    pairs = list(zip(points, points[1:] + (points[:1] if closed else [])))
    for a, b in pairs:
        mesh.segment(a, b, colour, width)


def line(mesh, a, b, colour, width=1.2):
    """A line with a faint glow, GLOW_WIDTH wider, under it."""
    mesh.segment(a, b, with_alpha(colour, GLOW), width + GLOW_WIDTH)
    mesh.segment(a, b, colour, width)


def scanlines(mesh, left, top, width, height, colour=SCAN, gap=SCAN_GAP):
    y = top + 1.0
    while y < top + height - 1.0:
        mesh.face([(left, y), (left + width, y), (left + width, y + 1.0), (left, y + 1.0)], [colour] * 4)
        y += gap


def panel(mesh, left, top, width, height, cut=CUT, fill=GLASS, edge=CYAN_SOFT, corners=("tl", "br"),
          scan=True, edge_width=1.0):
    """A filled panel with cut corners, scan lines across it and its edge drawn."""
    points = cut_rect(left, top, width, height, cut, corners)
    mesh.polygon(points, fill)
    if scan:
        inset = max(2.0, min(cut, 0.5 * height))
        scanlines(mesh, left + 2.0, top + inset, width - 4.0, height - 2.0 * inset)
    if edge is not None:
        outline(mesh, points, edge, edge_width)
    return points


def brackets(mesh, left, top, width, height, arm=12.0, colour=CYAN, width_=1.6):
    """An L at each corner of the rectangle, its arms `arm` long."""
    right, bottom = left + width, top + height
    for x, y, sx, sy in ((left, top, 1, 1), (right, top, -1, 1), (right, bottom, -1, -1), (left, bottom, 1, -1)):
        line(mesh, (x, y), (x + sx * arm, y), colour, width_)
        line(mesh, (x, y), (x, y + sy * arm), colour, width_)


def segments(mesh, left, top, width, height, count, value, on, off, gap=2.0):
    """A bar of `count` segments, the first `value` (0 to 1) of them lit."""
    step = (width + gap) / count
    lit = int(round(max(0.0, min(1.0, value)) * count))
    for k in range(count):
        x = left + k * step
        c = on if k < lit else off
        mesh.face([(x, top), (x + step - gap, top), (x + step - gap, top + height), (x, top + height)], [c] * 4)
    return lit


def arc(mesh, cx, cy, radius, colour, width=1.2, start=0.0, end=2.0 * math.pi, steps=64):
    """An arc from `start` to `end` radians clockwise from up."""
    n = max(2, int(math.ceil(steps * abs(end - start) / (2.0 * math.pi))))
    points = [(cx + radius * math.sin(start + (end - start) * k / n), cy - radius * math.cos(start + (end - start) * k / n))
              for k in range(n + 1)]
    for a, b in zip(points, points[1:]):
        mesh.segment(a, b, colour, width)


def diamond(mesh, x, y, size, colour, edge=SHADOW):
    if edge is not None:
        r = size + 1.5
        mesh.face([(x - r, y), (x, y - r), (x + r, y), (x, y + r)], [edge] * 4)
    mesh.face([(x - size, y), (x, y - size), (x + size, y), (x, y + size)], [colour] * 4)


def chevron(mesh, x, y, size, colour, down=True, width=1.6):
    """A V pointing down (up when not `down`), its tip at (x, y)."""
    s = -1.0 if down else 1.0
    line(mesh, (x - size, y + s * size), (x, y), colour, width)
    line(mesh, (x + size, y + s * size), (x, y), colour, width)


# ── Key hints ──────────────────────────────────────────────────────────────

def cap_text(name):
    """(the text a key shows, whether it is a gamepad's)."""
    pad = name.startswith("pad:")
    return (name[4:] if pad else name).upper(), pad


def cap_width(name):
    return measure.width(cap_text(name)[0], CAP_SIZE, "mono") + 2 * CAP_PAD


def hint_width(hint):
    keys, label = hint
    width = sum(cap_width(k) for k in keys) + CAP_GAP * (len(keys) - 1)
    return width + (CAP_LABEL_GAP + measure.width(label, CAP_LABEL_SIZE) if label else 0.0)


def hints_width(hints):
    return sum(hint_width(h) for h in hints) + HINT_GAP * max(0, len(hints) - 1)


def caps(layer, hints, x, y, label_colour=WHITE, alpha=1.0):
    """The hints in a row from (x, the row's middle y); returns its width."""
    mesh, start = layer.mesh, x
    for n, (keys, label) in enumerate(hints):
        if n:
            x += HINT_GAP
        for k, name in enumerate(keys):
            if k:
                x += CAP_GAP
            text, pad = cap_text(name)
            w = cap_width(name)
            points = cut_rect(x, y - 0.5 * CAP_HEIGHT, w, CAP_HEIGHT, 4.0, ("tl", "br"))
            mesh.polygon(points, with_alpha(PAD_FILL if pad else GLASS_DEEP, alpha))
            outline(mesh, points, with_alpha(CYAN, alpha), 1.0)
            layer.text(text, (x + 0.5 * w, y), CAP_SIZE, with_alpha(CYAN if not pad else WHITE, alpha), "mono",
                       "center", shadow=False)
            x += w
        if label:
            x += CAP_LABEL_GAP
            x += layer.text(label, (x, y), CAP_LABEL_SIZE, with_alpha(label_colour, alpha))
    return x - start

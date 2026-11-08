"""
The walker's screens in the Field Slate style (fieldslate.py): the
Slate, a tablet projected from the wrist with the game's apps, which
leaves the world running behind it; and the System screen, the only
pause, which also serves as the start's menu. Each draws on its own
screen canvas, laid out for REFERENCE_SIZE divided by the HUD's scale
(`set_scale`), afresh each frame by `update(input, dt)` while shown.

`Surface` is what both share: a canvas, a FadeLayer, and the pointer.
While drawing, a part that can be clicked adds a region (`region`, or
`button`, which draws one); after drawing, a press of the left button
does the topmost region under the pointer, and a slider's track follows
the pointer while the button stays down. `over(rect)` says whether the
pointer is over a rectangle now.

`FadeLayer` is a scifi.Layer whose shapes and texts can be faded
(`alpha`) and scaled about a point (`zoom`), for the Slate's opening and
closing.

Items (`draw_items`) are dicts: "heading", "slider"
(with "min", "max", "get", "set", "show" or "format", "step"),
"toggle", "choice" (with "options"), "button" (with "do", and "keys"
shown at its right), "note" (a function giving a line, or "") and
"stat" (a label and "get"), any but a heading, a note or a stat with a
"hint"; a list of them, or ("columns", [items, ...]). `draw_controls`
draws a sheet of the controls, columns of sections (heading, rows), each
row (keys, text).

The System screen (`SystemScreen`) lists its entries at the left, each
(name, keys shown at its right, what it does): a function done when it
is chosen, or a section drawn in the pane at the right while it is the
one chosen, a function (surface, left, top, width, height); `idle`, a
section, stands in the pane while the chosen entry is not one. Up and
Down move the choice, Enter does it; the pointer chooses too. A
"divider" entry draws a rule. With `card`, a function drawing a card
above the entries (the start's Continue), the entries start under it.

The Slate (`Slate`) is a rounded pane with a rail of its apps' icons at
the left (System at its foot), the open app's label, title and corner,
its content, and a footer of key hints and its note; a beam of light
falls from it towards the wrist. It opens over SLATE_IN growing from
SLATE_IN_SCALE of its size with one sweep of light and a flicker, and
closes over SLATE_OUT. Q and E move to the app before or after; a click
on an icon opens its app. An app is an object with `name`, `icon` (a key
of ICONS), `label()`, `title()`, `corner(surface, right, top)` (what
stands at the header's right, or nothing), `draw(surface, left, top,
width, height)`, `hints()` (its key hints, before the Slate's own) and
`note()` (the footer's line at the right); and when it has them,
`key(key)` (the keys the Slate does not take; True when taken),
`scroll(steps)` (the mouse wheel) and `hide()` (called each frame it is
not the open app).
"""

import math

import shoonyakasha as sk

import fieldslate as fs
import scifi
from fieldslate import AMBER, HOLO, ICE, INK, MIST, SLATE, with_alpha

REFERENCE_SIZE = (1600, 900)
ROW = {"heading": 34.0, "slider": 44.0, "toggle": 44.0, "button": 50.0, "choice": 44.0, "note": 28.0, "stat": 32.0}
CONTROL = 300.0                  # canvas units across a row's control, at its right, at most
CONTROL_SHARE = 0.55             # of the row's width, at most
COLUMN_GAP = 40.0
BUTTON_WIDTH = 340.0
SWITCH = (44.0, 24.0)            # a toggle's switch
CONTROLS_HEADING, CONTROLS_ROW, CONTROLS_LINE, CONTROLS_GAP = 34.0, 28.0, 18.0, 10.0
PANEL = (182.0, 71.0, 1236.0, 724.0)   # the Slate's left, top, width and height in the reference layout
RAIL = 90.0                      # its rail's width
RAIL_STEP = 67.0                 # between the rail's icons
CONTENT = (123.0, 101.0, 32.0)   # the content's left inset, the header's height, the right inset
FOOTER = 62.0                    # from the pane's bottom to the footer's middle... less its height
SWEEP_SHARE = 0.6                # of SLATE_IN the opening sweep takes
APP_KEYS = ([("Q", "E"), "Switch app"], [("Tab",), "Close"], [("Esc",), "System"])
ENTRY = (300.0, 48.0, 52.0)      # the System screen's entries: width, height, step


# ── Drawing with a fade ────────────────────────────────────────────────────

class FadeLayer(scifi.Layer):
    """A scifi.Layer whose shapes and texts are drawn at `alpha` and
    scaled by `zoom[2]` about (`zoom[0]`, `zoom[1]`)."""

    def __init__(self, ui, parent):
        super().__init__(ui, parent)
        self.alpha = 1.0
        self.zoom = None

    def _at(self, at):
        if self.zoom is None:
            return at
        cx, cy, s = self.zoom
        return (cx + s * (at[0] - cx), cy + s * (at[1] - cy))

    def text(self, text, at, size, colour, face="body", align="left", shadow=True):
        width = scifi.measure.width(text, size, face) if text else 0.0
        s = self.zoom[2] if self.zoom is not None else 1.0
        super().text(text, self._at(at), size * s, with_alpha(colour, self.alpha), face, align, shadow)
        return width

    def end(self):
        mesh = self.mesh
        if self.alpha < 1.0:
            mesh.colors = [c[:3] + (c[3] * self.alpha,) for c in mesh.colors]
        if self.zoom is not None:
            mesh.positions = [self._at(p) for p in mesh.positions]
        super().end()


class Surface:
    def __init__(self, engine, sort_order):
        ui = engine.ui
        self.engine, self.ui = engine, ui
        self.canvas = ui.create_canvas(reference_size=REFERENCE_SIZE, sort_order=sort_order)
        self.layer = FadeLayer(ui, self.canvas)
        self.regions = []             # (left, top, width, height, what it does) drawn this frame
        self.point = None             # the pointer in canvas units, this frame
        self.drag = None              # (slider item, track left, track width) the pointer holds
        self.was_down = False
        self.hint = ""                # the hovered item's hint
        self.visible = False
        self.ui.set_visible(self.canvas, False)

    @property
    def mesh(self):
        return self.layer.mesh

    def set_scale(self, s):
        self.ui.set_canvas_scaling(self.canvas, sk.CANVAS_SCALE_WITH_SCREEN,
                                   reference_size=(REFERENCE_SIZE[0] / s, REFERENCE_SIZE[1] / s))

    def size(self):
        return self.ui.get_canvas_size(self.canvas)

    def pointer(self, input):
        """The pointer in canvas units, or None."""
        fb = self.engine.framebuffer_size
        width, height = self.size()
        if fb[0] <= 0 or fb[1] <= 0:
            return None
        x, y = input.get_mouse_position()
        return x * width / fb[0], y * height / fb[1]

    def over(self, left, top, width, height):
        p = self.point
        return p is not None and left <= p[0] <= left + width and top <= p[1] <= top + height

    def region(self, left, top, width, height, do):
        self.regions.append((left, top, width, height, do))

    def begin(self, input):
        self.point = self.pointer(input) if input is not None else None
        self.regions = []
        self.hint = ""
        self.layer.begin()

    def finish(self, input):
        """The layer handed to the canvas, then the pointer's press done."""
        self.layer.end()
        down = input is not None and input.is_mouse_button_down(sk.keys.MOUSE_LEFT)
        pressed = down and not self.was_down
        self.was_down = down
        if self.drag is not None:
            if not down:
                self.drag = None
            elif self.point is not None:
                item, left, w = self.drag
                item["set"](slider_value(item, (self.point[0] - left) / w))
            return
        if not pressed or self.point is None:
            return
        for left, top, w, h, do in reversed(self.regions):
            if left <= self.point[0] <= left + w and top <= self.point[1] <= top + h:
                do()
                return

    def button(self, left, top, width, text, do, value=None, kind="secondary", enabled=True,
               height=fs.BUTTON_HEIGHT, size="body"):
        """A button (fieldslate.button) that does `do` when clicked while `enabled`. Returns its width."""
        hover = enabled and self.over(left, top, width, height)
        fs.button(self.layer, left, top, width, text, value, kind, hover, enabled, height, size)
        if enabled and do is not None:
            self.region(left, top, width, height, do)
        return width


def slider_value(item, fraction):
    """The value of `item` (a slider) at `fraction` of its track, snapped to its step."""
    lo, hi = item["min"], item["max"]
    value = lo + max(0.0, min(1.0, fraction)) * (hi - lo)
    step = item.get("step")
    if step:
        value = lo + round((value - lo) / step) * step
    return max(lo, min(hi, value))


def next_option(options, value, direction):
    """The option's value `direction` (1 or -1) along from `value` in `options` (value, text), wrapping."""
    values = [v for v, _ in options]
    k = values.index(value) if value in values else -1
    return values[(k + direction) % len(values)]


def value_text(item, value):
    if "show" in item:
        return item["show"](value)
    return item.get("format", "%.2f") % value


# ── Items ──────────────────────────────────────────────────────────────────

def draw_items(surface, content, left, top, width):
    """A list of items, or ("columns", [items, ...]) side by side, from (left, top). Returns the bottom."""
    if isinstance(content, tuple) and content[0] == "controls":
        return draw_controls(surface, content[1], left, top, width)
    if isinstance(content, tuple) and content[0] == "columns":
        columns = content[1]
        col_w = (width - COLUMN_GAP * (len(columns) - 1)) / len(columns)
        bottom = top
        for c, items in enumerate(columns):
            y = top
            for item in items:
                y += draw_item(surface, item, left + c * (col_w + COLUMN_GAP), y, col_w)
            bottom = max(bottom, y)
        return bottom
    y = top
    for item in content:
        y += draw_item(surface, item, left, y, width)
    return y


def draw_item(surface, item, x, y, w):
    """One item from (x, y) across `w`; returns its height."""
    layer, mesh = surface.layer, surface.mesh
    h = ROW[next(k for k in ROW if k in item)]
    mid = y + 0.5 * h
    control = min(CONTROL, CONTROL_SHARE * w)
    cx = x + w - control
    line = fs.tint(ICE, 0.1, fs.ON_SLATE)
    if "heading" in item:
        fs.label(layer, item["heading"], (x, mid + 4.0))
        return h
    if "stat" in item:
        fs.write(layer, item["stat"], (x, mid), "body", MIST)
        fs.write(layer, item["get"](), (x + w, mid), "body", ICE, "num", "right")
        mesh.face([(x, y + h - 0.5), (x + w, y + h - 0.5), (x + w, y + h + 0.5), (x, y + h + 0.5)], [line] * 4)
        return h
    if "note" in item:
        text = item["note"]()
        if text:
            fs.write(layer, fit_text(text, w, "small"), (x, mid), "small", MIST)
        return h
    if surface.over(x - 8.0, y, w + 16.0, h) and item.get("hint"):
        surface.hint = item["hint"]
    if "button" in item:
        bw = min(BUTTON_WIDTH, w)
        surface.button(x, y + 6.0, bw, item["button"], item["do"], height=h - 12.0)
        if item.get("keys"):
            fs.write(layer, item["keys"], (x + bw - 14.0, mid), "small", MIST, "num", "right")
        return h
    name = item.get("slider") or item.get("toggle") or item.get("choice")
    hot = surface.over(x - 8.0, y, w + 16.0, h) or (surface.drag is not None and surface.drag[0] is item)
    if hot:
        fs.fill(mesh, fs.rounded_rect(x - 10.0, y + 2.0, w + 20.0, h - 4.0, fs.RADIUS_SMALL),
                fs.tint(ICE, 0.05, fs.ON_SLATE))
    fs.write(layer, name, (x, mid), "body", ICE)
    if "slider" in item:
        value = item["get"]()
        track_w = control - 90.0
        span = item["max"] - item["min"]
        fraction = max(0.0, min(1.0, (value - item["min"]) / span if span > 0 else 0.0))
        fs.bar(mesh, cx, mid - 1.5, track_w, fraction, HOLO, 3.0, fs.TRACK)
        fs.dot(mesh, cx + track_w * fraction, mid, 7.0, ICE)
        fs.write(layer, value_text(item, value), (x + w, mid), "body", HOLO, "num", "right")

        def grab(item=item, left=cx, width=track_w):
            surface.drag = (item, left, width)
            if surface.point is not None:
                item["set"](slider_value(item, (surface.point[0] - left) / width))
        surface.region(cx - 10.0, y, track_w + 20.0, h, grab)
    elif "toggle" in item:
        on = bool(item["get"]())
        sw, sh = SWITCH
        left = x + w - sw
        fs.fill(mesh, fs.rounded_rect(left, mid - 0.5 * sh, sw, sh, 0.5 * sh),
                with_alpha(HOLO, 0.9) if on else fs.tint(ICE, 0.14, fs.ON_SLATE))
        fs.dot(mesh, left + (sw - 0.5 * sh if on else 0.5 * sh), mid, 0.5 * sh - 4.0, INK if on else ICE)
        surface.region(x, y, w, h, lambda item=item: item["set"](not item["get"]()))
    else:
        value = item["get"]()
        text = next((t for v, t in item["options"] if v == value), str(value))
        aw, right = 28.0, x + w
        back_x = right - control + 90.0
        for ax, glyph, step in ((back_x, "‹", -1), (right - aw, "›", 1)):
            hot_arrow = surface.over(ax, mid - 14.0, aw, 28.0)
            fs.fill(mesh, fs.rounded_rect(ax, mid - 14.0, aw, 28.0, 7.0),
                    fs.tint(HOLO if hot_arrow else ICE, 0.16 if hot_arrow else 0.07, fs.ON_SLATE))
            fs.write(layer, glyph, (ax + 0.5 * aw, mid - 1.0), "lead", ICE, "text_medium", "center")
            surface.region(ax, mid - 14.0, aw, 28.0,
                           lambda item=item, step=step: item["set"](next_option(item["options"], item["get"](), step)))
        fs.write(layer, text, (0.5 * (back_x + aw + right - aw), mid), "body", ICE, "num", "center")
    return h


def draw_controls(surface, columns, left, top, width):
    """Each column's sections: a heading, then each row's keys with its
    text beside them, past the column's widest keys and wrapped to it."""
    layer = surface.layer
    col_w = width / len(columns)
    bottom = top
    for c, sections in enumerate(columns):
        cx, cy = left + c * col_w, top
        keys_w = max(fs.hints_width([(keys, None)]) for _, rows in sections for keys, _ in rows)
        text_x = cx + keys_w + 12.0
        for heading, rows in sections:
            fs.label(layer, heading, (cx, cy + 10.0), HOLO)
            cy += CONTROLS_HEADING
            for keys, text in rows:
                fs.hints(layer, [(keys, None)], cx, cy)
                lines = wrap_text(text, cx + col_w - 24.0 - text_x, "small")
                for k, line in enumerate(lines):
                    fs.write(layer, line, (text_x, cy + CONTROLS_LINE * k), "small", ICE)
                cy += CONTROLS_ROW + CONTROLS_LINE * max(0, len(lines) - 1)
            cy += CONTROLS_GAP
        bottom = max(bottom, cy)
    return bottom


def wrap_text(text, width, size="body", face="text"):
    """`text` broken between words into lines no wider than `width`."""
    lines, current = [], ""
    for word in text.split():
        candidate = word if not current else current + " " + word
        if current and fs.text_width(candidate, size, face) > width:
            lines.append(current)
            current = word
        else:
            current = candidate
    if current:
        lines.append(current)
    return lines


def fit_text(text, width, size="body", face="text"):
    """`text` cut with an ellipsis to fit `width`."""
    if fs.text_width(text, size, face) <= width:
        return text
    while text and fs.text_width(text + "…", size, face) > width:
        text = text[:-1]
    return text.rstrip() + "…"


# ── Icons ──────────────────────────────────────────────────────────────────

def _lines(mesh, colour, points, closed=False, width=1.5):
    pairs = list(zip(points, points[1:] + (points[:1] if closed else [])))
    for a, b in pairs:
        mesh.segment(a, b, colour, width)


def icon(mesh, name, x, y, colour):
    """The rail's icon `name` (a key of ICONS), about 18 units across, centred at (x, y)."""
    ICONS[name](mesh, x, y, colour)


def _overview(mesh, x, y, c):
    for dx, dy in ((-5, -5), (5, -5), (-5, 5), (5, 5)):
        points = fs.rounded_rect(x + dx - 4.0, y + dy - 4.0, 8.0, 8.0, 2.0)
        fs.stroke(mesh, points, c, 1.4)


def _market(mesh, x, y, c):
    _lines(mesh, c, [(x - 9.0, y + 8.0), (x + 9.0, y + 8.0)])
    for dx, h in ((-5.0, 8.0), (0.0, 13.0), (5.0, 6.0)):
        mesh.segment((x + dx, y + 8.0), (x + dx, y + 8.0 - h), c, 1.5)


def _map(mesh, x, y, c):
    points = [(x - 9.0, y - 6.0), (x - 3.0, y - 9.0), (x + 3.0, y - 6.0), (x + 9.0, y - 9.0),
              (x + 9.0, y + 6.0), (x + 3.0, y + 9.0), (x - 3.0, y + 6.0), (x - 9.0, y + 9.0)]
    _lines(mesh, c, points, closed=True)
    mesh.segment((x - 3.0, y - 9.0), (x - 3.0, y + 6.0), c, 1.2)
    mesh.segment((x + 3.0, y - 6.0), (x + 3.0, y + 9.0), c, 1.2)


def _document(mesh, x, y, c, rows=3):
    fs.stroke(mesh, fs.rounded_rect(x - 7.0, y - 9.0, 14.0, 18.0, 2.5), c, 1.4)
    for k in range(rows):
        mesh.segment((x - 3.5, y - 4.0 + 4.0 * k), (x + 3.5, y - 4.0 + 4.0 * k), c, 1.2)


def _contracts(mesh, x, y, c):
    _document(mesh, x, y, c, 3)


def _log(mesh, x, y, c):
    _document(mesh, x, y, c, 2)


def _workshop(mesh, x, y, c):
    mesh.segment((x - 7.0, y + 7.0), (x + 3.0, y - 3.0), c, 2.0)
    fs.dot(mesh, x + 5.0, y - 5.0, 4.0, c, hollow=True, width=1.5)


def _vehicle(mesh, x, y, c):
    _lines(mesh, c, [(x - 9.0, y + 3.0), (x - 6.0, y - 4.0), (x + 6.0, y - 4.0), (x + 9.0, y + 3.0),
                     (x + 9.0, y + 6.0), (x - 9.0, y + 6.0)], closed=True)
    mesh.segment((x - 9.0, y + 1.0), (x + 9.0, y + 1.0), c, 1.2)


def _system(mesh, x, y, c):
    fs.dot(mesh, x, y, 4.0, c, hollow=True, width=1.4)
    for k in range(8):
        a = math.pi * k / 4.0
        mesh.segment((x + 6.5 * math.cos(a), y + 6.5 * math.sin(a)), (x + 9.0 * math.cos(a), y + 9.0 * math.sin(a)),
                     c, 1.4)


def _debug(mesh, x, y, c):
    fs.dot(mesh, x, y + 1.0, 5.0, c, hollow=True, width=1.4)
    for side in (-1.0, 1.0):
        for k, dy in enumerate((-3.0, 1.0, 5.0)):
            mesh.segment((x + side * 4.5, y + dy), (x + side * 9.0, y + dy - 2.0 + 2.0 * k), c, 1.2)
    mesh.segment((x - 2.0, y - 4.0), (x - 4.0, y - 8.0), c, 1.2)
    mesh.segment((x + 2.0, y - 4.0), (x + 4.0, y - 8.0), c, 1.2)


ICONS = {"overview": _overview, "market": _market, "map": _map, "contracts": _contracts, "workshop": _workshop,
         "vehicle": _vehicle, "log": _log, "system": _system, "debug": _debug}


# ── The System screen ──────────────────────────────────────────────────────

class SystemScreen(Surface):
    def __init__(self, engine, label, title, entries, note=None, card=None, idle=None, sort_order=3):
        super().__init__(engine, sort_order)
        self.label, self.title = label, title
        self.entries = entries        # [(name, keys or None, a function or a section)] or "divider"
        self.note = note              # a function giving the line at the bottom left, or None
        self.card = card              # a function (surface, left, top, width) drawing a card; returns its height
        self.idle = idle              # the section shown while the chosen entry is not one, or None
        self.choice = self._first()

    def _first(self):
        return next(k for k, e in enumerate(self.entries) if e != "divider")

    def show(self, on, section=None):
        """Shown, chosen on the entry named `section` when given, or hidden."""
        self.visible = on
        if section is not None:
            self.choose(section)
        self.drag = None
        self.ui.set_visible(self.canvas, on)

    def choose(self, name):
        for k, e in enumerate(self.entries):
            if e != "divider" and e[0] == name:
                self.choice = k

    def chosen(self):
        return self.entries[self.choice][0]

    def key(self, key):
        """Up, Down and Enter; True when it took the key."""
        if not self.visible:
            return False
        if key in (sk.keys.UP, sk.keys.DOWN):
            step = -1 if key == sk.keys.UP else 1
            k = self.choice
            for _ in range(len(self.entries)):
                k = (k + step) % len(self.entries)
                if self.entries[k] != "divider":
                    break
            self.choice = k
            return True
        if key in (sk.keys.ENTER, sk.keys.KP_ENTER):
            self.do(self.choice)
            return True
        return False

    def do(self, k):
        self.choice = k
        what = self.entries[k][2]
        if not getattr(what, "section", False):
            what()

    def update(self, input, dt=0.0):
        if not self.visible:
            return
        width, height = self.size()
        if width <= 0 or height <= 0:
            return
        self.begin(input)
        layer, mesh = self.layer, self.mesh
        top_c, bottom_c = with_alpha(SLATE, 0.92), with_alpha(fs.hex_colour("#0B1A12"), 0.82)
        mesh.face([(0, 0), (width, 0), (width, height), (0, height)], [top_c, top_c, bottom_c, bottom_c])
        x = 96.0
        fs.label(layer, self.label, (x, 86.0))
        fs.write(layer, self.title, (x, 128.0), 44, ICE, "display")
        y = 200.0
        if self.card is not None:
            y += self.card(self, x, y, 520.0) + 30.0
        ew, eh, step = ENTRY
        for k, entry in enumerate(self.entries):
            if entry == "divider":
                line = fs.tint(ICE, 0.12, fs.ON_SLATE)
                mesh.face([(x, y + 10.0), (x + ew, y + 10.0), (x + ew, y + 11.0), (x, y + 11.0)], [line] * 4)
                y += 22.0
                continue
            name, keys, _ = entry
            chosen, hot = k == self.choice, self.over(x, y, ew, eh)
            points = fs.rounded_rect(x, y, ew, eh, fs.RADIUS_SMALL)
            if chosen:
                fs.fill(mesh, points, HOLO)
            elif hot:
                fs.fill(mesh, points, fs.tint(ICE, 0.06, fs.ON_SLATE))
            fs.write(layer, name, (x + 20.0, y + 0.5 * eh), "lead", INK if chosen else ICE, "text_medium")
            if keys:
                kw = fs.keycap_width(keys)
                fs.keycap(layer, keys, x + ew - 18.0 - kw, y + 0.5 * eh)
            self.region(x, y, ew, eh, lambda k=k: self.do(k))
            y += step
        if self.note is not None:
            fs.write(layer, self.note(), (x, height - 108.0), "small", MIST, "num")
        fs.hints(layer, [(("Up", "Down"), "Move"), (("Enter",), "Select")] + self.close_hints(), x, height - 55.0)
        what = self.entries[self.choice][2]
        if not getattr(what, "section", False):
            what = self.idle
        if what is not None:
            left, top = 436.0, 200.0
            w, h = min(1068.0, width - left - 96.0), height - top - 100.0
            fs.pane(mesh, left, top, w, h, 24.0, with_alpha(SLATE, 0.6), fs.tint(ICE, 0.14, fs.ON_SLATE))
            what(self, left + 30.0, top + 30.0, w - 60.0, h - 60.0)
            if self.hint:
                fs.write(layer, fit_text(self.hint, w - 60.0, "small"), (left + 30.0, top + h - 26.0), "small",
                         MIST)
        self.finish(input)

    def close_hints(self):
        return [(("Esc",), "Resume")] if self.card is None else []


def section(draw):
    """`draw` (surface, left, top, width, height) marked as a section of the System screen."""
    draw.section = True
    return draw


def items_section(title, content, right_label=None):
    """A section of a title and its items (a function giving them, or them)."""
    @section
    def draw(surface, left, top, width, height):
        fs.write(surface.layer, title, (left, top + 14.0), 24, ICE, "display")
        if right_label:
            fs.label(surface.layer, right_label(), (left + width, top + 20.0), MIST, "right")
        draw_items(surface, content() if callable(content) else content, left, top + 52.0, width)
    return draw


# ── The Slate ──────────────────────────────────────────────────────────────

class Slate(Surface):
    def __init__(self, engine, apps, system=None, sort_order=2):
        super().__init__(engine, sort_order)
        self.apps = apps
        self.app = 0
        self.system = system          # a function opening the System screen, from the rail's foot
        self.opened = False           # open, or opening
        self.clock = 0.0              # s since it began opening or closing
        self.clock_dt = 0.0           # s since the last frame

    def app_named(self, name):
        return next(k for k, a in enumerate(self.apps) if a.name == name)

    def open(self, name=None):
        if name is not None:
            self.app = self.app_named(name)
        if not self.opened:
            self.opened, self.clock = True, 0.0
            self.visible = True
            self.ui.set_visible(self.canvas, True)

    def close(self, at_once=False):
        if self.opened:
            self.opened, self.clock = False, 0.0
        if at_once:
            self.visible = False
            self.ui.set_visible(self.canvas, False)

    def key(self, key):
        """Q and E, else the open app's `key(key)` when it has one; True when it was taken."""
        if not self.opened:
            return False
        if key in (sk.keys.Q, sk.keys.E):
            self.app = (self.app + (-1 if key == sk.keys.Q else 1)) % len(self.apps)
            return True
        app = self.apps[self.app]
        return bool(getattr(app, "key", None) and app.key(key))

    def scroll(self, steps):
        """The mouse wheel, to the open app's `scroll(steps)` when it has one."""
        app = self.apps[self.app]
        if self.opened and getattr(app, "scroll", None):
            app.scroll(steps)

    def update(self, input, dt=0.0):
        if not self.visible:
            return
        self.clock += dt
        self.clock_dt = dt
        if not self.opened and self.clock >= fs.SLATE_OUT:
            self.visible = False
            self.ui.set_visible(self.canvas, False)
            return
        width, height = self.size()
        if width <= 0 or height <= 0:
            return
        self.begin(input if self.opened else None)
        layer = self.layer
        if self.opened:
            t = min(1.0, self.clock / fs.SLATE_IN)
            grow = fs.ease_out(t)
            flicker = 0.75 if 0.45 < t < 0.55 else 1.0
            layer.alpha = min(1.0, 0.2 + 0.8 * grow) * flicker
            s = fs.SLATE_IN_SCALE + (1.0 - fs.SLATE_IN_SCALE) * grow
        else:
            t = min(1.0, self.clock / fs.SLATE_OUT)
            layer.alpha = 1.0 - t
            s = 1.0 - (1.0 - fs.SLATE_IN_SCALE) * t
        left, top, pw, ph = self.panel_rect(width, height)
        layer.zoom = (left + 0.5 * pw, top + 0.5 * ph, s) if s < 0.9999 else None
        self.draw(width, height, left, top, pw, ph, t if self.opened else 1.0)
        self.finish(input if self.opened and self.clock >= fs.SLATE_IN else None)

    def panel_rect(self, width, height):
        left, top, pw, ph = PANEL
        dx, dy = 0.5 * (width - REFERENCE_SIZE[0]), 0.5 * (height - REFERENCE_SIZE[1])
        return left + dx, top + dy, pw, ph

    def draw(self, width, height, left, top, pw, ph, t):
        layer, mesh = self.layer, self.mesh
        dim = with_alpha(SLATE, 0.5)
        mesh.face([(0, 0), (width, 0), (width, height), (0, height)], [dim] * 4)
        # The beam towards the wrist.
        cx, bottom = left + 0.5 * pw, top + ph
        beam, clear = fs.tint(HOLO, 0.16, fs.ON_SLATE), with_alpha(HOLO, 0.0)
        mesh.face([(cx - 250.0, bottom), (cx + 250.0, bottom), (cx + 6.0, bottom + 90.0), (cx - 6.0, bottom + 90.0)],
                  [beam, beam, clear, clear])
        ring = [(cx + 36.0 * math.cos(a), bottom + 90.0 + 5.0 * math.sin(a))
                for a in (2.0 * math.pi * k / 32 for k in range(32))]
        fs.fill(mesh, ring, with_alpha(HOLO, 0.85))
        fs.pane(mesh, left, top, pw, ph, 28.0, with_alpha(fs.hex_colour("#0E1C25"), 0.93),
                fs.tint(ICE, 0.13, fs.ON_SLATE), glow_colour=HOLO, glow_reach=18.0)
        line = fs.tint(ICE, 0.1, fs.ON_SLATE)
        rx = left + RAIL
        mesh.face([(rx, top + 1.0), (rx + 1.0, top + 1.0), (rx + 1.0, bottom - 1.0), (rx, bottom - 1.0)], [line] * 4)
        if t < 1.0:                    # the opening sweep of light
            sweep = min(1.0, t / SWEEP_SHARE)
            y = top + ph * sweep
            glow = fs.tint(HOLO, 0.35 * (1.0 - sweep), fs.ON_SLATE)
            mesh.face([(left + 4.0, y - 18.0), (left + pw - 4.0, y - 18.0), (left + pw - 4.0, y), (left + 4.0, y)],
                      [with_alpha(glow, 0.0), with_alpha(glow, 0.0), glow, glow])
        self.draw_rail(left, top, ph)
        for k, other in enumerate(self.apps):
            if k != self.app and getattr(other, "hide", None):
                other.hide()
        app = self.apps[self.app]
        x = left + CONTENT[0]
        right = left + pw - CONTENT[2]
        fs.label(layer, app.label(), (x, top + 32.0))
        fs.write(layer, app.title(), (x, top + 66.0), 30, ICE, "display")
        app.corner(self, right, top + 32.0)
        body_top = top + CONTENT[1]
        app.draw(self, x, body_top, right - x, ph - CONTENT[1] - FOOTER - 18.0)
        hints = list(app.hints()) + [tuple(h) for h in APP_KEYS[:2]]
        fy = bottom - 31.0
        fs.hints(layer, hints, x, fy)
        note = self.hint or app.note()
        if note:
            room = right - x - fs.hints_width(hints) - 30.0
            fs.write(layer, fit_text(note, room, "small"), (right, fy), "small", MIST, "text", "right")

    def draw_rail(self, left, top, ph):
        layer, mesh = self.layer, self.mesh
        cx = left + 0.5 * RAIL
        y = top + 41.0
        for k, app in enumerate(self.apps):
            active = k == self.app
            box = (left + 10.0, y - 30.0, RAIL - 20.0, 60.0)
            hot = self.over(*box)
            if active or hot:
                fs.pane(mesh, *box, fs.RADIUS_SMALL, fs.tint(HOLO, 0.12 if active else 0.06, fs.ON_SLATE),
                        fs.tint(HOLO, 0.34, fs.ON_SLATE) if active else None)
            colour = HOLO if active else (ICE if hot else MIST)
            icon(mesh, app.icon, cx, y - 9.0, colour)
            fs.write(layer, app.name, (cx, y + 15.0), "small", colour, "text_medium", "center")
            self.region(*box, lambda k=k: setattr(self, "app", k))
            y += RAIL_STEP
        if self.system is not None:
            y = top + ph - 54.0
            box = (left + 10.0, y - 30.0, RAIL - 20.0, 60.0)
            hot = self.over(*box)
            colour = ICE if hot else MIST
            icon(mesh, "system", cx, y - 9.0, colour)
            fs.write(layer, "System", (cx, y + 15.0), "small", colour, "text_medium", "center")
            self.region(*box, self.system)


def credits_corner(surface, right, top, credits, extra=None):
    """CREDITS over the credits in AMBER at the header's right; `extra`
    (label, value text) to its left. Returns the left of what it drew."""
    layer = surface.layer
    fs.label(layer, "Credits", (right, top + 8.0), MIST, "right")
    w = fs.write(layer, fs.credits(credits), (right, top + 38.0), 26, AMBER, "num_medium", "right")
    x = right - w
    if extra is not None:
        name, value = extra
        x -= 36.0
        vw = fs.write(layer, value, (x, top + 40.0), "lead", ICE, "num", "right")
        fs.label(layer, name, (x, top + 17.0), MIST, "right")
        x -= vw
    return x

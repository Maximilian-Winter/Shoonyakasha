"""
The walker's menu (walker.py) in the interface style of scifi.py: a panel
in the middle of the screen over a backdrop that dims everything behind
it, with a title, a line under it (`subtitle`, a function giving the
text), a row of tabs, the open tab's content, and a footer that describes
the item under the pointer at its left and names the keys that close it
at its right. It draws on its own screen canvas, over the HUD and the
map, laid out for its reference size divided by the HUD's scale
(`set_scale`), and is drawn afresh each frame by `update(input)` while
shown.

A tab is (name, content): content is a list of items, ("columns",
[items, ...]) for items in columns side by side, or ("controls",
columns) for a sheet of the controls. Items are dicts, as
shoonyakasha.toolkit.overlay.SettingsMenu takes them, plus choices and
notes:
    {"heading": "Input"}
    {"slider": "Field of view", "min": 55, "max": 100, "get": f, "set": g, "format": "%.0f"}
        (or "show": a function from the value to the text shown, in place of "format";
         "step": the value moves in steps of this)
    {"toggle": "Head bob", "get": f, "set": g}
    {"button": "Resume", "do": f}
    {"choice": "Window", "options": [(value, text), ...], "get": f, "set": g}
    {"note": f}            (a function giving a line of text, or "" for none)
    {"stat": "Credits", "get": f}   (a label, and a function giving its value's text)
and any but a heading, a note or a stat may have "hint", a sentence the footer
shows while the pointer is over it. The columns of a controls sheet are
lists of sections, each (heading, rows), each row (keys, text), drawn
with scifi.caps.

The pointer works it: a click on a button does it; on a toggle flips
it; on a choice's arrows takes the option before or after, elsewhere on
it the one after; on a tab opens it. A slider follows the pointer while
the left button, pressed on it, is held. `update` hands the setters what
the pointer changed and draws every item from its getter, so changes
made elsewhere show.
"""

import shoonyakasha as sk

import scifi
from scifi import measure, with_alpha

REFERENCE_SIZE = (1600, 900)
PANEL = (980.0, 600.0)           # canvas units across and down, at most
PAD = 26.0
HEADER = 64.0                    # the title and its line
TABS = 40.0                      # the row of tabs
FOOTER = 46.0
ROW = {"heading": 36.0, "slider": 42.0, "toggle": 42.0, "button": 48.0, "choice": 42.0, "note": 28.0,
       "stat": 30.0}
CONTROL = 320.0                  # canvas units across a row's control, at its right, at most
CONTROL_SHARE = 0.56             # of the row's width, at most
COLUMN_GAP = 34.0                # between the columns of a tab
BUTTON_WIDTH = 360.0
LABEL_SIZE = 17
HOVER = (0.38, 0.88, 1.0, 0.12)
ACTIVE_TAB = (0.38, 0.88, 1.0, 0.22)


def value_text(item, value):
    if "show" in item:
        return item["show"](value)
    return item.get("format", "%.2f") % value


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


class Terminal:
    def __init__(self, engine, title, tabs, subtitle=None, close_text="ESC  Close", reference_size=REFERENCE_SIZE):
        ui = engine.ui
        self.engine, self.ui = engine, ui
        self.reference_size = reference_size
        self.canvas = ui.create_canvas(reference_size=reference_size, sort_order=2)
        self.layer = scifi.Layer(ui, self.canvas)
        self.title, self.subtitle, self.close_text = title, subtitle, close_text
        self.tabs = tabs
        self.tab = 0
        self.visible = False
        self.regions = []         # (left, top, width, height, item, part) drawn last frame
        self.hover = None         # (item, part) under the pointer
        self.drag = None          # (slider item, track left, track width) the pointer holds
        self.was_down = False
        self.show(False)

    def set_tabs(self, tabs, tab=0):
        self.tabs, self.tab = tabs, tab

    def set_scale(self, s):
        self.ui.set_canvas_scaling(self.canvas, sk.CANVAS_SCALE_WITH_SCREEN,
                                   reference_size=(self.reference_size[0] / s, self.reference_size[1] / s))

    def show(self, on, tab=None):
        self.visible = on
        if tab is not None:
            self.tab = tab
        self.drag = None
        self.ui.set_visible(self.canvas, on)

    def tab_named(self, name):
        return next(k for k, (n, _) in enumerate(self.tabs) if n == name)

    # ── The pointer ──

    def pointer(self, input):
        """The pointer in canvas units, or None."""
        fb = self.engine.framebuffer_size
        width, height = self.ui.get_canvas_size(self.canvas)
        if fb[0] <= 0 or fb[1] <= 0:
            return None
        x, y = input.get_mouse_position()
        return x * width / fb[0], y * height / fb[1]

    def hit(self, point):
        if point is None:
            return None
        for left, top, w, h, item, part in reversed(self.regions):
            if left <= point[0] <= left + w and top <= point[1] <= top + h:
                return item, part, left, w
        return None

    def handle(self, point, down):
        """The pointer's work this frame (see the module's docstring)."""
        pressed = down and not self.was_down
        self.was_down = down
        found = self.hit(point)
        self.hover = found[:2] if found is not None else None
        if self.drag is not None:
            if not down:
                self.drag = None
            elif point is not None:
                item, left, w = self.drag
                item["set"](slider_value(item, (point[0] - left) / w))
            return
        if not pressed or found is None:
            return
        item, part, left, w = found
        if part == "tab":
            self.tab = item
        elif "button" in item:
            item["do"]()
        elif "toggle" in item:
            item["set"](not item["get"]())
        elif "choice" in item:
            item["set"](next_option(item["options"], item["get"](), -1 if part == "back" else 1))
        elif "slider" in item and part == "track":
            self.drag = (item, left, w)
            item["set"](slider_value(item, (point[0] - left) / w))

    # ── Drawing ──

    def _region(self, left, top, w, h, item, part="item"):
        self.regions.append((left, top, w, h, item, part))

    def _hovered(self, item, part=None):
        if self.hover is None or (part is not None and self.hover[1] != part):
            return False
        return self.hover[0] is item or (isinstance(item, int) and self.hover[0] == item)

    def draw_item(self, item, x, y, w):
        layer, mesh = self.layer, self.layer.mesh
        h = ROW[next(k for k in ROW if k in item)]
        mid = y + 0.5 * h
        control = min(CONTROL, CONTROL_SHARE * w)
        cx = x + w - control
        if "heading" in item:
            layer.text(item["heading"].upper(), (x, mid + 4.0), 15, scifi.CYAN, "title")
            mesh.segment((x + measure.width(item["heading"].upper(), 15, "title") + 12.0, mid + 4.0),
                         (x + w, mid + 4.0), scifi.CYAN_FAINT, 1.0)
            return h
        if "stat" in item:
            layer.text(item["stat"].upper(), (x, mid), 14, scifi.MUTED, "title")
            layer.text(item["get"](), (x + w, mid), 15, scifi.WHITE, "mono", "right")
            mesh.segment((x, y + h - 1.0), (x + w, y + h - 1.0), scifi.CYAN_FAINT, 1.0)
            return h
        if "note" in item:
            text = item["note"]()
            if text:
                layer.text(text, (x, mid), 15, scifi.MUTED)
            return h
        if "button" in item:
            bw = min(BUTTON_WIDTH, w)
            hot = self._hovered(item)
            scifi.panel(mesh, x, y + 4.0, bw, h - 8.0, 8.0, HOVER if hot else scifi.GLASS,
                        scifi.CYAN if hot else scifi.CYAN_SOFT, ("tl", "br"), scan=False)
            if hot:
                mesh.face([(x + 6.0, y + 14.0), (x + 9.0, y + 14.0), (x + 9.0, y + h - 14.0), (x + 6.0, y + h - 14.0)],
                          [scifi.CYAN] * 4)
            layer.text(item["button"].upper(), (x + 20.0, mid), 18, scifi.WHITE if hot else scifi.CYAN, "title")
            if item.get("keys"):
                layer.text(item["keys"], (x + bw - 14.0, mid), 13, scifi.MUTED, "mono", "right")
            self._region(x, y + 4.0, bw, h - 8.0, item)
            return h
        name = item.get("slider") or item.get("toggle") or item.get("choice")
        if self._hovered(item) or (self.drag is not None and self.drag[0] is item):
            mesh.face([(x - 8.0, y + 2.0), (x + w + 8.0, y + 2.0), (x + w + 8.0, y + h - 2.0), (x - 8.0, y + h - 2.0)],
                      [HOVER] * 4)
        layer.text(name, (x, mid), LABEL_SIZE, scifi.WHITE)
        if "slider" in item:
            value = item["get"]()
            track_w = control - 90.0
            fraction = (value - item["min"]) / (item["max"] - item["min"]) if item["max"] > item["min"] else 0.0
            fraction = max(0.0, min(1.0, fraction))
            mesh.segment((cx, mid), (cx + track_w, mid), scifi.CYAN_FAINT, 4.0)
            scifi.line(mesh, (cx, mid), (cx + track_w * fraction, mid), scifi.CYAN, 3.0)
            hx = cx + track_w * fraction
            mesh.polygon([(hx, mid - 8.0), (hx + 6.0, mid), (hx, mid + 8.0), (hx - 6.0, mid)], scifi.WHITE)
            layer.text(value_text(item, value), (x + w, mid), 15, scifi.CYAN, "mono", "right")
            self._region(x, y, w, h, item, "label")
            self._region(cx - 8.0, y, track_w + 16.0, h, item, "track")
        elif "toggle" in item:
            on = bool(item["get"]())
            bx, bw = x + w - 110.0, 110.0
            for k, (text, lit) in enumerate((("OFF", not on), ("ON", on))):
                sx = bx + k * 0.5 * bw
                points = scifi.cut_rect(sx + 2.0, mid - 12.0, 0.5 * bw - 4.0, 24.0, 6.0,
                                        ("tl",) if k == 0 else ("br",))
                mesh.polygon(points, ACTIVE_TAB if lit else scifi.GLASS_DEEP)
                scifi.outline(mesh, points, scifi.CYAN if lit else scifi.CYAN_FAINT, 1.0)
                layer.text(text, (sx + 0.25 * bw, mid), 14, scifi.WHITE if lit else scifi.MUTED, "mono", "center",
                           shadow=False)
            self._region(x, y, w, h, item)
        else:
            value = item["get"]()
            text = next((t for v, t in item["options"] if v == value), str(value))
            aw = 26.0
            right = x + w
            for part, ax, glyph in (("back", right - control + 90.0, "<"), ("next", right - aw, ">")):
                hot = self._hovered(item, part)
                points = scifi.cut_rect(ax, mid - 12.0, aw, 24.0, 5.0, ("tl", "br"))
                mesh.polygon(points, ACTIVE_TAB if hot else scifi.GLASS_DEEP)
                scifi.outline(mesh, points, scifi.CYAN_SOFT, 1.0)
                layer.text(glyph, (ax + 0.5 * aw, mid), 15, scifi.CYAN, "mono", "center", shadow=False)
            self._region(x, y, w, h, item)
            self._region(right - control + 90.0, mid - 12.0, aw, 24.0, item, "back")
            self._region(right - aw, mid - 12.0, aw, 24.0, item, "next")
            layer.text(text, (0.5 * (right - control + 90.0 + right), mid), 15, scifi.WHITE, "mono", "center")
        return h

    def draw_controls(self, columns, x, y, w):
        layer = self.layer
        col_w = w / len(columns)
        for c, sections in enumerate(columns):
            cx, cy = x + c * col_w, y
            for heading, rows in sections:
                layer.text(heading.upper(), (cx, cy + 12.0), 15, scifi.CYAN, "title")
                layer.mesh.segment((cx, cy + 26.0), (cx + col_w - 24.0, cy + 26.0), scifi.CYAN_FAINT, 1.0)
                cy += 42.0
                for keys, text in rows:
                    width = scifi.caps(layer, [(keys, None)], cx, cy)
                    lines = scifi.wrap(text, 16, col_w - 24.0 - max(width, 120.0) - 12.0)
                    for k, line in enumerate(lines):
                        layer.text(line, (cx + max(width, 120.0) + 12.0, cy + 18.0 * k), 16, scifi.WHITE)
                    cy += 30.0 + 18.0 * max(0, len(lines) - 1)
                cy += 10.0

    def update(self, input):
        """One frame while shown: the pointer's work, then the menu drawn."""
        if not self.visible:
            return
        width, height = self.ui.get_canvas_size(self.canvas)
        if width <= 0 or height <= 0:
            return
        self.handle(self.pointer(input), input.is_mouse_button_down(sk.keys.MOUSE_LEFT))
        self.regions = []
        layer = self.layer
        layer.begin()
        mesh = layer.mesh
        mesh.face([(0, 0), (width, 0), (width, height), (0, height)], [scifi.BACKDROP] * 4)
        scifi.scanlines(mesh, 0.0, 0.0, width, height, with_alpha(scifi.SCAN, 0.6), 4.0)
        pw, ph = min(PANEL[0], width - 40.0), min(PANEL[1], height - 40.0)
        left, top = 0.5 * (width - pw), 0.5 * (height - ph)
        scifi.panel(mesh, left, top, pw, ph, 18.0, scifi.GLASS_DEEP, scifi.CYAN_SOFT, ("tl", "br"))
        scifi.brackets(mesh, left - 6.0, top - 6.0, pw + 12.0, ph + 12.0, 26.0, scifi.CYAN, 1.8)
        x, w = left + PAD, pw - 2 * PAD
        layer.text(self.title.upper(), (x, top + 26.0), 28, scifi.WHITE, "title")
        subtitle = self.subtitle() if self.subtitle is not None else ""
        if subtitle:
            layer.text(subtitle, (x, top + 50.0), 14, scifi.MUTED, "mono")
        ty = top + HEADER
        tx = x
        for k, (name, _) in enumerate(self.tabs):
            tw = measure.width(name.upper(), 15, "title") + 36.0
            on, hot = k == self.tab, self._hovered(k, "tab")
            points = scifi.cut_rect(tx, ty, tw, TABS - 8.0, 7.0, ("tr",))
            mesh.polygon(points, ACTIVE_TAB if on else (HOVER if hot else scifi.GLASS))
            scifi.outline(mesh, points, scifi.CYAN if on else scifi.CYAN_SOFT, 1.0)
            layer.text(name.upper(), (tx + 18.0, ty + 0.5 * (TABS - 8.0)), 15,
                       scifi.WHITE if on or hot else scifi.CYAN, "title")
            self._region(tx, ty, tw, TABS - 8.0, k, "tab")
            tx += tw + 6.0
        mesh.segment((left + 1.0, ty + TABS - 2.0), (left + pw - 1.0, ty + TABS - 2.0), scifi.CYAN_SOFT, 1.0)
        cy = ty + TABS + 8.0
        name, content = self.tabs[self.tab]
        if isinstance(content, tuple) and content[0] == "controls":
            self.draw_controls(content[1], x, cy + 4.0, w)
        elif isinstance(content, tuple) and content[0] == "columns":
            columns = content[1]
            col_w = (w - COLUMN_GAP * (len(columns) - 1)) / len(columns)
            for c, items in enumerate(columns):
                iy = cy
                for item in items:
                    iy += self.draw_item(item, x + c * (col_w + COLUMN_GAP), iy, col_w)
        else:
            for item in content:
                cy += self.draw_item(item, x, cy, w)
        fy = top + ph - FOOTER
        mesh.segment((left + 1.0, fy), (left + pw - 1.0, fy), scifi.CYAN_FAINT, 1.0)
        hint = ""
        if self.hover is not None and isinstance(self.hover[0], dict):
            hint = self.hover[0].get("hint", "")
        if hint:
            for k, line in enumerate(scifi.wrap(hint, 15, w - 200.0)[:2]):
                layer.text(line, (x, fy + 16.0 + 16.0 * k), 15, scifi.MUTED)
        layer.text(self.close_text, (x + w, fy + 0.5 * FOOTER), 13, scifi.CYAN_SOFT, "mono", "right")
        layer.end()

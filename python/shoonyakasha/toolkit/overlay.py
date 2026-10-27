"""
Overlays on screen canvases for programs that show a 3D view: cards of
readings, cards of prose, a bar of button hints, a sheet of the controls,
and a settings menu, all in one style (a dark panel with a gold hairline,
gold headings over faint rules).

    from shoonyakasha.toolkit import overlay
    card = overlay.Card(engine.ui, canvas, corner=(0, 0), max_rows=8)
    card.update("FREE CAMERA", [("SPEED", "12 m/s"), ("ORBIT",), ("PERIAPSIS", "40 km")])
    note = overlay.Narration(engine.ui, canvas, corner=(0, 1))
    note.update("CAPTURE", "The neutron is caught ...", step="2 / 5", hints=[(("[", "]"), "Back, next")])
    bar = overlay.HintBar(engine.ui, canvas)
    bar.update([(("W", "S"), "Throttle"), (("pad:A",), "Assist")])
    menu = overlay.SettingsMenu(engine, "Settings", items)       # menu.update() each frame
    sheet = overlay.HelpOverlay(engine, "CONTROLS", columns)

Cards: `Card.update(title, rows)` shows the rows given, each ("LABEL",
"value") or ("HEADING",), and fits the panel's height to them. Only what
changed since the last update is handed to the canvas.

Prose: `Narration.update(title, text, step, hints)` shows a gold title,
a step (such as "2 / 5") at its right, the text wrapped between words to
the card's width (`wrap_text`, measured as the key caps are) and a row of
key caps; the panel fits its lines.

Hints: a hint is (caps, label), caps a tuple of key or button names, each
drawn as a small cap, then the label. A name starting with "pad:" is a
gamepad's button or stick, shown without the prefix. Keyboard caps are
ivory with dark text; the gamepad's face buttons A, B, X and Y are caps in
their usual green, red, blue and yellow, its other buttons and sticks
grey. `KeyCaps` lays hints out in a row from a point on a parent element,
reusing its elements; `HintBar` is a dark panel along the bottom of the
canvas, centred, fitted to the hints handed to `update`, redrawn only when
they change. Text is measured with Pillow from the engine's default font
(fonts/Roboto-Regular.ttf), or estimated without it.

The controls sheet: `HelpOverlay(engine, title, columns)` takes columns of
sections, each (heading, rows), each row (caps, text); it is centred over
everything, built once and shown or hidden as a whole.

The settings menu: down the right side of the screen, a gold title with
the keys that close it, the settings in a scroll view grouped under gold
headings, and a footer that describes the setting under the pointer. Each
item is a dict:
    {"heading": "Simulation"}
    {"slider": "Exposure", "min": 0.05, "max": 2.0, "get": f, "set": g, "format": "%.2f"}
        (or "show": a function from the value to the text shown, in place of "format")
    {"toggle": "Ocean", "get": f, "set": g}
    {"button": "New seed", "do": f}
and any of them but a heading may have "keys", a tuple of the key names
that change it too (drawn as caps), and "hint", a sentence the footer
shows while the pointer is over it. `get()` reads the setting and
`set(value)` changes it; `do()` acts. Every `update()` hands the widgets
the pointer changed to their setters, and moves the others to what their
getters say, so changes made elsewhere show in the menu. Its widgets stay
out of keyboard and gamepad navigation, so Tab and the arrow keys keep
their meaning in the program.

Importing this module does not need the engine's extension; building an
overlay does.
"""


class _Engine:
    """The shoonyakasha package, imported when first used."""

    def __getattr__(self, name):
        import shoonyakasha
        return getattr(shoonyakasha, name)


sk = _Engine()

GOLD = (0.95, 0.85, 0.55, 1.0)


# ── Key caps and the hint bar ──────────────────────────────────────────────

HINT_PANEL = (0.02, 0.024, 0.03, 0.8)
HINT_HAIRLINE = (0.95, 0.85, 0.55, 0.6)
HINT_LABEL = (0.86, 0.88, 0.9, 1.0)
CAP = (0.92, 0.9, 0.84, 1.0)
CAP_TEXT = (0.06, 0.06, 0.07, 1.0)
PAD_GREY = (0.32, 0.34, 0.38, 1.0)
PAD_TEXT = (0.97, 0.97, 0.97, 1.0)
PAD_FACES = {"A": (0.30, 0.66, 0.22, 1.0), "B": (0.82, 0.24, 0.20, 1.0),
             "X": (0.20, 0.45, 0.84, 1.0), "Y": (0.90, 0.70, 0.10, 1.0)}
PAD_PREFIX = "pad:"

CAP_SIZE = 14            # canvas units of the caps' text
HINT_LABEL_SIZE = 15
CAP_HEIGHT = 22
CAP_PAD = 7              # left and right of a cap's text
CAP_GAP = 4              # between the caps of one hint
LABEL_GAP = 7            # between the caps and the label
HINT_GAP = 20            # between hints


class TextMeasure:
    """Widths of text in canvas units at a size, from the engine's default font."""

    def __init__(self):
        self.fonts = {}
        try:
            from PIL import ImageFont
            from .. import assets
            self.path = str(assets.locate("fonts/Roboto-Regular.ttf"))
            self.image_font = ImageFont
        except Exception:
            self.image_font = None

    def width(self, text, size):
        if self.image_font is None:
            return 0.56 * size * len(text)
        font = self.fonts.get(size)
        if font is None:
            font = self.fonts[size] = self.image_font.truetype(self.path, size)
        return font.getlength(text)


measure = TextMeasure()


def cap_text(name):
    """(the text a cap shows, whether it is a gamepad's)."""
    pad = name.startswith(PAD_PREFIX)
    return (name[len(PAD_PREFIX):] if pad else name), pad


def cap_width(name):
    return max(CAP_HEIGHT, measure.width(cap_text(name)[0], CAP_SIZE) + 2 * CAP_PAD)


def hint_width(hint):
    caps, label = hint
    width = sum(cap_width(c) for c in caps) + CAP_GAP * (len(caps) - 1)
    return width + (LABEL_GAP + measure.width(label, HINT_LABEL_SIZE) if label else 0.0)


def row_width(hints):
    return sum(hint_width(h) for h in hints) + HINT_GAP * max(len(hints) - 1, 0)


class KeyCaps:
    """Hints in a row on `parent`, with its top left at (x, y) from the
    parent's top left (canvas units, y down). Elements are made as needed
    and kept for the next call."""

    def __init__(self, ui, parent):
        self.ui = ui
        self.parent = parent
        self.caps = []           # (panel, text)
        self.labels = []
        self.used = (0, 0)

    def _place(self, element, x, y, width, height):
        self.ui.set_rect(element, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0), position=(x, y),
                         size=(width, height))

    def _cap(self, index):
        while len(self.caps) <= index:
            panel = self.ui.create_panel(self.parent, size=(CAP_HEIGHT, CAP_HEIGHT), color=CAP)
            text = self.ui.create_text(panel, "", size=CAP_SIZE, color=CAP_TEXT)
            self.ui.set_rect(text, anchor_min=(0, 0), anchor_max=(1, 1), size=(0, 0))
            self.ui.set_text_align(text, sk.TEXT_ALIGN_CENTER, sk.TEXT_ALIGN_MIDDLE)
            self.caps.append((panel, text))
        return self.caps[index]

    def _label(self, index):
        while len(self.labels) <= index:
            text = self.ui.create_text(self.parent, "", size=HINT_LABEL_SIZE, color=HINT_LABEL)
            self.ui.set_text_align(text, sk.TEXT_ALIGN_LEFT, sk.TEXT_ALIGN_MIDDLE)
            self.labels.append(text)
        return self.labels[index]

    def show(self, hints, x, y, label_colour=HINT_LABEL):
        """Lay out `hints` from (x, y); returns the row's width."""
        ui = self.ui
        caps = labels = 0
        start = x
        for k, (names, label) in enumerate(hints):
            if k:
                x += HINT_GAP
            for n, name in enumerate(names):
                if n:
                    x += CAP_GAP
                panel, text = self._cap(caps)
                caps += 1
                width = cap_width(name)
                shown, pad = cap_text(name)
                ui.set_color(panel, PAD_FACES.get(shown, PAD_GREY) if pad else CAP)
                ui.set_color(text, PAD_TEXT if pad else CAP_TEXT)
                ui.set_text(text, shown)
                self._place(panel, x, y, width, CAP_HEIGHT)
                ui.set_visible(panel, True)
                x += width
            if label:
                text = self._label(labels)
                labels += 1
                x += LABEL_GAP
                width = measure.width(label, HINT_LABEL_SIZE) + 2
                ui.set_text(text, label)
                ui.set_color(text, label_colour)
                self._place(text, x, y, width, CAP_HEIGHT)
                ui.set_visible(text, True)
                x += width
        for panel, _ in self.caps[caps:]:
            ui.set_visible(panel, False)
        for text in self.labels[labels:]:
            ui.set_visible(text, False)
        self.used = (caps, labels)
        return x - start


class HintBar:
    """The hints for what the program is doing now, along the bottom of the
    canvas. `update(hints)` redraws only when they change."""

    MARGIN = 14              # from the canvas's bottom edge
    PAD_X, PAD_Y = 16, 9

    def __init__(self, ui, canvas):
        self.ui = ui
        self.panel = ui.create_panel(canvas, size=(100, CAP_HEIGHT + 2 * self.PAD_Y), color=HINT_PANEL)
        hairline = ui.create_panel(self.panel, size=(100, 1), color=HINT_HAIRLINE)
        ui.set_rect(hairline, anchor_min=(0, 0), anchor_max=(1, 0), pivot=(0, 0), position=(0, 0), size=(0, 1))
        self.caps = KeyCaps(ui, self.panel)
        self.hints = None
        self.visible = None
        self.show(True)

    def show(self, on):
        if on != self.visible:
            self.ui.set_visible(self.panel, on)
            self.visible = on

    def update(self, hints):
        hints = tuple((tuple(caps), label) for caps, label in hints)
        if hints == self.hints:
            return
        self.hints = hints
        width = row_width(hints) + 2 * self.PAD_X
        height = CAP_HEIGHT + 2 * self.PAD_Y
        self.ui.set_rect(self.panel, anchor_min=(0.5, 1), anchor_max=(0.5, 1), pivot=(0.5, 1),
                         position=(0, -self.MARGIN), size=(width, height))
        self.caps.show(hints, self.PAD_X, self.PAD_Y)


# ── Cards ────────────────────────────────────────────────────────────────────

CARD_PANEL = (0.02, 0.024, 0.03, 0.84)
CARD_HAIRLINE = (0.95, 0.85, 0.55, 0.75)
CARD_RULE = (0.95, 0.85, 0.55, 0.3)
CARD_LABEL = (0.7, 0.73, 0.78, 1.0)
CARD_VALUE = (0.96, 0.95, 0.92, 1.0)

CARD_WIDTH = 300
CARD_PAD = 18                 # left and right of the rows, above the title, below the last row
CARD_TITLE_SIZE, CARD_TITLE_HEIGHT = 15, 32
CARD_LABEL_SIZE, CARD_VALUE_SIZE, CARD_ROW = 14, 17, 26
CARD_HEADING_GAP = 10         # extra space above a heading


class Card:
    def __init__(self, ui, canvas, corner, max_rows, margin=(24, 24), width=CARD_WIDTH):
        """`corner`: the canvas corner the card sits in, (0, 0) top left,
        (1, 1) bottom right; `margin`: its distance from the canvas's edges."""
        self.ui = ui
        self.corner = corner
        self.margin = margin
        self.width = width
        self.panel = ui.create_panel(canvas, size=(width, 100), color=CARD_PANEL)
        hairline = ui.create_panel(self.panel, size=(2, 100), color=CARD_HAIRLINE)
        ui.set_rect(hairline, anchor_min=(0, 0), anchor_max=(0, 1), pivot=(0, 0), position=(0, 0), size=(2, 0))
        self.title = self._text(CARD_TITLE_SIZE, GOLD, sk.TEXT_ALIGN_LEFT)
        self.rows = []
        for _ in range(max_rows):
            label = self._text(CARD_LABEL_SIZE, CARD_LABEL, sk.TEXT_ALIGN_LEFT)
            value = self._text(CARD_VALUE_SIZE, CARD_VALUE, sk.TEXT_ALIGN_RIGHT)
            rule = ui.create_panel(self.panel, size=(width - 2 * CARD_PAD, 1), color=CARD_RULE)
            ui.set_rect(rule, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0), position=(CARD_PAD, 0),
                        size=(width - 2 * CARD_PAD, 1))
            self.rows.append((label, value, rule))
        self.state = {}          # element: (text, colour, y, visible) last handed to the canvas
        self.visible = None
        self.show(False)

    def _text(self, size, colour, align):
        text = self.ui.create_text(self.panel, "", size=size, color=colour)
        self.ui.set_rect(text, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0), position=(CARD_PAD, 0),
                         size=(self.width - 2 * CARD_PAD, CARD_ROW))
        self.ui.set_text_align(text, align, sk.TEXT_ALIGN_MIDDLE)
        return text

    def show(self, on):
        if self.visible != on:
            self.ui.set_visible(self.panel, on)
            self.visible = on

    def _set(self, element, y=None, text=None, colour=None, visible=True):
        """Hand the canvas what changed of an element's text, colour, height
        and visibility."""
        old = self.state.get(element, (None, None, None, None))
        if visible != old[3]:
            self.ui.set_visible(element, visible)
        if not visible:
            self.state[element] = old[:3] + (False,)
            return
        if text is not None and text != old[0]:
            self.ui.set_text(element, text)
        if colour is not None and colour != old[1]:
            self.ui.set_color(element, colour)
        if y is not None and y != old[2]:
            self.ui.set_position(element, (CARD_PAD, y))
        self.state[element] = (text, colour, y, True)

    def update(self, title, rows):
        y = CARD_PAD - 6
        self._set(self.title, y, title)
        y += CARD_TITLE_HEIGHT
        for k, (label, value, rule) in enumerate(self.rows):
            if k >= len(rows):
                for element in (label, value, rule):
                    self._set(element, visible=False)
                continue
            row = rows[k]
            if len(row) == 1:
                y += CARD_HEADING_GAP
                self._set(label, y, row[0], GOLD)
                self._set(value, visible=False)
                self._set(rule, y + CARD_ROW - 3)
            else:
                self._set(label, y, row[0], CARD_LABEL)
                self._set(value, y, row[1])
                self._set(rule, visible=False)
            y += CARD_ROW
        height = y + CARD_PAD - 8
        corner = self.corner
        position = (self.margin[0] * (1 - 2 * corner[0]), self.margin[1] * (1 - 2 * corner[1]))
        if self.state.get(self.panel) != height:
            self.ui.set_rect(self.panel, anchor_min=corner, anchor_max=corner, pivot=corner,
                             position=position, size=(self.width, height))
            self.state[self.panel] = height


# ── Narration ────────────────────────────────────────────────────────────────

NARRATION_WIDTH = 440
NARRATION_TEXT = (0.9, 0.91, 0.93, 1.0)
NARRATION_MUTED = (0.6, 0.63, 0.68, 1.0)
NARRATION_TEXT_SIZE, NARRATION_LINE = 16, 23
NARRATION_STEP_SIZE = 13
NARRATION_HINTS_GAP = 10      # above the key caps' row


def wrap_text(text, size, width):
    """`text` broken into lines no wider than `width` canvas units at font
    size `size`, between words; a line break in the text starts a new line,
    and a word wider than `width` gets a line of its own."""
    lines = []
    for paragraph in text.split("\n"):
        line = ""
        for word in paragraph.split():
            candidate = word if not line else line + " " + word
            if line and measure.width(candidate, size) > width:
                lines.append(line)
                line = word
            else:
                line = candidate
        lines.append(line)
    return lines


class Narration:
    def __init__(self, ui, canvas, corner=(0, 1), max_lines=8, margin=(24, 24), width=NARRATION_WIDTH):
        """A card of prose: a gold title, a step such as "2 / 5" at its
        right, the text wrapped below in up to `max_lines` lines, and an
        optional row of key caps. `corner` and `margin` place it as a
        Card's."""
        self.ui = ui
        self.corner = corner
        self.margin = margin
        self.width = width
        inner = width - 2 * CARD_PAD
        self.panel = ui.create_panel(canvas, size=(width, 100), color=CARD_PANEL)
        hairline = ui.create_panel(self.panel, size=(2, 100), color=CARD_HAIRLINE)
        ui.set_rect(hairline, anchor_min=(0, 0), anchor_max=(0, 1), pivot=(0, 0), position=(0, 0), size=(2, 0))
        self.title = self._text(CARD_TITLE_SIZE, GOLD, sk.TEXT_ALIGN_LEFT, CARD_TITLE_HEIGHT)
        self.step = self._text(NARRATION_STEP_SIZE, NARRATION_MUTED, sk.TEXT_ALIGN_RIGHT, CARD_TITLE_HEIGHT)
        self.rule = ui.create_panel(self.panel, size=(inner, 1), color=CARD_RULE)
        ui.set_rect(self.rule, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0),
                    position=(CARD_PAD, CARD_PAD - 6 + CARD_TITLE_HEIGHT - 2), size=(inner, 1))
        self.lines = [self._text(NARRATION_TEXT_SIZE, NARRATION_TEXT, sk.TEXT_ALIGN_LEFT, NARRATION_LINE)
                      for _ in range(max_lines)]
        self.caps = KeyCaps(ui, self.panel)
        self.shown = None        # (title, text, step, hints) last laid out
        self.visible = None
        self.show(False)

    def _text(self, size, colour, align, height):
        text = self.ui.create_text(self.panel, "", size=size, color=colour)
        self.ui.set_rect(text, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0), position=(CARD_PAD, 0),
                         size=(self.width - 2 * CARD_PAD, height))
        self.ui.set_text_align(text, align, sk.TEXT_ALIGN_MIDDLE)
        return text

    def show(self, on):
        if self.visible != on:
            self.ui.set_visible(self.panel, on)
            self.visible = on

    def update(self, title, text, step="", hints=()):
        """Show `title`, `step` and `text`, wrapped, and the key caps of
        `hints` (as a HintBar's) below. Lines past `max_lines` are dropped,
        the last one shown ending in an ellipsis. Does nothing when all
        four are as last shown."""
        if self.shown == (title, text, step, tuple(hints)):
            return
        self.shown = (title, text, step, tuple(hints))
        ui = self.ui
        y = CARD_PAD - 6
        for element, value in ((self.title, title), (self.step, step)):
            ui.set_text(element, value)
            ui.set_position(element, (CARD_PAD, y))
        y += CARD_TITLE_HEIGHT + 4
        wrapped = wrap_text(text, NARRATION_TEXT_SIZE, self.width - 2 * CARD_PAD)
        if len(wrapped) > len(self.lines):
            wrapped = wrapped[:len(self.lines)]
            wrapped[-1] += " …"
        for k, element in enumerate(self.lines):
            on = k < len(wrapped)
            ui.set_visible(element, on)
            if on:
                ui.set_text(element, wrapped[k])
                ui.set_position(element, (CARD_PAD, y))
                y += NARRATION_LINE
        if hints:
            y += NARRATION_HINTS_GAP
            self.caps.show(list(hints), CARD_PAD, y, NARRATION_MUTED)
            y += CAP_HEIGHT
        else:
            self.caps.show([], CARD_PAD, y)
        height = y + CARD_PAD - 4
        corner = self.corner
        position = (self.margin[0] * (1 - 2 * corner[0]), self.margin[1] * (1 - 2 * corner[1]))
        ui.set_rect(self.panel, anchor_min=corner, anchor_max=corner, pivot=corner,
                    position=position, size=(self.width, height))


# ── The controls sheet ───────────────────────────────────────────────────────

SHEET_PANEL = (0.02, 0.024, 0.03, 0.92)
SHEET_HAIRLINE = (0.95, 0.85, 0.55, 0.75)
SHEET_RULE = (0.95, 0.85, 0.55, 0.3)
SHEET_TEXT = (0.86, 0.88, 0.9, 1.0)
SHEET_MUTED = (0.6, 0.63, 0.68, 1.0)

SHEET_PAD = 28                 # around the columns
SHEET_TITLE_HEIGHT = 56
SHEET_COLUMN_WIDTH = 380
SHEET_COLUMN_GAP = 32
SHEET_ROW = 27
SHEET_HEADING = 34             # a heading's row, its rule under it
SHEET_SECTION_GAP = 14         # above a heading after the first in a column
SHEET_CAPS_WIDTH = 140         # the caps' share of a row; the text starts after it


class HelpOverlay:
    def __init__(self, engine, title, columns, footer="", reference_size=(1600, 900)):
        ui = engine.ui
        self.ui = ui
        self.canvas = ui.create_canvas(reference_size=reference_size)
        ui.set_canvas_sort_order(self.canvas, 20)
        rows_height = max(self._column_height(column) for column in columns)
        width = 2 * SHEET_PAD + len(columns) * SHEET_COLUMN_WIDTH + (len(columns) - 1) * SHEET_COLUMN_GAP
        height = SHEET_TITLE_HEIGHT + rows_height + 2 * SHEET_PAD
        panel = ui.create_panel(self.canvas, size=(width, height), color=SHEET_PANEL)
        ui.set_rect(panel, anchor_min=(0.5, 0.5), anchor_max=(0.5, 0.5), pivot=(0.5, 0.5), position=(0, 0),
                    size=(width, height))
        hairline = ui.create_panel(panel, size=(2, height), color=SHEET_HAIRLINE)
        ui.set_rect(hairline, anchor_min=(0, 0), anchor_max=(0, 1), pivot=(0, 0), position=(0, 0), size=(2, 0))
        self._text(panel, title, 22, GOLD, SHEET_PAD, SHEET_PAD - 4, width - 2 * SHEET_PAD, 30)
        if footer:
            self._text(panel, footer, 14, SHEET_MUTED, SHEET_PAD, SHEET_PAD, width - 2 * SHEET_PAD, 30, sk.TEXT_ALIGN_RIGHT)

        for c, column in enumerate(columns):
            x = SHEET_PAD + c * (SHEET_COLUMN_WIDTH + SHEET_COLUMN_GAP)
            y = SHEET_PAD + SHEET_TITLE_HEIGHT
            for s, (heading, rows) in enumerate(column):
                if s:
                    y += SHEET_SECTION_GAP
                self._text(panel, heading.upper(), 15, GOLD, x, y, SHEET_COLUMN_WIDTH, SHEET_HEADING - 8)
                rule = ui.create_panel(panel, size=(SHEET_COLUMN_WIDTH, 1), color=SHEET_RULE)
                self._place(rule, x, y + SHEET_HEADING - 8, SHEET_COLUMN_WIDTH, 1)
                y += SHEET_HEADING
                for caps, text in rows:
                    KeyCaps(ui, panel).show([(caps, "")], x, y + (SHEET_ROW - CAP_HEIGHT) / 2 - 1)
                    self._text(panel, text, 15, SHEET_TEXT, x + SHEET_CAPS_WIDTH, y, SHEET_COLUMN_WIDTH - SHEET_CAPS_WIDTH, SHEET_ROW)
                    y += SHEET_ROW
        self.visible = True
        self.show(False)

    @staticmethod
    def _column_height(column):
        return sum(SHEET_HEADING + SHEET_ROW * len(rows) for _, rows in column) + SHEET_SECTION_GAP * (len(column) - 1)

    def _place(self, element, x, y, width, height):
        self.ui.set_rect(element, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0), position=(x, y),
                         size=(width, height))

    def _text(self, parent, text, size, colour, x, y, width, height, align=None):
        """A text element; `align` is a horizontal alignment, left by default."""
        element = self.ui.create_text(parent, text, size=size, color=colour)
        self._place(element, x, y, width, height)
        self.ui.set_text_align(element, sk.TEXT_ALIGN_LEFT if align is None else align, sk.TEXT_ALIGN_MIDDLE)
        return element

    def show(self, on):
        if on != self.visible:
            self.visible = on
            self.ui.set_visible(self.canvas, on)


# ── The settings menu ────────────────────────────────────────────────────────

MENU_WIDTH = 400
MENU_PAD = 20
MENU_TOP = 62                 # the title's band
MENU_FOOTER = 74              # the footer's band
MENU_SCROLLBAR = 12           # kept clear at the right of the rows
MENU_PANEL = (0.02, 0.024, 0.03, 0.9)
MENU_HAIRLINE = (0.95, 0.85, 0.55, 0.75)
MENU_RULE = (0.95, 0.85, 0.55, 0.3)
MENU_TEXT = (0.86, 0.88, 0.9, 1.0)
MENU_VALUE = (0.96, 0.95, 0.92, 1.0)
MENU_MUTED = (0.6, 0.63, 0.68, 1.0)
MENU_TRACK = (0.13, 0.14, 0.17, 1.0)
MENU_FILL = (0.85, 0.74, 0.45, 1.0)
MENU_HANDLE = (0.96, 0.95, 0.92, 1.0)
MENU_SURFACE = (0.12, 0.13, 0.16, 1.0)
MENU_IDLE_FOOTER = "Point at a setting to see what it does."


class SettingsMenu:
    def __init__(self, engine, title, items, close_keys=("F1",), reference_size=(1600, 900)):
        self.engine = engine
        ui = engine.ui
        self.ui = ui
        self.canvas = ui.create_canvas(reference_size=reference_size)
        ui.set_canvas_sort_order(self.canvas, 10)
        panel = ui.create_panel(self.canvas, size=(MENU_WIDTH, 100), color=MENU_PANEL)
        ui.set_rect(panel, anchor_min=(1, 0), anchor_max=(1, 1), pivot=(1, 0), position=(-16, 16), size=(MENU_WIDTH, -32))
        hairline = ui.create_panel(panel, size=(2, 100), color=MENU_HAIRLINE)
        ui.set_rect(hairline, anchor_min=(0, 0), anchor_max=(0, 1), pivot=(0, 0), position=(0, 0), size=(2, 0))

        heading = ui.create_text(panel, title.upper(), size=20, color=GOLD)
        self._place(heading, MENU_PAD, 18, MENU_WIDTH - 2 * MENU_PAD, 30)
        ui.set_text_align(heading, sk.TEXT_ALIGN_LEFT, sk.TEXT_ALIGN_MIDDLE)
        close = [(close_keys, "close")]
        KeyCaps(ui, panel).show(close, MENU_WIDTH - MENU_PAD - row_width(close), 18 + (30 - CAP_HEIGHT) / 2)
        rule = ui.create_panel(panel, size=(MENU_WIDTH - 2 * MENU_PAD, 1), color=MENU_RULE)
        self._place(rule, MENU_PAD, MENU_TOP - 6, MENU_WIDTH - 2 * MENU_PAD, 1)

        self.view = ui.create_scroll_view(panel, size=(MENU_WIDTH - 2 * MENU_PAD, 100))
        ui.set_rect(self.view, anchor_min=(0, 0), anchor_max=(1, 1), pivot=(0, 0), position=(MENU_PAD, MENU_TOP),
                    size=(-2 * MENU_PAD + MENU_SCROLLBAR, -(MENU_TOP + MENU_FOOTER)))
        ui.set_scroll_settings(self.view, wheel_speed=60.0)
        content = ui.scroll_content(self.view)

        footer_rule = ui.create_panel(panel, size=(MENU_WIDTH - 2 * MENU_PAD, 1), color=MENU_RULE)
        ui.set_rect(footer_rule, anchor_min=(0, 1), anchor_max=(0, 1), pivot=(0, 1), position=(MENU_PAD, -(MENU_FOOTER - 6)),
                    size=(MENU_WIDTH - 2 * MENU_PAD, 1))
        self.footer = ui.create_text(panel, MENU_IDLE_FOOTER, size=14, color=MENU_MUTED)
        ui.set_rect(self.footer, anchor_min=(0, 1), anchor_max=(0, 1), pivot=(0, 1), position=(MENU_PAD, -12),
                    size=(MENU_WIDTH - 2 * MENU_PAD, MENU_FOOTER - 24))
        ui.set_text_align(self.footer, sk.TEXT_ALIGN_LEFT, sk.TEXT_ALIGN_TOP)
        ui.set_text_wrap(self.footer, True)
        self.footer_text = MENU_IDLE_FOOTER

        width = MENU_WIDTH - 2 * MENU_PAD - MENU_SCROLLBAR
        self.items = []
        self.hints = {}          # widget: footer text
        for item in items:
            entry = dict(item)
            if "heading" in item:
                row = self._row(content, width, 40)
                text = ui.create_text(row, item["heading"].upper(), size=14, color=GOLD)
                self._place(text, 0, 12, width, 22)
                ui.set_text_align(text, sk.TEXT_ALIGN_LEFT, sk.TEXT_ALIGN_MIDDLE)
                line = ui.create_panel(row, size=(width, 1), color=MENU_RULE)
                self._place(line, 0, 36, width, 1)
            elif "slider" in item:
                row = self._row(content, width, 52)
                entry["label"] = ui.create_text(row, item["slider"], size=15, color=MENU_TEXT)
                self._place(entry["label"], 0, 2, width, 22)
                ui.set_text_align(entry["label"], sk.TEXT_ALIGN_LEFT, sk.TEXT_ALIGN_MIDDLE)
                self._caps(row, item, measure.width(item["slider"], 15) + 10, 2)
                entry["value"] = ui.create_text(row, "", size=15, color=MENU_VALUE)
                self._place(entry["value"], 0, 2, width, 22)
                ui.set_text_align(entry["value"], sk.TEXT_ALIGN_RIGHT, sk.TEXT_ALIGN_MIDDLE)
                entry["widget"] = ui.create_slider(row, min=item["min"], max=item["max"], value=item["get"](),
                                                   size=(width, 18))
                self._place(entry["widget"], 0, 28, width, 18)
                for part, colour in (("track", MENU_TRACK), ("fill", MENU_FILL), ("handle", MENU_HANDLE)):
                    ui.set_widget_part_color(entry["widget"], part, colour)
                self._label(entry)
            elif "toggle" in item:
                # The toggle spans the row so all of it takes the pointer; its label is the row's own
                # text, in the sliders' size.
                row = self._row(content, width, 36)
                caps = self._caps_width(item)
                entry["widget"] = ui.create_toggle(row, "", is_on=item["get"](), size=(width - caps, 28))
                self._place(entry["widget"], 0, 4, width - caps, 28)
                ui.set_widget_part_color(entry["widget"], "fill", MENU_FILL)
                label = ui.create_text(row, item["toggle"], size=15, color=MENU_TEXT)
                self._place(label, 34, 4, width - caps - 34, 28)
                ui.set_text_align(label, sk.TEXT_ALIGN_LEFT, sk.TEXT_ALIGN_MIDDLE)
                self._caps(row, item, width - caps + 8, 4 + (28 - CAP_HEIGHT) / 2)
            elif "button" in item:
                row = self._row(content, width, 44)
                caps = self._caps_width(item)
                entry["widget"] = ui.create_button(row, item["button"], size=(width - caps, 34))
                self._place(entry["widget"], 0, 5, width - caps, 34)
                ui.set_widget_part_color(entry["widget"], "surface", MENU_SURFACE)
                ui.set_widget_part_color(entry["widget"], "text", GOLD)
                self._caps(row, item, width - caps + 8, 5 + (34 - CAP_HEIGHT) / 2)
            if "widget" in entry:
                ui.set_navigable(entry["widget"], False)
                self.hints[entry["widget"]] = self._footer_text(item)
            self.items.append(entry)
        self.visible = True
        self.show(False)

    def _place(self, element, x, y, width, height):
        self.ui.set_rect(element, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0), position=(x, y),
                         size=(width, height))

    def _row(self, content, width, height):
        row = self.ui.create_element(content, size=(width, height))
        self.ui.set_layout_element(row, min=(width, height), preferred=(width, height))
        return row

    @staticmethod
    def _caps_width(item):
        return row_width([(item["keys"], "")]) + 8 if "keys" in item else 0

    def _caps(self, row, item, x, y):
        if "keys" in item:
            KeyCaps(self.ui, row).show([(item["keys"], "")], x, y)

    @staticmethod
    def _footer_text(item):
        name = item.get("slider") or item.get("toggle") or item.get("button")
        text = item.get("hint", name)
        if "keys" in item:
            text += "  Keys: " + " ".join(item["keys"]) + "."
        return text

    def _label(self, entry):
        value = entry["get"]()
        shown = entry["show"](value) if "show" in entry else entry.get("format", "%.2f") % value
        if shown != entry.get("shown"):
            self.ui.set_text(entry["value"], shown)
            entry["shown"] = shown

    def show(self, on):
        self.visible = on
        self.ui.set_visible(self.canvas, on)

    def update(self):
        if not self.visible:
            return
        ui = self.ui
        for entry in self.items:
            widget = entry.get("widget")
            if widget is None:
                continue
            if "slider" in entry:
                if ui.value_changed(widget):
                    entry["set"](ui.slider_value(widget))
                elif abs(ui.slider_value(widget) - entry["get"]()) > 1e-6:
                    ui.set_slider_value(widget, entry["get"]())
                self._label(entry)
            elif "toggle" in entry:
                if ui.value_changed(widget):
                    entry["set"](ui.toggle_value(widget))
                elif ui.toggle_value(widget) != bool(entry["get"]()):
                    ui.set_toggle_value(widget, bool(entry["get"]()))
            elif "button" in entry and ui.was_clicked(widget):
                entry["do"]()
        footer = self.hints.get(ui.hovered_element, MENU_IDLE_FOOTER)
        if footer != self.footer_text:
            ui.set_text(self.footer, footer)
            self.footer_text = footer

    @property
    def pointer_over(self):
        return self.visible and self.ui.pointer_over_ui

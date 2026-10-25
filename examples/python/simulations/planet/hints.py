"""
Key caps and a bar of button hints on a screen canvas.

A hint is (caps, label): caps a tuple of key or button names, each drawn
as a small cap, then the label. A name starting with "pad:" is a
gamepad's button or stick, shown without the prefix. Keyboard caps are
ivory with dark text; the gamepad's face buttons A, B, X and Y are caps in
their usual green, red, blue and yellow, its other buttons and sticks
grey.

`KeyCaps` lays hints out in a row from a point on a parent element, reusing
its elements. `HintBar` is a dark panel along the bottom of the canvas,
centred, that shows the hints handed to `update`, fitted to their width.
Text is measured with Pillow from the engine's default font
(fonts/Roboto-Regular.ttf), or estimated without it.
"""

import shoonyakasha as sk

PANEL = (0.02, 0.024, 0.03, 0.8)
HAIRLINE = (0.95, 0.85, 0.55, 0.6)
LABEL = (0.86, 0.88, 0.9, 1.0)
CAP = (0.92, 0.9, 0.84, 1.0)
CAP_TEXT = (0.06, 0.06, 0.07, 1.0)
PAD_GREY = (0.32, 0.34, 0.38, 1.0)
PAD_TEXT = (0.97, 0.97, 0.97, 1.0)
PAD_FACES = {"A": (0.30, 0.66, 0.22, 1.0), "B": (0.82, 0.24, 0.20, 1.0),
             "X": (0.20, 0.45, 0.84, 1.0), "Y": (0.90, 0.70, 0.10, 1.0)}
PAD_PREFIX = "pad:"

CAP_SIZE = 14            # canvas units of the caps' text
LABEL_SIZE = 15
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
            self.path = str(sk.assets.locate("fonts/Roboto-Regular.ttf"))
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
    return width + (LABEL_GAP + measure.width(label, LABEL_SIZE) if label else 0.0)


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
            text = self.ui.create_text(self.parent, "", size=LABEL_SIZE, color=LABEL)
            self.ui.set_text_align(text, sk.TEXT_ALIGN_LEFT, sk.TEXT_ALIGN_MIDDLE)
            self.labels.append(text)
        return self.labels[index]

    def show(self, hints, x, y, label_colour=LABEL):
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
                width = measure.width(label, LABEL_SIZE) + 2
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
        self.panel = ui.create_panel(canvas, size=(100, CAP_HEIGHT + 2 * self.PAD_Y), color=PANEL)
        hairline = ui.create_panel(self.panel, size=(100, 1), color=HAIRLINE)
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

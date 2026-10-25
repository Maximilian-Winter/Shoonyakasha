"""
Cards of readings on a screen canvas: a dark translucent panel with a gold
hairline down its left edge, a small uppercase gold title, and rows of
small uppercase grey labels with their values in ivory, right-aligned. A
row can instead be a section heading: gold, with a faint rule under it.

`Card.update(title, rows)` shows the rows given, each ("LABEL", "value")
or ("HEADING",), and fits the panel's height to them. Only what changed
since the last update is handed to the canvas.
"""

import shoonyakasha as sk

PANEL = (0.02, 0.024, 0.03, 0.84)
GOLD = (0.95, 0.85, 0.55, 1.0)
HAIRLINE = (0.95, 0.85, 0.55, 0.75)
RULE = (0.95, 0.85, 0.55, 0.3)
LABEL = (0.7, 0.73, 0.78, 1.0)
VALUE = (0.96, 0.95, 0.92, 1.0)

WIDTH = 300
PAD = 18                 # left and right of the rows, above the title, below the last row
TITLE_SIZE, TITLE_HEIGHT = 15, 32
LABEL_SIZE, VALUE_SIZE, ROW = 14, 17, 26
HEADING_GAP = 10         # extra space above a heading


class Card:
    def __init__(self, ui, canvas, corner, max_rows, margin=(24, 24), width=WIDTH):
        """`corner`: the canvas corner the card sits in, (0, 0) top left,
        (1, 1) bottom right; `margin`: its distance from the canvas's edges."""
        self.ui = ui
        self.corner = corner
        self.margin = margin
        self.width = width
        self.panel = ui.create_panel(canvas, size=(width, 100), color=PANEL)
        hairline = ui.create_panel(self.panel, size=(2, 100), color=HAIRLINE)
        ui.set_rect(hairline, anchor_min=(0, 0), anchor_max=(0, 1), pivot=(0, 0), position=(0, 0), size=(2, 0))
        self.title = self._text(TITLE_SIZE, GOLD, sk.TEXT_ALIGN_LEFT)
        self.rows = []
        for _ in range(max_rows):
            label = self._text(LABEL_SIZE, LABEL, sk.TEXT_ALIGN_LEFT)
            value = self._text(VALUE_SIZE, VALUE, sk.TEXT_ALIGN_RIGHT)
            rule = ui.create_panel(self.panel, size=(width - 2 * PAD, 1), color=RULE)
            ui.set_rect(rule, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0), position=(PAD, 0),
                        size=(width - 2 * PAD, 1))
            self.rows.append((label, value, rule))
        self.state = {}          # element: (text, colour, y, visible) last handed to the canvas
        self.visible = None
        self.show(False)

    def _text(self, size, colour, align):
        text = self.ui.create_text(self.panel, "", size=size, color=colour)
        self.ui.set_rect(text, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0), position=(PAD, 0),
                         size=(self.width - 2 * PAD, ROW))
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
            self.ui.set_position(element, (PAD, y))
        self.state[element] = (text, colour, y, True)

    def update(self, title, rows):
        y = PAD - 6
        self._set(self.title, y, title)
        y += TITLE_HEIGHT
        for k, (label, value, rule) in enumerate(self.rows):
            if k >= len(rows):
                for element in (label, value, rule):
                    self._set(element, visible=False)
                continue
            row = rows[k]
            if len(row) == 1:
                y += HEADING_GAP
                self._set(label, y, row[0], GOLD)
                self._set(value, visible=False)
                self._set(rule, y + ROW - 3)
            else:
                self._set(label, y, row[0], LABEL)
                self._set(value, y, row[1])
                self._set(rule, visible=False)
            y += ROW
        height = y + PAD - 8
        corner = self.corner
        position = (self.margin[0] * (1 - 2 * corner[0]), self.margin[1] * (1 - 2 * corner[1]))
        if self.state.get(self.panel) != height:
            self.ui.set_rect(self.panel, anchor_min=corner, anchor_max=corner, pivot=corner,
                             position=position, size=(self.width, height))
            self.state[self.panel] = height

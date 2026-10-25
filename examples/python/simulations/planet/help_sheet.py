"""
A sheet of the controls on a screen canvas, centred, over everything else:
a dark panel with a gold hairline down its left edge and a gold title, and
columns of sections, each a gold heading over a faint rule and rows of key
caps (hints.py) with what they do.

`HelpOverlay(engine, title, columns)` takes columns of sections, each
(heading, rows), each row (caps, text) with caps a tuple of key or button
names. It is built once and shown or hidden as a whole.
"""

import shoonyakasha as sk

from hints import CAP_HEIGHT, KeyCaps

PANEL = (0.02, 0.024, 0.03, 0.92)
GOLD = (0.95, 0.85, 0.55, 1.0)
HAIRLINE = (0.95, 0.85, 0.55, 0.75)
RULE = (0.95, 0.85, 0.55, 0.3)
TEXT = (0.86, 0.88, 0.9, 1.0)
MUTED = (0.6, 0.63, 0.68, 1.0)

PAD = 28                 # around the columns
TITLE_HEIGHT = 56
COLUMN_WIDTH = 380
COLUMN_GAP = 32
ROW = 27
HEADING = 34             # a heading's row, its rule under it
SECTION_GAP = 14         # above a heading after the first in a column
CAPS_WIDTH = 140         # the caps' share of a row; the text starts after it


class HelpOverlay:
    def __init__(self, engine, title, columns, footer="", reference_size=(1600, 900)):
        ui = engine.ui
        self.ui = ui
        self.canvas = ui.create_canvas(reference_size=reference_size)
        ui.set_canvas_sort_order(self.canvas, 20)
        rows_height = max(self._column_height(column) for column in columns)
        width = 2 * PAD + len(columns) * COLUMN_WIDTH + (len(columns) - 1) * COLUMN_GAP
        height = TITLE_HEIGHT + rows_height + 2 * PAD
        panel = ui.create_panel(self.canvas, size=(width, height), color=PANEL)
        ui.set_rect(panel, anchor_min=(0.5, 0.5), anchor_max=(0.5, 0.5), pivot=(0.5, 0.5), position=(0, 0),
                    size=(width, height))
        hairline = ui.create_panel(panel, size=(2, height), color=HAIRLINE)
        ui.set_rect(hairline, anchor_min=(0, 0), anchor_max=(0, 1), pivot=(0, 0), position=(0, 0), size=(2, 0))
        self._text(panel, title, 22, GOLD, PAD, PAD - 4, width - 2 * PAD, 30)
        if footer:
            self._text(panel, footer, 14, MUTED, PAD, PAD, width - 2 * PAD, 30, sk.TEXT_ALIGN_RIGHT)

        for c, column in enumerate(columns):
            x = PAD + c * (COLUMN_WIDTH + COLUMN_GAP)
            y = PAD + TITLE_HEIGHT
            for s, (heading, rows) in enumerate(column):
                if s:
                    y += SECTION_GAP
                self._text(panel, heading.upper(), 15, GOLD, x, y, COLUMN_WIDTH, HEADING - 8)
                rule = ui.create_panel(panel, size=(COLUMN_WIDTH, 1), color=RULE)
                self._place(rule, x, y + HEADING - 8, COLUMN_WIDTH, 1)
                y += HEADING
                for caps, text in rows:
                    KeyCaps(ui, panel).show([(caps, "")], x, y + (ROW - CAP_HEIGHT) / 2 - 1)
                    self._text(panel, text, 15, TEXT, x + CAPS_WIDTH, y, COLUMN_WIDTH - CAPS_WIDTH, ROW)
                    y += ROW
        self.visible = True
        self.show(False)

    @staticmethod
    def _column_height(column):
        return sum(HEADING + ROW * len(rows) for _, rows in column) + SECTION_GAP * (len(column) - 1)

    def _place(self, element, x, y, width, height):
        self.ui.set_rect(element, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0), position=(x, y),
                         size=(width, height))

    def _text(self, parent, text, size, colour, x, y, width, height, align=sk.TEXT_ALIGN_LEFT):
        element = self.ui.create_text(parent, text, size=size, color=colour)
        self._place(element, x, y, width, height)
        self.ui.set_text_align(element, align, sk.TEXT_ALIGN_MIDDLE)
        return element

    def show(self, on):
        if on != self.visible:
            self.visible = on
            self.ui.set_visible(self.canvas, on)

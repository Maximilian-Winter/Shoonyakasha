"""
A settings menu on a screen canvas down the right side of the screen, in
the cards' style (cards.py): a dark panel with a gold hairline down its
left edge, a gold title with the key that closes it, the settings in a
scroll view grouped under gold headings, and a footer that describes the
setting under the pointer. Shown and hidden as a whole.

Each item is a dict:
    {"heading": "Simulation"}
    {"slider": "Exposure", "min": 0.05, "max": 2.0, "get": f, "set": g, "format": "%.2f"}
        (or "show": a function from the value to the text shown, in place of "format")
    {"toggle": "Ocean", "get": f, "set": g}
    {"button": "New seed", "do": f}
and any of them but a heading may have "keys", a tuple of the key names
that change it too (drawn as caps, hints.py), and "hint", a sentence the
footer shows while the pointer is over it.
`get()` reads the setting and `set(value)` changes it; `do()` acts. Every
`update()` hands the widgets the pointer changed to their setters, and
moves the others to what their getters say, so changes made elsewhere (by
keys) show in the menu.

The widgets stay out of keyboard and gamepad navigation, so Tab and the
arrow keys keep their meaning in the program.
"""

import shoonyakasha as sk

from hints import CAP_HEIGHT, KeyCaps, measure, row_width

WIDTH = 400
PAD = 20
TOP = 62                 # the title's band
FOOTER = 74              # the footer's band
SCROLLBAR = 12           # kept clear at the right of the rows
PANEL = (0.02, 0.024, 0.03, 0.9)
GOLD = (0.95, 0.85, 0.55, 1.0)
HAIRLINE = (0.95, 0.85, 0.55, 0.75)
RULE = (0.95, 0.85, 0.55, 0.3)
TEXT = (0.86, 0.88, 0.9, 1.0)
VALUE = (0.96, 0.95, 0.92, 1.0)
MUTED = (0.6, 0.63, 0.68, 1.0)
TRACK = (0.13, 0.14, 0.17, 1.0)
FILL = (0.85, 0.74, 0.45, 1.0)
HANDLE = (0.96, 0.95, 0.92, 1.0)
SURFACE = (0.12, 0.13, 0.16, 1.0)
IDLE_FOOTER = "Point at a setting to see what it does."


class SettingsMenu:
    def __init__(self, engine, title, items, close_keys=("F1",), reference_size=(1600, 900)):
        self.engine = engine
        ui = engine.ui
        self.ui = ui
        self.canvas = ui.create_canvas(reference_size=reference_size)
        ui.set_canvas_sort_order(self.canvas, 10)
        panel = ui.create_panel(self.canvas, size=(WIDTH, 100), color=PANEL)
        ui.set_rect(panel, anchor_min=(1, 0), anchor_max=(1, 1), pivot=(1, 0), position=(-16, 16), size=(WIDTH, -32))
        hairline = ui.create_panel(panel, size=(2, 100), color=HAIRLINE)
        ui.set_rect(hairline, anchor_min=(0, 0), anchor_max=(0, 1), pivot=(0, 0), position=(0, 0), size=(2, 0))

        heading = ui.create_text(panel, title.upper(), size=20, color=GOLD)
        self._place(heading, PAD, 18, WIDTH - 2 * PAD, 30)
        ui.set_text_align(heading, sk.TEXT_ALIGN_LEFT, sk.TEXT_ALIGN_MIDDLE)
        close = [(close_keys, "close")]
        KeyCaps(ui, panel).show(close, WIDTH - PAD - row_width(close), 18 + (30 - CAP_HEIGHT) / 2)
        rule = ui.create_panel(panel, size=(WIDTH - 2 * PAD, 1), color=RULE)
        self._place(rule, PAD, TOP - 6, WIDTH - 2 * PAD, 1)

        self.view = ui.create_scroll_view(panel, size=(WIDTH - 2 * PAD, 100))
        ui.set_rect(self.view, anchor_min=(0, 0), anchor_max=(1, 1), pivot=(0, 0), position=(PAD, TOP),
                    size=(-2 * PAD + SCROLLBAR, -(TOP + FOOTER)))
        ui.set_scroll_settings(self.view, wheel_speed=60.0)
        content = ui.scroll_content(self.view)

        footer_rule = ui.create_panel(panel, size=(WIDTH - 2 * PAD, 1), color=RULE)
        ui.set_rect(footer_rule, anchor_min=(0, 1), anchor_max=(0, 1), pivot=(0, 1), position=(PAD, -(FOOTER - 6)),
                    size=(WIDTH - 2 * PAD, 1))
        self.footer = ui.create_text(panel, IDLE_FOOTER, size=14, color=MUTED)
        ui.set_rect(self.footer, anchor_min=(0, 1), anchor_max=(0, 1), pivot=(0, 1), position=(PAD, -12),
                    size=(WIDTH - 2 * PAD, FOOTER - 24))
        ui.set_text_align(self.footer, sk.TEXT_ALIGN_LEFT, sk.TEXT_ALIGN_TOP)
        ui.set_text_wrap(self.footer, True)
        self.footer_text = IDLE_FOOTER

        width = WIDTH - 2 * PAD - SCROLLBAR
        self.items = []
        self.hints = {}          # widget: footer text
        for item in items:
            entry = dict(item)
            if "heading" in item:
                row = self._row(content, width, 40)
                text = ui.create_text(row, item["heading"].upper(), size=14, color=GOLD)
                self._place(text, 0, 12, width, 22)
                ui.set_text_align(text, sk.TEXT_ALIGN_LEFT, sk.TEXT_ALIGN_MIDDLE)
                line = ui.create_panel(row, size=(width, 1), color=RULE)
                self._place(line, 0, 36, width, 1)
            elif "slider" in item:
                row = self._row(content, width, 52)
                entry["label"] = ui.create_text(row, item["slider"], size=15, color=TEXT)
                self._place(entry["label"], 0, 2, width, 22)
                ui.set_text_align(entry["label"], sk.TEXT_ALIGN_LEFT, sk.TEXT_ALIGN_MIDDLE)
                self._caps(row, item, measure.width(item["slider"], 15) + 10, 2)
                entry["value"] = ui.create_text(row, "", size=15, color=VALUE)
                self._place(entry["value"], 0, 2, width, 22)
                ui.set_text_align(entry["value"], sk.TEXT_ALIGN_RIGHT, sk.TEXT_ALIGN_MIDDLE)
                entry["widget"] = ui.create_slider(row, min=item["min"], max=item["max"], value=item["get"](),
                                                   size=(width, 18))
                self._place(entry["widget"], 0, 28, width, 18)
                for part, colour in (("track", TRACK), ("fill", FILL), ("handle", HANDLE)):
                    ui.set_widget_part_color(entry["widget"], part, colour)
                self._label(entry)
            elif "toggle" in item:
                # The toggle spans the row so all of it takes the pointer; its label is the row's own
                # text, in the sliders' size.
                row = self._row(content, width, 36)
                caps = self._caps_width(item)
                entry["widget"] = ui.create_toggle(row, "", is_on=item["get"](), size=(width - caps, 28))
                self._place(entry["widget"], 0, 4, width - caps, 28)
                ui.set_widget_part_color(entry["widget"], "fill", FILL)
                label = ui.create_text(row, item["toggle"], size=15, color=TEXT)
                self._place(label, 34, 4, width - caps - 34, 28)
                ui.set_text_align(label, sk.TEXT_ALIGN_LEFT, sk.TEXT_ALIGN_MIDDLE)
                self._caps(row, item, width - caps + 8, 4 + (28 - CAP_HEIGHT) / 2)
            elif "button" in item:
                row = self._row(content, width, 44)
                caps = self._caps_width(item)
                entry["widget"] = ui.create_button(row, item["button"], size=(width - caps, 34))
                self._place(entry["widget"], 0, 5, width - caps, 34)
                ui.set_widget_part_color(entry["widget"], "surface", SURFACE)
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
        footer = self.hints.get(ui.hovered_element, IDLE_FOOTER)
        if footer != self.footer_text:
            ui.set_text(self.footer, footer)
            self.footer_text = footer

    @property
    def pointer_over(self):
        return self.visible and self.ui.pointer_over_ui

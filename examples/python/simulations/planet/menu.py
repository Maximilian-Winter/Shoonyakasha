"""
A settings menu on a screen canvas: a column of headings, sliders,
toggles and buttons down the right side of the screen, shown and hidden as
a whole.

Each item is a dict:
    {"heading": "Simulation"}
    {"slider": "Exposure", "min": 0.05, "max": 2.0, "get": f, "set": g, "format": "%.2f"}
        (or "show": a function from the value to the text shown, in place of "format")
    {"toggle": "Ocean", "get": f, "set": g}
    {"button": "New seed", "do": f}
`get()` reads the setting and `set(value)` changes it; `do()` acts. Every
`update()` hands the widgets the pointer changed to their setters, and
moves the others to what their getters say, so changes made elsewhere (by
keys) show in the menu.

The widgets stay out of keyboard and gamepad navigation, so Tab and the
arrow keys keep their meaning in the program.
"""

import shoonyakasha as sk

WIDTH = 380
ROW = 30
PANEL = (0.05, 0.06, 0.08, 0.86)
HEADING = (0.95, 0.85, 0.55, 1.0)
TEXT = (0.88, 0.9, 0.92, 1.0)


class SettingsMenu:
    def __init__(self, engine, title, items, reference_size=(1600, 900)):
        self.engine = engine
        ui = engine.ui
        self.canvas = ui.create_canvas(reference_size=reference_size)
        ui.set_canvas_sort_order(self.canvas, 10)
        panel = ui.create_panel(self.canvas, size=(WIDTH, 100), color=PANEL)
        ui.set_rect(panel, anchor_min=(1, 0), anchor_max=(1, 1), pivot=(1, 0), position=(-16, 16), size=(WIDTH, -32))
        column = ui.create_vertical_layout(panel, size=(WIDTH - 32, 100), spacing=4, padding=(16, 12, 16, 12))
        ui.set_rect(column, anchor_min=(0, 0), anchor_max=(1, 1), size=(0, 0))

        heading = ui.create_text(column, title, size=24, color=HEADING)
        ui.set_size(heading, (WIDTH - 32, 34))
        self.items = []
        for item in items:
            entry = dict(item)
            if "heading" in item:
                text = ui.create_text(column, item["heading"], size=20, color=HEADING)
                ui.set_size(text, (WIDTH - 32, ROW + 4))
            elif "slider" in item:
                entry["label"] = ui.create_text(column, "", size=17, color=TEXT)
                ui.set_size(entry["label"], (WIDTH - 32, 22))
                entry["widget"] = ui.create_slider(column, min=item["min"], max=item["max"], value=item["get"](),
                                                   size=(WIDTH - 32, 20))
                self._label(entry)
            elif "toggle" in item:
                entry["widget"] = ui.create_toggle(column, item["toggle"], is_on=item["get"](), size=(WIDTH - 32, ROW))
            elif "button" in item:
                entry["widget"] = ui.create_button(column, item["button"], size=(WIDTH - 32, 36))
            if "widget" in entry:
                ui.set_navigable(entry["widget"], False)
            self.items.append(entry)
        self.visible = True
        self.show(False)

    def _label(self, entry):
        value = entry["get"]()
        shown = entry["show"](value) if "show" in entry else entry.get("format", "%.2f") % value
        self.engine.ui.set_text(entry["label"], "%s    %s" % (entry["slider"], shown))

    def show(self, on):
        self.visible = on
        self.engine.ui.set_visible(self.canvas, on)

    def update(self):
        if not self.visible:
            return
        ui = self.engine.ui
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

    @property
    def pointer_over(self):
        return self.visible and self.engine.ui.pointer_over_ui

"""Retained canvas components; no callbacks are installed on the engine here."""
from dataclasses import dataclass
import math
from .state import Value, Parameter


@dataclass(frozen=True)
class Theme:
    background: tuple = (0.055, 0.065, 0.085, 1.0)
    surface: tuple = (0.12, 0.14, 0.18, 1.0)
    text: tuple = (0.90, 0.92, 0.96, 1.0)
    muted: tuple = (0.57, 0.63, 0.73, 1.0)
    accent: tuple = (0.28, 0.70, 0.90, 1.0)
    track: tuple = (0.07, 0.09, 0.12, 1.0)
    error: tuple = (1.0, 0.40, 0.38, 1.0)
    normal: tuple = (1.0, 1.0, 1.0, 1.0)
    hover: tuple = (1.18, 1.18, 1.18, 1.0)
    pressed: tuple = (0.80, 0.80, 0.80, 1.0)
    focused: tuple = (1.10, 1.25, 1.45, 1.0)
    disabled: tuple = (0.65, 0.65, 0.65, 0.55)
    font: int = 0
    font_size: float = 18.0
    spacing: float = 10.0
    padding: float = 16.0
    control_height: float = 36.0


class Component:
    native_widget = False

    def __init__(self, context, parent=None, *, entity=None, size=(240, 36)):
        self.context, self.ui = context, context.ui
        self.parent = parent if isinstance(parent, Component) else None
        target = parent.content if self.parent else (context.root if parent is None else parent)
        self.entity = entity if entity is not None else self.ui.create_element(target, size=size)
        self.content = self.entity
        self._visible, self._enabled, self.disposed = True, True, False
        self._children, self._subscriptions, self._cache = [], [], {}
        if self.parent:
            self.parent._children.append(self)
        context._components.append(self)

    @staticmethod
    def target(context, parent):
        return parent.content if isinstance(parent, Component) else (context.root if parent is None else parent)

    def _changed(self, name, value, callback):
        if self._cache.get(name, object()) != value:
            self._cache[name] = value
            callback()

    def place(self, x, y, width=None, height=None, anchor=(0, 0)):
        self.ui.set_anchor(self.entity, anchor)
        self.ui.set_position(self.entity, (x, y))
        if width is not None and height is not None:
            self.ui.set_size(self.entity, (width, height))
        return self

    @property
    def visible(self):
        return self._visible

    @visible.setter
    def visible(self, value):
        self._visible = bool(value)
        self._changed("visible", self._visible, lambda: self.ui.set_visible(self.entity, self._visible))

    @property
    def enabled(self):
        return self._enabled

    @enabled.setter
    def enabled(self, value):
        self._enabled = bool(value)
        if self.native_widget:
            self._apply_enabled(self._enabled)

    def _active(self):
        return not self.disposed and self.visible and self.enabled and (self.parent is None or self.parent._active())

    def _apply_enabled(self, enabled):
        self._changed("enabled", enabled, lambda: self.ui.set_interactable(self.entity, enabled))

    def _poll(self, dt):
        pass

    def style(self):
        pass

    def subscribe(self, value, callback, *, immediate=True):
        handle = value.subscribe(callback, immediate=immediate)
        self._subscriptions.append(handle)
        return handle

    def dispose(self):
        if self.disposed:
            return
        self.disposed = True
        for child in tuple(self._children):
            child.dispose()
        for unsubscribe in self._subscriptions:
            unsubscribe()
        self._subscriptions.clear()
        if self.context.engine.scene.is_valid(self.entity):
            self.context.engine.scene.destroy_entity(self.entity)
        self.context._components.remove(self)
        if self.parent:
            self.parent._children.remove(self)


class Panel(Component):
    def __init__(self, context, parent=None, *, size=(320, 240)):
        entity = context.ui.create_panel(self.target(context, parent), size=size)
        super().__init__(context, parent, entity=entity)
        self.style()

    def style(self):
        self.ui.set_color(self.entity, self.context.theme.surface)


class Column(Component):
    method = "create_vertical_layout"

    def __init__(self, context, parent=None, *, size=(300, 400), spacing=None, padding=None):
        theme = context.theme
        padding = theme.padding if padding is None else padding
        padding = (padding,) * 4 if isinstance(padding, (int, float)) else padding
        entity = getattr(context.ui, self.method)(self.target(context, parent), size=size,
                    spacing=theme.spacing if spacing is None else spacing, padding=padding)
        super().__init__(context, parent, entity=entity)
        self.ui.set_layout_child_sizing(entity, expand_width=self.method == "create_vertical_layout")


class Row(Column):
    method = "create_horizontal_layout"


class Grid(Component):
    def __init__(self, context, parent=None, *, size=(400, 300), cell_size=(120, 40), columns=0):
        entity = context.ui.create_grid_layout(self.target(context, parent), size=size,
                    cell_size=cell_size, columns=columns, spacing=(context.theme.spacing,) * 2)
        super().__init__(context, parent, entity=entity)


class ScrollPanel(Component):
    def __init__(self, context, parent=None, *, size=(320, 400), horizontal=False, vertical=True):
        entity = context.ui.create_scroll_view(self.target(context, parent), size=size,
                                              horizontal=horizontal, vertical=vertical)
        super().__init__(context, parent, entity=entity)
        self.content = self.ui.scroll_content(entity)


class Label(Component):
    def __init__(self, context, parent=None, text="", *, size=(240, 30), font_size=None, muted=False):
        self._text = str(text)
        self.font_size, self.muted = font_size, muted
        self._color = None
        entity = context.ui.create_text(self.target(context, parent), self._text,
                                        size=font_size or context.theme.font_size)
        super().__init__(context, parent, entity=entity)
        self.ui.set_size(entity, size)
        self._cache["text"] = self._text
        self.style()

    @property
    def text(self):
        return self._text

    @text.setter
    def text(self, text):
        self._text = str(text)
        self._changed("text", self._text, lambda: self.ui.set_text(self.entity, self._text))

    @property
    def color(self):
        return self._color or (self.context.theme.muted if self.muted else self.context.theme.text)

    @color.setter
    def color(self, color):
        self._color = tuple(color)
        self._changed("color", self._color, lambda: self.ui.set_color(self.entity, self._color))

    def style(self):
        t = self.context.theme
        self.ui.set_color(self.entity, self.color)
        self.ui.set_font(self.entity, t.font or self.ui.default_font)
        self.ui.set_font_size(self.entity, self.font_size or t.font_size)


class Widget(Component):
    native_widget = True

    def style(self):
        t = self.context.theme
        for part, color in (("surface", t.surface), ("text", t.text), ("track", t.track),
                            ("fill", t.accent), ("handle", t.text)):
            self.ui.set_widget_part_color(self.entity, part, color)
        self.ui.set_interaction_colors(self.entity, t.normal, t.hover, t.pressed, t.focused, t.disabled)
        # Native text fields and simple buttons expose font setters at the widget root.
        self.ui.set_font_size(self.entity, t.font_size)
        self.ui.set_font(self.entity, t.font or self.ui.default_font)


class Button(Widget):
    def __init__(self, context, parent=None, text="Button", *, on_click=None, size=None, allow_when_locked=False):
        size = size or (160, context.theme.control_height)
        self.on_click = on_click
        self.allow_when_locked = allow_when_locked
        entity = context.ui.create_button(self.target(context, parent), text, size=size)
        super().__init__(context, parent, entity=entity)
        self.style()

    def _poll(self, dt):
        if self.ui.was_clicked(self.entity) and self.on_click:
            self.on_click()


class Toggle(Widget):
    def __init__(self, context, parent=None, text="", *, value=None, size=None):
        size = size or (240, context.theme.control_height)
        self.value = value if value is not None else Value(False)
        entity = context.ui.create_toggle(self.target(context, parent), text, is_on=self.value.value, size=size)
        super().__init__(context, parent, entity=entity)
        self.subscribe(self.value, lambda data: self.ui.set_toggle_value(self.entity, data))
        self.style()

    def _poll(self, dt):
        if self.ui.value_changed(self.entity) and not self.value.read_only:
            self.value.value = self.ui.toggle_value(self.entity)


class Slider(Widget):
    def __init__(self, context, parent=None, *, value=None, minimum=0.0, maximum=1.0, size=None):
        size = size or (240, context.theme.control_height)
        self.value = value if value is not None else Value(minimum)
        minimum = getattr(self.value, "minimum", None) if getattr(self.value, "minimum", None) is not None else minimum
        maximum = getattr(self.value, "maximum", None) if getattr(self.value, "maximum", None) is not None else maximum
        self.minimum, self.maximum = minimum, maximum
        entity = context.ui.create_slider(self.target(context, parent), min=minimum, max=maximum,
                                          value=self.value.value, size=size)
        super().__init__(context, parent, entity=entity)
        self.subscribe(self.value, lambda data: self.ui.set_slider_value(self.entity, data))
        self.style()

    def _poll(self, dt):
        if self.ui.value_changed(self.entity) and not self.value.read_only:
            data = self.ui.slider_value(self.entity)
            step = getattr(self.value, "step", None)
            if step:
                data = self.minimum + round((data - self.minimum) / step) * step
                last = self.minimum + math.floor((self.maximum - self.minimum) / step + 1e-9) * step
                data = max(self.minimum, min(last, data))
            if getattr(self.value, "type", None) is int:
                data = round(data)
            self.value.value = data


class TextField(Widget):
    def __init__(self, context, parent=None, *, value=None, placeholder="", size=None):
        size = size or (240, context.theme.control_height)
        self.value = value if value is not None else Value("")
        entity = context.ui.create_text_field(self.target(context, parent), placeholder=placeholder, size=size)
        super().__init__(context, parent, entity=entity)
        self.subscribe(self.value, lambda data: self.ui.set_text(self.entity, str(data)))
        self.style()

    def _poll(self, dt):
        if self.ui.text_changed(self.entity) and not self.value.read_only:
            self.value.value = self.ui.get_text(self.entity)


class NumericField(Widget):
    def __init__(self, context, parent=None, *, value, size=(240, 60)):
        self.value, self.error = value, ""
        super().__init__(context, parent, size=size)
        self.field = context.ui.create_text_field(self.entity, size=(size[0], 34))
        self.ui.set_anchor(self.field, (0, 0))
        self.message = Label(context, self, size=(size[0], 22), font_size=13)
        self.message.place(0, 36)
        self.subscribe(value, self._sync)
        self.style()

    def _sync(self, data):
        format = getattr(self.value, "format", None)
        text = format(data) if callable(format) else (format.format(data) if format else str(data))
        self._changed("text", text, lambda: self.ui.set_text(self.field, text))
        self.error = ""
        self.message.text = ""

    def style(self):
        t = self.context.theme
        self.ui.set_widget_part_color(self.field, "surface", t.surface)
        self.ui.set_widget_part_color(self.field, "text", t.text)
        self.ui.set_font(self.field, t.font or self.ui.default_font)
        self.ui.set_font_size(self.field, t.font_size)
        self.ui.set_interaction_colors(self.field, t.normal, t.hover, t.pressed, t.focused, t.disabled)
        self.message.color = t.error

    def _apply_enabled(self, enabled):
        self._changed("enabled", enabled, lambda: self.ui.set_interactable(self.field, enabled))
        self._changed("read_only", not enabled,
                      lambda: self.ui.set_text_field_options(self.field, read_only=not enabled))

    def commit(self, text):
        try:
            data = float(text)
            if not math.isfinite(data):
                raise ValueError("Enter a finite number")
            if getattr(self.value, "type", None) is int:
                if not math.isfinite(data) or int(data) != data:
                    raise ValueError("Enter a whole number")
                data = int(data)
            self.value.value = data
            self.error = ""
        except (ValueError, OverflowError) as error:
            self.error = str(error)
        self.message.text = self.error
        return not self.error

    def _poll(self, dt):
        if self.ui.text_submitted(self.field) and not self.value.read_only:
            self.commit(self.ui.get_text(self.field))


class ChoiceSelector(Row):
    def __init__(self, context, parent=None, *, value, choices=None, size=(280, 38)):
        super().__init__(context, parent, size=size, padding=0)
        self.value = value
        self.choices = tuple(choices or getattr(value, "choices", ()))
        if not self.choices:
            raise ValueError("choice selector requires choices")
        self.label = Label(context, self, size=(size[0] - 100, size[1]))
        Button(context, self, "<", size=(40, size[1]), on_click=lambda: self.select(-1))
        Button(context, self, ">", size=(40, size[1]), on_click=lambda: self.select(1))
        self.subscribe(value, lambda data: setattr(self.label, "text", data))

    def select(self, direction):
        if not self.value.read_only:
            self.value.value = self.choices[(self.choices.index(self.value.value) + direction) % len(self.choices)]


class Section(Column):
    def __init__(self, context, parent=None, title="", *, size=(300, 300)):
        super().__init__(context, parent, size=size)
        Label(context, self, title, font_size=24)


class ParameterRow(Column):
    def __init__(self, context, parent=None, *, parameter, size=(300, 104)):
        super().__init__(context, parent, size=size, padding=0, spacing=4)
        self.value = parameter
        self.label = Label(context, self, size=(size[0], 24))
        self.subscribe(parameter, lambda _: setattr(self.label, "text", parameter.name + ": " + parameter.display()))
        if parameter.type is bool:
            self.control = Toggle(context, self, value=parameter)
        elif parameter.type is str:
            self.control = ChoiceSelector(context, self, value=parameter)
        else:
            self.control = NumericField(context, self, value=parameter, size=(size[0], 60))
            if parameter.minimum is not None and parameter.maximum is not None:
                # Numeric editor plus slider, both bound to the same parameter.
                self.ui.set_size(self.entity, (size[0], 138))
                Slider(context, self, value=parameter)


class Tabs(Column):
    def __init__(self, context, parent=None, *, size=(500, 400)):
        super().__init__(context, parent, size=size, padding=0)
        self.bar = Row(context, self, size=(size[0], 38), padding=0)
        self.pages = []
        self.page_size = (size[0], size[1] - 48)
        self.selected = -1

    def add(self, title):
        index = len(self.pages)
        Button(self.context, self.bar, title, on_click=lambda: self.select(index))
        page = Column(self.context, self, size=self.page_size)
        self.pages.append(page)
        page.visible = False
        if index == 0:
            self.select(0)
        return page

    def select(self, index):
        if not 0 <= index < len(self.pages):
            raise IndexError(index)
        self.selected = index
        for i, page in enumerate(self.pages):
            page.visible = i == index


class Modal(Panel):
    def __init__(self, context, *, title="", message="", size=(480, 220)):
        # Dedicated canvas blocks scene input and draws above regular screen UI.
        self.canvas = context.ui.create_canvas(sort_order=1000)
        self.shade = context.ui.create_panel(self.canvas, size=(1920, 1080), color=(0, 0, 0, 0.6))
        context.ui.set_rect(self.shade, anchor_min=(0, 0), anchor_max=(1, 1), size=(0, 0))
        super().__init__(context, self.shade, size=size)
        column = Column(context, self, size=size)
        Label(context, column, title, font_size=24, size=(size[0] - 32, 32))
        Label(context, column, message, size=(size[0] - 32, 70))
        self.close_button = Button(context, column, "Close", on_click=self.close)
        self.ui.set_visible(self.canvas, False)
        self._visible = False
        self._previous_focus = None

    def open(self):
        if self.context.modal and self.context.modal is not self:
            self.context.modal.close()
        self._previous_focus = self.ui.focused_element
        self.context.modal = self
        self._visible = True
        self.ui.set_visible(self.canvas, True)
        self.ui.focus(self.close_button.entity)

    def close(self):
        self._visible = False
        self.ui.set_visible(self.canvas, False)
        if self.context.modal is self:
            self.context.modal = None
        if self._previous_focus is not None:
            self.ui.focus(self._previous_focus)

    def dispose(self):
        if self.disposed:
            return
        self.close()
        super().dispose()
        if self.context.engine.scene.is_valid(self.canvas):
            self.context.engine.scene.destroy_entity(self.canvas)


class Tooltip(Label):
    def __init__(self, context, target, text, *, delay=0.5):
        super().__init__(context, text=text, size=(300, 48), font_size=15)
        self.target, self.delay, self.elapsed = target, delay, 0.0
        self.visible = False

    def _poll(self, dt):
        if self.target._active() and self.ui.is_hovered(self.target.entity):
            self.elapsed += dt
            self.visible = self.elapsed >= self.delay
            x, y, width, height = self.ui.get_rect(self.target.entity)
            self.place(x, y + height + 8)
        else:
            self.elapsed = 0.0
            self.visible = False


class Notification(Label):
    def __init__(self, context, text, *, seconds=3.0):
        super().__init__(context, text=text, size=(500, 48))
        self.remaining = seconds
        self.place(24, -72, anchor=(0, 1))

    def _poll(self, dt):
        self.remaining -= dt
        if self.remaining <= 0:
            self.dispose()


class StatusReadout(Label):
    def __init__(self, context, parent=None, *, getter, interval=0.1, size=(300, 30)):
        super().__init__(context, parent, size=size, muted=True)
        self.getter, self.interval, self.elapsed = getter, interval, interval

    def _poll(self, dt):
        self.elapsed += dt
        if self.elapsed >= self.interval:
            self.elapsed = 0
            self.text = self.getter()


class PlaybackControls(Column):
    def __init__(self, context, parent=None, *, playback, size=(400, 120)):
        super().__init__(context, parent, size=size, padding=0)
        self.playback = playback
        row = Row(context, self, size=(size[0], 36), padding=0)
        Button(context, row, "Play / pause", size=(130, 36), on_click=self.toggle)
        Button(context, row, "Step", size=(70, 36), on_click=playback.single_step)
        Button(context, row, "Reset", size=(80, 36), on_click=playback.reset)
        self.seek_value = Value(0.0)
        if math.isfinite(playback.duration):
            self.seek_slider = Slider(context, self, value=self.seek_value, maximum=playback.duration, size=(size[0], 28))
            self.subscribe(self.seek_value, lambda time: playback.seek(time), immediate=False)
        self.speed_value = Parameter("Playback speed", playback.clock.speed, minimum=0.0, maximum=8.0)
        self.speed_field = NumericField(context, self, value=self.speed_value, size=(size[0], 54))
        self.subscribe(self.speed_value, lambda speed: setattr(playback.clock, "speed", speed), immediate=False)
        self.status = StatusReadout(context, self, getter=lambda: (
            "Seeking %.0f%%" % (100 * playback.progress) if playback.seeking else "%.2f s" % playback.time))
        self.ui.set_size(self.entity, (size[0], 176))

    def toggle(self):
        self.playback.pause() if self.playback.playing else self.playback.play()

    def _poll(self, dt):
        if hasattr(self, "seek_slider") and not self.ui.is_pressed(self.seek_slider.entity) and not self.ui.value_changed(self.seek_slider.entity):
            # Synchronize the display without producing a user seek event.
            time = self.playback.time
            self.seek_value._value = time
            self._changed("position", time, lambda: self.ui.set_slider_value(self.seek_slider.entity, time))

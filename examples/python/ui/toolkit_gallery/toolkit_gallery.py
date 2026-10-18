"""Run with the shoonyakasha package installed, like the other examples."""
import argparse
from dataclasses import replace
import math
from pathlib import Path
import shoonyakasha as sk
from shoonyakasha.toolkit.app import Application, Context, Screen
from shoonyakasha.toolkit import ui
from shoonyakasha.toolkit.state import Parameter, Value
from shoonyakasha.toolkit.plots import Plot, Series
from shoonyakasha.toolkit.drawing import Drawing


class Gallery(Screen):
    def __init__(self, app):
        self.app = app
        self.frames, self.time = 0, 0.0

    def mount(self, context):
        self.context = context
        ui.Label(context, text="SHOONYAKASHA / TOOLKIT", size=(1000, 60), font_size=40).place(32, 24)
        ui.Label(context, text="Compose once. Explore in real time.", size=(900, 36), muted=True).place(32, 88)
        tabs = self.tabs = ui.Tabs(context, size=(470, 810)).place(32, 152)
        controls = tabs.add("Controls")
        details = tabs.add("More")
        self.gain = Parameter("Gain", 1.0, minimum=0.0, maximum=2.0, step=0.05, format="{:.2f}")
        ui.ParameterRow(context, controls, parameter=self.gain, size=(420, 138))
        self.motion = Value(True)
        ui.Toggle(context, controls, "Animate plot", value=self.motion)
        ui.TextField(context, controls, placeholder="Write a note...")
        choice = Parameter("Quality", "Medium", choices=("Low", "Medium", "High"))
        ui.ParameterRow(context, controls, parameter=choice)
        self.modal = ui.Modal(context, title="A composable modal", message="Keyboard focus returns to the previous control when closed.")
        button = ui.Button(context, controls, "Open modal", on_click=self.modal.open, size=(300, 36))
        ui.Tooltip(context, button, "Native keyboard navigation and modal focus")
        ui.Button(context, controls, "Notify", on_click=lambda: ui.Notification(context, "Components work with existing engine callbacks."))
        ui.Button(context, controls, "Change theme", on_click=self.theme)
        self.light = False
        ui.StatusReadout(context, controls, getter=lambda: "Gain %.2f | %d retained components" %
                         (self.gain.value, len(context._components)), size=(420, 32))
        scroll = ui.ScrollPanel(context, details, size=(420, 550))
        ui.Label(context, scroll, "Unicode: Ω, العربية, नमस्ते", size=(390, 34))
        for i in range(14):
            ui.Button(context, scroll, "Reusable row %02d" % (i + 1), size=(350, 36))
        self.plot = Plot(context, title="Live signal / bounded history", size=(750, 420), y_range=(-2.2, 2.2)).place(550, 170)
        self.signal = self.plot.add(Series("signal", capacity=1000))
        self.reference = self.plot.add(Series("reference", color=(0.95, 0.65, 0.35, 1), capacity=1000))
        self.drawing = Drawing(context, size=(750, 250)).place(550, 650)
        with self.drawing.redraw() as d:
            d.rectangle((0, 0), (720, 210), context.theme.surface)
            d.polyline(((30, 140), (120, 70), (210, 140), (300, 70)), context.theme.accent, 3, dash=10)
            d.arrow((350, 140), (470, 70), color=(0.95, 0.65, 0.35, 1), width=4)
            d.circle((590, 105), 52, context.theme.accent, filled=False, width=3)
        ui.Label(context, text="Retained triangles. No generated PNGs.", size=(750, 32), muted=True).place(550, 900)

        # A separate context on an existing world canvas uses explicit update forwarding.
        self.world_canvas = context.ui.create_world_canvas((560, 340), (2.2, 1.4))
        context.engine.scene.set_position(self.world_canvas, (3.4, 0.0, 0.0))
        self.world = Context(context.engine, parent=self.world_canvas)
        column = ui.Column(self.world, size=(540, 320)).place(10, 10)
        ui.Label(self.world, column, "WORLD CANVAS", size=(500, 48), font_size=32)
        ui.Label(self.world, column, "The same Python components", size=(500, 36))
        self.world_count = Value(0)
        label = ui.Label(self.world, column, size=(500, 36))
        label.subscribe(self.world_count, lambda count: setattr(label, "text", "%d clicks" % count))
        ui.Button(self.world, column, "Click in 3D", size=(400, 54),
                  on_click=lambda: setattr(self.world_count, "value", self.world_count.value + 1))

    def theme(self):
        self.light = not self.light
        self.context.apply_theme(replace(ui.Theme(), surface=(0.24, 0.18, 0.12, 1),
                                        accent=(0.95, 0.65, 0.35, 1)) if self.light else ui.Theme())

    def update(self, dt):
        self.frames += 1
        self.world.update(dt)
        if self.motion.value:
            self.time += dt
            self.signal.append(self.time, self.gain.value * math.sin(self.time * 3))
            self.reference.append(self.time, math.cos(self.time * 2) * 0.6)
        self.plot.x_range = (max(0, self.time - 8), max(8, self.time))
        self.plot.refresh()
        if self.app.args.smoke:
            if self.frames == 8:
                self.gain.value = 1.5
                self.modal.open()
            if self.frames == 14:
                self.modal.close()
                self.theme()
                self.tabs.select(1)
            if self.frames == 20:
                self.tabs.select(0)

    def unmount(self):
        self.world.dispose()
        self.context.engine.scene.destroy_entity(self.world_canvas)


class GalleryApp(Application):
    def __init__(self, args):
        self.args = args
        super().__init__(Gallery(self), title="Shoonyakasha Toolkit Gallery", width=args.width, height=args.height,
                         log_file="toolkit_gallery.log", environment_color=(0.0, 0.0, 0.0), enable_validation=args.smoke)

    def on_init(self):
        camera = self.engine.create_camera((0, 0.3, 5), speed=0)
        self.engine.scene.set_rotation(camera, sk.mathutil.look_rotation((0, 0.3, 5), (0, 0, 0)))

    def on_post_init(self):
        self.engine.apply_pipeline_preset("low")

    def on_post_render(self):
        if self.screen.frames == 60 and self.args.screenshot:
            if not self.engine.capture_screenshot(str(Path(self.args.screenshot).resolve())):
                raise RuntimeError("gallery screenshot failed")
        if self.args.smoke and self.screen.frames >= 65:
            self.engine.request_quit()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--screenshot")
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    GalleryApp(parser.parse_args()).run()

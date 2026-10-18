"""A driven oscillator: interactive lesson and frame-confirmed video export."""
import argparse
from collections import deque
import math
from pathlib import Path
from shoonyakasha.toolkit.app import Application, Screen
from shoonyakasha.toolkit import ui
from shoonyakasha.toolkit.state import Parameter, ParameterSet
from shoonyakasha.toolkit.timeline import Binding, Timeline, Track
from shoonyakasha.toolkit.playback import Playback
from shoonyakasha.toolkit.lessons import Lesson, Chapter
from shoonyakasha.toolkit.plots import Plot, Series
from shoonyakasha.toolkit.drawing import Drawing
from shoonyakasha.toolkit.capture import Exporter, ExportOptions


class Oscillator:
    """RK4 integration of m*x'' + damping*x' + stiffness*x = sin(phase)."""
    def __init__(self, parameters):
        self.parameters = parameters
        self.reset(0)

    def reset(self, seed):
        self.x = self.velocity = self.time = self.phase = 0.0
        self.history = deque([(0.0, 0.0)], maxlen=4096)
        self.revision = getattr(self, "revision", 0) + 1

    def step(self, dt):
        p = self.parameters
        m, k, damping = p["Mass"].value, p["Stiffness"].value, p["Damping"].value
        omega = 2 * math.pi * p["Driving frequency"].value

        def f(x, v, t):
            return v, (math.sin(self.phase + omega * t) - damping * v - k * x) / m

        x, v = self.x, self.velocity
        a, b = f(x, v, 0)
        c, d = f(x + a * dt / 2, v + b * dt / 2, dt / 2)
        e, g = f(x + c * dt / 2, v + d * dt / 2, dt / 2)
        h, i = f(x + e * dt, v + g * dt, dt)
        self.x += dt * (a + 2 * c + 2 * e + h) / 6
        self.velocity += dt * (b + 2 * d + 2 * g + i) / 6
        self.time += dt
        self.phase += omega * dt
        self.history.append((self.time, self.x))
        self.revision += 1


class OscillatorScreen(Screen):
    def __init__(self, app):
        self.app, self.frames = app, 0

    def mount(self, context):
        self.context = context
        background = ui.Panel(context, size=(1920, 1080)).place(0, 0)
        context.ui.set_color(background.entity, context.theme.background)
        self.title = ui.Label(context, text="DRIVEN OSCILLATOR", size=(1700, 70), font_size=42).place(40, 25)
        self.caption = ui.Label(context, text="", size=(1700, 52), font_size=23).place(40, 100)
        self.parameters = ParameterSet(
            Parameter("Mass", 1.0, minimum=0.2, maximum=3.0, step=0.1, unit="kg", format="{:.1f}"),
            Parameter("Stiffness", 20.0, minimum=1.0, maximum=40.0, step=0.5, unit="N/m", format="{:.1f}"),
            Parameter("Damping", 0.15, minimum=0.0, maximum=2.0, unit="N s/m", format="{:.2f}"),
            Parameter("Driving frequency", 0.25, minimum=0.1, maximum=2.0, unit="Hz", format="{:.2f}"))
        controls = ui.Column(context, size=(390, 860), padding=0, spacing=6).place(40, 180)
        for p in self.parameters:
            ui.ParameterRow(context, controls, parameter=p, size=(360, 138))
        frequency = self.parameters["Driving frequency"]
        damping = self.parameters["Damping"]
        self.lesson = Lesson(
            Chapter("Sweep through resonance", 12.0,
                    Timeline(Track(Binding.value(frequency), [(0, 0.25), (12, 1.15)])),
                    "The response grows as the drive approaches the natural frequency."),
            Chapter("Damping limits the response", 12.0,
                    Timeline(Track(Binding.value(frequency), [(0, 0.71), (12, 0.71)]),
                             Track(Binding.value(damping), [(0, 0.15), (12, 1.8)])),
                    "Increasing damping removes energy and reduces the oscillation."),
            title=self.title, caption=self.caption)
        self.model = Oscillator(self.parameters)
        self.app.playback = self.playback = Playback(self.model, parameters=self.parameters, timeline=self.lesson.timeline)
        ui.PlaybackControls(context, controls, playback=self.playback, size=(370, 176))
        row = ui.Row(context, controls, size=(370, 36), padding=0)
        ui.Button(context, row, "Previous", on_click=lambda: self.lesson.previous(self.playback))
        ui.Button(context, row, "Next", on_click=lambda: self.lesson.next(self.playback))
        ui.Button(context, controls, "Cancel seek / export", size=(320, 36), on_click=self.cancel, allow_when_locked=True)
        self.plot = Plot(context, title="Displacement / metres", size=(1370, 440), y_range=(-0.65, 0.65)).place(480, 180)
        self.series = self.plot.add(Series("x(t)", capacity=4096))
        self.diagram = Drawing(context, size=(1330, 280)).place(500, 680)
        ui.Label(context, text="Spring force", size=(200, 30)).place(500, 970)
        ui.Label(context, text="Driving force", size=(200, 30)).place(900, 970)
        ui.StatusReadout(context, getter=lambda: "Natural frequency: %.3f Hz | RK4, 120 steps/s" %
                        (math.sqrt(self.parameters["Stiffness"].value / self.parameters["Mass"].value) / (2 * math.pi)),
                        size=(900, 34)).place(500, 1020)
        self.app.exporter = Exporter(context.engine, self.playback, context=context)
        self.last_revision = -1
        self.playback.play()

    def cancel(self):
        self.playback.cancel_seek()
        self.app.exporter.cancel()

    def update(self, dt):
        self.frames += 1
        model, theme = self.model, self.context.theme
        if model.revision != self.last_revision:
            self.series.clear()
            self.series.extend(model.history)
            self.last_revision = model.revision
        self.plot.x_range = (max(0, model.time - 12), max(12, model.time))
        self.plot.cursor = model.time
        self.plot.refresh()
        x = 610 + model.x * 400
        with self.diagram.redraw() as d:
            d.line((30, 150), (1250, 150), color=theme.muted, width=2)
            points = [(30, 100)]
            for i in range(25):
                points.append((50 + (x - 100) * i / 24, 100 + (20 if i % 2 else -20)))
            points.append((x - 38, 100))
            d.polyline(points, theme.muted, 3)
            d.rectangle((x - 38, 60), (76, 80), theme.accent)
            spring = -self.parameters["Stiffness"].value * model.x
            d.arrow((100, 220), (100 + spring * 15, 220), color=theme.accent, width=3)
            d.arrow((500, 220), (500 + math.sin(model.phase) * 100, 220), color=(0.95, 0.65, 0.35, 1), width=3)
        if self.app.args.smoke and not self.app.args.video:
            if self.frames == 8:
                self.playback.pause()
                self.playback.single_step()
            if self.frames == 10:
                self.playback.seek(2.0)
            if self.frames == 20:
                self.playback.play()


class OscillatorApp(Application):
    def __init__(self, args):
        self.args = args
        super().__init__(OscillatorScreen(self), title="Shoonyakasha / Resonance", width=args.width,
                         height=args.height, log_file="toolkit_oscillator.log", enable_validation=args.smoke)

    def on_init(self):
        self.engine.create_camera((0, 0, 4), speed=0)

    def on_post_init(self):
        self.engine.apply_pipeline_preset("low")
        if self.args.video:
            self.exporter.start(str(Path(self.args.video).resolve()), duration=self.args.duration,
                options=ExportOptions(width=self.args.width, height=self.args.height,
                                      fps=self.args.fps, quit_when_done=True))

    def on_post_render(self):
        if self.screen.frames == 60 and self.args.screenshot:
            self.exporter.screenshot(str(Path(self.args.screenshot).resolve()))
        if self.args.smoke and not self.args.video and self.screen.frames >= 65:
            self.engine.request_quit()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--screenshot")
    parser.add_argument("--video")
    parser.add_argument("--duration", type=float, default=None)
    parser.add_argument("--fps", type=int, default=30)
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    OscillatorApp(parser.parse_args()).run()

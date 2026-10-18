"""Behavior tests for the toolkit without a Vulkan device."""
import math
import os
from pathlib import Path
import random
import subprocess
import sys
from types import SimpleNamespace
import unittest
import gc
import weakref

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "python"))
from shoonyakasha.toolkit.app import Application, Context, Screen
from shoonyakasha.toolkit import ui
from shoonyakasha.toolkit.state import Value, Parameter, ParameterSet, bind_custom
from shoonyakasha.toolkit.timeline import Binding, Timeline, Track
from shoonyakasha.toolkit.playback import Playback, Clock
from shoonyakasha.toolkit.lessons import Chapter, Lesson
from shoonyakasha.toolkit.drawing import Drawing, Mesh
from shoonyakasha.toolkit.plots import Plot, Series, decimate
from shoonyakasha.toolkit.capture import Exporter, ExportOptions


class FakeEngine:
    def __init__(self):
        self.next_entity = 0
        self.entities, self.parents, self.calls = set(), {}, []
        self.text, self.values = {}, {}
        self.clicks, self.changes = set(), set()
        self.focused_element = 0
        self.default_font = 1
        self.framebuffer_size = (1920, 1080)
        self.rect_ready = True
        self.ui = self.scene = self
        self.is_recording = False
        self.recorded_frame_count = 0
        self.recording_paused = False
        self.quit = self.stops = 0
        self.record_ok = self.stop_ok = True
        self.meshes = []

    def __getattr__(self, name):
        if name.startswith("create_"):
            def create(*args, **kwargs):
                self.next_entity += 1
                entity = self.next_entity
                self.entities.add(entity)
                if name != "create_canvas" and args:
                    self.parents[entity] = args[0]
                if name == "create_text":
                    self.text[entity] = args[1]
                if name == "create_slider":
                    self.values[entity] = kwargs["value"]
                return entity
            return create
        if name.startswith("set_"):
            return lambda *args, **kwargs: self.calls.append((name, args, kwargs))
        raise AttributeError(name)

    def is_valid(self, entity):
        return entity in self.entities

    def destroy_entity(self, entity):
        for child, parent in tuple(self.parents.items()):
            if parent == entity:
                self.destroy_entity(child)
        self.entities.discard(entity)
        self.parents.pop(entity, None)

    def set_text(self, entity, text):
        self.text[entity] = text
        self.calls.append(("set_text", entity, text))

    def get_text(self, entity):
        return self.text.get(entity, "")

    def was_clicked(self, entity):
        return entity in self.clicks

    def value_changed(self, entity):
        return entity in self.changes

    def text_changed(self, entity):
        return entity in self.changes

    def text_submitted(self, entity):
        return entity in self.changes

    def is_hovered(self, entity):
        return entity in self.changes

    def is_pressed(self, entity):
        return False

    def get_rect(self, entity):
        return (0, 0, 600, 320) if self.rect_ready else (0, 0, 0, 0)

    def get_canvas_size(self, entity):
        return 1920, 1080

    def focus(self, entity):
        self.focused_element = entity

    def scroll_content(self, entity):
        return self.create_element(entity)

    def set_geometry(self, entity, points, indices, colors):
        self.meshes.append((entity, points, indices, colors))

    def start_recording(self, path, **kwargs):
        self.is_recording = self.record_ok
        self.recorded_frame_count = 0
        return self.record_ok

    def stop_recording(self):
        self.stops += 1
        self.is_recording = False
        return self.stop_ok

    def request_quit(self):
        self.quit += 1

    def present(self, write=True):
        if write and self.is_recording and not self.recording_paused:
            self.recorded_frame_count += 1

    def capture_screenshot(self, path):
        self.calls.append(("screenshot", path))
        return True


class StateTests(unittest.TestCase):
    def test_subscription_change_suppression_and_idempotent_unsubscribe(self):
        value, seen = Value(1), []
        unsubscribe = value.subscribe(seen.append, immediate=True)
        value.value = 1
        value.value = 2
        unsubscribe()
        unsubscribe()
        value.value = 3
        self.assertEqual(seen, [1, 2])

    def test_parameter_validation_types_bounds_and_step(self):
        p = Parameter("mass", 1.0, minimum=0.5, maximum=3, step=0.5)
        for invalid in (True, "2", math.nan, math.inf, 0.0, 1.2):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                p.value = invalid
        p.value = 2
        self.assertEqual(p.value, 2.0)
        with self.assertRaises(ValueError):
            Parameter("choice", "unknown", choices=("a", "b"))

    def test_transaction_validates_before_notifications(self):
        a, b = Parameter("a", 1), Parameter("b", 2, maximum=3)
        parameters = ParameterSet(a, b)
        observed = []
        a.subscribe(lambda _: observed.append(parameters.values()))
        with self.assertRaises(ValueError):
            parameters.assign({"a": 9, "b": 5})
        self.assertEqual(parameters.values(), {"a": 1, "b": 2})
        parameters.assign({"a": 3, "b": 3})
        self.assertEqual(observed, [{"a": 3, "b": 3}])
        parameters.save_preset("saved")
        parameters.reset()
        parameters.apply_preset("saved")
        self.assertEqual(a.value, 3)

    def test_explicit_custom_binding(self):
        engine, p = FakeEngine(), Parameter("mass", 1.0)
        unsubscribe = bind_custom(p, engine, "osc.mass")
        p.value = 2.0
        unsubscribe()
        self.assertEqual(engine.calls[-1][1], ("osc.mass", 2.0))


class Model:
    def __init__(self, parameter=None):
        self.parameter = parameter

    def reset(self, seed):
        self.rng = random.Random(seed)
        self.x = 0
        self.history = []

    def step(self, dt):
        self.x += dt * (self.parameter.value if self.parameter else 1) + self.rng.random() * dt
        self.history.append(self.x)


class TimeTests(unittest.TestCase):
    def test_tracks_absolute_interpolation_and_conflicts(self):
        v = Value(0.0)
        track = Track(Binding.value(v), [(0, 0.0), (2, 4.0)])
        timeline = Timeline(track)
        timeline.evaluate(1)
        self.assertEqual(v.value, 2)
        timeline.evaluate(0)
        self.assertEqual(v.value, 0)
        with self.assertRaises(ValueError):
            timeline.add(track)
        self.assertEqual(Track(Binding.value(v), [(0, False), (1, True)]).sample(0.5), False)
        self.assertEqual(Track(Binding.value(v), [(0, (0.0, 2.0)), (1, (2.0, 4.0))]).sample(0.5), (1.0, 3.0))
        self.assertAlmostEqual(Track(Binding.value(v), [(0, 0.0), (1, 1.0)], "ease_in_out").sample(0.25), 0.15625)

    def test_backlog_is_retained_and_partition_does_not_change_result(self):
        clock = Clock(step=0.01, max_steps=2)
        self.assertEqual(clock.consume(0.1), 2)
        self.assertAlmostEqual(clock.accumulator, 0.08)
        a, b = Playback(Model(), seed=4), Playback(Model(), seed=4)
        a.play()
        b.play()
        for _ in range(10):
            a.update(0.1)
        for _ in range(100):
            b.update(0.01)
        self.assertEqual(a.clock.tick, 120)
        self.assertEqual(a.simulation.x, b.simulation.x)

    def test_seek_replays_parameters_and_seed_in_batches(self):
        p = Parameter("force", 1.0)
        t = Timeline(Track(Binding.value(p), [(0, 1.0), (1, 3.0)]))
        playback = Playback(Model(p), timeline=t, seed=12, replay_batch=7)
        playback.play()
        for _ in range(10):
            playback.update(0.1)
        expected = playback.simulation.x
        playback.seek(1)
        self.assertTrue(p.read_only)
        playback.update(0)
        self.assertEqual(playback.clock.tick, 7)
        while playback.seeking:
            playback.update(0)
        self.assertEqual(playback.simulation.x, expected)
        self.assertFalse(p.read_only)
        p.value = 9.0
        playback.play()
        self.assertEqual(p.value, 3.0)
        playback.seek(0.8)
        playback.cancel_seek()
        self.assertFalse(playback.seeking)

    def test_lessons_hold_gaps_and_reconstruct_prior_chapters(self):
        v = Value(0.0)
        lesson = Lesson(Chapter("A", 2, Timeline(Track(Binding.value(v), [(0, 0.0), (1, 2.0)]))),
                        Chapter("B", 2),
                        Chapter("C", 2, Timeline(Track(Binding.value(v), [(0, 5.0), (2, 9.0)]))))
        lesson.timeline.evaluate(3)
        self.assertEqual(v.value, 2)
        lesson.timeline.evaluate(5)
        self.assertEqual(v.value, 7)
        playback = Playback(timeline=lesson.timeline)
        lesson.navigate(playback, 2)
        while playback.seeking:
            playback.update(0)
        self.assertEqual(v.value, 5)
        self.assertFalse(playback.playing)
        lesson.previous(playback)
        self.assertEqual(playback._seek_time, 2)


class ComponentTests(unittest.TestCase):
    def setUp(self):
        self.engine = FakeEngine()
        self.context = Context(self.engine)

    def test_ownership_external_canvas_and_unsubscribe(self):
        canvas = self.engine.create_canvas()
        context = Context(self.engine, parent=canvas)
        row = ui.Row(context)
        p = Value(False)
        ui.Toggle(context, row, value=p)
        self.assertEqual(len(p._listeners), 1)
        row.dispose()
        row.dispose()
        self.assertEqual(len(p._listeners), 0)
        context.dispose()
        self.assertTrue(self.engine.is_valid(canvas))

    def test_hidden_disabled_and_modal_events(self):
        seen = []
        button = ui.Button(self.context, on_click=lambda: seen.append(1))
        self.engine.clicks.add(button.entity)
        button.enabled = False
        self.context.update(0)
        button.enabled = True
        self.context.update(0)
        self.assertEqual(seen, [1])
        self.engine.focus(button.entity)
        modal = ui.Modal(self.context, title="Question")
        modal.open()
        status = ui.StatusReadout(self.context, getter=lambda: "Still updating")
        self.context.update(0)
        self.assertEqual(seen, [1])
        self.assertEqual(status.text, "Still updating")
        modal.close()
        self.assertEqual(self.engine.focused_element, button.entity)
        self.context.update(0)
        self.assertEqual(seen, [1, 1])

    def test_numeric_field_invalid_input_preserves_model(self):
        p = Parameter("mass", 1.0, minimum=0.5)
        field = ui.NumericField(self.context, value=p)
        self.assertFalse(field.commit("bad"))
        self.assertEqual(p.value, 1.0)
        self.assertTrue(field.error)
        self.assertFalse(field.commit("nan"))
        self.assertTrue(field.commit("2.5"))
        self.assertEqual(p.value, 2.5)
        self.assertEqual(field.message.text, "")

    def test_drawing_cache_and_no_per_segment_entities(self):
        drawing = Drawing(self.context)
        before = len(self.engine.entities)
        for _ in range(2):
            with drawing.redraw():
                drawing.line((0, 0), (10, 10), dash=2)
                drawing.circle((30, 30), 8)
            self.context.update(0)
        self.assertEqual(len(self.engine.meshes), 1)
        self.assertEqual(len(self.engine.entities), before)
        _, points, indices, colors = self.engine.meshes[0]
        self.assertEqual(len(points), len(colors))
        self.assertLess(max(indices), len(points))
        self.assertTrue(any(c[3] == 0 for c in colors))
        with self.assertRaises(ValueError):
            drawing.line((math.nan, 0), (1, 1))

    def test_geometry_waits_for_layout_and_then_keeps_its_cache(self):
        self.engine.rect_ready = False
        drawing = Drawing(self.context)
        drawing.line((0, 0), (50, 50))
        self.context.update(0)
        self.assertFalse(self.context.ready)
        self.assertEqual(self.engine.meshes, [])
        self.engine.rect_ready = True
        self.context.update(0)
        self.assertTrue(self.context.ready)
        self.context.update(0)
        self.assertEqual(len(self.engine.meshes), 1)

    def test_plot_history_gaps_and_bounded_geometry(self):
        series = Series("signal", capacity=10000)
        series.extend((i, math.sin(i)) for i in range(20000))
        self.assertEqual(len(series.points), 10000)
        reduced = decimate(series.points, 100)
        self.assertLessEqual(len(reduced), 400)
        reduced = decimate([(i, math.nan if i == 50 else i) for i in range(1000)], 10)
        self.assertTrue(any(math.isnan(y) for x, y in reduced))
        plot = Plot(self.context)
        plot.add(series)
        plot.refresh()
        count = len(self.engine.meshes)
        plot.refresh()
        self.assertEqual(count, len(self.engine.meshes))

    def test_runner_exception_requests_exit_and_cleans_up(self):
        class BadScreen(Screen):
            unmounted = False
            def update(self, dt):
                raise RuntimeError("model failed")
            def unmount(self):
                self.unmounted = True
        screen = BadScreen()
        app = Application(screen)
        callbacks = app.callbacks(self.engine)
        callbacks["init"]()
        callbacks["update"](0)
        self.assertEqual(self.engine.quit, 1)
        callbacks["cleanup"]()
        self.assertTrue(screen.unmounted)
        with self.assertRaisesRegex(RuntimeError, "model failed"):
            app.raise_if_failed()

    def test_native_callback_holders_do_not_retain_application(self):
        app = Application()
        callbacks = app.callbacks(self.engine)
        reference = weakref.ref(app)
        del app
        gc.collect()
        self.assertIsNone(reference())
        callbacks["cleanup"]()  # A callback surviving its owner is harmless.

    def test_gallery_component_construction_and_theme(self):
        p = Parameter("gain", 1.0, minimum=0.0, maximum=2.0)
        ui.ParameterRow(self.context, parameter=p)
        playback = Playback(timeline=Timeline(duration=2))
        controls = ui.PlaybackControls(self.context, playback=playback)
        self.context.apply_theme(ui.Theme(font_size=22))
        self.context.update(0)
        self.assertEqual(controls.seek_value.value, playback.time)
        self.context.dispose()
        self.assertFalse(self.engine.entities)

    def test_locked_context_allows_only_explicit_cancel_controls(self):
        seen = []
        normal = ui.Button(self.context, on_click=lambda: seen.append("normal"))
        cancel = ui.Button(self.context, on_click=lambda: seen.append("cancel"), allow_when_locked=True)
        self.engine.clicks.update((normal.entity, cancel.entity))
        self.context.locked = True
        self.context.update(0)
        self.assertEqual(seen, ["cancel"])


class ExportTests(unittest.TestCase):
    def setUp(self):
        self.engine = FakeEngine()
        self.playback = Playback(Model(), timeline=Timeline(duration=1))
        self.context = Context(self.engine)
        self.exporter = Exporter(self.engine, self.playback, context=self.context)

    def test_frame_zero_retry_and_exact_count(self):
        self.exporter.start("out.mp4", options=ExportOptions(fps=10, quit_when_done=True))
        self.exporter.update()
        self.assertEqual(self.playback.time, 0)
        self.engine.present(False)
        self.exporter.post_render()
        self.exporter.update()
        self.assertEqual(self.playback.time, 0)
        times = []
        while self.exporter.active:
            self.exporter.update()
            times.append(self.playback.time)
            self.engine.present()
            self.exporter.post_render()
        self.assertEqual(len(times), 10)
        self.assertEqual(times[0], 0)
        self.assertAlmostEqual(times[-1], 0.9)
        self.assertTrue(self.exporter.completed)
        self.assertFalse(self.context.locked)
        self.assertEqual(self.engine.quit, 1)

    def test_replay_preparation_does_not_record_unready_states(self):
        self.playback.replay_batch = 1
        self.exporter.start("out.mp4", options=ExportOptions(fps=2))
        self.exporter.update()
        self.engine.present()
        self.exporter.post_render()
        self.exporter.update()
        self.assertTrue(self.engine.recording_paused)
        self.engine.present()
        self.exporter.post_render()
        self.assertEqual(self.exporter.sample, 1)

    def test_cancel_failure_resize_and_finalization(self):
        self.exporter.start("out.mp4")
        self.exporter.cancel()
        self.assertFalse(self.exporter.completed)
        self.assertFalse(self.context.locked)
        self.exporter.start("out.mp4")
        with self.assertRaisesRegex(RuntimeError, "resized"):
            self.exporter.on_resize(800, 600)
        self.assertFalse(self.context.locked)
        self.exporter.start("out.mp4")
        self.engine.is_recording = False
        with self.assertRaisesRegex(RuntimeError, "encoder"):
            self.exporter.update()
        self.engine.record_ok = False
        with self.assertRaisesRegex(RuntimeError, "start"):
            self.exporter.start("out.mp4")
        self.assertFalse(self.context.locked)

    def test_screenshot_after_present_and_finalization_error(self):
        self.exporter.screenshot("shot.png")
        self.assertFalse(any(c[0] == "screenshot" for c in self.engine.calls))
        self.exporter.post_render()
        self.assertEqual(self.engine.calls[-1], ("screenshot", "shot.png"))
        self.exporter.start("out.mp4")
        self.engine.stop_ok = False
        with self.assertRaisesRegex(RuntimeError, "finalization"):
            self.exporter.cancel()
        self.assertFalse(self.context.locked)

    def test_framebuffer_dimensions_must_match_before_locking(self):
        with self.assertRaisesRegex(ValueError, "framebuffer"):
            self.exporter.start("out.mp4", options=ExportOptions(width=640, height=480))
        self.assertFalse(self.context.locked)

    def test_export_waits_for_canvas_layout_before_recording_frame_zero(self):
        self.engine.rect_ready = False
        drawing = Drawing(self.context)
        drawing.line((0, 0), (50, 50))
        self.exporter.start("out.mp4")
        self.context.update(0)
        self.exporter.update()
        self.engine.present()
        self.exporter.post_render()
        self.assertEqual(self.engine.recorded_frame_count, 0)
        self.engine.rect_ready = True
        self.context.update(0)
        self.exporter.update()
        self.assertEqual(self.playback.time, 0)
        self.engine.present()
        self.exporter.post_render()
        self.assertEqual(self.engine.recorded_frame_count, 1)


class ImportTests(unittest.TestCase):
    def test_all_modules_import_without_extension(self):
        code = "import sys; sys.modules['shoonyakasha._shoonyakasha'] = None; " + \
               "from shoonyakasha.toolkit import app, ui, state, drawing, plots, playback, timeline, lessons, capture"
        env = dict(os.environ, PYTHONPATH=str(Path(__file__).resolve().parents[2] / "python"))
        result = subprocess.run([sys.executable, "-c", code], env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()

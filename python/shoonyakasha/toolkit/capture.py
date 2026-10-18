"""Frame-confirmed export through the engine's existing recording support."""
from dataclasses import dataclass
import math


@dataclass(frozen=True)
class ExportOptions:
    width: int = 1920
    height: int = 1080
    fps: int = 30
    quality: int = 18
    quit_when_done: bool = False

    def __post_init__(self):
        for value in (self.width, self.height, self.fps):
            if not isinstance(value, int) or value <= 0:
                raise ValueError("export dimensions and fps must be positive integers")
        if not isinstance(self.quality, int) or not 0 <= self.quality <= 51:
            raise ValueError("quality must be between 0 and 51")


class Exporter:
    def __init__(self, engine, playback, *, context=None):
        self.engine, self.playback, self.context = engine, playback, context
        self.active = self.completed = False
        self.error = None
        self.sample = self.total_frames = 0
        self._screenshots = []

    @property
    def progress(self):
        return self.sample / self.total_frames if self.total_frames else 0.0

    def start(self, path, *, duration=None, options=None):
        if self.active or self.engine.is_recording:
            raise RuntimeError("recording is already active")
        self.options = options or ExportOptions()
        if tuple(self.engine.framebuffer_size) != (self.options.width, self.options.height):
            raise ValueError("export dimensions must match the initialized framebuffer")
        duration = self.playback.duration if duration is None else duration
        if not math.isfinite(duration) or duration <= 0 or duration > self.playback.duration:
            raise ValueError("export needs a positive, bounded duration")
        self.total_frames = int(math.ceil(duration * self.options.fps))
        self.sample, self.error, self.completed = 0, None, False
        self._old_lock = self.context.locked if self.context else False
        self.playback.reset()
        self.playback._export_locked = True
        self.playback._lock()
        if self.context:
            self.context.locked = True
        try:
            if not self.engine.start_recording(str(path), fps=self.options.fps,
                                               quality=self.options.quality, fixed_step=True):
                raise RuntimeError("could not start video recording; see engine log")
        except BaseException:
            self._restore()
            raise
        self.active = True
        self._expected = self.engine.recorded_frame_count
        self.engine.recording_paused = True
        self._ready = False

    def _restore(self):
        self.playback._export_locked = False
        self.playback.pause()
        if self.context:
            self.context.locked = self._old_lock

    def _finish(self, error=None):
        if not self.active:
            return
        self.active = False
        self.error = error
        try:
            self.engine.recording_paused = False
            if not self.engine.stop_recording() and self.error is None:
                self.error = RuntimeError("video finalization failed; see engine log")
        except BaseException as failure:
            self.error = self.error or failure
        finally:
            self._restore()
        self.completed = self.error is None and self.sample == self.total_frames
        if self.error:
            raise self.error
        if self.completed and self.options.quit_when_done:
            self.engine.request_quit()

    def update(self, dt=0.0):
        if not self.active:
            return
        if not self.engine.is_recording:
            self._finish(RuntimeError("video encoder stopped before export completed"))
            return
        if self.context is not None and not self.context.ready:
            self._ready = False
            self.engine.recording_paused = True
            return
        self._ready = self.playback.advance_to(self.sample / self.options.fps)
        self.engine.recording_paused = not self._ready

    def post_render(self):
        pending, self._screenshots = self._screenshots, []
        for path in pending:
            if not self.engine.capture_screenshot(path):
                raise RuntimeError("screenshot capture failed: " + path)
        if not self.active:
            return
        if not self.engine.is_recording:
            self._finish(RuntimeError("video encoder stopped before export completed"))
            return
        written = self.engine.recorded_frame_count
        if written == self._expected:
            return  # No successful capture: preserve this sample for the next frame.
        if not self._ready or written != self._expected + 1:
            self._finish(RuntimeError("unexpected recording frame count"))
            return
        self.sample += 1
        self._expected = written
        self.engine.recording_paused = True
        if self.sample == self.total_frames:
            self._finish()

    def on_resize(self, width, height):
        if self.active and (width, height) != (self.options.width, self.options.height):
            self._finish(RuntimeError("window resized during export"))

    def screenshot(self, path):
        self._screenshots.append(str(path))

    def cancel(self):
        if self.active:
            self._finish()

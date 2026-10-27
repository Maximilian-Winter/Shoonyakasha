"""Play recorded frames into a pipeline buffer.

A recording is an array whose first axis counts frames: a NumPy array, an
.npy file, or an .npy file at an http(s) URL. A Player reads its frames on
a background thread, a few ahead, and an engine system writes one into the
buffer each engine frame, or at a fixed rate:

    import shoonyakasha as sk

    def on_init():
        player = sk.streams.play(engine, "Particles", "recording.npy")   # (frames, N, 4) float32

    engine.set_on_init(on_init)

Each frame must be the bytes of the whole buffer, or of its first part: the
player writes it at offset 0, as Engine.set_buffer does. A file is memory-
mapped, so frames are read from disk as they are played; a URL is read
once from start to end (again from the start to loop).
"""

import queue
import threading
from pathlib import Path

__all__ = ["Player", "play", "frames"]


def _npy_frames_from_stream(stream):
    """The frames of the .npy data `stream` reads, first axis first, as bytes."""
    import numpy as np
    from numpy.lib import format as npy

    version = npy.read_magic(stream)
    shape, fortran, dtype = (npy.read_array_header_1_0(stream) if version == (1, 0)
                             else npy.read_array_header_2_0(stream))
    if fortran:
        raise ValueError("arrays in Fortran order are not played")
    if not shape:
        raise ValueError("a recording needs a frame axis; the array is a single value")
    frame_bytes = int(np.prod(shape[1:], dtype=np.int64)) * dtype.itemsize
    for _ in range(shape[0]):
        data = stream.read(frame_bytes)
        if len(data) < frame_bytes:
            raise ValueError("the recording ends inside a frame")
        yield data


def frames(source, loop=False, timeout=60):
    """Iterate the frames of `source` as bytes, first axis first: a NumPy
    array, the path of an .npy file, or an http(s) URL of one. With `loop`,
    start again after the last frame, without end."""
    import numpy as np

    while True:
        if isinstance(source, (np.ndarray, np.generic)):
            if np.ndim(source) == 0:
                raise ValueError("a recording needs a frame axis; the array is a single value")
            array = np.ascontiguousarray(source)
            for frame in array:
                yield frame.tobytes()
        elif isinstance(source, str) and source.startswith(("http://", "https://")):
            import urllib.request
            request = urllib.request.Request(source, headers={"User-Agent": "shoonyakasha"})
            with urllib.request.urlopen(request, timeout=timeout) as response:
                yield from _npy_frames_from_stream(response)
        else:
            array = np.load(Path(source), mmap_mode="r")
            if array.ndim == 0:
                raise ValueError("a recording needs a frame axis; the array is a single value")
            for frame in array:
                yield np.ascontiguousarray(frame).tobytes()
        if not loop:
            return


class Player:
    """Writes the frames of a recording into the pipeline buffer `buffer`,
    one each engine frame or `rate` per second, read `read_ahead` frames in
    advance on a background thread. Made by play(); start it from on_init
    or later, when the engine's systems run.

    A frame that has not been read in time is not waited for: the buffer
    keeps the frame before, and `late` counts it."""

    def __init__(self, engine, buffer, source, rate=None, loop=True, read_ahead=8):
        if rate is not None and rate <= 0:
            raise ValueError("rate must be positive, or None for one frame per engine frame")
        self.engine = engine
        self.buffer = buffer
        self.source = source
        self.rate = rate
        self.loop = loop
        self.played = 0
        self.late = 0
        self.error = None
        self._queue = queue.Queue(maxsize=max(1, read_ahead))
        self._stop = threading.Event()
        self._finished = threading.Event()
        self._thread = None
        self._due = 0.0
        self._system = "streams.Player %s %d" % (buffer, id(self))

    @property
    def done(self):
        """True once every frame of a recording that does not loop was
        played, or playing stopped: by stop(), or because reading or
        writing a frame failed (see `error`). A done player's system stays
        registered, idle, until stop()."""
        return self._stop.is_set() or (self._finished.is_set() and self._queue.empty())

    def start(self):
        """Start reading and playing."""
        if self._thread is not None:
            return self
        self._thread = threading.Thread(target=self._read, name=self._system, daemon=True)
        self._thread.start()
        if not self.engine.ecs.add_system(self._system, self._play):
            raise RuntimeError("a system named %r already exists" % self._system)
        return self

    def stop(self):
        """Stop playing; the buffer keeps the last frame written."""
        self._stop.set()
        try:
            self.engine.ecs.remove_system(self._system)
        except Exception:
            pass
        while not self._queue.empty():
            self._queue.get_nowait()

    def _read(self):
        try:
            for frame in frames(self.source, loop=self.loop):
                while not self._stop.is_set():
                    try:
                        self._queue.put(frame, timeout=0.1)
                        break
                    except queue.Full:
                        continue
                if self._stop.is_set():
                    return
        except Exception as exc:  # reported through .error; this thread has no caller
            self.error = exc
        finally:
            self._finished.set()

    def _play(self, dt):
        if self.rate is None:
            due = 1
        else:
            self._due += dt * self.rate
            due = int(self._due)
            self._due -= due
        frame = None
        for _ in range(due):
            try:
                frame = self._queue.get_nowait()
            except queue.Empty:
                if not self._finished.is_set():
                    self.late += 1
                break
        if frame is not None and not self._stop.is_set():
            try:
                self.engine.set_buffer(self.buffer, frame)
                self.played += 1
            except ValueError as exc:   # the frame does not fit the buffer: playing stops
                self.error = exc
                self._stop.set()
        return True


def play(engine, buffer, source, rate=None, loop=True, read_ahead=8):
    """Start a Player writing the frames of `source` into pipeline buffer
    `buffer`: one per engine frame, or `rate` per second (frames due in one
    engine frame are skipped to the last). `source` is a NumPy array, the
    path of an .npy file, or an http(s) URL of one, its first axis the
    frames. Call it from on_init or later."""
    return Player(engine, buffer, source, rate=rate, loop=loop, read_ahead=read_ahead).start()

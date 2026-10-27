"""shoonyakasha.streams without a GPU: frames from arrays, .npy files and
.npy over HTTP, and a Player paced by a stand-in engine.

    python -m unittest discover -s tests/python
"""

import functools
import http.server
import shutil
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "python"))

from shoonyakasha import streams  # noqa: E402


class FakeEcs:
    def __init__(self):
        self.systems = {}

    def add_system(self, name, callback, priority=0):
        if name in self.systems:
            return False
        self.systems[name] = callback
        return True

    def remove_system(self, name):
        return self.systems.pop(name, None) is not None


class FakeEngine:
    """Runs systems on tick(); set_buffer keeps what each buffer was given."""

    def __init__(self, capacity=None):
        self.ecs = FakeEcs()
        self.buffers = {}
        self.capacity = capacity

    def set_buffer(self, name, data, offset=0):
        if self.capacity is not None and len(data) > self.capacity:
            raise ValueError("does not fit")
        self.buffers.setdefault(name, []).append(bytes(data))

    def tick(self, dt=1 / 60):
        for callback in list(self.ecs.systems.values()):
            callback(dt)


def recording(count=6, values=4):
    return np.stack([np.full(values, i, dtype=np.float32) for i in range(count)])


class Frames(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def serve(self):
        class Quiet(http.server.SimpleHTTPRequestHandler):
            def log_message(self, *args):
                pass

        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Quiet, directory=str(self.tmp)))
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        return "http://127.0.0.1:%d/" % server.server_address[1]

    def test_array_file_and_url_give_the_same_frames(self):
        data = recording()
        np.save(self.tmp / "r.npy", data)
        expected = [frame.tobytes() for frame in data]
        self.assertEqual(list(streams.frames(data)), expected)
        self.assertEqual(list(streams.frames(self.tmp / "r.npy")), expected)
        self.assertEqual(list(streams.frames(self.serve() + "r.npy")), expected)

    def test_recording_shape(self):
        data = recording(5, values=4)
        np.save(self.tmp / "r.npy", data)
        self.assertEqual(streams.recording_shape(data), (5, 4))
        self.assertEqual(streams.recording_shape(self.tmp / "r.npy"), (5, 4))
        self.assertEqual(streams.recording_shape(self.serve() + "r.npy"), (5, 4))

    def test_loop_starts_again(self):
        data = recording(3)
        frames = streams.frames(data, loop=True)
        got = [next(frames) for _ in range(7)]
        self.assertEqual(got, [data[i % 3].tobytes() for i in range(7)])

    def test_bad_recordings(self):
        with self.assertRaises(ValueError):
            list(streams.frames(np.float32(1.0)))
        np.save(self.tmp / "f.npy", np.asfortranarray(np.zeros((3, 4), np.float32)))
        with self.assertRaises(ValueError):
            list(streams.frames(self.serve() + "f.npy"))


class PlayerPacing(unittest.TestCase):
    def wait_for(self, player, frames=1):
        deadline = time.time() + 5
        while player._queue.qsize() < frames and not player._finished.is_set() and time.time() < deadline:
            time.sleep(0.01)

    def test_one_frame_per_engine_frame_then_done(self):
        engine = FakeEngine()
        player = streams.play(engine, "B", recording(6), loop=False, read_ahead=8)
        self.wait_for(player, 6)
        for _ in range(10):
            engine.tick()
        self.assertEqual(engine.buffers["B"], [frame.tobytes() for frame in recording(6)])
        self.assertTrue(player.done)
        self.assertEqual((player.played, player.late), (6, 0))
        player.stop()
        self.assertEqual(engine.ecs.systems, {})

    def test_rate_writes_the_latest_due_frame(self):
        engine = FakeEngine()
        player = streams.play(engine, "B", recording(30), rate=30, loop=False, read_ahead=30)
        self.wait_for(player, 30)
        engine.tick(dt=0.1)        # three frames due: the third is written
        self.assertEqual(engine.buffers["B"], [recording(30)[2].tobytes()])
        engine.tick(dt=0.01)       # 0.3 of a frame: nothing yet
        self.assertEqual(len(engine.buffers["B"]), 1)
        player.stop()

    def test_a_frame_that_does_not_fit_stops_playing(self):
        engine = FakeEngine(capacity=8)
        player = streams.play(engine, "B", recording(3, values=4), loop=False)
        self.wait_for(player)
        engine.tick()
        self.assertIsInstance(player.error, ValueError)
        self.assertTrue(player.done)
        player.stop()

    def test_two_players_on_one_engine(self):
        engine = FakeEngine()
        a = streams.play(engine, "A", recording(2), loop=False)
        b = streams.play(engine, "B", recording(2), loop=False)
        self.assertEqual(len(engine.ecs.systems), 2)
        a.stop()
        b.stop()


if __name__ == "__main__":
    unittest.main()

"""The viewer's replay of a precomputed recording (precompute_replay.py).

sk.streams.play writes the recording's frames into the pipeline buffer
"Replay", `rate` frames a second; the viewer sets the rate from its time
scale, 0 while paused. Replay keeps the playback's own clock alongside, to
tell the shaders the simulation time (for the stars' ages) and how far each
star is to move on along its velocity past the frame written last.
"""

import json
from pathlib import Path

import numpy as np
import shoonyakasha as sk

import model as m


class Replay:
    def __init__(self, engine, path):
        """`path` is the recording without .npy: <path>.npy, <path>_stars.npy, <path>.json."""
        self.engine = engine
        self.path = Path(path)
        self.player = None
        self.position = 0.0    # frames since playing started

    @property
    def available(self):
        return all(Path(str(self.path) + suffix).exists() for suffix in (".npy", "_stars.npy", ".json"))

    def start(self):
        """Write the first frame and the stars' codes and birth times, and start playing."""
        meta = json.loads(Path(str(self.path) + ".json").read_text())
        self.frames = meta["frames"]
        self.frame_units = meta["frame_myr"] / m.TIME_MYR
        stars = np.load(str(self.path) + "_stars.npy").astype(np.float32)
        self.count = len(stars)
        recording = np.load(str(self.path) + ".npy", mmap_mode="r")
        self.engine.set_buffer("ReplayStars", stars.tobytes())
        self.engine.set_buffer("Replay", np.ascontiguousarray(recording[0]).tobytes())
        self.position = 0.0
        self.player = sk.streams.play(self.engine, "Replay", str(self.path) + ".npy", rate=1.0, loop=True)
        self.player.rate = 0.0
        return meta

    def stop(self):
        if self.player is not None:
            self.player.stop()
            self.player = None

    def update(self, dt, units_per_second):
        """Advance by `dt` seconds at `units_per_second` of simulation time (0 to pause).
        Returns the simulation time shown and how far past its frame it is, in time units."""
        rate = units_per_second / self.frame_units
        self.player.rate = rate
        self.position += rate * dt
        # The player writes frame k once `position` passes k + 1 (frame 0 was written at start).
        shown = max(self.position - 1.0, 0.0)
        frame = int(shown) % self.frames
        ahead = (shown - int(shown)) * self.frame_units
        return (frame * self.frame_units + ahead), ahead

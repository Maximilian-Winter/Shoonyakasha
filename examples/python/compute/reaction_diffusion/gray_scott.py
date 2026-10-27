"""Gray-Scott reaction-diffusion on the GPU, shared by the scripts beside it.

GrayScott keeps the grid in two GpuBuffers of (A, B) float pairs and steps
it by running gray_scott.comp from one into the other and swapping them.
Nothing is copied back until state() is called.
"""

import struct
import time
from pathlib import Path

import numpy as np
import shoonyakasha as sk

HERE = Path(__file__).resolve().parent
SHADER = HERE / "gray_scott.comp"
OUTPUT = HERE / "output"

# (f, k) pairs, each named for the pattern it grows into from seed().
PRESETS = {
    "coral": (0.0545, 0.062),        # branching stripes filling the grid
    "mitosis": (0.0367, 0.0649),     # spots that divide and spread
    "fingerprints": (0.037, 0.06),   # long winding stripes
    "maze": (0.029, 0.057),          # stripes closing into a labyrinth
    "spots": (0.03, 0.062),          # a lattice of stable spots
    "worms": (0.058, 0.065),         # separate stripes drifting apart
    "holes": (0.039, 0.058),         # B everywhere, with round holes
    "chaos": (0.018, 0.051),         # spots and fronts forming and dying
}


def seed(width, height, spots=20, radius=8, rng=None):
    """A grid full of A with B in `spots` random squares: (height, width, 2) float32."""
    rng = rng or np.random.default_rng(1)
    cells = np.zeros((height, width, 2), dtype=np.float32)
    cells[..., 0] = 1.0
    for _ in range(spots):
        x, y = rng.integers(radius, width - radius), rng.integers(radius, height - radius)
        cells[y - radius:y + radius, x - radius:x + radius] = (0.5, 0.25)
    cells += rng.uniform(-0.01, 0.01, cells.shape).astype(np.float32)
    return np.clip(cells, 0.0, 1.0)


class GrayScott:
    """A grid of `cells` ((height, width, 2) float32) stepped on the GPU.

    `feed` and `kill` are each one value, or a (first, last) pair that the
    shader spreads over the rows (feed) and columns (kill)."""

    def __init__(self, ctx, cells, feed, kill):
        self.ctx = ctx
        self.height, self.width = cells.shape[:2]
        self.kernel = ctx.kernel(SHADER)
        self.buffers = [ctx.upload(np.ascontiguousarray(cells, dtype=np.float32)), ctx.buffer(cells.nbytes)]
        feed = (feed, feed) if np.isscalar(feed) else feed
        kill = (kill, kill) if np.isscalar(kill) else kill
        self.push = struct.pack("<4f2I", *feed, *kill, self.width, self.height)
        self.steps = 0

    def step(self, count=1):
        """Run `count` steps, each one dispatch reading one buffer and writing the other."""
        for _ in range(count):
            self.ctx.run(self.kernel, self.buffers, threads=(self.width, self.height), push=self.push)
            self.buffers.reverse()
        self.steps += count

    def state(self):
        """The current grid copied back: (height, width, 2) float32 of (A, B)."""
        return self.buffers[0].to_numpy(np.float32, (self.height, self.width, 2))


def timed(label, fn, *args):
    start = time.perf_counter()
    result = fn(*args)
    print("%s: %.2f s" % (label, time.perf_counter() - start))
    return result

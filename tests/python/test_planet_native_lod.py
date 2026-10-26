"""The native level of detail (sk.PlanetLod) against the Python one it
replaces (planet_lod_reference.py, quadtree.py), frame by frame along a
camera path: a descent from 3,000 km to 50 m above the planet while
turning, a moon moving on its orbit, a split factor changed, the level of
detail frozen for a while, the sun's rebuilds asked for, every colour mode,
and the planet's tree dropped and made again. Heights come back two frames
after their jobs, as the GPU's do. Every frame the jobs, the selected nodes
and their slots, the missing nodes and their distances, the instances and
the four draw lists must be the same, to the bit. Where this interpreter's
shoonyakasha has no extension (another test put the source tree first on
sys.path), the comparison runs in a fresh interpreter, which imports the
installed package.

    python -m unittest discover -s tests/python
"""

import math
import os
import subprocess
import sys
import unittest

import numpy as np

import shoonyakasha as sk
from shoonyakasha.planet import cube_sphere

from planet_lod_reference import ReferenceLod

SLOTS, JOBS, VERTICES, SKIRT = 2048, 16, 357, 0.05
RADIUS, MOON_RADIUS, MOON_DISTANCE = 100_000.0, 25_000.0, 600_000.0
FACE_COLOURS = ((0.62, 0.30, 0.25), (0.25, 0.50, 0.55), (0.55, 0.55, 0.30),
                (0.40, 0.30, 0.60), (0.30, 0.55, 0.30), (0.60, 0.42, 0.25))
INDEX_COUNT = 2304
SHADOW_REACH = 25_000.0
FRAMES = 700
LATENCY = 2


def heights_of(job):
    """Made-up lowest and highest heights for a job's chunk, some below sea level."""
    face, level, x, y = (int(v) for v in job[0])
    low = float((face * 7919 + level * 104729 + x * 31 + y * 17) % 2000 - 1000)
    return low, low + 200.0


def normalize(v):
    n = math.sqrt(sum(c * c for c in v))
    return tuple(c / n for c in v)


def view_at(frame):
    """The camera on the path: over the site, coming down from 3,000 km to 50 m in the first
    500 frames while turning, then moving along the ground."""
    site = cube_sphere.direction(4, 1.0 / 64.0, 1.0 / 64.0)
    t = min(frame, 500) / 500.0
    height = 3_000_000.0 * (50.0 / 3_000_000.0) ** t
    drift = max(frame - 500, 0) * 0.0002
    up = normalize((site[0] + drift, site[1], site[2]))
    east = normalize((up[2], 0.0, -up[0]))
    turn = frame * 0.01
    level = (east[0] * math.cos(turn), math.sin(turn) * 0.5, east[2] * math.cos(turn))
    forward = normalize(tuple(level[k] - up[k] * (0.3 + 0.7 * (1.0 - t)) for k in range(3)))
    position = tuple(up[k] * (RADIUS + 400.0 + height) for k in range(3))
    return (position, forward, 70.0, 16.0 / 9.0)


def bodies_at(frame):
    angle = 0.6 + frame * 0.003
    moon = (MOON_DISTANCE * math.cos(angle), MOON_DISTANCE * math.sin(angle) * 0.3, MOON_DISTANCE * math.sin(angle))
    return [(0, (0.0, 0.0, 0.0), RADIUS - 1650.0, True), (1, moon, MOON_RADIUS - 2000.0, False)]


class NativeLodMatchesPython(unittest.TestCase):
    def test_frame_by_frame(self):
        if not sk.extension_available():
            here = os.path.dirname(os.path.abspath(__file__))
            result = subprocess.run([sys.executable, os.path.abspath(__file__)], cwd=here, capture_output=True,
                                    text=True)
            self.assertEqual(result.returncode, 0, result.stdout[-3000:] + result.stderr[-3000:])
            return
        self.compare()

    def compare(self):
        native = sk.PlanetLod(SLOTS, JOBS, VERTICES, SKIRT)
        native.set_face_colours(FACE_COLOURS)
        python = ReferenceLod(SLOTS, JOBS, VERTICES, SKIRT, FACE_COLOURS)
        for lod in (native, python):
            lod.create_tree(0, RADIUS, -2000.0, 6000.0, 12, 1.25, 0.05)
            lod.create_tree(1, MOON_RADIUS, -2000.0, 1500.0, 12, 1.25, 0.05)
        gpu = np.zeros((SLOTS, 4), dtype=np.float32)
        history = []
        compared = {"selected": 0, "jobs": 0}

        def native_jobs():
            return native.jobs()

        def check_jobs(frame):
            self.assertEqual(native_jobs(), python.pool.jobs, "jobs at frame %d" % frame)
            compared["jobs"] += len(python.pool.jobs)

        def run_jobs():
            for job in python.pool.jobs:
                slot = int(job[1][0])
                low, high = heights_of(job)
                gpu[slot] = (low, high, job[1][2], 0.0)
            native.clear_jobs()
            python.pool.clear_jobs()

        check_jobs(0)
        run_jobs()
        for frame in range(1, FRAMES + 1):
            history.append(gpu.copy())
            if len(history) > LATENCY:
                rows = history.pop(0)
                native.take_heights(rows.tobytes())
                python.pool.take_heights(rows)
            if frame == 300:
                for lod in (native, python):
                    lod.drop_tree(0)
                    lod.create_tree(0, RADIUS, -2000.0, 6000.0, 12, 1.25, 0.05)
            if frame == 420:
                native.set_split_factor(0, 1.6)
                python.trees[0].split_factor = 1.6
            frozen = 450 <= frame < 460
            requeue = frame % 97 == 0
            view, bodies = view_at(frame), bodies_at(frame)
            native.update(view, bodies, frame, frozen, requeue)
            python.update(view, bodies, frame, frozen, requeue)
            check_jobs(frame)
            for body in (0, 1):
                tree = python.trees[body]
                expected = [((n.face, n.level, n.x, n.y), n.chunk) for n in tree.selected]
                got = [((n["face"], n["level"], n["x"], n["y"]), n["slot"]) for n in native.selected(body)]
                self.assertEqual(got, expected, "selected nodes of body %d at frame %d" % (body, frame))
                compared["selected"] += len(expected)
                expected = [(d, (n.face, n.level, n.x, n.y)) for d, n in tree.missing]
                got = [(d, (n["face"], n["level"], n["x"], n["y"])) for d, n in native.missing_nodes(body)]
                self.assertEqual(got, expected, "missing nodes of body %d at frame %d" % (body, frame))
            self.assertEqual(native.used, python.pool.used)
            self.assertEqual(native.pending, len(python.pool.pending))
            self.assertEqual(native.rebuild_queued, len(python.rebuild))
            colour = (frame // 50) % 3
            native.write_draws(view, bodies, colour, SHADOW_REACH, INDEX_COUNT)
            instances, chunk, ocean, shadow, culled = python.write_draws(view, bodies, colour, SHADOW_REACH,
                                                                          INDEX_COUNT)
            self.assertEqual(native.instances(), instances.tobytes(), "instances at frame %d" % frame)
            self.assertEqual(native.draws("chunk"), chunk.tobytes(), "chunk draws at frame %d" % frame)
            self.assertEqual(native.draws("ocean"), ocean.tobytes(), "ocean draws at frame %d" % frame)
            self.assertEqual(native.draws("shadow"), shadow.tobytes(), "shadow draws at frame %d" % frame)
            self.assertEqual(native.culled, culled)
            run_jobs()
        # The path has to have exercised the trees: deep levels, many chunks, rebuilds.
        deepest = max(n["level"] for n in native.nodes(0))
        self.assertGreaterEqual(deepest, 10)
        self.assertGreater(compared["selected"], 50_000)
        self.assertGreater(compared["jobs"], 1_000)


if __name__ == "__main__":
    unittest.main()

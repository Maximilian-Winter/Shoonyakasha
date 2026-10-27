"""Particles simulated on a Python thread and streamed to the GPU while the engine runs.

    python streamed_particles.py                         # live: 65536 particles
    python streamed_particles.py --count 200000 --attractors 6
    python streamed_particles.py --record swarm.npy --seconds 8
    python streamed_particles.py --play swarm.npy        # or an http(s) URL of one

A background thread integrates the particles with NumPy, pulled and swirled
around a few attractors, and writes their positions into the pipeline
buffer "Particles" through engine.stream(), never waiting for the engine.
The attractors are ECS entities: each frame they are moved with
scene.set_transforms, read back with scene.get_transforms, and their rows
(position, strength, colour) handed to the GPU with ecs.set_gpu_components;
the shader tints each particle by the attractors near it. --record also
saves the frames sent as an .npy, and --play plays one back with
sk.streams.play instead of simulating.
"""

import argparse
import math
import os
import threading
import time

import numpy as np
import shoonyakasha as sk
from shoonyakasha.mathutil import look_rotation, orbit

HERE = os.path.dirname(os.path.abspath(__file__))
PIPELINE = os.path.join(HERE, "pipeline")
CAPACITY = 262144        # "Particles" elementCount in pipeline/pipeline.json
MAX_ATTRACTORS = 16      # "Attractors" elementCount

COLORS = np.array([(1.0, 0.35, 0.15), (0.2, 0.55, 1.0), (0.95, 0.85, 0.3), (0.6, 0.25, 1.0),
                   (0.2, 1.0, 0.6), (1.0, 0.3, 0.6), (0.4, 0.9, 1.0), (1.0, 0.6, 0.2)], dtype=np.float32)


def attractor_positions(count, t):
    """Where the attractors are at time t: each on its own slow Lissajous path."""
    i = np.arange(count, dtype=np.float32)
    phase = i * (2.0 * math.pi / max(count, 1))
    return np.stack([2.2 * np.sin(0.31 * t + phase), 1.1 * np.sin(0.47 * t + 2.0 * phase),
                     2.2 * np.cos(0.23 * t + phase)], axis=1).astype(np.float32)


def _axes():
    """A tilted spin axis for each attractor, so their vortices lie in different planes."""
    k = np.arange(MAX_ATTRACTORS, dtype=np.float32)
    axes = np.stack([np.sin(1.7 * k), np.ones_like(k), np.cos(2.3 * k)], axis=1)
    return axes / np.linalg.norm(axes, axis=1, keepdims=True)


AXES = _axes()


class Simulation:
    """Particles carried around the attractors by a swirling flow, stepped on
    a background thread at up to `hz` steps a second; each step's positions
    go to `stream` (and to `recording`, one row per step, until it is full)."""

    def __init__(self, count, stream, hz=120.0, recording=None):
        self.rng = np.random.default_rng(7)
        self.pos = self.rng.normal(0.0, 1.5, (count, 3)).astype(np.float32)
        self.vel = np.zeros((count, 3), dtype=np.float32)
        self.out = np.zeros((count, 4), dtype=np.float32)
        self.stream = stream
        self.hz = hz
        self.recording = recording
        self.recorded = 0
        self.steps = 0
        self.attractors = np.zeros((0, 4), dtype=np.float32)   # replaced whole by the engine thread
        self.stopped = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)

    def step(self, dt):
        # Each particle's velocity follows a flow: around each attractor it
        # swirls about that attractor's own axis and drifts inwards, both
        # fading with distance, and a weak pull home keeps the swarm together.
        target = -0.07 * self.pos
        for k, (x, y, z, strength) in enumerate(self.attractors):
            d = np.array((x, y, z), dtype=np.float32) - self.pos
            near = np.exp(-np.einsum("ij,ij->i", d, d) / (2.0 * 1.15 ** 2)) * strength
            target += (2.5 * np.cross(AXES[k % len(AXES)], d) + 0.7 * d) * near[:, None]
        self.vel += (target - self.vel) * min(1.0, 2.5 * dt)
        self.vel += self.rng.normal(0.0, 0.5 * dt, self.vel.shape).astype(np.float32)
        self.pos += self.vel * dt
        self.out[:, :3] = self.pos
        self.out[:, 3] = np.sqrt(np.einsum("ij,ij->i", self.vel, self.vel))

    def run(self):
        period = 1.0 / self.hz
        next_time = time.perf_counter()
        while not self.stopped.is_set():
            self.step(period)
            self.stream.write(self.out)
            if self.recording is not None and self.recorded < len(self.recording):
                self.recording[self.recorded] = self.out
                self.recorded += 1
            self.steps += 1
            next_time += period
            delay = next_time - time.perf_counter()
            if delay > 0:
                time.sleep(delay)
            else:
                next_time = time.perf_counter()

    def start(self):
        self.thread.start()

    def stop(self):
        self.stopped.set()
        self.thread.join()


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--count", type=int, default=65536, help="particles, at most %d" % CAPACITY)
    parser.add_argument("--attractors", type=int, default=4, help="at most %d" % MAX_ATTRACTORS)
    parser.add_argument("--hz", type=float, default=120.0, help="simulation steps a second")
    parser.add_argument("--record", metavar="NPY", help="also save the frames sent, then quit")
    parser.add_argument("--seconds", type=float, default=8.0, help="how long --record records")
    parser.add_argument("--play", metavar="NPY_OR_URL", help="play a recording instead of simulating")
    parser.add_argument("--screenshot", metavar="PNG", help="save a frame after --duration seconds, then quit")
    parser.add_argument("--duration", type=float, default=0.0, help="quit after this many seconds (0: run until closed)")
    parser.add_argument("--width", type=int, default=1600)
    parser.add_argument("--height", type=int, default=900)
    args = parser.parse_args()
    if not 1 <= args.attractors <= MAX_ATTRACTORS:
        parser.error("--attractors must be 1 to %d" % MAX_ATTRACTORS)
    if args.play:
        shape = sk.streams.recording_shape(args.play)
        if len(shape) != 3 or shape[2] != 4:
            parser.error("a recording is (frames, particles, 4) float32; this one is %s" % (shape,))
        args.count = shape[1]
    if not 1 <= args.count <= CAPACITY:
        parser.error("--count must be 1 to %d" % CAPACITY)
    return args


def main():
    args = parse_args()
    sk.shaders.compile_dir(os.path.join(PIPELINE, "shaders"))
    count = args.count
    engine = sk.Engine(title="Streamed particles", width=args.width, height=args.height,
                       pipeline_json_path=PIPELINE, render_graph_parameters={"particleCount": count})
    state = {"frame": 0, "time": 0.0, "last_report": time.perf_counter(), "last_applied": 0, "last_steps": 0}

    recording = None
    if args.record:
        frames = max(1, int(args.seconds * args.hz))
        recording = np.lib.format.open_memmap(args.record, mode="w+", dtype=np.float32,
                                              shape=(frames, count, 4))

    def on_init():
        engine.set_custom_float("particles.pointSize", 0.05)
        state["camera"] = engine.create_camera((0.0, 2.0, 6.5), fov=55.0)
        state["attractors"] = [engine.scene.create_entity("attractor %d" % i) for i in range(args.attractors)]
        state["stream"] = engine.stream("Particles")
        if args.play:
            state["player"] = sk.streams.play(engine, "Particles", args.play, rate=args.hz, loop=True)
        else:
            state["simulation"] = Simulation(count, state["stream"], hz=args.hz, recording=recording)
        move_attractors(0.0)
        if "simulation" in state:
            state["simulation"].start()

    def move_attractors(t):
        scene, entities = engine.scene, state["attractors"]
        scene.set_transforms(entities, positions=attractor_positions(len(entities), t))
        positions = scene.get_transforms(entities)[0]
        strength = np.ones((len(entities), 1), dtype=np.float32)
        colors = COLORS[np.arange(len(entities)) % len(COLORS)]
        rows = np.hstack([positions, strength, colors, np.ones((len(entities), 1), np.float32)]).astype(np.float32)
        engine.ecs.set_gpu_components("Attractor", entities, rows)
        if "simulation" in state:
            state["simulation"].attractors = np.hstack([positions, strength])

    def on_update(dt):
        state["frame"] += 1
        state["time"] += dt
        t = state["time"]
        move_attractors(t)

        eye = orbit((0.0, 0.0, 0.0), 6.5, 18.0 * t, 14.0 + 8.0 * math.sin(0.2 * t))
        engine.scene.set_position(state["camera"], eye)
        engine.scene.set_rotation(state["camera"], look_rotation(eye, (0.0, 0.0, 0.0)))

        report()
        simulation = state.get("simulation")
        if recording is not None and simulation and simulation.recorded >= len(recording):
            recording.flush()
            print("recorded %d frames of %d particles into %s" % (len(recording), count, args.record))
            engine.request_quit()
        if args.duration and t >= args.duration:
            if args.screenshot and not state.get("captured"):
                engine.capture_screenshot(args.screenshot)
                print("saved", args.screenshot)
                state["captured"] = True
            else:
                engine.request_quit()

    def report():
        now = time.perf_counter()
        elapsed = now - state["last_report"]
        if elapsed < 1.0:
            return
        stream = state["stream"]
        line = "%.0f fps" % (state.get("frames_since", 0) / elapsed)
        if "simulation" in state:
            line += ", %.0f buffer writes applied/s" % ((stream.applied - state["last_applied"]) / elapsed)
            steps = state["simulation"].steps
            line += ", %.0f simulation steps/s" % ((steps - state["last_steps"]) / elapsed)
            state["last_steps"] = steps
        if "player" in state:
            player = state["player"]
            line += ", %d frames played, %d late" % (player.played, player.late)
            if player.error:
                line += ", error: %s" % player.error
        if stream.error:
            line += ", refused: %s" % stream.error
        print(line)
        state["last_applied"] = stream.applied
        state["last_report"] = now
        state["frames_since"] = 0

    def on_post_render():
        state["frames_since"] = state.get("frames_since", 0) + 1

    def on_cleanup():
        if "simulation" in state:
            state["simulation"].stop()
        if "player" in state:
            state["player"].stop()

    engine.set_on_init(on_init)
    engine.set_on_update(on_update)
    engine.set_on_post_render(on_post_render)
    engine.set_on_cleanup(on_cleanup)
    engine.run()


if __name__ == "__main__":
    main()

# Streamed particles

Particles simulated with NumPy on a Python thread and streamed to the GPU while
the engine runs, swirling around a few moving attractors. Run it with the
installed `shoonyakasha` package; its shaders compile with glslc from the
Vulkan SDK on first run.

```sh
python streamed_particles.py                         # live, 65536 particles, 4 attractors
python streamed_particles.py --count 200000 --attractors 6
python streamed_particles.py --record swarm.npy --seconds 8
python streamed_particles.py --play swarm.npy        # or an http(s) URL of one
python streamed_particles.py --duration 12 --screenshot particles.png
```

## How the data gets there

**The particles** live on a background thread (`Simulation`). Each step moves
every particle's velocity towards a flow around the attractors (a swirl about
each attractor's own tilted axis and a drift inwards, both fading with
distance, plus a weak pull back to the centre) and writes the positions, with
each particle's speed in `w`, into the pipeline buffer `Particles` through
`engine.stream("Particles").write(...)`. The thread never waits for the engine:
the engine applies the newest write once a frame, and a write that arrives
before the engine took the previous one replaces it. The thread aims at `--hz`
steps a second (120); how many it reaches depends on the CPU and on the Python
interpreter it shares with the engine's callbacks, and it prints the rate it
gets once a second.

**The attractors** are ECS entities. Each frame `move_attractors` moves them
all with one `scene.set_transforms` call, reads their positions back with
`scene.get_transforms`, and hands each one's row (position and strength, then
colour) to the GPU with `ecs.set_gpu_components("Attractor", ...)`. The same
positions go to the simulation thread. The pipeline's `Attractors` buffer has
`"source": {"type": "scene", "data": "components", "component": "Attractor"}`,
so the engine copies the rows into it every frame, and
`scene.custom.components.Attractor.count` tells the shader how many there are.
The vertex shader tints each particle with the colours of the attractors near
it and brightens it with its speed.

**Recordings.** `--record` also writes each step's positions into an `.npy` of
shape `(steps, particles, 4)`, memory-mapped so it never all sits in memory,
and quits when `--seconds` of steps are recorded. `--play` plays one with
`sk.streams.play` at `--hz` frames a second, looping, instead of simulating; the
particle count comes from the recording (`sk.streams.recording_shape`). A URL is
read from start to end once per loop. The attractors are not recorded: they
move as they do live, and only tint the particles.

## The pipeline

[pipeline/pipeline.json](pipeline/pipeline.json) has one pass: it clears the
window and draws `particleCount` points (the render graph parameter the script
sets from `--count`), each read from `Particles` by `gl_VertexIndex`, as soft
round sprites added onto each other. `Particles` holds up to 262144 particles
and `Attractors` up to 16. `scene.custom.particles.pointSize` is a sprite's
size in world units.

See [Streaming data in](../../../../docs/guides/compute-and-data-flow.md#streaming-data-in).

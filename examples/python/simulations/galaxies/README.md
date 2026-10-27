# Galaxy collision simulation

Two disc galaxies meet, throw out tidal tails and a bridge, and merge. The
design is in [galaxy-sim-design.md](../../../../galaxy-sim-design.md).

```sh
python viewer.py                          # two galaxies of half a million particles each
python viewer.py --inclination 0 150      # the second disc turning against the orbit
python viewer.py --scene ring             # a companion falls through a disc: a ring galaxy
python precompute_replay.py               # the merger with every particle massive -> recordings/ (3.5 minutes)
python viewer.py --scene replay           # play it back
python check.py                           # the model's checks on the GPU, without a window
python check.py merger                    # one of them: kernels, disc, merger
```

## The model

[model.py](model.py) works in kpc, 10^10 solar masses and G = 1: one time
unit is 4.714 Myr and one velocity unit 207.4 km/s.

Each galaxy (`Galaxy`) is a rigid potential that moves as one body: a
Hernquist halo, a Hernquist bulge and a Miyamoto–Nagai disc whose axis is
the galaxy's spin. Stars and gas are test particles (`PARTICLE`, 32 bytes:
position, kind + 4 × galaxy index, velocity, birth time) that feel the sum
of the galaxies' potentials.

The galaxy centres pull on each other, each pair with the mean of A's mass
in B's field and B's mass in A's. They also feel Chandrasekhar dynamical
friction from each other's halos, applied as a decay of each pair's
relative velocity, `exp(-rate dt)`; `Params.log_lambda` (0.2) sets its
strength and 0 turns it off.

Two rules act on the gas, through a grid of 128³ cells of 1 kpc. The grid is
centred on the mass-weighted centre of the galaxies within 48 kpc of the
heaviest (`grid_centre`), so a galaxy that flies off does not drag the grid
away. The rules' constants are in `Params`:

- **Gas dissipation:** each step a gas particle takes on part of its cell's
  mean velocity, `1 - exp(-gas_relax dt)` (`gas_relax` 0.5 a time unit).
  This keeps the cell's momentum, and gas streams that cross lose their
  relative motion.
- **Star formation:** a gas particle becomes a young star, its birth time
  the step's start, with probability `dt` times the sum of two rates:
  - **Dense gas:** where the cell's gas density is above `sf_density` (0.01,
    that is 10^8 solar masses per kpc³), `sf_efficiency / t_ff`, with
    `sf_efficiency` 0.05 and `t_ff = sqrt(3π / (32 ρ))` the free-fall time.
  - **Compressed gas:** where the gas converges faster than 0.02 a time unit
    and its density is above 0.001, `sf_compression × (−div v − 0.02)`,
    with `sf_compression` 0.3. `div v` is the divergence of the cells' mean
    velocities, taken from the six neighbouring cells.
- **Gas mass:** the galaxies' gas (`Galaxy.gas_mass`, 10^10 solar masses
  each) is shared among their gas particles.

[shaders/step.comp](shaders/step.comp) runs a third of a time step, by
`index`: mode `index % 3`, in the step `index / 3` of the frame.

- **Mode 0** steps the centres, in one invocation, and clears the grid.
- **Mode 1** adds each gas particle and its momentum to its cell, with
  integer atomics on fixed-point values.
- **Mode 2** applies the gas rules, counts the stars formed into `Stats`,
  and steps every particle (kick, drift, kick). The first kick uses the
  centres from before the step and the second the centres from after it.

It includes [shaders/galaxy.glsl](shaders/galaxy.glsl). `model.Simulator`
runs it on `sk.ComputeContext` with steps of 0.05 units, and
`stars_formed()` reads the count back. `step_centres` and
`galaxy_acceleration` compute the motion in NumPy.

`merger(...)` builds two galaxies:

- **The particles:** per galaxy, 10% bulge, 70% stellar disc (exponential,
  scale length 3 kpc) and 20% gas disc (5 kpc, thinner), each on circular
  orbits of the galaxy's potential plus a small dispersion.
- **Ages:** bulge stars are 8 to 12 Gyr old. A disc star at radius R is up
  to 12 Gyr × e^(-R / 3 kpc) old, so the outer disc is young. A star's
  birth time is minus its age.
- **The orbit** is set at pericentre. The relative speed there is
  `1 - boundness` times the escape speed from the pair's potential, and the
  approach to `separation` is that motion run backward without friction.
- **The discs' tilts** come from `inclination`, in degrees from the orbit's
  axis; above 90° a disc turns against the orbit.

`ring(...)` builds a ring galaxy. The target disc is at rest at the
origin. A companion of 1/5 its mass falls through it along its axis, 1 kpc
from its centre, at 1.5 times the escape speed, its own disc tilted 30°.

`isolated(...)` builds one galaxy at rest.

## The viewer

[viewer.py](viewer.py) builds a merger, turns it so the orbital plane is
horizontal, and runs it in the engine's render graph,
[pipeline/](pipeline/pipeline.json):

1. **`Step0` to `Step23`** run `step.comp` with `index` = the pass's repeat
   index: 8 steps a frame.
2. **`Stars`** ([shaders/star.vert](shaders/star.vert), colours in
   [shaders/view.glsl](shaders/view.glsl)) draws every particle
   as a soft point, adding into the HDR image `hdrColor`.
   - A star's colour and brightness come from its age: blue and bright
     under 10 Myr, through pale blue at 300 Myr and yellow-white at 3 Gyr,
     to orange and dimmer at 10 Gyr.
   - A star formed during the run also glows pink for its first 15 Myr,
     drawn three times as large, as a young star's ionised gas does.
   - Gas is drawn faint red, at twice the size.
   - `G` colours every particle by its galaxy instead.
3. **`BloomDown0` to `BloomDown5` and `BloomUp4` to `BloomUp0`** build a
   bloom chain at half resolution, its mip 0 the sum of every level.
4. **`Tonemap`** ([shaders/tonemap.frag](shaders/tonemap.frag)):
   - adds `bloom` (0.25) times the chain to the image and multiplies by
     `exposure` (0.8);
   - maps the luminance through the ACES filmic curve and scales the colour
     to it, keeping the hue, so dense cores stay warm;
   - spreads what a channel has beyond 1 over the others.
5. **`UIOverlay`** draws the screen canvases.

The frame's 8 steps share the simulation time owed, 8 time units a second
(38 Myr) at time scale 1, each step at most 0.05 units long. The galaxy
records and `Stats` are read back every frame. They place the cinematic
camera, which slowly circles `grid_centre` (the origin in the replay); in the ring scene, the camera
otherwise moves along with the target galaxy as it drifts. Every 2 seconds
the viewer prints the separation, the stars formed, and the star formation
rate over the last 50 Myr in solar masses a year.

Keys: `1` the merger, `2` the ring galaxy, `3` the replay, `R` restart, `SPACE` pause, `+`
and `-` time scale, `C` the cinematic camera, `G` colour by galaxy or by
age. Hold the right mouse button and use WASD, Q and E to fly. `--scene`
picks the first scene; `--inclination`, `--ratio` and `--pericentre` set up
the merger; `--count` sets the particles of the live scenes. `--duration` and
`--screenshot` run for a while, save a frame and quit.

## The self-gravity replay

[precompute_replay.py](precompute_replay.py) runs the merger's starting
galaxies with every body massive: per galaxy a live Hernquist halo (32,768
bodies) and bulge (8,192), with isotropic speeds from the Jeans equation,
and an exponential disc (24,576) with Toomre's Q = 1.5. The halos deform
and drag on each other, and the discs feel their own gravity.

[shaders/nbody.comp](shaders/nbody.comp) sums every pair directly, a tile
of 256 bodies at a time in shared memory, Plummer-softened at 0.2 kpc, on
`sk.ComputeContext`. A step (kick, drift, kick) is three passes, mode 1, 0,
2.

Every 20 steps (4.7 Myr) the script writes the bulge and disc stars in the
viewer's world coordinates (`model.TO_WORLD`):

| File | Contents |
|---|---|
| `recordings/merger.npy` | frames × stars × 8 half floats: x, y, z, 0, vx, vy, vz, 0 |
| `recordings/merger_stars.npy` | per star, kind + 4 × galaxy and birth time |
| `recordings/merger.json` | frame interval, run constants, pericentres, merger time, momentum drift |

`--gyr`, `--halo`, `--bulge`, `--disc`, `--dt`, `--softening`, `--every`
and `--out` change the run.

In the viewer, key `3` plays it ([replay.py](replay.py)):
- `sk.streams.play` writes its frames into the pipeline buffer `Replay`,
  `rate` frames a second. The viewer sets the rate from the time scale each
  frame, 0 while paused.
- `Replay` keeps the playback's clock beside the player. That gives the
  simulation time, for the stars' ages, and how far past the frame written
  last the playback is.
- The `ReplayStars` pass ([shaders/replay.vert](shaders/replay.vert)) draws
  each star that far along its recorded velocity, in the same colours as
  the live stars, 10 times as bright for its fewer stars.
- It replaces `Step0`-`Step23` and `Stars`, which are switched off.

## Checks

[check.py](check.py):

- **kernels:** one step of both modes against the NumPy functions.
- **disc:** one galaxy for 2 Gyr; how far its disc stars move in radius and
  height.
- **merger:** two galaxies of 16k particles; their pericentres and the time
  they merge, within 1 kpc for good.
- **gas:**
  - how much one step of gas dissipation changes the gas's total momentum;
  - the star formation rate every 118 Myr for 2 Gyr, for an isolated galaxy
    and a merger;
  - the merger's peak must be more than twice the isolated galaxy's.
- **ring:**
  - the radius, between 5 and 15 kpc, where the target's disc stars are
    most overdense against the start, at 100 and 150 Myr;
  - the first ring's speed;
  - young stars by radius at 450 Myr.

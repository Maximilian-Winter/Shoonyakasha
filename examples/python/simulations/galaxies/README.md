# Galaxy collision simulation

Two disc galaxies meet, throw out tidal tails and a bridge, and merge. The
design is in [galaxy-sim-design.md](../../../../galaxy-sim-design.md).

```sh
python viewer.py                          # two galaxies of half a million particles each
python viewer.py --inclination 0 150      # the second disc turning against the orbit
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

[shaders/step.comp](shaders/step.comp) runs half a time step: in its even
mode it steps the centres, in one invocation; in its odd mode every particle
(kick, drift, kick), its first kick from the centres before the step and its
second from after. It includes [shaders/galaxy.glsl](shaders/galaxy.glsl).
`model.Simulator` runs it on `sk.ComputeContext` with steps of 0.05 units;
`step_centres` and `galaxy_acceleration` compute the same in NumPy.

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

`isolated(...)` builds one galaxy at rest.

## The viewer

[viewer.py](viewer.py) builds a merger, turns it so the orbital plane is
horizontal, and runs it in the engine's render graph,
[pipeline/](pipeline/pipeline.json):

1. **`Step0` to `Step15`** run `step.comp` with `mode` = the pass's repeat
   index: 8 steps a frame.
2. **`Stars`** ([shaders/star.vert](shaders/star.vert)) draws every particle
   as a soft point, adding into the HDR image `hdrColor`.
   - A star's colour and brightness come from its age: blue and bright
     under 10 Myr, through pale blue at 300 Myr and yellow-white at 3 Gyr,
     to orange and dimmer at 10 Gyr.
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
records are read back every frame. They place the cinematic camera, which
slowly circles the galaxies' centre of mass, and give the separation the
viewer prints every 2 seconds.

Keys: `R` restart, `SPACE` pause, `+` and `-` time scale, `C` the
cinematic camera, `G` colour by galaxy or by age. Hold the right mouse
button and use WASD, Q and E to fly. `--inclination`, `--ratio`,
`--pericentre` and `--count` set up the merger. `--duration` and
`--screenshot` run for a while, save a frame and quit.

## Checks

[check.py](check.py):

- **kernels:** one step of both modes against the NumPy functions.
- **disc:** one galaxy for 2 Gyr; how far its disc stars move in radius and
  height.
- **merger:** two galaxies of 16k particles; their pericentres and the time
  they merge, within 1 kpc for good.

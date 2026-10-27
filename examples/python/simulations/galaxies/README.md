# Galaxy collision simulation

Two disc galaxies meet, throw out tidal tails and a bridge, and merge. The
design is in [galaxy-sim-design.md](../../../../galaxy-sim-design.md).

```sh
python check.py              # the model's checks on the GPU, without a window
python check.py merger       # one of them: kernels, disc, merger
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

[shaders/galaxies.comp](shaders/galaxies.comp) steps the centres in one
invocation, and [shaders/step.comp](shaders/step.comp) steps every particle
(kick, drift, kick), its first kick from the centres before the step and its
second from after. Both include [shaders/galaxy.glsl](shaders/galaxy.glsl).
The time step is 0.05 units. `model.Simulator` runs them on
`sk.ComputeContext`; `step_centres` and `galaxy_acceleration` compute the
same in NumPy.

`merger(...)` builds two galaxies:

- **The particles:** per galaxy, 10% bulge, 70% stellar disc (exponential,
  scale length 3 kpc) and 20% gas disc (5 kpc, thinner), each on circular
  orbits of the galaxy's potential plus a small dispersion.
- **The orbit** is set at pericentre. The relative speed there is
  `1 - boundness` times the escape speed from the pair's potential, and the
  approach to `separation` is that motion run backward without friction.
- **The discs' tilts** come from `inclination`, in degrees from the orbit's
  axis; above 90° a disc turns against the orbit.

`isolated(...)` builds one galaxy at rest.

## Checks

[check.py](check.py):

- **kernels:** one step of both kernels against the NumPy functions.
- **disc:** one galaxy for 2 Gyr; how far its disc stars move in radius and
  height.
- **merger:** two galaxies of 16k particles; their pericentres and the time
  they merge, within 1 kpc for good.

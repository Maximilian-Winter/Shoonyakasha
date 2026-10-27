# Nuclear force simulation

Nucleons as classical particles under two forces, a short-range attraction
and the protons' Coulomb repulsion, run on the GPU. The design is in
[nuclear-sim-design.md](../../../../nuclear-sim-design.md).

```sh
python binding_curve.py                   # binding energy per nucleon against mass number -> output/binding_curve.png
python precompute.py                      # ground states of H-1 ... U-238 -> configs/
python precompute.py --setting scenario   # the viewer's nuclei -> configs/scenario/
python viewer.py                          # a neutron into U-235
python viewer.py --scene chain            # 64 U-235 nuclei and one neutron
python viewer.py --scene fusion           # deuterium and tritium: He-4 and a neutron
```

## The model

[physics.py](physics.py) holds the constants (`Params`) and the nucleon record
(`NUCLEON`, 48 bytes: position, type, velocity, kinetic energy, force, nucleus
id). Every nucleon has mass 1; lengths are in units of `r0`.

[shaders/step.comp](shaders/step.comp) runs half a time step for every
nucleon. In its even mode it sums, over every pair of nucleons, a tile of 128
at a time:

```text
strong   V(r) = d_core e^(-2a(r - r0)) - 2 D e^(-a(r - r0))    D = d_unlike   (proton-neutron)
                                                               D = d_protons  (proton-proton)
                                                               D = d_neutrons (neutron-neutron)
Coulomb  V(r) = kc S(r) / r                                     protons only
```

The repulsive core is the same for every pair; only the attraction depends
on the pair. The screening `S(r)` is 1 below `screen_start` and falls
smoothly to 0 at `screen_end`; with `screen_end` 0 there is none. The even
mode also writes each nucleon's share of the potential energy, and with
`isolate` it only lets nucleons with the same nucleus id feel each other, so
many nuclei can be cooled side by side in one buffer. The odd mode steps
velocities from forces and positions from velocities (semi-implicit Euler),
with optional damping. `physics.Simulator` runs it on buffers that stay on the
GPU.

### Two settings

The defaults (`d_core` 1, `d_unlike` 1, `d_protons` and `d_neutrons` 0.1,
`a` 4, `kc` 0.16, no screening) are the realistic setting, tuned with
`binding_curve.py` and isobar scans against the real nuclei:

| | Model | Real |
|---|---|---|
| Peak of binding energy per nucleon | A = 56 | iron, A = 56 |
| U-238 against Fe-56, per nucleon | 0.89 | 0.86 |
| Most bound Z at A = 56 | about 24 | 25 |
| Most bound Z at A = 238 | about 89 | 92 |

The model's peak is 2.10 units a nucleon against iron's 8.79 MeV: one unit is
about 4.2 MeV.

`physics.SCENARIO` is the viewer's setting: `kc` 0.36, at which uranium is
close enough to splitting that, with the capture rule, a captured neutron
splits it, typically 40 to 80 time units later; and Coulomb screened from
9 to 11, beyond the size of a uranium nucleus, so that nuclei 21 apart do
not feel each other.

## Rules

Three rules in [events.py](events.py) add what the potentials do not
produce. Each works from a look at the nucleons and gives a kick per
nucleon, `v' = v * scale + dv`, that leaves momentum unchanged, and energy
too apart from what emission radiates:

- **Capture radius** (`steer_kicks`): a free neutron within 10 of the centre
  of a nucleus of at least 200 nucleons is turned toward that centre, in an
  elastic turn between the two: their relative velocity keeps its length,
  and the nucleus recoils.
- **Capture** (`CaptureRule`): a nucleus of at least 200 nucleons that takes
  in a free neutron (nucleons from a group of at most 4) turns half its
  internal kinetic energy into stretching along its long axis
  (`stretch_kicks`), for up to 200 time units. It does so whenever it will
  be lengthening when the kick lands, which `stretching` works out from the
  look's positions, velocities and forces, and only from looks taken at
  least 0.5 time units after its last kick landed. The viewer passes both
  times: when each readback was taken and when the kicks land.
- **Emission** (`emission_kicks`): each fragment of a split gives off its
  outermost neutron at speed 2.5, the rest recoiling, and radiates part of
  its remaining internal kinetic energy.

`FissionTracker` finds the splits. Every nucleon carries the id of the nucleus
it belongs to; a nucleus has split when its nucleons lie in two or more groups
of at least 20, nucleons closer than 3 linking a group. Each fragment then
gets a new id. `ReactionTracker` names the groups by their protons and
neutrons (`nucleus_name`: p, n, D, T, He-4, ...) and reports a change in that
list once it has lasted 5 time units.

## The viewer

[viewer.py](viewer.py) runs the model under `SCENARIO` in the engine's render
graph, [pipeline/](pipeline/pipeline.json):

1. `Kick0`, `Kick1` ([shaders/kick.comp](shaders/kick.comp)) apply the
   `Kicks` buffer once each time `nuclear.kickSequence` changes.
2. `Step0` to `Step15` run `step.comp` with `mode` = the pass's repeat index:
   8 steps a frame.
3. `Nucleons` draws every nucleon as a lit sphere, protons orange and
   neutrons blue, glowing with kinetic energy (fully at 6 units in fission
   and the chain reaction, 0.3 in fusion).
4. `Trails` ([shaders/trail.vert](shaders/trail.vert)) adds a fading blue
   ribbon behind each nucleon marked in the `Marks` buffer, from its position
   back to `x - v * trailTime` (6 time units): the path a free neutron has
   covered in that time.
5. `Flashes` ([shaders/flash.vert](shaders/flash.vert)) adds a soft warm glow
   for each of the 64 entries of the `Flashes` buffer (place, start time,
   strength), growing and fading over 30 time units of simulation time; the
   larger the strength, the larger the glow.
6. `UIOverlay` draws the screen canvases (a `ui_canvas` pass).

Every step is 0.02 time units long. A frame runs its steps once real time has
gathered enough for them (20 time units a second at time scale 1) and
otherwise runs them with `dt` 0, so slow frames slow the simulation down.
Every 4 frames the nucleons are read back, the rules run, and any kicks are
sent with `set_buffer("Kicks", ...)`. The neutrons alone in their group are
marked for trails. Each split adds a flash at the split nucleus's centre,
its strength the energy the emission rule radiated (at least 1); each
fusion reaction adds one of strength 1 at its largest product.

Keys: `1` fission, `2` chain reaction, `3` fusion, `R` restart, `SPACE`
pause, `+` and `-` time scale, `F1` settings, `F2` the controls, `H` the
explanation cards, `[` and `]` the previous and next card. Hold the right
mouse button and use WASD, Q and E to fly the camera. `--lattice` and
`--spacing` set the chain reaction's cube of nuclei. In fusion, `--reaction
dt` (deuterium and tritium) or `dd` (two deuterium) picks the nuclei and
`--speed` their closing speed, 0.5 unless given; the viewer prints each
split and reaction. `--duration` and `--screenshot` run for a while, save a
frame and quit.

### The screen

[interface.py](interface.py) builds it from `shoonyakasha.toolkit.overlay`:

- **Readings** (top left, a `Card`): simulation time and speed, the number
  of nucleons, and the scene's events: captures, splits and neutrons given
  off, or in fusion the reactions and the latest one, and the free neutrons.
- **Explanation cards** (bottom left, a `Narration`): the scene's story from
  [story.py](story.py), a list of beats, each a title, a few sentences and a
  trigger on the simulation's progress (a capture, a split, a time since one,
  a reaction). A beat shows when its trigger first holds. With *Pause at each
  new card* on (the default, off with `--duration`) the simulation pauses on
  each new card until `SPACE`; otherwise each card stays up at least 6
  seconds. `[` and `]` step through the beats reached so far.
- **Key hints** along the bottom (a `HintBar`), and the controls sheet on
  `F2` (a `HelpOverlay`).
- **Settings** on `F1` (a `SettingsMenu`): the scenes, pause, restart and
  time scale; what scenes are built with, applied on restart (neutron
  speeds, the chain's spacing and cube side, the fusion reaction and its
  speed); the rules' constants, applied at once (the capture rule on or off,
  the capture radius, the emitted neutrons' speed, the share of heat
  radiated); and what is drawn (cards, readings, hints, trails and their
  length, flashes).

## The tools

**`binding_curve.py`** cools one nucleus per mass number (with the valley of
stability's proton number, from several proton arrangements, keeping the most
bound) and plots binding energy per nucleon beside the semi-empirical mass
formula scaled to the model's peak. `--d-protons`, `--d-neutrons`, `--kc`, `--a`
and `--d-unlike` try other parameters.

**`precompute.py`** cools the nuclei in `NUCLEI` the same way, from `--seeds`
proton arrangements, and keeps the most bound arrangement that lost no
nucleon and stays whole for 400 time units without damping. It writes
`configs/<name>.npy`, one row of (x, y, z, type) per nucleon relative to the
centre of mass, and `configs/index.json` with each nucleus's Z, N and binding
energy and the parameters used. `--setting scenario` cools H-2, H-3, He-4 and
U-235 under `SCENARIO` into `configs/scenario/`.

`physics.relax_batch` does the cooling: nucleons start on a compact
face-centred cubic lattice, the protons at random sites, and are stepped with
damping until they settle.

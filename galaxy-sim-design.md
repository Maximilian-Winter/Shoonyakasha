# Galaxy Collision Simulation — Design Document

## Overview

Two disc galaxies meet, swing past each other, throw out tidal tails and a
bridge, and merge, with a burst of star formation where their gas collides.
A tech demo of the engine's recent features, each used for what it is for:

| Feature | Use |
|---|---|
| Render graph compute passes, split pipeline files | the live simulation of about a million stars, drawn in HDR with bloom |
| `engine.compute` / `sk.ComputeContext` | the self-gravitating reference run, precomputed; tests of the live kernels without a window |
| `engine.set_buffer` | a million particles built in NumPy, sent when a scene starts |
| `sk.streams.play` | the replay scene: the precomputed run streamed frame by frame into a buffer |
| Readback | the readings: separation, closing speed, star formation rate |
| `toolkit.overlay` | readings, explanation cards, settings menu, key hints, as in the nuclear simulation |

Lives in `examples/python/simulations/galaxies/`.

## 1. Physics Model

### 1.1 Units

Length 1 kpc, mass 10^10 solar masses, G = 1. Then one time unit is 4.72 Myr
and one velocity unit 207 km/s. The readings show Myr, kpc and km/s.

### 1.2 Live model: rigid potentials and test particles

Every particle feeling every other is out of reach live at a million
particles (10^12 pairs a step). The live model is Toomre and Toomre's
restricted method, extended so the galaxies can merge:

- **Each galaxy is a rigid potential** that moves as one body: a Hernquist
  halo (M_h, a_h), a Hernquist bulge (M_b, a_b) and a Miyamoto–Nagai disc
  (M_d, a_d, b_d) in the galaxy's own plane, set by its spin axis.
- **Stars and gas are test particles.** Each feels the sum of the galaxies'
  potentials and has no mass of its own. The cost is (particles × galaxies)
  a step.
- **The galaxy centres** move under each other's pull (the gradient of one
  galaxy's potential at the other's centre, times its mass), plus
  Chandrasekhar dynamical friction:

```text
F_df = -4 pi G^2 M^2 ln(Lambda) rho(r) / v^2 * [erf(X) - 2X/sqrt(pi) exp(-X^2)] * v_hat
X = v / (sqrt(2) sigma(r))
```

  Here rho and sigma are the other galaxy's halo density and dispersion at
  this galaxy's centre. Friction drains the orbit, so the galaxies merge
  after two or three passes instead of passing forever. ln(Lambda) is the
  knob that sets how many passes. Friction is applied as an exact decay of
  each pair's relative velocity over each half step, `exp(-rate dt/2)`, which
  keeps the pair's momentum and stays stable however strong it is (§8.2).
- **The orbit is set at pericentre.** The relative speed there is a fraction
  (1 - boundness) of the escape speed from the galaxies' actual pair
  potential. The approach to the starting separation is that motion run
  backward, without friction (§8.3).

What this leaves out: the halos don't deform, and discs don't
self-gravitate, so there are no spiral arms or bars of the disc's own
making. The replay scene (§5.3) has them, and a card says so.

### 1.3 Gas and star formation (rules)

As in the nuclear simulation, rules add what the forces alone don't produce.
Each is a compute pass:

- **Gas dissipation.** Gas particles deposit mass and momentum into a
  128³ grid that follows the centre of mass (1 kpc cells; integer atomics on
  fixed-point values). Each gas particle's velocity then relaxes toward its
  cell's mean velocity at a rate `k_gas`. Clouds that collide lose their
  relative motion, so gas sinks to the centres and piles up in the bridge.
- **Star formation.** A gas particle in a cell denser than `rho_sf` becomes a
  young star with probability `eps_sf * dt / t_ff(rho)`, using a per-particle
  hash for the random draw. It records its birth time. It counts into an
  atomic counter in `Stats`, which is read back as the star formation rate.

### 1.4 Integration

Leapfrog (kick-drift-kick) with a fixed step dt = 0.05 units (0.24 Myr).
Plummer softening is 0.1 kpc inside the potentials. The viewer runs 8 steps
a frame, with the nuclear viewer's fixed-step accumulator. One merger is
about 650 units, about 30 seconds at time scale 1.

### 1.5 Reference model: full self-gravity, precomputed

`precompute_replay.py` runs the same starting galaxies with every particle
massive. Each galaxy has 16k halo, 4k bulge and 12k disc particles: 65,536
in all, summed directly in tiles of 256 on `sk.ComputeContext`. That is
4.3 × 10^9 pairs a step, about 0.2 s on the development GPU. 3000 steps take
about 10 minutes. The script writes the stars every 8 steps as half-float
(x, y, z, kind): 375 frames of 32k stars, 98 MB, into
`recordings/<scene>.npy`.

## 2. Particle Data

```text
Particle (32 bytes, std430)
  vec4 positionKind   xyz kpc; w = kind + 4 * galaxy (kind: 0 bulge, 1 disc, 2 gas, 3 young star)
  vec4 velocityBirth  xyz; w = birth time (young stars)

Galaxy (96 bytes, at most 4)
  vec4 positionMass   centre, total mass
  vec4 velocity
  vec4 previous       centre before the step, for the particles' first kick
  vec4 spin           disc axis
  vec4 shape          a_h, a_b, a_d, b_d
  vec4 masses         M_h, M_b, M_d

Stats   uint starsFormed, uint gasLeft, ... (readback per_frame)
Grid    128^3 × uvec4 (mass, momentum fixed point), cleared each step
Replay  32768 × uvec2 (packHalf2x16 pairs), filled by sk.streams.play
```

1,048,576 particles: per galaxy 10% bulge, 70% disc, 20% gas.

## 3. GPU Pipeline

`pipeline/` split like the nuclear viewer's. Implemented so far:

```text
pipeline.json            include: data.json, resources.json, passes/*.json
passes/10_sim.json       Step{s}, repeated 16 times: step.comp, even mode the centres (one invocation),
                         odd mode the particles; 8 steps a frame
passes/20_draw.json      Stars (point sprites, additive into hdrColor R16G16B16A16)
passes/30_post.json      BloomDown{m}, BloomUp{m}, Tonemap -> swapchain   (bloom shaders from the showroom)
passes/40_ui.json        UIOverlay
```

Planned: the gas grid passes (`GridClear`, `GridDeposit`) and the gas and
star formation rules in step.comp's odd mode (step 3), and `Replay` in
20_draw.json (step 5).

The centres and the particles are one shader in two modes, as in the
nuclear pipeline, because the render graph repeats one pass, not a pair of
them: Step0 (centres), Step1 (particles), Step2 (centres), and so on.

The viewer builds a scene in NumPy when it starts (0.9 s for a million
particles) and sends it with `set_buffer`. The `.npy` buffer `source` is not
used.

## 4. Rendering

- **Stars.** Gaussian point sprites, 2 px, additive in HDR. Colour and
  brightness come from a star's age, its birth time being minus its age at
  the start:
  - under 10 Myr: blue, bright;
  - 300 Myr: pale blue;
  - 3 Gyr: yellow-white;
  - 10 Gyr: orange, dimmer.

  Bulge stars are 8-12 Gyr old. Disc stars are younger outward (up to
  12 Gyr × e^(-R/R_d)), so cores are warm and tails blue.
- **Gas:** faint red, 4 px.
- **Young stars** (step 3) get their birth time when they form, so they
  start blue and age along the same palette.
- **Colour by galaxy** of origin (`G`) is the other mode.
- **Bloom and tone mapping.** The ACES curve maps luminance alone and the
  colour is scaled to it. Mapping each channel bleached dense cores to white
  (§8.6).
- **Camera.** Free flight (RMB + WASD), or a slow cinematic orbit around the
  centre of mass (`C`).

## 5. Scenes

### 5.1 Merger (key 1)

Two equal galaxies, both discs prograde (turning the way they orbit), with
the first pericentre at about 10 kpc. Expected: long tails after the first
pass, a bridge, two or three passes, a merged remnant with a starburst at
its centre. The menu sets the second disc's inclination (0 to 180°; above
90° it is retrograde, which in Toomre's result makes short, stubby tails),
the mass ratio and the pericentre.

### 5.2 Ring galaxy (key 2)

A small companion (mass ratio 1:5) drops straight through the centre of a
disc, along its axis. The sudden pull and release drive an expanding ring of
compressed gas and young stars outward, like the Cartwheel galaxy.

### 5.3 Self-gravity replay (key 3)

The recording from §1.5, played with `sk.streams.play(engine, "Replay",
"recordings/merger.npy", rate=30)`. Pause, speed and restart act on the
player. The discs here grow spiral arms and bars of their own, which the
live model can't; a card says which model is which. Without the recording,
the scene says how to make it.

## 6. Interface

As in the nuclear viewer: `interface.py` and `story.py` built on
`toolkit.overlay`.

- **Readings:**
  - time in Myr;
  - centre separation (kpc) and closing speed (km/s);
  - star formation rate in solar masses a year (from `Stats` read back, one
    gas particle being about 2 × 10^4 solar masses);
  - stars formed;
  - gas left.
- **Cards** for each scene:
  - Merger: two discs; first approach; tidal tails; the bridge; friction
    and the second pass; starburst; remnant.
  - Ring galaxy: the plunge; the ring wave; star formation in the ring.
  - Replay: what differs from the live model.
- **Settings (F1):**
  - Simulation: screenshot, scenes, pause, restart, time scale.
  - Scene, applied on restart: inclination, mass ratio, pericentre.
  - Rules, applied at once: dynamical friction (ln Lambda, 0 = off), gas
    dissipation `k_gas`, star formation threshold and efficiency.
  - Display: colour mode, exposure, bloom, point size, cinematic camera,
    cards, readings, hints.
- **Keys:**
  - `P` screenshot into `galaxy_captures/`, `F2` the controls sheet;
  - `H`, `[` and `]` for the cards;
  - `1`–`3` scenes, `R` restart, `SPACE` pause, `+` and `-` time scale.

## 7. Implementation Order

1. **Model.** Implemented:
   - `model.py`: units, potentials, forces, friction, starting galaxies and
     orbits in NumPy; `Simulator` on `sk.ComputeContext`.
   - `shaders/galaxies.comp` and `shaders/step.comp`.
   - `check.py`: the kernels against NumPy, an isolated disc for 2 Gyr, a
     merger (§8).
2. **Viewer.** Implemented: the merger scene, pipeline, star sprites, HDR,
   bloom, tonemap, cinematic camera.
3. **Gas and star formation.** Grid passes, young stars, the `Stats`
   readback.
4. **Ring galaxy scene.**
5. **Reference run and replay.** `precompute_replay.py` and the replay scene
   through `sk.streams.play`.
6. **Interface.** Readings, cards, settings, screenshots; README.

Each step ends with the full test suites run, as before. Measurements go
into §8 as they are made.

## 8. Measurements

`check.py` runs these on `sk.ComputeContext`.

### 8.1 Kernels and an isolated disc

One step of `galaxies.comp` and `step.comp` agrees with model.py's NumPy to
4e-7 (centres) and 1e-6 (particles). One galaxy alone (64k particles, 2 Gyr,
8485 steps, 4 s) keeps its disc: the median star's radius changes by 1.8%,
1.2% of stars change by more than 20%, and the mean height goes from 0.24 to
0.29 kpc.

Galaxy: halo 60 at scale 15, bulge 1 at 0.5, disc 5 with a = 3, b = 0.3
(circular speed 1.23 units, 255 km/s, at 8 kpc); stellar disc R_d = 3 kpc,
gas disc R_d = 5 kpc.

### 8.2 Friction

With Chandrasekhar's force added explicitly and ln(Lambda) = 3, the
galaxies stopped at 16 kpc before passing, then crept together. Near the
centres the drag rate times dt exceeded 1, the update overshot, and a final
close pass threw them apart at 25 units. Applying friction as an exact
decay of the relative velocity removed the overshoot. With the full galaxy
masses in the formula, the useful values are far below the usual 2-3
(boundness 0.05, pericentre 10 kpc, prograde discs at 0 and 30 degrees):

| ln(Lambda) | Pericentres (Myr, kpc) | Apocentre (kpc) | Merged (Myr) |
|---|---|---|---|
| 0 | 139 at 10.0, 3301 at 10.0 | 212 | no |
| 0.1 | 140, 1299, 1829, 2083, 2192 | 101 | 2307 |
| 0.2 | 141 at 9.7, 713 at 6.2 | 58 | 884 |
| 0.3 | 142 at 9.6, 464 at 3.1 | 36 | 971 |
| 0.5 | 145 at 9.2 | 16 | 2274 (creeps in) |

0.2 is the default. On the GPU: passes at 141 and 707 Myr, merged at
895 Myr.

### 8.3 The orbit

A parabola for point masses of the galaxies' total mass (pericentre 10 kpc
asked) gave a closest approach of 16.7 kpc, and the galaxies escaped
without friction: the halos' potential is shallower than a point's. Setting
the orbit at pericentre against the actual pair potential and running it
backward gives the pericentre asked (10.0 kpc without friction).

### 8.4 What it looks like

128k particles, prograde discs at 0 and 30 degrees: a bridge and two tails
at 300 Myr, the galaxies apart again at 500, falling back at 700, one
remnant ringed with shells by 1200-1600 Myr.

### 8.5 The viewer's speed

A million particles, 8 steps a frame, at 1600 × 900 on the development GPU:
210-390 frames a second.

### 8.6 Colour

- **Colour by kind** (bulge yellow, disc white-blue, gas pink): cores
  saturated to white, and the gas disc, which reaches further out than the
  stellar disc, made the tails pink.
- **ACES per channel** bleaches every bright core to white whatever its
  colour. Mapping the luminance and scaling the colour keeps the cores
  warm.
- **Pale linear colours** such as (0.6, 0.75, 1.0) show almost white after
  sRGB encoding. The palette needs strongly saturated linear values,
  (0.20, 0.38, 1.0) for young stars.

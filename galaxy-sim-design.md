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
Both run in step.comp, modes 1 and 2 (§3):

- **Gas dissipation.** Gas particles deposit mass and momentum into a
  128³ grid that follows the centre of mass (1 kpc cells; integer atomics on
  fixed-point values). Each gas particle's velocity then relaxes toward its
  cell's mean velocity at a rate `k_gas`. Clouds that collide lose their
  relative motion, so gas sinks to the centres and piles up in the bridge.
- **Star formation.** A gas particle becomes a young star, using a
  per-particle hash for the random draw. It records its birth time and
  counts into an atomic counter in `Stats`, which is read back as the star
  formation rate. Its probability is `dt` times the sum of:
  - **dense gas:** `eps_sf / t_ff(rho)` where the cell is denser than
    `rho_sf`;
  - **compressed gas:** `sf_compression * (-div v - 0.02)` where the gas
    converges faster than 0.02 a time unit and is denser than 0.001
    (§8.8).

  Over the passage of a compression, the compressed-gas term turns about
  `sf_compression * ln(density ratio)` of the gas into stars.
- **The grid's centre** is the mass-weighted centre of the galaxies within
  48 kpc of the heaviest, so a companion that flies off does not drag the
  grid off the target.

### 1.4 Integration

Leapfrog (kick-drift-kick), with Plummer softening of 0.1 kpc inside the
potentials. check.py steps 0.05 units (0.24 Myr). The viewer runs 8 steps a
frame, sharing the simulation time owed between them: 8 units (38 Myr) a
second at time scale 1, each step at most 0.05 units. Unlike the nuclear
model's stiff forces, the smooth potentials take a step that varies from
frame to frame. The merger, to about 900 Myr, takes about 25 seconds.

### 1.5 Reference model: full self-gravity, precomputed

Implemented as `precompute_replay.py`. It runs the merger's starting
galaxies with every body massive, summed directly in tiles of 256 by
`shaders/nbody.comp` on `sk.ComputeContext`. Each galaxy has:
- a live Hernquist halo of 32k bodies and a bulge of 8k, both with Jeans
  velocities;
- an exponential disc of 24k with Toomre Q = 1.5.

That is 131,072 bodies, or 1.7 × 10^10 pairs a step: 32 ms (§8.9). 1.5 Gyr
(6364 steps) takes 208 s.

Every 20 steps the script writes the 65,536 bulge and disc stars as half
floats (x, y, z, 0, vx, vy, vz, 0) in world coordinates: 319 frames 4.7 Myr
apart, 334 MB, into `recordings/merger.npy`. Beside it go
`merger_stars.npy` (kind + 4 × galaxy, birth time per star) and
`merger.json` (frame interval, run constants, the measured pericentres and
merger time).

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
Replay       65536 × uvec4 (half floats x y, z 0, vx vy, vz 0), filled by sk.streams.play
ReplayStars  65536 × vec2 (kind + 4 * galaxy, birth time), set when the replay starts
```

1,048,576 particles: per galaxy 10% bulge, 70% disc, 20% gas.

## 3. GPU Pipeline

`pipeline/` split like the nuclear viewer's. Implemented so far:

```text
pipeline.json            include: data.json, resources.json, passes/*.json
passes/10_sim.json       Step{s}, repeated 24 times: step.comp, mode = s % 3:
                         0 the centres (one invocation) and clearing the gas grid,
                         1 gas into the grid, 2 the gas rules and every particle; 8 steps a frame
passes/20_draw.json      Stars (point sprites, additive into hdrColor R16G16B16A16)
passes/30_post.json      BloomDown{m}, BloomUp{m}, Tonemap -> swapchain   (bloom shaders from the showroom)
passes/40_ui.json        UIOverlay
```

20_draw.json also holds `ReplayStars` (shaders/replay.vert), enabled in the
replay scene in place of `Step{s}` and `Stars`.

The centres, the grid and the particles are one shader in three modes
because the render graph repeats one pass, not a sequence of them: Step0
(centres), Step1 (gas into the grid), Step2 (particles), Step3 (centres),
and so on.

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
- **Young stars** get their birth time when they form, so they start blue
  and age along the same palette. For their first 15 Myr they are drawn
  three times as large with a pink glow that fades with age.
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

Implemented as `model.ring`:
- the companion passes 1 kpc from the centre at 1.5 times the escape speed,
  its disc tilted 30°;
- the scene is set in the target's rest frame, with the target at the
  origin;
- in the viewer the camera moves with the target as it drifts.

### 5.3 Self-gravity replay (key 3)

Implemented (`replay.py`). The recording from §1.5 is played with
`sk.streams.play(engine, "Replay", "recordings/merger.npy", loop=True)`:
- The viewer sets `player.rate` each frame from the time scale (38 Myr a
  second at 1), and to 0 while paused.
- It keeps the playback's clock beside the player. From that, each star is
  moved on along its recorded velocity past its frame, so motion stays
  smooth between frames 4.7 Myr apart.
- Restart starts a new player from the first frame.

The discs here grow spiral arms and bars of their own, which the live model
can't; a card says which model is which. Without the recording, the viewer
prints how to make it and stays in its scene.

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
3. **Gas and star formation.** Implemented: the grid, both rules, young
   stars' glow, the `Stats` readback and the printed star formation rate;
   `check.py gas`.
4. **Ring galaxy scene.** Implemented: `model.ring`, the compressed-gas
   star formation, the grid's centre, scene keys `1` and `2`; `check.py
   ring`.
5. **Reference run and replay.** Implemented: `shaders/nbody.comp`,
   `precompute_replay.py`, `replay.py`, the ReplayStars pass and key `3`.
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

### 8.7 Gas and star formation

Defaults: `gas_relax` 0.5, `sf_density` 0.01 (10^8 solar masses per kpc^3,
above the quiet disc's densest 1 kpc cell, about 0.006), `sf_efficiency`
0.05, 10^10 solar masses of gas per galaxy.

- One step of relaxing toward the cells' mean velocity changes the gas's
  total momentum by 9e-8 of its total |momentum| (fixed point at 1/1024).
- An isolated galaxy forms about 2 solar masses of stars a year once its
  gas has settled (none in the first 118 Myr). The real Milky Way forms 1.5
  to 2. 32% of its gas forms stars in 2 Gyr.
- The merger, sampled every 118 Myr:

  | Myr | 0 | 118 | 236 | 354 | 472 | 590 | 708 | 826 | 944 | 1062 | 1180 | 1652 | 1770 |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|---|
  | M_sun/yr | 1.1 | 4.5 | 9.5 | 13.6 | 15.4 | 13.2 | 12.2 | 17.6 | 10.3 | 5.6 | 3.5 | 0.3 | 0.5 |

  It rises after the first pass (141 Myr) and peaks after the second
  (707 Myr), at about four times two quiet discs. It then falls as the gas
  runs out: 67% of the gas forms stars in 2 Gyr.
- The viewer's printed rate (50 Myr average from the readback) follows the
  same curve: 3.2 at 162 Myr, 14.6 at 540 Myr.
- **Frame rate:**
  - before the gas rules: 210-390 fps;
  - with them: 125-215 fps;
  - clearing the 32 MB grid 8 times a frame is most of the difference.
- **Colour:** at 400 Myr pink rings of new stars show around both cores. At
  the second pass, pink knots show inside the merging core.

### 8.8 The ring galaxy and compressed gas

With only the density threshold, almost no stars formed in the ring.

- **Densities, star formation off:** the ring raises the outer gas density
  about tenfold. Taking the densest cell in each band of radius:
  - 0.0008 rises to 0.0079 at 9-12 kpc (180 Myr);
  - 0.0004 rises to 0.0079 at 15-20 kpc (260 Myr).

  That is still under 0.01, the threshold.
- **Lower thresholds:**
  - at 0.005, 44 young stars formed beyond 6 kpc by 470 Myr;
  - at 0.003, 262; isolated galaxies formed 3 solar masses a year instead
    of 2.

  A threshold can't tell the ring from a disc's centre, which is always
  dense.
- **The compressed-gas term** (converging flow, as in Katz 1992), measured
  by radius at 470 Myr: young stars at 0-3/3-6/6-9/9-12/12-15/15-20/20-30
  kpc, with the star formation rate in solar masses a year (isolated galaxy
  over the first 118-Myr bins; merger peak; ring's first bin):

  | sf_compression | ring young stars by radius | isolated | merger peak | ring, first 118 Myr |
  |---|---|---|---|---|
  | 0 | 2587 / 25 / 0 / 0 / 0 / 0 / 0 | 0.0-2.3 | 17.6 (second pass) | 0.4 |
  | 0.1 | 3412 / 1711 / 1257 / 569 / 30 / 14 / 45 | 0.8-3.1 | 22.6 (first pass) | 6.9 |
  | 0.3 | 4745 / 4224 / 3089 / 1447 / 90 / 32 / 128 | 2.1-4.0 | 32.7 (first pass) | 17.6 |
  | 1.0 | 6049 / 7597 / 5341 / 2883 / 283 / 90 / 277 | 6.2-6.6 | 52.0, gas gone by 950 Myr | 37.4 |

  At 1.0 the isolated galaxy's start (13k gas particles, noisy cell means)
  and the merger's first pass use up the gas. 0.3 is the default.
- **The grid's centre.** Centred on all the galaxies' centre of mass, the
  grid followed the companion as it left. By 420 Myr the centre was 63 kpc
  from the target, and the target's gas fell off the grid's edge. Centring
  on the galaxies near the heaviest raised the ring's late star formation
  (third 118-Myr bin) from 1.6 to 3.5 solar masses a year.
- **The rings** (`check.py ring`, 256k particles): the target's disc stars
  are most overdense at 7.5 kpc at 100 Myr and 11.5 kpc at 150 Myr, which
  is 78 km/s outward (the Cartwheel's ring expands at about 53 km/s).
  Second and third rings follow inside, at 6 kpc at 200 Myr and 10 kpc at
  300 Myr.
- **The zero-momentum frame** moved the target at 0.84 units (174 km/s),
  and it left the viewer's camera. The scene is now set in the target's
  rest frame, and the camera follows its remaining drift.
- **In the viewer:** two bright rings with pink knots of new stars, from
  320 to 770 Myr, at 210-220 fps.

### 8.9 The self-gravitating reference run

- **Speed:** nbody.comp's force pass (`inversesqrt`, tiles of 256 in shared
  memory) sums 450 billion pairs a second at 16k bodies and 534 billion at
  65k: 8 ms a step. The 0.2 s a step estimated in §1.5's plan was 25 times
  too slow, so the run uses 131,072 bodies (32 ms a step).
- **The run:** 1.5 Gyr, 6364 steps of 0.05, softening 0.2 kpc, takes 208 s.
  The galaxies' centres are the median of each galaxy's bulge stars.
  - pericentres: 141 Myr at 8.5 kpc, 382 at 1.5, 429 at 1.1;
  - merged (within 1 kpc for good) at 462 Myr;
  - total momentum changes by 1.6e-4 of the sum of |m v|.
- **Against the live model:** passes at 141 and 707 Myr, merged at 895
  (§8.2). The live halos, deforming and dragging on each other, merge the
  galaxies in half the time of the tuned friction rule.
- **What it shows:**
  - by 98 Myr, both discs have grown spiral arms of their own;
  - at 249 Myr, long tails and a bridge;
  - at 281-348 Myr, barred, elongated cores;
  - from 450 Myr, one elongated remnant.
- **Playback:** 108 frames played by 511 Myr (511 / 4.7 Myr a frame), none
  late, 2400 frames a second. The first recording held positions alone. At
  time scale 1 (38 Myr a second) that is 8 frames a second; recording
  velocities and moving each star on between frames doubled the file to
  334 MB.

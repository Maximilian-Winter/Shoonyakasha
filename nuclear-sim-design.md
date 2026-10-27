# Nuclear Force Simulation — Design Document

## Overview

A GPU-driven nuclear physics simulation for the Shoonyakasha Vulkan engine. The goal is **believable, entertaining, and educational** behavior — not research-grade accuracy. The simulation uses classical particle dynamics with tuned potentials that reproduce the emergent behavior of nuclear binding, fission, fusion, and chain reactions.

The core insight: all of nuclear physics at the visible scale comes from **two competing forces** — the strong nuclear force (short-range attraction) and Coulomb repulsion (long-range, protons only). Tune these correctly and fission, fusion, stability, and instability all emerge naturally from the simulation without special-case code.

---

## 1. Physics Model

### 1.1 The Two Forces

**Strong Nuclear Analog — Morse Potential**

A short-range interaction between all nucleon pairs (proton–proton, proton–neutron, neutron–neutron), with the repulsive core and the attraction weighted separately:

```
V(r) = D_core * exp(-2a(r - r₀)) - 2 * D * exp(-a(r - r₀))
D = D_unlike for a proton–neutron pair, D_like for two of the same kind (D_like < D_unlike)
```

- `D_core` — strength of the repulsive core, the same for every pair
- `D_unlike`, `D_like` — attraction depth (binding strength, in simulation energy units)
- `a` — controls the width of the potential well
- `r₀` — equilibrium distance (where nucleons "want" to sit)
- Behavior: hard-core repulsion at r < r₀, attraction at r₀ < r < ~3r₀, negligible beyond ~3r₀
- With `D = D_core` (every proton–neutron pair) this is the plain Morse potential

The corresponding force (negative gradient):

```
F(r) = 2a * (D_core * exp(-2a(r - r₀)) - D * exp(-a(r - r₀)))
```

Weaker like-pair attraction is the model's symmetry energy: without it a cluster of neutrons binds more tightly than iron, and nuclei absorb neutrons without limit (see §9). A shared core keeps nucleons of any kind from overlapping; when one depth scales both core and attraction, weakening like pairs also softens their core and the clusters drift towards neutrons.

This is the force that holds nuclei together. Its short range means each nucleon only binds to its immediate neighbors — this is crucial for reproducing the "surface tension" effect that makes nuclei behave like liquid drops.

**Coulomb Repulsion — Protons Only**

```
F_coulomb(r) = k_c / r²    (repulsive, proton–proton pairs only)
```

- `k_c` — Coulomb constant (tunable simulation parameter)
- Long-range: falls off as 1/r² but never reaches zero
- This is what destabilizes large nuclei — more protons means more cumulative repulsion

### 1.2 Why This Produces Real Nuclear Behavior

| Phenomenon | How it emerges |
|---|---|
| **Nuclear binding** | Morse attraction pulls nucleons together; hard core prevents collapse; equilibrium cluster forms |
| **Nuclear saturation** | Short range of Morse means each nucleon only feels ~12 neighbors, so binding energy scales with surface area, not volume squared |
| **Small nucleus stability** | Few protons → Coulomb repulsion is negligible → strong force wins easily |
| **Large nucleus instability** | Many protons → Coulomb repulsion (long-range, all pairs) eventually rivals binding energy → nucleus becomes fragile |
| **Fission** | Perturb a large nucleus (e.g., neutron impact) → oscillation → elongation → neck thins → Coulomb pushes halves apart → snap |
| **Fusion** | Two small nuclei approach → Coulomb barrier repels at distance → if kinetic energy is high enough, they get within Morse range → strong force grabs and binds them |
| **Chain reaction** | Fission ejects free neutrons → if they hit another large nucleus → more fission → exponential cascade |
| **Binding energy curve** | Iron-56 region is most tightly bound per nucleon. Below iron: fusion releases energy. Above iron: fission releases energy. Emerges from the balance of Morse vs Coulomb at different cluster sizes |

### 1.3 Damping

Without energy dissipation, nuclei vibrate forever. Add a velocity damping term:

```
v_new = v_old * (1.0 - damping * dt)
```

Use damping only to cool nuclei into their ground states during precomputation (`damping = 1.0`). Run scenarios without it: damping drains the energy a captured neutron brings before it can deform the nucleus.

---

## 2. Particle Data

### 2.1 Nucleon Buffer

```cpp
struct Nucleon {
    vec3  position;      // world position
    float type;          // 0.0 = neutron, 1.0 = proton
    vec3  velocity;      // current velocity
    float energy;        // kinetic energy (for visual glow)
    vec3  force;         // accumulated force this frame
    uint  nucleus_id;    // cluster ID, 0 = free particle
};
```

Stride: 48 bytes, GPU-friendly alignment.

### 2.2 Nucleus Buffer (Aggregate Data)

```cpp
struct Nucleus {
    vec3  center_of_mass;
    float total_mass;
    float total_charge;     // number of protons
    float binding_energy;   // sum of potential energies
    float deformation;      // elongation measure (max/min axis ratio)
    uint  nucleon_count;
};
```

Updated each frame from the nucleon buffer via a reduction pass. The `deformation` value is key for detecting imminent fission — when it exceeds a threshold, the nucleus is about to split.

---

## 3. GPU Compute Pipeline

Two compute dispatches per simulation step, both of [step.comp](examples/python/simulations/nuclear/shaders/step.comp): all-pairs forces (even mode), then integration (odd mode). The viewer's render graph runs 8 steps a frame.

**Implemented: all pairs.** Forces are summed over every pair, a shared-memory tile of 128 nucleons at a time. Coulomb must reach every proton of the *same* nucleus as well as other nuclei: a U-238 nucleus is about 6·r₀ across, twice the 3·r₀ cutoff of §3.2, and the Coulomb energy that destabilizes heavy nuclei comes from those distant pairs inside it. The chain-reaction scenes (tens of uranium nuclei, ~10⁴ nucleons) are within reach of all pairs. The spatial hash (§3.1) and the centre-of-mass approximation (§3.2) stay a later option for much larger scenes, for the Morse term only.

### 3.1 Pass 1 — Spatial Hash Construction (deferred)

**Purpose:** Build a uniform grid for fast neighbor lookup.

- Divide the simulation volume into cells of size `~3 * r₀` (the Morse cutoff distance)
- Each nucleon hashes to a cell: `cell = floor(position / cell_size)`
- GPU counting sort: count per cell → prefix sum → scatter nucleons into sorted buffer
- Output: sorted nucleon buffer + cell offset table

**Complexity:** O(N), fully parallel.

**Notes:** Standard GPU spatial hashing. The cell size matches the strong force cutoff, so neighbor search only needs to check the 27 adjacent cells (3³ neighborhood).

### 3.2 Pass 2 — Force Accumulation

**Purpose:** Compute the net force on each nucleon.

For each nucleon:
1. Look up its grid cell
2. Iterate all nucleons in the 27 neighboring cells
3. For each neighbor within cutoff distance:
   - Compute Morse force (all pairs)
   - If both are protons: add Coulomb repulsion
4. For distant Coulomb: use nucleus center-of-mass approximation
   - Each proton feels repulsion from other nuclei's total charge at their center of mass
   - This avoids O(N²) for long-range Coulomb while keeping the destabilizing effect
5. Write accumulated force to the nucleon's `force` field

**Shader pseudocode:**

```glsl
// Per nucleon
vec3 total_force = vec3(0.0);
vec3 my_pos = nucleons[my_id].position;
float my_type = nucleons[my_id].type;

// Short-range: iterate neighbor cells
for each neighbor in nearby_cells {
    vec3 r_vec = neighbor.position - my_pos;
    float r = length(r_vec);
    vec3 r_hat = r_vec / r;

    if (r < cutoff && r > epsilon) {
        // Morse force
        float exp1 = exp(-a * (r - r0));
        float exp2 = exp1 * exp1;
        float f_morse = 2.0 * D * a * (exp2 - exp1);
        total_force += f_morse * r_hat;

        // Coulomb (proton-proton only)
        if (my_type > 0.5 && neighbor.type > 0.5) {
            float f_coulomb = k_c / (r * r);
            total_force += f_coulomb * r_hat;  // repulsive
        }
    }
}

// Long-range Coulomb from other nuclei (approximate)
if (my_type > 0.5) {  // proton
    for each nucleus != my_nucleus {
        vec3 r_vec = nucleus.center_of_mass - my_pos;
        float r = length(r_vec);
        float f = k_c * nucleus.total_charge / (r * r);
        total_force += f * normalize(r_vec);  // repulsive
    }
}

nucleons[my_id].force = total_force;
```

### 3.3 Pass 3 — Integration (semi-implicit Euler)

Velocity from the force, then position from the new velocity: semi-implicit (symplectic) Euler, not velocity Verlet. It conserves energy well at `dt = 0.02` (measured drift under 0.05 of ~450 units over 40,000 steps).

**Purpose:** Update positions and velocities from accumulated forces.

```glsl
vec3 acceleration = nucleon.force / nucleon_mass;

// Velocity Verlet
nucleon.velocity += acceleration * dt;
nucleon.velocity *= (1.0 - damping * dt);  // dissipation
nucleon.position += nucleon.velocity * dt;

// Update kinetic energy for visual effects
nucleon.energy = 0.5 * dot(nucleon.velocity, nucleon.velocity);
```

### 3.4 Pass 4 — Cluster Analysis (Optional, Lower Frequency)

Run every N frames, not every frame. Uses a connected-component algorithm on the spatial hash to assign `nucleus_id` values and compute aggregate nucleus data.

- Two nucleons are "bound" if their separation < 1.5 * r₀ and their relative kinetic energy < binding threshold
- Connected components via GPU label propagation
- Reduce per-nucleus: center of mass, charge, deformation tensor
- Deformation = ratio of largest to smallest eigenvalue of the position covariance matrix within the cluster

---

## 4. Precomputation

### 4.1 Stable Nuclear Configurations

Run the simulation offline with high damping to find minimum-energy arrangements for common nuclei:

| Config | Protons | Neutrons | Total | Notes |
|---|---|---|---|---|
| Hydrogen-1 | 1 | 0 | 1 | Single proton |
| Deuterium | 1 | 1 | 2 | Simplest bound state |
| Tritium | 1 | 2 | 3 | |
| Helium-3 | 2 | 1 | 3 | |
| Helium-4 (alpha) | 2 | 2 | 4 | Very tightly bound, important |
| Carbon-12 | 6 | 6 | 12 | |
| Iron-56 | 26 | 30 | 56 | Peak binding energy per nucleon |
| Uranium-235 | 92 | 143 | 235 | Fissile |
| Uranium-238 | 92 | 146 | 238 | Fertile |

**Process:**
1. Place N nucleons randomly in a small sphere
2. Run simulation with damping = 0.5 for ~10,000 steps
3. Once total kinetic energy drops below threshold, configuration is stable
4. Save relative positions to binary file

**File format:** `configs/<name>.npy`, one row of `(x, y, z, type)` float32 per nucleon relative to the centre of mass (type 1 proton, 0 neutron), and `configs/index.json` with each nucleus's Z, N and binding energy and the parameters used ([precompute.py](examples/python/simulations/nuclear/precompute.py)). Nucleons start on a compact face-centred cubic lattice rather than at random: random starts leave voids and stray nucleons.

### 4.2 Binding Energy Curve

After generating all configs, compute binding energy per nucleon for each. Plot this — it should roughly match the real curve (peak at iron, rising for light elements, declining for heavy). If it doesn't, adjust `D`, `a`, `r₀`, and `k_c` until it does.

---

## 5. Simulation Scenarios

### 5.1 Fission Demo

1. Spawn a Uranium-235 nucleus from precomputed config
2. Fire a slow neutron at it from a distance
3. Neutron is absorbed → nucleus gains energy → starts oscillating
4. Deformation increases → neck forms → snaps into two fragments
5. 2–3 free neutrons are ejected
6. Fragments fly apart with high kinetic energy (Coulomb repulsion of ~46 vs ~46 protons)

### 5.2 Chain Reaction

1. Spawn a grid/cluster of Uranium-235 nuclei with some spacing
2. Fire one neutron at one nucleus
3. If spacing is right (critical density), ejected neutrons hit other nuclei
4. Exponential cascade → energy release
5. If spacing is too large (subcritical), chain dies out
6. Visual: expanding wave of fission events

### 5.3 Fusion Demo

1. Spawn two Deuterium nuclei separated by some distance
2. Give them velocity toward each other
3. At low velocity: Coulomb barrier bounces them apart
4. At high velocity: they merge into Helium-3 or Helium-4 + released neutron
5. Energy release visible as high kinetic energy of products

### 5.4 Stellar Core

1. Fill a volume with hydrogen (single protons)
2. Apply an inward gravitational force (simulating stellar gravity)
3. Compression raises collision energy
4. Fusion ignites when collision energies exceed Coulomb barrier
5. Outward pressure from fusion products vs inward gravity → equilibrium
6. When fuel depletes → collapse → heavier element fusion → iron core → no more energy → final collapse

---

## 6. Rendering

### 6.1 Nucleon Rendering

- Instanced spheres from the nucleon SSBO
- Protons: warm color (red/orange)
- Neutrons: cool color (blue/grey)
- Glow intensity proportional to `energy` field
- Size: all nucleons same radius (they are in reality nearly identical in size)

### 6.2 Free Neutron Trails

- Free neutrons (nucleus_id == 0) with velocity above threshold get a motion trail
- Render as a short line or ribbon in the velocity direction
- Makes chain reactions visually readable — you can follow the neutrons

**Implemented:** the viewer marks the neutrons alone in their group at each readback, and a `Trails` pass draws a camera-facing ribbon from each marked neutron back to x − v·6 time units, the straight path it has covered, fading toward the tail.

### 6.3 Energy Release Effects

- When a nucleus splits (fission detected via deformation threshold), spawn a point light flash at the split location
- The flash intensity scales with the energy released (difference in binding energy before and after)
- Bloom post-process catches this and creates the visual "flash"

**Implemented without bloom:** a `Flashes` pass draws each flash as a soft additive disc that grows and fades over 30 time units of simulation time. A split's flash sits at the split nucleus's centre with strength equal to the energy the emission rule radiates (at least 1); a fusion reaction's sits at its largest product with strength 1.

### 6.4 Scale Bridging (Optional — Macro Effects)

For connecting the nuclear simulation to the planet-scale engine:

- A nuclear detonation simulation at nucleon scale produces a total energy output
- Feed this energy as a point heat source into a fluid/particle simulation at macro scale
- The hot gas bubble rises through the atmospheric density model
- Rayleigh-Taylor instability at the bubble boundary creates the mushroom shape naturally
- This bridges the femtometer-scale nuclear sim with the kilometer-scale atmospheric sim

---

## 7. Parameter Tuning Guide

The four key parameters and what they control:

| Parameter | Symbol | Effect when increased |
|---|---|---|
| Morse depth | `D` | Stronger binding → nuclei hold together more aggressively → harder to fission |
| Morse width | `a` | Sharper/narrower potential well → nucleons sit more rigidly → less liquid-drop behavior |
| Equilibrium distance | `r₀` | Larger nuclei for same nucleon count → changes density |
| Coulomb constant | `k_c` | Stronger proton repulsion → large nuclei become unstable at lower Z → shifts the fission threshold |

**Tuning target:** The binding energy per nucleon curve, and the valley of stability (the most bound Z at fixed A).

**Tuned values:** `D_core = 1.0`, `D_unlike = 1.0`, `D_like = 0.1`, `a = 4.0`, `r₀ = 1.0`, `k_c = 0.16` (see §9). The steps below are the original starting point.

1. Start with `D = 1.0`, `a = 2.0`, `r₀ = 1.0`, `k_c = 0.1` (measured: collapses; see §9)
2. Generate stable configs for He-4, C-12, Fe-56, U-238
3. Compute binding energy per nucleon for each
4. Adjust until:
   - He-4 is bound but not as tightly as Fe-56 per nucleon
   - Fe-56 is the peak
   - U-238 is less bound per nucleon than Fe-56
   - U-238 is *marginally* stable (high deformation oscillations)
5. Test: firing a neutron into U-238 should cause fission. If it doesn't, increase `k_c` or decrease `D` slightly

---

## 8. Implementation Order

1. **Nucleon buffer + spatial hash** — get GPU particles with neighbor finding working
2. **Morse potential only** — no Coulomb yet, just see particles clump into blobs
3. **Add Coulomb** — protons repel, watch clusters settle into neutron-rich configurations
4. **Precompute configs** — generate and save stable nuclei
5. **Fission test** — spawn U-235, fire neutron, see if it splits
6. **Tune potentials** — iterate on parameters until behavior is right
7. **Chain reaction** — multiple nuclei, watch cascade
8. **Fusion test** — deuterium-deuterium at varying energies
9. **Visual polish** — glow, trails, flash effects
10. **Scale bridge** — connect energy output to macro-scale effects in the planet engine

**Status:** steps 2–6 are done (all pairs, so no spatial hash): the realistic model, the scenario setting (§9.4), neutron capture and emission by rule (§9.5, §9.6), and the runtime viewer with the fission demo. Step 7 runs with a capture radius by rule (§9.9, §9.10), step 8 needs no rule (§9.11), and step 9 is done (§6.2, §6.3). Next: step 10.

---

## 9. Measurements

Measured with the exact all-pairs model ([examples/python/simulations/nuclear](examples/python/simulations/nuclear)), nuclei cooled from a lattice with damping.

### 9.1 The starting values collapse

With `D = 1`, `a = 2` (`a·r₀ = 2`) the core is soft: V(0) is only ~40·D. In large clusters the attraction of many neighbours pushes nucleons through it: in U-238 nearest neighbours end up 0.47·r₀ apart, and binding per nucleon grows with size without limit (He-4 1.5, Fe-56 13.7, U-238 30.6). There is no saturation and no peak. At `a = 4` spacing stays near r₀ and the curve saturates.

### 9.2 Symmetry energy

With the same attraction for every pair, 56 neutrons bind more tightly than Fe-56 (3.65 against 3.46 per nucleon at `a = 4`): neutrons are only ever attracted. Weaker like-pair attraction fixes this; with a shared core (§1.1) and `D_like = 0.1`, `k_c = 0.16`:

| | Model | Real |
|---|---|---|
| Peak of binding energy per nucleon | A = 56 | A = 56 |
| U-238 / Fe-56 binding per nucleon | 0.89 | 0.86 |
| Most bound Z at A = 56 | ~24 | 25 |
| Most bound Z at A = 238 | ~89 | 92 |

One simulation energy unit is about 4.2 MeV (the peak of 2.10 units a nucleon against iron's 8.79 MeV). Light nuclei come out under-bound (He-4 ~4 MeV a nucleon against 7.1), and helium-4's extra stability, magic numbers and the U-235/U-238 difference do not appear: they come from quantum effects a classical model does not have.

### 9.3 Fission and the binding curve cannot share one setting

With the tuned values, a slow or fast neutron (up to 8 units, ~34 MeV) is captured by U-235 and the nucleus oscillates without splitting for 800 time units. Raising `k_c`:

| `k_c` | U-235 binding / A | No neutron | Slow neutron (0.3) | Fast neutron (2.0) |
|---|---|---|---|---|
| 0.24 – 0.35 | 1.46 – 0.86 | holds | holds | holds |
| 0.36 | 0.81 | holds (400 time units) | splits at t ≈ 100: 124 + 112 | splits: 156 + 80 |

At `k_c = 0.36` fission is neutron-induced, but the heavier fragment can split again, the curve's peak moves to A = 20 and U-238 / Fe-56 falls to 0.50. In a classical liquid drop one ratio, Coulomb against surface energy, sets both the curve's peak and how close uranium is to splitting: a realistic curve means a realistic barrier, which a classical nucleus crosses about once in ~10⁹ vibrations.

No neutrons were emitted by the fragments in 800 time units; one proton was.

### 9.4 Two settings

- **Realistic** (`k_c = 0.16`, no screening): the binding curve and the stable nuclei.
- **Scenario** (`k_c = 0.36`, Coulomb screened from 9 to 11, §9.7): the viewer's setting. Uranium for scenarios is cooled under it, and kept only if it stays whole for 400 time units without damping: damping during cooling can hold together a nucleus that falls apart once it stops.

Cooled that way, scenario U-235 (binding 0.78 a nucleon) is stable alone: 27 copies with slight random motion all stay whole for 1500 time units, and a lone copy for 1600. Without a rule it also stays whole after capturing a neutron. At `k_c` 0.32 and 0.34 it is just as stable alone, and with the capture rule splits less often (6 and 4 of 8 against 8 of 8).

### 9.5 Neutron emission: weaker neutron–neutron attraction does not do it

Neutron–neutron attraction `D_nn` was lowered below proton–proton (`D_pp = 0.1`), so neutron-rich fragments might drip neutrons. Scanned over `k_c` 0.30–0.365 and `D_nn` 0.10 to −0.20, then six trials (different cooled nuclei and impact points) for the closest settings, 600 time units each:

| `k_c`, `D_nn` | Cooled whole | Left alone | Hit by a slow neutron | Free neutrons |
|---|---|---|---|---|
| 0.355, 0.06 | 3 of 6 | 2 of 3 fall apart | 2–5 pieces | 1, once |
| 0.360, 0.08 | 4 of 6 | 2 of 4 fall apart | 1–4 pieces | 0–1 |
| 0.360, 0.10 | 4 of 6 | all hold | 2 of 4 split (2 and 3 pieces) | none |

A weaker `D_nn` loosens the whole nucleus rather than its excess neutrons: each neutron stays held by its proton neighbours, and uranium breaks into several pieces, alone or not, before neutrons come off. At `D_nn ≤ 0` uranium cannot be cooled whole. Emitting 2–3 neutrons per fission does not emerge from these potentials; it needs a rule (a nucleus that splits gives off neutrons from its fragments) or another mechanism.

The viewer uses a rule (`events.emission_kicks`): each fragment gives off its outermost neutron at 2.5 units relative to the fragment, the rest of the fragment recoils so momentum is unchanged, and the neutron is paid for from the fragment's internal kinetic energy, half of what remains then being radiated. Without radiation the total energy changes by less than 0.001.

### 9.6 Capture: the compound nucleus stretches by rule

Scenario U-235 takes in a slow neutron and keeps it, but does not split: capture at 1 unit leaves 5.3 units of internal kinetic energy in the 236-nucleon nucleus (6.2 at 2 units), spread over all its motion. A stretch along one axis, v ∝ (x·u) u, splits the cold nucleus when it carries enough energy:

| Stretch energy | 2 | 4 | 8 | 12 | 16 – 32 |
|---|---|---|---|---|---|
| Splits (3 axes) | 0 | 1 | 1 | 3 | 3 |

A rule (`events.CaptureRule`): a nucleus of at least 200 nucleons that takes in nucleons from a group of at most 4 turns its internal kinetic energy into a stretch along its longest axis, energy and momentum unchanged. How often it does so decides the outcome (all of the energy each time, looks 2 time units apart, impact point and orientation random):

| Stretch | Split |
|---|---|
| once | 0 of 6 |
| every 8 time units, 4 or 8 times | 2 and 4 of 6 |
| every 2 time units, only while the nucleus is lengthening | 10 of 10 |

A kicked nucleus lengthens for only about 1.4 time units before it rebounds, so the stretches must come within that phase, and in the viewer a kick lands up to a few frames after the look it was worked out from (0.64 time units at 37 frames a second). Measured on one captured nucleus, four slight variations each, at different look intervals and delays:

| Rule | Looks 0.16 apart, no delay | 0.64, none | 0.64, delay 0.8 | 1.0, delay 1.6 |
|---|---|---|---|---|
| All the energy, at most every 0.5 after the last kick landed, when lengthening at landing | 4 of 4 | 0 of 4 | 4 of 4 | 4 of 4 |
| Half the energy, the same | 4 of 4 | 4 of 4 | 4 of 4 | 4 of 4 |

The rule of the table above (all the energy, at most every 2 time units, when lengthening at the look) splits this nucleus 4 of 4 offline with looks 0.64 apart and delays of 0.8 to 3.2, but in the engine only after 100 time units or more, or not within 200: there its stretches fall once a cycle. A stretch replaces the nucleus's internal motion, so one worked out from a look taken before the last kick landed undoes it; and whether the nucleus is lengthening when the kick lands is carried forward from the look with the forces it holds (`events.stretching`). The last row is the rule, for up to 200 time units after capture. Under the scenario setting uranium then splits 37–66 time units after capture, 10 of 10 times in two pieces (e.g. 130 + 106, 152 + 84, 186 + 50); left alone it does not split. Each stretch changes the kinetic energy by at most 4·10⁻⁶.

Afterwards the heavier fragment sometimes splits again 10–25 time units later: with emission, 3–4 of 6 runs end as two fragments and the others as three. Radiating 90 % of the fragments' internal energy instead of 50 % does not change this.

### 9.7 Nuclei side by side: screening

Bare nuclei repel each other: 27 scenario U-235 nuclei 11 apart blow apart to a radius of 1500 within 300 time units and tear into 45 pieces; with `isolate` (no forces between nuclei) all 27 stay whole. Real nuclei sit inside atoms whose electrons screen this charge. Coulomb is screened in the scenario setting: `S(r)` falls smoothly from 1 at `r = 9` (U-235's protons are at most 8.6 apart) to 0 at 11.

The screening has to end before neighbouring surfaces meet. With it ending at 12 and nuclei 17 apart (surfaces 7.8 apart), a 4 × 4 × 4 lattice conserves energy (drift 0.5 in 100 time units) but the nuclei's mutual Coulomb energy drains into their internal motion, 48 to 346 units in 150 time units, until they split. Ending at 10 with nuclei 20 apart, the lattice has no internal motion after 400 time units; ending at 11, capture splits more often in two pieces than ending at 10 (9 against 7 of 10). The viewer places nuclei 21 apart.

### 9.8 A fixed time step

Semi-implicit Euler conserves energy at a fixed step. Changing the step every few steps does not: the 27-nucleus lattice, stepped with a random step between 0.003 and 0.02 every 8 steps, heats from 53 to 96 units of kinetic energy in 200 time units, where at a fixed step it levels off near 70; in the viewer its nuclei then split. The viewer always steps 0.02 and paces by running a frame's steps or not.

### 9.9 Chain reaction

64 U-235 nuclei, 4 a side and 21 apart, and one neutron at 1 unit, without the rule of §9.10: the first nucleus splits, but in 464 time units none of the 4 neutrons it gives off is captured. Without the neutron no nucleus splits in 403 time units. A neutron passes about 5–6 spacings between nuclei (mean free path d³ / πR², R ≈ 5) while the cube is 3 spacings across, so most neutrons leave it. Real slow neutrons are captured by U-235 over a cross-section a few hundred times its geometric size, a wave effect a classical point neutron does not have.

### 9.10 Capture radius

A rule (`events.steer_kicks`): a free neutron (alone in its group) within 10 of the centre of a nucleus of at least 200 nucleons is turned toward that centre. The turn is elastic between the neutron and the nucleus: in their centre-of-mass frame the relative velocity keeps its length and points at the centre, so momentum and energy are unchanged (to 2·10⁻⁷) and the nucleus recoils. A neutron at 2.5 units passing 8 from a uranium's centre is turned and captured, and the uranium splits (5 of 5); one passing 12 away is left alone.

With it the mean free path is d³ / π·10², about 1.4 spacings at 21. In the viewer (64 nuclei, 4 a side):

| Spacing | Neutron | After 80 s (about 410 time units) |
|---|---|---|
| 21 | at 1 unit | 8 captures, 19 splits, still rising (13 splits at 350) |
| 21 | at rest | nothing |
| 30 | at 1 unit | the first nucleus splits; none of its neutrons is captured |

Splits outnumber captures because heavy fragments sometimes split again (§9.6).

### 9.11 Fusion

Light nuclei need no rule: the potentials alone give a Coulomb barrier, fusion, and the real reaction channels. Two scenario nuclei head on, their total momentum zero, 16 random orientations each, products after 150–600 time units (groups linked closer than 3):

| D + D, each at | Products |
|---|---|
| 0.22 | D + D 14, He-4 2 |
| 0.25 | He-4 16 |
| 0.30 | He-4 8, D + D 8 |
| 0.40 | D + D 9, T + p 4, He-3 + n 2, He-4 1 |
| 0.50 | D + D 7, T + p 6, He-3 + n 3 |
| 0.80 | D + D 13, T + p 2, He-3 + n 1 |
| 1.0 (8 orientations) | D + D 5, D + n + p 3 |
| 2.0 – 3.0 (8 orientations) | mostly free nucleons |

| D + T, closing at | Products |
|---|---|
| 0.30 | D + T 16 |
| 0.40 | D + T 15, He-4 + n 1 |
| 0.50 | He-4 + n 16 |
| 0.60 – 1.0 | He-4 + n 13–14, the rest D + T or other |
| 1.4 | He-4 + n 8, D + T 7 |
| 2.0 | D + D + n 10, D + T 4, T + n + p 2 |

Below the barrier the nuclei bounce. Above it they merge into a hot compound nucleus: D + D into He-4, which at higher energy gives off a nucleon as T + p or He-3 + n, the two real D + D channels; D + T into He-5, which gives off a neutron, leaving He-4. Faster still, the nuclei break up. The energy released is small because the model's light nuclei are under-bound (§9.2): scenario D binds 1.00 units, T 2.01, He-4 3.76, so D + T → He-4 + n releases 0.75 units (real: 17.6 MeV, about 4.2 units), and D + D → T + p releases 0.01 (real: 4.0 MeV). The neutron from D + T at 0.5 leaves at 0.15–0.73; most of the energy stays in the He-4 as internal motion, which a real nucleus would lose as a gamma ray.

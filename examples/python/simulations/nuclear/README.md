# Nuclear force simulation

Nucleons as classical particles under two forces, a short-range attraction
and the protons' Coulomb repulsion, run on the GPU with `sk.ComputeContext`.
The design is in [nuclear-sim-design.md](../../../../nuclear-sim-design.md).
This folder holds the physics and the tools that tune it and make the nuclei
the scenarios start from; it needs no window.

```sh
python binding_curve.py          # binding energy per nucleon against mass number -> output/binding_curve.png
python precompute.py             # ground states of H-1 ... U-238 -> configs/
```

## The model

[physics.py](physics.py) holds the constants (`Params`) and the nucleon record
(`NUCLEON`, 48 bytes: position, type, velocity, kinetic energy, force, nucleus
id). Every nucleon has mass 1; lengths are in units of `r0`.

[shaders/forces.comp](shaders/forces.comp) sums, over every pair of nucleons,
a tile of 128 at a time:

```text
strong   V(r) = d_core e^(-2a(r - r0)) - 2 D e^(-a(r - r0))    D = d_unlike (proton-neutron)
                                                               D = d_like   (two of a kind)
Coulomb  V(r) = kc / r                                          protons only
```

The repulsive core is the same for every pair; only the attraction depends
on the pair. It also writes each nucleon's share of the potential energy, and
with `isolate` it only lets nucleons with the same nucleus id feel each other,
so many nuclei can be cooled side by side in one buffer.
[shaders/integrate.comp](shaders/integrate.comp) steps velocities from forces
and positions from velocities (semi-implicit Euler), with optional damping.
`physics.Simulator` runs the two kernels on buffers that stay on the GPU.

The defaults (`d_core` 1, `d_unlike` 1, `d_like` 0.1, `a` 4, `kc` 0.16) were
tuned with `binding_curve.py` and isobar scans against the real nuclei:

| | Model | Real |
|---|---|---|
| Peak of binding energy per nucleon | A = 56 | iron, A = 56 |
| U-238 against Fe-56, per nucleon | 0.89 | 0.86 |
| Most bound Z at A = 56 | about 24 | 25 |
| Most bound Z at A = 238 | about 89 | 92 |

The model's peak is 2.10 units a nucleon against iron's 8.79 MeV: one unit is
about 4.2 MeV.

## The tools

**`binding_curve.py`** cools one nucleus per mass number (with the valley of
stability's proton number, from several proton arrangements, keeping the most
bound) and plots binding energy per nucleon beside the semi-empirical mass
formula scaled to the model's peak. `--d-like`, `--kc`, `--a` and `--d-unlike`
try other parameters.

**`precompute.py`** cools the nuclei in `NUCLEI` the same way, from `--seeds`
proton arrangements, and keeps the most bound arrangement that lost no
nucleon. It writes `configs/<name>.npy`, one row of (x, y, z, type) per
nucleon relative to the centre of mass, and `configs/index.json` with each
nucleus's Z, N and binding energy and the parameters used.

`physics.relax_batch` does the cooling: nucleons start on a compact
face-centred cubic lattice, the protons at random sites, and are stepped with
damping until they settle.

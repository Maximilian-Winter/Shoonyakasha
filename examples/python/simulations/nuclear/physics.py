"""The nucleon model shared by the nuclear simulation's scripts.

Nucleons are point particles of equal mass. A Morse potential binds every
pair at short range: its repulsive core is the same for every pair, its
attraction stronger between a proton and a neutron than between two of the
same kind. Protons repel each other by Coulomb's law at every range.
forces.comp sums both over all pairs; integrate.comp steps the motion.

Nucleons are kept as NUCLEON records (48 bytes, the shaders' Nucleon struct):
position, type (0 neutron, 1 proton), velocity, kinetic energy, force, and
the id of the nucleus it was placed in.
"""

import struct
from dataclasses import dataclass, replace
from pathlib import Path

import numpy as np
import shoonyakasha as sk

HERE = Path(__file__).resolve().parent
SHADERS = HERE / "shaders"

NUCLEON = np.dtype([("position", "<f4", 3), ("type", "<f4"), ("velocity", "<f4", 3), ("energy", "<f4"),
                    ("force", "<f4", 3), ("nucleus", "<u4")])
assert NUCLEON.itemsize == 48


@dataclass(frozen=True)
class Params:
    """The model's constants, in simulation units (r0 = 1, nucleon mass = 1)."""
    d_core: float = 1.0       # Morse core strength, every pair
    d_unlike: float = 1.0     # Morse attraction, proton-neutron
    d_like: float = 0.1       # Morse attraction, proton-proton and neutron-neutron
    a: float = 4.0            # Morse width: larger is narrower and stiffer
    r0: float = 1.0           # equilibrium distance
    kc: float = 0.16          # Coulomb constant
    mass: float = 1.0
    min_distance: float = 0.05

    def with_(self, **changes):
        return replace(self, **changes)

    def forces_push(self, count, isolate):
        return struct.pack("<7f2I", self.d_core, self.d_unlike, self.d_like, self.a, self.r0, self.kc, self.min_distance,
                           count, 1 if isolate else 0)


def fcc_points(count, spacing=1.0):
    """The `count` points of a face-centred cubic lattice nearest its centre,
    nearest neighbours `spacing` apart: a compact, roughly round cluster."""
    side = int(np.ceil((count * 2) ** (1 / 3))) + 2
    g = np.arange(-side, side + 1)
    x, y, z = np.meshgrid(g, g, g, indexing="ij")
    cells = np.stack([x.ravel(), y.ravel(), z.ravel()], axis=1)
    basis = np.array([[0, 0, 0], [0.5, 0.5, 0], [0.5, 0, 0.5], [0, 0.5, 0.5]])
    points = (cells[:, None, :] + basis[None, :, :]).reshape(-1, 3) * (spacing * np.sqrt(2.0))
    order = np.argsort(np.einsum("ij,ij->i", points, points), kind="stable")
    return points[order[:count]]


def make_nucleus(z, n, nucleus_id=1, center=(0.0, 0.0, 0.0), rng=None, params=Params()):
    """Z protons and N neutrons on a compact lattice around `center`, the
    protons at random lattice sites."""
    rng = rng or np.random.default_rng(0)
    count = z + n
    out = np.zeros(count, dtype=NUCLEON)
    out["position"] = fcc_points(count, params.r0) + np.asarray(center, dtype=np.float32)
    out["type"][rng.choice(count, z, replace=False)] = 1.0
    out["nucleus"] = nucleus_id
    return out


def valley_z(a):
    """A stable proton number for mass number `a` (the liquid drop model's
    valley of stability), rounded."""
    return max(1, int(round(a / (2.0 + 0.0153 * a ** (2.0 / 3.0)))))


def components(positions, bond):
    """Labels 0..k-1 of the groups that pairs closer than `bond` connect."""
    count = len(positions)
    d2 = ((positions[:, None, :] - positions[None, :, :]) ** 2).sum(-1)
    linked = d2 < bond * bond
    labels = -np.ones(count, dtype=np.int64)
    current = 0
    for start in range(count):
        if labels[start] >= 0:
            continue
        stack = [start]
        labels[start] = current
        while stack:
            i = stack.pop()
            for j in np.nonzero(linked[i] & (labels < 0))[0]:
                labels[j] = current
                stack.append(j)
        current += 1
    return labels


def binding_energy(potential):
    """The binding energy of nucleons at rest whose potential shares are
    `potential`: how much energy it takes to pull them all apart."""
    return 0.0 - float(np.sum(potential, dtype=np.float64))


def relax_batch(ctx, specs, params=Params(), steps=3000, dt=0.02, damping=1.0, seed=0, bond=1.6):
    """Cool each nucleus of `specs`, a list of (Z, N), into a ground state:
    started on a compact lattice with the protons at random sites, then
    stepped with damping, all side by side in one buffer without feeling
    each other. Returns one dict per nucleus: z, n, positions and types
    (centred on its centre of mass), binding energy, and `lost`, how many
    nucleons ended outside its largest group (closer than `bond` links)."""
    rng = np.random.default_rng(seed)
    parts = [make_nucleus(z, n, nucleus_id=k, rng=rng, params=params) for k, (z, n) in enumerate(specs)]
    nucleons = np.concatenate(parts)
    sim = Simulator(ctx, nucleons, params, isolate=True)
    sim.step(steps, dt=dt, damping=damping)
    state, potential = sim.state(), sim.potential()
    results = []
    start = 0
    for (z, n), part in zip(specs, parts):
        sl = slice(start, start + len(part))
        start += len(part)
        positions = state["position"][sl].astype(np.float64)
        labels = components(positions, bond * params.r0)
        largest = np.bincount(labels).max()
        center = positions.mean(axis=0)
        results.append({
            "z": z, "n": n,
            "positions": (positions - center).astype(np.float32),
            "types": state["type"][sl].copy(),
            "binding_energy": binding_energy(potential[sl]),
            "kinetic_energy": float(state["energy"][sl].sum()),
            "lost": int(len(positions) - largest),
        })
    return results


class Simulator:
    """Nucleons on the GPU, stepped with forces.comp and integrate.comp."""

    def __init__(self, ctx, nucleons, params=Params(), isolate=False):
        self.ctx = ctx
        self.params = params
        self.isolate = isolate
        self.count = len(nucleons)
        self.forces = ctx.kernel(SHADERS / "forces.comp")
        self.integrate = ctx.kernel(SHADERS / "integrate.comp")
        self.nucleons = ctx.upload(np.ascontiguousarray(nucleons, dtype=NUCLEON))
        self.potentials = ctx.buffer(4 * self.count)
        self.compute_forces()

    def compute_forces(self):
        self.ctx.run(self.forces, [self.nucleons, self.potentials], threads=self.count,
                     push=self.params.forces_push(self.count, self.isolate))

    def step(self, steps=1, dt=0.02, damping=0.0):
        push = struct.pack("<3fI", dt, damping, self.params.mass, self.count)
        for _ in range(steps):
            self.ctx.run(self.integrate, [self.nucleons], threads=self.count, push=push)
            self.compute_forces()

    def state(self):
        return self.nucleons.to_numpy(np.uint8).view(NUCLEON)

    def potential(self):
        """Each nucleon's share of the potential energy (half of each pair's)."""
        return self.potentials.to_numpy(np.float32)

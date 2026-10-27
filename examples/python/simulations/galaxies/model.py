"""The galaxy model shared by the galaxy simulation's scripts.

Units: length 1 kpc, mass 10^10 solar masses, G = 1, so one time unit is
TIME_MYR million years and one velocity unit VELOCITY_KMS km/s.

Each galaxy is a rigid potential that moves as one body: a Hernquist halo, a
Hernquist bulge and a Miyamoto-Nagai disc in the plane normal to its spin
axis. Stars and gas are test particles that feel the sum of the galaxies'
potentials. The galaxy centres pull on each other and feel Chandrasekhar
dynamical friction from each other's halos, so they lose orbital energy and
merge. shaders/step.comp steps the centres (even mode) and the
particles; the NumPy functions here compute the same accelerations.

Particles are PARTICLE records (32 bytes, the shaders' Particle struct):
position, kind + 4 * galaxy index (kind: BULGE, DISC, GAS, YOUNG), velocity,
and birth time (stars; negative for those older than the simulation).
"""

import math
import struct
from dataclasses import dataclass, replace
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
SHADERS = HERE / "shaders"

TIME_MYR = 4.714         # one time unit in Myr
VELOCITY_KMS = 207.4     # one velocity unit in km/s

BULGE, DISC, GAS, YOUNG = 0, 1, 2, 3
MAX_GALAXIES = 4
GRID = 128               # gas grid cells a side, centred on the galaxies' centre of mass
CELL = 1.0               # gas grid cell size, kpc
STEP_PASSES = 3          # step.comp passes a time step: centres and grid clear, gas deposit, particles

PARTICLE = np.dtype([("position", "<f4", 3), ("kind", "<f4"), ("velocity", "<f4", 3), ("birth", "<f4")])
assert PARTICLE.itemsize == 32

GALAXY = np.dtype([("position", "<f4", 3), ("mass", "<f4"), ("velocity", "<f4", 3), ("pad0", "<f4"),
                   ("previous", "<f4", 3), ("pad1", "<f4"), ("spin", "<f4", 3), ("pad2", "<f4"),
                   ("shape", "<f4", 4), ("masses", "<f4", 4)])
assert GALAXY.itemsize == 96


@dataclass(frozen=True)
class Galaxy:
    """One galaxy's mass model (simulation units)."""
    halo_mass: float = 60.0
    halo_scale: float = 15.0
    bulge_mass: float = 1.0
    bulge_scale: float = 0.5
    disc_mass: float = 5.0
    disc_scale: float = 3.0      # Miyamoto-Nagai a
    disc_height: float = 0.3     # Miyamoto-Nagai b
    gas_mass: float = 1.0        # the gas disc's mass, which its particles share

    @property
    def mass(self):
        return self.halo_mass + self.bulge_mass + self.disc_mass

    def scaled(self, factor):
        """The galaxy with every mass times `factor` and every length times its cube root."""
        s = factor ** (1.0 / 3.0)
        return replace(self, halo_mass=self.halo_mass * factor, bulge_mass=self.bulge_mass * factor,
                       disc_mass=self.disc_mass * factor, gas_mass=self.gas_mass * factor,
                       halo_scale=self.halo_scale * s,
                       bulge_scale=self.bulge_scale * s, disc_scale=self.disc_scale * s,
                       disc_height=self.disc_height * s)


@dataclass(frozen=True)
class Params:
    """The integration's constants and the gas rules'."""
    dt: float = 0.05
    softening: float = 0.1
    log_lambda: float = 0.2     # strength of dynamical friction (its Coulomb logarithm); 0 turns it off
    gas_relax: float = 0.5      # rate at which gas takes on its grid cell's mean velocity, per time unit
    sf_density: float = 0.01    # gas density above which stars form, 10^10 solar masses per kpc^3
    sf_efficiency: float = 0.05  # share of a cell's gas turned into stars per free-fall time

    def with_(self, **changes):
        return replace(self, **changes)

    def uniform(self, count, galaxy_count, gas_mass, time=0.0):
        """The 48 bytes of the Params uniform block of step.comp (std140): `gas_mass` is one
        gas particle's mass, `time` the simulation time at the start of the frame's first step."""
        return struct.pack("<2I8f8x", count, galaxy_count, self.dt, self.softening, self.log_lambda, time,
                           self.gas_relax, self.sf_density, self.sf_efficiency, gas_mass)


# --- accelerations ---------------------------------------------------------

def hernquist(d, mass, scale, softening):
    """Acceleration at offsets `d` (N, 3) from the centre of a Hernquist sphere."""
    r = np.sqrt((d * d).sum(-1) + softening * softening)
    return -(mass / (r * (r + scale) ** 2))[:, None] * d


def miyamoto_nagai(d, spin, mass, a, b, softening):
    """Acceleration at offsets `d` from the centre of a Miyamoto-Nagai disc with axis `spin`."""
    z = d @ spin
    zeta = np.sqrt(z * z + b * b)
    big_d = np.sqrt((d * d).sum(-1) - z * z + (a + zeta) ** 2 + softening * softening)
    return -(mass / big_d ** 3)[:, None] * (d + (a / zeta * z)[:, None] * spin)


def galaxy_acceleration(d, galaxy, spin, softening):
    """Acceleration from one galaxy at offsets `d` from its centre."""
    return (hernquist(d, galaxy.halo_mass, galaxy.halo_scale, softening)
            + hernquist(d, galaxy.bulge_mass, galaxy.bulge_scale, softening)
            + miyamoto_nagai(d, spin, galaxy.disc_mass, galaxy.disc_scale, galaxy.disc_height, softening))


def circular_speed(galaxy, radius, softening=0.1):
    """Circular speed in a galaxy's disc plane at `radius` (array)."""
    radius = np.atleast_1d(np.asarray(radius, np.float64))
    d = np.stack([radius, np.zeros_like(radius), np.zeros_like(radius)], -1)
    a = galaxy_acceleration(d, galaxy, np.array([0.0, 0.0, 1.0]), softening)
    return np.sqrt(np.maximum(-a[:, 0] * radius, 0.0))


def halo_density(galaxy, r):
    return galaxy.halo_mass * galaxy.halo_scale / (2.0 * math.pi * r * (r + galaxy.halo_scale) ** 3)


def halo_dispersion(galaxy, r):
    """One-dimensional velocity dispersion of the halo at `r`, taken as v_circ / sqrt(2)."""
    return math.sqrt(galaxy.halo_mass * r / (r + galaxy.halo_scale) ** 2 / 2.0)


def friction_rate(galaxy_a, galaxy_b, distance, speed, log_lambda, softening):
    """How fast friction slows two galaxies' relative motion: the relative
    velocity decays as exp(-rate * t) at this distance and speed.

    The friction force is Chandrasekhar's, with A in B's halo and B in A's
    averaged, acting on A and oppositely on B."""
    if log_lambda <= 0.0 or speed < 1e-6:
        return 0.0
    r = math.sqrt(distance * distance + softening * softening)
    total = 0.0
    for mover, field in ((galaxy_a, galaxy_b), (galaxy_b, galaxy_a)):
        x = speed / (math.sqrt(2.0) * halo_dispersion(field, r))
        shape = math.erf(x) - 2.0 * x / math.sqrt(math.pi) * math.exp(-x * x)
        total += 4.0 * math.pi * log_lambda * mover.mass ** 2 * halo_density(field, r) * shape / speed ** 3
    return 0.5 * total * (1.0 / galaxy_a.mass + 1.0 / galaxy_b.mass)


# --- starting conditions ---------------------------------------------------

def basis(spin):
    """Two unit vectors that, with `spin`, make a right-handed frame."""
    spin = np.asarray(spin, np.float64) / np.linalg.norm(spin)
    helper = np.array([1.0, 0.0, 0.0]) if abs(spin[0]) < 0.9 else np.array([0.0, 1.0, 0.0])
    e1 = np.cross(helper, spin)
    e1 /= np.linalg.norm(e1)
    return e1, np.cross(spin, e1)


def spin_axis(inclination_deg, node_deg=0.0):
    """A disc axis tilted `inclination_deg` from +z toward the direction `node_deg` in the xy plane."""
    i, n = math.radians(inclination_deg), math.radians(node_deg)
    return np.array([math.sin(i) * math.sin(n), -math.sin(i) * math.cos(n), math.cos(i)])


def sample_exponential_disc(rng, count, scale_length, cut):
    """Radii of an exponential surface density, out to `cut`."""
    radii = np.empty(0)
    while radii.size < count:
        r = rng.gamma(2.0, scale_length, 2 * count)
        radii = np.concatenate([radii, r[r < cut]])
    return radii[:count]


def vertical_frequency(galaxy, radius, softening, h=0.01):
    """Frequency of small vertical oscillations in the disc plane at `radius` (array)."""
    d = np.stack([radius, np.zeros_like(radius), np.full_like(radius, h)], -1)
    az = galaxy_acceleration(d, galaxy, np.array([0.0, 0.0, 1.0]), softening)[:, 2]
    return np.sqrt(np.maximum(-az / h, 0.0))


def make_galaxy(rng, galaxy, count, index, position, velocity, spin, fractions=(0.1, 0.7, 0.2),
                softening=0.1, disc_length=3.0, gas_length=5.0):
    """`count` particles of one galaxy, centred on `position` and moving with `velocity`."""
    n_bulge = int(count * fractions[0])
    n_gas = int(count * fractions[2])
    n_disc = count - n_bulge - n_gas
    e1, e2 = basis(spin)
    spin = np.asarray(spin, np.float64) / np.linalg.norm(spin)
    out = np.zeros(count, PARTICLE)

    def place(sl, local_pos, local_vel, kind):
        frame = np.stack([e1, e2, spin])          # rows: local x, y, z in world space
        out["position"][sl] = local_pos @ frame + position
        out["velocity"][sl] = local_vel @ frame + velocity
        out["kind"][sl] = kind + 4 * index

    def disc(n, length, height, dispersion, cut):
        r = sample_exponential_disc(rng, n, length, cut)
        phi = rng.uniform(0.0, 2.0 * math.pi, n)
        nu = vertical_frequency(galaxy, r, softening)
        z = rng.normal(0.0, height, n)
        pos = np.stack([r * np.cos(phi), r * np.sin(phi), z], -1)
        vc = circular_speed(galaxy, r, softening)
        vel = np.stack([-vc * np.sin(phi), vc * np.cos(phi), nu * height * rng.normal(0.0, 1.0, n)], -1)
        vel[:, :2] += rng.normal(0.0, dispersion, (n, 2))
        return pos, vel

    b0, d0, g0 = 0, n_bulge, n_bulge + n_disc
    pos, vel = disc(n_disc, disc_length, 0.3, 0.03, 5.0 * disc_length)
    place(slice(d0, g0), pos, vel, DISC)
    # Disc stars are older toward the centre: up to 12 Gyr there, falling off
    # with radius over one scale length.
    radius = np.hypot(pos[:, 0], pos[:, 1])
    age = 12000.0 * rng.uniform(0.02, 1.0, n_disc) * np.exp(-radius / disc_length)
    out["birth"][d0:g0] = -age / TIME_MYR
    pos, vel = disc(n_gas, gas_length, 0.1, 0.01, 4.0 * gas_length)
    place(slice(g0, count), pos, vel, GAS)
    pos, vel = make_bulge(rng, galaxy, n_bulge, softening)
    place(slice(b0, d0), pos, vel, BULGE)
    out["birth"][b0:d0] = -rng.uniform(8000.0, 12000.0, n_bulge) / TIME_MYR
    return out


def make_bulge(rng, galaxy, count, softening, cut=5.0):
    """A Hernquist bulge with isotropic speeds from the Jeans equation in its galaxy's spherical mass."""
    a = galaxy.bulge_scale
    u = rng.uniform(0.0, 1.0, 4 * count)
    r = a * np.sqrt(u) / (1.0 - np.sqrt(u))      # Hernquist: M(<r)/M = r^2 / (r + a)^2
    r = r[r < cut][:count]
    direction = rng.normal(size=(count, 3))
    direction /= np.linalg.norm(direction, axis=1, keepdims=True)
    grid = np.geomspace(1e-3, 200.0, 2000)
    rho = 1.0 / (grid * (grid + a) ** 3)
    enclosed = (galaxy.halo_mass * grid ** 2 / (grid + galaxy.halo_scale) ** 2
                + galaxy.bulge_mass * grid ** 2 / (grid + a) ** 2
                + galaxy.disc_mass * grid ** 3 / (grid ** 2 + (galaxy.disc_scale + galaxy.disc_height) ** 2) ** 1.5)
    integrand = rho * enclosed / (grid ** 2 + softening ** 2)
    pressure = np.concatenate([np.cumsum((integrand[1:] + integrand[:-1])[::-1] * np.diff(grid)[::-1] / 2.0)[::-1],
                               [0.0]])
    sigma = np.sqrt(np.interp(r, grid, pressure / rho))
    return r[:, None] * direction, sigma[:, None] * rng.normal(size=(count, 3))


def pair_potential(galaxy_a, galaxy_b, distance, spins, softening, far=2000.0):
    """The potential energy of two galaxy centres `distance` apart along x."""
    r = np.geomspace(distance, far, 400)
    force = np.array([centre_forces([galaxy_a, galaxy_b], np.array([[x, 0.0, 0.0], [0.0, 0.0, 0.0]]), spins,
                                    Params(softening=softening))[0, 0] for x in r])
    return float(np.sum((force[1:] + force[:-1]) * np.diff(r)) / 2.0) - galaxy_a.mass * galaxy_b.mass / far


def approach(galaxies, spins, pericentre, boundness, separation, params=None):
    """The centres' positions and velocities `separation` apart, before they pass at `pericentre`
    in the xy plane turning anticlockwise. At pericentre their relative speed is (1 - boundness)
    times the speed that would just let them escape; the approach is the motion from there
    run backward, without friction."""
    params = params or Params()
    ma, mb = galaxies[0].mass, galaxies[1].mass
    reduced = ma * mb / (ma + mb)
    escape = math.sqrt(-2.0 * pair_potential(galaxies[0], galaxies[1], pericentre, spins, params.softening) / reduced)
    speed = (1.0 - boundness) * escape
    x = np.array([[-pericentre * mb / (ma + mb), 0.0, 0.0], [pericentre * ma / (ma + mb), 0.0, 0.0]])
    v = np.array([[0.0, -speed * mb / (ma + mb), 0.0], [0.0, speed * ma / (ma + mb), 0.0]])
    back = params.with_(dt=-abs(params.dt), log_lambda=0.0)
    for _ in range(100000):
        if np.linalg.norm(x[1] - x[0]) >= separation:
            break
        x, v = step_centres(galaxies, x, v, spins, back)
    return x, v


def galaxy_records(galaxies, positions, velocities, spins):
    """The GALAXY records the shaders read."""
    out = np.zeros(MAX_GALAXIES, GALAXY)
    for i, (g, x, v, s) in enumerate(zip(galaxies, positions, velocities, spins)):
        out[i]["position"] = x
        out[i]["previous"] = x
        out[i]["mass"] = g.mass
        out[i]["velocity"] = v
        out[i]["spin"] = np.asarray(s) / np.linalg.norm(s)
        out[i]["shape"] = (g.halo_scale, g.bulge_scale, g.disc_scale, g.disc_height)
        out[i]["masses"] = (g.halo_mass, g.bulge_mass, g.disc_mass, 0.0)
    return out


@dataclass
class Scene:
    """What a scene starts from: the galaxy models and records, and the particles."""
    galaxies: list
    records: np.ndarray
    particles: np.ndarray

    @property
    def gas_mass(self):
        """One gas particle's mass: the galaxies' gas shared among their gas particles."""
        gas = int(((self.particles["kind"].astype(np.int64) & 3) == GAS).sum())
        return sum(g.gas_mass for g in self.galaxies) / max(gas, 1)


def merger(count=1 << 20, inclination=(0.0, 30.0), ratio=1.0, pericentre=10.0, boundness=0.05,
           separation=60.0, seed=1, base=Galaxy()):
    """Two disc galaxies `separation` apart, approaching to pass at `pericentre` (see approach);
    the second `ratio` times the mass of the first. Inclinations are in degrees from the orbital
    plane's axis; above 90 a disc turns against the orbit."""
    rng = np.random.default_rng(seed)
    gals = [base, base.scaled(ratio)]
    spins = [spin_axis(inclination[0], 0.0), spin_axis(inclination[1], 60.0)]
    xs, vs = approach(gals, spins, pericentre, boundness, separation)
    total = gals[0].mass + gals[1].mass
    counts = [int(count * gals[0].mass / total)]
    counts.append(count - counts[0])
    parts = [make_galaxy(rng, g, n, i, x, v, s) for i, (g, n, x, v, s) in enumerate(zip(gals, counts, xs, vs, spins))]
    return Scene(gals, galaxy_records(gals, xs, vs, spins), np.concatenate(parts))


def isolated(count=1 << 16, seed=1, base=Galaxy()):
    """One galaxy at rest at the origin."""
    rng = np.random.default_rng(seed)
    spin = np.array([0.0, 0.0, 1.0])
    zero = np.zeros(3)
    return Scene([base], galaxy_records([base], [zero], [zero], [spin]),
                 make_galaxy(rng, base, count, 0, zero, zero, spin))


# --- stepping --------------------------------------------------------------

def centre_forces(galaxies, x, spins, params):
    """The gravitational force on each galaxy centre, as step.comp computes it."""
    n = len(galaxies)
    forces = np.zeros((n, 3))
    for i in range(n):
        for j in range(n):
            if i == j:
                continue
            d = (x[i] - x[j])[None]
            forces[i] += 0.5 * (galaxies[i].mass * galaxy_acceleration(d, galaxies[j], spins[j], params.softening)[0]
                                - galaxies[j].mass * galaxy_acceleration(-d, galaxies[i], spins[i], params.softening)[0])
    return forces


def apply_friction(galaxies, x, v, params, dt):
    """Slow each pair's relative velocity by friction over `dt`, keeping the pair's momentum."""
    v = v.copy()
    for i in range(len(galaxies)):
        for j in range(i + 1, len(galaxies)):
            rel = v[i] - v[j]
            speed = float(np.linalg.norm(rel))
            rate = friction_rate(galaxies[i], galaxies[j], float(np.linalg.norm(x[i] - x[j])), speed,
                                 params.log_lambda, params.softening)
            change = rel * (math.exp(-rate * dt) - 1.0)
            ma, mb = galaxies[i].mass, galaxies[j].mass
            v[i] += change * mb / (ma + mb)
            v[j] -= change * ma / (ma + mb)
    return v


def step_centres(galaxies, x, v, spins, params):
    """One step of the centres: half a step of gravity and friction, a drift,
    and another half; returns the new positions and velocities."""
    masses = np.array([g.mass for g in galaxies])[:, None]
    half = 0.5 * params.dt
    v = apply_friction(galaxies, x, v + half * centre_forces(galaxies, x, spins, params) / masses, params, half)
    x = x + params.dt * v
    v = apply_friction(galaxies, x, v + half * centre_forces(galaxies, x, spins, params) / masses, params, half)
    return x, v


class Simulator:
    """A scene on the GPU, stepped with shaders/step.comp."""

    def __init__(self, ctx, scene, params=Params()):
        self.ctx = ctx
        self.params = params
        self.count = len(scene.particles)
        self.galaxy_count = len(scene.galaxies)
        self.gas_mass = scene.gas_mass
        self.time = 0.0
        self.kernel = ctx.kernel(SHADERS / "step.comp")
        self.particles = ctx.upload(np.ascontiguousarray(scene.particles))
        self.galaxies = ctx.upload(np.ascontiguousarray(scene.records))
        self.grid = ctx.buffer(GRID ** 3 * 16)
        self.stats = ctx.buffer(16)
        self.uniform = ctx.upload(params.uniform(self.count, self.galaxy_count, self.gas_mass))

    def step(self, steps=1):
        bindings = [self.particles, self.galaxies, self.uniform, self.grid, self.stats]
        threads = max(self.count, 1 << 16)
        for _ in range(steps):
            self.uniform.upload(self.params.uniform(self.count, self.galaxy_count, self.gas_mass, self.time))
            for mode in range(STEP_PASSES):
                self.ctx.run(self.kernel, bindings, threads=threads, push=struct.pack("<I", mode))
            self.time += self.params.dt

    def stars_formed(self):
        return int(self.stats.to_numpy(np.uint32)[0])

    def state(self):
        return self.particles.to_numpy(np.uint8).view(PARTICLE)

    def records(self):
        return self.galaxies.to_numpy(np.uint8).view(GALAXY)

"""Checks of the galaxy model on sk.ComputeContext, without a window.

    python check.py              # all checks
    python check.py merger       # one: kernels, disc, merger

kernels  one step on the GPU against model.py's NumPy accelerations
disc     an isolated galaxy for 2 Gyr: how far its disc stars wander in radius and height
merger   two galaxies: their separation over time, pericentres and when they merge
"""

import sys
import time

import numpy as np
import shoonyakasha as sk

import model as m


def kernels(ctx):
    scene = m.merger(count=4096, separation=30.0)
    params = m.Params()
    sim = m.Simulator(ctx, scene, params)
    sim.step(1)
    gpu, records = sim.state(), sim.records()

    spins = [r["spin"].astype(np.float64) for r in scene.records[:2]]
    x0 = scene.records["position"][:2].astype(np.float64)
    v0 = scene.records["velocity"][:2].astype(np.float64)
    x1, v1 = m.step_centres(scene.galaxies, x0, v0, spins, params)
    centre_error = max(np.abs(records["position"][:2] - x1).max(), np.abs(records["velocity"][:2] - v1).max())

    def accel(x, centres):
        return sum(m.galaxy_acceleration(x - c, g, s, params.softening)
                   for g, c, s in zip(scene.galaxies, centres, spins))

    x = scene.particles["position"].astype(np.float64)
    v = scene.particles["velocity"].astype(np.float64)
    v = v + 0.5 * params.dt * accel(x, x0)
    x = x + params.dt * v
    v = v + 0.5 * params.dt * accel(x, x1)
    particle_error = max(np.abs(gpu["position"] - x).max(), np.abs(gpu["velocity"] - v).max())
    print("kernels: centres differ by %.2e, particles by %.2e" % (centre_error, particle_error))
    return centre_error < 1e-4 and particle_error < 1e-4


def disc(ctx, gyr=2.0):
    scene = m.isolated(count=1 << 16)
    sim = m.Simulator(ctx, scene)
    start = scene.particles
    steps = int(gyr * 1000.0 / m.TIME_MYR / sim.params.dt)
    t = time.perf_counter()
    sim.step(steps)
    seconds = time.perf_counter() - t
    end = sim.state()
    is_disc = (start["kind"] % 4) == m.DISC
    r0 = np.hypot(*start["position"][is_disc, :2].T)
    r1 = np.hypot(*end["position"][is_disc, :2].T)
    moved = np.abs(r1 - r0) / np.maximum(r0, 0.5)
    h0 = np.abs(start["position"][is_disc, 2]).mean()
    h1 = np.abs(end["position"][is_disc, 2]).mean()
    print("disc: %d steps (%.1f s); median radial change %.1f%%, beyond 20%%: %.1f%%; mean |z| %.2f -> %.2f kpc"
          % (steps, seconds, 100 * np.median(moved), 100 * (moved > 0.2).mean(), h0, h1))
    return np.median(moved) < 0.1 and h1 < 2.0 * h0


def merger(ctx, log_lambda=None, gyr=4.0, quiet=False):
    params = m.Params() if log_lambda is None else m.Params(log_lambda=log_lambda)
    scene = m.merger(count=1 << 14)
    sim = m.Simulator(ctx, scene, params)
    every = int(round(5.0 / params.dt))
    times, separations = [], []
    for k in range(int(gyr * 1000.0 / m.TIME_MYR / 5.0)):
        sim.step(every)
        r = sim.records()
        times.append((k + 1) * 5.0 * m.TIME_MYR)
        separations.append(float(np.linalg.norm(r["position"][0] - r["position"][1])))
    s = np.array(separations)
    minima = [i for i in range(1, len(s) - 1) if s[i] < s[i - 1] and s[i] <= s[i + 1] and s[i] > 1.0]
    merged = next((times[i] for i in range(len(s)) if (s[i:] < 1.0).all()), None)
    if not quiet:
        print("merger (lnLambda %.1f): pericentres at %s Myr (%s kpc); merged %s"
              % (params.log_lambda, [round(times[i]) for i in minima], [float(round(s[i], 1)) for i in minima],
                 "at %d Myr" % merged if merged else "not within %.0f Gyr" % gyr))
    return merged is not None


CHECKS = {"kernels": kernels, "disc": disc, "merger": merger}


def main():
    names = sys.argv[1:] or list(CHECKS)
    with sk.ComputeContext() as ctx:
        results = {name: CHECKS[name](ctx) for name in names}
    print(", ".join("%s %s" % (k, "ok" if v else "FAILED") for k, v in results.items()))
    return 0 if all(results.values()) else 1


if __name__ == "__main__":
    sys.exit(main())

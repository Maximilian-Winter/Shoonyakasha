"""The merger with every particle massive, precomputed for the viewer's replay.

    python precompute_replay.py                  # 131,072 bodies, 1.5 Gyr -> recordings/merger*
    python precompute_replay.py --gyr 0.3 --halo 4096 --bulge 1024 --disc 3072 --out recordings/quick

The two galaxies of model.merger (same masses, discs, tilts and orbit) are
built of bodies that all pull on each other: per galaxy a Hernquist halo
and bulge with isotropic speeds from the Jeans equation, and an exponential
disc (scale length 3 kpc) whose radial speed spread gives Toomre's Q = 1.5.
shaders/nbody.comp sums every pair on sk.ComputeContext (kick, drift,
kick). The halos are bodies too, so they deform and drag on each other
(dynamical friction) without a rule, and the discs feel their own gravity.

Every --every steps the bulge and disc stars are written, in the viewer's
world coordinates (model.TO_WORLD), as half floats (x, y, z, 0, vx, vy, vz,
0); the viewer moves each star on along its velocity between frames:

    <out>.npy          (frames, stars, 8) float16, the recording sk.streams.play plays
    <out>_stars.npy    (stars, 2) float32: kind + 4 * galaxy, birth time (minus the age)
    <out>.json         frame interval, steps, bodies and the run's constants
"""

import argparse
import json
import math
import struct
import time
from pathlib import Path

import numpy as np
import shoonyakasha as sk

import model as m

TOOMRE_Q = 1.5
DISC_LENGTH = 3.0
DISC_HEIGHT = 0.3


def live_disc(rng, galaxy, count, softening):
    """`count` positions, velocities and ages (Myr) of an exponential disc in the xy plane."""
    r = m.sample_exponential_disc(rng, count, DISC_LENGTH, 5.0 * DISC_LENGTH)
    phi = rng.uniform(0.0, 2.0 * math.pi, count)
    z = rng.normal(0.0, DISC_HEIGHT, count)
    vc = m.circular_speed(galaxy, r, softening)
    dr = 0.01
    slope = (np.log(m.circular_speed(galaxy, r + dr, softening)) - np.log(m.circular_speed(galaxy, r - dr, softening))
             ) / (np.log(r + dr) - np.log(np.maximum(r - dr, 1e-3)))
    omega = vc / r
    kappa = omega * np.sqrt(np.maximum(2.0 * (1.0 + slope), 1e-6))
    surface = galaxy.disc_mass / (2.0 * math.pi * DISC_LENGTH ** 2) * np.exp(-r / DISC_LENGTH)
    sigma_r = TOOMRE_Q * 3.36 * surface / kappa
    sigma_phi = sigma_r * kappa / (2.0 * omega)
    sigma_z = np.sqrt(math.pi * surface * DISC_HEIGHT)
    # Asymmetric drift: the mean rotation lags the circular speed.
    v_phi = np.sqrt(np.maximum(vc ** 2 + sigma_r ** 2 * (1.0 - kappa ** 2 / (4.0 * omega ** 2) - 2.0 * r / DISC_LENGTH),
                               0.0))
    v_r = rng.normal(0.0, 1.0, count) * sigma_r
    v_t = v_phi + rng.normal(0.0, 1.0, count) * sigma_phi
    pos = np.stack([r * np.cos(phi), r * np.sin(phi), z], -1)
    vel = np.stack([v_r * np.cos(phi) - v_t * np.sin(phi), v_r * np.sin(phi) + v_t * np.cos(phi),
                    rng.normal(0.0, 1.0, count) * sigma_z], -1)
    age = 12000.0 * rng.uniform(0.02, 1.0, count) * np.exp(-r / DISC_LENGTH)
    return pos, vel, age


def live_galaxy(rng, galaxy, counts, index, position, velocity, spin, softening):
    """The bodies of one galaxy, (N, 3, 4) float32 rows of position and mass, velocity and
    acceleration, halo first; and the stars' codes and birth times (bulge, then disc)."""
    n_halo, n_bulge, n_disc = counts
    halo_cut = 10.0 * galaxy.halo_scale
    bulge_cut = 5.0
    parts = []
    pos, vel = m.make_spheroid(rng, galaxy, n_halo, galaxy.halo_scale, softening, halo_cut)
    parts.append((pos, vel, galaxy.halo_mass * (halo_cut / (halo_cut + galaxy.halo_scale)) ** 2 / n_halo))
    pos, vel = m.make_spheroid(rng, galaxy, n_bulge, galaxy.bulge_scale, softening, bulge_cut)
    parts.append((pos, vel, galaxy.bulge_mass * (bulge_cut / (bulge_cut + galaxy.bulge_scale)) ** 2 / n_bulge))
    pos, vel, disc_age = live_disc(rng, galaxy, n_disc, softening)
    parts.append((pos, vel, galaxy.disc_mass / n_disc))

    e1, e2 = m.basis(spin)
    frame = np.stack([e1, e2, np.asarray(spin) / np.linalg.norm(spin)])
    bodies = np.zeros((sum(counts), 3, 4), np.float32)
    start = 0
    for pos, vel, mass in parts:
        end = start + len(pos)
        bodies[start:end, 0, :3] = pos @ frame + position
        bodies[start:end, 0, 3] = mass
        bodies[start:end, 1, :3] = vel @ frame + velocity
        start = end
    codes = np.concatenate([np.full(n_bulge, m.BULGE + 4 * index), np.full(n_disc, m.DISC + 4 * index)])
    ages = np.concatenate([rng.uniform(8000.0, 12000.0, n_bulge), disc_age])
    return bodies, codes.astype(np.float32), (-ages / m.TIME_MYR).astype(np.float32)


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--gyr", type=float, default=1.5, help="simulated time")
    parser.add_argument("--halo", type=int, default=32768, help="halo bodies a galaxy")
    parser.add_argument("--bulge", type=int, default=8192, help="bulge stars a galaxy")
    parser.add_argument("--disc", type=int, default=24576, help="disc stars a galaxy")
    parser.add_argument("--dt", type=float, default=0.05, help="time step, time units")
    parser.add_argument("--softening", type=float, default=0.2, help="Plummer softening, kpc")
    parser.add_argument("--every", type=int, default=20, help="steps between recorded frames")
    parser.add_argument("--out", default="recordings/merger", help="the recording's path, without .npy")
    return parser.parse_args()


def main():
    args = parse_args()
    out = m.HERE / args.out
    out.parent.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(1)
    base = m.merger(count=1024)       # the merger's galaxies, tilts and orbit
    counts = (args.halo, args.bulge, args.disc)
    galaxies, stars, codes, births = [], [], [], []
    start = 0
    for i, galaxy in enumerate(base.galaxies):
        record = base.records[i]
        bodies, c, b = live_galaxy(rng, galaxy, counts, i, record["position"].astype(np.float64),
                                   record["velocity"].astype(np.float64), record["spin"].astype(np.float64),
                                   args.softening)
        galaxies.append(bodies)
        stars.append(np.arange(start + args.halo, start + len(bodies)))
        codes.append(c)
        births.append(b)
        start += len(bodies)
    bodies = np.concatenate(galaxies)
    stars = np.concatenate(stars)
    bulges = [start_ + args.halo + np.arange(args.bulge) for start_ in range(0, len(bodies), sum(counts))]
    count = len(bodies)
    steps = int(round(args.gyr * 1000.0 / m.TIME_MYR / args.dt))
    frames = steps // args.every + 1
    print("%d bodies (%d stars recorded), %d steps of %.3g, %d frames" % (count, len(stars), steps, args.dt, frames))

    recording = np.lib.format.open_memmap(str(out) + ".npy", mode="w+", dtype=np.float16,
                                          shape=(frames, len(stars), 8))
    np.save(str(out) + "_stars.npy", np.stack([np.concatenate(codes), np.concatenate(births)], -1))
    centres = []

    def push(mode):
        return struct.pack("<2I2f", mode, count, args.dt, args.softening)

    def record(frame, state):
        positions = state[:, 0, :3].astype(np.float64)
        recording[frame, :, :3] = (positions[stars] @ m.TO_WORLD).astype(np.float16)
        recording[frame, :, 4:7] = (state[stars, 1, :3].astype(np.float64) @ m.TO_WORLD).astype(np.float16)
        centres.append([np.median(positions[b], axis=0) for b in bulges])

    with sk.ComputeContext() as ctx:
        kernel = ctx.kernel(m.SHADERS / "nbody.comp")
        buffer = ctx.upload(bodies)
        ctx.run(kernel, [buffer], threads=count, push=push(0))
        record(0, bodies)
        began = time.perf_counter()
        for step in range(1, steps + 1):
            for mode in (1, 0, 2):
                ctx.run(kernel, [buffer], threads=count, push=push(mode))
            if step % args.every == 0:
                record(step // args.every, buffer.to_numpy(np.float32).reshape(count, 3, 4))
            if step % max(steps // 10, 1) == 0:
                print("  %d%% (%.0f Myr), %.0f s" % (100 * step // steps, step * args.dt * m.TIME_MYR,
                                                    time.perf_counter() - began))
        final = buffer.to_numpy(np.float32).reshape(count, 3, 4)
    recording.flush()

    seconds = time.perf_counter() - began
    frame_myr = args.every * args.dt * m.TIME_MYR
    separation = [float(np.linalg.norm(c[1] - c[0])) for c in centres]
    minima = [i for i in range(1, len(separation) - 1)
              if separation[i] < separation[i - 1] and separation[i] <= separation[i + 1] and separation[i] > 1.0]
    merged = next((i for i in range(len(separation)) if max(separation[i:]) < 1.0), None)
    momentum = (final[:, 1, :3] * final[:, 0, 3:4]).sum(0) - (bodies[:, 1, :3] * bodies[:, 0, 3:4]).sum(0)
    scale = (np.abs(bodies[:, 1, :3]) * bodies[:, 0, 3:4]).sum()
    summary = {
        "frame_myr": frame_myr, "frames": frames, "steps": steps, "dt": args.dt, "softening": args.softening,
        "bodies": count, "stars": int(len(stars)), "per_galaxy": {"halo": args.halo, "bulge": args.bulge,
                                                                  "disc": args.disc},
        "seconds": round(seconds, 1),
        "pericentres": [{"myr": round(i * frame_myr), "kpc": round(separation[i], 1)} for i in minima],
        "merged_myr": round(merged * frame_myr) if merged is not None else None,
        "momentum_drift": float(np.abs(momentum).max() / scale),
    }
    Path(str(out) + ".json").write_text(json.dumps(summary, indent=2))
    print("done in %.0f s: pericentres %s, merged %s, momentum drift %.1e -> %s.npy (%.0f MB)"
          % (seconds, [(p["myr"], p["kpc"]) for p in summary["pericentres"]],
             "at %d Myr" % summary["merged_myr"] if merged is not None else "not yet",
             summary["momentum_drift"], out, recording.nbytes / 1e6))


if __name__ == "__main__":
    main()

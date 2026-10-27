"""The binding energy per nucleon of the model's nuclei, against mass number.

    python binding_curve.py                      # the default parameters
    python binding_curve.py --kc 0.12 --d-protons 0.2 --d-neutrons 0.05

Cools a nucleus for each mass number A, with the valley of stability's
proton number, and plots its binding energy per nucleon beside the real
curve (the semi-empirical mass formula, scaled to the model's peak). The
model's curve should rise steeply for light nuclei, peak near iron (A = 56)
and fall slowly towards uranium. Writes output/binding_curve.png and prints
the table.
"""

import argparse

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import shoonyakasha as sk  # noqa: E402

from physics import HERE, Params, relax_batch, valley_z  # noqa: E402

MASS_NUMBERS = [2, 3, 4, 6, 8, 12, 16, 20, 28, 40, 56, 70, 90, 110, 140, 170, 200, 238]


def semf(a, z):
    """Binding energy per nucleon in MeV from the semi-empirical mass formula."""
    b = 15.75 * a - 17.8 * a ** (2 / 3) - 0.711 * z * (z - 1) / a ** (1 / 3) - 23.7 * (a - 2 * z) ** 2 / a
    return b / a


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    defaults = Params()
    parser.add_argument("--d-unlike", type=float, default=defaults.d_unlike)
    parser.add_argument("--d-protons", type=float, default=defaults.d_protons)
    parser.add_argument("--d-neutrons", type=float, default=defaults.d_neutrons)
    parser.add_argument("--a", type=float, default=defaults.a)
    parser.add_argument("--kc", type=float, default=defaults.kc)
    parser.add_argument("--steps", type=int, default=3000)
    parser.add_argument("--seeds", type=int, default=3, help="proton arrangements tried per nucleus; the best is kept")
    parser.add_argument("--output", default=str(HERE / "output" / "binding_curve.png"))
    return parser.parse_args()


def curve(ctx, params, steps, seeds):
    """(A, Z, best binding energy per nucleon, nucleons lost) for each mass number."""
    specs = [(valley_z(a), a - valley_z(a)) for a in MASS_NUMBERS]
    best = {}
    for seed in range(seeds):
        for result in relax_batch(ctx, specs, params, steps=steps, seed=seed):
            a = result["z"] + result["n"]
            per = result["binding_energy"] / a
            if a not in best or per > best[a][2]:
                best[a] = (a, result["z"], per, result["lost"])
    return [best[a] for a in MASS_NUMBERS]


def main():
    args = parse_args()
    params = Params(d_unlike=args.d_unlike, d_protons=args.d_protons, d_neutrons=args.d_neutrons, a=args.a,
                    kc=args.kc)
    with sk.ComputeContext() as ctx:
        rows = curve(ctx, params, args.steps, args.seeds)

    print("%5s %4s %9s %5s" % ("A", "Z", "BE/A", "lost"))
    for a, z, per, lost in rows:
        print("%5d %4d %9.4f %5d" % (a, z, per, lost))
    peak = max(rows, key=lambda r: r[2])
    print("peak at A = %d (BE/A %.4f); params %s" % (peak[0], peak[2], params))

    a_values = np.array([r[0] for r in rows])
    model = np.array([r[2] for r in rows])
    real = np.array([semf(r[0], r[1]) for r in rows])
    real_scaled = real / real.max() * model.max()
    fig, ax = plt.subplots(figsize=(8, 5), dpi=110)
    ax.plot(a_values, model, "o-", label="model")
    ax.plot(a_values, real_scaled, "--", color="grey", label="real (mass formula), scaled to the model's peak")
    ax.axvline(56, color="orange", lw=0.8, alpha=0.6)
    ax.set_xlabel("mass number A")
    ax.set_ylabel("binding energy per nucleon (simulation units)")
    ax.set_title("d_unlike %.2f, d_protons %.2f, d_neutrons %.2f, a %.1f, kc %.3f"
                 % (params.d_unlike, params.d_protons, params.d_neutrons, params.a, params.kc))
    ax.legend()
    fig.tight_layout()
    from pathlib import Path
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output)
    print("wrote", args.output)


if __name__ == "__main__":
    main()

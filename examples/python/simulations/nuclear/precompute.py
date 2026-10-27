"""Cool the nuclei the scenarios start from into their ground states.

    python precompute.py                  # every nucleus in NUCLEI
    python precompute.py --seeds 8        # try more proton arrangements

Each nucleus is cooled from several random proton arrangements on a compact
lattice (physics.relax_batch); the most tightly bound is kept. Writes
configs/<name>.npy, one row per nucleon of (x, y, z, type) float32 relative
to the centre of mass, type 1 for a proton and 0 for a neutron, and
configs/index.json with each nucleus's Z, N, binding energy and binding
energy per nucleon, in simulation units, and the parameters used.
"""

import argparse
import json
from dataclasses import asdict

import numpy as np
import shoonyakasha as sk

from physics import HERE, Params, relax_batch

CONFIGS = HERE / "configs"

# name: (protons, neutrons)
NUCLEI = {
    "H-1": (1, 0),
    "H-2": (1, 1),
    "H-3": (1, 2),
    "He-3": (2, 1),
    "He-4": (2, 2),
    "C-12": (6, 6),
    "Fe-56": (26, 30),
    "U-235": (92, 143),
    "U-238": (92, 146),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--seeds", type=int, default=6, help="proton arrangements tried per nucleus")
    parser.add_argument("--steps", type=int, default=6000)
    args = parser.parse_args()

    params = Params()
    names = list(NUCLEI)
    specs = [NUCLEI[name] for name in names]
    best = {}
    with sk.ComputeContext() as ctx:
        for seed in range(args.seeds):
            for name, result in zip(names, relax_batch(ctx, specs, params, steps=args.steps, seed=seed)):
                if result["lost"]:
                    continue
                if name not in best or result["binding_energy"] > best[name]["binding_energy"]:
                    best[name] = result

    CONFIGS.mkdir(exist_ok=True)
    index = {"params": asdict(params), "nuclei": {}}
    print("%-7s %4s %4s %10s %8s" % ("nucleus", "Z", "N", "binding", "per A"))
    for name in names:
        if name not in best:
            print("%-7s lost nucleons in every arrangement; not written" % name)
            continue
        result = best[name]
        rows = np.hstack([result["positions"], result["types"][:, None]]).astype(np.float32)
        np.save(CONFIGS / ("%s.npy" % name), rows)
        a = result["z"] + result["n"]
        index["nuclei"][name] = {"z": result["z"], "n": result["n"], "binding_energy": result["binding_energy"],
                                 "binding_energy_per_nucleon": result["binding_energy"] / a,
                                 "file": "%s.npy" % name}
        print("%-7s %4d %4d %10.3f %8.4f" % (name, result["z"], result["n"], result["binding_energy"],
                                            result["binding_energy"] / a))
    (CONFIGS / "index.json").write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")
    print("wrote", CONFIGS)


if __name__ == "__main__":
    main()

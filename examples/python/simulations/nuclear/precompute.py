"""Cool the nuclei the scenarios start from into their ground states.

    python precompute.py                      # every nucleus in NUCLEI, realistic setting
    python precompute.py --seeds 8            # try more proton arrangements
    python precompute.py --setting scenario   # the scenarios' nuclei, into configs/scenario

Each nucleus is cooled from several random proton arrangements on a compact
lattice (physics.relax_batch); the most tightly bound of those still whole
after running undamped (physics.holds_together) is kept. Writes
configs/<name>.npy, one row per nucleon of (x, y, z, type) float32 relative
to the centre of mass, type 1 for a proton and 0 for a neutron, and
configs/index.json with each nucleus's Z, N, binding energy and binding
energy per nucleon, in simulation units, and the parameters used. With
--setting scenario it cools SCENARIO_NUCLEI under physics.SCENARIO into
configs/scenario instead.
"""

import argparse
import json
from dataclasses import asdict

import numpy as np
import shoonyakasha as sk

from physics import HERE, SCENARIO, Params, holds_together, relax_batch

CONFIGS = HERE / "configs"
SCENARIO_NUCLEI = ("H-2", "H-3", "He-4", "U-235")

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
    parser.add_argument("--setting", choices=("realistic", "scenario"), default="realistic")
    args = parser.parse_args()

    scenario = args.setting == "scenario"
    params = SCENARIO if scenario else Params()
    folder = CONFIGS / "scenario" if scenario else CONFIGS
    names = list(SCENARIO_NUCLEI if scenario else NUCLEI)
    specs = [NUCLEI[name] for name in names]
    best = {}
    with sk.ComputeContext() as ctx:
        for seed in range(args.seeds):
            results = relax_batch(ctx, specs, params, steps=args.steps, seed=seed)
            holds = holds_together(ctx, results, params)
            for name, result, whole in zip(names, results, holds):
                if result["lost"] or not whole:
                    continue
                if name not in best or result["binding_energy"] > best[name]["binding_energy"]:
                    best[name] = result

    folder.mkdir(parents=True, exist_ok=True)
    index = {"params": asdict(params), "nuclei": {}}
    print("%-7s %4s %4s %10s %8s" % ("nucleus", "Z", "N", "binding", "per A"))
    for name in names:
        if name not in best:
            print("%-7s lost nucleons or fell apart in every arrangement; not written" % name)
            continue
        result = best[name]
        rows = np.hstack([result["positions"], result["types"][:, None]]).astype(np.float32)
        np.save(folder / ("%s.npy" % name), rows)
        a = result["z"] + result["n"]
        index["nuclei"][name] = {"z": result["z"], "n": result["n"], "binding_energy": result["binding_energy"],
                                 "binding_energy_per_nucleon": result["binding_energy"] / a,
                                 "file": "%s.npy" % name}
        print("%-7s %4d %4d %10.3f %8.4f" % (name, result["z"], result["n"], result["binding_energy"],
                                            result["binding_energy"] / a))
    (folder / "index.json").write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")
    print("wrote", folder)


if __name__ == "__main__":
    main()

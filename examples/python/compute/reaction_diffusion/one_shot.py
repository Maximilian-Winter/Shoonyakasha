"""One Gray-Scott pattern at high resolution: run it, save it, plot it.

    python one_shot.py                       # coral, 1024 x 1024, 10000 steps
    python one_shot.py --preset mitosis --size 2048 --steps 20000

Writes output/<preset>.npy, the final (A, B) grid, and output/<preset>.png,
B in a colour map. The .npy opens anywhere NumPy does:

    cells = np.load("output/coral.npy")      # (height, width, 2)
    plt.imshow(cells[..., 1], cmap="magma")
"""

import argparse

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import shoonyakasha as sk  # noqa: E402

from gray_scott import OUTPUT, PRESETS, GrayScott, seed, timed  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--preset", choices=sorted(PRESETS), default="coral")
    parser.add_argument("--size", type=int, default=1024)
    parser.add_argument("--steps", type=int, default=10000)
    args = parser.parse_args()

    feed, kill = PRESETS[args.preset]
    OUTPUT.mkdir(exist_ok=True)
    with sk.ComputeContext() as ctx:
        sim = GrayScott(ctx, seed(args.size, args.size, spots=args.size // 25), feed, kill)
        timed("%d steps of %d x %d" % (args.steps, args.size, args.size), sim.step, args.steps)
        cells = sim.state()

    np.save(OUTPUT / ("%s.npy" % args.preset), cells)
    plt.imsave(OUTPUT / ("%s.png" % args.preset), cells[..., 1], cmap="magma")
    print("wrote %s and %s" % (OUTPUT / ("%s.npy" % args.preset), OUTPUT / ("%s.png" % args.preset)))


if __name__ == "__main__":
    main()

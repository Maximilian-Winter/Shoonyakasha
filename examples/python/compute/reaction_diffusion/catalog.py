"""Every Gray-Scott pattern in one image: a map of the (k, f) parameter space.

    python catalog.py                        # 1024 x 1024, 12000 steps
    python catalog.py --size 2048 --steps 20000

One simulation whose kill rate k grows from left to right and whose feed
rate f grows from top to bottom, so each region of the grid settles into
the pattern of its own (k, f). Writes output/catalog.png, labelled with the
axes and the presets, and output/catalog.exr, B as one float channel.
"""

import argparse

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import shoonyakasha as sk  # noqa: E402

from gray_scott import OUTPUT, PRESETS, GrayScott, seed, timed  # noqa: E402

FEED = (0.01, 0.09)
KILL = (0.045, 0.07)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--size", type=int, default=1024)
    parser.add_argument("--steps", type=int, default=12000)
    args = parser.parse_args()

    OUTPUT.mkdir(exist_ok=True)
    with sk.ComputeContext() as ctx:
        # Spots everywhere, so every region has some B to start from.
        sim = GrayScott(ctx, seed(args.size, args.size, spots=args.size // 2, radius=4), FEED, KILL)
        timed("%d steps of %d x %d" % (args.steps, args.size, args.size), sim.step, args.steps)
        b = sim.state()[..., 1]

    sk.save_exr(OUTPUT / "catalog.exr", b)

    # The grid wraps, so its edges meet the opposite edge's rates; the plot
    # leaves out a border of `trim` cells.
    trim = max(2, args.size // 128)

    def rate(ends, cell):
        return ends[0] + (ends[1] - ends[0]) * cell / (args.size - 1)

    last = args.size - 1 - trim
    extent = (rate(KILL, trim), rate(KILL, last), rate(FEED, last), rate(FEED, trim))
    fig, ax = plt.subplots(figsize=(9, 9), dpi=120)
    ax.imshow(b[trim:-trim, trim:-trim], cmap="magma", extent=extent, aspect="auto")
    for name, (f, k) in PRESETS.items():
        if extent[3] <= f <= extent[2] and extent[0] <= k <= extent[1]:
            ax.plot(k, f, "o", color="white", markersize=4)
            ax.annotate(name, (k, f), xytext=(5, 4), textcoords="offset points", color="white", fontsize=9)
    ax.set_xlabel("kill rate k")
    ax.set_ylabel("feed rate f")
    ax.set_title("Gray-Scott patterns over (k, f), %d steps" % args.steps)
    fig.tight_layout()
    fig.savefig(OUTPUT / "catalog.png")
    print("wrote %s and %s" % (OUTPUT / "catalog.png", OUTPUT / "catalog.exr"))


if __name__ == "__main__":
    main()

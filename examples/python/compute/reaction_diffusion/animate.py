"""A Gray-Scott pattern growing, as an animated GIF.

    python animate.py                        # mitosis, 512 x 512, 8000 steps
    python animate.py --preset worms --every 50

The grid stays on the GPU in two buffers that swap roles every step; only
every `--every`th step is copied back, to become a frame. Writes
output/<preset>.gif and the final grid as output/<preset>.exr, A in the red
channel and B in the green, in 32-bit floats.
"""

import argparse

import matplotlib
matplotlib.use("Agg")
from matplotlib import colormaps  # noqa: E402
import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402
import shoonyakasha as sk  # noqa: E402

from gray_scott import OUTPUT, PRESETS, GrayScott, seed  # noqa: E402


def frame(cells, cmap=colormaps["magma"]):
    """B in a colour map, as an 8-bit RGB image."""
    return Image.fromarray((cmap(cells[..., 1] / 0.5)[..., :3] * 255).astype(np.uint8))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--preset", choices=sorted(PRESETS), default="mitosis")
    parser.add_argument("--size", type=int, default=512)
    parser.add_argument("--steps", type=int, default=8000)
    parser.add_argument("--every", type=int, default=80, help="steps between frames")
    args = parser.parse_args()

    feed, kill = PRESETS[args.preset]
    OUTPUT.mkdir(exist_ok=True)
    frames = []
    with sk.ComputeContext() as ctx:
        sim = GrayScott(ctx, seed(args.size, args.size, spots=6, radius=10), feed, kill)
        while sim.steps < args.steps:
            sim.step(min(args.every, args.steps - sim.steps))
            frames.append(frame(sim.state()))
        cells = sim.state()

    gif = OUTPUT / ("%s.gif" % args.preset)
    frames[0].save(gif, save_all=True, append_images=frames[1:], duration=40, loop=0)
    exr = OUTPUT / ("%s.exr" % args.preset)
    sk.save_exr(exr, cells, half=False)
    print("%d frames from %d steps: wrote %s and %s" % (len(frames), sim.steps, gif, exr))


if __name__ == "__main__":
    main()

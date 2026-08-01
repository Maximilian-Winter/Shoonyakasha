#!/usr/bin/env python3
"""Downsample a Radiance (.hdr) environment map.

Pure Python and dependency-free on purpose: this runs once to produce the small
environment maps committed under assets/env/, and asking contributors to install
numpy or imageio to regenerate them would defeat the point.

Radiance RGBE stores each pixel as three mantissa bytes and a shared exponent.
Averaging has to happen in linear space, so pixels are decoded to float, boxed
down, and re-encoded.

    python tools/downsample_hdr.py in.hdr out.hdr --width 1024
"""

import argparse
import struct
import sys


def read_hdr(path):
    """Return (width, height, [ (r,g,b) floats ]) from a Radiance file."""
    with open(path, "rb") as f:
        data = f.read()

    if not data.startswith(b"#?"):
        raise ValueError("not a Radiance file (missing #? signature)")

    # Header: text lines, terminated by a blank line, then the resolution line.
    pos = data.index(b"\n") + 1
    while True:
        end = data.index(b"\n", pos)
        line = data[pos:end]
        pos = end + 1
        if not line.strip():
            break

    end = data.index(b"\n", pos)
    resolution = data[pos:end].decode("ascii").split()
    pos = end + 1
    if len(resolution) != 4 or resolution[0] != "-Y" or resolution[2] != "+X":
        raise ValueError("unsupported scanline order: " + " ".join(resolution))
    height = int(resolution[1])
    width = int(resolution[3])

    pixels = [None] * (width * height)

    for y in range(height):
        row = decode_scanline(data, pos, width)
        pos = row[1]
        rgbe = row[0]
        base = y * width
        for x in range(width):
            r, g, b, e = rgbe[x * 4:x * 4 + 4]
            if e == 0:
                pixels[base + x] = (0.0, 0.0, 0.0)
            else:
                scale = 2.0 ** (e - 136)  # 2^(e-128) / 256
                pixels[base + x] = (r * scale, g * scale, b * scale)

    return width, height, pixels


def decode_scanline(data, pos, width):
    """Decode one scanline, RLE or flat. Returns (bytes, new_pos)."""
    out = bytearray(width * 4)

    rle = (4 <= width <= 0x7FFF
           and data[pos] == 2 and data[pos + 1] == 2
           and ((data[pos + 2] << 8) | data[pos + 3]) == width)

    if not rle:
        # Flat scanline: interleaved RGBE, four bytes per pixel.
        out[:] = data[pos:pos + width * 4]
        return bytes(out), pos + width * 4

    pos += 4
    for channel in range(4):
        x = 0
        while x < width:
            count = data[pos]
            pos += 1
            if count > 128:
                # A run: one value repeated (count - 128) times.
                count -= 128
                value = data[pos]
                pos += 1
                for i in range(count):
                    out[(x + i) * 4 + channel] = value
            else:
                # A literal span of `count` distinct values.
                for i in range(count):
                    out[(x + i) * 4 + channel] = data[pos + i]
                pos += count
            x += count
    return bytes(out), pos


def box_downsample(width, height, pixels, out_width):
    """Average square blocks of pixels in linear space."""
    factor = width // out_width
    if factor < 1 or width % out_width != 0:
        raise ValueError(
            "target width %d must divide the source width %d exactly" % (out_width, width))
    out_height = height // factor
    inv = 1.0 / (factor * factor)

    out = [None] * (out_width * out_height)
    for oy in range(out_height):
        for ox in range(out_width):
            r = g = b = 0.0
            for sy in range(oy * factor, oy * factor + factor):
                base = sy * width + ox * factor
                for sx in range(factor):
                    pr, pg, pb = pixels[base + sx]
                    r += pr
                    g += pg
                    b += pb
            out[oy * out_width + ox] = (r * inv, g * inv, b * inv)
    return out_width, out_height, out


def encode_rgbe(r, g, b):
    peak = max(r, g, b)
    if peak < 1e-32:
        return (0, 0, 0, 0)
    mantissa, exponent = frexp_pair(peak)
    scale = mantissa * 256.0 / peak
    return (min(255, int(r * scale)),
            min(255, int(g * scale)),
            min(255, int(b * scale)),
            exponent + 128)


def frexp_pair(value):
    import math
    mantissa, exponent = math.frexp(value)
    return mantissa, exponent


def write_hdr(path, width, height, pixels):
    with open(path, "wb") as f:
        f.write(b"#?RADIANCE\n")
        f.write(b"# Downsampled by tools/downsample_hdr.py\n")
        f.write(b"FORMAT=32-bit_rle_rgbe\n\n")
        f.write(("-Y %d +X %d\n" % (height, width)).encode("ascii"))
        # Flat scanlines: simpler than RLE, and these files are small by design.
        row = bytearray(width * 4)
        for y in range(height):
            base = y * width
            for x in range(width):
                r, g, b, e = encode_rgbe(*pixels[base + x])
                row[x * 4:x * 4 + 4] = bytes((r, g, b, e))
            f.write(row)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source")
    parser.add_argument("destination")
    parser.add_argument("--width", type=int, default=1024,
                        help="target width; must divide the source width exactly")
    args = parser.parse_args()

    width, height, pixels = read_hdr(args.source)
    print("read  %s  %dx%d" % (args.source, width, height))

    out_w, out_h, out_pixels = box_downsample(width, height, pixels, args.width)
    write_hdr(args.destination, out_w, out_h, out_pixels)
    print("wrote %s  %dx%d" % (args.destination, out_w, out_h))
    return 0


if __name__ == "__main__":
    sys.exit(main())

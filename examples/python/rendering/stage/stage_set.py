"""
The stage example's set, written as glTF files and an HDR environment.

Nothing here is downloaded: the set is generated on first start into
assets/stage/set/ and reused after that (delete the directory, or bump
VERSION, to regenerate it). Sizes are in metres at a set scale of 1, which
fits a car; stage.py scales the set for larger models.

- hall.gltf: the room. A dark floor ("hall_floor"), black walls and ceiling
  ("hall_walls"), a stage deck in the middle ("deck") with a light strip
  along its front ("deck_strip"), a tribune of seat rows facing it
  ("tribune", "tribune_seats"), and a box truss over the deck on four towers
  ("truss").
- head.gltf: a moving head's head, a box with a round lens in its front,
  facing -Z, centred on its tilt axis ("head_body", "head_lens").
- yoke_floor.gltf / yoke_hung.gltf: a moving head's base and the yoke that
  holds the head, standing on the floor or hanging from the truss, with the
  head's centre at the origin ("yoke_base", "yoke_arms").
- smoke_machine.gltf: a box with a nozzle facing -Z ("smoke_body",
  "smoke_nozzle", "smoke_led"), standing on the origin.
- hall.hdr: an almost black environment.

The layout, in set units: the deck is DECK wide, deep and high, centred at
DECK_CENTRE; the truss is a rectangle TRUSS_HEIGHT above the floor with
corners at (+-TRUSS_X, TRUSS_Z0) and (+-TRUSS_X, TRUSS_Z1); the tribune's
rows start TRIBUNE_Z in front of the deck's centre.
"""

import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "showroom"))
import studio  # noqa: E402

VERSION = 1          # bump to regenerate existing sets

HALL = (18.0, 12.0, 18.0)            # half width, height, half depth
DECK = (14.0, 0.5, 10.0)             # width, height, depth
DECK_CENTRE = (0.0, -1.0)            # x, z
TRUSS_HEIGHT = 7.0
TRUSS_X = 6.5
TRUSS_Z0, TRUSS_Z1 = -5.5, 3.5
TRUSS_SECTION = 0.32
TRIBUNE_Z = 8.0
TRIBUNE_ROWS = 6
TRIBUNE_RISE, TRIBUNE_DEPTH, TRIBUNE_WIDTH = 0.45, 1.0, 16.0


# ── Mesh helpers ─────────────────────────────────────────────────────────

class Mesh:
    """Positions, normals, uvs and indices, which other meshes add to."""

    def __init__(self):
        self.p, self.n, self.u, self.i = [], [], [], []

    def add(self, p, n, u, i):
        base = len(self.p)
        self.p += p
        self.n += n
        self.u += u
        self.i += [base + k for k in i]
        return self

    def parts(self):
        return self.p, self.n, self.u, self.i


def cuboid(centre, size):
    """An axis-aligned box of `size` centred on `centre`."""
    p, n, u, i = studio.box(size[0], size[1], size[2], y0=-size[1] / 2.0)
    return [(x + centre[0], y + centre[1], z + centre[2]) for x, y, z in p], n, u, i


def _sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _unit(v):
    length = math.sqrt(sum(x * x for x in v)) or 1.0
    return tuple(x / length for x in v)


def strut(a, b, thickness):
    """A square bar from a to b, `thickness` across."""
    axis = _unit(_sub(b, a))
    side = _unit(_cross(axis, (0.0, 1.0, 0.0) if abs(axis[1]) < 0.9 else (1.0, 0.0, 0.0)))
    up = _cross(side, axis)
    h = thickness / 2.0
    corners = []
    for end in (a, b):
        for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            corners.append(tuple(end[k] + side[k] * sx * h + up[k] * sy * h for k in range(3)))
    positions, normals, uvs, indices = [], [], [], []
    # Four sides, each its own quad so the normals stay flat.
    for s in range(4):
        q = [corners[s], corners[(s + 1) % 4], corners[4 + (s + 1) % 4], corners[4 + s]]
        mid = tuple(sum(c[k] for c in q) / 4.0 for k in range(3))
        centre = tuple((a[k] + b[k]) / 2.0 for k in range(3))
        normal = _unit(_sub(mid, centre))
        base = len(positions)
        positions += q
        normals += [normal] * 4
        uvs += [(0, 0), (1, 0), (1, 1), (0, 1)]
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    return positions, normals, uvs, studio._orient(positions, normals, indices)


def disc(centre, radius, segments=32):
    """A disc facing -Z."""
    positions = [centre] + [(centre[0] + radius * math.cos(2 * math.pi * s / segments),
                             centre[1] + radius * math.sin(2 * math.pi * s / segments), centre[2])
                            for s in range(segments)]
    normals = [(0.0, 0.0, -1.0)] * len(positions)
    uvs = [(0.5, 0.5)] + [(0.5 + 0.5 * math.cos(2 * math.pi * s / segments),
                           0.5 + 0.5 * math.sin(2 * math.pi * s / segments)) for s in range(segments)]
    indices = []
    for s in range(segments):
        indices += [0, 1 + s, 1 + (s + 1) % segments]
    return positions, normals, uvs, studio._orient(positions, normals, indices)


def truss_beam(mesh, a, b, section=TRUSS_SECTION, chord=0.045, brace=0.025):
    """A box truss from a to b: four chords at the corners of a square
    `section` across, and zigzag braces on its four faces."""
    axis = _unit(_sub(b, a))
    side = _unit(_cross(axis, (0.0, 1.0, 0.0) if abs(axis[1]) < 0.9 else (1.0, 0.0, 0.0)))
    up = _cross(side, axis)
    h = section / 2.0 - chord / 2.0
    offsets = [(-h, -h), (h, -h), (h, h), (-h, h)]
    length = math.sqrt(sum(x * x for x in _sub(b, a)))

    def at(t, sx, sy):
        return tuple(a[k] + axis[k] * t + side[k] * sx + up[k] * sy for k in range(3))

    for sx, sy in offsets:
        mesh.add(*strut(at(0.0, sx, sy), at(length, sx, sy), chord))
    steps = max(1, int(round(length / section)))
    for face in range(4):
        (x0, y0), (x1, y1) = offsets[face], offsets[(face + 1) % 4]
        for s in range(steps):
            t0, t1 = length * s / steps, length * (s + 1) / steps
            if s % 2:
                mesh.add(*strut(at(t0, x0, y0), at(t1, x1, y1), brace))
            else:
                mesh.add(*strut(at(t0, x1, y1), at(t1, x0, y0), brace))


# ── The hall ─────────────────────────────────────────────────────────────

def deck_top():
    return DECK[1]


def hall(path):
    w = studio.GltfWriter()
    floor_m = w.material("hall_floor", (0.03, 0.03, 0.034), roughness=0.3)
    walls_m = w.material("hall_walls", (0.012, 0.012, 0.014), roughness=0.9)
    deck_m = w.material("deck", (0.02, 0.02, 0.022), metallic=0.0, roughness=0.45)
    strip_m = w.material("deck_strip", (0.02, 0.02, 0.02), roughness=0.4, emissive=(1.0, 1.0, 1.0))
    tribune_m = w.material("tribune", (0.11, 0.11, 0.115), roughness=0.85)
    seats_m = w.material("tribune_seats", (0.22, 0.015, 0.025), roughness=0.7)
    truss_m = w.material("truss", (0.55, 0.56, 0.58), metallic=1.0, roughness=0.35)

    hx, hy, hz = HALL
    floor = Mesh().add(*cuboid((0.0, -0.05, 0.0), (2 * hx, 0.1, 2 * hz)))
    walls = Mesh()
    t = 0.2
    walls.add(*cuboid((0.0, hy / 2.0, -hz - t / 2.0), (2 * hx + 2 * t, hy, t)))
    walls.add(*cuboid((0.0, hy / 2.0, hz + t / 2.0), (2 * hx + 2 * t, hy, t)))
    walls.add(*cuboid((-hx - t / 2.0, hy / 2.0, 0.0), (t, hy, 2 * hz)))
    walls.add(*cuboid((hx + t / 2.0, hy / 2.0, 0.0), (t, hy, 2 * hz)))
    walls.add(*cuboid((0.0, hy + t / 2.0, 0.0), (2 * hx + 2 * t, t, 2 * hz + 2 * t)))

    dx, dy, dz = DECK
    cx, cz = DECK_CENTRE
    deck = Mesh().add(*cuboid((cx, dy / 2.0, cz), DECK))
    strip = Mesh().add(*cuboid((cx, dy * 0.82, cz + dz / 2.0 + 0.012), (dx - 0.1, 0.05, 0.03)))

    tribune, seats = Mesh(), Mesh()
    for row in range(TRIBUNE_ROWS):
        top = TRIBUNE_RISE * (row + 1)
        z0 = TRIBUNE_Z + TRIBUNE_DEPTH * row
        tribune.add(*cuboid((0.0, top / 2.0, z0 + TRIBUNE_DEPTH / 2.0), (TRIBUNE_WIDTH, top, TRIBUNE_DEPTH)))
        seats.add(*cuboid((0.0, top + 0.06, z0 + 0.3), (TRIBUNE_WIDTH - 0.6, 0.12, 0.42)))
        seats.add(*cuboid((0.0, top + 0.3, z0 + 0.53), (TRIBUNE_WIDTH - 0.6, 0.45, 0.06)))
    back = TRIBUNE_Z + TRIBUNE_DEPTH * TRIBUNE_ROWS
    tribune.add(*cuboid((0.0, 1.6, back + 0.1), (TRIBUNE_WIDTH, 3.2, 0.2)))

    truss = Mesh()
    y = TRUSS_HEIGHT
    h = TRUSS_SECTION / 2.0
    corners = [(-TRUSS_X, TRUSS_Z0), (TRUSS_X, TRUSS_Z0), (TRUSS_X, TRUSS_Z1), (-TRUSS_X, TRUSS_Z1)]
    for k in range(4):
        (x0, z0), (x1, z1) = corners[k], corners[(k + 1) % 4]
        # The beams run between the towers' faces.
        a = (x0 + (h if x1 > x0 else -h if x1 < x0 else 0.0), y, z0 + (h if z1 > z0 else -h if z1 < z0 else 0.0))
        b = (x1 - (h if x1 > x0 else -h if x1 < x0 else 0.0), y, z1 - (h if z1 > z0 else -h if z1 < z0 else 0.0))
        truss_beam(truss, a, b)
    for x, z in corners:
        truss_beam(truss, (x, dy, z), (x, y + h, z))
        truss.add(*cuboid((x, dy + 0.01, z), (0.7, 0.02, 0.7)))

    for name, mesh, material in (("hall_floor", floor, floor_m), ("hall_walls", walls, walls_m),
                                 ("deck", deck, deck_m), ("deck_strip", strip, strip_m),
                                 ("tribune", tribune, tribune_m), ("tribune_seats", seats, seats_m),
                                 ("truss", truss, truss_m)):
        w.node(name, w.mesh(name, *mesh.parts(), material))
    w.write(path)


# ── Fixtures ─────────────────────────────────────────────────────────────

HEAD_SIZE = (0.26, 0.26, 0.38)
LENS_RADIUS = 0.095
YOKE_REACH = 0.25        # from the head's centre to the yoke's crossbar


def head(path):
    w = studio.GltfWriter()
    body_m = w.material("head_body", (0.025, 0.025, 0.028), metallic=0.3, roughness=0.45)
    lens_m = w.material("head_lens", (1.0, 1.0, 1.0), roughness=0.1, emissive=(1.0, 1.0, 1.0))
    body = Mesh().add(*cuboid((0.0, 0.0, 0.0), HEAD_SIZE))
    lens = Mesh().add(*disc((0.0, 0.0, -HEAD_SIZE[2] / 2.0 - 0.004), LENS_RADIUS))
    w.node("head_body", w.mesh("head_body", *body.parts(), body_m))
    w.node("head_lens", w.mesh("head_lens", *lens.parts(), lens_m))
    w.write(path)


def yoke(path, hung):
    """Base and yoke; the yoke's arms reach up to the head (or down to it
    when hung)."""
    w = studio.GltfWriter()
    metal_m = w.material("yoke_arms", (0.03, 0.03, 0.033), metallic=0.3, roughness=0.5)
    base_m = w.material("yoke_base", (0.02, 0.02, 0.022), metallic=0.2, roughness=0.55)
    s = 1.0 if hung else -1.0       # which way from the head the base lies (+Y when hung)
    arms = Mesh()
    for x in (-0.165, 0.165):
        arms.add(*cuboid((x, s * YOKE_REACH / 2.0, 0.0), (0.04, YOKE_REACH + 0.04, 0.09)))
    arms.add(*cuboid((0.0, s * YOKE_REACH, 0.0), (0.37, 0.05, 0.11)))
    base = Mesh().add(*cuboid((0.0, s * (YOKE_REACH + 0.1), 0.0), (0.36, 0.15, 0.36)))
    w.node("yoke_arms", w.mesh("yoke_arms", *arms.parts(), metal_m))
    w.node("yoke_base", w.mesh("yoke_base", *base.parts(), base_m))
    w.write(path)


def yoke_base_offset():
    """From the head's centre to the far side of the base."""
    return YOKE_REACH + 0.175


def smoke_machine(path):
    w = studio.GltfWriter()
    body_m = w.material("smoke_body", (0.06, 0.06, 0.065), metallic=0.4, roughness=0.4)
    nozzle_m = w.material("smoke_nozzle", (0.15, 0.15, 0.15), metallic=1.0, roughness=0.3)
    led_m = w.material("smoke_led", (0.0, 0.0, 0.0), emissive=(0.1, 1.0, 0.2))
    body = Mesh().add(*cuboid((0.0, 0.17, 0.0), (0.36, 0.3, 0.6)))
    nozzle = Mesh().add(*strut((0.0, 0.17, -0.3), (0.0, 0.17, -0.42), 0.07))
    led = Mesh().add(*cuboid((0.12, 0.25, 0.302), (0.03, 0.03, 0.006)))
    w.node("smoke_body", w.mesh("smoke_body", *body.parts(), body_m))
    w.node("smoke_nozzle", w.mesh("smoke_nozzle", *nozzle.parts(), nozzle_m))
    w.node("smoke_led", w.mesh("smoke_led", *led.parts(), led_m))
    w.write(path)


NOZZLE = (0.0, 0.17, -0.42)          # where the smoke leaves, from the machine's origin


def environment(path, width=256, height=128):
    rows = []
    for j in range(height):
        elevation = 90.0 - 180.0 * (j + 0.5) / height
        v = 0.0025 if elevation > 0.0 else 0.0015
        rows.append([(v, v, v * 1.1)] * width)
    studio._write_hdr(path, rows, width, height)


# ── Entry point ──────────────────────────────────────────────────────────

FILES = ("hall.gltf", "head.gltf", "yoke_floor.gltf", "yoke_hung.gltf", "smoke_machine.gltf", "hall.hdr")


def ensure(directory):
    """Write the set into `directory` unless it is already there, at this
    VERSION. Returns the directory."""
    marker = os.path.join(directory, "version.txt")
    try:
        with open(marker, encoding="utf-8") as f:
            current = f.read().strip() == str(VERSION)
    except OSError:
        current = False
    if current and all(os.path.exists(os.path.join(directory, name)) for name in FILES):
        return directory
    os.makedirs(directory, exist_ok=True)
    print("[stage] writing the set into %s" % directory)
    hall(os.path.join(directory, "hall.gltf"))
    head(os.path.join(directory, "head.gltf"))
    yoke(os.path.join(directory, "yoke_floor.gltf"), hung=False)
    yoke(os.path.join(directory, "yoke_hung.gltf"), hung=True)
    smoke_machine(os.path.join(directory, "smoke_machine.gltf"))
    environment(os.path.join(directory, "hall.hdr"))
    with open(marker, "w", encoding="utf-8") as f:
        f.write(str(VERSION))
    return directory


if __name__ == "__main__":
    ensure(sys.argv[1] if len(sys.argv) > 1 else "set")

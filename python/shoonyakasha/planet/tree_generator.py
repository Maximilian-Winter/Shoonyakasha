"""
Trees grown from species: a trunk and levels of branches, each a bark
tube bent along its length, and foliage cards from vegetation_textures'
atlas along the outer branches, in vegetation's conventions (unit height,
y up, the base at the origin; cards' normals out from the crown's centre).

A `Species` has its `levels` (`Level`: for every branch of the level
before, `children` branches from `start` to its tip, each `angle` degrees
from its parent, `length` and `radius` times its parent's at that point,
shorter towards the parent's tip by `shorten`, tapering to `taper` of its
radius at its tip, bent by up to `gnarliness` radians at each of its
`sections`, a tube of `segments` sides), a `force` its branches bend
towards (up for light, down for weight) by `force_strength` over their
length, a `twist` (radians per unit along a parent the children turn
round it), its trunk's radius and its `leaves` (`Leaves`: `count` cards on
each branch of the last level, from `start` along it, `size` long, of the
atlas cards `cards`, leaning `angle` degrees out from the branch, `up`
lifting them towards the sky; `flat` cards lie level, facing the sky;
`flatten` of the way from where a card grows to the highest card's
height is where it is put, so a crown can be a level layer).

A vertex's sway (vegetation's vertex layout) is how far out along the
branches it is: 0 on the trunk, (level - 1 + t) / (levels - 1) at t along a
branch of a later level, a card's its branch's there; its phase is its
branch's, a seeded number from 0 to 1.

`grow(species, seed)` grows one tree, the same for the same seed (a card
that would reach into the ground points up instead, or is lifted, and a
branch that would reach below GROUND runs level):
(vertices, indices, trunk) with vegetation's vertex layout, `trunk` its
trunk's radius at the ground and the height its first branch leaves it, as
shares of the tree's height. `SPECIES` holds the presets by the names of
vegetation.TREES.
"""

import math
from dataclasses import dataclass, field

import numpy as np

from . import vegetation_textures


GROUND = 0.02               # a branch's point lower than this (of the trunk's length) runs level instead


@dataclass
class Level:
    children: int = 0
    start: float = 0.0
    angle: float = 45.0
    length: float = 0.5
    radius: float = 0.5
    shorten: float = 0.0
    taper: float = 0.4
    gnarliness: float = 0.1
    sections: int = 3
    segments: int = 5


@dataclass
class Leaves:
    cards: tuple = ("broadleaf_a",)
    count: int = 6
    start: float = 0.3
    size: float = 0.2
    angle: float = 40.0
    up: float = 0.3
    flat: bool = False
    flatten: float = 0.0


@dataclass
class Species:
    name: str
    levels: list
    leaves: Leaves
    force: tuple = (0.0, 1.0, 0.0)
    force_strength: float = 0.0
    twist: float = 0.0
    trunk_radius: float = 0.03
    crown_normal: float = 0.75
    trunk_lean: float = 0.03


@dataclass
class Branch:
    origin: np.ndarray
    direction: np.ndarray
    length: float
    radius: float
    level: int
    phase: float = 0.0
    points: list = field(default_factory=list)
    radii: list = field(default_factory=list)


def unit(v):
    v = np.asarray(v, np.float64)
    n = np.linalg.norm(v)
    return v / n if n > 0 else v


def perpendicular(d):
    side = np.cross(d, [0.0, 0.0, 1.0] if abs(d[2]) < 0.9 else [1.0, 0.0, 0.0])
    return unit(side)


def rotate(v, axis, angle):
    """`v` turned `angle` radians about the unit `axis` (Rodrigues)."""
    axis = unit(axis)
    c, s = math.cos(angle), math.sin(angle)
    return v * c + np.cross(axis, v) * s + axis * np.dot(axis, v) * (1.0 - c)


def tilt(direction, angle, around):
    """`direction` tilted `angle` radians away from itself, towards the side `around` radians round it."""
    side = rotate(perpendicular(direction), direction, around)
    return unit(direction * math.cos(angle) + side * math.sin(angle))


def grow_branch(b, species, level, rng):
    """The points and radii along `b`, bent by the force and its gnarliness."""
    p, d = np.asarray(b.origin, np.float64), unit(b.direction)
    force = unit(species.force)
    for s in range(level.sections + 1):
        t = s / level.sections
        b.points.append(p.copy())
        b.radii.append(b.radius * (1.0 + (level.taper - 1.0) * t))
        d = unit(d + force * (species.force_strength / level.sections))
        if level.gnarliness > 0.0:
            d = tilt(d, rng.normal(0.0, level.gnarliness), rng.uniform(0.0, 2.0 * math.pi))
        if p[1] + d[1] * (b.length / level.sections) < GROUND:     # a branch reaching the ground runs level
            d = unit(np.array([d[0], max(d[1], 0.0), d[2]]))
        p = p + d * (b.length / level.sections)


def point_on(b, t):
    """The point, direction and radius `t` (0 .. 1) along branch `b`."""
    n = len(b.points) - 1
    x = min(max(t, 0.0), 1.0) * n
    i = min(int(x), n - 1)
    f = x - i
    p = b.points[i] * (1.0 - f) + b.points[i + 1] * f
    return p, unit(b.points[i + 1] - b.points[i]), b.radii[i] * (1.0 - f) + b.radii[i + 1] * f


def branches(species, rng):
    """Every branch of a tree, grown from the trunk out."""
    lean = tilt(np.array([0.0, 1.0, 0.0]), abs(rng.normal(0.0, species.trunk_lean)), rng.uniform(0.0, 2.0 * math.pi))
    trunk = Branch(np.array([0.0, -0.03, 0.0]), lean, 1.03, species.trunk_radius, 0)
    out, todo = [], [trunk]
    while todo:
        b = todo.pop()
        level = species.levels[b.level]
        grow_branch(b, species, level, rng)
        out.append(b)
        if b.level + 1 >= len(species.levels):
            continue
        child = species.levels[b.level + 1]
        start = rng.uniform(0.0, 2.0 * math.pi)
        for k in range(child.children):
            t = child.start + (1.0 - child.start) * (k + rng.uniform(0.2, 0.8)) / child.children
            p, d, r = point_on(b, t)
            around = start + 2.0 * math.pi * k * 0.381966 + species.twist * t + rng.uniform(-0.3, 0.3)
            angle = math.radians(child.angle) * rng.uniform(0.85, 1.15)
            length = b.length * child.length * (1.0 - child.shorten * t) * rng.uniform(0.85, 1.15)
            todo.append(Branch(p, tilt(d, angle, around), length, max(r * child.radius, 1e-3), b.level + 1,
                               float(rng.random())))
    return out


def sway(b, t, levels):
    """A point's sway `t` along branch `b` of a tree of `levels` levels."""
    if b.level == 0 or levels < 2:
        return 0.0
    return min((b.level - 1 + t) / (levels - 1), 1.0)


def tube(builder, b, segments, shade, levels):
    """A bark tube along branch `b`'s points, its rings turned along with it."""
    from .vegetation import PART_BARK
    side = perpendicular(unit(b.points[1] - b.points[0]))
    v = 0.0
    rings = []
    for i, (p, r) in enumerate(zip(b.points, b.radii)):
        d = unit((b.points[min(i + 1, len(b.points) - 1)] - b.points[max(i - 1, 0)]))
        side = unit(side - d * np.dot(side, d))
        other = np.cross(d, side)
        if i:
            v += np.linalg.norm(p - b.points[i - 1]) / (2.0 * math.pi * max(b.radii[0], 1e-3))
        ring = []
        moves = sway(b, i / (len(b.points) - 1), levels)
        for k in range(segments + 1):
            a = 2.0 * math.pi * k / segments
            n = math.cos(a) * side + math.sin(a) * other
            ring.append(builder.vertex(p + r * n, n, PART_BARK, shade, (k / segments, v), moves, b.phase))
        rings.append(ring)
    for a, c in zip(rings, rings[1:]):
        for k in range(segments):
            builder.indices += [a[k], c[k], c[k + 1], a[k], c[k + 1], a[k + 1]]


def grow(species, seed):
    """One tree of `species` from `seed`: (vertices, indices, (trunk radius, trunk height))."""
    from .vegetation import _Builder, _crown_shade
    rng = np.random.default_rng(seed)
    builder = _Builder()
    grown = branches(species, rng)
    last = len(species.levels) - 1
    for b in grown:
        level = species.levels[b.level]
        tube(builder, b, level.segments, 1.0, len(species.levels))
    leaves = species.leaves
    placed = []
    for b in grown:
        if b.level != last:
            continue
        for k in range(leaves.count):
            t = leaves.start + (1.0 - leaves.start) * (k + rng.uniform(0.0, 1.0)) / leaves.count
            p, d, _ = point_on(b, t)
            out = tilt(d, math.radians(leaves.angle) * rng.uniform(0.6, 1.2), rng.uniform(0.0, 2.0 * math.pi))
            out = unit(out + np.array([0.0, leaves.up, 0.0]))
            facing = unit(perpendicular(out) + rng.normal(0.0, 0.3, 3))
            if leaves.flat:
                out = unit(np.array([out[0], 0.15 * rng.normal(), out[2]]))
                facing = unit(np.array([0.0, 1.0, 0.0]) + rng.normal(0.0, 0.2, 3))
            placed.append((p, out, facing, leaves.size * rng.uniform(0.8, 1.2),
                           leaves.cards[int(rng.integers(len(leaves.cards)))],
                           sway(b, t, len(species.levels)), b.phase))
    if placed and leaves.flatten > 0.0:
        top = max(p[1] for p, *_ in placed)
        placed = [(p + np.array([0.0, (top - p[1]) * leaves.flatten, 0.0]), *rest) for p, *rest in placed]
    bases = np.array([p for p, *_ in placed]) if placed else np.zeros((1, 3))
    centre = bases.mean(axis=0)
    radii = np.maximum(np.abs(bases - centre).max(axis=0), 0.05)
    for p, out, facing, size, card, moves, phase in placed:
        if p[1] + out[1] * size < 0.0:            # a card that would reach into the ground points up
            out = unit(np.array([out[0], abs(out[1]), out[2]]))
        across = unit(np.cross(out, unit(facing)))
        lowest = min(p[1] + side * across[1] * size / 2 + reach * out[1] * size for side in (-1, 1) for reach in (0, 1))
        if lowest < 0.0:                           # and one whose side would is lifted
            p = p + np.array([0.0, -lowest, 0.0])
        tip = p + out * size
        builder.card(p, out, facing, size, size, vegetation_textures.card_rect(card), centre,
                     _crown_shade(p, centre, radii), _crown_shade(tip, centre, radii),
                     crown_normal=species.crown_normal, sway=moves, phase=phase)
    vertices, indices = builder.arrays()
    height = max(float(np.array(builder.vertices)[:, 1].max()), 1e-6)
    first = min((b for b in grown if b.level == 1), key=lambda b: b.origin[1], default=None)
    trunk_top = (first.origin[1] if first is not None else 0.6) / height
    return vertices, indices, (species.trunk_radius / height, float(trunk_top))


SPECIES = {
    "conifer_a": Species(               # a spruce: whorls of drooping branches up to the top
        "conifer_a",
        [Level(sections=6, segments=7, taper=0.1, gnarliness=0.02),
         Level(children=34, start=0.18, angle=95.0, length=0.32, radius=0.35, shorten=0.85, taper=0.3,
               gnarliness=0.05, sections=2, segments=3)],
        Leaves(("conifer_a", "conifer_b"), count=5, start=0.05, size=0.2, angle=25.0, up=0.15),
        force=(0.0, -1.0, 0.0), force_strength=0.25, twist=2.4, trunk_radius=0.028, crown_normal=0.6),
    "conifer_b": Species(               # a pine: a bare trunk under a rounded crown
        "conifer_b",
        [Level(sections=5, segments=7, taper=0.35, gnarliness=0.04),
         Level(children=9, start=0.55, angle=55.0, length=0.32, radius=0.45, shorten=0.5, taper=0.4,
               gnarliness=0.15, sections=3, segments=4),
         Level(children=4, start=0.3, angle=45.0, length=0.45, radius=0.6, taper=0.4, gnarliness=0.2,
               sections=2, segments=3)],
        Leaves(("conifer_a", "conifer_b"), count=5, start=0.1, size=0.2, angle=35.0, up=0.5),
        force=(0.0, 1.0, 0.0), force_strength=0.15, trunk_radius=0.034),
    "broadleaf_a": Species(             # a round crown
        "broadleaf_a",
        [Level(sections=4, segments=8, taper=0.6, gnarliness=0.06),
         Level(children=6, start=0.42, angle=48.0, length=0.48, radius=0.5, shorten=0.3, taper=0.45,
               gnarliness=0.18, sections=3, segments=5),
         Level(children=5, start=0.25, angle=40.0, length=0.5, radius=0.55, taper=0.4, gnarliness=0.25,
               sections=2, segments=3)],
        Leaves(("broadleaf_a", "broadleaf_b"), count=16, start=0.05, size=0.13, angle=45.0, up=0.3),
        force=(0.0, 1.0, 0.0), force_strength=0.1, trunk_radius=0.045),
    "broadleaf_b": Species(             # a tall oval crown
        "broadleaf_b",
        [Level(sections=4, segments=8, taper=0.5, gnarliness=0.05),
         Level(children=7, start=0.38, angle=30.0, length=0.45, radius=0.5, shorten=0.5, taper=0.45,
               gnarliness=0.15, sections=3, segments=5),
         Level(children=4, start=0.25, angle=35.0, length=0.45, radius=0.55, taper=0.4, gnarliness=0.25,
               sections=2, segments=3)],
        Leaves(("broadleaf_a", "broadleaf_b"), count=16, start=0.05, size=0.12, angle=40.0, up=0.4),
        force=(0.0, 1.0, 0.0), force_strength=0.25, trunk_radius=0.043),
    "fir": Species(                     # a narrow fir: level branches of flat sprays to the top
        "fir",
        [Level(sections=6, segments=7, taper=0.08, gnarliness=0.015),
         Level(children=40, start=0.12, angle=88.0, length=0.26, radius=0.3, shorten=0.9, taper=0.3,
               gnarliness=0.04, sections=2, segments=3)],
        Leaves(("fir_a", "fir_b"), count=7, start=0.05, size=0.18, angle=20.0, up=0.0, flat=True),
        force=(0.0, -1.0, 0.0), force_strength=0.08, twist=2.4, trunk_radius=0.026, crown_normal=0.6),
    "acacia": Species(                  # a flat-topped crown on a forked trunk
        "acacia",
        [Level(sections=3, segments=7, taper=0.7, gnarliness=0.08),
         Level(children=4, start=0.6, angle=42.0, length=0.75, radius=0.55, taper=0.45, gnarliness=0.12,
               sections=3, segments=5),
         Level(children=6, start=0.45, angle=75.0, length=0.4, radius=0.5, taper=0.4, gnarliness=0.1,
               sections=2, segments=3)],
        Leaves(("acacia",), count=14, start=0.05, size=0.19, angle=60.0, up=0.0, flat=True, flatten=0.6),
        force=(0.0, 1.0, 0.0), force_strength=0.1, trunk_radius=0.04, crown_normal=0.5),
}

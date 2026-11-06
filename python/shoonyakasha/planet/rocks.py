"""
The rocks the VegetationScatter pass places (vegetation_scatter.comp) and
the cells whose rock it leaves out.

A rock is named by its cell: (body, grid, face, cell u, cell v), `grid` 0
for the boulders (on the trees' grid) and 1 for the small rocks (on their
own finer grid). `TakenRocks` is the set of those whose rocks were taken
out of the scatter (their rocks moved into the physics), kept on the GPU
in the TakenRocks buffer: an open-addressing table of SLOTS entries, each
two words, (body | face << 1 | grid << 4 | cell u << 8, cell v), placed
by linear probing from `slot` (the same hash as the shader's takenSlot),
EMPTY in the first word where there is none. `upload()` sends it once it
changed, and gives the count for planet.vegetation.rocks.w; the table holds
at most MAX_TAKEN.

`clear_body(index)` puts back the rocks of one body.

`rock_grid(kind)` is the grid of a rock kind (vegetation.KINDS' "rock" or
"boulder"); ROCK_COLOURS the rocks' colour on each body, `rock_colour` a
rock's colour with an ore rock's tint (ORE_ROCKS, its resource's
ORE_TINTS, ORE_TINT_SHARE) and `tinted` a colour tinted by a rock's tint,
as drawn; SPECK_COLOURS an ore rock's specks' colour by resource; DENSITY
a plain rock's density and `density(resource)` a rock's by its resource
(DENSITIES).
"""

import numpy as np

from shoonyakasha.planet.resources import FERRITE

SLOTS = 8192                     # entries in TakenRocks (two words each)
EMPTY = 0xFFFFFFFF
MAX_TAKEN = SLOTS // 2           # rocks taken at most, so probes stay short
ROCK_KINDS = ("rock", "boulder")
DENSITY = 2600.0                 # kg/m^3
# kg/m^3 by resource id (shoonyakasha.planet.resources): none, ferrite, cuprite, quartz, amber, evaporite, ice,
# ilmenite, iridium.
DENSITIES = (DENSITY, 3400.0, 3000.0, 2650.0, 1100.0, 2200.0, 920.0, 4700.0, 8000.0)
# The rocks' colour by body index (vegetation_scatter.comp's PLANET_ROCK and MOON_ROCK), before the tint.
ROCK_COLOURS = {0: (0.39, 0.35, 0.33), 1: (0.2, 0.195, 0.185)}
# Ore rocks (vegetation_scatter.comp's and ore_colours.glsl's): the richness above which rocks are ore,
# the tint they turn towards and their specks' colour by resource id, and the tint's share at richness 0
# (twice that at 1).
ORE_ROCKS = 0.3
ORE_TINTS = ((0.46, 0.25, 0.13), (0.46, 0.25, 0.13), (0.30, 0.42, 0.33), (0.78, 0.80, 0.84),
             (0.36, 0.22, 0.10), (0.85, 0.80, 0.76), (0.62, 0.72, 0.82), (0.07, 0.07, 0.08),
             (0.05, 0.05, 0.07))
SPECK_COLOURS = ((1.00, 0.72, 0.28), (1.00, 0.50, 0.20), (0.35, 1.00, 0.55), (0.65, 0.85, 1.00),
                 (1.00, 0.78, 0.28), (1.00, 0.60, 0.75), (0.70, 0.90, 1.00), (0.80, 0.85, 0.90),
                 (0.70, 0.45, 1.00))
ORE_TINT_SHARE = 0.35


def density(resource):
    return DENSITIES[min(int(resource), len(DENSITIES) - 1)]


def speck_colour(resource):
    return SPECK_COLOURS[min(int(resource), len(SPECK_COLOURS) - 1)]


def tinted(colour, tint):
    """A colour tinted as vegetation.vert tints a plant's: 80 % to 115 %."""
    shade = 0.8 + 0.35 * tint
    return tuple(c * shade for c in colour)


def rock_colour(body, grade=0.0, resource=FERRITE):
    """A rock's colour on body index `body` before the tint: an ore rock's
    (`grade` above ORE_ROCKS) turned towards its resource's ORE_TINTS as the
    scatter turns it."""
    colour = ROCK_COLOURS[body]
    if grade <= ORE_ROCKS:
        return colour
    share = ORE_TINT_SHARE * (1.0 + grade)
    tint = ORE_TINTS[min(int(resource), len(ORE_TINTS) - 1)]
    return tuple(c + (t - c) * share for c, t in zip(colour, tint))


def rock_grid(kind):
    return 1 if kind == "rock" else 0


def taken_words(key):
    """The table's two words for a rock's key (body, grid, face, cu, cv)."""
    body, grid, face, cu, cv = key
    return (body | (face << 1) | (grid << 4) | ((cu & 0xFFFF) << 8)) & 0xFFFFFFFF, cv & 0xFFFFFFFF


def slot(words):
    """The slot a key's probe starts at (vegetation_scatter.comp's takenSlot)."""
    a, b = words
    return (((a * 0x9E3779B1) & 0xFFFFFFFF) ^ ((b * 0x85EBCA77) & 0xFFFFFFFF) ^ (a >> 16)) & (SLOTS - 1)


class TakenRocks:
    def __init__(self, engine):
        self.engine = engine
        self.keys = set()
        self.changed = True

    def __contains__(self, key):
        return key in self.keys

    def __len__(self):
        return len(self.keys)

    def add(self, key):
        """Take the rock of `key` out of the scatter; False when the table is full."""
        if key in self.keys:
            return True
        if len(self.keys) >= MAX_TAKEN:
            return False
        self.keys.add(key)
        self.changed = True
        return True

    def discard(self, key):
        if key in self.keys:
            self.keys.discard(key)
            self.changed = True

    def clear(self):
        if self.keys:
            self.keys.clear()
            self.changed = True

    def clear_body(self, body):
        """The rocks of body index `body` put back."""
        for key in [k for k in self.keys if k[0] == body]:
            self.discard(key)

    def table(self):
        """The TakenRocks buffer's words."""
        words = np.full(2 * SLOTS, EMPTY, dtype=np.uint32)
        for key in sorted(self.keys):
            entry = taken_words(key)
            s = slot(entry)
            while words[2 * s] != EMPTY:
                s = (s + 1) & (SLOTS - 1)
            words[2 * s], words[2 * s + 1] = entry
        return words

    def upload(self):
        """The table sent to the GPU when it changed; the count of rocks taken."""
        if self.changed:
            self.engine.set_buffer("TakenRocks", self.table())
            self.changed = False
        return len(self.keys)

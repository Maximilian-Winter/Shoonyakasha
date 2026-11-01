"""
The trees, cacti and rocks near the camera, from the NearbyTrees buffer
that the VegetationScatter pass fills as it places the vegetation and
the rocks (vegetation_scatter.comp).

`NearbyTrees.request(camera, radius, centres)` asks for those within
`radius` metres of the camera this frame, with a stamp (in
planet.vegetation.nearby) kept with the camera's position and the bodies'
centres; `NearbyTrees.read()` gives the list that came back (some frames
later), each one's base from its body's centre as it was that frame (so a
moving body's plants stay where they are on it), or None when no new list
came back. Every plant with a trunk (the trees' kinds and the
cactus) and every rock is listed, up to CAPACITY.

A `NearbyTree` is named by its cell (`key`: its body, grid, cube face and
cell, as rocks' keys, the same every frame), with its kind
(vegetation.KINDS), its base on the drawn surface, its height, its width
factor (as vegetation.glsl's vegetationWidth, from its tint), its turn
about its up (cosine, sine) and its tint. `rock` tells a rock. `trunk`
gives a plant's trunk as a column: (radius, height) in metres, from
TRUNKS, the share of the plant's height each mesh's trunk has
(vegetation.py) near its foot and up to where it thins or forks.

`listed_keys(data)` gives the stamp and the keys of a NearbyTrees
readback's bytes, without taking it.
"""

from dataclasses import dataclass

import numpy as np

from . import vegetation
from .rocks import ROCK_KINDS, rock_grid

CAPACITY = 1024                  # plants NearbyTrees holds (vegetation.nearby.z)
ROCK_INDEX = vegetation.KINDS.index("rock")   # the small rocks' kind, on grid 1
HEADER = 4                       # words before the first plant: count, stamp, 2 unused
WORDS = 12                       # words per plant

# Per kind: (trunk radius, trunk height) as shares of the plant's height, before its width factor.
TRUNKS = {
    "conifer_a": (0.027, 0.7),
    "conifer_b": (0.032, 0.8),
    "broadleaf_a": (0.043, 0.5),
    "broadleaf_b": (0.043, 0.5),
    "acacia": (0.038, 0.45),
    "cactus": (0.095, 0.95),
}


def width_factor(tint):
    """vegetation.glsl's vegetationWidth: 0.85 to 1.15 from the plant's tint."""
    t = np.float32(tint) * np.float32(7.31)
    return float(0.85 + 0.3 * (t - np.floor(t)))


@dataclass
class NearbyTree:
    key: tuple                   # (body, grid, face, cell u, cell v), as rocks' keys
    kind: str
    base: tuple                  # from its body's centre
    height: float                # m
    width: float                 # the width factor
    turn: tuple = (1.0, 0.0)     # cosine and sine of its turn about its up
    tint: float = 0.0            # 0 .. 1

    @property
    def body(self):
        return self.key[0]

    @property
    def rock(self):
        return self.kind in ROCK_KINDS

    @property
    def trunk(self):
        """(radius, height) of its trunk in metres."""
        name = self.kind[:-4] if self.kind.endswith("_far") else self.kind
        radius, top = TRUNKS[name]
        return radius * self.height * self.width, top * self.height


def key_of(row):
    """The key of a listed row of WORDS words."""
    kind = vegetation.KINDS[int(row[5])]
    return (int(row[9]), rock_grid(kind) if kind in ROCK_KINDS else 0, int(row[6]),
            int(row[7].view(np.int32)), int(row[8].view(np.int32)))


def listed_keys(data):
    """(stamp, the set of keys listed) of NearbyTrees' bytes `data`."""
    words = np.frombuffer(data, dtype=np.uint32)
    count = min(int(words[0]), CAPACITY)
    rows = words[HEADER:HEADER + count * WORDS].reshape(count, WORDS)
    return int(words[1]), {key_of(row) for row in rows}


class NearbyTrees:
    def __init__(self, engine):
        self.engine = engine
        self.stamp = 0
        self.cameras = {}            # stamp: the camera's position that frame

    def request(self, camera, radius, centres=((0.0, 0.0, 0.0),)):
        """List the plants with trunks and the rocks within `radius` m of
        `camera` (planet space) this frame; `centres` are the bodies'
        centres (planet space, by body index) this frame."""
        self.stamp += 1
        self.cameras[self.stamp] = (tuple(camera), tuple(tuple(c) for c in centres))
        self.engine.set_custom_vec4("planet.vegetation.nearby", (float(radius), float(self.stamp), float(CAPACITY),
                                                                 0.0))
        self.engine.set_buffer("NearbyTrees", np.zeros(HEADER, dtype=np.uint32))

    def read(self):
        """The list of NearbyTree that came back since the last call, or
        None. Stamps older than the one that came back are dropped."""
        readback = self.engine.get_readback("NearbyTrees")
        if readback is None:
            return None
        words = np.frombuffer(readback["data"], dtype=np.uint32)
        stamp = int(words[1])
        frame = self.cameras.pop(stamp, None)
        for older in [s for s in self.cameras if s < stamp]:
            del self.cameras[older]
        if frame is None:
            return None
        camera, centres = frame
        count = min(int(words[0]), CAPACITY)
        rows = words[HEADER:HEADER + count * WORDS].reshape(count, WORDS)
        floats = rows.view(np.float32)
        cells = rows.view(np.int32)
        body = rows[:, 9].astype(np.int64)
        known = np.array(list(centres) + [(0.0, 0.0, 0.0)], dtype=np.float64)
        base = np.asarray(camera, dtype=np.float64) + floats[:, 0:3].astype(np.float64) \
            - known[np.minimum(body, len(centres))]
        kinds = rows[:, 5].astype(np.int64)
        grid = (kinds == ROCK_INDEX).astype(np.int64)
        t = floats[:, 4] * np.float32(7.31)
        width = 0.85 + 0.3 * (t - np.floor(t)).astype(np.float64)
        keys = np.stack([body, grid, rows[:, 6].astype(np.int64), cells[:, 7], cells[:, 8]], axis=1).tolist()
        names = [vegetation.KINDS[k] for k in kinds.tolist()]
        return [NearbyTree(tuple(key), name, tuple(b), h, w, (c, s), tint)
                for key, name, b, h, w, c, s, tint in zip(keys, names, base.tolist(), floats[:, 3].tolist(),
                                                          width.tolist(), floats[:, 10].tolist(),
                                                          floats[:, 11].tolist(), floats[:, 4].tolist())]

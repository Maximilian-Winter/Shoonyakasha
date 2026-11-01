"""
The trees and cacti near the camera, from the NearbyTrees buffer that the
VegetationScatter pass fills as it places the planet's vegetation
(vegetation_scatter.comp).

`NearbyTrees.request(camera, radius)` asks for those within `radius`
metres of the camera this frame, with a stamp (in
planet.vegetation.nearby) kept with the camera's position;
`NearbyTrees.read()` gives the list that came back (some frames later),
each plant's base in planet space from that frame's camera, or None when
no new list came back. Every plant with a trunk is listed (up to CAPACITY):
the trees' kinds and the cactus.

A `NearbyTree` is named by its cell (`key`: its cube face and the cell of
the face's grid, the same every frame), with its kind (vegetation.KINDS),
its base on the drawn surface, its height and its width factor (as
vegetation.glsl's vegetationWidth). `trunk` gives its trunk as a column:
(radius, height) in metres, from TRUNKS, the share of the plant's height
each mesh's trunk has (vegetation.py) near its foot and up to where it
thins or forks.
"""

from dataclasses import dataclass

import numpy as np

from . import vegetation

CAPACITY = 256                   # plants NearbyTrees holds (vegetation.nearby.z)
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
    key: tuple                   # (face, cell u, cell v)
    kind: str
    base: tuple                  # planet space
    height: float                # m
    width: float                 # the width factor

    @property
    def trunk(self):
        """(radius, height) of its trunk in metres."""
        name = self.kind[:-4] if self.kind.endswith("_far") else self.kind
        radius, top = TRUNKS[name]
        return radius * self.height * self.width, top * self.height


class NearbyTrees:
    def __init__(self, engine):
        self.engine = engine
        self.stamp = 0
        self.cameras = {}            # stamp: the camera's position that frame

    def request(self, camera, radius):
        """List the plants with trunks within `radius` m of `camera` (planet space) this frame."""
        self.stamp += 1
        self.cameras[self.stamp] = tuple(camera)
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
        camera = self.cameras.pop(stamp, None)
        for older in [s for s in self.cameras if s < stamp]:
            del self.cameras[older]
        if camera is None:
            return None
        count = min(int(words[0]), CAPACITY)
        rows = words[HEADER:HEADER + count * WORDS].reshape(count, WORDS)
        floats = rows.view(np.float32)
        trees = []
        for row, f in zip(rows, floats):
            base = (camera[0] + float(f[0]), camera[1] + float(f[1]), camera[2] + float(f[2]))
            trees.append(NearbyTree((int(row[6]), int(row[7].view(np.int32)), int(row[8].view(np.int32))),
                                    vegetation.KINDS[int(row[5])], base, float(f[3]), width_factor(f[4])))
        return trees

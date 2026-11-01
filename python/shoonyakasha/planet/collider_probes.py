"""
The terrain under a physics bubble as a triangle mesh in its physics, from
heights the ColliderProbes pass measures on the GPU.

`ColliderPatch`: a square grid of COLUMNS x COLUMNS points SPACING metres
apart in the plane tangent to the body's sphere at the bubble's anchor
(Bubble.anchor, east and north), centred on a whole grid cell `cell` of
that plane; each point's direction is from the body's centre through it.
Patches of one anchor share their directions where they overlap. Heights
are the terrain's at those directions (the same function the chunks are
built from); on a body with a sea, heights below sea level count as sea
level.

`ColliderProbes`: hands a patch's directions to the ColliderProbes pass
(ColliderRequests, with its stamp in planet.collider.params) and takes the
heights back from ColliderHeights some frames later, by stamp. Results
asked for under an earlier terrain version (PlanetWorld.terrain_version)
are dropped.

`TerrainCollider`: keeps one patch as a static mesh body in the bubble's
physics around a point it is given each frame (the focus, ahead along its
velocity by the probes' latency), asking for a new patch once that point
is more than a quarter of the patch's width from the patch's centre or the
terrain changed. The mesh in use stays until the new one's heights come
back.
"""

import math

import numpy as np

from .vec import add, dot, normalize, scale, sub

COLUMNS = 65                     # grid points along each side
SPACING = 0.5                    # metres between grid points
CAPACITY = 4225                  # requests the ColliderProbes pass takes (ColliderRequests' elementCount)
RECENTRE_FRACTION = 0.25         # of the patch's width the focus may move from its centre


def plane_coordinates(body, anchor, east, north, point):
    """(x, y) in metres where the direction of `point` (relative to the
    body's centre) meets the plane tangent to the body's sphere at
    `anchor`, along east and north; None on the far side."""
    d = normalize(point)
    along = dot(d, anchor)
    if along <= 0.0:
        return None
    on_plane = sub(scale(d, body.radius / along), scale(anchor, body.radius))
    return dot(on_plane, east), dot(on_plane, north)


class ColliderPatch:
    def __init__(self, body, anchor, east, north, cell, columns=COLUMNS, spacing=SPACING):
        """The grid on `body` centred on grid cell `cell` (i, j) of the
        plane at the unit direction `anchor`, along `east` and `north`."""
        self.body = body
        self.anchor, self.east, self.north = anchor, east, north
        self.cell = cell
        self.columns = columns
        self.spacing = spacing
        mid = (columns - 1) // 2
        steps = (np.arange(columns, dtype=np.float64) - mid) * spacing
        x = cell[0] * spacing + steps[np.newaxis, :]
        y = cell[1] * spacing + steps[:, np.newaxis]
        plane = (np.asarray(anchor, dtype=np.float64) * body.radius
                 + x[..., np.newaxis] * np.asarray(east, dtype=np.float64)
                 + y[..., np.newaxis] * np.asarray(north, dtype=np.float64))
        # Row-major, (rows along north, columns along east, xyz).
        self.directions = plane / np.linalg.norm(plane, axis=2, keepdims=True)
        centre = add(scale(anchor, body.radius), add(scale(east, cell[0] * spacing), scale(north, cell[1] * spacing)))
        self.centre_direction = normalize(centre)
        self.heights = None

    @property
    def size(self):
        """Metres across the grid."""
        return (self.columns - 1) * self.spacing

    def set_heights(self, heights):
        """Terrain heights (m) at the directions, row by row."""
        h = np.asarray(heights, dtype=np.float64).reshape(self.columns, self.columns)
        self.heights = np.maximum(h, 0.0) if self.body.ocean else h

    def points(self):
        """The surface's points relative to the body's centre, shape (rows, columns, 3)."""
        return self.directions * (self.body.radius + self.heights)[..., np.newaxis]

    def offset(self, point):
        """(x, y) metres from the patch's centre along east and north to
        where `point`'s direction (relative to the body's centre) meets
        the anchor's plane; None on the far side."""
        xy = plane_coordinates(self.body, self.anchor, self.east, self.north, point)
        if xy is None:
            return None
        return xy[0] - self.cell[0] * self.spacing, xy[1] - self.cell[1] * self.spacing

    def covers(self, point, margin=0.0):
        """Whether `point` (relative to the body's centre) is over the
        grid, at least `margin` metres inside its edges."""
        xy = self.offset(point)
        half = 0.5 * self.size - margin
        return xy is not None and abs(xy[0]) <= half and abs(xy[1]) <= half

    def normal(self, point):
        """The unit normal of the grid triangle under `point` (relative to
        the body's centre), or None off the grid. Needs the heights."""
        xy = self.offset(point)
        if xy is None:
            return None
        mid = (self.columns - 1) // 2
        x, y = xy[0] / self.spacing + mid, xy[1] / self.spacing + mid
        last = self.columns - 1
        if not (0.0 <= x <= last and 0.0 <= y <= last):
            return None
        i, j = min(int(x), last - 1), min(int(y), last - 1)
        p = self.points()
        if (x - i) + (y - j) <= 1.0:
            a, b, c = p[j, i], p[j, i + 1], p[j + 1, i]
        else:
            a, b, c = p[j, i + 1], p[j + 1, i + 1], p[j + 1, i]
        n = np.cross(b - a, c - a)
        return tuple(float(v) for v in n / np.linalg.norm(n))


class ColliderProbes:
    def __init__(self, engine):
        self.engine = engine
        self.stamp = 0
        self.pending = {}            # stamp: (terrain version, patch)
        self.asked = False           # whether this frame asked for a patch
        self.idle = True             # whether the pass was last told to measure nothing

    def request(self, patch, version):
        """Ask for `patch`'s heights under terrain version `version`."""
        directions = patch.directions.reshape(-1, 3)
        count = len(directions)
        if count > CAPACITY:
            raise ValueError("a patch of %d points; the pass takes %d" % (count, CAPACITY))
        data = np.empty((count, 4), dtype=np.float32)
        data[:, :3] = directions
        data[:, 3] = float(patch.body.index)
        self.engine.set_buffer("ColliderRequests", data)
        self.stamp += 1
        self.engine.set_custom_vec4("planet.collider.params", (float(count), float(self.stamp), 0.0, 0.0))
        self.pending[self.stamp] = (version, patch)
        self.asked = True
        self.idle = False

    def read(self, version):
        """The patch whose heights came back this frame, with them set, or
        None. Requests older than the one that came back are dropped."""
        readback = self.engine.get_readback("ColliderHeights")
        if readback is None or not self.pending:
            return None
        rows = np.frombuffer(readback["data"], dtype=np.float32).reshape(-1, 4)
        stamp = int(rows[0, 1])
        entry = self.pending.pop(stamp, None)
        for older in [s for s in self.pending if s < stamp]:
            del self.pending[older]
        if entry is None or entry[0] != version:
            return None
        patch = entry[1]
        patch.set_heights(rows[:patch.columns * patch.columns, 0])
        return patch

    def is_pending(self, patch):
        return any(p is patch for _, p in self.pending.values())

    def end_frame(self):
        """After this frame's requests: the pass told to measure nothing
        when none was asked for."""
        if not self.asked and not self.idle:
            self.engine.set_custom_vec4("planet.collider.params", (0.0, float(self.stamp), 0.0, 0.0))
            self.idle = True
        self.asked = False

    def clear(self):
        self.pending.clear()


class TerrainCollider:
    def __init__(self, bubble, probes, columns=COLUMNS, spacing=SPACING, friction=0.8):
        self.bubble = bubble
        self.probes = probes
        self.columns = columns
        self.spacing = spacing
        self.friction = friction
        self.patch = None            # the patch the mesh body was built from
        self.version = None          # the terrain version it was measured under
        self.mesh = None             # its body id
        self.requested = None        # the patch asked for and not back yet
        self.builds = 0              # meshes built

    def update(self, focus, velocity, latency, version):
        """Take a patch that came back, and ask for a new one if needed:
        `focus` and `velocity` in bubble coordinates, `latency` the probes'
        latency in seconds, `version` the terrain's."""
        arrived = self.probes.read(version)
        if arrived is not None and arrived is self.requested:
            self.build(arrived, version)
            self.requested = None
        if self.requested is not None and not self.probes.is_pending(self.requested):
            self.requested = None
        if self.requested is not None:
            return
        ahead = self.bubble.body_relative(add(focus, scale(velocity, latency)))
        if self.patch is not None and self.version == version:
            xy = self.patch.offset(ahead)
            if xy is not None and max(abs(xy[0]), abs(xy[1])) <= RECENTRE_FRACTION * self.patch.size:
                return
        self.requested = ColliderPatch(self.bubble.body, self.bubble.anchor, self.bubble.east, self.bubble.north,
                                       self.cell(ahead), self.columns, self.spacing)
        self.probes.request(self.requested, version)

    def cell(self, point):
        """The grid cell of the bubble's anchor plane nearest under `point` (body-relative)."""
        b = self.bubble
        x, y = plane_coordinates(b.body, b.anchor, b.east, b.north, point)
        return int(math.floor(x / self.spacing + 0.5)), int(math.floor(y / self.spacing + 0.5))

    def build(self, patch, version):
        """`patch` as the mesh body, in place of the one before."""
        origin = np.asarray(self.bubble.origin, dtype=np.float64)
        centre = scale(patch.centre_direction, patch.body.radius)
        centre_bubble = sub(centre, self.bubble.origin)
        relative = (patch.points() - origin - np.asarray(centre_bubble, dtype=np.float64)).astype(np.float32)
        mesh = self.bubble.physics.create_grid_mesh(np.ascontiguousarray(relative), patch.columns, patch.columns,
                                                    centre_bubble, friction=self.friction)
        if self.mesh is not None:
            self.bubble.physics.remove(self.mesh)
        self.mesh, self.patch, self.version = mesh, patch, version
        self.builds += 1

    def covers(self, p, margin=0.0):
        """Whether bubble coordinates `p` are over the mesh, at least `margin` metres inside its edges."""
        return self.patch is not None and self.patch.covers(self.bubble.body_relative(p), margin)

    def drop(self):
        """The mesh body removed and no patch asked for."""
        if self.mesh is not None:
            self.bubble.physics.remove(self.mesh)
        self.mesh = self.patch = self.requested = self.version = None
        self.probes.clear()

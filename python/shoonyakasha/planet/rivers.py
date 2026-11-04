"""
Rivers traced over a grid of terrain heights (world.River).

`GridFrame(centre, radius, size, spacing)` is a square grid of `size` x
`size` points `spacing` metres apart in the plane tangent to the sphere of
`radius` at the unit `centre`, along vec.local_frame's east and north;
`directions()` gives each point's unit direction from the sphere's
centre, row by row (index j * size + i, i along east, j along north), as
the HeightGrid pass measures them (height_grid.comp). `to_plane` and
`from_plane` map unit directions to and from the plane (x east, y north,
metres from the centre's point, through the sphere's centre).

`trace(heights, frame, ...)` follows the water over a grid of heights:

- The heights are smoothed SMOOTH times by a 3 x 3 box, and those within
  an `avoid` circle ((unit direction, radius in metres) pairs) raised by
  AVOID_RAISE, so the water goes round them.
- The water drains to the grid's border and, with `sea`, to the cells at
  or below sea level. A priority flood from those fills every hollow up
  to where it spills (each filled cell FILL_STEP above the one it was
  reached from) and gives each cell the neighbour it drains to: the one
  it was reached from.
- Each cell's catchment is its own area and that of every cell draining
  through it. The cells whose catchment is at least MIN_AREA carry a
  river.
- A cell's water level is its smoothed height less INCISION, or the
  lowest level of a river cell draining into it if that is lower, and at
  least 0 with `sea`, so the level never rises downstream.
- The rivers are traced from each river cell no river cell drains into,
  downstream until a cell at or below sea level (with `sea`), the border,
  or a cell already traced (a junction). A river that ends at a junction
  stops short of the centre of the river it joins by that river's half
  width there.
- `wet`, a function of a unit direction giving a water level or None
  (such as world.lake_level), cuts the rivers where it gives a level:
  the parts in a lake are left out.
- Each step between a river's cells is split into SUBDIVIDE, its points
  moved by the meanders (MEANDER_WAVES: one displacement of the plane for
  every river, so rivers that meet stay together), smoothed SMOOTH_PATH
  times by Chaikin's corner cutting (its ends kept) and simplified by
  Douglas-Peucker to within SIMPLIFY; its levels made to never rise along
  it again, its width WIDTH_GAIN times the square root of the catchment
  in km2 (within WIDTH) and its depth DEPTH_SHARE of the width (within
  DEPTH). Rivers with fewer than 2 points are dropped, and the rivers
  whose own catchment is largest are kept first, up to MAX_SEGMENTS
  segments in all.

`pack(rivers, frame, radius)` lays the rivers out for the GPU (rivers.glsl):
the river data (LOOKUP x LOOKUP cells over the grid's square, each the
first and the count of its entries in the indices; the indices of the
segments within REACH of the cell, four to a vec4; then three vec4 per
segment) and the frame's four vec4 (TerrainParams' riverFrame).
`RiverLookup` answers the same questions in Python: `level(direction)`,
the water level where a river's water covers a unit direction, or None.
`ribbon(rivers, frame, radius)` gives the water's mesh along each river,
relative to the frame's centre point: two vertices per point at its half
width either side, each (position, across), (up, depth), and the
triangles' indices.
"""

import heapq
import math

import numpy as np

from .vec import local_frame, normalize

GRID = 512                       # points along each side of the height grid
SPACING = 200.0                  # m between them
SMOOTH = 1
AVOID_RAISE = 500.0              # m
FILL_STEP = 1e-3                 # m a filled cell stands above the one it drains to, at least
MIN_AREA = 6.0e6                 # m2 of catchment a river cell drains, at least
INCISION = 1.5                   # m the water lies below the smoothed ground
SMOOTH_PATH = 2
SUBDIVIDE = 4                    # steps each step between a river's cells is split into before the meanders
MEANDER_WAVES = (                # per axis of the plane: (amplitude m, wavelength m, direction rad, phase rad)
    ((28.0, 610.0, 0.3, 0.0), (18.0, 970.0, 1.9, 1.3), (11.0, 1530.0, 3.6, 2.9)),
    ((28.0, 650.0, 1.1, 0.7), (18.0, 1030.0, 2.6, 2.2), (11.0, 1470.0, 4.4, 0.4)),
)
SIMPLIFY = 5.0                   # m a simplified river's centre line strays from its smoothed one, at most
MAX_SEGMENTS = 8000              # segments of all the rivers, at most
WIDTH_GAIN = 4.0                 # m per square root of km2 of catchment
WIDTH = (3.0, 40.0)              # m
DEPTH_SHARE = 0.12
DEPTH = (0.8, 4.0)               # m
LOOKUP = 64                      # cells along each side of the lookup grid
BANK, BANK_SLOPE, BANK_MAX = 8.0, 2.5, 120.0   # m, m per m above the water, m: the banks (rivers.glsl)
REACH = 0.5 * WIDTH[1] + BANK_MAX               # m from a segment's centre line its ground can change, at most
MAX_DATA = 49152                 # vec4 of the river data (pipeline.json's RiverData)
MAX_VERTICES = 65536             # the ribbon's vertices (RiverVertices holds two vec4 each)
MAX_INDICES = 196608             # the ribbon's indices (RiverIndices)

_NEIGHBOURS = ((-1, -1), (0, -1), (1, -1), (-1, 0), (1, 0), (-1, 1), (0, 1), (1, 1))


class GridFrame:
    def __init__(self, centre, radius, size=GRID, spacing=SPACING):
        self.centre = np.array(normalize(centre), dtype=np.float64)
        east, north = local_frame(tuple(self.centre))
        self.east, self.north = np.array(east), np.array(north)
        self.radius, self.size, self.spacing = float(radius), int(size), float(spacing)

    def offsets(self):
        """Metres from the centre of each point along a side."""
        return (np.arange(self.size) - 0.5 * (self.size - 1)) * self.spacing

    def from_plane(self, x, y):
        """Unit directions (..., 3) through the plane's points (x east, y north)."""
        x, y = np.asarray(x, dtype=np.float64), np.asarray(y, dtype=np.float64)
        p = (self.centre * self.radius + x[..., None] * self.east + y[..., None] * self.north)
        return p / np.linalg.norm(p, axis=-1, keepdims=True)

    def directions(self):
        o = self.offsets()
        x, y = np.meshgrid(o, o)
        return self.from_plane(x, y).reshape(-1, 3)

    def to_plane(self, directions):
        """(x, y) in the plane of unit directions (..., 3) on the near side."""
        d = np.asarray(directions, dtype=np.float64)
        along = d @ self.centre
        p = d * (self.radius / along)[..., None] - self.centre * self.radius
        return p @ self.east, p @ self.north


def _smooth(h, times):
    for _ in range(times):
        padded = np.pad(h, 1, mode="edge")
        h = sum(padded[1 + dj:1 + dj + h.shape[0], 1 + di:1 + di + h.shape[1]]
                for dj in (-1, 0, 1) for di in (-1, 0, 1)) / 9.0
    return h


def _flood(h, outlets):
    """Priority flood from `outlets`: (filled heights, the flat index each
    cell drains to or -1, the flat indices in the order they were reached)."""
    n = h.shape[0]
    flat = h.ravel()
    filled = flat.copy()
    receiver = np.full(n * n, -1, dtype=np.int64)
    done = outlets.ravel().copy()
    heap = [(float(flat[k]), int(k)) for k in np.flatnonzero(done)]
    heapq.heapify(heap)
    order = []
    while heap:
        level, k = heapq.heappop(heap)
        order.append(k)
        j, i = divmod(k, n)
        for di, dj in _NEIGHBOURS:
            ii, jj = i + di, j + dj
            if 0 <= ii < n and 0 <= jj < n:
                m = jj * n + ii
                if not done[m]:
                    done[m] = True
                    filled[m] = max(float(flat[m]), level + FILL_STEP)
                    receiver[m] = k
                    heapq.heappush(heap, (float(filled[m]), m))
    return filled.reshape(h.shape), receiver, np.array(order, dtype=np.int64)


def _chaikin(points, times):
    """Corner cutting of an (n, k) array of points, its ends kept."""
    for _ in range(times):
        if len(points) < 3:
            return points
        a, b = points[:-1], points[1:]
        cut = np.empty((2 * len(a), points.shape[1]))
        cut[0::2] = 0.75 * a + 0.25 * b
        cut[1::2] = 0.25 * a + 0.75 * b
        points = np.vstack([points[:1], cut[1:-1], points[-1:]])
    return points


def width_for(area):
    return float(np.clip(WIDTH_GAIN * math.sqrt(area / 1e6), *WIDTH))


def depth_for(width):
    return float(np.clip(DEPTH_SHARE * width, *DEPTH))


def trace(heights, frame, sea=True, avoid=(), wet=None):
    """The rivers (world.River) over `heights`, a (size, size) array of the
    terrain's heights at `frame`'s directions in their order."""
    from .world import River
    n = frame.size
    h = _smooth(np.asarray(heights, dtype=np.float64).reshape(n, n), SMOOTH)
    o = frame.offsets()
    gx, gy = np.meshgrid(o, o)
    if avoid:
        directions = frame.directions().reshape(n, n, 3)
        for centre, radius in avoid:
            angle = np.arccos(np.clip(directions @ np.array(normalize(centre)), -1.0, 1.0))
            h = np.where(angle * frame.radius < radius, h + AVOID_RAISE, h)
    outlets = np.zeros((n, n), dtype=bool)
    outlets[0, :] = outlets[-1, :] = outlets[:, 0] = outlets[:, -1] = True
    if sea:
        outlets |= h <= 0.0
    _, receiver, order = _flood(h, outlets)
    cell_area = frame.spacing * frame.spacing
    area = np.full(n * n, cell_area)
    for k in order[::-1]:
        r = receiver[k]
        if r >= 0:
            area[r] += area[k]
    river = area >= MIN_AREA
    flat_h = h.ravel()
    level = flat_h - INCISION
    for k in order[::-1]:
        r = receiver[k]
        if river[k] and r >= 0 and river[r]:
            level[r] = min(level[r], level[k])
    if sea:
        level = np.maximum(level, 0.0)
    inflow = np.zeros(n * n, dtype=np.int64)
    for k in np.flatnonzero(river):
        if receiver[k] >= 0 and river[receiver[k]]:
            inflow[receiver[k]] += 1
    sea_cell = (flat_h <= 0.0) if sea else np.zeros(n * n, dtype=bool)
    traced = np.zeros(n * n, dtype=bool)
    sources = [k for k in np.flatnonzero(river & (inflow == 0)) if not sea_cell[k]]
    sources.sort(key=lambda k: (-area[k], k))
    paths = []
    for k in sources:
        cells, joined = [], False
        while True:
            cells.append(k)
            if traced[k]:
                joined = True
                break
            traced[k] = True
            r = receiver[k]
            if r < 0 or sea_cell[k] or not river[r]:
                break
            k = r
        paths.append((cells, joined))
    found = []
    for cells, joined in paths:
        cells = np.array(cells)
        own = cells[:-1] if joined and len(cells) > 1 else cells
        catchment = float(area[own].max())
        xy = np.stack([gx.ravel()[cells], gy.ravel()[cells]], axis=1)
        values = np.column_stack([xy, level[cells], area[cells]])
        if joined:
            values = _cut_end(values, 0.5 * width_for(area[cells[-1]]))
        values = _subdivide(values, SUBDIVIDE)
        values[:, :2] = _warp(values[:, :2])
        for piece in _dry_pieces(values, frame, wet):
            smooth = _simplify(_chaikin(piece, SMOOTH_PATH), SIMPLIFY)
            if len(smooth) < 2:
                continue
            levels = np.minimum.accumulate(smooth[:, 2])
            widths = [width_for(a) for a in smooth[:, 3]]
            points = [tuple(d) for d in frame.from_plane(smooth[:, 0], smooth[:, 1])]
            found.append((catchment, River(tuple(points), tuple(float(v) for v in levels), tuple(widths),
                                           tuple(depth_for(w) for w in widths))))
    found.sort(key=lambda c: -c[0])
    rivers, segments = [], 0
    for _, river in found:
        if segments + len(river.points) - 1 > MAX_SEGMENTS:
            continue
        segments += len(river.points) - 1
        rivers.append(river)
    return rivers


def _simplify(points, tolerance):
    """Douglas-Peucker on an (n, k) array of points by their first two
    columns: the points more than `tolerance` from the line through those
    kept around them, and the ends."""
    if len(points) < 3:
        return points
    keep = np.zeros(len(points), dtype=bool)
    keep[0] = keep[-1] = True
    stack = [(0, len(points) - 1)]
    xy = points[:, :2]
    while stack:
        a, b = stack.pop()
        if b - a < 2:
            continue
        line = xy[b] - xy[a]
        length = float(np.hypot(*line))
        rel = xy[a + 1:b] - xy[a]
        if length < 1e-9:
            distance = np.hypot(rel[:, 0], rel[:, 1])
        else:
            distance = np.abs(rel[:, 0] * line[1] - rel[:, 1] * line[0]) / length
        k = int(np.argmax(distance))
        if distance[k] > tolerance:
            keep[a + 1 + k] = True
            stack += [(a, a + 1 + k), (a + 1 + k, b)]
    return points[keep]


def _subdivide(values, parts):
    """An (n, k) array of points with each step between them split into `parts` equal steps."""
    if len(values) < 2 or parts <= 1:
        return values
    t = np.arange(parts) / parts
    steps = values[:-1, None, :] + (values[1:] - values[:-1])[:, None, :] * t[None, :, None]
    return np.vstack([steps.reshape(-1, values.shape[1]), values[-1:]])


def _warp(xy):
    """Plane points (n, 2) moved by the meanders' displacement: for each
    axis, the sum of MEANDER_WAVES' waves (amplitude in metres, wavelength
    in metres, direction in radians, phase), the same for every river, so
    rivers that meet stay together."""
    out = xy.astype(np.float64).copy()
    for axis, waves in enumerate(MEANDER_WAVES):
        for amplitude, wavelength, angle, phase in waves:
            k = 2.0 * math.pi / wavelength * np.array([math.cos(angle), math.sin(angle)])
            out[:, axis] += amplitude * np.sin(xy @ k + phase)
    return out


def _cut_end(values, back):
    """`values` (points with x, y first) with its end pulled back along it by `back` metres."""
    left = back
    while len(values) >= 2 and left > 0.0:
        step = float(np.hypot(*(values[-1, :2] - values[-2, :2])))
        if step > left:
            t = left / step
            values = values.copy()
            values[-1] = values[-1] + (values[-2] - values[-1]) * t
            return values
        left -= step
        values = values[:-1]
    return values


def _dry_pieces(values, frame, wet):
    """The runs of `values` whose points `wet` gives no level for."""
    if wet is None:
        return [values]
    directions = frame.from_plane(values[:, 0], values[:, 1])
    dry = [wet(tuple(d)) is None for d in directions]
    pieces, start = [], None
    for k, ok in enumerate(dry + [False]):
        if ok and start is None:
            start = k
        elif not ok and start is not None:
            pieces.append(values[start:k])
            start = None
    return pieces


def _segments(rivers):
    """(a, b, level a, level b, width a, width b, depth a, depth b) of every segment."""
    out = []
    for river in rivers:
        for k in range(len(river.points) - 1):
            out.append((river.points[k], river.points[k + 1], river.levels[k], river.levels[k + 1],
                        river.widths[k], river.widths[k + 1], river.depths[k], river.depths[k + 1]))
    return out


def _cell_lists(segments, frame, radius):
    """For each lookup cell (row by row), the indices of the segments within REACH of it."""
    cell = frame.size * frame.spacing / LOOKUP
    half = 0.5 * LOOKUP * cell
    lists = [[] for _ in range(LOOKUP * LOOKUP)]
    if not segments:
        return lists, cell
    a = np.array([s[0] for s in segments])
    b = np.array([s[1] for s in segments])
    ax, ay = frame.to_plane(a)
    bx, by = frame.to_plane(b)
    for index in range(len(segments)):
        lo_x, hi_x = min(ax[index], bx[index]) - REACH, max(ax[index], bx[index]) + REACH
        lo_y, hi_y = min(ay[index], by[index]) - REACH, max(ay[index], by[index]) + REACH
        i0, i1 = max(int(math.floor((lo_x + half) / cell)), 0), min(int(math.floor((hi_x + half) / cell)), LOOKUP - 1)
        j0, j1 = max(int(math.floor((lo_y + half) / cell)), 0), min(int(math.floor((hi_y + half) / cell)), LOOKUP - 1)
        for j in range(j0, j1 + 1):
            for i in range(i0, i1 + 1):
                lists[j * LOOKUP + i].append(index)
    return lists, cell


def pack(rivers, frame, radius):
    """(data, riverFrame): the river data as a (MAX_DATA, 4) float32 array
    and TerrainParams' four riverFrame vec4."""
    segments = _segments(rivers)
    lists, cell = _cell_lists(segments, frame, radius)
    indices = [k for cell_list in lists for k in cell_list]
    index_rows = (len(indices) + 3) // 4
    first_index, first_segment = LOOKUP * LOOKUP, LOOKUP * LOOKUP + index_rows
    rows = first_segment + 3 * len(segments)
    if rows > MAX_DATA:
        raise ValueError("the rivers need %d vec4 of river data, more than %d" % (rows, MAX_DATA))
    data = np.zeros((MAX_DATA, 4), dtype=np.float32)
    start = 0
    for k, cell_list in enumerate(lists):
        data[k, 0], data[k, 1] = start, len(cell_list)
        start += len(cell_list)
    flat = np.zeros(4 * index_rows, dtype=np.float32)
    flat[:len(indices)] = indices
    data[first_index:first_segment] = flat.reshape(-1, 4)
    for k, (a, b, la, lb, wa, wb, da, db) in enumerate(segments):
        row = first_segment + 3 * k
        data[row] = tuple(a) + (la,)
        data[row + 1] = tuple(b) + (lb,)
        data[row + 2] = (wa, wb, da, db)
    c, e, nn = frame.centre, frame.east, frame.north
    params = [tuple(c) + (float(len(segments)),), tuple(e) + (cell,), tuple(nn) + (float(LOOKUP),),
              (float(first_index), float(first_segment), REACH, 0.0)]
    return data, params


class RiverLookup:
    """The rivers' water in Python, as rivers.glsl finds it."""

    def __init__(self, rivers, frame, radius):
        self.frame, self.radius = frame, radius
        self.segments = _segments(rivers)
        self.lists, self.cell = _cell_lists(self.segments, frame, radius)

    def level(self, direction):
        """The water level of the first segment, in its cell's list, whose
        water covers the unit `direction` (within its half width), or None."""
        if not self.segments:
            return None
        d = np.asarray(direction, dtype=np.float64)
        if d @ self.frame.centre <= 0.5:
            return None
        x, y = self.frame.to_plane(d)
        half = 0.5 * LOOKUP * self.cell
        i, j = int(math.floor((x + half) / self.cell)), int(math.floor((y + half) / self.cell))
        if not (0 <= i < LOOKUP and 0 <= j < LOOKUP):
            return None
        for k in self.lists[j * LOOKUP + i]:
            a, b, la, lb, wa, wb, _, _ = self.segments[k]
            pa = (d - np.asarray(a)) * self.radius
            ba = (np.asarray(b) - np.asarray(a)) * self.radius
            t = float(np.clip(pa @ ba / max(ba @ ba, 1e-12), 0.0, 1.0))
            if np.linalg.norm(pa - ba * t) < 0.5 * (wa + (wb - wa) * t):
                return la + (lb - la) * t
        return None


def ribbon(rivers, frame, radius):
    """(vertices, indices): the water's mesh, vertices a (count, 8) float32
    array of (position relative to the frame's centre point, across -1 or
    1) and (unit up, depth at the centre line), indices uint32 triangles."""
    origin = frame.centre * radius
    vertices, indices = [], []
    for river in rivers:
        dirs = np.asarray(river.points, dtype=np.float64)
        count = len(dirs)
        base = len(vertices)
        for k in range(count):
            up = dirs[k]
            along = dirs[min(k + 1, count - 1)] - dirs[max(k - 1, 0)]
            along -= up * (along @ up)
            side = np.cross(along, up)
            side /= max(np.linalg.norm(side), 1e-12)
            centre = up * (radius + river.levels[k])
            half = 0.5 * river.widths[k]
            for across in (-1.0, 1.0):
                p = centre + side * (across * half) - origin
                vertices.append((p[0], p[1], p[2], across, up[0], up[1], up[2], river.depths[k]))
        for k in range(count - 1):
            a, b = base + 2 * k, base + 2 * k + 2
            indices += [a, b, a + 1, a + 1, b, b + 1]
    if len(vertices) > MAX_VERTICES or len(indices) > MAX_INDICES:
        raise ValueError("the rivers' water needs %d vertices and %d indices, more than %d and %d"
                         % (len(vertices), len(indices), MAX_VERTICES, MAX_INDICES))
    return (np.array(vertices, dtype=np.float32).reshape(-1, 8), np.array(indices, dtype=np.uint32))

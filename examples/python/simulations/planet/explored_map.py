"""
The walker's map of the ground it has explored (walker.py): terrain
heights and ore richness measured with the world's extra probes around
the player, drawn as a minimap that turns with the view and a full map
with north up.

Each body has a `Grid` of SIZE x SIZE cells CELL metres apart in the plane
tangent to the body's sphere at its origin direction (the site's up on the
planet, where the player first comes down on any other body); cell (i, j)
is i cells east and j cells north of the grid's middle, and its direction
is where the line from the body's centre through that point of the plane
meets the sphere. A cell holds its terrain height (NaN until measured) and
its ore richness.

`ExploredMap.probe` measures first the cells `reveal` queued (those
within a radius of a place, as a survey beacon reveals them), then the
cells nearest the player within REVEAL metres that are not measured yet,
the extra probes' number at a time (`walked` counts those measured around
the player, not revealed), as
the props do (props.Props.probe): only while nothing else uses the probes,
and it lets them go while `yield_to` is true (a prop needs its ground).
`ExploredMap.share` is the share of the cells within a radius of a place
that are measured.

`render` draws a grid around a cell into an RGBA image: heights coloured
by the body's ramp (LAND, SEA, MOON, shades of cyan), shaded by their
slope lit from the north-west, brightened along a contour line every
`contour` metres when given, ore richer than ORE_SHOWN turned towards
ORE_COLOUR, every other row darkened by SCAN_DARK with `scan`, and cells
not measured (or outside the grid) `unexplored`. With a heading, the
image turns so that the heading is up; `circle` leaves the corners
outside the inscribed circle transparent. `to_pixel` and `from_pixel`
convert between cells and the image's pixels the same way.

`MapView` draws in the interface style of scifi.py, on a screen canvas
laid out for its reference size divided by the HUD's scale
(`set_scale`). The minimap at the top right is a round scope that turns
with the view: the image in a ring, range rings, bearing ticks every 30
degrees with N in amber, a sweep turning once every SWEEP_SECONDS, the
markers (name, cell coordinates, colour; one beyond the rim held on it)
and the player's arrow, and its range under it. While open, the full map
stands in the middle, north up, over a backdrop that dims the view: a
panel with a title tab naming the body, the map with faint lines every
scale bar's length (`scale_bar`) east and north of the grid's middle,
range rings around the player at that length, corner brackets, the scale
bar at its bottom left and the zoom's segments at its bottom right, and
the keys that work on it (MAP_HINTS) below. On it the markers are named to
their right, starting LABEL_OFFSET from them, the names of markers within
MERGE_REACH of an earlier one joined to its name (`merge`), each name
moved down while it would cover one before it (`separate`). A column at
its left gives the body, the player's place east and north of the grid's
middle, the heading, the cells explored and their area, and the range; a
column at its right the markers, nearest first, with their distances and
bearings (`contacts`). The map is FULL_MAP across, less where the canvas
is too small for it and its columns.
"""

import math

import numpy as np

import shoonyakasha as sk
from shoonyakasha.planet.rocks import ORE_ROCKS
from shoonyakasha.planet.vec import add, dot, local_frame, normalize, scale, sub

import scifi
from scifi import measure, with_alpha

CELL = 25.0                      # m between cells
SIZE = 1024                      # cells along each side
REVEAL = 250.0                   # m around the player the probes measure
ORE_SHOWN = ORE_ROCKS
ORE_COLOUR = np.array([1.0, 0.62, 0.15], dtype=np.float32)
UNEXPLORED = np.array([0.01, 0.035, 0.055, 0.82], dtype=np.float32)   # on the minimap
UNEXPLORED_FULL = np.array([0.008, 0.026, 0.042, 0.97], dtype=np.float32)   # on the full map
# (height in m, colour) stops, linear between them.
LAND = [(0.0, (0.07, 0.30, 0.28)), (300.0, (0.12, 0.42, 0.40)), (1_200.0, (0.22, 0.56, 0.55)),
        (2_500.0, (0.42, 0.72, 0.74)), (4_000.0, (0.86, 0.97, 1.0))]
SEA = [(-2_000.0, (0.01, 0.04, 0.12)), (-200.0, (0.03, 0.11, 0.25)), (0.0, (0.06, 0.2, 0.38))]
MOON = [(-2_000.0, (0.12, 0.16, 0.2)), (0.0, (0.3, 0.38, 0.44)), (1_500.0, (0.66, 0.76, 0.82))]
LIGHT = normalize((-1.0, 1.0, 1.4))  # east, north, up: from the north-west, above
SHADE_RELIEF = 3.0               # the slopes exaggerated this much for the shading
CONTOUR_LIGHT = np.array([0.1, 0.22, 0.24], dtype=np.float32)   # added along a contour line
SCAN_DARK = 0.88                 # the darkened rows' share of their colour
MINIMAP = 220                    # canvas units across
MINIMAP_PIXELS = 110             # the minimap image's pixels across, stretched to MINIMAP
MINIMAP_RANGE = 400.0            # m from the centre to the minimap's rim
MINIMAP_TOP = 40.0               # canvas units from the top
MINIMAP_MARGIN = 24.0            # canvas units from the right
MINIMAP_CONTOUR = 50.0           # m between the minimap's contour lines
MINIMAP_INTERVAL = 0.1           # s between the minimap's redraws
SWEEP_SECONDS = 4.0
SWEEP_WIDTH = 0.8                # radians the sweep's trail covers
FULL_MAP = 720                   # canvas units across, at most
FULL_PIXELS = 360                # the full map image's pixels across, stretched to its size
FULL_CONTOURS = {500.0: 25.0, 1_000.0: 50.0, 2_000.0: 100.0, 4_000.0: 200.0, 8_000.0: 400.0}   # m, by range
FULL_PAD = 14                    # canvas units of panel around the full map
FULL_HEADER, FULL_FOOTER = 40, 46   # the panel's bands above and below it
COLUMN = 250                     # canvas units across each column beside it
COLUMN_GAP = 18
MAP_HINTS = [(("Wheel",), "Zoom"), (("LMB",), "Set the waypoint"), (("RMB",), "Clear it"),
             (("M", "Esc"), "Close")]
SCALE_STEPS = (25.0, 50.0, 100.0, 200.0, 250.0, 500.0, 1_000.0, 2_000.0, 2_500.0, 5_000.0)   # m
MERGE_REACH = 14.0               # canvas units within which the full map's markers share a label
LABEL_OFFSET = 12.0              # canvas units from a marker on the full map to the left of its label
FULL_RANGES = (500.0, 1_000.0, 2_000.0, 4_000.0, 8_000.0)   # m from the centre to the edge, by zoom
FULL_INTERVAL = 0.2              # s between the full map's redraws
MAX_CONTACTS = 9
LABEL_SIZE = 15
LABEL_WIDTH, LABEL_HEIGHT = 70.0, 16.0   # canvas units a label keeps clear of earlier ones
TEXT = scifi.WHITE
MUTED = scifi.MUTED
GRID_LINE = (0.38, 0.88, 1.0, 0.1)
RING_LINE = (0.38, 0.88, 1.0, 0.22)
PLAYER = (0.9, 1.0, 1.0, 1.0)


def ramp(stops, heights):
    """Colours (N x 3) of `heights` (N) from (height, colour) stops."""
    xs = np.array([h for h, _ in stops], dtype=np.float32)
    cs = np.array([c for _, c in stops], dtype=np.float32)
    return np.stack([np.interp(heights, xs, cs[:, k]) for k in range(3)], axis=-1)


class Grid:
    def __init__(self, body, origin, cell=CELL, size=SIZE):
        """The grid of `body` (a world body: radius, name, ocean) around the
        unit direction `origin`."""
        self.body = body
        self.origin = normalize(origin)
        self.east, self.north = local_frame(self.origin)
        self.cell, self.size = cell, size
        self.half = 0.5 * (size - 1)
        self.heights = np.full((size, size), np.nan, dtype=np.float32)   # [j, i]
        self.ores = np.zeros((size, size), dtype=np.float32)
        self.pending = set()      # cells asked for and not back yet
        self.measured = 0

    def index(self, direction):
        """(i, j) in cells of the unit `direction` (from the body's centre),
        where its line meets the plane; None on the far side."""
        along = dot(direction, self.origin)
        if along <= 1e-6:
            return None
        on_plane = sub(scale(direction, 1.0 / along), self.origin)
        r = self.body.radius
        return (dot(on_plane, self.east) * r / self.cell + self.half,
                dot(on_plane, self.north) * r / self.cell + self.half)

    def direction(self, i, j):
        r = self.body.radius
        point = add(self.origin, add(scale(self.east, (i - self.half) * self.cell / r),
                                     scale(self.north, (j - self.half) * self.cell / r)))
        return normalize(point)

    def inside(self, i, j):
        return 0 <= i < self.size and 0 <= j < self.size

    def is_measured(self, i, j):
        return not math.isnan(float(self.heights[j, i]))

    def set(self, i, j, height, ore):
        if not self.is_measured(i, j):
            self.measured += 1
        self.heights[j, i] = height
        self.ores[j, i] = ore


def reveal_offsets(reveal=REVEAL, cell=CELL):
    """(di, dj) of the cells within `reveal` metres of a cell, nearest first."""
    n = int(math.ceil(reveal / cell))
    offsets = [(di, dj) for dj in range(-n, n + 1) for di in range(-n, n + 1)
               if (di * di + dj * dj) * cell * cell <= reveal * reveal]
    offsets.sort(key=lambda o: (o[0] * o[0] + o[1] * o[1], o[1], o[0]))
    return offsets


class ExploredMap:
    def __init__(self, world, cell=CELL, size=SIZE, reveal=REVEAL):
        self.world = world
        self.cell, self.size = cell, size
        self.grids = {}           # body name: Grid
        self.offsets = reveal_offsets(reveal, cell)
        self.asked = None         # the probes asked for: [(direction, body)]
        self.asking = []          # their (grid, i, j)
        self.revealing = []       # (grid, i, j) queued by `reveal`, measured before the cells around the player
        self.from_reveal = set()  # (grid, i, j) of the asked cells that `reveal` queued
        self.walked = 0           # cells measured around the player (not revealed)

    def grid(self, body, direction):
        """`body`'s grid, made around the site's up on the planet, else around `direction`."""
        grid = self.grids.get(body.name)
        if grid is None:
            origin = self.world.site_frame()[1] if body is self.world.planet else direction
            grid = self.grids[body.name] = Grid(body, origin, self.cell, self.size)
        return grid

    def reveal(self, body, direction, radius):
        """The cells of `body`'s grid within `radius` metres of `direction`
        not measured yet queued, nearest first, to be measured before those
        around the player. Returns how many."""
        grid = self.grid(body, direction)
        at = grid.index(direction)
        if at is None:
            return 0
        ci, cj = int(round(at[0])), int(round(at[1]))
        n = int(radius / self.cell)
        cells = [(di * di + dj * dj, ci + di, cj + dj) for di in range(-n, n + 1) for dj in range(-n, n + 1)
                 if di * di + dj * dj <= n * n]
        queued = set((g, i, j) for g, i, j in self.revealing)
        added = 0
        for _, i, j in sorted(cells):
            if (grid.inside(i, j) and not grid.is_measured(i, j) and (i, j) not in grid.pending
                    and (grid, i, j) not in queued):
                self.revealing.append((grid, i, j))
                added += 1
        return added

    def share(self, body, direction, radius):
        """The share of `body`'s grid cells within `radius` metres of the
        unit `direction` that are measured; 0 with none of them in the grid."""
        grid = self.grid(body, direction)
        at = grid.index(direction)
        if at is None:
            return 0.0
        ci, cj = int(round(at[0])), int(round(at[1]))
        n = int(radius / self.cell)
        total = measured = 0
        for dj in range(-n, n + 1):
            for di in range(-n, n + 1):
                i, j = ci + di, cj + dj
                if di * di + dj * dj <= n * n and grid.inside(i, j):
                    total += 1
                    measured += grid.is_measured(i, j)
        return measured / total if total else 0.0

    def probe(self, body, position, probes, yield_to=False):
        """The cells around `position` (from `body`'s centre) measured, with
        `probes` of the world's extra probes a round (see the module's
        docstring)."""
        world = self.world
        if world.extra_probes and world.extra_probes != self.asked:
            return
        if self.asked is not None:
            if [probe for probe, _ in world.extra_heights] != self.asked:
                return
            for (grid, i, j), (_, height), (_, ore) in zip(self.asking, world.extra_heights, world.extra_ores):
                grid.pending.discard((i, j))
                if not grid.is_measured(i, j) and (grid, i, j) not in self.from_reveal:
                    self.walked += 1
                self.from_reveal.discard((grid, i, j))
                grid.set(i, j, height, ore)
            self.asked, self.asking = None, []
        if yield_to:
            world.extra_probes = []
            return
        direction = normalize(position)
        grid = self.grid(body, direction)
        at = grid.index(direction)
        wanted = []
        while self.revealing and len(wanted) < probes:
            g, i, j = self.revealing.pop(0)
            if g.body is body and (i, j) not in g.pending and not g.is_measured(i, j):
                wanted.append((g, i, j))
                self.from_reveal.add((g, i, j))
        if at is not None and len(wanted) < probes:
            ci, cj = int(round(at[0])), int(round(at[1]))
            for di, dj in self.offsets:
                i, j = ci + di, cj + dj
                if (grid.inside(i, j) and (i, j) not in grid.pending and not grid.is_measured(i, j)
                        and (grid, i, j) not in wanted):
                    wanted.append((grid, i, j))
                    if len(wanted) == probes:
                        break
        if not wanted:
            world.extra_probes = []
            return
        asked = [(grid.direction(i, j), body) for grid, i, j in wanted]
        for grid, i, j in wanted:
            grid.pending.add((i, j))
        world.extra_probes = asked
        self.asked, self.asking = asked, wanted


def view_axes(heading):
    """(right, up) of the image in (east, north) for a heading in degrees
    clockwise from north; north up for None."""
    if heading is None:
        return (1.0, 0.0), (0.0, 1.0)
    h = math.radians(heading)
    return (math.cos(h), -math.sin(h)), (math.sin(h), math.cos(h))


def to_pixel(at, centre, metres, pixels, heading=None, cell=CELL):
    """The image pixel (x right, y down) of cell coordinates `at` in an
    image `pixels` across showing `metres` from `centre` to its edge."""
    (rx, ry), (ux, uy) = view_axes(heading)
    e, n = (at[0] - centre[0]) * cell, (at[1] - centre[1]) * cell
    per = 0.5 * pixels / metres
    return (0.5 * pixels + (e * rx + n * ry) * per, 0.5 * pixels - (e * ux + n * uy) * per)


def from_pixel(pixel, centre, metres, pixels, heading=None, cell=CELL):
    """The cell coordinates under an image pixel; the inverse of to_pixel."""
    (rx, ry), (ux, uy) = view_axes(heading)
    per = 0.5 * pixels / metres
    x, y = (pixel[0] - 0.5 * pixels) / per, (0.5 * pixels - pixel[1]) / per
    e, n = x * rx + y * ux, x * ry + y * uy
    return (centre[0] + e / cell, centre[1] + n / cell)


def render(grid, centre, metres, pixels, heading=None, circle=False, unexplored=UNEXPLORED, contour=None,
           scan=False):
    """An RGBA uint8 image (pixels x pixels x 4) of `grid` around cell
    coordinates `centre` (see the module's docstring)."""
    k = (np.arange(pixels, dtype=np.float32) + 0.5 - 0.5 * pixels) * (metres / (0.5 * pixels))
    x, y = np.meshgrid(k, -k)                       # metres right and up from the centre
    (rx, ry), (ux, uy) = view_axes(heading)
    i = centre[0] + (x * rx + y * ux) / grid.cell
    j = centre[1] + (x * ry + y * uy) / grid.cell
    ii, jj = np.floor(i + 0.5).astype(np.int64), np.floor(j + 0.5).astype(np.int64)
    inside = (ii >= 0) & (ii < grid.size - 1) & (jj >= 0) & (jj < grid.size - 1)
    ci, cj = np.clip(ii, 0, grid.size - 2), np.clip(jj, 0, grid.size - 2)
    h = grid.heights[cj, ci]
    he = grid.heights[cj, ci + 1]
    hn = grid.heights[cj + 1, ci]
    known = inside & ~np.isnan(h)
    sea = grid.body.ocean if hasattr(grid.body, "ocean") else False
    hs = np.nan_to_num(h)
    if grid.body.name == "moon":
        rgb = ramp(MOON, hs)
    else:
        rgb = np.where((hs < 0.0)[..., None] & sea, ramp(SEA, hs), ramp(LAND, hs))
    ground = np.maximum(hs, 0.0) if sea else hs
    de = np.where(np.isnan(he), 0.0, (np.maximum(np.nan_to_num(he), 0.0) if sea else np.nan_to_num(he)) - ground)
    dn = np.where(np.isnan(hn), 0.0, (np.maximum(np.nan_to_num(hn), 0.0) if sea else np.nan_to_num(hn)) - ground)
    nx, ny, nz = -de * SHADE_RELIEF / grid.cell, -dn * SHADE_RELIEF / grid.cell, np.ones_like(de)
    norm = np.sqrt(nx * nx + ny * ny + nz * nz)
    light = (nx * LIGHT[0] + ny * LIGHT[1] + nz * LIGHT[2]) / norm
    rgb = rgb * np.clip(0.45 + 0.75 * light, 0.3, 1.25)[..., None]
    if contour:
        band = np.floor(ground / contour)
        crossed = np.zeros(band.shape, dtype=bool)
        for d in (de, dn):
            crossed |= np.floor((ground + d) / contour) != band
        rgb = rgb + CONTOUR_LIGHT * crossed[..., None]
    ore = np.where(known, grid.ores[cj, ci], 0.0)
    t = np.clip((ore - ORE_SHOWN) / ORE_SHOWN, 0.0, 1.0)[..., None] * (ore >= ORE_SHOWN)[..., None]
    rgb = rgb * (1.0 - 0.7 * t) + ORE_COLOUR * (0.7 * t)
    rgba = np.concatenate([np.clip(rgb, 0.0, 1.0), np.ones(rgb.shape[:2] + (1,), dtype=np.float32)], axis=-1)
    rgba = np.where(known[..., None], rgba, unexplored)
    if scan:
        rgba[1::2, :, :3] *= SCAN_DARK
    if circle:
        rgba[..., 3] = np.where(x * x + y * y <= metres * metres, rgba[..., 3], 0.0)
    return np.ascontiguousarray((rgba * 255.0 + 0.5).astype(np.uint8))


def separate(labels, width=LABEL_WIDTH, height=LABEL_HEIGHT):
    """(text, (x, y), colour) labels, each moved down by `height` while it
    lies within `width` across and `height` down of a label before it."""
    placed = []
    for text, (x, y), colour in labels:
        while any(abs(x - px) < width and abs(y - py) < height for _, (px, py), _ in placed):
            y += height
        placed.append((text, (x, y), colour))
    return placed


def merge(labels, reach=MERGE_REACH):
    """(text, (x, y), colour) labels with those within `reach` canvas units
    of an earlier one folded into it: their texts joined by ", " at the
    earlier one's place, in its colour."""
    merged = []
    for text, (x, y), colour in labels:
        near = next((m for m in merged if math.hypot(x - m[1][0], y - m[1][1]) <= reach), None)
        if near is None:
            merged.append([text, (x, y), colour])
        else:
            near[0] += ", " + text
    return [tuple(m) for m in merged]


def scale_bar(metres, pixels):
    """(metres, pixels) of the scale bar for a map `pixels` across showing
    `metres` from its middle to its edge: the longest of SCALE_STEPS no
    longer than a quarter of the map's width."""
    per = pixels / (2.0 * metres)
    fitting = [s for s in SCALE_STEPS if s * per <= 0.25 * pixels]
    step = fitting[-1] if fitting else SCALE_STEPS[0]
    return step, step * per


def contacts(markers, at, cell=CELL):
    """(name, metres, bearing in degrees, colour) of `markers` (name, cell
    coordinates, colour) from the cell coordinates `at`, nearest first."""
    found = []
    for name, where, colour in markers:
        e, n = (where[0] - at[0]) * cell, (where[1] - at[1]) * cell
        found.append((name, math.hypot(e, n), math.degrees(math.atan2(e, n)) % 360.0, colour))
    found.sort(key=lambda c: c[1])
    return found


def full_size(width, height):
    """The full map's size in canvas units on a canvas `width` x `height`."""
    room_h = height - 2 * FULL_PAD - FULL_HEADER - FULL_FOOTER - 24
    room_w = width - 2 * (COLUMN + COLUMN_GAP) - 2 * FULL_PAD - 24
    return max(200.0, min(float(FULL_MAP), room_h, room_w))


class MapView:
    def __init__(self, engine, reference_size=(1600, 900)):
        ui = engine.ui
        self.ui = ui
        self.reference_size = reference_size
        self.canvas = ui.create_canvas(reference_size=reference_size, sort_order=1)
        self.back = scifi.Layer(ui, self.canvas)
        self.full_image = ui.create_image(self.canvas, size=(FULL_MAP, FULL_MAP))
        self.mini_image = ui.create_image(self.canvas, size=(MINIMAP, MINIMAP))
        for element in (self.full_image, self.mini_image):
            ui.set_raycast_target(element, False)
        self.front = scifi.Layer(ui, self.canvas)
        self.open = False
        self.zoom = 0                 # index into FULL_RANGES
        self.since = {"mini": 1e9, "full": 1e9}
        self.clock = 0.0
        self.full_rect = (0.0, 0.0, float(FULL_MAP))   # the full map's left, top and size, canvas units
        self.full_centre = None       # the cell coordinates at its middle, last drawn
        self.set_open(False)

    def show(self, on):
        self.ui.set_visible(self.canvas, on)

    def set_scale(self, s):
        """Drawn `s` times its size: the canvas laid out for its reference size / s."""
        self.ui.set_canvas_scaling(self.canvas, sk.CANVAS_SCALE_WITH_SCREEN,
                                   reference_size=(self.reference_size[0] / s, self.reference_size[1] / s))

    def set_open(self, open_):
        self.open = open_
        self.ui.set_visible(self.full_image, open_)
        self.ui.set_visible(self.mini_image, not open_)
        self.since["full"] = 1e9

    def zoom_by(self, steps):
        self.zoom = max(0, min(len(FULL_RANGES) - 1, self.zoom - int(steps)))
        self.since["full"] = 1e9

    def click(self, canvas_point):
        """The cell coordinates under a point of the canvas on the open full map, or None."""
        if not self.open or self.full_centre is None:
            return None
        left, top, size = self.full_rect
        x, y = canvas_point[0] - left, canvas_point[1] - top
        if not (0.0 <= x <= size and 0.0 <= y <= size):
            return None
        return from_pixel((x, y), self.full_centre, FULL_RANGES[self.zoom], size)

    @staticmethod
    def _arrow(mesh, x, y, angle, size, colour):
        """A triangle pointing `angle` radians clockwise from up, at (x, y), with a dark outline."""
        s, c = math.sin(angle), math.cos(angle)

        def p(ax, ay, grow=0.0):
            ax, ay = ax * (1.0 + grow), ay * (1.0 + grow)
            return (x + ax * c - ay * s, y + ax * s + ay * c)
        shape = ((0.0, -size), (0.6 * size, 0.7 * size), (0.0, 0.35 * size), (-0.6 * size, 0.7 * size))
        mesh.face([p(ax, ay, 0.35) for ax, ay in shape], [with_alpha(scifi.CYAN, 0.35)] * 4)
        mesh.face([p(ax, ay) for ax, ay in shape], [colour] * 4)

    # ── The minimap ──

    def draw_minimap(self, dt, grid, at, heading, markers, width):
        back, front = self.back.mesh, self.front.mesh
        left, top = width - MINIMAP_MARGIN - MINIMAP, MINIMAP_TOP
        self.since["mini"] += dt
        if self.since["mini"] >= MINIMAP_INTERVAL:
            self.since["mini"] = 0.0
            self.ui.set_rect(self.mini_image, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0),
                             position=(left, top), size=(MINIMAP, MINIMAP))
            self.ui.set_texture_data(self.mini_image, render(grid, at, MINIMAP_RANGE, MINIMAP_PIXELS, heading,
                                                             circle=True, contour=MINIMAP_CONTOUR))
        r = 0.5 * MINIMAP
        cx, cy = left + r, top + r
        disc = [(cx + (r + 8.0) * math.sin(a), cy - (r + 8.0) * math.cos(a))
                for a in np.linspace(0.0, 2.0 * math.pi, 64, endpoint=False)]
        back.polygon(disc, scifi.GLASS_DEEP)
        scifi.arc(front, cx, cy, r, scifi.CYAN, 1.6)
        scifi.arc(front, cx, cy, r + 8.0, scifi.CYAN_SOFT, 1.0)
        scifi.arc(front, cx, cy, 0.5 * r, RING_LINE, 1.0)
        front.segment((cx - r, cy), (cx + r, cy), GRID_LINE, 1.0)
        front.segment((cx, cy - r), (cx, cy + r), GRID_LINE, 1.0)
        # The sweep: a fan behind its leading edge, fading to nothing.
        lead = 2.0 * math.pi * (self.clock / SWEEP_SECONDS % 1.0)
        steps = 16
        for k in range(steps):
            a0, a1 = lead - SWEEP_WIDTH * (k + 1) / steps, lead - SWEEP_WIDTH * k / steps
            c0, c1 = with_alpha(scifi.CYAN, 0.16 * (1.0 - (k + 1) / steps)), with_alpha(scifi.CYAN, 0.16 * (1.0 - k / steps))
            front.face([(cx, cy), (cx + r * math.sin(a0), cy - r * math.cos(a0)),
                        (cx + r * math.sin(a1), cy - r * math.cos(a1))], [c1, c0, c1])
        front.segment((cx, cy), (cx + r * math.sin(lead), cy - r * math.cos(lead)), with_alpha(scifi.CYAN, 0.5), 1.2)
        for b in range(0, 360, 30):
            a = math.radians(b - heading)
            inner, outer = r + 2.0, r + (8.0 if b % 90 == 0 else 5.0)
            front.segment((cx + inner * math.sin(a), cy - inner * math.cos(a)),
                          (cx + outer * math.sin(a), cy - outer * math.cos(a)), scifi.CYAN, 1.4)
        a = math.radians(-heading)
        self.front.text("N", (cx + (r + 18.0) * math.sin(a), cy - (r + 18.0) * math.cos(a)), 16, scifi.AMBER,
                        "title", "center")
        for name, where, colour in markers:
            px, py = to_pixel(where, at, MINIMAP_RANGE, MINIMAP, heading)
            dx, dy = px - r, py - r
            d = math.hypot(dx, dy)
            if d > r - 6.0:
                dx, dy = dx * (r - 6.0) / d, dy * (r - 6.0) / d
            scifi.diamond(front, cx + dx, cy + dy, 4.0, colour)
        self._arrow(front, cx, cy, 0.0, 8.0, PLAYER)
        tab_w = 120.0
        scifi.panel(back, cx - 0.5 * tab_w, top + MINIMAP + 14.0, tab_w, 20.0, 6.0, scifi.GLASS, scifi.CYAN_SOFT,
                    ("bl", "br"), scan=False)
        self.front.text("RANGE  " + distance_label(MINIMAP_RANGE).upper(), (cx, top + MINIMAP + 24.0), 13, MUTED,
                        "mono", "center")

    # ── The full map ──

    def draw_full(self, dt, grid, at, heading, markers, width, height):
        back, front, text = self.back.mesh, self.front.mesh, self.front.text
        size = full_size(width, height)
        metres = FULL_RANGES[self.zoom]
        left = 0.5 * (width - size)
        top = 0.5 * (height - size - FULL_HEADER - FULL_FOOTER) + FULL_HEADER
        self.full_rect = (left, top, size)
        back.face([(0, 0), (width, 0), (width, height), (0, height)], [scifi.BACKDROP] * 4)
        scifi.scanlines(back, 0.0, 0.0, width, height, with_alpha(scifi.SCAN, 0.6), 4.0)
        pl, pt = left - FULL_PAD, top - FULL_PAD - FULL_HEADER
        pw, ph = size + 2 * FULL_PAD, size + 2 * FULL_PAD + FULL_HEADER + FULL_FOOTER
        scifi.panel(back, pl, pt, pw, ph, 16.0, scifi.GLASS_DEEP, scifi.CYAN_SOFT, ("tr", "bl"))
        title = "SURVEY MAP  //  %s" % grid.body.name.upper()
        tab = measure.width(title, 17, "title") + 36.0
        back.polygon([(pl, pt), (pl + tab + 18.0, pt), (pl + tab, pt + 28.0), (pl, pt + 28.0)],
                     with_alpha(scifi.CYAN, 0.18))
        front.segment((pl, pt + 28.0), (pl + tab, pt + 28.0), scifi.CYAN, 1.2)
        text(title, (pl + 16.0, pt + 14.0), 17, scifi.CYAN, "title")
        text("NORTH UP", (pl + pw - 22.0, pt + 14.0), 14, MUTED, "title", "right")

        self.since["full"] += dt
        if self.since["full"] >= FULL_INTERVAL:
            self.since["full"] = 0.0
            self.full_centre = at
            self.ui.set_rect(self.full_image, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0),
                             position=(left, top), size=(size, size))
            self.ui.set_texture_data(self.full_image, render(grid, at, metres, FULL_PIXELS, unexplored=UNEXPLORED_FULL,
                                                             contour=FULL_CONTOURS.get(metres), scan=True))
        centre = self.full_centre or at
        step, bar = scale_bar(metres, size)
        self._grid_lines(front, grid, centre, metres, step, left, top, size)
        px, py = to_pixel(at, centre, metres, size)
        player = (left + px, top + py)
        per = size / (2.0 * metres)
        for k in range(1, int(metres / step) + 1):
            ring = k * step * per
            if ring < 0.5 * size - 2.0:
                scifi.arc(front, player[0], player[1], ring, RING_LINE, 1.0, steps=72)
        scifi.brackets(front, left - 4.0, top - 4.0, size + 8.0, size + 8.0, 22.0, scifi.CYAN, 1.8)
        found = []
        for name, where, colour in markers:
            mx, my = to_pixel(where, centre, metres, size)
            if 0.0 <= mx <= size and 0.0 <= my <= size:
                x, y = left + mx, top + my
                scifi.diamond(front, x, y, 6.0, colour)
                found.append((name, (x, y), colour))
        labels = [(t.upper(), (x + LABEL_OFFSET, y), c) for t, (x, y), c in merge(found)]
        for t, (x, y), c in separate(labels):
            text(t, (x, y), LABEL_SIZE, c, "title")
        self._arrow(front, player[0], player[1], math.radians(heading), 10.0, PLAYER)

        bx, by = left + 18.0, top + size - 20.0
        self._scale_bar(front, bx, by, bar)
        text(distance_label(step).upper(), (bx + 0.5 * bar, by - 12.0), 13, TEXT, "mono", "center")
        zx, zy = left + size - 18.0 - 110.0, top + size - 24.0
        text("ZOOM", (zx - 8.0, zy + 4.0), 13, MUTED, "title", "right")
        scifi.segments(front, zx, zy, 110.0, 8.0, len(FULL_RANGES), (len(FULL_RANGES) - self.zoom) / len(FULL_RANGES),
                       scifi.CYAN, scifi.CYAN_FAINT, 3.0)
        hints_w = scifi.hints_width(MAP_HINTS)
        scifi.caps(self.front, MAP_HINTS, 0.5 * (width - hints_w), top + size + FULL_PAD + 0.5 * FULL_FOOTER, MUTED)

        # The columns.
        column_top = pt
        lx = pl - COLUMN_GAP - COLUMN
        rows = [("BODY", grid.body.name.upper()),
                ("EAST", distance_label((at[0] - grid.half) * grid.cell)),
                ("NORTH", distance_label((at[1] - grid.half) * grid.cell)),
                ("HEADING", "%03d" % round(heading % 360.0)),
                ("EXPLORED", format(grid.measured, ",")),
                ("AREA", "%.2f km2" % (grid.measured * grid.cell * grid.cell / 1e6)),
                ("RANGE", distance_label(metres))]
        self._column(lx, column_top, "TELEMETRY", [(label, value, TEXT) for label, value in rows])
        rx = pl + pw + COLUMN_GAP
        listed = contacts(markers, at, grid.cell)[:MAX_CONTACTS]
        self._contacts(rx, column_top, listed)

    def _column(self, x, y, title, rows):
        back, text = self.back.mesh, self.front.text
        height = 44.0 + 26.0 * len(rows) + 10.0
        scifi.panel(back, x, y, COLUMN, height, 10.0, scifi.GLASS_DEEP, scifi.CYAN_SOFT, ("tl", "br"))
        text(title, (x + 14.0, y + 18.0), 15, scifi.CYAN, "title")
        self.front.mesh.segment((x + 14.0, y + 32.0), (x + COLUMN - 14.0, y + 32.0), scifi.CYAN_FAINT, 1.0)
        cy = y + 50.0
        for label, value, colour in rows:
            text(label, (x + 14.0, cy), 13, MUTED, "title")
            text(value, (x + COLUMN - 14.0, cy), 14, colour, "mono", "right")
            cy += 26.0

    def _contacts(self, x, y, listed):
        back, front, text = self.back.mesh, self.front.mesh, self.front.text
        height = 44.0 + 40.0 * max(1, len(listed)) + 10.0
        scifi.panel(back, x, y, COLUMN, height, 10.0, scifi.GLASS_DEEP, scifi.CYAN_SOFT, ("tl", "br"))
        text("CONTACTS", (x + 14.0, y + 18.0), 15, scifi.CYAN, "title")
        front.segment((x + 14.0, y + 32.0), (x + COLUMN - 14.0, y + 32.0), scifi.CYAN_FAINT, 1.0)
        cy = y + 52.0
        if not listed:
            text("None in range", (x + 14.0, cy), 14, MUTED)
        for name, metres, angle, colour in listed:
            scifi.diamond(front, x + 19.0, cy, 4.0, colour)
            text(name.upper(), (x + 30.0, cy), 15, colour, "title")
            text(distance_label_short(metres), (x + COLUMN - 14.0, cy), 14, TEXT, "mono", "right")
            text("BRG %03d" % round(angle), (x + 30.0, cy + 17.0), 12, MUTED, "mono")
            cy += 40.0

    def _grid_lines(self, mesh, grid, centre, metres, step, left, top, size):
        """Faint lines every `step` metres east and north of the grid's
        middle across the full map."""
        per = size / (2.0 * metres)
        for axis in (0, 1):
            offset = (centre[axis] - grid.half) * grid.cell        # m east (north) of the grid's middle
            first = math.ceil((offset - metres) / step)
            for k in range(first, int(math.floor((offset + metres) / step)) + 1):
                d = (k * step - offset) * per
                if axis == 0:
                    x = left + 0.5 * size + d
                    mesh.segment((x, top), (x, top + size), GRID_LINE, 1.0)
                    mesh.segment((x, top), (x, top + 6.0), scifi.CYAN_SOFT, 1.0)
                    mesh.segment((x, top + size - 6.0), (x, top + size), scifi.CYAN_SOFT, 1.0)
                else:
                    y = top + 0.5 * size - d
                    mesh.segment((left, y), (left + size, y), GRID_LINE, 1.0)
                    mesh.segment((left, y), (left + 6.0, y), scifi.CYAN_SOFT, 1.0)
                    mesh.segment((left + size - 6.0, y), (left + size, y), scifi.CYAN_SOFT, 1.0)

    @staticmethod
    def _scale_bar(mesh, x, y, length_):
        """The scale bar: a line `length_` long from (x, y) with a tick at each end."""
        for colour, w in ((scifi.SHADOW, 4.0), (TEXT, 2.0)):
            mesh.segment((x, y), (x + length_, y), colour, w)
            mesh.segment((x, y - 5.0), (x, y + 1.0), colour, w)
            mesh.segment((x + length_, y - 5.0), (x + length_, y + 1.0), colour, w)

    def update(self, dt, grid, at, heading, markers):
        """One frame: `grid` the body's, `at` the player's cell coordinates,
        `heading` its view's in degrees, `markers` (name, cell coordinates,
        colour)."""
        width, height = self.ui.get_canvas_size(self.canvas)
        if width <= 0 or height <= 0 or grid is None or at is None:
            return
        self.clock += dt
        self.back.begin()
        self.front.begin()
        if self.open:
            self.draw_full(dt, grid, at, heading, markers, width, height)
        else:
            self.draw_minimap(dt, grid, at, heading, markers, width)
        self.back.end()
        self.front.end()


def distance_label(metres):
    return "%.0f m" % metres if abs(metres) < 1_000.0 else "%g km" % round(metres / 1_000.0, 2)


def distance_label_short(metres):
    return "%.0f m" % metres if metres < 1_000.0 else "%.1f km" % (metres / 1_000.0)

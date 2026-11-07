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

`MapView` draws the minimap on a screen canvas laid out for its
reference size divided by the HUD's scale (`set_scale`), in the Field
Slate style (fieldslate.py), at the bottom right and only when `update`
is asked for it: a round scope that turns with the view, the image on a
glass disc in a thin ring, a range ring and cross lines, bearing ticks
every 30 degrees with N named, a sweep turning once every SWEEP_SECONDS,
the markers (name, cell coordinates, colour; one beyond the rim held on
it) and the player's arrow.

For a map's labels (the slate's Map app): `merge` joins the names of
markers within MERGE_REACH of an earlier one to its name, `separate`
moves each name down while it would cover one before it, and
`scale_bar` picks the scale bar's length; `contacts` gives markers'
distances and bearings, nearest first. FULL_RANGES are a full map's
ranges and FULL_CONTOURS their contour lines' spacing.
"""

import math

import numpy as np

import shoonyakasha as sk
from shoonyakasha.planet.rocks import ORE_ROCKS
from shoonyakasha.planet.vec import add, dot, local_frame, normalize, scale, sub

import fieldslate as fs
import scifi
from scifi import with_alpha

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
MINIMAP = 160                    # canvas units across
MINIMAP_PIXELS = 110             # the minimap image's pixels across, stretched to MINIMAP
MINIMAP_RANGE = 400.0            # m from the centre to the minimap's rim
MINIMAP_MARGIN = 40.0            # canvas units from the right and the bottom
MINIMAP_CONTOUR = 50.0           # m between the minimap's contour lines
MINIMAP_INTERVAL = 0.1           # s between the minimap's redraws
SWEEP_SECONDS = 4.0
SWEEP_WIDTH = 0.8                # radians the sweep's trail covers
FULL_CONTOURS = {500.0: 25.0, 1_000.0: 50.0, 2_000.0: 100.0, 4_000.0: 200.0, 8_000.0: 400.0}   # m, by range
SCALE_STEPS = (25.0, 50.0, 100.0, 200.0, 250.0, 500.0, 1_000.0, 2_000.0, 2_500.0, 5_000.0)   # m
MERGE_REACH = 14.0               # canvas units within which markers on a map share a label
FULL_RANGES = (500.0, 1_000.0, 2_000.0, 4_000.0, 8_000.0)   # m from the centre to the edge, by zoom
LABEL_WIDTH, LABEL_HEIGHT = 70.0, 16.0   # canvas units a label keeps clear of earlier ones


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


class MapView:
    def __init__(self, engine, reference_size=(1600, 900)):
        ui = engine.ui
        self.ui = ui
        self.reference_size = reference_size
        self.canvas = ui.create_canvas(reference_size=reference_size, sort_order=1)
        self.back = scifi.Layer(ui, self.canvas)
        self.mini_image = ui.create_image(self.canvas, size=(MINIMAP, MINIMAP))
        ui.set_raycast_target(self.mini_image, False)
        self.front = scifi.Layer(ui, self.canvas)
        self.since = 1e9              # s since the minimap's image was last drawn
        self.clock = 0.0

    def show(self, on):
        self.ui.set_visible(self.canvas, on)

    def set_scale(self, s):
        """Drawn `s` times its size: the canvas laid out for its reference size / s."""
        self.ui.set_canvas_scaling(self.canvas, sk.CANVAS_SCALE_WITH_SCREEN,
                                   reference_size=(self.reference_size[0] / s, self.reference_size[1] / s))

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

    def draw_minimap(self, dt, grid, at, heading, markers, width, height):
        back, front = self.back.mesh, self.front.mesh
        left, top = width - MINIMAP_MARGIN - MINIMAP, height - MINIMAP_MARGIN - MINIMAP
        self.since += dt
        if self.since >= MINIMAP_INTERVAL:
            self.since = 0.0
            self.ui.set_rect(self.mini_image, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0),
                             position=(left, top), size=(MINIMAP, MINIMAP))
            self.ui.set_texture_data(self.mini_image, render(grid, at, MINIMAP_RANGE, MINIMAP_PIXELS, heading,
                                                             circle=True, contour=MINIMAP_CONTOUR))
        r = 0.5 * MINIMAP
        cx, cy = left + r, top + r
        line = fs.tint(fs.ICE, 0.18, fs.ON_SLATE)
        disc = [(cx + (r + 4.0) * math.cos(a), cy + (r + 4.0) * math.sin(a))
                for a in np.linspace(0.0, 2.0 * math.pi, 64, endpoint=False)]
        fs.fill(back, disc, with_alpha(fs.SLATE, 0.6))
        fs.dot(front, cx, cy, r + 4.0, fs.tint(fs.ICE, 0.45, fs.ON_WORLD), hollow=True, width=1.2)
        fs.dot(front, cx, cy, 0.5 * r, line, hollow=True, width=1.0)
        front.segment((cx - r, cy), (cx + r, cy), line, 1.0)
        front.segment((cx, cy - r), (cx, cy + r), line, 1.0)
        # The sweep: a fan behind its leading edge, fading to nothing.
        lead = 2.0 * math.pi * (self.clock / SWEEP_SECONDS % 1.0)
        steps = 16
        for k in range(steps):
            a0, a1 = lead - SWEEP_WIDTH * (k + 1) / steps, lead - SWEEP_WIDTH * k / steps
            c0 = fs.tint(fs.HOLO, 0.14 * (1.0 - (k + 1) / steps), fs.ON_SLATE)
            c1 = fs.tint(fs.HOLO, 0.14 * (1.0 - k / steps), fs.ON_SLATE)
            front.face([(cx, cy), (cx + r * math.sin(a0), cy - r * math.cos(a0)),
                        (cx + r * math.sin(a1), cy - r * math.cos(a1))], [c1, c0, c1])
        for b in range(0, 360, 30):
            a = math.radians(b - heading)
            inner, outer = r + 4.0, r + (9.0 if b % 90 == 0 else 7.0)
            front.segment((cx + inner * math.sin(a), cy - inner * math.cos(a)),
                          (cx + outer * math.sin(a), cy - outer * math.cos(a)), fs.tint(fs.ICE, 0.6, fs.ON_WORLD),
                          1.2)
        a = math.radians(-heading)
        fs.write(self.front, "N", (cx + (r + 18.0) * math.sin(a), cy - (r + 18.0) * math.cos(a)), 12, fs.ICE,
                 "text_medium", "center")
        for name, where, colour in markers:
            px, py = to_pixel(where, at, MINIMAP_RANGE, MINIMAP, heading)
            dx, dy = px - r, py - r
            d = math.hypot(dx, dy)
            if d > r - 6.0:
                dx, dy = dx * (r - 6.0) / d, dy * (r - 6.0) / d
            x, y, s = cx + dx, cy + dy, 4.0
            if name == "Waypoint":
                front.face([(x - s, y - s), (x + s, y - s), (x, y + 0.6 * s)], [colour] * 3)
            else:
                front.face([(x - s, y), (x, y - s), (x + s, y), (x, y + s)], [colour] * 4)
        self._arrow(front, cx, cy, 0.0, 8.0, fs.ICE)

    def update(self, dt, grid, at, heading, markers, mini=True):
        """One frame: `grid` the body's, `at` the player's cell coordinates,
        `heading` its view's in degrees, `markers` (name, cell coordinates,
        colour); the minimap only with `mini`."""
        width, height = self.ui.get_canvas_size(self.canvas)
        if width <= 0 or height <= 0 or grid is None or at is None:
            return
        self.clock += dt
        self.back.begin()
        self.front.begin()
        self.ui.set_visible(self.mini_image, mini)
        if mini:
            self.draw_minimap(dt, grid, at, heading, markers, width, height)
        self.back.end()
        self.front.end()


def distance_label(metres):
    return "%.0f m" % metres if abs(metres) < 1_000.0 else "%g km" % round(metres / 1_000.0, 2)

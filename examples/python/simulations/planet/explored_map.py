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

`ExploredMap.probe` measures the cells nearest the player within REVEAL
metres that are not measured yet, the extra probes' number at a time, as
the props do (props.Props.probe): only while nothing else uses the probes,
and it lets them go while `yield_to` is true (a prop needs its ground).

`render` draws a grid around a cell into an RGBA image: heights coloured
by the body's ramp (LAND, SEA, MOON), shaded by their slope lit from the
north-west, ore richer than ORE_SHOWN turned towards ORE_COLOUR, and
cells not measured (or outside the grid) `unexplored`. With a heading, the
image turns so that the heading is up; `circle` leaves the corners
outside the inscribed circle transparent. `to_pixel` and `from_pixel`
convert between cells and the image's pixels the same way.

`MapView` shows the minimap at the top right of a screen canvas and, while
open, the full map in the middle, with markers (name, direction from the
body's centre, colour) and the player's arrow drawn over them; a marker
beyond the minimap's edge is held on its rim. The full map stands on a
panel with its title, the cells explored and the distance to its edge
above it and the keys that work on it (MAP_HINTS) below; faint lines cross
it every scale bar's length (`scale_bar`) east and north of the grid's
middle, and the scale bar is at its bottom left. On it the markers are
named to their right, starting LABEL_OFFSET from them, the names of
markers within MERGE_REACH of an earlier one joined to its name (`merge`),
each name moved down while it would cover one before it (`separate`).
"""

import math

import numpy as np

import shoonyakasha as sk
from shoonyakasha.planet.rocks import ORE_ROCKS
from shoonyakasha.planet.vec import add, dot, local_frame, normalize, scale, sub
from shoonyakasha.toolkit.drawing import Mesh
from shoonyakasha.toolkit.overlay import CAP_HEIGHT, GOLD, KeyCaps, measure

CELL = 25.0                      # m between cells
SIZE = 1024                      # cells along each side
REVEAL = 250.0                   # m around the player the probes measure
ORE_SHOWN = ORE_ROCKS
ORE_COLOUR = np.array([1.0, 0.62, 0.15], dtype=np.float32)
UNEXPLORED = np.array([0.04, 0.05, 0.07, 0.82], dtype=np.float32)   # on the minimap
UNEXPLORED_FULL = np.array([0.04, 0.05, 0.07, 1.0], dtype=np.float32)   # on the full map
# (height in m, colour) stops, linear between them.
LAND = [(0.0, (0.30, 0.47, 0.24)), (300.0, (0.42, 0.55, 0.28)), (1_200.0, (0.56, 0.50, 0.34)),
        (2_500.0, (0.55, 0.53, 0.50)), (4_000.0, (0.93, 0.94, 0.96))]
SEA = [(-2_000.0, (0.05, 0.12, 0.30)), (-200.0, (0.12, 0.30, 0.52)), (0.0, (0.25, 0.48, 0.66))]
MOON = [(-2_000.0, (0.22, 0.22, 0.24)), (0.0, (0.48, 0.48, 0.50)), (1_500.0, (0.78, 0.78, 0.80))]
LIGHT = normalize((-1.0, 1.0, 1.4))  # east, north, up: from the north-west, above
SHADE_RELIEF = 3.0               # the slopes exaggerated this much for the shading
MINIMAP = 220                    # canvas units across
MINIMAP_PIXELS = 110             # the minimap image's pixels across, stretched to MINIMAP
MINIMAP_RANGE = 400.0            # m from the centre to the minimap's rim
MINIMAP_MARGIN = 24.0
MINIMAP_INTERVAL = 0.1           # s between the minimap's redraws
FULL_MAP = 720                   # canvas units across
FULL_PIXELS = 360                # the full map image's pixels across, stretched to FULL_MAP
FULL_PAD = 12                    # canvas units of panel around the full map
FULL_HEADER, FULL_FOOTER = 34, 40   # the panel's bands above and below it
MAP_HINTS = [(("Wheel",), "Zoom"), (("LMB",), "Set the waypoint"), (("RMB",), "Clear it"),
             (("M", "Esc"), "Close")]
SCALE_STEPS = (25.0, 50.0, 100.0, 200.0, 250.0, 500.0, 1_000.0, 2_000.0, 2_500.0, 5_000.0)   # m
MERGE_REACH = 14.0               # canvas units within which the full map's markers share a label
LABEL_OFFSET = 12.0              # canvas units from a marker on the full map to the left of its label
FULL_RANGES = (500.0, 1_000.0, 2_000.0, 4_000.0, 8_000.0)   # m from the centre to the edge, by zoom
FULL_INTERVAL = 0.2              # s between the full map's redraws
MAX_LABELS = 12
LABEL_WIDTH, LABEL_HEIGHT = 70.0, 15.0   # canvas units a label keeps clear of earlier ones
TEXT = (0.92, 0.96, 1.0, 0.95)
SHADOW = (0.0, 0.0, 0.0, 0.6)
PANEL = (0.02, 0.024, 0.03, 0.92)
HAIRLINE = (0.95, 0.85, 0.55, 0.75)
MUTED = (0.6, 0.63, 0.68, 1.0)
RIM = (0.95, 0.85, 0.55, 0.7)     # the minimap's edge
GRID_LINE = (1.0, 1.0, 1.0, 0.07)
PLAYER = (1.0, 1.0, 1.0, 1.0)


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

    def grid(self, body, direction):
        """`body`'s grid, made around the site's up on the planet, else around `direction`."""
        grid = self.grids.get(body.name)
        if grid is None:
            origin = self.world.site_frame()[1] if body is self.world.planet else direction
            grid = self.grids[body.name] = Grid(body, origin, self.cell, self.size)
        return grid

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
                grid.set(i, j, height, ore)
            self.asked, self.asking = None, []
        if yield_to:
            world.extra_probes = []
            return
        direction = normalize(position)
        grid = self.grid(body, direction)
        at = grid.index(direction)
        wanted = []
        if at is not None:
            ci, cj = int(round(at[0])), int(round(at[1]))
            for di, dj in self.offsets:
                i, j = ci + di, cj + dj
                if grid.inside(i, j) and (i, j) not in grid.pending and not grid.is_measured(i, j):
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


def render(grid, centre, metres, pixels, heading=None, circle=False, unexplored=UNEXPLORED):
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
    ore = np.where(known, grid.ores[cj, ci], 0.0)
    t = np.clip((ore - ORE_SHOWN) / ORE_SHOWN, 0.0, 1.0)[..., None] * (ore >= ORE_SHOWN)[..., None]
    rgb = rgb * (1.0 - 0.7 * t) + ORE_COLOUR * (0.7 * t)
    rgba = np.concatenate([np.clip(rgb, 0.0, 1.0), np.ones(rgb.shape[:2] + (1,), dtype=np.float32)], axis=-1)
    rgba = np.where(known[..., None], rgba, unexplored)
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


class MapView:
    def __init__(self, engine, reference_size=(1600, 900)):
        ui = engine.ui
        self.ui = ui
        self.canvas = ui.create_canvas(reference_size=reference_size, sort_order=1)
        width, height = FULL_MAP + 2 * FULL_PAD, FULL_MAP + 2 * FULL_PAD + FULL_HEADER + FULL_FOOTER
        self.full_panel = ui.create_panel(self.canvas, size=(width, height), color=PANEL)
        hairline = ui.create_panel(self.full_panel, size=(2, height), color=HAIRLINE)
        ui.set_rect(hairline, anchor_min=(0, 0), anchor_max=(0, 1), pivot=(0, 0), position=(0, 0), size=(2, 0))
        heading = ui.create_text(self.full_panel, "MAP", size=18, color=GOLD)
        self.info = ui.create_text(self.full_panel, "", size=14, color=MUTED)
        for element, align in ((heading, sk.TEXT_ALIGN_LEFT), (self.info, sk.TEXT_ALIGN_RIGHT)):
            ui.set_rect(element, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0), position=(FULL_PAD, 6),
                        size=(FULL_MAP, FULL_HEADER))
            ui.set_text_align(element, align, sk.TEXT_ALIGN_MIDDLE)
        KeyCaps(ui, self.full_panel).show(MAP_HINTS, FULL_PAD,
                                          height - FULL_FOOTER + 0.5 * (FULL_FOOTER - CAP_HEIGHT) - 4, MUTED)
        self.info_text = None
        self.full_image = ui.create_image(self.canvas, size=(FULL_MAP, FULL_MAP))
        self.mini_image = ui.create_image(self.canvas, size=(MINIMAP, MINIMAP))
        self.overlay = ui.create_element(self.canvas)
        ui.set_rect(self.overlay, anchor_min=(0, 0), anchor_max=(1, 1), pivot=(0, 0), position=(0, 0), size=(0, 0))
        for element in (self.full_panel, self.full_image, self.mini_image, self.overlay):
            ui.set_raycast_target(element, False)
        self.labels = [self._text(13) for _ in range(MAX_LABELS)]
        self.scale_text = self._text(12)
        self.texts = {}
        self.open = False
        self.zoom = 0                 # index into FULL_RANGES
        self.since = {"mini": 1e9, "full": 1e9}
        self.full_rect = (0.0, 0.0)   # the full map's top left, canvas units
        self.full_centre = None       # the cell coordinates at its middle, last drawn
        self.set_open(False)

    def _text(self, size):
        pair = []
        for c, offset in ((SHADOW, (1.0, 1.2)), (TEXT, (0.0, 0.0))):
            text = self.ui.create_text(self.canvas, "", size=size, color=c)
            self.ui.set_text_align(text, sk.TEXT_ALIGN_CENTER, sk.TEXT_ALIGN_MIDDLE)
            self.ui.set_raycast_target(text, False)
            pair.append((text, offset))
        return pair

    def _place(self, pair, text, centre, size=(200, 18), colour=None):
        """`text` centred at `centre`; None hides it."""
        for k, (element, offset) in enumerate(pair):
            old = self.texts.get(element)
            if text is None:
                if old is None or old[0] is not None:
                    self.ui.set_visible(element, False)
                self.texts[element] = (None, None, None)
                continue
            new = (text, (round(centre[0] + offset[0], 1), round(centre[1] + offset[1], 1)),
                   colour if k == 1 else None)
            if new == old:
                continue
            if old is None or old[0] is None:
                self.ui.set_visible(element, True)
            if old is None or old[0] != text:
                self.ui.set_text(element, text)
            if old is None or old[1] != new[1]:
                self.ui.set_rect(element, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0.5, 0.5),
                                 position=new[1], size=size)
            if k == 1 and colour is not None and (old is None or old[2] != colour):
                self.ui.set_color(element, colour)
            self.texts[element] = new

    def show(self, on):
        self.ui.set_visible(self.canvas, on)

    def set_open(self, open_):
        self.open = open_
        self.ui.set_visible(self.full_panel, open_)
        self.ui.set_visible(self.full_image, open_)
        self.since["full"] = 1e9

    def zoom_by(self, steps):
        self.zoom = max(0, min(len(FULL_RANGES) - 1, self.zoom - int(steps)))
        self.since["full"] = 1e9

    def click(self, canvas_point):
        """The cell coordinates under a point of the canvas on the open full map, or None."""
        if not self.open or self.full_centre is None:
            return None
        x, y = canvas_point[0] - self.full_rect[0], canvas_point[1] - self.full_rect[1]
        if not (0.0 <= x <= FULL_MAP and 0.0 <= y <= FULL_MAP):
            return None
        return from_pixel((x, y), self.full_centre, FULL_RANGES[self.zoom], FULL_MAP)

    @staticmethod
    def _arrow(mesh, x, y, angle, size, colour):
        """A triangle pointing `angle` radians clockwise from up, at (x, y), with a dark outline."""
        s, c = math.sin(angle), math.cos(angle)

        def p(ax, ay, grow=0.0):
            ax, ay = ax * (1.0 + grow), ay * (1.0 + grow)
            return (x + ax * c - ay * s, y + ax * s + ay * c)
        shape = ((0.0, -size), (0.6 * size, 0.7 * size), (0.0, 0.35 * size), (-0.6 * size, 0.7 * size))
        mesh.face([p(ax, ay, 0.3) for ax, ay in shape], [SHADOW] * 4)
        mesh.face([p(ax, ay) for ax, ay in shape], [colour] * 4)

    @staticmethod
    def _diamond(mesh, x, y, size, colour):
        r = size + 1.5
        mesh.face([(x - r, y), (x, y - r), (x + r, y), (x, y + r)], [SHADOW] * 4)
        mesh.face([(x - size, y), (x, y - size), (x + size, y), (x, y + size)], [colour] * 4)

    def _grid_lines(self, mesh, grid, centre, metres, step, left, top):
        """Faint lines every `step` metres east and north of the grid's
        middle across the full map."""
        per = FULL_MAP / (2.0 * metres)
        for axis in (0, 1):
            offset = (centre[axis] - grid.half) * grid.cell        # m east (north) of the grid's middle
            first = math.ceil((offset - metres) / step)
            for k in range(first, int(math.floor((offset + metres) / step)) + 1):
                d = (k * step - offset) * per
                if axis == 0:
                    x = left + 0.5 * FULL_MAP + d
                    mesh.segment((x, top), (x, top + FULL_MAP), GRID_LINE, 1.0)
                else:
                    y = top + 0.5 * FULL_MAP - d
                    mesh.segment((left, y), (left + FULL_MAP, y), GRID_LINE, 1.0)

    def update(self, dt, grid, at, heading, markers):
        """One frame: `grid` the body's, `at` the player's cell coordinates,
        `heading` its view's in degrees, `markers` (name, cell coordinates,
        colour)."""
        width, height = self.ui.get_canvas_size(self.canvas)
        if width <= 0 or height <= 0 or grid is None or at is None:
            return
        mesh = Mesh()
        labels = []
        # The minimap, turning with the view.
        left, top = width - MINIMAP_MARGIN - MINIMAP, MINIMAP_MARGIN + 40.0
        self.since["mini"] += dt
        if self.since["mini"] >= MINIMAP_INTERVAL:
            self.since["mini"] = 0.0
            self.ui.set_rect(self.mini_image, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0),
                             position=(left, top), size=(MINIMAP, MINIMAP))
            self.ui.set_texture_data(self.mini_image,
                                     render(grid, at, MINIMAP_RANGE, MINIMAP_PIXELS, heading, circle=True))
        r = 0.5 * MINIMAP
        cx, cy = left + r, top + r
        steps = 64
        for k in range(steps):
            a0, a1 = 2.0 * math.pi * k / steps, 2.0 * math.pi * (k + 1) / steps
            p0 = (math.sin(a0), -math.cos(a0))
            p1 = (math.sin(a1), -math.cos(a1))
            mesh.segment((cx + (r + 1.5) * p0[0], cy + (r + 1.5) * p0[1]),
                         (cx + (r + 1.5) * p1[0], cy + (r + 1.5) * p1[1]), SHADOW, 4.0)
            mesh.segment((cx + r * p0[0], cy + r * p0[1]), (cx + r * p1[0], cy + r * p1[1]), RIM, 1.5)
        for name, where, colour in markers:
            px, py = to_pixel(where, at, MINIMAP_RANGE, MINIMAP, heading)
            dx, dy = px - r, py - r
            d = math.hypot(dx, dy)
            if d > r - 6.0:
                dx, dy = dx * (r - 6.0) / d, dy * (r - 6.0) / d
            self._diamond(mesh, cx + dx, cy + dy, 4.0, colour)
        nx, ny = to_pixel((at[0], at[1] + 1.0), at, 1.0, 2.0, heading, cell=1.0)
        n_angle = math.atan2(nx - 1.0, -(ny - 1.0))
        north = (cx + (r + 11.0) * math.sin(n_angle), cy - (r + 11.0) * math.cos(n_angle))
        mesh.face([(north[0] - 8.0, north[1]), (north[0], north[1] - 8.0), (north[0] + 8.0, north[1]),
                   (north[0], north[1] + 8.0)], [PANEL] * 4)
        labels.append(("N", north, GOLD))
        self._arrow(mesh, cx, cy, 0.0, 8.0, PLAYER)

        # The full map, north up.
        if self.open:
            metres = FULL_RANGES[self.zoom]
            left = 0.5 * (width - FULL_MAP)
            top = 0.5 * (height - FULL_MAP - FULL_HEADER - FULL_FOOTER) + FULL_HEADER
            self.full_rect = (left, top)
            self.ui.set_rect(self.full_panel, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0),
                             position=(left - FULL_PAD, top - FULL_PAD - FULL_HEADER),
                             size=(FULL_MAP + 2 * FULL_PAD, FULL_MAP + 2 * FULL_PAD + FULL_HEADER + FULL_FOOTER))
            self.since["full"] += dt
            if self.since["full"] >= FULL_INTERVAL:
                self.since["full"] = 0.0
                self.full_centre = at
                self.ui.set_rect(self.full_image, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0),
                                 position=(left, top), size=(FULL_MAP, FULL_MAP))
                self.ui.set_texture_data(self.full_image,
                                         render(grid, at, metres, FULL_PIXELS, unexplored=UNEXPLORED_FULL))
            centre = self.full_centre or at
            step, bar = scale_bar(metres, FULL_MAP)
            self._grid_lines(mesh, grid, centre, metres, step, left, top)
            found = []
            for name, where, colour in markers:
                px, py = to_pixel(where, centre, metres, FULL_MAP)
                if 0.0 <= px <= FULL_MAP and 0.0 <= py <= FULL_MAP:
                    x, y = left + px, top + py
                    self._diamond(mesh, x, y, 6.0, colour)
                    found.append((name, (x, y), colour))
            for text, (x, y), colour in merge(found):
                labels.append((text, (x + LABEL_OFFSET + 0.5 * measure.width(text, 13), y), colour))
            px, py = to_pixel(at, centre, metres, FULL_MAP)
            self._arrow(mesh, left + px, top + py, math.radians(heading), 10.0, PLAYER)
            bx, by = left + 16.0, top + FULL_MAP - 18.0
            self._scale_bar(mesh, bx, by, bar)
            self._place(self.scale_text, distance_label(step), (bx + 0.5 * bar, by - 12.0), (120, 16))
            info = "%s cells explored     %s to the edge" % (format(grid.measured, ","), distance_label(metres))
            if info != self.info_text:
                self.ui.set_text(self.info, info)
                self.info_text = info
        else:
            self._place(self.scale_text, None, (0.0, 0.0))
        labels = separate(labels)
        for k, pair in enumerate(self.labels):
            if k < len(labels):
                self._place(pair, labels[k][0], labels[k][1], colour=labels[k][2])
            else:
                self._place(pair, None, (0.0, 0.0))
        self.ui.set_geometry(self.overlay, mesh.positions, mesh.indices, mesh.colors)

    @staticmethod
    def _scale_bar(mesh, x, y, length_):
        """The scale bar: a line `length_` long from (x, y) with a tick at each end."""
        for colour, w in ((SHADOW, 4.0), (TEXT, 2.0)):
            mesh.segment((x, y), (x + length_, y), colour, w)
            mesh.segment((x, y - 5.0), (x, y + 1.0), colour, w)
            mesh.segment((x + length_, y - 5.0), (x + length_, y + 1.0), colour, w)


def distance_label(metres):
    return "%.0f m" % metres if metres < 1_000.0 else "%g km" % (metres / 1_000.0)

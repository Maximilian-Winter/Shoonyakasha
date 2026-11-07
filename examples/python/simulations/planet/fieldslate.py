"""
The walker's Field Slate style: light on glass. Thin strokes, a soft
glow, rounded panes and no cut corners, in one light colour for
everything interactive (HOLO) and AMBER for what is worth money or
attention.

Colours: HOLO (focus, values, active), ICE (text), MIST (labels), SLATE
(the glass, GROUND at 88 %), AMBER (ore, credits, caution), MINT (gain,
open) and CORAL (loss, danger), INK (text on a HOLO button), and the
tints built from them: PANE and PANE_EDGE (a pane on the glass),
ACTIVE_EDGE and TRACK (an empty bar or ring). The ui blends in linear
light, so a colour laid at an alpha over a dark ground looks far
brighter than it would blended in sRGB: `tint` gives a colour the alpha
(`blend_alpha`) that looks like an sRGB alpha over a ground, ON_SLATE
(the slate's glass with the world behind it) or ON_WORLD (a daylit sky,
for the HUD).

Faces (registered with scifi.add_faces, so scifi.Layer draws them):
"display" Space Grotesk SemiBold for titles and places, "text" Inter
Regular for body text, "text_medium" Inter Medium for buttons and
labels, "text_strong" Inter SemiBold for a prompt's verb, "num"
JetBrains Mono for every number and "num_medium" JetBrains Mono Medium
for large ones. Sizes are em sizes in canvas units (SIZES gives the type
scale); the ui sizes a face by its ascent plus descent, LINE em, so
`write` hands it the em size times LINE, and `text_width` measures the
same.

Shapes, in canvas units with y down, into a toolkit.drawing.Mesh:
`rounded_rect` (the points of a rounded rectangle), `fill` (a convex
shape), `stroke`, `glow` (a band fading out from a convex shape's edge),
`pane`, `bar`, `ring`, `dot`. Parts, onto a scifi.Layer: `write` (a text
without a shadow), `label` (an upper-case label in MIST), `pill`,
`button`, `keycap`, `hints` (a row of key hints) and `prompt_width`.
Each part returns its width.

Text: `grouped` (thousands with commas), `credits` ("2,000 cr"), `mass`
("42 kg") and `distance` ("640 m", "2.4 km", "75 km").

Motion: SLATE_IN, SLATE_IN_SCALE and SLATE_OUT for the slate, TOAST_SLIDE,
TOAST_SECONDS and TOAST_FADE for toasts, and `Roll`, a number that rolls
to its target over ROLL_SECONDS in place of jumping.
"""

import math

import scifi
from scifi import measure, with_alpha


def hex_colour(text, alpha=1.0):
    """The RGBA colour of `text` ("#RRGGBB") at `alpha`."""
    text = text.lstrip("#")
    return tuple(int(text[k:k + 2], 16) / 255.0 for k in (0, 2, 4)) + (alpha,)


def to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


LUMINANCE = (0.2126, 0.7152, 0.0722)


def blend_alpha(colour, alpha, ground):
    """The alpha at which `colour` blended over `ground` in linear light (as
    the ui blends) looks as it does blended at `alpha` in sRGB: the least
    squares fit over the channels, weighted by their share of luminance."""
    top = bottom = 0.0
    for c, g, w in zip(colour[:3], ground[:3], LUMINANCE):
        lc, lg = to_linear(c), to_linear(g)
        top += w * (to_linear(g + alpha * (c - g)) - lg) * (lc - lg)
        bottom += w * (lc - lg) ** 2
    return max(0.0, min(1.0, top / bottom)) if bottom > 1e-12 else alpha


def tint(colour, alpha, ground):
    """`colour` at the alpha that looks like `alpha` over `ground` (blend_alpha)."""
    return colour[:3] + (blend_alpha(colour, alpha, ground),)


HOLO = hex_colour("#7FE3FF")
ICE = hex_colour("#D6EEF7")
MIST = hex_colour("#8FB0C0")
SLATE = hex_colour("#0F1B24")
AMBER = hex_colour("#FFB547")
MINT = hex_colour("#6BF2B4")
CORAL = hex_colour("#FF7A6B")
INK = hex_colour("#07131A")
ON_SLATE = hex_colour("#12242E")  # how the slate's glass looks with the world behind it
ON_WORLD = hex_colour("#5F87A8")  # a daylit sky, the ground the HUD's glass is tinted for
GROUND = tint(SLATE, 0.88, ON_WORLD)
PANE = tint(ICE, 0.035, ON_SLATE)
PANE_EDGE = tint(ICE, 0.09, ON_SLATE)
ACTIVE_EDGE = tint(HOLO, 0.32, ON_SLATE)
TRACK = tint(ICE, 0.12, ON_SLATE)
TINT = 0.16                      # a pill's ground: its colour at this alpha (before blend_alpha)
GLOW_ALPHA = 0.22                # a glow's alpha at the edge of its shape (before blend_alpha)

FACES = {"display": "fonts/SpaceGrotesk-SemiBold.ttf", "text": "fonts/Inter-Regular.ttf",
         "text_medium": "fonts/Inter-Medium.ttf", "text_strong": "fonts/Inter-SemiBold.ttf",
         "num": "fonts/JetBrainsMono-Regular.ttf", "num_medium": "fonts/JetBrainsMono-Medium.ttf"}
LINE = {"display": 1.29, "text": 1.22, "text_medium": 1.22, "text_strong": 1.22, "num": 1.32, "num_medium": 1.32}
ESTIMATES = {face: 0.55 / LINE[face] for face in FACES}   # widths per size without Pillow
scifi.add_faces(FACES, ESTIMATES)

SIZES = {"label": 11, "small": 13, "body": 15, "lead": 18, "heading": 20, "title": 30, "number": 26}   # em, px

RADIUS = 14.0                    # canvas units: a pane's corners
RADIUS_SMALL = 8.0               # a row's, a button's, a toast's
CORNER_STEP = 4.0                # canvas units of arc per point of a rounded corner, at most
PILL_HEIGHT, PILL_PAD = 20.0, 9.0
BUTTON_HEIGHT, BUTTON_PAD = 36.0, 18.0
CAP_HEIGHT, CAP_PAD, CAP_SIZE = 22.0, 7.0, 11
CAP_GAP, CAP_LABEL_GAP, HINT_GAP = 4.0, 7.0, 16.0

SLATE_IN = 0.22                  # s the slate takes to open, growing from SLATE_IN_SCALE of its size
SLATE_IN_SCALE = 0.96
SLATE_OUT = 0.14                 # s it takes to close
TOAST_SLIDE = 12.0               # canvas units a toast slides in
TOAST_SECONDS = 4.0              # s a toast shows before it fades
TOAST_FADE = 0.5                 # s it fades over
ROLL_SECONDS = 0.4               # s a rolling number takes to reach its target


# ── Shapes ─────────────────────────────────────────────────────────────────

def rounded_rect(left, top, width, height, radius=RADIUS):
    """The points, clockwise on screen from the top left corner's arc, of a
    rectangle with corners rounded by `radius` (at most half its sides)."""
    r = max(0.0, min(radius, 0.5 * width, 0.5 * height))
    if r <= 0.0:
        return [(left, top), (left + width, top), (left + width, top + height), (left, top + height)]
    steps = max(2, int(math.ceil(0.5 * math.pi * r / CORNER_STEP)))
    points = []
    # each corner's centre, and its arc's first angle (clockwise on screen, from the -x axis)
    for cx, cy, start in ((left + r, top + r, math.pi), (left + width - r, top + r, 1.5 * math.pi),
                          (left + width - r, top + height - r, 0.0), (left + r, top + height - r, 0.5 * math.pi)):
        for k in range(steps + 1):
            a = start + 0.5 * math.pi * k / steps
            point = (cx + r * math.cos(a), cy + r * math.sin(a))
            if not points or math.hypot(point[0] - points[-1][0], point[1] - points[-1][1]) > 1e-6:
                points.append(point)
    if math.hypot(points[0][0] - points[-1][0], points[0][1] - points[-1][1]) <= 1e-6:
        points.pop()
    return points


def _outward(points):
    """Each point's outward unit normal (the mean of its two edges'), for a
    convex shape clockwise on screen, and how far to move along it for a
    band of one unit."""
    n = len(points)
    normals = []
    for k in range(n):
        a, b, c = points[k - 1], points[k], points[(k + 1) % n]
        e1 = _edge_normal(a, b)
        e2 = _edge_normal(b, c)
        x, y = e1[0] + e2[0], e1[1] + e2[1]
        size = math.hypot(x, y)
        if size < 1e-9:
            normals.append((e2, 1.0))
            continue
        x, y = x / size, y / size
        cos_half = max(0.2, x * e2[0] + y * e2[1])
        normals.append(((x, y), 1.0 / cos_half))
    return normals


def _edge_normal(a, b):
    """The outward unit normal of the edge from `a` to `b`, clockwise on screen (y down)."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    size = math.hypot(dx, dy)
    if size < 1e-9:
        return (0.0, 0.0)
    return (dy / size, -dx / size)


def fill(mesh, points, colour):
    """A convex shape filled, with a one-unit fringe fading out round it."""
    if len(points) < 3:
        return
    mesh.face(points, [colour] * len(points))
    band(mesh, points, colour, with_alpha(colour, 0.0), 1.0)


def band(mesh, points, inner, outer, reach):
    """A band `reach` wide round a convex shape, `inner` at its edge
    fading to `outer` (a negative reach goes inwards)."""
    normals = _outward(points)
    out = [(p[0] + n[0] * s * reach, p[1] + n[1] * s * reach) for p, (n, s) in zip(points, normals)]
    count = len(points)
    for k in range(count):
        j = (k + 1) % count
        mesh.face([points[k], points[j], out[j], out[k]], [inner, inner, outer, outer])


def stroke(mesh, points, colour, width=1.0):
    """The closed outline of a convex shape, `width` wide, centred on its edge."""
    if width <= 0.0:
        return
    half = 0.5 * width
    normals = _outward(points)
    inner = [(p[0] - n[0] * s * half, p[1] - n[1] * s * half) for p, (n, s) in zip(points, normals)]
    outer = [(p[0] + n[0] * s * half, p[1] + n[1] * s * half) for p, (n, s) in zip(points, normals)]
    count = len(points)
    for k in range(count):
        j = (k + 1) % count
        mesh.face([inner[k], inner[j], outer[j], outer[k]], [colour] * 4)
    clear = with_alpha(colour, 0.0)
    band(mesh, outer, colour, clear, 1.0)
    band(mesh, inner, colour, clear, -1.0)


def glow(mesh, points, colour, reach=10.0, alpha=GLOW_ALPHA, ground=ON_SLATE):
    """A soft glow `reach` wide fading out from a convex shape's edge, at
    `alpha` (as over `ground`) there."""
    band(mesh, points, tint(colour, alpha, ground), with_alpha(colour, 0.0), reach)


def pane(mesh, left, top, width, height, radius=RADIUS, fill_colour=PANE, edge=PANE_EDGE, edge_width=1.0,
         glow_colour=None, glow_reach=12.0):
    """A rounded pane: its glow (with `glow_colour`), its fill and its edge. Returns its points."""
    points = rounded_rect(left, top, width, height, radius)
    if glow_colour is not None:
        glow(mesh, points, glow_colour, glow_reach)
    if fill_colour is not None:
        fill(mesh, points, fill_colour)
    if edge is not None:
        stroke(mesh, points, edge, edge_width)
    return points


def bar(mesh, left, top, width, value, colour=HOLO, height=3.0, track=TRACK):
    """A thin rounded bar `width` across, its first `value` (0 to 1) lit in `colour`."""
    if track is not None:
        fill(mesh, rounded_rect(left, top, width, height, 0.5 * height), track)
    lit = max(0.0, min(1.0, value)) * width
    if lit >= 1.0:
        fill(mesh, rounded_rect(left, top, lit, height, 0.5 * height), colour)
    return lit


def ring(mesh, cx, cy, radius, value, colour=HOLO, width=3.0, track=TRACK, steps=72):
    """A ring gauge: the track all round and `value` (0 to 1) of it lit
    clockwise from the top in `colour`."""
    if track is not None:
        _arc_band(mesh, cx, cy, radius, width, 0.0, 2.0 * math.pi, track, steps)
    value = max(0.0, min(1.0, value))
    if value > 0.0:
        _arc_band(mesh, cx, cy, radius, width, 0.0, 2.0 * math.pi * value, colour, steps)


def _arc_band(mesh, cx, cy, radius, width, start, end, colour, steps):
    """A band `width` wide along the circle from `start` to `end` radians clockwise from up."""
    n = max(2, int(math.ceil(steps * abs(end - start) / (2.0 * math.pi))))
    clear = with_alpha(colour, 0.0)
    rings = (radius - 0.5 * width - 1.0, radius - 0.5 * width, radius + 0.5 * width, radius + 0.5 * width + 1.0)
    alphas = (clear, colour, colour, clear)
    previous = None
    for k in range(n + 1):
        a = start + (end - start) * k / n
        s, c = math.sin(a), -math.cos(a)
        here = [(cx + r * s, cy + r * c) for r in rings]
        if previous is not None:
            for j in range(3):
                mesh.face([previous[j], here[j], here[j + 1], previous[j + 1]],
                          [alphas[j], alphas[j], alphas[j + 1], alphas[j + 1]])
        previous = here


def dot(mesh, x, y, radius, colour, hollow=False, width=1.2):
    """A round dot, or with `hollow` its outline."""
    steps = max(8, int(math.ceil(2.0 * math.pi * radius / 2.0)))
    points = [(x + radius * math.cos(2.0 * math.pi * k / steps), y + radius * math.sin(2.0 * math.pi * k / steps))
              for k in range(steps)]
    if hollow:
        stroke(mesh, points, colour, width)
    else:
        fill(mesh, points, colour)


# ── Parts ──────────────────────────────────────────────────────────────────

def em(size):
    """`size` (a key of SIZES, or an em size in canvas units) in canvas units."""
    return SIZES.get(size, size)


def text_width(text, size="body", face="text"):
    """The width of `text` at the em size `size` in `face`."""
    return measure.width(text, em(size) * LINE[face], face)


def write(layer, text, at, size="body", colour=ICE, face="text", align="left"):
    """`text` at `at` (its left, middle or right by `align`, its middle
    across) at the em size `size` (a key of SIZES, or canvas units).
    Returns its width."""
    return layer.text(text, at, em(size) * LINE[face], colour, face, align, shadow=False)


def label(layer, text, at, colour=MIST, align="left", size="label"):
    """An upper-case label, in Inter Medium."""
    return write(layer, text.upper(), at, size, colour, "text_medium", align)


def pill_width(text, size="label"):
    return text_width(text.upper(), size, "text_medium") + 2.0 * PILL_PAD


def pill(layer, text, x, y, colour, align="left", size="label", ground=ON_SLATE):
    """A tag: `text` upper-case in `colour` on a pill of it at TINT over
    `ground`, its middle across at `y` and its left, middle or right at `x`
    by `align`. Returns its width."""
    width = pill_width(text, size)
    left = x - scifi.ALIGN[align] * width
    fill(layer.mesh, rounded_rect(left, y - 0.5 * PILL_HEIGHT, width, PILL_HEIGHT, 0.5 * PILL_HEIGHT),
         tint(colour, TINT, ground))
    write(layer, text.upper(), (left + 0.5 * width, y), size, colour, "text_medium", "center")
    return width


def button_width(text, value=None, size="body"):
    width = text_width(text, size, "text_medium") + 2.0 * BUTTON_PAD
    if value:
        width += text_width(" · ", size, "text_medium") + text_width(value, size, "num")
    return width


def button(layer, left, top, width, text, value=None, kind="primary", hover=False, enabled=True, height=BUTTON_HEIGHT,
           size="body", ground=ON_SLATE):
    """A button over `ground`: "primary" HOLO with INK text, "secondary" a
    pane with ICE text, or "text" HOLO text alone; its text centred, then
    " · " and `value` in the mono face when given. Returns its width."""
    mesh = layer.mesh
    alpha = 1.0 if enabled else 0.4
    lit = hover and enabled
    points = rounded_rect(left, top, width, height, RADIUS_SMALL)
    if kind == "primary":
        if lit:
            glow(mesh, points, HOLO, 10.0, 0.35, ground)
        fill(mesh, points, with_alpha(HOLO, alpha))
        colour = INK
    elif kind == "secondary":
        fill(mesh, points, tint(ICE, 0.08 if lit else 0.04, ground))
        stroke(mesh, points, tint(HOLO, 0.45, ground) if lit else tint(ICE, 0.16, ground))
        colour = with_alpha(ICE, alpha)
    else:
        if lit:
            fill(mesh, points, tint(HOLO, 0.1, ground))
        colour = with_alpha(HOLO, alpha)
    text_w = text_width(text, size, "text_medium")
    tail = (text_width(" · ", size, "text_medium") + text_width(value, size, "num")) if value else 0.0
    x = left + 0.5 * (width - text_w - tail)
    y = top + 0.5 * height
    x += write(layer, text, (x, y), size, colour, "text_medium")
    if value:
        x += write(layer, " · ", (x, y), size, colour, "text_medium")
        write(layer, value, (x, y), size, colour, "num")
    return width


def _cap_text(name):
    return name[4:] if name.startswith("pad:") else name


def keycap_width(name):
    return max(CAP_HEIGHT, text_width(_cap_text(name), CAP_SIZE, "num_medium") + 2.0 * CAP_PAD)


def keycap(layer, name, x, y, alpha=1.0):
    """A key, `name` ("Tab", "E", "pad:A"), in a rounded box from `x`, its
    middle across at `y`; a gamepad's filled. Returns its width."""
    width = keycap_width(name)
    points = rounded_rect(x, y - 0.5 * CAP_HEIGHT, width, CAP_HEIGHT, 5.0)
    pad = name.startswith("pad:")
    fill(layer.mesh, points, with_alpha(HOLO, 0.25 * alpha) if pad else with_alpha(SLATE, 0.75 * alpha))
    stroke(layer.mesh, points, with_alpha(ICE, 0.4 * alpha))
    write(layer, _cap_text(name), (x + 0.5 * width, y), CAP_SIZE, with_alpha(ICE, alpha), "num_medium", "center")
    return width


def hint_width(hint, size="small"):
    keys, text = hint
    width = sum(keycap_width(k) for k in keys) + CAP_GAP * max(0, len(keys) - 1)
    return width + (CAP_LABEL_GAP + text_width(text, size) if text else 0.0)


def hints_width(hints, size="small"):
    return sum(hint_width(h, size) for h in hints) + HINT_GAP * max(0, len(hints) - 1)


def hints(layer, items, x, y, colour=ICE, alpha=1.0, size="small", align="left"):
    """Key hints, each (keys, text), in a row from `x` (its left, middle or
    right by `align`), its middle across at `y`. Returns its width."""
    total = hints_width(items, size)
    x -= scifi.ALIGN[align] * total
    for n, (keys, text) in enumerate(items):
        if n:
            x += HINT_GAP
        for k, name in enumerate(keys):
            if k:
                x += CAP_GAP
            x += keycap(layer, name, x, y, alpha)
        if text:
            x += CAP_LABEL_GAP
            x += write(layer, text, (x, y), size, with_alpha(colour, alpha))
    return total


def prompt_width(key, verb, target, value=None, size="lead"):
    """The width of a reach prompt's content (`hud` draws it): key, verb, target and value."""
    width = keycap_width(key) + 12.0 + text_width(verb, size, "text_strong") + 9.0 + text_width(target, size)
    if value:
        width += 28.0 + text_width(value, "body", "num")
    return width


# ── Text ───────────────────────────────────────────────────────────────────

def grouped(value, decimals=0):
    """`value` with commas between the thousands: 1430 -> "1,430"."""
    return "{:,.{}f}".format(value, decimals)


def credits(value):
    return grouped(round(value)) + " cr"


def mass(kg):
    return grouped(round(kg)) + " kg"


def distance(metres):
    """Metres under 1 km, tenths of a km under 10 km, whole km beyond."""
    if metres < 1_000.0:
        return "%d m" % round(metres)
    if metres < 10_000.0:
        return "%.1f km" % (metres / 1_000.0)
    return grouped(round(metres / 1_000.0)) + " km"


# ── Motion ─────────────────────────────────────────────────────────────────

def ease_out(t):
    """0 at 0, 1 at 1, slowing towards the end."""
    t = max(0.0, min(1.0, t))
    return 1.0 - (1.0 - t) ** 3


class Roll:
    """A shown number that rolls to its target over ROLL_SECONDS, from
    wherever it is, when the target changes."""

    def __init__(self, value=0.0, seconds=ROLL_SECONDS):
        self.shown = self.start = self.target = value
        self.seconds = seconds
        self.t = seconds

    def update(self, target, dt):
        """The number to show this frame, rolling to `target`."""
        if target != self.target:
            self.start, self.target, self.t = self.shown, target, 0.0
        self.t = min(self.seconds, self.t + dt)
        self.shown = self.start + (self.target - self.start) * ease_out(self.t / self.seconds if self.seconds else 1.0)
        return self.shown

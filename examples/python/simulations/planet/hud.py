"""
The walker's head-up display (walker.py), in the Field Slate style
(fieldslate.py): one scifi.Layer of shapes and texts drawn afresh each
frame on a screen canvas laid out for REFERENCE_SIZE divided by the HUD's
scale (`set_scale`). Positions are canvas units, x to the right and y
down from the top left. `update(frame)` draws one Frame; what it shows
depends on the frame's `mode`, "foot", "car" or "space".

In every mode:
- **Toasts** at the top right: the last MAX_TOASTS messages handed to
  `toast`, newest on top, each a pane with a dot of its colour, its text
  and its detail at the right; each slides in TOAST_SLIDE from the right
  over TOAST_IN, shows for fieldslate.TOAST_SECONDS and fades out over
  fieldslate.TOAST_FADE.
- **Tint** under everything else, when given: a colour over the whole
  screen at TINT_CENTRE of its alpha and bands TINT_EDGE of the height
  along the edges at its full alpha, fading inwards (the walker's suit).
- **Flash** over the tint, when given: a colour evenly over the screen
  (the walker's meteor impacts).

On foot and in the car:
- **Pinned** at the top left, as last handed to `pinned`: its label, its
  title, a bar of its progress, and the progress's text with the time
  left under it.
- **Compass** across the top: a strip COMPASS_WIDTH wide showing
  COMPASS_SPAN degrees either side of the heading, a tick every 5
  degrees, longer every 15, the eight directions named every 45 above it;
  the heading in degrees under its middle. Markers (name, position,
  colour) stand on it at their bearing from the eye, along the ground
  (`bearing`), the MAX_MARKERS nearest (`markers`), a waypoint as a
  triangle, the others as diamonds; one beyond the span is held at the
  strip's end, dimmed. Under the heading, a row of the markers within the
  span, left to right, each with its name and distance along the ground,
  keeping LABEL_GAP between them (`label_rows`).

On foot:
- **Reticle** at the centre: corner brackets and a dot, in HOLO while
  something can be grabbed, dim while not, CORAL while it is too heavy or
  hot, AMBER while holding, with a ring of the charge while a punt
  charges; under it the charge, or the hold distance for HOLD_SECONDS
  after it changes (`status_text`).
- **Reach prompt** under the reticle while something is in reach: a pane
  with the key, the verb, the target and what it is worth, and a bar of a
  held use's progress; the frame's other hints in a row under it.
- **Suit** at the bottom left: a ring of the suit's reserve with its
  share in it, the suit's state and the temperature, the health when
  hurt, and the jet's fuel while it is not full (and FUEL_LINGER after it
  fills, fading over FUEL_FADE).
- **Readout** at the bottom right, left of the minimap: the speed along
  the ground, the body, the hour and, off the ground, the height above
  it, and the frame's corner hints.

In the car:
- **Car** at the bottom left: the fuel with its range at this speed and
  the hold's cargo, each with a bar.
- **Height ladder** at the right: the height above the ground from
  hovercar.HOVER_MIN to MAX_HEIGHT on even steps between LADDER's heights,
  the height the car keeps marked and the height it is at.
- **Readout** at the bottom right: the speed in km/h, the height above
  the sea and above the ground, the body and the hour, and the hints.

In space mode:
- **Transfer** at the top, around the planet: the angle the moon leads
  by, the angle to burn at, the time until the burn and its delta-v, a bar
  of the delta-v it uses and what is left after it.
- **Boresight** at the centre, and the **prograde** and **retrograde**
  markers and the **moon**'s label where they fall in the view (`project`).
- **Orbit** at the bottom left: the body, the periapsis, the apoapsis and
  the delta-v left with a bar of it against a full tank's.
- **Readout** at the bottom right: the speed in m/s, the nose hold, the
  assist and the warp as pills, the throttle with a bar, and the hints.
"""

import math
from dataclasses import dataclass, field

import shoonyakasha as sk
from shoonyakasha.planet.vec import dot, length, local_frame, normalize, scale, sub

import fieldslate as fs
import scifi
from fieldslate import AMBER, CORAL, HOLO, ICE, MINT, MIST, with_alpha

REFERENCE_SIZE = (1600, 900)
TEXT = ICE
DIM = MIST
ACCENT = AMBER
WARNING = CORAL
GOOD = MINT
RETICLE = {"idle": with_alpha(HOLO, 0.45), "can_grab": HOLO, "too_heavy": CORAL, "too_hot": CORAL,
           "holding": AMBER}
BRACKET, BRACKET_ARM = 16.0, 6.0     # canvas units from the centre to the brackets' corners; their arms
RING_RADIUS = 24.0
STATUS_BELOW = 34.0              # canvas units under the centre the status is centred
HOLD_SECONDS = 1.2
MARGIN = 40.0                    # canvas units from the screen's edges to the parts
PINNED_WIDTH = 340.0
COMPASS_TOP = 44.0               # canvas units from the top to the strip's line
COMPASS_WIDTH = 640.0
COMPASS_SPAN = 60.0              # degrees either side of the heading
LABELS = {0: "N", 45: "NE", 90: "E", 135: "SE", 180: "S", 225: "SW", 270: "W", 315: "NW"}
TICK_LENGTH = 7.0                # canvas units of the longest tick
HEADING_Y = 76.0                 # canvas units from the top to the heading's middle
LABEL_Y = 103.0                  # to the markers' labels' middle
MAX_MARKERS = 8
LABEL_GAP = 120.0                # canvas units between labels' middles, at least
LABEL_ROWS = 1
TOAST_WIDTH, TOAST_HEIGHT = 330.0, 36.0   # a toast of one line
TOAST_TOP = 111.0                # canvas units from the top to the newest toast's top
TOAST_GAP = 10.0                 # between toasts
TOAST_LINE, TOAST_LINES = 20.0, 2   # canvas units between a toast's lines, and its lines at most
TOAST_IN = 0.25                  # s a toast takes to slide in
MAX_TOASTS = 4
PROMPT_BELOW = 60.0              # canvas units under the centre the reach prompt is centred
PROMPT_HEIGHT, PROMPT_PAD = 42.0, 16.0
PROMPT_HINTS_BELOW = 95.0        # the hints under it
SUIT_RING = (76.0, 72.0, 30.0)   # the suit ring's centre from the left and the bottom, its radius
LOW_FUEL = 0.25
FUEL_LINGER, FUEL_FADE = 1.5, 0.5     # s
TINT_CENTRE, TINT_EDGE = 0.35, 0.22   # the suit's tint: its share of alpha all over, its edge bands' width
MINIMAP_SPACE = 200.0            # canvas units the minimap takes at the bottom right, on foot
AIRBORNE = 0.5                   # m the feet must be above the ground for the readout to give the height
LADDER = (0.45, 30.0, 100.0, 300.0)   # m: the height ladder's marked heights, evenly apart
LADDER_X, LADDER_TOP, LADDER_BOTTOM = 1308.0, 338.0, 563.0   # in the reference layout, from the right edge
CAR_WIDTH = 300.0
TRANSFER = (658.0, 136.0)        # the transfer panel's size
BORESIGHT = 46.0                 # canvas units either side of the centre


@dataclass
class Prompt:
    """What is in reach: `keys` (a key's name, or one for each input),
    `verb`, `target`, `detail` (what it is worth) in `detail_colour`, and a
    held use's `progress` (0 to 1) or None."""
    key: str
    verb: str
    target: str
    detail: str = ""
    detail_colour: tuple = AMBER
    progress: float = None


@dataclass
class Frame:
    """One frame of the HUD. In every mode: `view` (right, up, forward) of
    the camera, `eye` from the planet's centre, `body` the world body under
    it, `targets` the compass markers (name, position from the planet's
    centre, colour), `dt` the seconds since the last frame, `hour` (a
    text), `tint` and `flash` (((r, g, b), alpha) or None), `fov` the
    view's vertical field of view in degrees, `speed` (m/s), and the
    corner `hints` ((keys, text), ...).

    On foot: the gun's `status`, whether it `held` something, its
    `charge` and `hold_distance`; the reach `prompt` (Prompt or None) and
    the hints under it (`prompt_hints`); `height` the feet's above the
    ground (m, or None); `jet` the jet's fuel (0 to 1); the suit's
    `reserve` (0 to 1), `health` (0 to 1), `temperature` (degrees C or
    None) and `suit_state` ("ok", "cold" or "hot").

    In the car: `fuel` and `fuel_capacity` (kg), `fuel_range` (m at this
    speed, or None), `cargo` and `cargo_capacity` (kg), `height` above the
    ground, `keep` the height it keeps, `altitude` above the sea (m).

    In space mode: `space`, play.spaceflight.space_readout's dict, with
    "dv_full" the delta-v of a full tank."""
    mode: str
    view: tuple
    eye: tuple
    body: object
    targets: list = field(default_factory=list)
    dt: float = 0.0
    hour: str = None
    tint: tuple = None
    flash: tuple = None
    fov: float = 70.0
    speed: float = 0.0
    hints: list = field(default_factory=list)
    status: str = "idle"
    held: bool = False
    charge: float = 0.0
    hold_distance: float = None
    prompt: Prompt = None
    prompt_hints: list = field(default_factory=list)
    height: float = None
    jet: float = 1.0
    reserve: float = 1.0
    health: float = 1.0
    temperature: float = None
    suit_state: str = "ok"
    fuel: float = 0.0
    fuel_capacity: float = 1.0
    fuel_range: float = None
    cargo: float = 0.0
    cargo_capacity: float = 1.0
    keep: float = None
    altitude: float = None
    space: dict = None


# ── Geometry ───────────────────────────────────────────────────────────────

def bearing(up, direction):
    """Degrees clockwise from north (east is 90) of `direction` along the
    ground at the unit `up`, in [0, 360); None when it is along `up`."""
    east, north = local_frame(up)
    along = sub(direction, scale(up, dot(direction, up)))
    if length(along) < 1e-9:
        return None
    return math.degrees(math.atan2(dot(along, east), dot(along, north))) % 360.0


def relative(angle, heading):
    """`angle` less `heading` in degrees, wrapped to [-180, 180)."""
    return (angle - heading + 180.0) % 360.0 - 180.0


def ground_distance(radius, up, point_up):
    """Metres along the ground of a body of `radius` between the unit directions `up` and `point_up`."""
    return radius * math.acos(max(-1.0, min(1.0, dot(up, point_up))))


def compass_x(rel, centre=REFERENCE_SIZE[0] * 0.5):
    """(x on the strip, whether it is within the span) for a bearing `rel` degrees off the heading."""
    inside = -COMPASS_SPAN <= rel <= COMPASS_SPAN
    rel = max(-COMPASS_SPAN, min(COMPASS_SPAN, rel))
    return centre + rel * (0.5 * COMPASS_WIDTH / COMPASS_SPAN), inside


def compass_ticks(heading):
    """(degrees off the heading, tick length, label or None) of the ticks within the span."""
    ticks = []
    first = int(math.ceil((heading - COMPASS_SPAN) / 5.0)) * 5
    for angle in range(first, int(math.floor(heading + COMPASS_SPAN)) + 1, 5):
        a = angle % 360
        label = LABELS.get(a)
        size = 1.0 if label else (0.6 if a % 15 == 0 else 0.35)
        ticks.append((angle - heading, size, label))
    return ticks


def markers(centre, up, heading, radius, targets):
    """For each (name, position, colour) of `targets` (positions from the
    body's centre, as `centre`, the eye): (x on the strip, within the span,
    name, metres along the ground, colour), nearest first, at most
    MAX_MARKERS."""
    found = []
    for name, position, colour in targets:
        d = sub(position, centre)
        b = bearing(up, d)
        if b is None:
            continue
        x, inside = compass_x(relative(b, heading))
        found.append((x, inside, name, ground_distance(radius, up, normalize(position)), colour))
    found.sort(key=lambda m: m[3])
    return found[:MAX_MARKERS]


def label_rows(xs, gap=LABEL_GAP, rows=LABEL_ROWS):
    """For each marker's x on the strip (None: not labelled), the first of
    `rows` rows whose labels so far are all at least `gap` away from it,
    nearest markers first as given; None when none is, or for None."""
    placed = [[] for _ in range(rows)]
    result = []
    for x in xs:
        row = None
        if x is not None:
            row = next((r for r in range(rows) if all(abs(x - other) >= gap for other in placed[r])), None)
            if row is not None:
                placed[row].append(x)
        result.append(row)
    return result


def distance_text(metres):
    return "%.0f m" % metres if metres < 1_000.0 else "%.1f km" % (metres / 1_000.0)


def due_text(hours):
    """Time left: days and hours from a day, hours under a day, minutes under an hour."""
    if hours >= 24.0:
        days = int(hours // 24.0)
        return "%d d %d h" % (days, int(hours - 24.0 * days))
    if hours >= 1.0:
        return "%d h" % int(hours)
    return "%d min" % int(round(60.0 * hours))


def ladder_y(height, top=LADDER_TOP, bottom=LADDER_BOTTOM, marks=LADDER):
    """The y on the height ladder of `height`: LADDER's heights evenly apart
    from `bottom` up to `top`, straight between them, held at the ends."""
    step = (bottom - top) / (len(marks) - 1)
    if height <= marks[0]:
        return bottom
    for k in range(1, len(marks)):
        if height <= marks[k]:
            share = (height - marks[k - 1]) / (marks[k] - marks[k - 1])
            return bottom - step * (k - 1 + share)
    return top


def project(direction, view, fov, width, height):
    """(x, y) on a canvas `width` by `height` of a direction from the
    camera, its `view` (right, up, forward) and vertical `fov` in degrees;
    None behind it."""
    right, up, forward = view
    z = dot(direction, forward)
    if z <= 1e-6:
        return None
    f = 0.5 * height / math.tan(math.radians(fov) * 0.5)
    return 0.5 * width + f * dot(direction, right) / z, 0.5 * height - f * dot(direction, up) / z


def hints(status, held, charge, pad=False):
    """The key hints (keys, label) for what the gun's controls do now, for
    the gun's `status`; the gamepad's with `pad`."""
    grab, punt = ("pad:LT",), ("pad:RT",)
    if not pad:
        grab, punt = ("RMB",), ("LMB",)
    if held:
        if charge > 0.0:
            return [(punt, "Release to throw")]
        found = [(grab, "Drop"), (punt, "Throw, hold to charge")]
        if pad:
            return found + [(("pad:LB", "pad:RB"), "Distance")]
        return found + [(("R",), "Turn with the mouse"), (("Wheel",), "Distance")]
    if status == "can_grab":
        return [(grab, "Grab")]
    return []


def drive_hints(pad=False, can_leave=False, high=False, space=False, space_height=250.0, hover_back=300.0):
    """The corner hints in the car: getting out while it `can_leave`, and
    space mode (at `space_height` up; ready while `high`); in space mode
    (`space`) the assist, the nose hold, the time warp and back to hovering
    within `hover_back` of the ground."""
    if space:
        return [(("T",), "Assist"), (("H",), "Nose hold"), ((",", "."), "Warp"),
                (("G",), "Hover below %.0f m" % hover_back)]
    found = [((("pad:X",) if pad else ("F",)), "Get out")] if can_leave else []
    return found + [(("G",), "Space mode" if high else "Space mode above %.0f m" % space_height)]


def status_text(status, held, charge, hold_distance=None):
    """(text, colour) under the crosshair, or (None, None): "TOO HEAVY",
    "TOO HOT", the charge, or `hold_distance` when given."""
    if status == "too_heavy":
        return "TOO HEAVY", WARNING
    if status == "too_hot":
        return "TOO HOT", WARNING
    if held and charge > 0.0:
        return "CHARGE %d %%" % round(100.0 * charge), ACCENT
    if held and hold_distance is not None:
        return "HOLD %.1f M" % hold_distance, TEXT
    return None, None


def km_text(metres):
    return "escape" if math.isinf(metres) else "%s km" % fs.grouped(metres / 1_000.0, 1)


class Hud:
    def __init__(self, engine):
        ui = engine.ui
        self.ui = ui
        self.canvas = ui.create_canvas(reference_size=REFERENCE_SIZE)
        self.layer = scifi.Layer(ui, self.canvas)
        self.toasts = []              # [text, colour, detail, seconds left, seconds shown], newest last
        self.hold_distance = None     # the gun's hold distance last frame
        self.hold_shown = 0.0         # s the hold distance stays shown
        self.fuel_shown = 0.0         # s the jet fuel stays shown
        self.card = None              # (label, title, fraction or None, progress, due)
        self.speed = fs.Roll()

    def show(self, on):
        self.ui.set_visible(self.canvas, on)

    def set_scale(self, s):
        """The HUD drawn `s` times its size: the canvas laid out for REFERENCE_SIZE / s."""
        self.ui.set_canvas_scaling(self.canvas, sk.CANVAS_SCALE_WITH_SCREEN,
                                   reference_size=(REFERENCE_SIZE[0] / s, REFERENCE_SIZE[1] / s))

    def toast(self, text, colour=ACCENT, detail=None):
        """A message at the top right, with a dot of `colour` and `detail` at its right."""
        self.toasts.append([text, colour, detail, fs.TOAST_SECONDS + fs.TOAST_FADE, 0.0])
        del self.toasts[:-MAX_TOASTS]

    def pinned(self, label, title, fraction=None, progress=None, due=None):
        """The pinned goal at the top left: its `label`, `title`, progress
        as a `fraction` (None for no bar) and a text, and the time left
        (`due`, a text); `title` None hides it."""
        self.card = None if title is None else (label, title, fraction, progress, due)

    # ── Every mode ──

    def draw_toasts(self, dt, width):
        layer, mesh = self.layer, self.layer.mesh
        for toast in self.toasts:
            toast[3] -= dt
            toast[4] += dt
        self.toasts = [t for t in self.toasts if t[3] > 0.0]
        top = TOAST_TOP
        for text, colour, detail, left, age in reversed(self.toasts):
            fade = min(1.0, left / fs.TOAST_FADE, age / TOAST_IN)
            slide = (1.0 - min(1.0, age / TOAST_IN)) ** 2 * fs.TOAST_SLIDE
            x = width - MARGIN - TOAST_WIDTH + slide
            room = TOAST_WIDTH - 48.0
            if detail:
                room -= fs.text_width(detail, "small", "num") + 12.0
            lines = wrap(text, room, "body")
            if len(lines) > TOAST_LINES:
                lines = lines[:TOAST_LINES - 1] + [fit(" ".join(lines[TOAST_LINES - 1:]), room, "body")]
            h = TOAST_HEIGHT + TOAST_LINE * (len(lines) - 1)
            y = top + 0.5 * TOAST_HEIGHT
            fs.pane(mesh, x, top, TOAST_WIDTH, h, min(0.5 * TOAST_HEIGHT, fs.RADIUS), with_alpha(fs.GROUND, fade),
                    fs.tint(ICE, 0.12 * fade, fs.ON_WORLD))
            fs.dot(mesh, x + 18.0, y, 3.5, with_alpha(colour, fade))
            if detail:
                fs.write(layer, detail, (x + TOAST_WIDTH - 18.0, y), "small", with_alpha(MIST, fade), "num", "right")
            for n, line in enumerate(lines):
                fs.write(layer, line, (x + 30.0, y + TOAST_LINE * n), "body", with_alpha(ICE, fade))
            top += h + TOAST_GAP

    def draw_tint(self, tint, width, height):
        """`tint` ((r, g, b), alpha) over the screen: TINT_CENTRE of the
        alpha all over and the full alpha in bands TINT_EDGE of the height
        along the edges, fading inwards."""
        if tint is None or tint[1] <= 0.0:
            return
        colour, alpha = tint
        mesh = self.layer.mesh
        mesh.face([(0.0, 0.0), (width, 0.0), (width, height), (0.0, height)], [colour + (alpha * TINT_CENTRE,)] * 4)
        e = TINT_EDGE * height
        outer, inner = colour + (alpha,), colour + (0.0,)
        for a, b, c, d in (((0.0, 0.0), (width, 0.0), (width - e, e), (e, e)),
                           ((width, 0.0), (width, height), (width - e, height - e), (width - e, e)),
                           ((width, height), (0.0, height), (e, height - e), (width - e, height - e)),
                           ((0.0, height), (0.0, 0.0), (e, e), (e, height - e))):
            mesh.face([a, b, c, d], [outer, outer, inner, inner])

    # ── On foot and in the car ──

    def draw_pinned(self):
        if self.card is None:
            return
        label, title, fraction, progress, due = self.card
        layer, x = self.layer, MARGIN
        fs.scrim(layer.mesh, x - 12.0, 28.0, PINNED_WIDTH + 24.0, 86.0)
        fs.label(layer, label, (x, 41.0), MIST)
        fs.write(layer, fit(title, PINNED_WIDTH, 16, "text_medium"), (x, 65.0), 16, ICE, "text_medium")
        if fraction is not None:
            fs.bar(layer.mesh, x, 84.0, PINNED_WIDTH, fraction, HOLO, 2.5, fs.tint(ICE, 0.25, fs.ON_WORLD))
        if progress:
            fs.write(layer, progress, (x, 102.0), "small", ICE, "num")
        if due:
            fs.write(layer, due, (x + PINNED_WIDTH, 102.0), "small", ICE, "num", "right")

    def draw_compass(self, cx, heading, shown):
        layer, mesh = self.layer, self.layer.mesh
        base, half = COMPASS_TOP, 0.5 * COMPASS_WIDTH
        fs.scrim(mesh, cx - half - 20.0, 12.0, COMPASS_WIDTH + 40.0, LABEL_Y - 2.0, 0.35)
        line = fs.tint(ICE, 0.4, fs.ON_WORLD)
        steps = 24
        for k in range(steps):           # the strip's line, fading towards its ends
            a, b = k / steps, (k + 1) / steps
            fade_a, fade_b = 1.0 - abs(2.0 * a - 1.0) ** 3, 1.0 - abs(2.0 * b - 1.0) ** 3
            x0, x1 = cx - half - 40.0 + (COMPASS_WIDTH + 80.0) * a, cx - half - 40.0 + (COMPASS_WIDTH + 80.0) * b
            mesh.face([(x0, base - 0.5), (x1, base - 0.5), (x1, base + 0.5), (x0, base + 0.5)],
                      [with_alpha(line, fade_a), with_alpha(line, fade_b), with_alpha(line, fade_b),
                       with_alpha(line, fade_a)])
        for rel, size, name in compass_ticks(heading):
            x, _ = compass_x(rel, cx)
            fade = 1.0 - (abs(rel) / COMPASS_SPAN) ** 3
            tick = TICK_LENGTH * (0.8 if name else size)
            mesh.face([(x - 0.5, base - tick), (x + 0.5, base - tick), (x + 0.5, base), (x - 0.5, base)],
                      [with_alpha(line, fade)] * 4)
            if name:
                fs.write(layer, name, (x, base - 18.0), 12, with_alpha(ICE if len(name) == 1 else MIST, fade),
                         "text_medium", "center")
        fs.write(layer, "%d°" % round(heading % 360.0), (cx + 4.0, HEADING_Y), 20, ICE, "display", "center")
        labelled = []
        for x, inside, name, metres, colour in shown:
            x = x - REFERENCE_SIZE[0] * 0.5 + cx
            colour = colour if inside else with_alpha(colour, 0.45)
            marker(mesh, name, x, base, 5.0, colour)
            if inside:
                labelled.append((x, name, metres, colour))
        rows = label_rows([m[0] for m in labelled])
        row = sorted(m for m, r in zip(labelled, rows) if r is not None)
        texts = ["%s %s" % (name.upper(), distance_text(metres)) for _, name, metres, _ in row]
        widths = [fs.text_width(t, "small", "num") + 14.0 for t in texts]
        x = cx - 0.5 * (sum(widths) + 18.0 * max(0, len(widths) - 1))
        for (_, name, _, colour), text, w in zip(row, texts, widths):
            marker(mesh, name, x + 4.0, LABEL_Y, 3.5, colour)
            fs.write(layer, text, (x + 14.0, LABEL_Y), "small", colour, "num")
            x += w + 18.0

    # ── On foot ──

    def draw_reticle(self, cx, cy, frame):
        mesh = self.layer.mesh
        colour = RETICLE.get(frame.status, HOLO)
        g, arm = BRACKET, BRACKET_ARM
        for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            x, y = cx + sx * g, cy + sy * g
            mesh.segment((x, y), (x - sx * arm, y), colour, 1.5)
            mesh.segment((x, y), (x, y - sy * arm), colour, 1.5)
        fs.dot(mesh, cx, cy, 1.8, colour)
        if frame.held and frame.charge > 0.0:
            fs.ring(mesh, cx, cy, RING_RADIUS, frame.charge, AMBER, 2.5, with_alpha(AMBER, 0.25))
        distance = frame.hold_distance if frame.held else None
        if distance is not None and self.hold_distance is not None and abs(distance - self.hold_distance) > 1e-6:
            self.hold_shown = HOLD_SECONDS
        self.hold_distance = distance
        self.hold_shown = max(0.0, self.hold_shown - frame.dt)
        if frame.held:
            text, text_colour = status_text(frame.status, True, frame.charge,
                                            distance if self.hold_shown > 0.0 else None)
            if text:
                fs.write(self.layer, text, (cx, cy + STATUS_BELOW), "small", text_colour, "num", "center")

    def draw_prompt(self, cx, cy, prompt, more, height):
        layer, mesh = self.layer, self.layer.mesh
        y = cy + PROMPT_BELOW
        if prompt is not None:
            w = fs.prompt_width(prompt.key, prompt.verb, prompt.target, prompt.detail, size=16) + 2.0 * PROMPT_PAD
            left = cx - 0.5 * w
            fs.pane(mesh, left, y - 0.5 * PROMPT_HEIGHT, w, PROMPT_HEIGHT, fs.RADIUS_SMALL, fs.GROUND,
                    fs.tint(ICE, 0.12, fs.ON_WORLD))
            x = left + PROMPT_PAD
            x += fs.keycap(layer, prompt.key, x, y) + 12.0
            x += fs.write(layer, prompt.verb, (x, y), 16, ICE, "text_strong") + 9.0
            fs.write(layer, prompt.target, (x, y), 16, MIST)
            if prompt.detail:
                fs.write(layer, prompt.detail, (left + w - PROMPT_PAD, y), "body", prompt.detail_colour, "num",
                         "right")
            if prompt.progress is not None:
                fs.bar(mesh, left + 10.0, y + 0.5 * PROMPT_HEIGHT - 5.0, w - 20.0, prompt.progress, HOLO, 2.0,
                       None)
        if more:
            below = cy + PROMPT_HINTS_BELOW if prompt is not None else height - MARGIN - 7.0
            w = fs.hints_width(more)
            fs.scrim(mesh, cx - 0.5 * w - 10.0, below - 14.0, w + 20.0, 28.0, 0.4, 24.0)
            fs.hints(layer, more, cx, below, align="center")

    def draw_suit(self, frame, height):
        layer, mesh = self.layer, self.layer.mesh
        rx, below, r = SUIT_RING
        cx, cy = rx, height - below
        fs.scrim(mesh, cx - r - 10.0, cy - r - 14.0, 250.0, 2.0 * r + 28.0)
        reserve = max(0.0, min(1.0, frame.reserve))
        colour = MINT if reserve >= 0.995 else HOLO if reserve >= 0.5 else AMBER if reserve >= 0.2 else CORAL
        fs.ring(mesh, cx, cy, r, reserve, colour, 3.0, fs.tint(ICE, 0.2, fs.ON_WORLD))
        fs.write(layer, "%d" % round(100.0 * reserve), (cx, cy), 17, ICE, "num", "center")
        x = cx + r + 22.0
        state = {"cold": "Suit · cold", "hot": "Suit · hot"}.get(frame.suit_state, "Suit")
        fs.label(layer, state, (x, cy - 26.0), CORAL if frame.health < 0.995 else ICE)
        if frame.temperature is not None:
            w = fs.write(layer, "%d °C" % round(frame.temperature), (x, cy + 1.0), 22, ICE, "num")
            if frame.health < 0.995:
                fs.write(layer, "health %d %%" % round(100.0 * frame.health), (x + w + 12.0, cy + 2.0), "small",
                         CORAL, "num")
        if frame.jet < 0.999:
            self.fuel_shown = FUEL_LINGER
        else:
            self.fuel_shown = max(0.0, self.fuel_shown - frame.dt)
        alpha = min(1.0, self.fuel_shown / FUEL_FADE)
        if alpha > 0.0:
            fs.label(layer, "Jet", (x, cy + 26.0), with_alpha(ICE, alpha))
            fill = CORAL if frame.jet < LOW_FUEL else ICE
            fs.bar(mesh, x + 28.0, cy + 25.0, 110.0, frame.jet, with_alpha(fill, alpha), 2.5,
                   with_alpha(fs.tint(ICE, 0.25, fs.ON_WORLD), alpha))

    def draw_readout(self, frame, right, height):
        """The speed, the place and the corner hints, right-aligned at `right`."""
        layer = self.layer
        y = height - 102.0
        fs.scrim(layer.mesh, right - 240.0, y - 22.0, 252.0, 92.0)
        speed = self.speed.update(frame.speed, frame.dt)
        unit = fs.write(layer, "m/s", (right, y + 4.0), "small", ICE, "num", "right")
        fs.write(layer, "%.1f" % speed, (right - unit - 6.0, y), 28, ICE, "num", "right")
        place = frame.body.name + (" · " + frame.hour if frame.hour else "")
        if frame.height is not None and frame.height > AIRBORNE:
            place += " · %s up" % distance_text(frame.height)
        fs.label(layer, place, (right, height - 74.0), ICE, "right")
        if frame.hints:
            fs.hints(layer, frame.hints, right, height - 47.0, align="right")

    # ── In the car ──

    def draw_car(self, frame, height):
        layer, mesh = self.layer, self.layer.mesh
        x, w = MARGIN, CAR_WIDTH
        fs.scrim(mesh, x - 12.0, height - 174.0, w + 24.0, 112.0)
        track = fs.tint(ICE, 0.25, fs.ON_WORLD)
        fs.label(layer, "Hover car", (x, height - 159.0), ICE)
        share = frame.fuel / frame.fuel_capacity if frame.fuel_capacity else 0.0
        fs.label(layer, "Fuel", (x, height - 134.0), ICE)
        if frame.fuel <= 0.0:
            value, colour = "empty", CORAL
        else:
            value = fs.mass(frame.fuel) + (" · ~%s" % fs.distance(frame.fuel_range) if frame.fuel_range else "")
            colour = CORAL if share < LOW_FUEL else ICE
        fs.write(layer, value, (x + w, height - 132.0), "body", colour, "num", "right")
        fs.bar(mesh, x, height - 116.0, w, share, CORAL if share < LOW_FUEL else ICE, 2.5, track)
        fs.label(layer, "Hold", (x, height - 95.0), ICE)
        fs.write(layer, "%s / %s" % (fs.grouped(frame.cargo), fs.mass(frame.cargo_capacity)), (x + w, height - 93.0),
                 "body", ICE, "num", "right")
        fs.bar(mesh, x, height - 77.0, w, frame.cargo / frame.cargo_capacity if frame.cargo_capacity else 0.0, AMBER,
               2.5, track)

    def draw_ladder(self, frame, width, height):
        layer, mesh = self.layer, self.layer.mesh
        if frame.height is None:
            return
        x = width - (REFERENCE_SIZE[0] - LADDER_X)
        middle = 0.5 * height - 0.5 * REFERENCE_SIZE[1]
        top, bottom = LADDER_TOP + middle, LADDER_BOTTOM + middle
        fs.scrim(mesh, x - 46.0, top - 14.0, 150.0, bottom - top + 28.0, 0.35)
        line = fs.tint(ICE, 0.45, fs.ON_WORLD)
        mesh.face([(x - 0.75, top), (x + 0.75, top), (x + 0.75, bottom), (x - 0.75, bottom)], [line] * 4)
        for mark in LADDER:
            y = ladder_y(mark, top, bottom)
            fs.write(layer, "%g" % mark, (x - 8.0, y), "small", ICE, "num", "right")
        y = ladder_y(frame.height, top, bottom)
        mesh.face([(x - 7.0, y - 1.0), (x + 7.0, y - 1.0), (x + 7.0, y + 1.0), (x - 7.0, y + 1.0)], [ICE] * 4)
        if frame.keep is not None:
            y = ladder_y(frame.keep, top, bottom)
            mesh.face([(x + 3.0, y), (x + 11.0, y - 5.0), (x + 11.0, y + 5.0)], [HOLO] * 3)
            fs.write(layer, "hold %s" % distance_text(frame.keep), (x + 16.0, y), "small", HOLO, "num")

    def draw_car_readout(self, frame, width, height):
        layer = self.layer
        right = width - MARGIN
        fs.scrim(layer.mesh, right - 290.0, height - 160.0, 302.0, 128.0)
        speed = self.speed.update(3.6 * frame.speed, frame.dt)
        unit = fs.write(layer, "km/h", (right, height - 121.0), "body", ICE, "num", "right")
        fs.write(layer, "%.0f" % speed, (right - unit - 8.0, height - 128.0), 44, ICE, "num_medium", "right")
        parts = []
        if frame.altitude is not None:
            parts.append("ALT %s" % distance_text(frame.altitude))
        if frame.height is not None:
            parts.append("AGL %s" % distance_text(frame.height))
        if parts:
            fs.write(layer, " · ".join(parts), (right, height - 94.0), "body", ICE, "num", "right")
        fs.label(layer, frame.body.name + (" · " + frame.hour if frame.hour else ""), (right, height - 74.0),
                 ICE, "right")
        if frame.hints:
            fs.hints(layer, frame.hints, right, height - 47.0, align="right")

    # ── In space mode ──

    def draw_transfer(self, readout, width):
        t = readout["transfer"]
        if t is None:
            return
        layer, mesh = self.layer, self.layer.mesh
        w, h = TRANSFER
        left, top = 0.5 * (width - w), 29.0
        fs.pane(mesh, left, top, w, h, fs.RADIUS, with_alpha(fs.SLATE, 0.85), fs.tint(ICE, 0.12, fs.ON_SLATE))
        x = left + 22.0
        fs.label(layer, "Transfer to the moon", (x, top + 24.0), HOLO)
        fs.write(layer, "Hohmann from %s" % distance_text(t["from"]), (left + w - 22.0, top + 24.0), "small", MIST,
                 "num", "right")
        columns = (("Moon leads", "%d°" % round(math.degrees(t["now"])), ICE),
                   ("Burn at", "%d°" % round(math.degrees(t["lead"])), ICE),
                   ("Burn in", wait_text(t["wait"]), AMBER),
                   ("Burn", "%s m/s" % fs.grouped(t["burn"]), ICE))
        step = (w - 44.0) / 4.0
        for k, (name, value, colour) in enumerate(columns):
            fs.label(layer, name, (x + step * k, top + 50.0))
            fs.write(layer, value, (x + step * k, top + 73.0), 19, colour, "num")
        dv = readout["dv"]
        share = t["burn"] / dv if dv > 0.0 else 1.0
        fs.bar(mesh, x, top + 98.0, w - 44.0, share, AMBER if share <= 1.0 else CORAL, 2.5,
               fs.tint(ICE, 0.18, fs.ON_SLATE))
        fs.write(layer, "burn uses %s of %s m/s" % (fs.grouped(t["burn"]), fs.grouped(dv)), (x, top + 115.0),
                 "small", MIST, "num")
        after = dv - t["burn"]
        if after >= 0.0:
            fs.write(layer, "%s m/s left after" % fs.grouped(after), (left + w - 22.0, top + 115.0), "small", MINT,
                     "num", "right")
        else:
            fs.write(layer, "not enough", (left + w - 22.0, top + 115.0), "small", CORAL, "num", "right")

    def draw_space_markers(self, frame, width, height):
        layer, mesh = self.layer, self.layer.mesh
        cx, cy = 0.5 * width, 0.5 * height
        b = BORESIGHT
        for a, c in (((cx - b, cy), (cx - 14.0, cy)), ((cx - 14.0, cy), (cx - 7.0, cy + 8.0)),
                     ((cx - 7.0, cy + 8.0), (cx, cy)), ((cx, cy), (cx + 7.0, cy + 8.0)),
                     ((cx + 7.0, cy + 8.0), (cx + 14.0, cy)), ((cx + 14.0, cy), (cx + b, cy))):
            mesh.segment(a, c, ICE, 1.5)
        readout = frame.space
        prograde = readout["prograde"]
        if prograde is not None:
            for direction, name, colour in ((prograde, "Prograde", MINT), (scale(prograde, -1.0), "Retrograde", CORAL)):
                at = project(direction, frame.view, frame.fov, width, height)
                if at is None or not (0.0 <= at[0] <= width and 0.0 <= at[1] <= height):
                    continue
                x, y = at
                fs.dot(mesh, x, y, 7.0, colour, hollow=True, width=1.5)
                for a, c in (((x, y - 7.0), (x, y - 15.0)), ((x - 7.0, y), (x - 15.0, y)), ((x + 7.0, y), (x + 15.0, y))):
                    mesh.segment(a, c, colour, 1.5)
                if name == "Retrograde":
                    mesh.segment((x - 5.0, y - 5.0), (x + 5.0, y + 5.0), colour, 1.5)
                    mesh.segment((x - 5.0, y + 5.0), (x + 5.0, y - 5.0), colour, 1.5)
                fs.label(layer, name, (x + 22.0, y), colour)
        moon = readout["moon"]
        if moon is not None:
            centre, radius = moon
            to = sub(centre, frame.eye)
            at = project(to, frame.view, frame.fov, width, height)
            if at is not None and 0.0 <= at[0] <= width and 0.0 <= at[1] <= height:
                d = length(to)
                f = 0.5 * height / math.tan(math.radians(frame.fov) * 0.5)
                r = f * radius / max(d, radius)
                x, y = at[0] + r + 12.0, at[1]
                fs.write(layer, "MOON", (x, y - 9.0), "small", ICE, "num_medium")
                fs.write(layer, distance_text(d - radius), (x, y + 8.0), 11, MIST, "num")

    def draw_orbit(self, readout, height):
        layer, mesh = self.layer, self.layer.mesh
        x, w = MARGIN, CAR_WIDTH
        fs.scrim(mesh, x - 12.0, height - 162.0, w + 24.0, 136.0)
        fs.label(layer, "Orbit · " + readout["body"], (x, height - 144.0), ICE)
        rows = (("Periapsis", km_text(readout["periapsis"]), height - 116.0),
                ("Apoapsis", km_text(readout["apoapsis"]), height - 86.0),
                ("Δv left", "%s m/s" % fs.grouped(readout["dv"]), height - 55.0))
        for name, value, y in rows:
            fs.write(layer, name, (x, y), "body", ICE)
            fs.write(layer, value, (x + w, y), 17, ICE, "num", "right")
        full = readout.get("dv_full") or readout["dv"]
        fs.bar(mesh, x, height - 38.0, w, readout["dv"] / full if full > 0.0 else 0.0, ICE, 2.5,
               fs.tint(ICE, 0.18, fs.ON_SLATE))

    def draw_space_readout(self, frame, width, height):
        layer, mesh = self.layer, self.layer.mesh
        readout = frame.space
        right = width - MARGIN
        fs.scrim(mesh, right - 370.0, height - 176.0, 382.0, 146.0)
        speed = self.speed.update(frame.speed, frame.dt)
        unit = fs.write(layer, "m/s", (right, height - 140.0), "body", ICE, "num", "right")
        fs.write(layer, "%.0f" % speed, (right - unit - 8.0, height - 148.0), 44, ICE, "num_medium", "right")
        pills = []
        if readout["hold"]:
            pills.append(("Hold " + readout["hold"], MINT))
        if readout["assist"]:
            pills.append(("Assist", HOLO))
        if readout["warp"] > 1:
            pills.append(("Warp ×%d" % readout["warp"], AMBER))
        x = right
        for text, colour in reversed(pills):
            x -= fs.pill(layer, text, x, height - 106.0, colour, "right", "small", fs.ON_SLATE) + 8.0
        y = height - 78.0
        value = fs.write(layer, "%d %%" % round(100.0 * readout["throttle"]), (right, y), "small", ICE, "num", "right")
        bar_right = right - value - 12.0
        fs.bar(mesh, bar_right - 140.0, y - 1.0, 140.0, readout["throttle"], ICE, 2.0, fs.tint(ICE, 0.18, fs.ON_SLATE))
        fs.label(layer, "Throttle", (bar_right - 152.0, y), MIST, "right")
        if frame.hints:
            fs.hints(layer, frame.hints, right, height - 47.0, align="right")

    # ── The frame ──

    def update(self, frame):
        width, height = self.ui.get_canvas_size(self.canvas)
        if width <= 0 or height <= 0:
            return
        self.layer.begin()
        self.draw_tint(frame.tint, width, height)
        if frame.flash is not None and frame.flash[1] > 0.0:
            self.layer.mesh.face([(0.0, 0.0), (width, 0.0), (width, height), (0.0, height)],
                                 [tuple(frame.flash[0]) + (frame.flash[1],)] * 4)
        cx, cy = 0.5 * width, 0.5 * height
        if frame.mode == "space":
            self.draw_space_markers(frame, width, height)
            self.draw_transfer(frame.space, width)
            self.draw_orbit(frame.space, height)
            self.draw_space_readout(frame, width, height)
        else:
            right, up_view, forward = frame.view
            centre = frame.body.centre()
            up = normalize(sub(frame.eye, centre))
            heading = bearing(up, forward)
            if heading is None:
                heading = bearing(up, up_view) or 0.0
            shown = markers(sub(frame.eye, centre), up, heading, frame.body.radius,
                            [(n, sub(p, centre), c) for n, p, c in frame.targets])
            self.draw_compass(cx, heading, shown)
            self.draw_pinned()
            if frame.mode == "foot":
                self.draw_reticle(cx, cy, frame)
                self.draw_prompt(cx, cy, frame.prompt, frame.prompt_hints, height)
                self.draw_suit(frame, height)
                self.draw_readout(frame, width - MARGIN - MINIMAP_SPACE, height)
            else:
                self.draw_car(frame, height)
                self.draw_ladder(frame, width, height)
                self.draw_car_readout(frame, width, height)
        self.draw_toasts(frame.dt, width)
        self.layer.end()


def marker(mesh, name, x, y, size, colour):
    """A waypoint's triangle pointing down, or another marker's diamond, at (x, y)."""
    if name == "Waypoint":
        mesh.face([(x - size, y - size), (x + size, y - size), (x, y + 0.6 * size)], [colour] * 3)
    else:
        mesh.face([(x - size, y), (x, y - size), (x + size, y), (x, y + size)], [colour] * 4)


def wrap(text, width, size="body", face="text"):
    """`text` broken between words into lines no wider than `width` (a word wider stands alone)."""
    lines, current = [], ""
    for word in text.split():
        candidate = word if not current else current + " " + word
        if current and fs.text_width(candidate, size, face) > width:
            lines.append(current)
            current = word
        else:
            current = candidate
    if current:
        lines.append(current)
    return lines or [""]


def fit(text, width, size="body", face="text"):
    """`text` cut with an ellipsis to fit `width`."""
    if fs.text_width(text, size, face) <= width:
        return text
    while text and fs.text_width(text + "…", size, face) > width:
        text = text[:-1]
    return text.rstrip() + "…"


def wait_text(seconds):
    """A wait as mm:ss under an hour, else ">1 h"; "--:--" when it never comes."""
    if math.isinf(seconds):
        return "--:--"
    return "%02d:%02d" % divmod(int(seconds), 60) if seconds < 3600.0 else ">1 h"

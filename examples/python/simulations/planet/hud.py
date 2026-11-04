"""
The walker's head-up display (walker.py), on a screen canvas in the
interface style of scifi.py: one scifi.Layer of shapes and texts drawn
afresh each frame. Positions are canvas units, x to the right and y down
from the top left; the canvas is laid out for REFERENCE_SIZE divided by
the HUD's scale (`set_scale`).

- **Crosshair** at the centre: four short ticks around a dot, coloured by
  the gravity gun's status (gravity_gun.Gun.status) as CROSSHAIR gives,
  with corner brackets around it while something can be grabbed; while
  holding, a ring of RING_SEGMENTS segments around it, those of the
  charge lit while a punt charges.
- **Status** on a small panel under the crosshair: "TOO HEAVY" when
  the target is, the charge while a punt charges, or for HOLD_SECONDS
  after the hold distance changes, the distance (`status_text`).
- **Hints** along the bottom: what the gun's controls do now, as key
  hints (`hints`, scifi.caps) on a panel; hidden when there are none.
- **Compass** across the top: a strip COMPASS_WIDTH canvas units wide
  showing COMPASS_SPAN degrees either side of the view's heading, a tick
  every 5 degrees, longer every 15, the eight directions named every 45
  degrees above it (N in ACCENT, the four between dimmer), bracketed at
  both ends; the heading in degrees on a panel under its middle. Markers
  (name, position, colour) stand on it at their bearing from the eye,
  along the ground (`bearing`), the MAX_MARKERS nearest; one beyond the
  span is held at the strip's end, dimmed and without a label. The others
  show their name and distance along the ground on a small panel below
  the heading, in the first of LABEL_ROWS rows where they keep LABEL_GAP
  from the labels of nearer markers (`label_rows`).
- **Objective** at the top left: a panel with the header, the goal's
  title, its hint wrapped to the panel, a segmented bar of its progress
  with the progress's text, and rows of (label, value) under a rule, as
  last handed to `objective`.
- **Toasts** below the markers' labels: the last MAX_TOASTS messages
  handed to `toast`, newest on top, each on a panel with a bar of its
  colour at its left; each slides in from the left over TOAST_IN, shows
  for TOAST_SECONDS and fades out over its last TOAST_FADE.
- **Jet fuel** at the bottom left: a panel with the jet fuel's share and
  a segmented bar of it, in WARNING below LOW_FUEL, shown while the fuel
  is not full and for FUEL_LINGER seconds after it fills, fading over the
  last FUEL_FADE.
- **Readout** at the bottom right: a panel with the speed along the
  ground, the body's name and, off the ground, the height of the feet
  above it.
"""

import math

import shoonyakasha as sk
from shoonyakasha.planet.vec import dot, length, local_frame, normalize, scale, sub

import scifi
from scifi import measure, with_alpha

REFERENCE_SIZE = (1600, 900)
TEXT = scifi.WHITE
DIM = scifi.MUTED
ACCENT = scifi.AMBER
WARNING = scifi.RED
GOOD = scifi.GREEN
CROSSHAIR = {"idle": scifi.CYAN, "can_grab": GOOD, "too_heavy": WARNING, "holding": ACCENT}
CROSSHAIR_GAP, CROSSHAIR_TICK = 5.0, 7.0
GRAB_BRACKETS = 15.0             # canvas units from the centre to the brackets' corners while can_grab
RING_RADIUS = 20.0
RING_SEGMENTS = 24
STATUS_BELOW = 38.0              # canvas units under the centre the status is centred
HOLD_SECONDS = 1.2
MARGIN = 24.0                    # canvas units from the screen's edges to the panels
COMPASS_TOP = 30.0               # canvas units from the top to the strip's line
COMPASS_WIDTH = 640.0
COMPASS_SPAN = 60.0              # degrees either side of the heading
LABELS = {0: "N", 45: "NE", 90: "E", 135: "SE", 180: "S", 225: "SW", 270: "W", 315: "NW"}
TICK_LENGTH = 12.0               # canvas units of the longest tick
HEADING_BOX = (56.0, 22.0)       # width, height, under the strip's middle
HEADING_GAP = 9.0                # between the strip and the heading's box
MAX_MARKERS = 8
MARKER_LABEL_TOP = 50.0          # canvas units below the strip the first row of labels is centred
MARKER_ROW = 18.0                # between rows of labels
LABEL_GAP = 120.0                # canvas units between labels in a row, at least
LABEL_ROWS = 2
LABEL_SIZE = 16
CARD_WIDTH = 290
CARD_PAD = 14
HINT_SIZE = 16
BAR_SEGMENTS = 20
FUEL_PANEL = (230.0, 52.0)
FUEL_SEGMENTS = 24
LOW_FUEL = 0.25
FUEL_LINGER, FUEL_FADE = 1.5, 0.5     # s
READOUT_PANEL = (250.0, 66.0)
MAX_TOASTS = 3
TOAST_SECONDS = 5.0
TOAST_FADE = 1.0
TOAST_IN = 0.25                  # s a toast takes to slide in
TOAST_SLIDE = 40.0               # canvas units it slides
TOAST_TOP = 160.0                # canvas units from the top the newest toast is centred
TOAST_ROW = 36.0
TOAST_SIZE = 17
TOAST_HEIGHT = 28.0
HINT_BAR_HEIGHT = 36.0
AIRBORNE = 0.5                   # m the feet must be above the ground for the readout to give the height


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


def status_text(status, held, charge, hold_distance=None):
    """(text, colour) under the crosshair, or (None, None): "TOO HEAVY",
    the charge, or `hold_distance` when given."""
    if status == "too_heavy":
        return "TOO HEAVY", WARNING
    if held and charge > 0.0:
        return "CHARGE %d %%" % round(100.0 * charge), ACCENT
    if held and hold_distance is not None:
        return "HOLD %.1f M" % hold_distance, TEXT
    return None, None


class Hud:
    def __init__(self, engine):
        ui = engine.ui
        self.ui = ui
        self.canvas = ui.create_canvas(reference_size=REFERENCE_SIZE)
        self.layer = scifi.Layer(ui, self.canvas)
        self.toasts = []              # [text, colour, seconds left, seconds shown], newest last
        self.hold_distance = None     # the gun's hold distance last frame
        self.hold_shown = 0.0         # s the hold distance stays shown
        self.fuel_shown = 0.0         # s the jet fuel stays shown
        self.card = None              # (header, title, hint, fraction or None, progress, rows)

    def show(self, on):
        self.ui.set_visible(self.canvas, on)

    def set_scale(self, s):
        """The HUD drawn `s` times its size: the canvas laid out for REFERENCE_SIZE / s."""
        self.ui.set_canvas_scaling(self.canvas, sk.CANVAS_SCALE_WITH_SCREEN,
                                   reference_size=(REFERENCE_SIZE[0] / s, REFERENCE_SIZE[1] / s))

    def toast(self, text, colour=ACCENT):
        self.toasts.append([text, colour, TOAST_SECONDS, 0.0])
        del self.toasts[:-MAX_TOASTS]

    def objective(self, header, title, hint, fraction, progress, rows):
        """The objective panel: `header`, the goal's `title` and `hint`
        (None for none), its progress as a `fraction` (None for no bar) and
        a text, and rows of (label, value)."""
        self.card = (header, title, hint, fraction, progress, rows)

    # ── Parts ──

    def draw_crosshair(self, cx, cy, status, held, charge):
        mesh = self.layer.mesh
        colour = CROSSHAIR[status]
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            scifi.line(mesh, (cx + dx * CROSSHAIR_GAP, cy + dy * CROSSHAIR_GAP),
                       (cx + dx * (CROSSHAIR_GAP + CROSSHAIR_TICK), cy + dy * (CROSSHAIR_GAP + CROSSHAIR_TICK)),
                       colour, 1.5)
        mesh.face([(cx - 1.0, cy - 1.0), (cx + 1.0, cy - 1.0), (cx + 1.0, cy + 1.0), (cx - 1.0, cy + 1.0)],
                  [colour] * 4)
        if status == "can_grab" and not held:
            g = GRAB_BRACKETS
            scifi.brackets(mesh, cx - g, cy - g, 2.0 * g, 2.0 * g, 6.0, colour, 1.4)
        if held:
            step = 2.0 * math.pi / RING_SEGMENTS
            lit = int(round(charge * RING_SEGMENTS))
            for k in range(RING_SEGMENTS):
                on = k < lit
                scifi.arc(mesh, cx, cy, RING_RADIUS, ACCENT if on else with_alpha(colour, 0.45),
                          2.6 if on else 1.4, k * step + 0.03, (k + 1) * step - 0.03, steps=RING_SEGMENTS * 2)

    def draw_compass(self, cx, heading, shown):
        layer, mesh = self.layer, self.layer.mesh
        left, base = cx - 0.5 * COMPASS_WIDTH, COMPASS_TOP
        scifi.panel(mesh, left - 14.0, base - 26.0, COMPASS_WIDTH + 28.0, 32.0, 8.0, scifi.GLASS, None,
                    ("tl", "tr"), scan=False)
        scifi.line(mesh, (left, base), (left + COMPASS_WIDTH, base), scifi.CYAN_SOFT, 1.0)
        for x in (left - 8.0, left + COMPASS_WIDTH + 8.0):
            scifi.line(mesh, (x, base - 20.0), (x, base + 4.0), scifi.CYAN, 1.6)
            s = 1.0 if x > cx else -1.0
            scifi.line(mesh, (x, base - 20.0), (x - s * 5.0, base - 20.0), scifi.CYAN, 1.6)
            scifi.line(mesh, (x, base + 4.0), (x - s * 5.0, base + 4.0), scifi.CYAN, 1.6)
        for rel, size, label in compass_ticks(heading):
            x, _ = compass_x(rel, cx)
            fade = 1.0 - (abs(rel) / COMPASS_SPAN) ** 3
            if label:
                c = ACCENT if label == "N" else (TEXT if len(label) == 1 else DIM)
                layer.text(label, (x, base - 13.0), 17 if len(label) == 1 else 14, with_alpha(c, fade), "title",
                           "center")
                mesh.segment((x, base), (x, base - 4.0), with_alpha(c, fade), 1.6)
            else:
                mesh.segment((x, base), (x, base - TICK_LENGTH * size),
                             with_alpha(scifi.CYAN if size > 0.5 else scifi.CYAN_SOFT, fade), 1.1)
        box_w, box_h = HEADING_BOX
        box_top = base + HEADING_GAP
        scifi.panel(mesh, cx - 0.5 * box_w, box_top, box_w, box_h, 6.0, scifi.GLASS_DEEP, scifi.CYAN,
                    ("bl", "br"), scan=False)
        mesh.face([(cx - 5.0, box_top), (cx, box_top - 6.0), (cx + 5.0, box_top)], [scifi.CYAN] * 3)
        layer.text("%03d" % round(heading % 360.0), (cx, box_top + 0.5 * box_h), 16, TEXT, "mono", "center")
        rows = label_rows([m[0] if m[1] else None for m in shown])
        for k, (x, inside, name, metres, c) in enumerate(shown):
            x = x - REFERENCE_SIZE[0] * 0.5 + cx
            c = c if inside else with_alpha(c, 0.45)
            scifi.diamond(mesh, x, base, 5.0, c)
            if rows[k] is not None:
                y = base + MARKER_LABEL_TOP + MARKER_ROW * rows[k]
                text = "%s  %s" % (name.upper(), distance_text(metres))
                w = measure.width(text, LABEL_SIZE) + 16.0
                scifi.panel(mesh, x - 0.5 * w, y - 9.0, w, 18.0, 5.0, scifi.GLASS, None, ("tl", "br"), scan=False)
                layer.text(text, (x, y), LABEL_SIZE, c, "body", "center", shadow=False)

    def draw_card(self):
        if self.card is None:
            return
        header, title, hint, fraction, progress, rows = self.card
        layer, mesh = self.layer, self.layer.mesh
        x, y, w = MARGIN, MARGIN, CARD_WIDTH
        inner = w - 2 * CARD_PAD
        hint_lines = scifi.wrap(hint, HINT_SIZE, inner) if hint else []
        height = 34 + 26 + 20 * len(hint_lines) + (26 if progress else 0) + 12 + 22 * len(rows) + 10
        scifi.panel(mesh, x, y, w, height, 12.0, scifi.GLASS, scifi.CYAN_SOFT, ("tr", "bl"))
        tab = measure.width(header, 14, "title") + 2 * CARD_PAD
        mesh.polygon([(x, y), (x + tab + 12, y), (x + tab, y + 24), (x, y + 24)], with_alpha(scifi.CYAN, 0.2))
        mesh.segment((x, y + 24), (x + tab, y + 24), scifi.CYAN, 1.2)
        layer.text(header, (x + CARD_PAD, y + 12), 14, scifi.CYAN, "title")
        cy = y + 34 + 12
        layer.text(title, (x + CARD_PAD, cy), 20, TEXT, "title")
        cy += 24
        for text in hint_lines:
            layer.text(text, (x + CARD_PAD, cy), HINT_SIZE, DIM)
            cy += 20
        if progress:
            cy += 6
            text_w = layer.text(progress, (x + w - CARD_PAD, cy), 14, TEXT, "mono", "right")
            if fraction is not None:
                scifi.segments(mesh, x + CARD_PAD, cy - 4, inner - text_w - 12, 8, BAR_SEGMENTS, fraction,
                               scifi.CYAN, scifi.CYAN_FAINT)
            cy += 20
        cy += 4
        scifi.line(mesh, (x + CARD_PAD, cy), (x + w - CARD_PAD, cy), scifi.CYAN_FAINT, 1.0)
        cy += 16
        for label, value in rows:
            layer.text(label.upper(), (x + CARD_PAD, cy), 15, DIM, "title")
            layer.text(value, (x + w - CARD_PAD, cy), 15, ACCENT, "mono", "right")
            cy += 22

    def draw_toasts(self, dt, width):
        layer, mesh = self.layer, self.layer.mesh
        for toast in self.toasts:
            toast[2] -= dt
            toast[3] += dt
        self.toasts = [t for t in self.toasts if t[2] > 0.0]
        for k, (text, colour, left, age) in enumerate(reversed(self.toasts)):
            fade = min(1.0, left / TOAST_FADE, age / TOAST_IN)
            slide = (1.0 - min(1.0, age / TOAST_IN)) ** 2 * TOAST_SLIDE
            y = TOAST_TOP + TOAST_ROW * k
            w = measure.width(text, TOAST_SIZE) + 44.0
            x = 0.5 * (width - w) - slide
            scifi.panel(mesh, x, y - 0.5 * TOAST_HEIGHT, w, TOAST_HEIGHT, 7.0, with_alpha(scifi.GLASS, fade),
                        with_alpha(scifi.CYAN_SOFT, fade), ("tl", "br"), scan=False)
            mesh.face([(x + 6.0, y - 8.0), (x + 9.0, y - 8.0), (x + 9.0, y + 8.0), (x + 6.0, y + 8.0)],
                      [with_alpha(colour, fade)] * 4)
            layer.text(text, (x + 22.0, y), TOAST_SIZE, with_alpha(colour, fade))

    def draw_fuel(self, fuel, dt, height):
        if fuel < 0.999:
            self.fuel_shown = FUEL_LINGER
        else:
            self.fuel_shown = max(0.0, self.fuel_shown - dt)
        alpha = min(1.0, self.fuel_shown / FUEL_FADE)
        if alpha <= 0.0:
            return
        layer, mesh = self.layer, self.layer.mesh
        w, h = FUEL_PANEL
        x, y = MARGIN, height - MARGIN - h
        scifi.panel(mesh, x, y, w, h, 8.0, with_alpha(scifi.GLASS, alpha), with_alpha(scifi.CYAN_SOFT, alpha),
                    ("tr", "bl"), scan=False)
        fill = WARNING if fuel < LOW_FUEL else scifi.CYAN
        layer.text("JET", (x + 12.0, y + 16.0), 14, with_alpha(DIM, alpha), "title")
        layer.text("%d %%" % round(100.0 * fuel), (x + w - 12.0, y + 16.0), 14, with_alpha(fill, alpha), "mono",
                   "right")
        scifi.segments(mesh, x + 12.0, y + 30.0, w - 24.0, 10.0, FUEL_SEGMENTS, fuel, with_alpha(fill, alpha),
                       with_alpha(scifi.CYAN_FAINT, alpha))

    def draw_readout(self, speed, body, above, width, height):
        layer, mesh = self.layer, self.layer.mesh
        w, h = READOUT_PANEL
        x, y = width - MARGIN - w, height - MARGIN - h
        scifi.panel(mesh, x, y, w, h, 10.0, scifi.GLASS, scifi.CYAN_SOFT, ("tl", "br"), scan=False)
        scifi.brackets(mesh, x - 4.0, y - 4.0, w + 8.0, h + 8.0, 8.0, scifi.CYAN, 1.2)
        layer.text("M/S", (x + w - 12.0, y + 23.0), 14, DIM, "title", "right")
        layer.text("%.1f" % speed, (x + w - 46.0, y + 22.0), 28, TEXT, "mono", "right")
        layer.text("SPD", (x + 12.0, y + 23.0), 14, DIM, "title")
        place = body.name.upper()
        layer.text(place, (x + 12.0, y + h - 16.0), 15, scifi.CYAN, "title")
        if above is not None and above > AIRBORNE:
            layer.text("ALT " + distance_text(above), (x + w - 12.0, y + h - 16.0), 14, TEXT, "mono", "right")

    def draw_hints(self, shown, width, height):
        if not shown:
            return
        mesh = self.layer.mesh
        w = scifi.hints_width(shown) + 32.0
        x, y = 0.5 * (width - w), height - MARGIN - HINT_BAR_HEIGHT
        scifi.panel(mesh, x, y, w, HINT_BAR_HEIGHT, 8.0, scifi.GLASS, scifi.CYAN_SOFT, ("tl", "br"), scan=False)
        scifi.caps(self.layer, shown, x + 16.0, y + 0.5 * HINT_BAR_HEIGHT)

    def update(self, view, eye, body, player, gun, targets, speed, pad=False, dt=0.0, height=None):
        """One frame: `view` (right, up, forward) of the camera, `eye` from
        the planet's centre, `body` the world body under it, `player` the
        character, `gun` its gravity gun, `targets` the compass markers
        (name, position from the planet's centre, colour), `speed` along
        the ground in m/s, `pad` whether to name the gamepad's controls,
        `dt` the seconds since the last update, `height` the feet's height
        above the ground in metres, or None."""
        width, height_px = self.ui.get_canvas_size(self.canvas)
        if width <= 0 or height_px <= 0:
            return
        self.layer.begin()
        cx, cy = 0.5 * width, 0.5 * height_px
        right, up_view, forward = view
        centre = body.centre()
        up = normalize(sub(eye, centre))

        status = gun.status if gun is not None else "idle"
        held = gun is not None and gun.held is not None
        charge = gun.charge if held else 0.0
        self.draw_crosshair(cx, cy, status, held, charge)
        distance = gun.hold_distance if held else None
        if distance is not None and self.hold_distance is not None and abs(distance - self.hold_distance) > 1e-6:
            self.hold_shown = HOLD_SECONDS
        self.hold_distance = distance
        self.hold_shown = max(0.0, self.hold_shown - dt)
        text, text_colour = status_text(status, held, charge, distance if self.hold_shown > 0.0 else None)
        if text:
            w = measure.width(text, 15, "mono") + 18.0
            scifi.panel(self.layer.mesh, cx - 0.5 * w, cy + STATUS_BELOW - 10.0, w, 20.0, 5.0, scifi.GLASS, None,
                        ("tl", "br"), scan=False)
            self.layer.text(text, (cx, cy + STATUS_BELOW), 15, text_colour, "mono", "center", shadow=False)
        self.draw_hints(hints(status, held, charge, pad), width, height_px)

        heading = bearing(up, forward)
        if heading is None:
            heading = bearing(up, up_view)
        shown = markers(sub(eye, centre), up, heading, body.radius, [(n, sub(p, centre), c) for n, p, c in targets])
        self.draw_compass(cx, heading, shown)
        self.draw_card()
        self.draw_fuel(player.jet_fuel if player is not None else 1.0, dt, height_px)
        self.draw_readout(speed, body, height, width, height_px)
        self.draw_toasts(dt, width)
        self.layer.end()

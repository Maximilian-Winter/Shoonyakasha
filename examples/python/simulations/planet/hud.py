"""
The walker's head-up display (walker.py), on a screen canvas: lines and
filled shapes in one element (shoonyakasha.toolkit.drawing.Mesh), texts
with a dark shadow behind each, and a bar of key hints
(shoonyakasha.toolkit.overlay.HintBar). Positions are canvas units, x to
the right and y down from the top left; the canvas is laid out for
REFERENCE_SIZE.

- **Crosshair** at the centre: four short ticks around a dot, coloured by
  the gravity gun's status (gravity_gun.Gun.status) as CROSSHAIR gives;
  while holding, a ring around it, and while a punt charges, the ring's
  arc filled clockwise from the top with the charge.
- **Status** under the crosshair: "Too heavy" when the target is, the
  charge while a punt charges, or for HOLD_SECONDS after the hold
  distance changes, the distance (`status_text`).
- **Hints** along the bottom: what the gun's controls do now, as key caps
  (`hints`); hidden when there are none.
- **Compass** across the top: a strip COMPASS_WIDTH canvas units wide
  showing COMPASS_SPAN degrees either side of the view's heading, a tick
  every 5 degrees, longer every 15, the eight directions named every 45
  degrees inside it (N in ACCENT, the four between dimmer); the heading in
  degrees in an outlined box under its middle. Markers (name, position, colour)
  stand under it at their bearing from the eye, along the ground
  (`bearing`), the MAX_MARKERS nearest; one beyond the span is held at the
  strip's end, dimmed and without a label. The others show their name and
  distance along the ground below the heading, in the first of LABEL_ROWS
  rows where they keep LABEL_GAP from the labels of nearer markers
  (`label_rows`).
- **Toasts** below the markers' labels: the last MAX_TOASTS messages handed
  to `toast`, newest on top, each on a dark pill, shown for TOAST_SECONDS
  and fading out over its last TOAST_FADE.
- **Jet fuel** at the bottom left: a bar of the player's jet fuel, in
  WARNING below LOW_FUEL, shown while the fuel is not full and for
  FUEL_LINGER seconds after it fills, fading over the last FUEL_FADE.
- **Readout** at the bottom right: the speed along the ground, the body's
  name and, off the ground, the height of the feet above it.
"""

import math

import shoonyakasha as sk
from shoonyakasha.planet.vec import dot, length, local_frame, normalize, scale, sub
from shoonyakasha.toolkit.drawing import Mesh
from shoonyakasha.toolkit.overlay import HintBar, measure

REFERENCE_SIZE = (1600, 900)
TEXT = (0.92, 0.96, 1.0, 0.95)
DIM = (0.92, 0.96, 1.0, 0.55)
SHADOW = (0.0, 0.0, 0.0, 0.6)
LINE_SHADOW = (0.0, 0.0, 0.0, 0.35)
ACCENT = (1.0, 0.72, 0.3, 1.0)
WARNING = (1.0, 0.32, 0.25, 1.0)
GOOD = (0.55, 1.0, 0.62, 1.0)
PANEL = (0.02, 0.03, 0.05, 0.5)
PILL = (0.02, 0.03, 0.05, 0.62)
CROSSHAIR = {"idle": TEXT, "can_grab": GOOD, "too_heavy": WARNING, "holding": ACCENT}
CROSSHAIR_GAP, CROSSHAIR_TICK = 5.0, 7.0
RING_RADIUS = 16.0
STATUS_BELOW = 34.0              # canvas units under the centre the status is centred
HOLD_SECONDS = 1.2
COMPASS_TOP = 14.0
COMPASS_WIDTH = 640.0
COMPASS_SPAN = 60.0              # degrees either side of the heading
COMPASS_HEIGHT = 28.0
LABELS = {0: "N", 45: "NE", 90: "E", 135: "SE", 180: "S", 225: "SW", 270: "W", 315: "NW"}
TICK_LENGTH = 12.0               # canvas units of the longest tick
HEADING_BOX = (48.0, 22.0)       # width, height, under the strip's middle
HEADING_GAP = 8.0                # between the strip and the heading's box
MAX_MARKERS = 8
MARKER_LABEL_TOP = 46.0          # canvas units below the strip the first row of labels is centred
MARKER_ROW = 18.0                # between rows of labels
LABEL_GAP = 120.0                # canvas units between labels in a row, at least
LABEL_ROWS = 2
LABEL_SIZE = 13
FUEL_BAR = (64.0, 40.0, 180.0, 6.0)   # left, distance up from the bottom, width, height
LOW_FUEL = 0.25
FUEL_LINGER, FUEL_FADE = 1.5, 0.5     # s
LINE = 1.6
MAX_TOASTS = 3
TOAST_SECONDS = 5.0
TOAST_FADE = 1.0
TOAST_TOP = 150.0               # canvas units from the top the newest toast is centred
TOAST_ROW = 34.0
TOAST_SIZE = 16
TOAST_PAD = (14.0, 26.0)        # the pill's width beyond the text, its height
AIRBORNE = 0.5                  # m the feet must be above the ground for the readout to give the height


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
    """The key hints (caps, label) for what the gun's controls do now, for
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
    """(text, colour) under the crosshair, or (None, None): "Too heavy",
    the charge, or `hold_distance` when given."""
    if status == "too_heavy":
        return "Too heavy", WARNING
    if held and charge > 0.0:
        return "%d %%" % round(100.0 * charge), ACCENT
    if held and hold_distance is not None:
        return "Hold %.1f m" % hold_distance, TEXT
    return None, None


class Hud:
    def __init__(self, engine):
        ui = engine.ui
        self.ui = ui
        self.canvas = ui.create_canvas(reference_size=REFERENCE_SIZE)
        self.shapes = ui.create_element(self.canvas)
        ui.set_rect(self.shapes, anchor_min=(0, 0), anchor_max=(1, 1), pivot=(0, 0), position=(0, 0), size=(0, 0))
        ui.set_raycast_target(self.shapes, False)
        self.texts = {}
        self.status_text = self._text(14, TEXT)
        self.heading_text = self._text(14, TEXT)
        self.tick_labels = [self._text(14, TEXT) for _ in range(6)]
        self.marker_labels = [self._text(LABEL_SIZE, TEXT) for _ in range(MAX_MARKERS)]
        self.fuel_text = self._text(12, DIM, sk.TEXT_ALIGN_LEFT)
        self.speed_text = self._text(26, TEXT, sk.TEXT_ALIGN_RIGHT)
        self.unit_text = self._text(13, DIM, sk.TEXT_ALIGN_LEFT)
        self.place_text = self._text(13, DIM, sk.TEXT_ALIGN_RIGHT)
        self.toast_texts = [self._text(TOAST_SIZE, TEXT) for _ in range(MAX_TOASTS)]
        self.toasts = []              # [text, colour, seconds left], newest last
        self.bar = HintBar(ui, self.canvas)
        self.bar.show(False)
        self.hold_distance = None     # the gun's hold distance last frame
        self.hold_shown = 0.0         # s the hold distance stays shown
        self.fuel_shown = 0.0         # s the jet fuel stays shown

    def _text(self, size, colour, align=sk.TEXT_ALIGN_CENTER):
        """A text and its shadow (a dark copy behind it, a unit down and right)."""
        pair = []
        for c, offset in ((SHADOW, (1.0, 1.2)), (colour, (0.0, 0.0))):
            text = self.ui.create_text(self.canvas, "", size=size, color=c)
            self.ui.set_text_align(text, align, sk.TEXT_ALIGN_MIDDLE)
            self.ui.set_raycast_target(text, False)
            pair.append((text, offset))
        return pair

    def _place(self, pair, text, centre, size=(160, 22), colour=None):
        """`text` in a box of `size` centred at `centre`; None or "" hides
        it. `colour` sets the text's colour, and the shadow's alpha with it."""
        text = text or None
        for k, (element, offset) in enumerate(pair):
            old = self.texts.get(element)
            shade = colour if k == 1 else (None if colour is None else SHADOW[:3] + (SHADOW[3] * colour[3],))
            new = (text, round(centre[0] + offset[0], 1), round(centre[1] + offset[1], 1), size, shade)
            if new == old:
                continue
            if text is None:
                if old is None or old[0] is not None:
                    self.ui.set_visible(element, False)
                self.texts[element] = (None, None, None, None, None)     # placed again when next shown
                continue
            else:
                if old is None or old[0] is None:
                    self.ui.set_visible(element, True)
                if old is None or old[0] != text:
                    self.ui.set_text(element, text)
                if old is None or old[1:4] != new[1:4]:
                    self.ui.set_rect(element, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0.5, 0.5),
                                     position=new[1:3], size=size)
                if shade is not None and (old is None or old[4] != shade):
                    self.ui.set_color(element, shade)
            self.texts[element] = new

    def show(self, on):
        self.ui.set_visible(self.canvas, on)

    @staticmethod
    def _rect(mesh, left, top, width, height, colour):
        mesh.face([(left, top), (left + width, top), (left + width, top + height), (left, top + height)],
                  [colour] * 4)

    @staticmethod
    def _line(mesh, a, b, colour, width=LINE):
        mesh.segment(a, b, LINE_SHADOW, width + 2.0)
        mesh.segment(a, b, colour, width)

    def toast(self, text, colour=ACCENT):
        self.toasts.append([text, colour, TOAST_SECONDS])
        del self.toasts[:-MAX_TOASTS]

    def update_toasts(self, mesh, dt, width):
        for toast in self.toasts:
            toast[2] -= dt
        self.toasts = [t for t in self.toasts if t[2] > 0.0]
        shown = list(reversed(self.toasts))
        for k, pair in enumerate(self.toast_texts):
            if k < len(shown):
                text, colour, left = shown[k]
                fade = min(1.0, left / TOAST_FADE)
                y = TOAST_TOP + TOAST_ROW * k
                w = measure.width(text, TOAST_SIZE) + 2.0 * TOAST_PAD[0]
                self._rect(mesh, 0.5 * (width - w), y - 0.5 * TOAST_PAD[1], w, TOAST_PAD[1],
                           PILL[:3] + (PILL[3] * fade,))
                self._rect(mesh, 0.5 * (width - w), y - 0.5 * TOAST_PAD[1], 3.0, TOAST_PAD[1],
                           colour[:3] + (colour[3] * fade,))
                self._place(pair, text, (0.5 * width, y), (w, 24), (colour[0], colour[1], colour[2], round(fade, 2)))
            else:
                self._place(pair, None, (0.0, 0.0))

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
        mesh = Mesh()
        cx, cy = 0.5 * width, 0.5 * height_px
        right, up_view, forward = view
        centre = body.centre()
        up = normalize(sub(eye, centre))

        # Crosshair, ring, status and hints.
        status = gun.status if gun is not None else "idle"
        held = gun is not None and gun.held is not None
        colour = CROSSHAIR[status]
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            self._line(mesh, (cx + dx * CROSSHAIR_GAP, cy + dy * CROSSHAIR_GAP),
                       (cx + dx * (CROSSHAIR_GAP + CROSSHAIR_TICK), cy + dy * (CROSSHAIR_GAP + CROSSHAIR_TICK)), colour)
        self._rect(mesh, cx - 1.0, cy - 1.0, 2.0, 2.0, colour)
        charge = gun.charge if held else 0.0
        if held:
            steps = 40
            for k in range(steps):
                a0, a1 = 2.0 * math.pi * k / steps, 2.0 * math.pi * (k + 1) / steps
                lit = (k + 1) / steps <= charge
                self._line(mesh, (cx + RING_RADIUS * math.sin(a0), cy - RING_RADIUS * math.cos(a0)),
                           (cx + RING_RADIUS * math.sin(a1), cy - RING_RADIUS * math.cos(a1)),
                           WARNING if lit else colour, 2.4 if lit else LINE)
        distance = gun.hold_distance if held else None
        if distance is not None and self.hold_distance is not None and abs(distance - self.hold_distance) > 1e-6:
            self.hold_shown = HOLD_SECONDS
        self.hold_distance = distance
        self.hold_shown = max(0.0, self.hold_shown - dt)
        text, text_colour = status_text(status, held, charge, distance if self.hold_shown > 0.0 else None)
        self._place(self.status_text, text, (cx, cy + STATUS_BELOW), (200, 20), text_colour)
        shown_hints = hints(status, held, charge, pad)
        self.bar.show(bool(shown_hints))
        if shown_hints:
            self.bar.update(shown_hints)

        # Compass.
        heading = bearing(up, forward)
        if heading is None:
            heading = bearing(up, up_view)
        left = cx - 0.5 * COMPASS_WIDTH
        top, bottom = COMPASS_TOP, COMPASS_TOP + COMPASS_HEIGHT
        self._rect(mesh, left - 8.0, top, COMPASS_WIDTH + 16.0, COMPASS_HEIGHT, PANEL)
        labels = []
        for rel, size, label in compass_ticks(heading):
            x, _ = compass_x(rel, cx)
            if label:
                labels.append((label, x))
            else:
                self._line(mesh, (x, bottom - 3.0), (x, bottom - 3.0 - TICK_LENGTH * size), TEXT if size > 0.5 else DIM,
                           1.2)
        for k, pair in enumerate(self.tick_labels):
            if k < len(labels):
                name, x = labels[k]
                c = ACCENT if name == "N" else (TEXT if len(name) == 1 else DIM)
                self._place(pair, name, (x, top + 0.5 * COMPASS_HEIGHT), (40, 18), c)
            else:
                self._place(pair, None, (0.0, 0.0))
        box_w, box_h = HEADING_BOX
        box_top = bottom + HEADING_GAP
        self._rect(mesh, cx - 0.5 * box_w, box_top, box_w, box_h, PILL)
        corners = [(cx - 0.5 * box_w, box_top), (cx + 0.5 * box_w, box_top), (cx + 0.5 * box_w, box_top + box_h),
                   (cx - 0.5 * box_w, box_top + box_h)]
        for a, b in zip(corners, corners[1:] + corners[:1]):
            mesh.segment(a, b, DIM, 1.0)
        self._line(mesh, (cx - 5.0, box_top - 1.0), (cx, box_top - 6.0), TEXT, 1.4)
        self._line(mesh, (cx + 5.0, box_top - 1.0), (cx, box_top - 6.0), TEXT, 1.4)
        self._place(self.heading_text, "%03d" % round(heading % 360.0), (cx, box_top + 0.5 * box_h), (box_w, box_h))
        shown = markers(sub(eye, centre), up, heading,
                        body.radius, [(n, sub(p, centre), c) for n, p, c in targets])
        rows = label_rows([m[0] if m[1] else None for m in shown])
        for k in range(MAX_MARKERS):
            labelled = k < len(shown) and rows[k] is not None
            if k < len(shown):
                x, inside, name, metres, c = shown[k]
                x = x - REFERENCE_SIZE[0] * 0.5 + cx
                c = c if inside else (c[0], c[1], c[2], 0.5)
                mesh.face([(x, bottom - 7.0), (x + 5.0, bottom), (x, bottom + 7.0), (x - 5.0, bottom)], [c] * 4)
            if labelled:
                y = bottom + MARKER_LABEL_TOP + MARKER_ROW * rows[k]
                self._place(self.marker_labels[k], "%s  %s" % (name, distance_text(metres)), (x, y),
                            (LABEL_GAP + 40, 16), c)
            else:
                self._place(self.marker_labels[k], None, (0.0, 0.0))

        # Jet fuel, while not full and a moment after.
        fuel = player.jet_fuel if player is not None else 1.0
        if fuel < 0.999:
            self.fuel_shown = FUEL_LINGER
        else:
            self.fuel_shown = max(0.0, self.fuel_shown - dt)
        alpha = min(1.0, self.fuel_shown / FUEL_FADE)
        bx, up_from_bottom, bw, bh = FUEL_BAR
        by = height_px - up_from_bottom
        if alpha > 0.0:
            fill = WARNING if fuel < LOW_FUEL else ACCENT
            self._rect(mesh, bx - 44.0, by - 9.0, bw + 52.0, bh + 18.0, PANEL[:3] + (PANEL[3] * alpha,))
            self._rect(mesh, bx, by, bw, bh, (1.0, 1.0, 1.0, 0.12 * alpha))
            self._rect(mesh, bx, by, bw * fuel, bh, fill[:3] + (alpha,))
            self._place(self.fuel_text, "JET", (bx - 22.0, by + 0.5 * bh), (40, 16), DIM[:3] + (DIM[3] * alpha,))
        else:
            self._place(self.fuel_text, None, (0.0, 0.0))

        # Readout.
        rx = width - 36.0
        self._place(self.speed_text, "%.1f" % speed, (rx - 118.0, height_px - 64.0), (160, 34))
        self._place(self.unit_text, "m/s", (rx - 12.0, height_px - 60.0), (40, 18))
        place = body.name.upper()
        if height is not None and height > AIRBORNE:
            place = "%s  ·  %s above the ground" % (place, distance_text(height))
        self._place(self.place_text, place, (rx - 150.0, height_px - 36.0), (300, 18))

        self.update_toasts(mesh, dt, width)
        self.ui.set_geometry(self.shapes, mesh.positions, mesh.indices, mesh.colors)

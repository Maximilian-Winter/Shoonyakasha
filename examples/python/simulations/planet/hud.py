"""
The walker's head-up display (walker.py), on a screen canvas: lines and
filled shapes in one element (shoonyakasha.toolkit.drawing.Mesh) and
texts with a dark shadow behind each. Positions are canvas units, x to the
right and y down from the top left; the canvas is laid out for
REFERENCE_SIZE.

- **Crosshair** at the centre: four short ticks around a dot, coloured by
  the gravity gun's status (gravity_gun.Gun.status) as CROSSHAIR gives;
  while holding, a ring around it, and while a punt
  charges, the ring's arc filled clockwise from the top with the charge.
- **Prompt** under the crosshair: what the gun's controls do now
  (`prompt`).
- **Compass** across the top: COMPASS_WIDTH canvas units showing
  COMPASS_SPAN degrees either side of the view's heading, a tick every 5
  degrees, longer every 15, labelled every 45 (N, NE, E, ...); the heading
  in degrees under its middle. Markers (name, position, colour) stand on
  it at their bearing from the eye, along the ground (`bearing`), the
  MAX_MARKERS nearest; one beyond the span is held at the strip's end,
  dimmed and without a label. The others show their name and distance
  along the ground below the strip, in the first of LABEL_ROWS rows where
  they keep LABEL_GAP from the labels of nearer markers (`label_rows`).
- **Jet fuel** at the bottom left: a bar of the player's jet fuel, in
  WARNING below LOW_FUEL.
- **Readout** at the bottom right: the speed along the ground, the
  height above the body's radius, and the body's name.
"""

import math

import shoonyakasha as sk
from shoonyakasha.planet.vec import dot, length, local_frame, normalize, scale, sub
from shoonyakasha.toolkit.drawing import Mesh

REFERENCE_SIZE = (1600, 900)
TEXT = (0.92, 0.96, 1.0, 0.95)
DIM = (0.92, 0.96, 1.0, 0.45)
SHADOW = (0.0, 0.0, 0.0, 0.55)
LINE_SHADOW = (0.0, 0.0, 0.0, 0.35)
ACCENT = (1.0, 0.72, 0.3, 1.0)
WARNING = (1.0, 0.32, 0.25, 1.0)
GOOD = (0.55, 1.0, 0.62, 1.0)
PANEL = (0.02, 0.03, 0.05, 0.45)
CROSSHAIR = {"idle": TEXT, "can_grab": GOOD, "too_heavy": WARNING, "holding": ACCENT}
CROSSHAIR_GAP, CROSSHAIR_TICK = 5.0, 7.0
RING_RADIUS = 16.0
COMPASS_TOP = 22.0
COMPASS_WIDTH = 640.0
COMPASS_SPAN = 60.0              # degrees either side of the heading
COMPASS_HEIGHT = 18.0
LABELS = {0: "N", 45: "NE", 90: "E", 135: "SE", 180: "S", 225: "SW", 270: "W", 315: "NW"}
MAX_MARKERS = 8
MARKER_LABEL_TOP = 40.0          # canvas units below the strip the first row of labels is centred
MARKER_ROW = 30.0                # between rows of labels
LABEL_GAP = 90.0                 # canvas units between labels in a row, at least
LABEL_ROWS = 2
FUEL_BAR = (36.0, 60.0, 220.0, 8.0)   # left, distance up from the bottom, width, height
LOW_FUEL = 0.25
LINE = 1.6


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


def prompt(status, held, charge, pad=False):
    """What the gun's controls do now, for the gun's `status`."""
    grab, punt = ("LT", "RT") if pad else ("RMB", "LMB")
    if held:
        if charge > 0.0:
            return "%s  release to throw  %d %%" % (punt, round(100.0 * charge))
        text = "%s  drop     %s  throw (hold to charge)" % (grab, punt)
        return text if pad else text + "     R + mouse  turn"
    if status == "can_grab":
        return "%s  grab" % grab
    if status == "too_heavy":
        return "too heavy"
    return ""


class Hud:
    def __init__(self, engine):
        ui = engine.ui
        self.ui = ui
        self.canvas = ui.create_canvas(reference_size=REFERENCE_SIZE)
        self.shapes = ui.create_element(self.canvas)
        ui.set_rect(self.shapes, anchor_min=(0, 0), anchor_max=(1, 1), pivot=(0, 0), position=(0, 0), size=(0, 0))
        ui.set_raycast_target(self.shapes, False)
        self.texts = {}
        self.prompt_text = self._text(15, TEXT)
        self.heading_text = self._text(15, TEXT)
        self.tick_labels = [self._text(13, TEXT) for _ in range(6)]
        self.marker_names = [self._text(12, TEXT) for _ in range(MAX_MARKERS)]
        self.marker_distances = [self._text(11, DIM) for _ in range(MAX_MARKERS)]
        self.fuel_text = self._text(12, TEXT, sk.TEXT_ALIGN_LEFT)
        self.speed_text = self._text(20, TEXT, sk.TEXT_ALIGN_RIGHT)
        self.height_text = self._text(14, TEXT, sk.TEXT_ALIGN_RIGHT)
        self.body_text = self._text(12, DIM, sk.TEXT_ALIGN_RIGHT)

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
        """`text` in a box of `size` centred at `centre`; None or "" hides it."""
        text = text or None
        for k, (element, offset) in enumerate(pair):
            old = self.texts.get(element)
            new = (text, round(centre[0] + offset[0], 1), round(centre[1] + offset[1], 1), size,
                   colour if k == 1 else None)
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
                if k == 1 and colour is not None and (old is None or old[4] != colour):
                    self.ui.set_color(element, colour)
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

    def update(self, view, eye, body, player, gun, targets, speed, pad=False):
        """One frame: `view` (right, up, forward) of the camera, `eye` from
        the planet's centre, `body` the world body under it, `player` the
        character, `gun` its gravity gun, `targets` the compass markers
        (name, position from the planet's centre, colour), `speed` along
        the ground in m/s, `pad` whether to name the gamepad's controls."""
        width, height = self.ui.get_canvas_size(self.canvas)
        if width <= 0 or height <= 0:
            return
        mesh = Mesh()
        cx, cy = 0.5 * width, 0.5 * height
        right, up_view, forward = view
        centre = body.centre()
        up = normalize(sub(eye, centre))

        # Crosshair, ring and prompt.
        status = gun.status if gun is not None else "idle"
        colour = CROSSHAIR[status]
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            self._line(mesh, (cx + dx * CROSSHAIR_GAP, cy + dy * CROSSHAIR_GAP),
                       (cx + dx * (CROSSHAIR_GAP + CROSSHAIR_TICK), cy + dy * (CROSSHAIR_GAP + CROSSHAIR_TICK)), colour)
        self._rect(mesh, cx - 1.0, cy - 1.0, 2.0, 2.0, colour)
        charge = gun.charge if gun is not None and gun.held is not None else 0.0
        if gun is not None and gun.held is not None:
            steps = 40
            for k in range(steps):
                a0, a1 = 2.0 * math.pi * k / steps, 2.0 * math.pi * (k + 1) / steps
                lit = (k + 1) / steps <= charge
                self._line(mesh, (cx + RING_RADIUS * math.sin(a0), cy - RING_RADIUS * math.cos(a0)),
                           (cx + RING_RADIUS * math.sin(a1), cy - RING_RADIUS * math.cos(a1)),
                           WARNING if lit else colour, 2.4 if lit else LINE)
        self._place(self.prompt_text, prompt(status, gun is not None and gun.held is not None, charge, pad),
                    (cx, cy + 48.0), (600, 22), WARNING if status == "too_heavy" else TEXT)

        # Compass.
        heading = bearing(up, forward)
        if heading is None:
            heading = bearing(up, up_view)
        left = cx - 0.5 * COMPASS_WIDTH
        self._rect(mesh, left - 8.0, COMPASS_TOP - 6.0, COMPASS_WIDTH + 16.0, COMPASS_HEIGHT + 12.0, PANEL)
        labels = []
        for rel, size, label in compass_ticks(heading):
            x, _ = compass_x(rel, cx)
            bottom = COMPASS_TOP + COMPASS_HEIGHT
            self._line(mesh, (x, bottom), (x, bottom - COMPASS_HEIGHT * size), TEXT if label else DIM, 1.2)
            if label:
                labels.append((label, x))
        for k, pair in enumerate(self.tick_labels):
            if k < len(labels):
                self._place(pair, labels[k][0], (labels[k][1], COMPASS_TOP - 14.0), (40, 18),
                            ACCENT if labels[k][0] == "N" else TEXT)
            else:
                self._place(pair, None, (0.0, 0.0))
        bottom = COMPASS_TOP + COMPASS_HEIGHT
        self._line(mesh, (cx - 6.0, bottom + 10.0), (cx, bottom + 4.0), TEXT)
        self._line(mesh, (cx + 6.0, bottom + 10.0), (cx, bottom + 4.0), TEXT)
        self._place(self.heading_text, "%03d" % round(heading % 360.0), (cx, COMPASS_TOP + COMPASS_HEIGHT + 22.0),
                    (60, 20))
        shown = markers(sub(eye, centre), up, heading,
                        body.radius, [(n, sub(p, centre), c) for n, p, c in targets])
        rows = label_rows([m[0] if m[1] else None for m in shown])
        for k in range(MAX_MARKERS):
            labelled = k < len(shown) and rows[k] is not None
            if k < len(shown):
                x, inside, name, distance, c = shown[k]
                x = x - REFERENCE_SIZE[0] * 0.5 + cx
                c = c if inside else (c[0], c[1], c[2], 0.5)
                y = COMPASS_TOP + COMPASS_HEIGHT
                mesh.face([(x, y), (x - 6.0, y + 9.0), (x + 6.0, y + 9.0)], [c] * 3)
            if labelled:
                y = COMPASS_TOP + COMPASS_HEIGHT + MARKER_LABEL_TOP + MARKER_ROW * rows[k]
                self._place(self.marker_names[k], name, (x, y), (140, 16), c)
                self._place(self.marker_distances[k], distance_text(distance), (x, y + 13.0), (140, 14))
            else:
                self._place(self.marker_names[k], None, (0.0, 0.0))
                self._place(self.marker_distances[k], None, (0.0, 0.0))

        # Jet fuel.
        fuel = player.jet_fuel if player is not None else 0.0
        bx, up_from_bottom, bw, bh = FUEL_BAR
        by = height - up_from_bottom
        self._rect(mesh, bx - 4.0, by - 4.0, bw + 8.0, bh + 8.0, PANEL)
        self._rect(mesh, bx, by, bw * fuel, bh, WARNING if fuel < LOW_FUEL else ACCENT)
        self._place(self.fuel_text, "JET  %d %%" % round(100.0 * fuel), (bx + 0.5 * 160.0, by - 14.0), (160, 18))

        # Readout.
        altitude = length(sub(eye, centre)) - body.radius
        rx = width - 36.0
        self._place(self.speed_text, "%.1f m/s" % speed, (rx - 80.0, height - 84.0), (160, 26))
        self._place(self.height_text, "%s above datum" % distance_text(altitude), (rx - 100.0, height - 60.0),
                    (200, 20))
        self._place(self.body_text, body.name.upper(), (rx - 100.0, height - 40.0), (200, 16))

        self.ui.set_geometry(self.shapes, mesh.positions, mesh.indices, mesh.colors)

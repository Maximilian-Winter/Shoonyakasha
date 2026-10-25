"""
The ship's instruments in the cockpit view.

The head-up display: symbols on a screen canvas, drawn as anti-aliased
triangles (shoonyakasha.toolkit.drawing.Mesh) in green over a faint dark
stroke, within a field around the nose. Each symbol is placed by the
direction it marks, so it stays on it as the ship turns:
  - the waterline symbol at the nose;
  - the flight path marker where the ship is moving (held at the field's
    edge, dimmed, when that is outside it);
  - the horizon and a pitch ladder every 5 degrees, numbered every 10,
    dashed below the horizon, turned with the ship's roll;
  - the heading tape across the top, the heading boxed above it;
  - the speed boxed on the left, the throttle and the modes under it;
  - the height above the ground boxed on the right, the climb and the
    height above the datum under it;
  - the contact state (LANDED, CRASHED) under the nose.

The multi-function displays: world canvases on the cabin's two displays,
FLIGHT on the left and ORBIT on the right, each a title over rows of
labels and values.

`Instruments.update(view, readings)` draws a frame of both.
"""

import math

import shoonyakasha as sk
from shoonyakasha.toolkit.drawing import Mesh

HUD = (0.55, 1.0, 0.62, 0.95)
HUD_DIM = (0.55, 1.0, 0.62, 0.45)
HUD_SHADOW = (0.0, 0.04, 0.02, 0.4)
WARNING = (1.0, 0.72, 0.3, 1.0)
LINE = 1.6                       # canvas units: the symbols' lines
SHADOW_LINE = LINE + 2.4
FIELD = (15.0, 10.0)             # degrees the field reaches from the nose, across and up
LADDER_STEP = 5                  # degrees between the ladder's rungs
RUNG_GAP, RUNG_LENGTH, RUNG_TICK = 24.0, 40.0, 7.0
HORIZON_GAP, HORIZON_LENGTH = 26.0, 400.0
BOX_WIDTH = 80.0                 # the speed and height boxes, reaching 18 outside the field
HEADING_SCALE = 5.0              # canvas units per degree on the heading tape
HEADING_SPAN = 22                # degrees either side of the heading the tape shows
LABELS = 16                      # pooled ladder and tape numbers

MFD_PIXELS = (512, 500)
MFD_EMISSION = 2.5
MFD_BACKGROUND = (0.008, 0.016, 0.014, 1.0)
MFD_TITLE = (0.5, 0.92, 1.0, 1.0)
MFD_LABEL = (0.36, 0.7, 0.48, 1.0)
MFD_VALUE = (0.64, 1.0, 0.7, 1.0)
MFD_RULE = (0.36, 0.7, 0.48, 0.55)
MFD_ROWS = 7


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _combine(a, sa, b, sb):
    return (a[0] * sa + b[0] * sb, a[1] * sa + b[1] * sb, a[2] * sa + b[2] * sb)


def _normalize(a):
    n = math.sqrt(_dot(a, a))
    return (a[0] / n, a[1] / n, a[2] / n)


def clip(a, b, box):
    """The part of the segment a-b inside `box` (x0, y0, x1, y1), or None
    (Liang-Barsky)."""
    t0, t1 = 0.0, 1.0
    dx, dy = b[0] - a[0], b[1] - a[1]
    for p, q in ((-dx, a[0] - box[0]), (dx, box[2] - a[0]), (-dy, a[1] - box[1]), (dy, box[3] - a[1])):
        if p == 0.0:
            if q < 0.0:
                return None
            continue
        t = q / p
        if p < 0.0:
            t0 = max(t0, t)
        else:
            t1 = min(t1, t)
        if t0 > t1:
            return None
    return (a[0] + dx * t0, a[1] + dy * t0), (a[0] + dx * t1, a[1] + dy * t1)


class HeadUpDisplay:
    def __init__(self, engine, fov, reference_size=(1600, 900)):
        ui = engine.ui
        self.ui = ui
        self.fov = fov
        self.canvas = ui.create_canvas(reference_size=reference_size)
        self.symbols = ui.create_element(self.canvas)
        ui.set_rect(self.symbols, anchor_min=(0, 0), anchor_max=(1, 1), pivot=(0, 0), position=(0, 0), size=(0, 0))
        ui.set_raycast_target(self.symbols, False)
        self.labels = [self._text(14, HUD) for _ in range(LABELS)]
        self.heading = self._text(17, HUD)
        self.speed = self._text(20, HUD, sk.TEXT_ALIGN_RIGHT)
        self.speed_unit = self._text(13, HUD, sk.TEXT_ALIGN_LEFT)
        self.height = self._text(20, HUD, sk.TEXT_ALIGN_RIGHT)
        self.height_unit = self._text(13, HUD, sk.TEXT_ALIGN_LEFT)
        self.left_lines = self._text(14, HUD, sk.TEXT_ALIGN_LEFT, sk.TEXT_ALIGN_TOP)
        self.right_lines = self._text(14, HUD, sk.TEXT_ALIGN_RIGHT, sk.TEXT_ALIGN_TOP)
        self.contact = self._text(18, WARNING)
        self.texts = {}

    def _text(self, size, colour, align=sk.TEXT_ALIGN_CENTER, vertical=sk.TEXT_ALIGN_MIDDLE):
        """A text and its shadow (a dark copy behind it, a unit down and right)."""
        ui = self.ui
        pair = []
        for c, offset in (((0.0, 0.0, 0.0, 0.55), (1.0, 1.2)), (colour, (0.0, 0.0))):
            text = ui.create_text(self.canvas, "", size=size, color=c)
            ui.set_text_align(text, align, vertical)
            ui.set_raycast_target(text, False)
            pair.append((text, offset))
        return pair

    def _place(self, pair, text, centre, size=(90, 24), colour=None):
        """Show `text` in a box of `size` centred at `centre`; None hides it."""
        for k, (element, offset) in enumerate(pair):
            old = self.texts.get(element)
            new = (text, round(centre[0] + offset[0], 1), round(centre[1] + offset[1], 1), size,
                   colour if k == 1 else None)
            if new == old:
                continue
            if text is None:
                if old is None or old[0] is not None:
                    self.ui.set_visible(element, False)
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

    def update(self, view, r):
        """`view`: (right, up, forward) of the camera, the ship's nose, the
        planet's up at the ship, and the direction it moves in (None when
        it is not moving); `r`: the readings (Instruments.update)."""
        right, up, forward, nose, radial, moving = view
        width, height = self.ui.get_canvas_size(self.canvas)
        if width <= 0 or height <= 0:
            return
        scale = 0.5 * height / math.tan(math.radians(self.fov) * 0.5)

        def project(d):
            z = _dot(d, forward)
            if z <= 1e-6:
                return None
            return (0.5 * width + scale * _dot(d, right) / z, 0.5 * height - scale * _dot(d, up) / z)

        centre = project(nose)
        if centre is None:
            return
        half = (scale * math.tan(math.radians(FIELD[0])), scale * math.tan(math.radians(FIELD[1])))
        box = (centre[0] - half[0], centre[1] - half[1], centre[0] + half[0], centre[1] + half[1])
        ladder_box = (box[0] + BOX_WIDTH - 8.0, box[1], box[2] - BOX_WIDTH + 8.0, box[3])   # between the boxes
        mesh = Mesh()
        lines = []               # (points, colour, dash)

        def line(points, colour=HUD, dash=0.0):
            lines.append((points, colour, dash))

        # The heading frame: the nose's direction along the horizon, and the horizon's right.
        level = _combine(nose, 1.0, radial, -_dot(nose, radial))
        if _dot(level, level) < 1e-8:
            level = _combine(up, 1.0, radial, -_dot(up, radial))
        level = _normalize(level)
        level_right = _cross(level, radial)
        labels = []

        # The horizon and the ladder: rung centres along the nose's heading, laid out on the screen.
        nose_pitch = math.degrees(math.asin(max(-1.0, min(1.0, _dot(nose, radial)))))
        first = int(math.floor((nose_pitch - FIELD[1] - 6.0) / LADDER_STEP)) * LADDER_STEP
        for angle in range(first, first + int(2 * FIELD[1] + 12.0) + LADDER_STEP, LADDER_STEP):
            if abs(angle) > 90:
                continue
            a = math.radians(angle)
            direction = _combine(level, math.cos(a), radial, math.sin(a))
            p = project(direction)
            q = project(_combine(direction, 1.0, level_right, 0.05))
            if p is None or q is None:
                continue
            along = (q[0] - p[0], q[1] - p[1])
            n = math.hypot(*along)
            along = (along[0] / n, along[1] / n)
            toward_sky = (along[1], -along[0])
            if angle == 0:
                for sign in (-1.0, 1.0):
                    a0 = (p[0] + sign * along[0] * HORIZON_GAP, p[1] + sign * along[1] * HORIZON_GAP)
                    a1 = (p[0] + sign * along[0] * HORIZON_LENGTH, p[1] + sign * along[1] * HORIZON_LENGTH)
                    part = clip(a0, a1, ladder_box)
                    if part:
                        line(part)
                continue
            tick = -1.0 if angle > 0 else 1.0           # the rungs' ends point at the horizon
            for sign in (-1.0, 1.0):
                inner = (p[0] + sign * along[0] * RUNG_GAP, p[1] + sign * along[1] * RUNG_GAP)
                outer = (inner[0] + sign * along[0] * RUNG_LENGTH, inner[1] + sign * along[1] * RUNG_LENGTH)
                end = (outer[0] + tick * toward_sky[0] * RUNG_TICK, outer[1] + tick * toward_sky[1] * RUNG_TICK)
                inside = all(ladder_box[0] <= x <= ladder_box[2] and ladder_box[1] <= y <= ladder_box[3]
                             for x, y in (inner, outer, end))
                if not inside:
                    continue
                line((inner, outer, end), dash=0.0 if angle > 0 else 5.0)
                if angle % 10 == 0:
                    label = (outer[0] + sign * along[0] * 16.0 + tick * toward_sky[0] * 3.0,
                             outer[1] + sign * along[1] * 16.0 + tick * toward_sky[1] * 3.0)
                    labels.append(("%d" % angle, label))

        # The waterline symbol at the nose.
        cx, cy = centre
        line([(cx - 26, cy), (cx - 12, cy), (cx - 6, cy + 7), (cx, cy), (cx + 6, cy + 7), (cx + 12, cy),
              (cx + 26, cy)])

        # The flight path marker, held inside the field.
        if moving is not None:
            m = project(moving)
            if m is not None:
                inside = box[0] + 20 <= m[0] <= box[2] - 20 and box[1] + 12 <= m[1] <= box[3] - 12
                mx = min(max(m[0], box[0] + 20), box[2] - 20)
                my = min(max(m[1], box[1] + 12), box[3] - 12)
                colour = HUD if inside else HUD_DIM
                ring = [(mx + 6.5 * math.cos(k * math.pi / 8), my + 6.5 * math.sin(k * math.pi / 8))
                        for k in range(17)]
                line(ring, colour)
                line([(mx - 6.5, my), (mx - 17.0, my)], colour)
                line([(mx + 6.5, my), (mx + 17.0, my)], colour)
                line([(mx, my - 6.5), (mx, my - 13.0)], colour)

        # The heading tape across the top of the field.
        tape_y = box[1] - 14.0
        heading = r["heading"]
        start = int(math.ceil((heading - HEADING_SPAN) / 5.0)) * 5
        for mark in range(start, int(heading + HEADING_SPAN) + 1, 5):
            x = cx + (mark - heading) * HEADING_SCALE
            long_mark = mark % 10 == 0
            line([(x, tape_y), (x, tape_y - (8.0 if long_mark else 4.0))])
            if long_mark:
                labels.append(("%02d" % ((mark % 360) // 10), (x, tape_y - 18.0)))
        line([(cx, tape_y + 2.0), (cx - 5.0, tape_y + 9.0), (cx + 5.0, tape_y + 9.0), (cx, tape_y + 2.0)])
        heading_box = (cx - 24.0, tape_y - 54.0, cx + 24.0, tape_y - 32.0)
        line(self._rectangle(heading_box))
        self._place(self.heading, "%03d" % (int(round(heading)) % 360), (cx, tape_y - 43.0), (48, 22))

        # The speed on the left and the height above the ground on the right, boxed, at the nose's level.
        speed_box = (box[0] - 18.0, cy - 15.0, box[0] + BOX_WIDTH - 18.0, cy + 15.0)
        height_box = (box[2] - BOX_WIDTH + 18.0, cy - 15.0, box[2] + 18.0, cy + 15.0)
        for b in (speed_box, height_box):
            line(self._rectangle(b))
        self._place(self.speed, "%.0f" % r["speed"], (speed_box[0] + 36.0, cy), (66, 26))
        self._place(self.speed_unit, "M/S", (speed_box[0] + 30.0, cy - 25.0), (60, 16))
        value, unit = r["height"]
        self._place(self.height, value, (height_box[2] - 44.0, cy), (66, 26))
        self._place(self.height_unit, unit.upper() + "  AGL", (height_box[0] + 30.0, cy - 25.0), (60, 16))
        modes = "THR %3.0f%%" % (r["throttle"] * 100.0)
        modes += "\nASSIST" if r["assist"] else "\nASSIST OFF"
        if r["warp"] > 1:
            modes += "\nWARP x%d" % r["warp"]
        self._place(self.left_lines, modes, (speed_box[0] + 45.0, cy + 30.0 + 30.0), (90, 60))
        # The throttle's bar under the speed box.
        bar_y = cy + 22.0
        line([(speed_box[0], bar_y), (speed_box[0] + 80.0, bar_y)], HUD_DIM)
        if r["throttle"] > 0.0:
            line([(speed_box[0], bar_y), (speed_box[0] + 80.0 * min(r["throttle"], 1.0), bar_y)])
        self._place(self.right_lines, "VS %+.0f\n%s %s" % (r["climb"], r["datum_short"], r["datum_height"]),
                    (height_box[2] - 55.0, cy + 30.0 + 22.0), (110, 44))
        self._place(self.contact, r["contact"] or None, (cx, box[3] - 18.0), (360, 24))

        for k, pair in enumerate(self.labels):
            if k < len(labels):
                self._place(pair, labels[k][0], labels[k][1], (40, 18))
            else:
                self._place(pair, None, (0.0, 0.0))

        for points, colour, dash in lines:
            self._stroke(mesh, points, HUD_SHADOW, SHADOW_LINE, dash)
        for points, colour, dash in lines:
            self._stroke(mesh, points, colour, LINE, dash)
        self.ui.set_geometry(self.symbols, mesh.positions, mesh.indices, mesh.colors)

    @staticmethod
    def _rectangle(b):
        return [(b[0], b[1]), (b[2], b[1]), (b[2], b[3]), (b[0], b[3]), (b[0], b[1])]

    @staticmethod
    def _stroke(mesh, points, colour, width, dash):
        """Segments between `points`, dashed `dash` long (0: solid)."""
        for a, b in zip(points, points[1:]):
            if not dash:
                mesh.segment(a, b, colour, width)
                continue
            length = math.hypot(b[0] - a[0], b[1] - a[1])
            d = 0.0
            while d < length:
                e = min(d + dash, length)
                mesh.segment((a[0] + (b[0] - a[0]) * d / length, a[1] + (b[1] - a[1]) * d / length),
                             (a[0] + (b[0] - a[0]) * e / length, a[1] + (b[1] - a[1]) * e / length), colour, width)
                d += 2.0 * dash


class Display:
    """A multi-function display: a world canvas with a title, a tag at the
    title's right, and MFD_ROWS rows of a label and a value."""

    def __init__(self, engine, size):
        ui = engine.ui
        self.ui = ui
        self.entity = ui.create_world_canvas(pixel_size=MFD_PIXELS, world_size=size, emission=MFD_EMISSION)
        ui.set_canvas_clear_color(self.entity, MFD_BACKGROUND)
        engine.scene.set_render_layer_mask(self.entity, 2)
        w, h = MFD_PIXELS
        self.title = self._text(36, MFD_TITLE, sk.TEXT_ALIGN_LEFT, (26, 18), (w - 52, 40))
        self.tag = self._text(27, MFD_LABEL, sk.TEXT_ALIGN_RIGHT, (26, 22), (w - 52, 36))
        rule = ui.create_panel(self.entity, size=(w - 52, 2), color=MFD_RULE)
        ui.set_rect(rule, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0), position=(26, 68), size=(w - 52, 2))
        self.rows = []
        top, step = 84, (h - 84 - 30) / MFD_ROWS
        for k in range(MFD_ROWS):
            y = top + k * step
            label = self._text(27, MFD_LABEL, sk.TEXT_ALIGN_LEFT, (26, y), (w - 52, step))
            value = self._text(36, MFD_VALUE, sk.TEXT_ALIGN_RIGHT, (26, y), (w - 52, step))
            self.rows.append((label, value))
        self.bar = ui.create_panel(self.entity, size=(w - 52, 6), color=(MFD_RULE[0], MFD_RULE[1], MFD_RULE[2], 0.3))
        ui.set_rect(self.bar, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0), position=(26, h - 26),
                    size=(w - 52, 6))
        self.fill = ui.create_panel(self.bar, size=(0, 6), color=MFD_VALUE)
        self.texts = {}
        self.filled = None

    def _text(self, size, colour, align, position, box):
        text = self.ui.create_text(self.entity, "", size=size, color=colour)
        self.ui.set_rect(text, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0), position=position, size=box)
        self.ui.set_text_align(text, align, sk.TEXT_ALIGN_MIDDLE)
        return text

    def _set(self, element, text):
        if self.texts.get(element) != text:
            self.ui.set_text(element, text)
            self.texts[element] = text

    def update(self, title, tag, rows, bar=None):
        """`rows`: (label, value) pairs; `bar`: the fraction of the bar
        along the bottom to fill, or None for no bar."""
        self._set(self.title, title)
        self._set(self.tag, tag)
        for k, (label, value) in enumerate(self.rows):
            self._set(label, rows[k][0] if k < len(rows) else "")
            self._set(value, rows[k][1] if k < len(rows) else "")
        if bar != self.filled:
            self.ui.set_visible(self.bar, bar is not None)
            if bar is not None:
                self.ui.set_rect(self.fill, anchor_min=(0, 0), anchor_max=(max(min(bar, 1.0), 0.0), 1),
                                 pivot=(0, 0), position=(0, 0), size=(0, 0))
            self.filled = bar


class Instruments:
    def __init__(self, engine, displays, fov):
        """`displays`: how many multi-function displays, FLIGHT's first;
        `fov`: the camera's vertical field of view, degrees."""
        self.engine = engine
        self.hud = HeadUpDisplay(engine, fov)
        self.displays = [Display(engine, (1.0, 1.0)) for _ in range(displays)]
        self.visible = True
        self.show(False)

    def show(self, on):
        self.visible = on
        self.hud.show(on)
        for display in self.displays:
            self.engine.scene.set_visible(display.entity, on)

    def place(self, screens, to_world, euler_from_basis):
        """Put the displays on `screens`, (centre, right, up, (width,
        height)) of each in the camera's space at the eye (right +X, up +Y,
        back +Z), at their sizes; `to_world` turns a direction in the
        camera's space into the world's (the camera sits at the origin)."""
        for display, (centre, right, up, (width, height)) in zip(self.displays, screens):
            r, u = to_world(right), to_world(up)
            normal = _cross(r, u)
            self.engine.scene.set_position(display.entity, to_world(centre))
            self.engine.scene.set_rotation(display.entity, euler_from_basis(r, u, tuple(-c for c in normal)))
            self.engine.scene.set_scale(display.entity, (width, height, 1.0))

    def update(self, view, r):
        """`view`: as HeadUpDisplay.update; `r`: the readings, a dict of
        body, speed, climb, height (value, unit) above the ground, agl and
        datum_height (texts), datum and datum_short (its name, and that
        shortened for the HUD), heading, throttle, assist, warp,
        contact, periapsis and apoapsis (texts), circular, density,
        dynamic_pressure, glow."""
        self.hud.update(view, r)
        flight, orbit = self.displays
        flight.update("FLIGHT", "", [
            ("SPEED", "%.0f m/s" % r["speed"]),
            ("CLIMB", "%+.1f m/s" % r["climb"]),
            ("ABOVE GROUND", r["agl"]),
            ("ABOVE " + r["datum"].upper(), r["datum_height"]),
            ("THROTTLE", "%.0f %%" % (r["throttle"] * 100.0)),
            ("ASSIST", "ON" if r["assist"] else "OFF"),
            ("TIME WARP", "x%d" % r["warp"]),
        ], bar=r["throttle"])
        orbit.update("ORBIT", r["body"].upper(), [
            ("PERIAPSIS", r["periapsis"]),
            ("APOAPSIS", r["apoapsis"]),
            ("SPEED", "%.0f m/s" % r["speed"]),
            ("CIRCULAR SPEED", "%.0f m/s" % r["circular"]),
            ("AIR DENSITY", "%.4f kg/m3" % r["density"]),
            ("DYN. PRESSURE", "%.2f kPa" % (r["dynamic_pressure"] / 1000.0)),
            ("HEAT GLOW", "%.0f %%" % (r["glow"] * 100.0)),
        ])

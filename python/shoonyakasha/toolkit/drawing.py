"""Retained solid geometry with a one-unit alpha fringe; no texture files."""
from contextlib import contextmanager
import math
from .ui import Component


def _point(value):
    value = tuple(value)
    if len(value) != 2 or not all(math.isfinite(x) for x in value):
        raise ValueError("expected a finite 2D point")
    return value


def _color(value):
    value = tuple(value)
    if len(value) != 4 or not all(math.isfinite(x) for x in value):
        raise ValueError("expected a finite RGBA color")
    return value


def _cross(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


class Mesh:
    """CPU tessellator, independently testable without an engine."""
    def __init__(self):
        self.positions, self.indices, self.colors = [], [], []

    def face(self, points, colors):
        base = len(self.positions)
        self.positions.extend(points)
        self.colors.extend(colors)
        for i in range(1, len(points) - 1):
            self.indices.extend((base, base + i, base + i + 1))

    def segment(self, a, b, color, width):
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy)
        if length < 1e-10:
            return
        nx, ny = -dy / length, dx / length
        r = width / 2
        points = [(p[0] + nx * offset, p[1] + ny * offset)
                  for p, offset in ((a, -r), (b, -r), (b, r), (a, r))]
        self.face(points, [color] * 4)
        transparent = color[:3] + (0.0,)
        for sign, i, j in ((-1, 0, 1), (1, 3, 2)):
            pa = (a[0] + nx * (r + 1) * sign, a[1] + ny * (r + 1) * sign)
            pb = (b[0] + nx * (r + 1) * sign, b[1] + ny * (r + 1) * sign)
            self.face([points[i], points[j], pb, pa], [color, color, transparent, transparent])
        for p, q, tx, ty in ((points[0], points[3], -dx / length, -dy / length),
                              (points[1], points[2], dx / length, dy / length)):
            self.face([p, q, (q[0] + tx, q[1] + ty), (p[0] + tx, p[1] + ty)],
                      [color, color, transparent, transparent])

    def polygon(self, points, color):
        # Ear clipping supports simple concave polygons in either winding.
        points = list(points)
        if points and points[0] == points[-1]:
            points.pop()
        if len(points) < 3:
            raise ValueError("polygon needs at least three vertices")
        area = sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(points, points[1:] + points[:1]))
        if abs(area) < 1e-10:
            raise ValueError("degenerate polygon")
        sign = 1 if area > 0 else -1
        remaining = list(range(len(points)))
        while len(remaining) > 3:
            found = False
            for n, index in enumerate(remaining):
                ia, ib, ic = remaining[n - 1], index, remaining[(n + 1) % len(remaining)]
                a, b, c = points[ia], points[ib], points[ic]
                if sign * _cross(a, b, c) <= 1e-10:
                    continue
                if any(all(sign * _cross(u, v, points[j]) >= -1e-10
                           for u, v in ((a, b), (b, c), (c, a)))
                       for j in remaining if j not in (ia, ib, ic)):
                    continue
                self.face((a, b, c), [color] * 3)
                remaining.pop(n)
                found = True
                break
            if not found:
                raise ValueError("polygon must be simple and nondegenerate")
        self.face([points[i] for i in remaining], [color] * 3)
        # Outward alpha fringe, leaving interior triangles fully opaque.
        clear = color[:3] + (0.0,)
        for a, b in zip(points, points[1:] + points[:1]):
            dx, dy = b[0] - a[0], b[1] - a[1]
            length = math.hypot(dx, dy)
            if not length:
                continue
            nx, ny = sign * dy / length, -sign * dx / length
            self.face((a, b, (b[0] + nx, b[1] + ny), (a[0] + nx, a[1] + ny)),
                      (color, color, clear, clear))


class Drawing(Component):
    def __init__(self, context, parent=None, *, size=(400, 300)):
        super().__init__(context, parent, size=size)
        self.commands, self._submitted = [], None
        self.ui.set_raycast_target(self.entity, False)

    @contextmanager
    def redraw(self):
        self.clear()
        yield self
        self.flush()

    def clear(self):
        self.commands = []
        return self

    def polyline(self, points, color=(1, 1, 1, 1), width=2.0, dash=0.0):
        if not math.isfinite(width) or width <= 0 or not math.isfinite(dash) or dash < 0:
            raise ValueError("width must be positive; dash must be nonnegative")
        self.commands.append(("line", tuple(_point(p) for p in points), _color(color), width, dash))
        return self

    def line(self, a, b, **kwargs):
        return self.polyline((a, b), **kwargs)

    def polygon(self, points, color=(1, 1, 1, 1)):
        self.commands.append(("polygon", tuple(_point(p) for p in points), _color(color)))
        return self

    def rectangle(self, position, size, color=(1, 1, 1, 1), *, filled=True, width=2):
        x, y = _point(position)
        w, h = _point(size)
        points = ((x, y), (x + w, y), (x + w, y + h), (x, y + h))
        return self.polygon(points, color) if filled else self.polyline(points + points[:1], color, width)

    def circle(self, center, radius, color=(1, 1, 1, 1), *, filled=True, width=2, segments=48):
        x, y = _point(center)
        if not math.isfinite(radius) or radius <= 0 or not isinstance(segments, int) or segments < 3:
            raise ValueError("invalid circle")
        points = tuple((x + radius * math.cos(i * 2 * math.pi / segments),
                        y + radius * math.sin(i * 2 * math.pi / segments)) for i in range(segments))
        return self.polygon(points, color) if filled else self.polyline(points + points[:1], color, width)

    def arrow(self, a, b, color=(1, 1, 1, 1), *, width=2, head=12):
        a, b = _point(a), _point(b)
        if not math.isfinite(head) or head <= 0:
            raise ValueError("invalid arrow head")
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy)
        if length < 1e-10:
            return self
        dx, dy = dx / length, dy / length
        self.polyline((a, b), color, width)
        return self.polygon((b, (b[0] - head * dx - head * 0.45 * dy, b[1] - head * dy + head * 0.45 * dx),
                             (b[0] - head * dx + head * 0.45 * dy, b[1] - head * dy - head * 0.45 * dx)), color)

    def flush(self):
        # Native canvas rects are resolved by the first layout pass. Keep the
        # commands pending until their element has a usable target rectangle.
        rect = self.ui.get_rect(self.entity)
        if rect[2] <= 0 or rect[3] <= 0:
            return
        signature = tuple(self.commands)
        if signature == self._submitted:
            return
        mesh = Mesh()
        for command in self.commands:
            kind, points, color = command[:3]
            if kind == "polygon":
                mesh.polygon(points, color)
                continue
            width, dash = command[3:]
            travelled = 0.0
            for a, b in zip(points, points[1:]):
                length = math.hypot(b[0] - a[0], b[1] - a[1])
                if not dash:
                    mesh.segment(a, b, color, width)
                else:
                    distance = 0.0
                    while distance < length:
                        phase = (travelled + distance) % (2 * dash)
                        run = min(length - distance, (dash if phase < dash else 2 * dash) - phase)
                        if run < 1e-9:
                            run = min(length - distance, 1e-8)
                        if phase < dash:
                            p = lambda d: (a[0] + (b[0] - a[0]) * d / length,
                                           a[1] + (b[1] - a[1]) * d / length)
                            mesh.segment(p(distance), p(distance + run), color, width)
                        distance += run
                travelled += length
        self.ui.set_geometry(self.entity, mesh.positions, mesh.indices, mesh.colors)
        self._submitted = signature

    def _poll(self, dt):
        self.flush()

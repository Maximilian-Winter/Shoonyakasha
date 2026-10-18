"""Linear plots with bounded histories and viewport-aware curve reduction."""
from collections import deque
import math
from .drawing import Drawing
from .ui import Component, Label


class Series:
    def __init__(self, name="", *, color=(0.28, 0.70, 0.90, 1), capacity=4096, points=()):
        if not isinstance(capacity, int) or capacity < 2:
            raise ValueError("series capacity must be at least two")
        self.name, self.color = name, color
        self.points = deque(maxlen=capacity)
        self.revision = 0
        self.extend(points)

    def append(self, x, y):
        self.points.append((float(x), float(y)))
        self.revision += 1

    def extend(self, points):
        for x, y in points:
            self.append(x, y)

    def clear(self):
        self.points.clear()
        self.revision += 1


def decimate(points, width):
    """For monotone x, retain first/last/extrema and gaps per screen column.

    Arbitrary trajectories use ordered, fixed-size buckets, preserving peaks
    without turning a loop back into an x-sorted function.
    """
    points = list(points)
    limit = max(1, int(width))
    if len(points) <= limit * 4:
        return points
    result = []
    bucket_size = max(1, int(math.ceil(len(points) / limit)))
    for start in range(0, len(points), bucket_size):
        bucket = points[start:start + bucket_size]
        valid = [(i, p) for i, p in enumerate(bucket) if all(math.isfinite(v) for v in p)]
        if len(valid) != len(bucket):
            # Never bridge a discontinuity hidden by reduction.
            result.append((math.nan, math.nan))
            continue
        chosen = {0, len(bucket) - 1, min(valid, key=lambda item: item[1][1])[0],
                  max(valid, key=lambda item: item[1][1])[0]}
        result.extend(bucket[i] for i in sorted(chosen))
    return result


def _range(values, explicit):
    if explicit is not None:
        low, high = explicit
        if not math.isfinite(low) or not math.isfinite(high) or low >= high:
            raise ValueError("axis range must be finite and increasing")
        return low, high
    if not values:
        return 0.0, 1.0
    low, high = min(values), max(values)
    padding = max((high - low) * 0.05, abs(low) * 0.01, 1e-6)
    return low - padding, high + padding


class Plot(Component):
    def __init__(self, context, parent=None, *, title="", size=(600, 320), x_range=None, y_range=None):
        if size[0] < 140 or size[1] < 140:
            raise ValueError("plot must be at least 140 by 140 units")
        super().__init__(context, parent, size=size)
        self.size, self.x_range, self.y_range = size, x_range, y_range
        self.series, self.markers = [], []
        self.cursor = None
        self._signature = None
        self.title = Label(context, self, title, size=(size[0] - 70, 28)).place(52, 0)
        self.grid = Drawing(context, self, size=size).place(0, 0)
        self.width, self.height = size[0] - 76, size[1] - 100
        self.viewport = Component(context, self, size=(self.width, self.height)).place(58, 38)
        self.ui.set_clip(self.viewport.entity, True)
        self.curves = Drawing(context, self.viewport, size=(self.width, self.height)).place(0, 0)
        self.xticks = [Label(context, self, size=(85, 22), font_size=13, muted=True) for _ in range(5)]
        self.yticks = [Label(context, self, size=(55, 22), font_size=13, muted=True) for _ in range(5)]
        self.legend = Label(context, self, size=(size[0] - 70, 28), font_size=14).place(58, size[1] - 30)
        self.ui.set_rich_text(self.legend.entity)
        self.style()

    def style(self):
        self._signature = None

    def add(self, series):
        self.series.append(series)
        return series

    def _poll(self, dt):
        self.refresh()

    def refresh(self):
        canvas = self.ui.get_canvas_size(self.context.root) if self.context._owns_canvas else (1920, 1080)
        # Canvas width bounds the working set even before first layout is available.
        pixel_width = max(1, self.width * self.context.engine.framebuffer_size[0] / max(1, canvas[0]))
        signature = (tuple((s.revision, s.name, s.color) for s in self.series), self.x_range,
                     self.y_range, self.cursor, tuple(self.markers), self.context.theme, pixel_width)
        if signature == self._signature:
            return
        finite = [p for s in self.series for p in s.points if all(math.isfinite(v) for v in p)]
        x0, x1 = _range([p[0] for p in finite], self.x_range)
        y0, y1 = _range([p[1] for p in finite], self.y_range)
        project = lambda p: ((p[0] - x0) / (x1 - x0) * self.width,
                             (1 - (p[1] - y0) / (y1 - y0)) * self.height)
        t = self.context.theme
        with self.grid.redraw() as drawing:
            for i in range(5):
                x, y = 58 + self.width * i / 4, 38 + self.height * i / 4
                drawing.line((x, 38), (x, 38 + self.height), color=t.muted[:3] + (0.25,), width=1)
                drawing.line((58, y), (58 + self.width, y), color=t.muted[:3] + (0.25,), width=1)
                self.xticks[i].text = "%.3g" % (x0 + (x1 - x0) * i / 4)
                self.xticks[i].place(x - 24, 42 + self.height)
                self.yticks[i].text = "%.3g" % (y1 - (y1 - y0) * i / 4)
                self.yticks[i].place(0, y - 10)
        with self.curves.redraw() as drawing:
            for series in self.series:
                run = []
                for p in decimate(series.points, pixel_width):
                    if all(math.isfinite(v) for v in p):
                        run.append(project(p))
                    else:
                        drawing.polyline(run, series.color)
                        run = []
                drawing.polyline(run, series.color)
            if self.cursor is not None and math.isfinite(self.cursor):
                x = project((self.cursor, y0))[0]
                drawing.line((x, 0), (x, self.height), color=t.muted, width=1)
            for x, y, color in self.markers:
                if math.isfinite(x) and math.isfinite(y):
                    drawing.circle(project((x, y)), 4, color)
        def legend_entry(series):
            color = "#%02X%02X%02X" % tuple(round(max(0, min(1, c)) * 255) for c in series.color[:3])
            name = series.name.replace("<", "&lt;").replace(">", "&gt;")
            return "<color=%s>%s</color>" % (color, name)
        self.legend.text = "   ".join(legend_entry(s) for s in self.series)
        self._signature = signature

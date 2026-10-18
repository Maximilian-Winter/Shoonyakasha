"""Absolute-time tracks. Setters must reconstruct state without external effects."""
from dataclasses import dataclass
import math


@dataclass
class Binding:
    key: object
    get: object
    set: object
    parameter: object = None

    @classmethod
    def value(cls, value):
        return cls((id(value), "value"), lambda: value.value,
                   lambda data: value.set(data, authored=True), value)

    @classmethod
    def attribute(cls, target, name):
        return cls((id(target), name), lambda: getattr(target, name),
                   lambda data: setattr(target, name, data))

    @classmethod
    def scene(cls, scene, entity, property):
        return cls((id(scene), entity, property),
                   lambda: getattr(scene, "get_" + property)(entity),
                   lambda data: getattr(scene, "set_" + property)(entity, data))

    @classmethod
    def camera(cls, scene, entity, property="position"):
        if property not in ("position", "rotation", "camera_fov"):
            raise ValueError("camera property must be position, rotation, or camera_fov")
        return cls.scene(scene, entity, property)


@dataclass(frozen=True)
class Keyframe:
    time: float
    value: object


def _numeric(value):
    return isinstance(value, (float, int)) and not isinstance(value, bool)


def _lerp(a, b, t):
    if _numeric(a) and _numeric(b):
        return a + (b - a) * t
    if isinstance(a, tuple) and isinstance(b, tuple) and len(a) == len(b):
        return tuple(_lerp(x, y, t) for x, y in zip(a, b))
    return a


class Track:
    def __init__(self, binding, keyframes, interpolation="linear"):
        if interpolation not in ("hold", "linear", "ease_in_out"):
            raise ValueError("unknown interpolation")
        self.binding, self.interpolation = binding, interpolation
        self.keyframes = tuple(k if isinstance(k, Keyframe) else Keyframe(*k) for k in keyframes)
        if not self.keyframes:
            raise ValueError("a track requires keyframes")
        previous = -1.0
        for frame in self.keyframes:
            if not math.isfinite(frame.time) or frame.time < 0 or frame.time <= previous:
                raise ValueError("keyframe times must be finite, nonnegative and strictly increasing")
            previous = frame.time
        first = self.keyframes[0].value
        for frame in self.keyframes:
            if isinstance(first, tuple):
                if not isinstance(frame.value, tuple) or len(frame.value) != len(first):
                    raise ValueError("tuple keyframes must have equal dimensions")
            elif type(first) is not type(frame.value) and not (_numeric(first) and _numeric(frame.value)):
                raise ValueError("keyframe types must agree")
            values = frame.value if isinstance(frame.value, tuple) else (frame.value,)
            if any(_numeric(v) and not math.isfinite(v) for v in values):
                raise ValueError("nonfinite keyframe value")
            if binding.parameter is not None:
                binding.parameter.validate(frame.value)
        p = binding.parameter
        if isinstance(first, (str, bool, int)) or (p is not None and getattr(p, "step", None)):
            self.interpolation = "hold"

    @property
    def duration(self):
        return self.keyframes[-1].time

    def sample(self, time):
        frames = self.keyframes
        if time <= frames[0].time:
            return frames[0].value
        for a, b in zip(frames, frames[1:]):
            if time < b.time:
                t = (time - a.time) / (b.time - a.time)
                if self.interpolation == "hold":
                    return a.value
                if self.interpolation == "ease_in_out":
                    t = t * t * (3 - 2 * t)
                return _lerp(a.value, b.value, t)
        return frames[-1].value


class Timeline:
    def __init__(self, *tracks, duration=None):
        self.tracks = []
        self._baselines = {}
        self._duration = duration
        if duration is not None and (not math.isfinite(duration) or duration < 0):
            raise ValueError("duration must be finite and nonnegative")
        for track in tracks:
            self.add(track)

    def add(self, track):
        if any(t.binding.key == track.binding.key for t in self.tracks):
            raise ValueError("conflicting tracks for the same property")
        self.tracks.append(track)
        self._baselines[track.binding.key] = track.binding.get()
        return track

    @property
    def duration(self):
        return max([self._duration or 0.0] + [t.duration for t in self.tracks])

    @property
    def parameters(self):
        return [t.binding.parameter for t in self.tracks if t.binding.parameter is not None]

    def evaluate(self, time):
        if not math.isfinite(time) or time < 0:
            raise ValueError("time must be finite and nonnegative")
        for track in self.tracks:
            track.binding.set(track.sample(time))

    def reset(self):
        for track in self.tracks:
            p = track.binding.parameter
            track.binding.set(getattr(p, "default", self._baselines[track.binding.key]))

    def lock(self, enabled):
        for parameter in self.parameters:
            parameter.read_only = enabled

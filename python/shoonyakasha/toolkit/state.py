"""Observable values, validated parameters, and explicit engine bindings."""
import math


class Value:
    def __init__(self, value):
        self._value = value
        self._listeners = []
        self.read_only = False

    @property
    def value(self):
        return self._value

    @value.setter
    def value(self, value):
        self.set(value)

    def validate(self, value):
        return value

    def set(self, value, *, authored=False):
        if self.read_only and not authored:
            raise ValueError("value is controlled by playback")
        value = self.validate(value)
        if value == self._value:
            return
        self._value = value
        for listener in tuple(self._listeners):
            listener(value)

    def subscribe(self, callback, *, immediate=False):
        self._listeners.append(callback)
        active = True

        def unsubscribe():
            nonlocal active
            if active:
                active = False
                self._listeners.remove(callback)

        if immediate:
            try:
                callback(self._value)
            except BaseException:
                unsubscribe()
                raise
        return unsubscribe


class Parameter(Value):
    def __init__(self, name, default, *, type=None, minimum=None, maximum=None,
                 step=None, unit="", format=None, choices=None):
        self.name = name
        self.type = type or default.__class__
        self.minimum, self.maximum, self.step = minimum, maximum, step
        self.unit, self.format = unit, format
        self.choices = tuple(choices) if choices is not None else None
        if not name or self.type not in (float, int, bool, str):
            raise ValueError("a named float, int, bool or string parameter is required")
        if self.type is str and not self.choices:
            raise ValueError("string parameters require choices")
        if minimum is not None and maximum is not None and minimum > maximum:
            raise ValueError("minimum exceeds maximum")
        if step is not None and (not math.isfinite(step) or step <= 0):
            raise ValueError("step must be positive and finite")
        self.default = self.validate(default)
        super().__init__(self.default)

    def validate(self, value):
        if self.type in (float, int):
            if isinstance(value, bool) or not isinstance(value, (float, int)):
                raise ValueError("%s requires a number" % self.name)
            if not math.isfinite(value) or (self.type is int and int(value) != value):
                raise ValueError("%s requires a finite %s" % (self.name, self.type.__name__))
            value = self.type(value)
        elif not isinstance(value, self.type):
            raise ValueError("%s requires %s" % (self.name, self.type.__name__))
        if self.minimum is not None and value < self.minimum:
            raise ValueError("%s is below its minimum" % self.name)
        if self.maximum is not None and value > self.maximum:
            raise ValueError("%s exceeds its maximum" % self.name)
        if self.choices is not None and value not in self.choices:
            raise ValueError("%s is not an allowed choice" % self.name)
        if self.step is not None and self.type in (float, int):
            origin = self.minimum if self.minimum is not None else 0
            offset = (value - origin) / self.step
            if not math.isclose(offset, round(offset), rel_tol=1e-8, abs_tol=1e-7):
                raise ValueError("%s must align with its step" % self.name)
        return value

    def display(self):
        text = self.format(self.value) if callable(self.format) else (
            self.format.format(self.value) if self.format else str(self.value))
        return text + (" " + self.unit if self.unit else "")


class ParameterSet:
    def __init__(self, *parameters):
        self.parameters = {}
        self.presets = {}
        for parameter in parameters:
            if parameter.name in self.parameters:
                raise ValueError("duplicate parameter: " + parameter.name)
            self.parameters[parameter.name] = parameter

    def __getitem__(self, name):
        return self.parameters[name]

    def __iter__(self):
        return iter(self.parameters.values())

    def values(self):
        return {p.name: p.value for p in self}

    def assign(self, values, *, authored=False):
        # Validate the entire transaction before notifying any listener.
        checked = {name: self[name].validate(value) for name, value in values.items()}
        if not authored and any(self[name].read_only for name in checked):
            raise ValueError("parameters are controlled by playback")
        changed = []
        for name, value in checked.items():
            p = self[name]
            if p.value != value:
                p._value = value
                changed.append(p)
        for p in changed:
            for listener in tuple(p._listeners):
                listener(p.value)

    def reset(self):
        self.assign({p.name: p.default for p in self}, authored=True)

    def save_preset(self, name):
        self.presets[name] = self.values()

    def apply_preset(self, name):
        self.assign(self.presets[name])


def bind_custom(parameter, engine, key, kind="float"):
    """Return an unsubscribe handle; ``key`` is an explicit scene.custom key."""
    if kind not in ("float", "uint", "vec2", "vec3", "vec4", "mat4"):
        raise ValueError("unsupported custom binding type")
    setter = getattr(engine, "set_custom_" + kind)
    return parameter.subscribe(lambda value: setter(key, value), immediate=True)


def bind_scene(value, scene, entity, property):
    setter = getattr(scene, "set_" + property)
    return value.subscribe(lambda data: setter(entity, data), immediate=True)

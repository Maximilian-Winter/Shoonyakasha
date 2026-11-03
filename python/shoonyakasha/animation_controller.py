"""Build animation state machines for ``engine.animation.set_controller``.

The builder produces the controller's JSON form (see the animation guide);
the engine checks clip, joint and parameter names when the controller is set.

    from shoonyakasha.animation_controller import AnimationController

    c = AnimationController()
    c.float("speed")
    c.trigger("jump")
    base = c.layer("base")
    base.blend1d("locomotion", "speed", [("idle", 0.0), ("walk", 1.5), ("run", 4.0)])
    base.clip("jump", "jump", loop=False, events=[(0.35, "takeoff")])
    base.transition("locomotion", "jump", duration=0.15, when=[("jump",)])
    base.transition("jump", "locomotion", duration=0.25, exit_time=0.9)
    engine.animation.set_controller(hero, c)
"""

import json
from pathlib import Path

__all__ = ["AnimationController", "ControllerLayer"]

_MODES = ("override", "additive")


def _state(motion, speed, speed_parameter, loop, events):
    state = dict(motion)
    if speed != 1.0:
        state["speed"] = speed
    if speed_parameter is not None:
        state["speed_parameter"] = speed_parameter
    if not loop:
        state["loop"] = False
    if events:
        state["events"] = [[t, name] for t, name in events]
    return state


class ControllerLayer:
    """One layer of an AnimationController: states and the transitions
    between them. Made by ``AnimationController.layer``."""

    def __init__(self, name, weight=1.0, mode="override", mask=None, default=None):
        if mode not in _MODES:
            raise ValueError("mode must be 'override' or 'additive', not %r" % (mode,))
        self.name = name
        self.weight = weight
        self.mode = mode
        self.mask = list(mask) if mask else []
        self.default = default
        self.states = {}
        self.transitions = []

    def _add(self, name, state):
        if name in self.states:
            raise ValueError("layer %r already has a state %r" % (self.name, name))
        self.states[name] = state
        return self

    def clip(self, name, clip, *, speed=1.0, speed_parameter=None, loop=True, events=None):
        """A state playing one clip. ``events`` are (normalized time, name)
        pairs. Returns the layer."""
        return self._add(name, _state({"clip": clip}, speed, speed_parameter, loop, events))

    def blend1d(self, name, parameter, clips, *, speed=1.0, speed_parameter=None, loop=True, events=None):
        """A state blending clips by one parameter. ``clips`` are (clip,
        threshold) pairs with ascending thresholds. Returns the layer."""
        motion = {"blend1d": {"parameter": parameter, "clips": [[c, t] for c, t in clips]}}
        return self._add(name, _state(motion, speed, speed_parameter, loop, events))

    def blend2d(self, name, parameters, clips, *, speed=1.0, speed_parameter=None, loop=True, events=None):
        """A state blending clips placed on a plane by two parameters (x, y).
        ``clips`` are (clip, x, y) triples. Returns the layer."""
        px, py = parameters
        motion = {"blend2d": {"parameters": [px, py], "clips": [[c, x, y] for c, x, y in clips]}}
        return self._add(name, _state(motion, speed, speed_parameter, loop, events))

    def transition(self, source, target, *, duration=0.0, exit_time=None, when=None):
        """A transition from state ``source`` (or "any") to ``target``, fading
        over ``duration`` seconds. ``when`` holds conditions: ``(param,)`` for
        a bool or trigger, or ``(param, op, value)`` with op one of > < == !=.
        Returns the layer."""
        entry = {"from": source, "to": target}
        if duration:
            entry["duration"] = duration
        if exit_time is not None:
            entry["exit_time"] = exit_time
        if when:
            entry["when"] = [list(condition) for condition in when]
        self.transitions.append(entry)
        return self

    def to_dict(self):
        layer = {"name": self.name}
        if self.default is not None:
            layer["default"] = self.default
        if self.weight != 1.0:
            layer["weight"] = self.weight
        if self.mode != "override":
            layer["mode"] = self.mode
        if self.mask:
            layer["mask"] = list(self.mask)
        layer["states"] = dict(self.states)
        if self.transitions:
            layer["transitions"] = list(self.transitions)
        return layer


class AnimationController:
    """An animation state machine: parameters and layers."""

    def __init__(self):
        self.parameters = {}
        self.layers = []

    def _parameter(self, name, kind, default):
        if name in self.parameters:
            raise ValueError("parameter %r already exists" % (name,))
        entry = {"type": kind}
        if default is not None:
            entry["default"] = default
        self.parameters[name] = entry
        return self

    def float(self, name, default=None):
        """A float parameter (0 unless ``default`` is given). Returns the controller."""
        return self._parameter(name, "float", default)

    def int(self, name, default=None):
        """An int parameter. Returns the controller."""
        return self._parameter(name, "int", default)

    def bool(self, name, default=None):
        """A bool parameter. Returns the controller."""
        return self._parameter(name, "bool", default)

    def trigger(self, name):
        """A trigger: set by ``set_trigger``, cleared by the transition that
        uses it. Returns the controller."""
        return self._parameter(name, "trigger", None)

    def layer(self, name, *, weight=1.0, mode="override", mask=None, default=None):
        """Add a layer and return it. Layers apply in the order added.
        ``mask`` names joints whose subtrees the layer affects (all joints
        when empty); ``default`` names the starting state (the first added
        when None)."""
        if any(layer.name == name for layer in self.layers):
            raise ValueError("layer %r already exists" % (name,))
        layer = ControllerLayer(name, weight, mode, mask, default)
        self.layers.append(layer)
        return layer

    def to_dict(self):
        result = {}
        if self.parameters:
            result["parameters"] = {name: dict(entry) for name, entry in self.parameters.items()}
        result["layers"] = [layer.to_dict() for layer in self.layers]
        return result

    def to_json(self, indent=None):
        return json.dumps(self.to_dict(), indent=indent)

    def save(self, path):
        """Write the JSON form to ``path``."""
        Path(path).write_text(self.to_json(indent=2) + "\n", encoding="utf-8")

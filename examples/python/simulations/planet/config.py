"""
The walker's settings (walker.py), kept in a JSON file between runs.

`load(path)` reads the file and returns a Config with every setting of
DEFAULTS: a value missing from the file, of the wrong type, or naming no
option of a choice, takes its default; a number outside its RANGES is
moved into it. A file that cannot be read or parsed gives the defaults.
`Config.set(name, value)` takes the value the same way and marks the
config changed; `save()` writes the file, through a temporary file
beside it, when anything changed since it was read or last written.

The window's size (WINDOWS) is read when the engine is made; the others
are applied while the walker runs.
"""

import json
import os

WINDOWS = ((1280, 720), (1600, 900), (1920, 1080), (2560, 1440))
DEFAULTS = {
    "mouse_sensitivity": 1.0,
    "invert_y": False,
    "head_bob": True,
    "fov": 70.0,
    "hud_scale": 1.0,
    "window": [1600, 900],
    "render_scale": 1.0,
    "anti_aliasing": True,
    "clouds": True,
    "autosave": True,
}
RANGES = {
    "mouse_sensitivity": (0.25, 3.0),
    "fov": (55.0, 100.0),
    "hud_scale": (0.75, 1.5),
    "render_scale": (0.5, 1.0),
}
CHOICES = {"window": [list(w) for w in WINDOWS]}


def clean(name, value):
    """`value` for the setting `name` as the module's docstring says, or its default."""
    default = DEFAULTS[name]
    if isinstance(default, bool):
        return value if isinstance(value, bool) else default
    if isinstance(default, float):
        if isinstance(value, bool) or not isinstance(value, (int, float)) or value != value:
            return default
        lo, hi = RANGES[name]
        return max(lo, min(hi, float(value)))
    if name in CHOICES:
        value = list(value) if isinstance(value, (list, tuple)) else None
        return value if value in CHOICES[name] else list(default)
    return default


class Config:
    def __init__(self, path, values=None):
        self.path = path
        self.values = {name: clean(name, (values or {}).get(name, default)) for name, default in DEFAULTS.items()}
        self.changed = False

    def __getitem__(self, name):
        return self.values[name]

    def set(self, name, value):
        value = clean(name, value)
        if value != self.values[name]:
            self.values[name] = value
            self.changed = True

    def save(self):
        """The file written when anything changed; whether it was."""
        if not self.changed:
            return False
        with open(self.path + ".tmp", "w", encoding="utf-8") as f:
            json.dump(self.values, f, indent=2)
        os.replace(self.path + ".tmp", self.path)
        self.changed = False
        return True


def load(path):
    try:
        with open(path, encoding="utf-8") as f:
            values = json.load(f)
    except (OSError, ValueError):
        values = {}
    return Config(path, values if isinstance(values, dict) else {})

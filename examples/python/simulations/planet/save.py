"""
The walker's save (walker.py): a game's record as JSON in `<base>.json`
and the explored map's grids, compressed, in `<base>.npz`. Each is
written through a temporary file beside it and then put in place, the
grids first.

`write(base, data, grids)` stamps `data` (a dict of plain values) with
VERSION and the time, and stores of each grid (explored_map.Grid) only
its measured cells: their flat indices, heights and ore. `read(base)`
returns (data, arrays) or raises SaveError naming what is wrong: no save,
a file that cannot be read, or another version; a save of version 1 is
migrated to VERSION first (`migrate`). `restore_grids(explored,
arrays, bodies)` puts the grids back into an explored_map.ExploredMap
(`bodies` the world bodies by name; grids of bodies not among them are
left out) with nothing pending. `describe(data)` is a line about the save
for the menu.
"""

import json
import os
import time

import numpy as np

from shoonyakasha.planet.resources import FERRITE, NONE

VERSION = 2


class SaveError(Exception):
    pass


def paths(base):
    return base + ".json", base + ".npz"


def exists(base):
    return all(os.path.exists(p) for p in paths(base))


def grid_arrays(grids):
    """The arrays of `grids` (body name: Grid), keyed "<name>/<field>"."""
    arrays = {}
    for name, grid in grids.items():
        flat = np.flatnonzero(~np.isnan(grid.heights)).astype(np.int32)
        arrays[name + "/origin"] = np.array(grid.origin, dtype=np.float64)
        arrays[name + "/shape"] = np.array([grid.size, grid.cell], dtype=np.float64)
        arrays[name + "/index"] = flat
        arrays[name + "/height"] = grid.heights.ravel()[flat]
        arrays[name + "/ore"] = grid.ores.ravel()[flat]
    return arrays


def restore_grids(explored, arrays, bodies, make_grid):
    """`explored`'s grids made again from `arrays` with `make_grid(body,
    origin, cell, size)`, for the bodies by name in `bodies`."""
    explored.grids = {}
    explored.asked, explored.asking = None, []
    names = {key.split("/")[0] for key in arrays}
    for name in sorted(names):
        if name not in bodies:
            continue
        size, cell = arrays[name + "/shape"]
        grid = make_grid(bodies[name], tuple(arrays[name + "/origin"]), float(cell), int(size))
        flat = arrays[name + "/index"]
        grid.heights.ravel()[flat] = arrays[name + "/height"]
        grid.ores.ravel()[flat] = arrays[name + "/ore"]
        grid.measured = int(len(flat))
        explored.grids[name] = grid


def write(base, data, grids):
    data = dict(data, version=VERSION, saved_at=time.time())
    json_path, npz_path = paths(base)
    with open(npz_path + ".tmp", "wb") as f:
        np.savez_compressed(f, **grid_arrays(grids))
    os.replace(npz_path + ".tmp", npz_path)
    with open(json_path + ".tmp", "w", encoding="utf-8") as f:
        json.dump(data, f)
    os.replace(json_path + ".tmp", json_path)
    return data


def read(base):
    json_path, npz_path = paths(base)
    if not exists(base):
        raise SaveError("no save at %s" % json_path)
    try:
        with open(json_path, encoding="utf-8") as f:
            data = json.load(f)
        with np.load(npz_path) as npz:
            arrays = {key: npz[key] for key in npz.files}
    except (OSError, ValueError) as error:
        raise SaveError("the save could not be read: %s" % error)
    if isinstance(data, dict) and data.get("version") == 1:
        migrate(data)
    if not isinstance(data, dict) or data.get("version") != VERSION:
        raise SaveError("the save is not of version %d" % VERSION)
    return data, arrays


def migrate(data):
    """A save's data of version 1 made version 2, in place: the car with a
    full tank (fuel None) and an empty hold, each prop's record without a
    resource ferrite for an ore rock (grade above 0) and 0 for the rest, no
    upgrades owned, and the day at the new game's hour (day_offset None)."""
    car = data.get("car")
    if car is not None:
        car.setdefault("fuel", None)
        car.setdefault("cargo", [])
    for record in data.get("props", []):
        record.setdefault("resource", FERRITE if record.get("grade", 0.0) > 0.0 else NONE)
    data.setdefault("upgrades", [])
    data.setdefault("day_offset", None)
    data["version"] = 2


def describe(data):
    """"Saved 2026-11-04 14:05 · 1,240 cr · 2 of 6 objectives", from a save's data."""
    when = time.strftime("%Y-%m-%d %H:%M", time.localtime(data.get("saved_at", 0.0)))
    game = data.get("game", {})
    credits = format(int(round(data.get("market", {}).get("credits", 0.0))), ",")
    return "Saved %s  ·  %s cr  ·  %d objectives done" % (when, credits, game.get("objective", 0))

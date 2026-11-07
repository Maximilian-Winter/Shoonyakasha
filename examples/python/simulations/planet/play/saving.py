"""
Saving and loading the game (save.py), and autosave.

A save's data has a section for each system, each recorded and restored
by its module: "survival" (survival.record), "progress" (walking.record),
"economy", "vehicle" (with spaceflight's), "exploring", "contracting",
"showers" and "storms"; and those kept here: "world" (the terrain, the
world's clock and the rocks taken from the scatter), "player" (the body
the player is on, the feet from its centre, the heading and the pitch;
None while driving) and "props" (each prop's record, with where an ore
rock was first seen, a pod's point of interest, a courier's contract,
when a hot one struck, and whether it is the last meteorite). The explored
map's grids go beside it (save.write).
"""

from shoonyakasha.planet.vec import add, normalize, scale

import save
from explored_map import Grid
import hud as hud_module
from play.state import AUTOSAVE_SECONDS
from play.core import GOOD_TOAST, args, bodies, foot, planet, save_base, settings, state, world
from play import contracting, economy, exploring, menus, showers, storms, survival, vehicle, walking


def save_game(base=None, quiet=False):
    """The game written to `base` (save_base when None). Returns the data,
    or None without a player or the car driven."""
    player = foot.player
    if player is None and not state.vehicle.driving:
        if not quiet:
            state.ui.hud.toast("Nothing to save until you are on foot", hud_module.WARNING)
        return None
    bubble = foot.bubble
    data = {
        "world": {"terrain": planet.terrain, "time": world.time,
                  "taken": [list(key) for key in sorted(world.taken_rocks.keys)]},
        "player": None if player is None else {
            "on": bubble.body.name, "feet": list(bubble.body_relative(player.feet())),
            "heading": list(player.basis()[2]), "pitch": player.pitch},
        "props": props_record(bubble),
        "survival": survival.record(),
        "progress": walking.record(),
        "economy": economy.record(),
        "vehicle": vehicle.record(),
        "exploring": exploring.record(),
        "contracting": contracting.record(),
        "showers": showers.record(),
        "storms": storms.record(),
    }
    data = save.write(base or save_base, data, state.ui.map.grids)
    state.session.last_save = data
    state.session.autosave_in = AUTOSAVE_SECONDS
    if not quiet:
        state.ui.hud.toast("Game saved", GOOD_TOAST)
    print("[walker] saved to %s" % (base or save_base), flush=True)
    return data


def props_record(bubble):
    """Each prop's record (props.Props.records) with what the systems note about it."""
    if bubble is None:
        return []
    found = []
    for record, prop in zip(foot.props.records(bubble), foot.props.all):
        key = id(prop)
        seen, pod, hot = state.economy.seen.get(key), state.exploring.pods.get(key), state.showers.hot.get(key)
        found.append({"prop": record, "seen": None if seen is None else [seen[0], list(seen[1])],
                      "pod": None if pod is None else list(pod), "courier": state.contracts.couriers.get(key),
                      "hot": None if hot is None else list(hot), "meteorite": prop is state.showers.meteorite})
    return found


def restore_props(records, by_name):
    """The props as a save's "props" (`props_record`) has them, and what the systems note about each."""
    foot.props.clear(None)
    state.economy.seen, state.exploring.pods, state.showers.hot = {}, {}, {}
    state.contracts.couriers, state.contracts.crate_velocity = {}, {}
    state.showers.meteorite = None
    for entry in records:
        record = entry["prop"]
        if record["kind"] == "crate":
            prop = foot.props.restore(record, by_name)
        else:
            prop = foot.rocks.restore(foot.physics, record, by_name)
        key = id(prop)
        if entry["seen"] is not None:
            state.economy.seen[key] = (entry["seen"][0], tuple(entry["seen"][1]))
        if entry["pod"] is not None:
            state.exploring.pods[key] = tuple(entry["pod"])
        if entry["courier"] is not None:
            state.contracts.couriers[key] = entry["courier"]
        if entry["hot"] is not None:
            state.showers.hot[key] = tuple(entry["hot"])
        if entry["meteorite"]:
            state.showers.meteorite = prop


def load_game(base=None):
    """The game put back as the save at `base` (save_base when None) has
    it: driving the car when it was, else the player comes back once the
    ground under it is known (walking.maybe_spawn). Returns whether there
    was a save to load."""
    try:
        data, arrays = save.read(base or save_base)
    except save.SaveError as error:
        state.ui.hud.toast(str(error).capitalize(), hud_module.WARNING)
        print("[walker] %s" % error, flush=True)
        return False
    if data["world"]["terrain"] != planet.terrain:
        state.ui.hud.toast("The save is of another planet", hud_module.WARNING)
        return False
    by_name = {body.name: body for body in bodies}
    menus.set_paused(False)
    state.vehicle.driving = False
    vehicle.set_fov(settings["fov"])
    foot.drop_bubble()
    world.extra_probes = []
    world.time = data["world"]["time"]
    taken = world.taken_rocks
    taken.clear()
    for key in data["world"]["taken"]:
        taken.add(tuple(key))
    survival.restore(data["survival"])
    restore_props(data["props"], by_name)
    showers.restore(data["showers"], by_name)
    storms.restore(data["storms"])
    exploring.restore(data["exploring"])
    save.restore_grids(state.ui.map, arrays, by_name, Grid)
    walking.restore(data["progress"], by_name)
    economy.restore(data["economy"])
    vehicle.restore(data["vehicle"], by_name)
    contracting.restore(data["contracting"])
    p = data["player"]
    if p is None or data["vehicle"]["driving"]:
        state.session.loading = None
        state.vehicle.driving = True
        state.view.chase.reset()
        vehicle.place_chase(0.0)
    else:
        body = by_name[p["on"]]
        feet = add(body.centre(), tuple(p["feet"]))
        state.session.loading = p
        state.view.position = add(feet, scale(normalize(p["feet"]), 2.0))
        state.view.forward = tuple(p["heading"])
    world.ground = None
    state.session.last_save = data
    state.session.autosave_in = AUTOSAVE_SECONDS
    print("[walker] loaded %s" % (base or save_base), flush=True)
    return True


def autosave(dt):
    """With autosave on, the game saved after AUTOSAVE_SECONDS of play."""
    if not settings["autosave"] or args.check or foot.player is None or state.ui.system.visible:
        return
    state.session.autosave_in -= dt
    if state.session.autosave_in <= 0.0:
        save_game(quiet=True)
        state.ui.hud.toast("Autosaved", hud_module.DIM)

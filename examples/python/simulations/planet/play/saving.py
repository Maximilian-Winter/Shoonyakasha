"""
Saving and loading the game (save.py): what a save holds and how it is
put back, and autosave.
"""

from shoonyakasha.planet import world as planet_world
from shoonyakasha.planet.vec import add, normalize, scale

import contracts
import events
import flight
import hovercar
import save
import suit
import upgrades
from explored_map import Grid
import hud as hud_module
from play.core import (AUTOSAVE_SECONDS, GOOD_TOAST, SHIP_HALF, args, bodies, foot, planet, save_base, settings, state,
                       world)
from play import contracting, economy, exploring, menus, showers, site, spaceflight, survival, vehicle

def save_game(base=None, quiet=False):
    """The game written to `base` (save_base when None): the clock, the
    player (None while driving), the car, the props and the taken rocks,
    the market, the game's numbers, the waypoint, the day's offset
    (clock.py), the contracts (the active ones, today's offers accepted,
    the pin, the reputations and the counts; which crates are couriers'),
    the explored map, the meteorites' craters ("modifiers"), the next
    shower's start ("events"; a shower under way, its start) and which
    props are hot and since when, which is the last meteorite, and the log
    and the ledger (gamelog.py).
    Returns the data, or None without a player or the car driven."""
    player = foot.player
    if player is None and not state["driving"]:
        if not quiet:
            state["hud"].toast("Nothing to save until you are on foot", hud_module.WARNING)
        return None
    bubble = foot.bubble
    waypoint = state["waypoint"]
    car = state["car"]
    data = {
        "terrain": planet.terrain,
        "time": world.time,
        "player": None if player is None else {
            "on": bubble.body.name, "feet": list(bubble.body_relative(player.feet())),
            "heading": list(player.basis()[2]), "pitch": player.pitch},
        "car": {"position": list(car.position), "heading": list(car.heading), "target": car.target,
                "driving": state["driving"], "fuel": car.fuel, "body": spaceflight.car_body().name,
                "space": space_record(),
                "cargo": [dict(item, direction=list(item["direction"])) for item in car.cargo]},
        "taken": [list(key) for key in sorted(world.taken_rocks.keys)],
        "props": foot.props.records(bubble) if bubble is not None else [],
        "seen": [state["seen"].get(id(p)) for p in foot.props.all],
        "pods": [list(state["pods"][id(p)]) if id(p) in state["pods"] else None for p in foot.props.all],
        "couriers": [state["couriers"].get(id(p)) for p in foot.props.all],
        "contracts": {"active": [contracts.record(c) for c in state["contracts"]],
                      "taken": sorted(i for i in state["offers_taken"]
                                      if i.rsplit("/", 2)[1] == str(contracting.contract_day())),
                      "pinned": state["pinned"], "reputation": dict(state["reputation"]),
                      "done": state["contracts_done"], "failed": state["contracts_failed"]},
        "market": state["market"].record(state["outposts"]),
        "game": {"sold_kg": state["sold_kg"], "earned": state["earned"], "ore_chunks": state["ore_chunks"],
                 "ore_seen": state["ore_seen"], "waypoints": state["waypoints"],
                 "objective": state["objectives"].index},
        "waypoint": None if waypoint is None else [waypoint[0].name, list(waypoint[1])],
        "day_offset": survival.day_time() - world.time,
        "upgrades": sorted(state["owned"]),
        "suit": {"reserve": state["suit"].reserve, "health": state["suit"].health},
        "pois": state["pois"].record(),
        "parts": dict(state["parts"]),
        "survey_data": state["survey_data"],
        "modifiers": [events.crater_record(c, t, next(b.name for b in bodies if b.index == c.body))
                      for c, t in state["craters"]],
        "events": {"next_shower": state["shower"].start if state["shower"] is not None else state["next_shower"],
                   "storm": events.storm_record(world.storm) if world.storm is not None else None},
        "hot": [list(state["hot"][id(p)]) if id(p) in state["hot"] else None for p in foot.props.all],
        "meteorite": next((n for n, p in enumerate(foot.props.all) if p is state["meteorite"]), None),
        "log": state["log"].record(),
        "ledger": state["ledger"].record(),
    }
    data = save.write(base or save_base, data, state["map"].grids)
    state["last_save"] = data
    state["autosave_in"] = AUTOSAVE_SECONDS
    if not quiet:
        state["hud"].toast("Game saved", GOOD_TOAST)
    print("[walker] saved to %s" % (base or save_base), flush=True)
    return data


def load_game(base=None):
    """The game put back as the save at `base` (save_base when None) has
    it (see save_game): driving the car when it was, else the player comes
    back once the ground under it is known (maybe_spawn); a save without
    the car has it parked beside Base. Returns whether there was a save
    to load."""
    try:
        data, arrays = save.read(base or save_base)
    except save.SaveError as error:
        state["hud"].toast(str(error).capitalize(), hud_module.WARNING)
        print("[walker] %s" % error, flush=True)
        return False
    if data["terrain"] != planet.terrain:
        state["hud"].toast("The save is of another planet", hud_module.WARNING)
        return False
    by_name = {body.name: body for body in bodies}
    menus.set_paused(False)
    state["driving"] = False
    vehicle.set_fov(settings["fov"])
    foot.drop_bubble()
    foot.props.clear(None)
    world.extra_probes = []
    world.time = data["time"]
    state["day_offset"] = data.get("day_offset")
    state["fuel_call"] = None
    record = data.get("suit") or {}
    state["suit"] = suit.SuitState(record.get("reserve", 1.0), record.get("health", 1.0))
    taken = world.taken_rocks
    taken.clear()
    for key in data["taken"]:
        taken.add(tuple(key))
    state["seen"] = {}
    state["pods"] = {}
    state["couriers"], state["crate_velocity"] = {}, {}
    state["hot"], restored = {}, []
    none = [None] * len(data["props"])
    for record, seen, pod, courier, hot in zip(data["props"], data["seen"], data.get("pods") or none,
                                               data.get("couriers") or none, data.get("hot") or none):
        if record["kind"] == "crate":
            prop = foot.props.restore(record, by_name)
        else:
            prop = foot.rocks.restore(foot.physics, record, by_name)
        restored.append(prop)
        if hot is not None:
            state["hot"][id(prop)] = tuple(hot)
        if seen is not None:
            state["seen"][id(prop)] = (seen[0], tuple(seen[1]))
        if pod is not None:
            state["pods"][id(prop)] = tuple(pod)
        if courier is not None:
            state["couriers"][id(prop)] = courier
    meteorite = data.get("meteorite")
    state["meteorite"] = restored[meteorite] if meteorite is not None else None
    state["craters"] = [events.crater_from_record(r, by_name[r["body"]].index, planet_world.Crater)
                        for r in data.get("modifiers", []) if r.get("type") == "crater" and r.get("body") in by_name]
    world.set_craters([c for c, _ in state["craters"]])
    showers.set_shower(None)
    state["shower_body"] = None
    state["next_shower"] = (data.get("events") or {}).get("next_shower")
    storm = (data.get("events") or {}).get("storm")
    world.storm = events.storm_from_record(storm) if storm is not None else None
    state["storm_warned"], state["storm_closed"], state["in_storm"] = set(), set(), False
    state["flash"], state["shake"], state["plumes"] = None, None, []
    state["pois"].restore(data.get("pois", []), exploring.poi_seed())
    state["poi_want_at"], state["poi_asked"], state["poi_asking"] = None, None, []
    state["parts"] = dict(data.get("parts", {}))
    state["survey_data"] = data.get("survey_data", 0.0)
    state["log"].restore(data.get("log"))
    state["ledger"].restore(data.get("ledger"), None)
    save.restore_grids(state["map"], arrays, by_name, Grid)
    state["walked_counted"] = None
    game = data["game"]
    for name in ("sold_kg", "earned", "ore_chunks", "ore_seen", "waypoints"):
        state[name] = game[name]
    state["objectives"].index = game["objective"]
    waypoint = data["waypoint"]
    state["waypoint"] = None if waypoint is None else (by_name[waypoint[0]], tuple(waypoint[1]))
    state["market_record"] = data["market"]
    restore_market()
    state["car"] = None
    economy.apply_upgrades(k for k in data.get("upgrades", []) if k in upgrades.BY_KEY)
    record = data.get("car")
    if record is not None:
        state["car"] = hovercar.Car(tuple(record["position"]), tuple(record["heading"]), record["target"],
                                    params=state["params"]["car"], fuel=record.get("fuel"))
        state["car"].cargo = [dict(item, direction=tuple(item["direction"])) for item in record.get("cargo", [])]
        body = by_name.get(record.get("body", planet.name), planet)
        state["car_body"] = None if body is planet else body
        restore_space(record.get("space"))
        vehicle.update_ship()
    else:
        position, _, _, north = site.ship_pose()
        vehicle.make_car(position, north)
    kept = data.get("contracts") or {}
    state["contracts"] = [contracts.restore(r) for r in kept.get("active", [])]
    state["offers_taken"] = set(kept.get("taken", []))
    state["pinned"] = kept.get("pinned")
    state["reputation"] = dict(kept.get("reputation", {}))
    state["contracts_done"], state["contracts_failed"] = kept.get("done", 0), kept.get("failed", 0)
    held = {i.get("contract") for i in state["car"].cargo}
    for c in state["contracts"]:
        if c.kind == "recovery":
            contracting.place_pod(c, done=contracting.pod_key(c) in state["pods"].values() or c.id in held)
    state["car_poses"] = None
    p = data["player"]
    if p is None or (record is not None and record["driving"]):
        state["loading"] = None
        state["driving"] = True
        state["chase"].reset()
        vehicle.place_chase(0.0)
    else:
        body = by_name[p["on"]]
        feet = add(body.centre(), tuple(p["feet"]))
        state["loading"] = p
        state["position"] = add(feet, scale(normalize(p["feet"]), 2.0))
        state["forward"] = tuple(p["heading"])
    world.ground = None
    state["last_save"] = data
    state["autosave_in"] = AUTOSAVE_SECONDS
    print("[walker] loaded %s" % (base or save_base), flush=True)
    return True


def restore_market():
    """A loaded save's credits and supplies put back once the outposts' survey is done."""
    record = state["market_record"]
    if record is None or state["survey"] is not None:
        return
    state["market"].restore(record, state["outposts"])
    state["market_record"] = None


def space_record():
    """The car's space mode for the save: the ship's position and velocity
    (from the planet's centre), orientation and spin, and its controls; or None."""
    ship = state["space"]
    if ship is None:
        return None
    c = state["space_controls"]
    return {"position": list(ship.position), "velocity": list(ship.velocity), "orientation": list(ship.orientation),
            "spin": list(ship.angular_velocity), "assist": c.assist, "throttle": c.throttle}


def restore_space(record):
    """The car's space mode from a save's record (space_record), or none."""
    state["space"], state["warp"] = None, 0
    if record is None:
        return
    car = state["car"]
    state["space"] = flight.Ship(tuple(record["position"]), tuple(record["velocity"]), tuple(record["orientation"]),
                                 tuple(record["spin"]), params=hovercar.space_params(state["params"]["car"], SHIP_HALF),
                                 fuel=car.fuel, cargo=car.cargo)
    state["space_controls"] = flight.Controls(throttle=record["throttle"], assist=record["assist"])
    state["ship_time"] = world.time


def autosave(dt):
    """With autosave on, the game saved after AUTOSAVE_SECONDS of play."""
    if not settings["autosave"] or args.check or foot.player is None or state["system"].visible:
        return
    state["autosave_in"] -= dt
    if state["autosave_in"] <= 0.0:
        save_game(quiet=True)
        state["hud"].toast("Autosaved", hud_module.DIM)

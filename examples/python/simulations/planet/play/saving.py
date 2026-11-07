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
from play.state import AUTOSAVE_SECONDS
from play.core import (GOOD_TOAST, SHIP_HALF, args, bodies, foot, planet, save_base, settings, state,
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
    if player is None and not state.vehicle.driving:
        if not quiet:
            state.ui.hud.toast("Nothing to save until you are on foot", hud_module.WARNING)
        return None
    bubble = foot.bubble
    waypoint = state.progress.waypoint
    car = state.vehicle.car
    data = {
        "terrain": planet.terrain,
        "time": world.time,
        "player": None if player is None else {
            "on": bubble.body.name, "feet": list(bubble.body_relative(player.feet())),
            "heading": list(player.basis()[2]), "pitch": player.pitch},
        "car": {"position": list(car.position), "heading": list(car.heading), "target": car.target,
                "driving": state.vehicle.driving, "fuel": car.fuel, "body": spaceflight.car_body().name,
                "space": space_record(),
                "cargo": [dict(item, direction=list(item["direction"])) for item in car.cargo]},
        "taken": [list(key) for key in sorted(world.taken_rocks.keys)],
        "props": foot.props.records(bubble) if bubble is not None else [],
        "seen": [state.economy.seen.get(id(p)) for p in foot.props.all],
        "pods": [list(state.exploring.pods[id(p)]) if id(p) in state.exploring.pods else None for p in foot.props.all],
        "couriers": [state.contracts.couriers.get(id(p)) for p in foot.props.all],
        "contracts": {"active": [contracts.record(c) for c in state.contracts.active],
                      "taken": sorted(i for i in state.contracts.offers_taken
                                      if i.rsplit("/", 2)[1] == str(contracting.contract_day())),
                      "pinned": state.contracts.pinned, "reputation": dict(state.contracts.reputation),
                      "done": state.contracts.done, "failed": state.contracts.failed},
        "market": state.economy.market.record(state.site.outposts),
        "game": {"sold_kg": state.economy.sold_kg, "earned": state.economy.earned, "ore_chunks": state.progress.ore_chunks,
                 "ore_seen": state.progress.ore_seen, "waypoints": state.progress.waypoints,
                 "objective": state.progress.objectives.index},
        "waypoint": None if waypoint is None else [waypoint[0].name, list(waypoint[1])],
        "day_offset": survival.day_time() - world.time,
        "upgrades": sorted(state.economy.owned),
        "suit": {"reserve": state.suit.state.reserve, "health": state.suit.state.health},
        "pois": state.exploring.pois.record(),
        "parts": dict(state.economy.parts),
        "survey_data": state.economy.survey_data,
        "modifiers": [events.crater_record(c, t, next(b.name for b in bodies if b.index == c.body))
                      for c, t in state.showers.craters],
        "events": {"next_shower": state.showers.shower.start if state.showers.shower is not None else state.showers.next_start,
                   "storm": events.storm_record(world.storm) if world.storm is not None else None},
        "hot": [list(state.showers.hot[id(p)]) if id(p) in state.showers.hot else None for p in foot.props.all],
        "meteorite": next((n for n, p in enumerate(foot.props.all) if p is state.showers.meteorite), None),
        "log": state.progress.log.record(),
        "ledger": state.economy.ledger.record(),
    }
    data = save.write(base or save_base, data, state.ui.map.grids)
    state.session.last_save = data
    state.session.autosave_in = AUTOSAVE_SECONDS
    if not quiet:
        state.ui.hud.toast("Game saved", GOOD_TOAST)
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
        state.ui.hud.toast(str(error).capitalize(), hud_module.WARNING)
        print("[walker] %s" % error, flush=True)
        return False
    if data["terrain"] != planet.terrain:
        state.ui.hud.toast("The save is of another planet", hud_module.WARNING)
        return False
    by_name = {body.name: body for body in bodies}
    menus.set_paused(False)
    state.vehicle.driving = False
    vehicle.set_fov(settings["fov"])
    foot.drop_bubble()
    foot.props.clear(None)
    world.extra_probes = []
    world.time = data["time"]
    state.session.day_offset = data.get("day_offset")
    state.vehicle.fuel_call = None
    record = data.get("suit") or {}
    state.suit.state = suit.SuitState(record.get("reserve", 1.0), record.get("health", 1.0))
    taken = world.taken_rocks
    taken.clear()
    for key in data["taken"]:
        taken.add(tuple(key))
    state.economy.seen = {}
    state.exploring.pods = {}
    state.contracts.couriers, state.contracts.crate_velocity = {}, {}
    state.showers.hot, restored = {}, []
    none = [None] * len(data["props"])
    for record, seen, pod, courier, hot in zip(data["props"], data["seen"], data.get("pods") or none,
                                               data.get("couriers") or none, data.get("hot") or none):
        if record["kind"] == "crate":
            prop = foot.props.restore(record, by_name)
        else:
            prop = foot.rocks.restore(foot.physics, record, by_name)
        restored.append(prop)
        if hot is not None:
            state.showers.hot[id(prop)] = tuple(hot)
        if seen is not None:
            state.economy.seen[id(prop)] = (seen[0], tuple(seen[1]))
        if pod is not None:
            state.exploring.pods[id(prop)] = tuple(pod)
        if courier is not None:
            state.contracts.couriers[id(prop)] = courier
    meteorite = data.get("meteorite")
    state.showers.meteorite = restored[meteorite] if meteorite is not None else None
    state.showers.craters = [events.crater_from_record(r, by_name[r["body"]].index, planet_world.Crater)
                        for r in data.get("modifiers", []) if r.get("type") == "crater" and r.get("body") in by_name]
    world.set_craters([c for c, _ in state.showers.craters])
    showers.set_shower(None)
    state.showers.body = None
    state.showers.next_start = (data.get("events") or {}).get("next_shower")
    storm = (data.get("events") or {}).get("storm")
    world.storm = events.storm_from_record(storm) if storm is not None else None
    state.storms.warned, state.storms.closed, state.storms.inside = set(), set(), False
    state.showers.flash, state.showers.shake, state.showers.plumes = None, None, []
    state.exploring.pois.restore(data.get("pois", []), exploring.poi_seed())
    state.exploring.want_at, state.exploring.asked, state.exploring.asking = None, None, []
    state.economy.parts = dict(data.get("parts", {}))
    state.economy.survey_data = data.get("survey_data", 0.0)
    state.progress.log.restore(data.get("log"))
    state.economy.ledger.restore(data.get("ledger"), None)
    save.restore_grids(state.ui.map, arrays, by_name, Grid)
    state.exploring.walked_counted = None
    game = data["game"]
    state.economy.sold_kg, state.economy.earned = game["sold_kg"], game["earned"]
    state.progress.ore_chunks, state.progress.ore_seen = game["ore_chunks"], game["ore_seen"]
    state.progress.waypoints = game["waypoints"]
    state.progress.objectives.index = game["objective"]
    waypoint = data["waypoint"]
    state.progress.waypoint = None if waypoint is None else (by_name[waypoint[0]], tuple(waypoint[1]))
    state.session.market_record = data["market"]
    restore_market()
    state.vehicle.car = None
    economy.apply_upgrades(k for k in data.get("upgrades", []) if k in upgrades.BY_KEY)
    record = data.get("car")
    if record is not None:
        state.vehicle.car = hovercar.Car(tuple(record["position"]), tuple(record["heading"]), record["target"],
                                    params=state.economy.params["car"], fuel=record.get("fuel"))
        state.vehicle.car.cargo = [dict(item, direction=tuple(item["direction"])) for item in record.get("cargo", [])]
        body = by_name.get(record.get("body", planet.name), planet)
        state.vehicle.body = None if body is planet else body
        restore_space(record.get("space"))
        vehicle.update_ship()
    else:
        position, _, _, north = site.ship_pose()
        vehicle.make_car(position, north)
    kept = data.get("contracts") or {}
    state.contracts.active = [contracts.restore(r) for r in kept.get("active", [])]
    state.contracts.offers_taken = set(kept.get("taken", []))
    state.contracts.pinned = kept.get("pinned")
    state.contracts.reputation = dict(kept.get("reputation", {}))
    state.contracts.done, state.contracts.failed = kept.get("done", 0), kept.get("failed", 0)
    held = {i.get("contract") for i in state.vehicle.car.cargo}
    for c in state.contracts.active:
        if c.kind == "recovery":
            contracting.place_pod(c, done=contracting.pod_key(c) in state.exploring.pods.values() or c.id in held)
    state.vehicle.poses = None
    p = data["player"]
    if p is None or (record is not None and record["driving"]):
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


def restore_market():
    """A loaded save's credits and supplies put back once the outposts' survey is done."""
    record = state.session.market_record
    if record is None or state.site.survey is not None:
        return
    state.economy.market.restore(record, state.site.outposts)
    state.session.market_record = None


def space_record():
    """The car's space mode for the save: the ship's position and velocity
    (from the planet's centre), orientation and spin, and its controls; or None."""
    ship = state.space.ship
    if ship is None:
        return None
    c = state.space.controls
    return {"position": list(ship.position), "velocity": list(ship.velocity), "orientation": list(ship.orientation),
            "spin": list(ship.angular_velocity), "assist": c.assist, "throttle": c.throttle}


def restore_space(record):
    """The car's space mode from a save's record (space_record), or none."""
    state.space.ship, state.space.warp = None, 0
    if record is None:
        return
    car = state.vehicle.car
    state.space.ship = flight.Ship(tuple(record["position"]), tuple(record["velocity"]), tuple(record["orientation"]),
                                 tuple(record["spin"]), params=hovercar.space_params(state.economy.params["car"], SHIP_HALF),
                                 fuel=car.fuel, cargo=car.cargo)
    state.space.controls = flight.Controls(throttle=record["throttle"], assist=record["assist"])
    state.space.time = world.time


def autosave(dt):
    """With autosave on, the game saved after AUTOSAVE_SECONDS of play."""
    if not settings["autosave"] or args.check or foot.player is None or state.ui.system.visible:
        return
    state.session.autosave_in -= dt
    if state.session.autosave_in <= 0.0:
        save_game(quiet=True)
        state.ui.hud.toast("Autosaved", hud_module.DIM)

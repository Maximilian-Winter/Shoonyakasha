"""
The player on foot and what is drawn for them: spawning, the eye and the
view, the mouse and the gamepad, the viewmodel, the HUD and the map with
their markers, the waypoint, the crates, the loose ore rocks, the
objectives, and screenshots.
"""

import math
import os
import time

import numpy as np

from shoonyakasha import keys
from shoonyakasha.planet.bubble import Bubble
from shoonyakasha.planet.vec import add, cross, dot, length, normalize, quat_from_basis, scale, sub
from shoonyakasha.planet.rocks import density as rock_density, rock_colour
from shoonyakasha.planet import resources
from shoonyakasha.planet.world import EXTRA_PROBES, MAX_LEVEL
from shoonyakasha.planet.props import stack_offsets

import contracts
import fieldslate as fs
import pois
import hovercar
from trade import price, sale
import hud as hud_module
from hud import bearing
from objectives import progress_text, sale_items
from play.core import (CRATE_SIZE, GOOD_TOAST, HERE, HOVER_BACK, METEOR_MARKER, SPACE_HEIGHT, SPAWN_LIFT,
                       STORM_WARNING, bodies, engine, foot, planet, state, world)
from play import contracting, exploring, showers, site, spaceflight, storms, survival, vehicle

CAPTURES = os.path.join(HERE, "walker_captures")      # P
LOAD_LIFT = 0.3                  # m above the ground a loaded player is put down
CAPTURE_TOAST_FRAMES = 3         # frames after a screenshot before its toast
HOME_MARKER = fs.HOLO
OUTPOST_MARKER = fs.ICE
WAYPOINT_MARKER = fs.AMBER
WAYPOINT_REACH = 15.0           # m along the ground from the waypoint it counts as reached
CRATE_MASS = 40.0               # kg: a plain punt throws one at 30 m/s, hard enough to break ore
CRATE_STACKS = ((-9.0, -6.0, 3), (-10.5, -4.0, 3))   # (east, north of the site, crates)
ORE_SEEN_RANGE = 8.0            # m from the feet an ore rock counts as found
SHIP_MARKER = fs.HOLO
CONTRACT_MARKER = fs.AMBER
STORM_MARKER = fs.CORAL


def maybe_spawn():
    """The player put down once the terrain is built, the site's height and
    the ground under the camera are known: at the start beside the ship,
    or LOAD_LIFT above the ground where a loaded save had it; not while the
    start's menu is up or in the car."""
    if (foot.player is not None or planet.tree is None or world.site_height is None or state["starting"]
            or state["driving"]):
        return
    if state["ship"] is None:
        position, _, _, north = site.ship_pose()
        vehicle.make_car(position, north)
        if state["loading"] is None:
            state["position"] = add(site.spawn_point(), scale(state["ship"][2], 2.0))
            state["forward"] = state["ship"][3]
        world.ground = None
        return
    if world.ground is None or world.height_above_ground(state["position"]) > Bubble.RADIUS:
        return
    loading = state["loading"]
    if loading is not None and world.body_under(state["position"]).name != loading["on"]:
        return
    bubble = foot.ensure_bubble(state["position"])
    up = normalize(bubble.body.relative(state["position"]))
    lift = SPAWN_LIFT if loading is None else LOAD_LIFT
    feet = add(bubble.body.centre(), scale(up, bubble.body.radius + world.surface_height(state["position"]) + lift))
    foot.spawn(feet, state["forward"])
    place_eye()
    if loading is None:
        place_crates()
        goal = state["objectives"].current()
        state["hud"].toast("F1 shows the controls")
        state["hud"].toast("%s. %s" % (goal.title, goal.hint))
        print("[walker] on foot at Base", flush=True)
    else:
        foot.player.pitch = loading["pitch"]
        state["loading"] = None
        state["hud"].toast(loading.get("toast", "Game loaded"), GOOD_TOAST)
        print("[walker] on foot where the save had the player", flush=True)


def place_eye():
    eye = foot.place_eye()
    if eye is not None:
        state["position"] = eye
        state["forward"] = foot.view[2]


def view_basis():
    if state["driving"] and state["chase_basis"] is not None:
        return state["chase_basis"]
    if foot.view is not None:
        return foot.view
    forward = normalize(state["forward"])
    up = normalize(world.body_under(state["position"]).relative(state["position"]))
    right = normalize(cross(forward, up))
    return right, cross(right, forward), forward


def capture_mouse():
    """The mouse captured while `hud_shown`, free otherwise; captured again
    by a click when it was freed while it should be captured. Each time it
    is captured, the mouse buttons held then are ignored until let go
    (`ignore_held_buttons`)."""
    wanted = hud_shown()
    if wanted != state["captured"]:
        engine.input.set_mouse_captured(wanted)
        state["captured"] = wanted
        state["ignore_buttons"] = wanted
    elif wanted and not engine.input.is_mouse_captured() and engine.input.is_mouse_button_down(keys.MOUSE_LEFT):
        engine.input.set_mouse_captured(True)
        state["ignore_buttons"] = True


def ignore_held_buttons():
    """While the mouse buttons held when the mouse was captured are still
    held, the gun's controls they give cleared: the click that closed a
    menu or captured the mouse does not grab or punt."""
    if not state["ignore_buttons"]:
        return
    if engine.input.is_mouse_button_down(keys.MOUSE_LEFT) or engine.input.is_mouse_button_down(keys.MOUSE_RIGHT):
        controls = foot.gun_controls
        controls.grab = controls.punt = controls.punt_held = False
    else:
        state["ignore_buttons"] = False


def read_pad_enter():
    """The gamepad's X gets into the car when near it."""
    if not engine.input.is_gamepad_connected():
        return
    x = engine.input.is_gamepad_button_down(keys.GAMEPAD_X)
    if x and not state["pad_x"]:
        vehicle.enter_car()
    state["pad_x"] = x


def read_bumpers():
    """The gamepad's bumpers change the hold distance, one notch a press."""
    if not engine.input.is_gamepad_connected():
        return
    held = {b for b in (keys.GAMEPAD_LEFT_BUMPER, keys.GAMEPAD_RIGHT_BUMPER)
            if engine.input.is_gamepad_button_down(b)}
    for b in held - state["bumpers"]:
        foot.gun_controls.distance += 1.0 if b == keys.GAMEPAD_RIGHT_BUMPER else -1.0
    state["bumpers"] = held


def set_max_levels():
    for body in bodies:
        if body.tree is not None:
            wanted = foot.max_level(body) or MAX_LEVEL
            if body.tree.max_level != wanted:
                body.tree.max_level = wanted


def place_viewmodel(dt, camera):
    viewmodel, gun, player = state["viewmodel"], foot.gun, foot.player
    viewmodel.show(player is not None)
    if player is None:
        return
    held = None
    if gun.held is not None:
        held = sub(foot.bubble.to_world(foot.physics.get_transform(gun.held)[0]), camera)
    viewmodel.update(dt, view_basis(), foot.look_delta, player, gun, held, camera)


def markers(body):
    """(name, position from the planet's centre, colour) of the outposts
    on `body`, the ship on the planet, the waypoint on `body` and the
    points of interest (poi_markers), the pinned contract's target
    (contract_target) on the planet, an announced shower's centre and the
    last meteorite while it lies whole on `body`, and a storm's centre from
    STORM_WARNING before its start to its end on the planet."""
    found = [(o.name, o.point(), HOME_MARKER if o is state["home"] else OUTPOST_MARKER)
             for o in state["outposts"] if o.body is body]
    if state["ship"] is not None and body is planet:
        found.append(("Ship", state["ship"][0], SHIP_MARKER))
    waypoint = state["waypoint"]
    if waypoint is not None and waypoint[0] is body:
        found.append(("Waypoint", add(body.centre(), scale(waypoint[1], body.radius)), WAYPOINT_MARKER))
    target = contracting.contract_target(contracting.pinned_contract())
    if target is not None and body is planet:
        found.append(("Contract", target, CONTRACT_MARKER))
    if body is showers.shower_body() and state["shower"] is not None and state["shower_warned"]:
        found.append(("Meteors", add(body.centre(), scale(state["shower"].centre, body.radius)), METEOR_MARKER))
    meteorite = showers.meteorite_point()
    if meteorite is not None and world.body_under(meteorite) is body:
        found.append(("Meteorite", meteorite, METEOR_MARKER))
    storm = world.storm
    if body is planet and storm is not None and storm.start - STORM_WARNING <= world.time < storm.end:
        found.append(("Storm", add(planet.centre(), scale(storm.centre_at(world.time), planet.radius)), STORM_MARKER))
    return found + exploring.poi_markers(body)


def hud_shown():
    """On foot or in the car, with the slate and the System screen hidden."""
    return ((foot.player is not None or state["driving"]) and not state["slate"].visible
            and not state["system"].visible)


def update_hud(dt):
    """The HUD while `hud_shown`, with `markers` on the compass: on foot,
    in the car or in its space mode (hud.Frame's modes)."""
    hud, player = state["hud"], foot.player
    shown = hud_shown()
    hud.show(shown)
    if not shown:
        return
    pad = engine.input.is_gamepad_connected()
    common = {"view": view_basis(), "eye": state["position"], "dt": dt, "hour": survival.hour_text(),
              "flash": showers.flash_tint(), "fov": world.fov}
    if state["driving"]:
        car, body = state["car"], spaceflight.car_body()
        if state["space"] is not None:
            readout = spaceflight.space_readout()
            readout["dv_full"] = hovercar.delta_v(car.params, car.params.fuel_capacity, car.cargo_mass)
            hints = hud_module.drive_hints(pad, space=True, hover_back=HOVER_BACK)
            hud.update(hud_module.Frame("space", body=body, space=readout, speed=readout["speed"], hints=hints,
                                        **common))
            return
        up = car.up()
        speed = length(sub(car.velocity, scale(up, dot(car.velocity, up))))
        burn = car.burn_rate(state["car_controls"])
        hints = hud_module.drive_hints(pad, car.can_leave(), car.height is not None and car.height >= SPACE_HEIGHT,
                                       space_height=SPACE_HEIGHT)
        hud.update(hud_module.Frame(
            "car", body=body, targets=markers(body), speed=speed, hints=hints, fuel=car.fuel,
            fuel_capacity=car.params.fuel_capacity,
            fuel_range=car.fuel / burn * speed if burn > 1e-6 and speed > 1.0 else None, cargo=car.cargo_mass,
            cargo_capacity=car.params.cargo_capacity, height=car.height, keep=car.target,
            altitude=length(car.position) - body.radius, **common))
        return
    body = foot.bubble.body
    up = foot.bubble.up(player.feet())
    v = foot.physics.character_velocity(player.character)
    gun = foot.gun
    held = gun is not None and gun.held is not None
    prompt, more = reach_prompt(pad)
    suit_state, params, temperature = "ok", state["suit_params"], state["temperature"]
    if temperature is not None and temperature < params.cold_limit:
        suit_state = "cold"
    elif temperature is not None and temperature > params.hot_limit:
        suit_state = "hot"
    hud.update(hud_module.Frame(
        "foot", body=body, targets=markers(body), speed=length(sub(v, scale(up, dot(v, up)))),
        hints=[(("Esc",), "Menu"), (("M",), "Map")], status=gun.status if gun is not None else "idle", held=held,
        charge=gun.charge if held else 0.0, hold_distance=gun.hold_distance if held else None, prompt=prompt,
        prompt_hints=more, height=world.height_above_ground(state["position"]) - player.eye_height,
        jet=player.jet_fuel, reserve=state["suit"].reserve / params.reserve, health=state["suit"].health,
        temperature=temperature, suit_state=suit_state, tint=survival.suit_effects()[0], **common))


def reach_prompt(pad):
    """(the HUD's reach prompt, the hints under it) on foot: what the gun
    aims at that it could grab (its name and what it is worth, or why it
    cannot), else the point of interest's spot in reach (E, with a held
    use's progress), else the car within reach; whichever of these is not
    the prompt among the hints. While the gun holds something, no prompt
    and the gun's hints."""
    gun = foot.gun
    grab, punt = ("pad:LT", "pad:RT") if pad else ("RMB", "LMB")
    if gun is not None and gun.held is not None:
        return None, hud_module.hints("holding", True, gun.charge, pad)
    prompt, more = None, []
    target = gun.target if gun is not None else None
    if target is not None and target.grabbable:
        name, detail, colour = target_text(target)
        status = gun.status
        if status in ("too_heavy", "too_hot"):
            prompt = hud_module.Prompt(grab, "Grab", name, "too heavy" if status == "too_heavy" else "too hot",
                                       hud_module.WARNING)
        else:
            prompt = hud_module.Prompt(grab, "Grab", name, detail, colour)
        more.append(((punt,), "Punt"))
    spot = state["use_spot"]
    if spot is not None:
        poi, k, _ = spot
        hold = pois.SPOTS[poi.kind][k][2]
        word = pois.USE_WORDS[poi.kind]
        if prompt is None:
            progress = state["use_progress"] / hold if hold > 0.0 and state["use_progress"] > 0.0 else None
            prompt = hud_module.Prompt("E", word, poi.name, "hold %.0f s" % hold if hold > 0.0 else "",
                                       hud_module.DIM, progress)
        else:
            more.append((("E",), word))
    if vehicle.near_car():
        key = "pad:X" if pad else "F"
        if prompt is None:
            prompt = hud_module.Prompt(key, "Get in", "Hover car")
        else:
            more.append(((key,), "Get in"))
    return prompt, more


def target_text(target):
    """(name, what it is worth, its colour) of what the gun aims at, a prop or a rock
    of the scatter not yet loose: an ore rock's resource, its mass, grade
    and its price at the nearest open outpost on its body (trade.price for
    its first kilogram), in ACCENT; a plain rock's, a pod's, a courier's or
    another crate's mass, in DIM."""
    mass = "%.0f kg" % target.mass
    prop = foot.props.find(target.body)
    if prop is not None:
        if id(prop) in state["pods"]:
            return "Cargo pod", mass, hud_module.DIM
        if id(prop) in state["couriers"]:
            return "Courier crate", mass, hud_module.DIM
        if prop.kind == "crate":
            return "Crate", mass, hud_module.DIM
        body, position, _ = foot.prop_state(prop)
        seen = state["seen"].get(id(prop), (body.name, normalize(position)))
        kind, grade, resource = prop.kind, prop.grade, prop.resource
    else:
        key = foot.rocks.bodies.get(target.body) if foot.rocks is not None else None
        if key is None:
            return "Rock", mass, hud_module.DIM
        rock, body = foot.rocks.rocks[key], foot.bubble.body
        position = rock.base
        seen = (body.name, normalize(position))
        kind, grade, resource = rock.kind, rock.grade, rock.resource
    if grade <= 0.0:
        return ("Boulder" if kind == "boulder" else "Rock"), mass, hud_module.DIM
    name = resources.name(resource) + (" boulder" if kind == "boulder" else " ore")
    detail = "%s · %.2f" % (mass, grade)
    item = {"body": seen[0], "direction": seen[1], "mass": target.mass, "grade": grade, "resource": resource}
    point = add(body.centre(), position)
    open_ = [o for o in state["outposts"] if o.body is body and not storms.closed(o)]
    if open_:
        outpost = min(open_, key=lambda o: length(sub(o.point(), point)))
        detail += " · ~%s cr" % format(int(round(price(outpost, item, world.time) * target.mass)), ",")
    return name, detail, hud_module.ACCENT


def update_map(dt):
    """The ground around the player (or the car) measured (yielding the
    extra probes to the props, the points of interest and a meteor shower's
    impacts waiting for their ground), the waypoint
    cleared once reached, and the minimap and full map drawn on foot or in
    the car while the menu is hidden."""
    view, player = state["map_view"], foot.player
    shown = (player is not None or state["driving"]) and not state["system"].visible and not state["slate"].visible
    view.show(shown)
    if player is None and not state["driving"]:
        return
    if state["driving"]:
        body, feet = spaceflight.car_body(), state["car"].position
    else:
        body = foot.bubble.body
        feet = foot.bubble.body_relative(player.feet())
    explored = state["map"]
    explored.probe(body, feet, EXTRA_PROBES,
                   yield_to=(foot.bubble is not None and bool(foot.props.needs_ground(foot.bubble)))
                   or bool(state["pois"].queue) or showers.shower_waiting())
    exploring.probe_pois()
    up = normalize(feet)
    waypoint = state["waypoint"]
    if waypoint is not None and waypoint[0] is body and body.radius * math.acos(
            max(-1.0, min(1.0, dot(up, waypoint[1])))) <= WAYPOINT_REACH:
        state["waypoint"] = None
        state["waypoints"] += 1
        state["hud"].toast("Waypoint reached")
    if not shown:
        return
    grid = explored.grid(body, up)
    at = grid.index(up)
    heading = bearing(up, view_basis()[2]) or 0.0
    cells = []
    for name, position, colour in markers(body):
        where = grid.index(normalize(sub(position, body.centre())))
        if where is not None:
            cells.append((name, where, colour))
    view.update(dt, grid, at, heading, cells, mini=not state["driving"])


def place_crates():
    """CRATE_STACKS stacks of crates beside the blocks, once."""
    _, up, east, north = world.site_frame()
    ground = world.site_ground()
    bubble = foot.bubble
    right, back = east, scale(north, -1.0)
    for e, n, count in CRATE_STACKS:
        for height in stack_offsets(count, CRATE_SIZE):
            spot = add(ground, add(scale(east, e), add(scale(north, n), scale(up, height + 0.1))))
            foot.props.spawn_crate(bubble, bubble.to_bubble(spot), quat_from_basis(right, up, back), CRATE_SIZE,
                                   mass=CRATE_MASS)



def ore_rock(spot, extent, grade, resource=None):
    """A loose ore rock of the scatter's "rock" kind scaled by `extent`, of
    `grade` and `resource` (ferrite when None), at `spot` (planet space),
    level with the ground; its prop."""
    bubble = foot.bubble
    point = bubble.to_bubble(spot)
    up = bubble.up(point)
    east = normalize(cross((0.0, 1.0, 0.0), up)) if abs(up[1]) < 0.99 else (1.0, 0.0, 0.0)
    points, _ = foot.rocks.kind_data(foot.physics, "rock")
    hull = (points * np.array(extent)).astype(np.float32)
    resource = resources.FERRITE if resource is None else resource
    body = foot.physics.create_convex_hull(hull, point, quat_from_basis(east, up, cross(east, up)),
                                           motion="dynamic", density=rock_density(resource), friction=0.8,
                                           restitution=0.05)
    colour = rock_colour(bubble.body.index, grade, resource)
    entity, glow = foot.rocks.entity("rock", extent, colour, grade, resource)
    radius = float(np.linalg.norm(hull, axis=1).max())
    return foot.props.add_rock(body, entity, 2.0 * radius, radius, None, ("hull", hull, foot.physics.mass(body)),
                               0.5 * extent[1], bubble.body, "rock", tuple(extent), colour, grade, glow, resource)


def settled(prop):
    """Whether a prop has come to rest: resting out of the physics, or asleep in it."""
    return prop.state == "resting" or (prop.body is not None and not foot.physics.is_active(prop.body))


def ore_rocks():
    """(prop, body, position from the body's centre, speed, first seen) of
    each loose ore rock, its speed infinite until it has settled (so it
    does not sell in the air); each first seen now noted with its body's
    name and its direction."""
    seen, found = state["seen"], []
    for prop in foot.props.rocks:
        if prop.grade <= 0.0:
            continue
        body, position, velocity = foot.prop_state(prop)
        if id(prop) not in seen:
            seen[id(prop)] = (body.name, normalize(position))
            if prop.key is None:
                state["ore_chunks"] += 1
        found.append((prop, body, position, length(velocity) if settled(prop) else math.inf, seen[id(prop)]))
    present = {id(p) for p in foot.props.rocks}
    for key in [k for k in seen if k not in present]:
        del seen[key]
    return found


def ore_seen_near(feet):
    """Whether a fixed ore boulder or a loose ore rock lies within ORE_SEEN_RANGE of `feet` (from the body's centre)."""
    body = foot.bubble.body
    for rock in foot.rocks.rocks.values():
        if rock.grade > 0.0 and rock.body == body.index and length(sub(rock.base, feet)) <= ORE_SEEN_RANGE:
            return True
    return False


def update_game():
    """Ore sold where it rests on an open outpost's pad (or delivered to a
    delivery contract that wants it there), the pods sold and the contracts' crates
    delivered, the objectives moved on, the objective card shown (the
    pinned contract's in place of the objective)."""
    hud, player = state["hud"], foot.player
    if state["market_record"] is None:
        state["ledger"].watch(state["market"].credits)
    if player is None:
        return
    feet = foot.bubble.body_relative(player.feet())
    ore = ore_rocks()
    if not state["ore_seen"] and (ore_seen_near(feet) or any(length(sub(p[2], feet)) <= ORE_SEEN_RANGE
                                                               for p in ore)):
        state["ore_seen"] = 1
    held = foot.props.find(foot.gun.held) if foot.gun is not None and foot.gun.held is not None else None
    for outpost, prop, item in sale_items(ore, [o for o in state["outposts"] if not storms.closed(o)], held):
        if contracting.deliver(outpost, item):
            foot.props.remove(foot.bubble, prop, release=False)
            continue
        credits, supplies = sale(outpost, [item], world.time)
        outpost.supply.update(supplies)
        state["market"].credits += credits
        state["earned"] += credits
        state["sold_kg"] += item["mass"]
        state["ledger"].sold(item["resource"], item["mass"])
        foot.props.remove(foot.bubble, prop, release=False)
        hud.toast("Sold %.0f kg of %s (grade %.2f) at %s: +%s cr"
                  % (item["mass"], resources.name(item["resource"]).lower(), item["grade"], outpost.name,
                     format(int(round(credits)), ",")), GOOD_TOAST, kind="money")
    exploring.sell_pods(held)
    contracting.deliver_couriers(held)
    snapshot = game_numbers()
    objectives = state["objectives"]
    for goal in objectives.update(snapshot):
        hud.toast("Objective complete: %s" % goal.title, GOOD_TOAST)
        upcoming = objectives.current()
        if upcoming is not None:
            hud.toast("Next: %s. %s" % (upcoming.title, upcoming.hint))
        else:
            hud.toast("Every objective done. The planet is yours to explore.")
    goal = objectives.current()
    c = contracting.pinned_contract()
    if c is not None:
        fraction = contracts.progress(c) if c.kind in ("delivery", "survey") else None
        hud.pinned("Pinned · " + c.kind, contracts.short_task(c), fraction, contracting.contract_done_text(c),
                   hud_module.due_text(contracting.hours_left(c)))
    elif goal is not None:
        shown, target = objectives.progress(snapshot)
        fraction = None if target == 1 else shown / target
        hud.pinned("Objective %d / %d" % (min(objectives.index + 1, len(objectives.goals)), len(objectives.goals)),
                   goal.title, fraction, progress_text(goal, shown, target))
    else:
        hud.pinned("", None)


def game_numbers():
    return {"cells": sum(grid.measured for grid in state["map"].grids.values()),
            "waypoints": state["waypoints"], "ore_seen": state["ore_seen"], "ore_chunks": state["ore_chunks"],
            "sold_kg": state["sold_kg"], "earned": state["earned"]}


def take_capture(directory=CAPTURES):
    """A screenshot of the window as it is into `directory`, named by the time; a toast names it
    CAPTURE_TOAST_FRAMES later, so that it is not in the picture. Returns its path, or None."""
    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, time.strftime("walker_%Y%m%d_%H%M%S.png"))
    if not engine.capture_screenshot(path):
        state["hud"].toast("The screenshot failed", hud_module.WARNING)
        return None
    print("[walker] screenshot: %s" % path, flush=True)
    state["capture_toast"] = [os.path.basename(path), CAPTURE_TOAST_FRAMES]
    return path


def capture_toast():
    pending = state["capture_toast"]
    if pending is None:
        return
    pending[1] -= 1
    if pending[1] <= 0:
        state["hud"].toast("Screenshot saved: %s" % pending[0], GOOD_TOAST)
        state["capture_toast"] = None


def screenshot(path):
    print("[walker] screenshot: %s" % (path if engine.capture_screenshot(path) else "failed"), flush=True)

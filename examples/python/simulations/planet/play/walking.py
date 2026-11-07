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
import pois
from trade import sale
import hud as hud_module
from hud import bearing
from objectives import progress_text, sale_items
from play.core import (CRATE_SIZE, GOOD_TOAST, HERE, METEOR_MARKER, SPACE_HEIGHT, SPAWN_LIFT, STORM_WARNING, bodies,
                       engine, foot, planet, state, world)
from play import contracting, exploring, showers, site, spaceflight, storms, survival, vehicle

CAPTURES = os.path.join(HERE, "walker_captures")      # P
LOAD_LIFT = 0.3                  # m above the ground a loaded player is put down
CAPTURE_TOAST_FRAMES = 3         # frames after a screenshot before its toast
OUTPOST_MARKER = (0.55, 1.0, 0.62, 1.0)
WAYPOINT_MARKER = (1.0, 0.45, 0.85, 1.0)
WAYPOINT_REACH = 15.0           # m along the ground from the waypoint it counts as reached
CRATE_MASS = 40.0               # kg: a plain punt throws one at 30 m/s, hard enough to break ore
CRATE_STACKS = ((-9.0, -6.0, 3), (-10.5, -4.0, 3))   # (east, north of the site, crates)
ORE_SEEN_RANGE = 8.0            # m from the feet an ore rock counts as found
SHIP_MARKER = (0.55, 0.8, 1.0, 1.0)
CONTRACT_MARKER = (1.0, 0.82, 0.3, 1.0)
STORM_MARKER = (0.7, 0.75, 0.95, 1.0)


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
    found = [(o.name, o.point(), OUTPOST_MARKER) for o in state["outposts"] if o.body is body]
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
    """On foot or in the car, with the full map and the menu hidden."""
    return ((foot.player is not None or state["driving"]) and not state["map_view"].open
            and not state["menu"].visible)


def update_hud(dt):
    """The HUD while `hud_shown`, with `markers` on the compass."""
    hud, player = state["hud"], foot.player
    shown = hud_shown()
    hud.show(shown)
    if not shown:
        return
    pad = engine.input.is_gamepad_connected()
    if state["driving"]:
        car = state["car"]
        up = car.up()
        speed = length(sub(car.velocity, scale(up, dot(car.velocity, up))))
        body = spaceflight.car_body()
        space = state["space"]
        if space is not None:
            speed = length(sub(space.velocity, body.velocity()))
        hud.update(view_basis(), state["position"], body, None, None, markers(body), speed, pad=pad, dt=dt,
                   height=car.height, drive=car.can_leave(), hour=survival.hour_text(),
                   car_fuel=car.fuel / car.params.fuel_capacity, flash=showers.flash_tint(),
                   space=spaceflight.space_lines() if space is not None else None,
                   high=car.height is not None and car.height >= SPACE_HEIGHT)
        return
    body = foot.bubble.body
    targets = markers(body)
    up = foot.bubble.up(player.feet())
    v = foot.physics.character_velocity(player.character)
    speed = length(sub(v, scale(up, dot(v, up))))
    height = world.height_above_ground(state["position"]) - player.eye_height
    extra = [((("pad:X",) if pad else ("F",)), "Get in")] if vehicle.near_car() else []
    spot = state["use_spot"]
    if spot is not None:
        poi, k, _ = spot
        hold = pois.SPOTS[poi.kind][k][2]
        word = "%s %s" % (pois.USE_WORDS[poi.kind], poi.name.lower())
        if hold > 0.0:
            word += " (hold, %.1f / %.0f s)" % (state["use_progress"], hold)
        extra.append((("E",), word))
    hud.update(view_basis(), state["position"], body, player, foot.gun, targets, speed, pad=pad, dt=dt,
               height=height, extra_hints=extra, hour=survival.hour_text(), tint=survival.suit_effects()[0], flash=showers.flash_tint())


def update_map(dt):
    """The ground around the player (or the car) measured (yielding the
    extra probes to the props, the points of interest and a meteor shower's
    impacts waiting for their ground), the waypoint
    cleared once reached, and the minimap and full map drawn on foot or in
    the car while the menu is hidden."""
    view, player = state["map_view"], foot.player
    shown = (player is not None or state["driving"]) and not state["menu"].visible
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
    view.update(dt, grid, at, heading, cells)


def set_waypoint(canvas_point):
    """The waypoint at the cell under a point of the open full map's canvas."""
    view = state["map_view"]
    if foot.player is None or not view.open:
        return False
    where = view.click(canvas_point)
    if where is None:
        return False
    body = foot.bubble.body
    grid = state["map"].grid(body, normalize(foot.bubble.body_relative(foot.player.feet())))
    state["waypoint"] = (body, grid.direction(where[0], where[1]))
    return True


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
        foot.props.remove(foot.bubble, prop, release=False)
        hud.toast("Sold %.0f kg of %s (grade %.2f) at %s: +%s cr"
                  % (item["mass"], resources.name(item["resource"]).lower(), item["grade"], outpost.name,
                     format(int(round(credits)), ",")), GOOD_TOAST)
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
    header = "OBJECTIVE  %d / %d" % (min(objectives.index + 1, len(objectives.goals)), len(objectives.goals))
    rows = [("Credits", "%s cr" % format(int(round(state["market"].credits)), ",")),
            ("Ore sold", "%s kg" % format(int(round(state["sold_kg"])), ","))] + survival.suit_rows()
    c = contracting.pinned_contract()
    if c is not None:
        k = next(n for n, a in enumerate(state["contracts"]) if a is c)
        fraction = contracts.progress(c) if c.kind in ("delivery", "survey") else None
        hud.objective("CONTRACT  %d / %d" % (k + 1, len(state["contracts"])), contracting.contract_title(c), contracts.task(c),
                      fraction, contracting.contract_progress_text(c).upper(), rows)
    elif goal is not None:
        shown, target = objectives.progress(snapshot)
        fraction = None if target == 1 else shown / target
        hud.objective(header, goal.title, goal.hint, fraction, progress_text(goal, shown, target).upper(), rows)
    else:
        hud.objective(header, "All objectives done", "The planet is yours to explore.", None, None, rows)


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

"""
Walker: the planet on foot, as a game. The player starts beside the parked
ship at Base, on the planet of the planet example (planet.py), and walks,
runs, jumps and jets with the gravity gun in hand
(shoonyakasha.planet.on_foot). The ship stands on its gear 16 m south of
the blocks as a landmark, a static hull in the physics. The outposts on
the planet and the moon are found by the same survey as in planet.py and
drawn as pads.

The HUD (hud.py) shows the gun's crosshair and prompts, a compass with the
outposts, the ship and the waypoint, the jet fuel and a readout. The
ground around the player is measured as it explores (explored_map.py) and
shown on a minimap at the top right; M opens the full map, which frees
the mouse and stops the player while open, and on which a left click sets
the waypoint (cleared within WAYPOINT_REACH of it, or by a right click).

The player is put down SPAWN_LIFT above the ground EAST_OF_SHIP east of
the ship, facing north, once the terrain is built and the ground there is
measured. Until then the camera waits 2 m above that spot.

Keys:
  W A S D                walk; left Shift run; left Ctrl crouch
  Space                  jump on the ground, jet in the air
  mouse                  look (captured; Escape frees it, a click captures it again)
  right mouse            gravity gun: grab, or pull from farther; again: drop
  left mouse             punt what is held or in front; hold while holding to charge
  R + mouse              turn the held body
  mouse wheel            the hold distance; on the full map, the zoom
  M                      the full map (Escape closes it); left click a waypoint, right click clears it
  F1                     the controls

Gamepad: left stick walk, right stick look, A jump and jet, left trigger
grab, right trigger punt, the bumpers the hold distance.

    python walker.py
    python walker.py --check [--shot FILE]   walk 10 s north, grab a crate, open the map, report and exit
"""

import argparse
import math
import os

import numpy as np

import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.planet import world as planet_world
from shoonyakasha.planet.bubble import Bubble
from shoonyakasha.planet.on_foot import OnFoot
from shoonyakasha.planet.vec import (add, cross, dot, euler_from_basis, length, normalize, quat_from_basis,
                                     scale, sub)
from shoonyakasha.planet.world import EXTRA_PROBES, FOV, MAX_LEVEL
from shoonyakasha.toolkit.overlay import HelpOverlay

import flight
from outposts import Pads, Survey, home_outpost
from rocks import Rocks
from explored_map import ExploredMap, MapView
from hud import Hud, bearing
from viewmodel import Viewmodel

HERE = os.path.dirname(os.path.abspath(__file__))
SPAWN_LIFT = 1.0                 # m above the ground the player is put down
EAST_OF_SHIP = 6.0               # m east of the ship's centre the player starts
SHIP_SOUTH = 16.0                # m south of the blocks the ship stands
SET_DOWN_DEPTH = 0.01            # m into the ground the ship's lowest gear reaches
SHIP_CLEARANCE = -flight.ShipParams().gear[0][1]   # m from the ship's centre down to its gear
SHIP_HULL = flight.ShipParams().hull + flight.ShipParams().gear
SHIP_MODEL = os.path.join(HERE, "models", "f117", "scene.gltf")
SHIP_MODEL_UNITS = 6.592
SHIP_MODEL_CENTRE = (0.0, 12.7, 9.9)
CHECK_SECONDS = 10.0
SHOT_SECONDS = 1.0               # s of walking before --shot's screenshot
CHECK_CRATE = 20.0               # kg
CHECK_AHEAD = 2.5                # m ahead of the feet the crate is dropped
GRAB_SECONDS = 1.5               # s the check holds the grab
OUTPOST_MARKER = (0.55, 1.0, 0.62, 1.0)
WAYPOINT_MARKER = (1.0, 0.45, 0.85, 1.0)
WAYPOINT_REACH = 15.0           # m along the ground from the waypoint it counts as reached
MAP_SECONDS = 2.0               # s --check keeps the full map open
MAP_CELLS = 200                 # cells --check wants explored by then
SHIP_MARKER = (0.55, 0.8, 1.0, 1.0)

HELP_COLUMNS = [
    [("On foot", [
        (("Mouse",), "Look around (Escape frees the mouse, a click takes it back)"),
        (("W", "A", "S", "D"), "Walk"),
        (("Shift", "Ctrl"), "Run / crouch"),
        (("Space",), "Jump; held in the air: jetpack"),
        (("M",), "Map: wheel to zoom, left click a waypoint, right click to clear"),
        (("F1",), "These controls"),
    ])],
    [("Gravity gun", [
        (("RMB",), "Grab, or pull from farther; again: drop"),
        (("LMB",), "Punt; hold while holding to charge"),
        (("R",), "With the mouse: turn the held body"),
        (("Wheel",), "Hold distance"),
    ])],
]

parser = argparse.ArgumentParser(description="The planet on foot")
parser.add_argument("--check", action="store_true",
                    help="put the player down, walk 10 s north, grab a crate, open the map, report and exit")
parser.add_argument("--shot", metavar="FILE", help="with --check: a screenshot after SHOT_SECONDS of walking")
args = parser.parse_args()

planet_world.compile_shaders()
engine = sk.Engine(title="Shoonyakasha - walker", width=1600, height=900,
                   pipeline_json_path=str(planet_world.PIPELINE))
world = planet_world.PlanetWorld(engine, fov=FOV)
planet, bodies = world.planet, world.bodies


def box():
    """A unit cube centred on the origin, as triangles with flat normals."""
    positions, normals = [], []
    for n in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)):
        u = (n[1], n[2], n[0])
        v = cross(n, u)
        corners = [add(scale(n, 0.5), add(scale(u, su * 0.5), scale(v, sv * 0.5)))
                   for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        for k in (0, 1, 2, 0, 2, 3):
            positions.append(corners[k])
            normals.append(n)
    return positions, normals


foot = OnFoot(engine, world, box(), make_rocks=lambda props: Rocks(engine, world, props))
pads = Pads(engine, box())
blocks = []                      # (entity, offset in planet space from the site's point on the ground)
state = {
    "position": (0.0, 0.0, 0.0),  # the camera, planet space
    "forward": (0.0, 0.0, 1.0),
    "frame_ms": 16.0,             # frame time, smoothed
    "home": None,                 # the outpost at the blocks
    "outposts": [],               # every outpost, the home one first
    "survey": None,               # the outposts' survey while the world measures it
    "ship": None,                 # (position, right, up, forward) of the parked ship, planet space
    "ship_model": None,           # (root entity, mesh entities)
    "ship_body": None,            # the ship's static hull in the bubble
    "viewmodel": None,
    "hud": None,                  # the head-up display (hud.Hud)
    "map": None,                  # the explored ground (explored_map.ExploredMap)
    "map_view": None,             # the minimap and the full map (explored_map.MapView)
    "waypoint": None,             # (body, unit direction from its centre) set on the full map
    "help": None,
    "captured": False,            # whether the mouse was last asked to be captured
    "bumpers": set(),             # the gamepad bumpers held last frame
    "check": None,                # --check: its stage, time in it, and what it measured
    "shot": False,                # --shot's screenshot taken
}


# ── The world ──────────────────────────────────────────────────────────────

def build_site():
    """The Base blocks around the site, in metres east, up and north of it."""
    _, up, east, north = world.site_frame()
    rotation = euler_from_basis(east, up, scale(north, -1.0))
    positions, normals = box()
    pieces = [
        ((0.0, 0.075, 0.0), (20.0, 0.05, 20.0), (0.35, 0.35, 0.38)),    # pad, 5 cm above the ground
        ((0.0, 0.6, 0.0), (1.0, 1.0, 1.0), (0.85, 0.85, 0.85)),
        ((2.0, 0.35, 1.0), (0.5, 0.5, 0.5), (0.85, 0.25, 0.20)),
        ((-6.0, 2.1, 4.0), (4.0, 4.0, 4.0), (0.25, 0.40, 0.85)),
        ((8.0, 15.1, -8.0), (0.5, 30.0, 0.5), (0.90, 0.30, 0.15)),      # mast
    ]
    for (e, u, n), size, colour in pieces:
        entity = engine.create_mesh(positions, normals=normals, name="Block")
        engine.scene.set_rotation(entity, rotation)
        engine.scene.set_scale(entity, size)
        engine.scene.set_material_vec4(entity, "baseColorFactor", colour + (1.0,))
        engine.scene.set_material_float(entity, "gridStrength", 0.0)
        engine.scene.set_render_layer_mask(entity, 1)
        blocks.append((entity, add(scale(east, e), add(scale(up, u), scale(north, n)))))


def begin_survey():
    survey = Survey(bodies, tuple(planet.terrain["seed"]))
    state["survey"] = survey
    world.start_survey(survey.points)


def update_outposts():
    """The home outpost at the site's height; once the survey's heights are
    back, the outposts chosen, their sites flattened and their pads drawn."""
    home = state["home"]
    home.height = world.site_height or 0.0
    survey = state["survey"]
    heights = world.surveyed() if survey is not None else None
    if heights is None:
        return
    found = survey.choose(heights, taken=[], home=home)
    state["outposts"] = [home] + sorted(found, key=lambda o: (o.body.index, o.name))
    world.set_sites([o.site() for o in state["outposts"] if o.flat_radius > 0.0])
    world.end_survey()
    state["survey"] = None
    pads.build(state["outposts"])


def ship_pose():
    """The parked ship: SHIP_SOUTH south of the blocks, its gear
    SET_DOWN_DEPTH into the ground, facing north."""
    _, up, east, north = world.site_frame()
    position = add(world.site_ground(), add(scale(up, SHIP_CLEARANCE - SET_DOWN_DEPTH), scale(north, -SHIP_SOUTH)))
    return position, east, up, north


def spawn_point():
    """The player's start: EAST_OF_SHIP east of the ship, on the sphere's
    radius of the site's height (the ground there comes from the probe)."""
    position, east, up, _ = ship_pose()
    return add(position, add(scale(east, EAST_OF_SHIP), scale(up, -SHIP_CLEARANCE)))


def build_ship_model():
    """The ship's model, its meshes parented to one entity scaled from the
    model's units to metres, its centre (SHIP_MODEL_CENTRE) at the
    entity's origin; hidden until the ship's pose is known."""
    result = engine.load_gltf_scene(SHIP_MODEL, flatten_hierarchy=True)
    if not result.success:
        print("[walker] the ship's model did not load:", result.error, flush=True)
        return
    root = engine.scene.create_entity("ShipModel")
    engine.scene.set_scale(root, (1.0 / SHIP_MODEL_UNITS,) * 3)
    for entity in result.entities:
        engine.scene.set_parent(entity, root)
        engine.scene.set_position(entity, scale(SHIP_MODEL_CENTRE, -1.0))
        engine.scene.set_material_float(entity, "gridStrength", 0.0)
        engine.scene.set_render_layer_mask(entity, 1)
        engine.scene.set_visible(entity, False)
    state["ship_model"] = (root, result.entities)


def place_ship(camera):
    """The ship's model relative to `camera`, and its hull in the bubble
    while the bubble is on the planet and the ship within its reach."""
    ship = state["ship"]
    if ship is None:
        return
    position, right, up, forward = ship
    if state["ship_model"] is not None:
        root, meshes = state["ship_model"]
        for entity in meshes:
            engine.scene.set_visible(entity, True)
        engine.scene.set_position(root, sub(position, camera))
        engine.scene.set_rotation(root, euler_from_basis(scale(right, -1.0), up, scale(forward, -1.0)))
    bubble = foot.bubble
    if bubble is None or bubble.body is not planet or state["ship_body"] is not None:
        return
    p = bubble.to_bubble(position)
    if length(p) < Bubble.RADIUS:
        hull = np.array(SHIP_HULL, dtype=np.float32)
        rotation = quat_from_basis(right, up, scale(forward, -1.0))
        state["ship_body"] = foot.physics.create_convex_hull(hull, p, rotation, motion="static", friction=0.8)


def remove_ship_body():
    body = state["ship_body"]
    if body is not None and foot.physics.contains(body):
        foot.physics.remove(body)
    state["ship_body"] = None


foot.on_drop = remove_ship_body


# ── The player ─────────────────────────────────────────────────────────────

def maybe_spawn():
    """The player put down once the terrain is built, the site's height and
    the ground under the camera are known."""
    if foot.player is not None or planet.tree is None or world.site_height is None:
        return
    if state["ship"] is None:
        state["ship"] = ship_pose()
        state["position"] = add(spawn_point(), scale(state["ship"][2], 2.0))
        state["forward"] = state["ship"][3]
        world.ground = None
        return
    if world.ground is None or world.height_above_ground(state["position"]) > Bubble.RADIUS:
        return
    bubble = foot.ensure_bubble(state["position"])
    up = normalize(bubble.body.relative(state["position"]))
    feet = add(bubble.body.centre(),
               scale(up, bubble.body.radius + world.surface_height(state["position"]) + SPAWN_LIFT))
    foot.spawn(feet, state["forward"])
    place_eye()
    print("[walker] on foot at Base", flush=True)


def place_eye():
    eye = foot.place_eye()
    if eye is not None:
        state["position"] = eye
        state["forward"] = foot.view[2]


def view_basis():
    if foot.view is not None:
        return foot.view
    forward = normalize(state["forward"])
    up = normalize(world.body_under(state["position"]).relative(state["position"]))
    right = normalize(cross(forward, up))
    return right, cross(right, forward), forward


def capture_mouse():
    """The mouse captured on foot, freed while the controls show; after
    Escape freed it, captured again by a click."""
    wanted = foot.player is not None and not state["help"].visible and not state["map_view"].open
    if wanted != state["captured"]:
        engine.input.set_mouse_captured(wanted)
        state["captured"] = wanted
    elif wanted and not engine.input.is_mouse_captured() and engine.input.is_mouse_button_down(keys.MOUSE_LEFT):
        engine.input.set_mouse_captured(True)


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
    viewmodel.update(dt, view_basis(), foot.look_delta, player, gun, held)


def markers(body):
    """(name, position from the planet's centre, colour) of the outposts
    on `body`, the ship on the planet and the waypoint on `body`."""
    found = [(o.name, o.point(), OUTPOST_MARKER) for o in state["outposts"] if o.body is body]
    if state["ship"] is not None and body is planet:
        found.append(("Ship", state["ship"][0], SHIP_MARKER))
    waypoint = state["waypoint"]
    if waypoint is not None and waypoint[0] is body:
        found.append(("Waypoint", add(body.centre(), scale(waypoint[1], body.radius)), WAYPOINT_MARKER))
    return found


def update_hud():
    """The HUD on foot while the controls and the full map are hidden, with
    `markers` on the compass."""
    hud, player = state["hud"], foot.player
    shown = player is not None and not state["help"].visible and not state["map_view"].open
    hud.show(shown)
    if not shown:
        return
    body = foot.bubble.body
    targets = markers(body)
    up = foot.bubble.up(player.feet())
    v = foot.physics.character_velocity(player.character)
    speed = length(sub(v, scale(up, dot(v, up))))
    hud.update(view_basis(), state["position"], body, player, foot.gun, targets, speed,
               pad=engine.input.is_gamepad_connected())


def run_check(dt):
    """--check, once on foot: walk north for CHECK_SECONDS; then a crate of
    CHECK_CRATE kg dropped CHECK_AHEAD ahead, and once it lies still the
    view turned to it and the grab held for GRAB_SECONDS; then the full
    map open for MAP_SECONDS and a waypoint set on it by a click, and the
    map shown half a second more. Passes when the player covered 80 % of
    walking speed's distance and stands on the ground, the gun holds the
    crate, at least MAP_CELLS cells are explored and the waypoint is
    set. With --shot, screenshots while walking (SHOT_SECONDS in), while
    holding (FILE with _hold added) and of the map (with _map added)."""
    player = foot.player
    if player is None:
        return
    check = state["check"]
    if check is None:
        check = state["check"] = {"stage": "walk", "start": foot.bubble.to_world(player.feet()), "time": 0.0}
    check["time"] += dt
    stage, t = check["stage"], check["time"]
    if stage == "walk":
        foot.controls.forward = 1.0
        if args.shot and t >= SHOT_SECONDS and not state["shot"]:
            state["shot"] = True
            screenshot(args.shot)
        if t >= CHECK_SECONDS:
            foot.controls.forward = 0.0
            moved = length(sub(foot.bubble.to_world(player.feet()), check["start"]))
            check["walk_ok"] = moved > 0.8 * CHECK_SECONDS * 4.0 and player.on_ground
            print("[walker] walked %.1f m in %.0f s, on the ground: %s: %s"
                  % (moved, t, player.on_ground, "passed" if check["walk_ok"] else "FAILED"), flush=True)
            right, up, forward = player.basis()
            level = normalize(sub(forward, scale(up, dot(forward, up))))
            spot = add(player.feet(), add(scale(level, CHECK_AHEAD), scale(up, 1.0)))
            check["crate"] = foot.props.spawn_crate(foot.bubble, spot, quat_from_basis(right, up, scale(level, -1.0)),
                                                    mass=CHECK_CRATE)
            check["stage"], check["time"] = "settle", 0.0
    elif stage == "settle":
        crate = check["crate"]
        if (crate.body is not None and not foot.physics.is_active(crate.body)) or t > 5.0:
            check["stage"], check["time"] = "grab", 0.0
    elif stage == "grab":
        crate = check["crate"]
        to = sub(foot.physics.get_transform(crate.body)[0], player.eye())
        up = player.up()
        player.pitch = math.asin(max(-1.0, min(1.0, dot(normalize(to), up))))
        foot.gun_controls.grab = True
        if t >= GRAB_SECONDS:
            held = foot.gun.held == crate.body
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                screenshot(base + "_hold" + ext)
            print("[walker] grabbed the %.0f kg crate %.1f m ahead: %s" % (CHECK_CRATE, CHECK_AHEAD,
                  "passed" if held else "FAILED"), flush=True)
            check["held_ok"] = held
            foot.gun_controls.grab = False
            state["map_view"].set_open(True)
            check["stage"], check["time"] = "map", 0.0
    elif stage == "map":
        if t >= MAP_SECONDS:
            view = state["map_view"]
            width, height = view.ui.get_canvas_size(view.canvas)
            set_waypoint((0.5 * width + 120.0, 0.5 * height - 160.0))
            body = foot.bubble.body
            cells = state["map"].grids[body.name].measured
            waypoint = state["waypoint"]
            map_ok = cells >= MAP_CELLS and waypoint is not None and waypoint[0] is body
            check["stage"], check["time"] = "map shown", 0.0
            check["map_ok"] = map_ok
            print("[walker] %d cells explored, a waypoint set on the map: %s"
                  % (cells, "passed" if map_ok else "FAILED"), flush=True)
    elif stage == "map shown":
        if t >= 0.5:
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                screenshot(base + "_map" + ext)
            os._exit(0 if check["walk_ok"] and check["held_ok"] and check["map_ok"] else 1)


def update_map(dt):
    """The ground around the player measured, the waypoint cleared once
    reached, and the minimap and full map drawn on foot while the
    controls are hidden."""
    view, player = state["map_view"], foot.player
    shown = player is not None and not state["help"].visible
    view.show(shown)
    if player is None:
        return
    body = foot.bubble.body
    feet = foot.bubble.body_relative(player.feet())
    explored = state["map"]
    explored.probe(body, feet, EXTRA_PROBES, yield_to=bool(foot.props.needs_ground(foot.bubble)))
    up = normalize(feet)
    waypoint = state["waypoint"]
    if waypoint is not None and waypoint[0] is body and body.radius * math.acos(
            max(-1.0, min(1.0, dot(up, waypoint[1])))) <= WAYPOINT_REACH:
        state["waypoint"] = None
        print("[walker] waypoint reached", flush=True)
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


def screenshot(path):
    print("[walker] screenshot: %s" % (path if engine.capture_screenshot(path) else "failed"), flush=True)


# ── The frame ──────────────────────────────────────────────────────────────

def frame(dt):
    clock = world.time
    world.begin_frame(state["position"])
    update_outposts()
    maybe_spawn()
    if foot.player is not None:
        if args.check:
            run_check(dt)
        elif not state["map_view"].open:
            foot.read_controls(engine.input)
            read_bumpers()
    world.time += dt
    latency = world.probe_latency * state["frame_ms"] / 1000.0
    foot.props.fly(world.time - clock, latency)
    active = foot.player is not None and not state["map_view"].open
    foot.step(dt, state["position"], state["position"], latency, active=active)
    foot.props.probe(foot.bubble, EXTRA_PROBES)
    place_eye()
    capture_mouse()
    set_max_levels()

    camera = state["position"]
    basis = view_basis()
    foot.request_nearby(camera, bodies)
    world.end_frame(camera, basis, dt, viewpoint=camera)
    ground = world.site_ground()
    for entity, offset in blocks:
        engine.scene.set_position(entity, sub(add(ground, offset), camera))
    pads.draw(camera)
    foot.props.draw(foot.bubble, camera)
    place_ship(camera)
    place_viewmodel(dt, camera)
    update_hud()
    update_map(dt)
    foot.end_frame()
    state["frame_ms"] = 0.9 * state["frame_ms"] + 0.1 * dt * 1000.0
    return True


def on_init():
    world.setup()
    build_site()
    state["home"] = home_outpost(world)
    state["outposts"] = [state["home"]]
    begin_survey()
    build_ship_model()
    state["viewmodel"] = Viewmodel(engine, box())
    state["hud"] = Hud(engine)
    state["hud"].show(False)
    state["map"] = ExploredMap(world)
    state["map_view"] = MapView(engine)
    state["map_view"].show(False)
    state["help"] = HelpOverlay(engine, "CONTROLS", HELP_COLUMNS, footer="F1 to close")
    state["help"].show(False)
    _, up, _, north = world.site_frame()
    state["position"] = add(world.site_ground(), scale(up, 5.0))
    state["forward"] = north
    engine.ecs.add_system("WalkerFrame", frame, priority=-1)


def on_key(key):
    view = state["map_view"]
    if key == keys.F1:
        state["help"].show(not state["help"].visible)
    elif key == keys.ESCAPE and state["help"].visible:
        state["help"].show(False)
    elif key == keys.M and foot.player is not None:
        view.set_open(not view.open)
    elif key == keys.ESCAPE and view.open:
        view.set_open(False)


def on_mouse_button(button, pressed):
    """On the open full map: a left click sets the waypoint, a right click clears it."""
    view = state["map_view"]
    if not pressed or not view.open:
        return
    if button == keys.MOUSE_RIGHT:
        state["waypoint"] = None
    elif button == keys.MOUSE_LEFT:
        x, y = engine.input.get_mouse_position()
        width, height = view.ui.get_canvas_size(view.canvas)
        fb = engine.framebuffer_size
        if fb[0] > 0 and fb[1] > 0:
            set_waypoint((x * width / fb[0], y * height / fb[1]))


def on_scroll(x_offset, y_offset):
    if state["map_view"].open:
        state["map_view"].zoom_by(y_offset)
    elif foot.player is not None:
        foot.gun_controls.distance += y_offset


engine.set_on_init(on_init)
engine.set_on_key_pressed(on_key)
engine.input.set_on_mouse_scroll(on_scroll)
engine.input.set_on_mouse_button(on_mouse_button)
engine.run()

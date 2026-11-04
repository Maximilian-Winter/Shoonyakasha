"""
Walker: the planet on foot, as a game. The player starts beside the parked
ship at Base, on the planet of the planet example (planet.py), and walks,
runs, jumps and jets with the gravity gun in hand
(shoonyakasha.planet.on_foot). The ship stands on its gear 16 m south of
the blocks as a landmark, a static hull in the physics. The outposts on
the planet and the moon are found by the same survey as in planet.py and
drawn as pads.

The interface is in the style of scifi.py. The HUD (hud.py) shows the
gun's crosshair and key hints, a compass with the outposts, the ship and
the waypoint, the objective with the credits and the ore sold, the jet
fuel and a readout. The ground around the player is measured as it
explores (explored_map.py) and shown on a round scope at the top right;
M opens the full map, which frees the mouse and stops the player while
open, and on which a left click sets the waypoint (cleared within
WAYPOINT_REACH of it, or by a right click).

Ore: a loose ore rock (a small one from the scatter, or a chunk of an ore
boulder broken by a strike at rocks.BREAK_SPEED) that comes to rest on an
outpost's pad, not held, sells there (objectives.sale_items, trade.sale),
priced from the body and place it was first seen at. CRATE_STACKS of
CRATE_MASS kg crates stand west of the blocks to throw. Meeting an
objective (objectives.GOALS), a sale and a reached waypoint show as
toasts.

The menu (terminal.py): Escape opens it, and F1 opens it on the
controls; the world's clock and the player stand still behind it. Its
System tab has Resume, Save game, Load game and Quit beside how the game
stands; its Settings tab the settings (config.py), kept in
walker_config.json: the mouse's sensitivity, inverted look, the head bob,
the field of view, the HUD's scale, the window's size (taken at the next
start), the render scale, anti-aliasing, the clouds and autosave; its
Controls tab the controls.

Saving (save.py): F5 or Save game writes the game to walker_save.json
and walker_save.npz: the clock, the player, the crates and loose rocks,
the rocks taken from the scatter, the credits and the outposts' supplies,
the objectives and the game's numbers, the waypoint and the explored
map. F9 or Load game puts it back; the player comes back LOAD_LIFT above
the ground where it stood once that ground is measured. With autosave on,
the game is saved every AUTOSAVE_SECONDS of play, on Quit and as the
window closes. When a save exists, the walker starts on a menu with
Continue (the save loaded) and New game.

The player is put down SPAWN_LIFT above the ground EAST_OF_SHIP east of
the ship, facing north, once the terrain is built and the ground there is
measured. Until then the camera waits 2 m above that spot.

Keys:
  W A S D                walk; left Shift run; left Ctrl crouch
  Space                  jump on the ground, jet in the air
  mouse                  look (captured while playing)
  Escape                 the menu: the world stands still
  P                      screenshot into walker_captures/
  right mouse            gravity gun: grab, or pull from farther; again: drop
  left mouse             punt what is held or in front; hold while holding to charge
  R + mouse              turn the held body
  mouse wheel            the hold distance; on the full map, the zoom
  M                      the full map (Escape closes it); left click a waypoint, right click clears it
  F5, F9                 save, load
  F1                     the menu on the controls

Gamepad: left stick walk, right stick look, A jump and jet, left trigger
grab, right trigger punt, the bumpers the hold distance.

    python walker.py
    python walker.py --check [--shot FILE]   walk, grab and throw a crate, set a waypoint, sell ore,
                                             pause, save, change the game and load it; report and exit
"""

import argparse
import math
import os
import tempfile
import time

import numpy as np

import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.planet import world as planet_world
from shoonyakasha.planet.bubble import Bubble
from shoonyakasha.planet.on_foot import OnFoot
from shoonyakasha.planet.vec import (add, cross, dot, euler_from_basis, length, normalize, quat_from_basis,
                                     scale, sub)
from shoonyakasha.planet.world import EXTRA_PROBES, FOV, MAX_LEVEL
from shoonyakasha.planet.props import stack_offsets
from shoonyakasha.planet.rocks import rock_colour
import config
import flight
import save
from outposts import Pads, Survey, home_outpost
from rocks import Rocks
from trade import Market, sale
from explored_map import ExploredMap, Grid, MapView
import hud as hud_module
from hud import Hud, bearing
from objectives import Objectives, progress_text, sale_items
from terminal import Terminal
from viewmodel import Viewmodel

HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(HERE, "walker_config.json")
CAPTURES = os.path.join(HERE, "walker_captures")      # P
SAVE_BASE = os.path.join(HERE, "walker_save")          # walker_save.json and walker_save.npz
CHECK_SAVE_BASE = os.path.join(tempfile.gettempdir(), "walker_check_save")
CHECK_CAPTURES = os.path.join(tempfile.gettempdir(), "walker_check_captures")
AUTOSAVE_SECONDS = 120.0         # s of play between autosaves
LOAD_LIFT = 0.3                  # m above the ground a loaded player is put down
LOAD_SECONDS = 15.0              # s --check waits for the loaded player
CAPTURE_TOAST_FRAMES = 3         # frames after a screenshot before its toast
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
CHARGE_SECONDS = 1.0             # s --check charges a punt of the held crate
PUNT_SHOT_SECONDS = 0.05         # s after the punt --shot's screenshot of it
OUTPOST_MARKER = (0.55, 1.0, 0.62, 1.0)
WAYPOINT_MARKER = (1.0, 0.45, 0.85, 1.0)
WAYPOINT_REACH = 15.0           # m along the ground from the waypoint it counts as reached
MAP_SECONDS = 2.0               # s --check keeps the full map open
MAP_CELLS = 200                 # cells --check wants explored by then
CRATE_SIZE = 0.6                # m
CRATE_MASS = 40.0               # kg: a plain punt throws one at 30 m/s, hard enough to break ore
CRATE_STACKS = ((-9.0, -6.0, 3), (-10.5, -4.0, 3))   # (east, north of the site, crates)
ORE_SEEN_RANGE = 8.0            # m from the feet an ore rock counts as found
GOOD_TOAST = (0.55, 1.0, 0.62, 1.0)
SELL_SECONDS = 6.0              # s --check waits for the ore it drops on the pad to sell
CHECK_ORE_EXTENT = (0.4, 0.3, 0.4)   # m: the ore rock --check drops on the pad
CHECK_ORE_GRADE = 0.6
PAUSE_SECONDS = 1.0             # s --check keeps the pause menu open
SHIP_MARKER = (0.55, 0.8, 1.0, 1.0)

HELP_COLUMNS = [
    [("On foot", [
        (("Mouse",), "Look around"),
        (("W", "A", "S", "D"), "Walk"),
        (("Shift", "Ctrl"), "Run / crouch"),
        (("Space",), "Jump; held in the air: jetpack"),
        (("M",), "Map: wheel to zoom, left click a waypoint, right click to clear"),
    ]), ("Game", [
        (("Esc",), "Pause"),
        (("P",), "Screenshot into walker_captures"),
        (("F5", "F9"), "Save / load"),
        (("F1",), "These controls"),
    ])],
    [("Gravity gun", [
        (("RMB",), "Grab, or pull from farther; again: drop"),
        (("LMB",), "Punt; hold while holding to charge"),
        (("R",), "With the mouse: turn the held body"),
        (("Wheel",), "Hold distance"),
    ]), ("Gamepad", [
        (("pad:LS", "pad:RS"), "Walk / look"),
        (("pad:A",), "Jump and jet"),
        (("pad:LT", "pad:RT"), "Grab / punt"),
        (("pad:LB", "pad:RB"), "Hold distance"),
    ])],
]

parser = argparse.ArgumentParser(description="The planet on foot")
parser.add_argument("--check", action="store_true",
                    help="walk 10 s north, grab a crate and throw it, set a waypoint on the map, sell ore at Base, "
                         "pause, save, change the game and load it; report and exit")
parser.add_argument("--shot", metavar="FILE", help="with --check: a screenshot after SHOT_SECONDS of walking")
args = parser.parse_args()

settings = config.Config(CONFIG_PATH) if args.check else config.load(CONFIG_PATH)
save_base = CHECK_SAVE_BASE if args.check else SAVE_BASE
planet_world.compile_shaders()
engine = sk.Engine(title="Shoonyakasha - walker", width=settings["window"][0], height=settings["window"][1],
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
    "market": Market(),           # the credits (trade.py)
    "seen": {},                   # id of a loose ore rock: (body name, unit direction) where first seen
    "sold_kg": 0.0,
    "earned": 0.0,                # credits from sales
    "ore_chunks": 0,              # chunks of broken ore boulders seen
    "ore_seen": 0,                # 1 once an ore rock was within ORE_SEEN_RANGE
    "waypoints": 0,               # waypoints reached
    "objectives": Objectives(),
    "menu": None,                 # the menu (terminal.Terminal): pause, settings, controls, the start
    "starting": False,            # the start's menu is up: no player until Continue or New game
    "loading": None,              # a save's player record waiting for the ground under it
    "market_record": None,        # a save's credits and supplies waiting for the outposts' survey
    "autosave_in": AUTOSAVE_SECONDS,
    "last_save": None,            # the data of the save last written or read
    "capture_toast": None,        # [file name, frames to wait] of the toast for a screenshot taken
    "captured": False,            # whether the mouse was last asked to be captured
    "ignore_buttons": False,      # the mouse buttons held when it was captured ignored until let go
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
    restore_market()


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
    the ground under the camera are known: at the start beside the ship,
    or LOAD_LIFT above the ground where a loaded save had it; not while the
    start's menu is up."""
    if foot.player is not None or planet.tree is None or world.site_height is None or state["starting"]:
        return
    if state["ship"] is None:
        state["ship"] = ship_pose()
        if state["loading"] is None:
            state["position"] = add(spawn_point(), scale(state["ship"][2], 2.0))
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
        state["hud"].toast("Game loaded", GOOD_TOAST)
        print("[walker] on foot where the save had the player", flush=True)


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
    on `body`, the ship on the planet and the waypoint on `body`."""
    found = [(o.name, o.point(), OUTPOST_MARKER) for o in state["outposts"] if o.body is body]
    if state["ship"] is not None and body is planet:
        found.append(("Ship", state["ship"][0], SHIP_MARKER))
    waypoint = state["waypoint"]
    if waypoint is not None and waypoint[0] is body:
        found.append(("Waypoint", add(body.centre(), scale(waypoint[1], body.radius)), WAYPOINT_MARKER))
    return found


def hud_shown():
    """On foot, with the full map and the menu hidden."""
    return foot.player is not None and not state["map_view"].open and not state["menu"].visible


def update_hud(dt):
    """The HUD while `hud_shown`, with `markers` on the compass."""
    hud, player = state["hud"], foot.player
    shown = hud_shown()
    hud.show(shown)
    if not shown:
        return
    body = foot.bubble.body
    targets = markers(body)
    up = foot.bubble.up(player.feet())
    v = foot.physics.character_velocity(player.character)
    speed = length(sub(v, scale(up, dot(v, up))))
    height = world.height_above_ground(state["position"]) - player.eye_height
    hud.update(view_basis(), state["position"], body, player, foot.gun, targets, speed,
               pad=engine.input.is_gamepad_connected(), dt=dt, height=height)


def run_check(dt):
    """--check, once on foot: walk north for CHECK_SECONDS, half way taking
    a screenshot into CHECK_CAPTURES as P does; then a crate of
    CHECK_CRATE kg dropped CHECK_AHEAD ahead, and once it lies still the
    view turned to it and the grab held for GRAB_SECONDS; then a punt of
    it charged for CHARGE_SECONDS and let go; PUNT_SHOT_SECONDS later the
    full map open for MAP_SECONDS and a waypoint set on it by a click, and
    the map shown half a second more; then the map closed and an ore rock
    (CHECK_ORE_EXTENT, CHECK_ORE_GRADE) dropped 1 m above the Base pad,
    waiting up to SELL_SECONDS for it to sell and a second more; then the
    menu shown for PAUSE_SECONDS; then the game saved to CHECK_SAVE_BASE,
    the credits, the ore sold and the explored map changed, and the save
    loaded, waiting up to LOAD_SECONDS for the player to come back and a
    second more. Passes when the player covered 80 % of walking speed's
    distance and stands on the ground, the screenshot was written and its
    toast shown, the gun holds the crate, the punt charged and threw it,
    at least MAP_CELLS cells are explored, the waypoint is set, the ore sold for credits, the world's clock stood
    still while paused, and the load brought back the credits, the ore
    sold, the cells, the props and the player within 2 m of where it was
    saved. The settings are the defaults and are not written. With --shot,
    screenshots while walking (SHOT_SECONDS in), while holding (FILE with
    _hold added), while charging (with _charge added), just after the punt
    (with _punt added), of the map (with _map added), after the sale (with
    _sell added), of the menu (with _pause added), of its Settings tab
    (with _settings added) and after the load (with _load added)."""
    player = foot.player
    check = state["check"]
    if player is None and (check is None or check["stage"] != "reload"):
        return
    if check is None:
        check = state["check"] = {"stage": "walk", "start": foot.bubble.to_world(player.feet()), "time": 0.0}
    check["time"] += dt
    stage, t = check["stage"], check["time"]
    if stage == "walk":
        foot.controls.forward = 1.0
        if args.shot and t >= SHOT_SECONDS and not state["shot"]:
            state["shot"] = True
            screenshot(args.shot)
        if t >= 0.5 * CHECK_SECONDS and "capture" not in check:
            check["capture"] = take_capture(CHECK_CAPTURES)
        if t >= CHECK_SECONDS:
            foot.controls.forward = 0.0
            moved = length(sub(foot.bubble.to_world(player.feet()), check["start"]))
            check["walk_ok"] = moved > 0.8 * CHECK_SECONDS * 4.0 and player.on_ground
            print("[walker] walked %.1f m in %.0f s, on the ground: %s: %s"
                  % (moved, t, player.on_ground, "passed" if check["walk_ok"] else "FAILED"), flush=True)
            capture = check["capture"]
            check["capture_ok"] = (capture is not None and os.path.exists(capture) and state["capture_toast"] is None
                                   and any(toast[0].startswith("Screenshot saved") for toast in state["hud"].toasts))
            print("[walker] a screenshot taken as P takes one, and its toast shown: %s"
                  % ("passed" if check["capture_ok"] else "FAILED"), flush=True)
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
            foot.gun_controls.punt = foot.gun_controls.punt_held = True
            check["stage"], check["time"] = "charge", 0.0
    elif stage == "charge":
        if t >= CHARGE_SECONDS:
            check["charge"] = foot.gun.charge
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                screenshot(base + "_charge" + ext)
            foot.gun_controls.punt_held = False
            check["stage"], check["time"] = "punt", 0.0
    elif stage == "punt":
        if t >= PUNT_SHOT_SECONDS:
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                screenshot(base + "_punt" + ext)
            last = foot.gun.last_punt
            punt_ok = (check["charge"] > 0.0 and foot.gun.held is None and last is not None
                       and last[0] == check["crate"].body)
            print("[walker] charged to %.0f %% and threw the crate at %.0f m/s: %s"
                  % (100.0 * check["charge"], last[1] if last is not None and last[1] else 0.0,
                     "passed" if punt_ok else "FAILED"), flush=True)
            check["punt_ok"] = punt_ok
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
            state["map_view"].set_open(False)
            _, up, _, _ = world.site_frame()
            check["credits"] = state["market"].credits
            check["ore"] = ore_rock(add(world.site_ground(), scale(up, 1.0)), CHECK_ORE_EXTENT, CHECK_ORE_GRADE)
            check["stage"], check["time"] = "sell", 0.0
    elif stage == "sell":
        sold = not any(prop is check["ore"] for prop in foot.props.rocks)
        if sold and "sold_at" not in check:
            check["sold_at"] = t
        if t >= check.get("sold_at", SELL_SECONDS) + 1.0 or t >= SELL_SECONDS + 1.0:   # the toast drawn first
            gained = state["market"].credits - check["credits"]
            sell_ok = sold and gained > 0.0 and state["sold_kg"] > 0.0
            print("[walker] ore dropped on the Base pad sold: %.0f kg for %.0f cr: %s"
                  % (state["sold_kg"], gained, "passed" if sell_ok else "FAILED"), flush=True)
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                screenshot(base + "_sell" + ext)
            check["sell_ok"] = sell_ok
            set_paused(True)
            check["stage"], check["time"] = "pause", 0.0
    elif stage == "pause":
        if "clock" not in check:
            check["clock"] = world.time      # this frame is the first that stands still
        if t >= PAUSE_SECONDS:
            pause_ok = world.time == check["clock"] and not hud_shown()
            print("[walker] paused for %.0f s, the world's clock still: %s"
                  % (PAUSE_SECONDS, "passed" if pause_ok else "FAILED"), flush=True)
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                screenshot(base + "_pause" + ext)
                state["menu"].show(True, state["menu"].tab_named("Settings"))
            check["pause_ok"] = pause_ok
            check["stage"], check["time"] = "settings", 0.0
    elif stage == "settings":
        if t >= 0.5 or not args.shot:
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                screenshot(base + "_settings" + ext)
            set_paused(False)
            saved = save_game(CHECK_SAVE_BASE)
            check["saved"] = {"credits": state["market"].credits, "cells": game_numbers()["cells"],
                              "props": len(foot.props.all), "sold_kg": state["sold_kg"],
                              "feet": foot.bubble.to_world(player.feet()), "time": saved["time"]}
            state["market"].credits += 999.0
            state["sold_kg"] += 99.0
            state["map"].grids = {}
            check["loaded"] = load_game(CHECK_SAVE_BASE)
            check["stage"], check["time"] = "reload", 0.0
    elif stage == "reload":
        if player is None:
            if t >= LOAD_SECONDS:
                print("[walker] the loaded player did not come back in %.0f s: FAILED" % LOAD_SECONDS, flush=True)
                os._exit(1)
            return
        check.setdefault("back", t)
        if t - check["back"] >= 1.0:
            saved = check["saved"]
            moved = length(sub(foot.bubble.to_world(player.feet()), saved["feet"]))
            load_ok = (check["loaded"] and state["market"].credits == saved["credits"]
                       and state["sold_kg"] == saved["sold_kg"] and game_numbers()["cells"] == saved["cells"]
                       and len(foot.props.all) == saved["props"] and moved < 2.0)
            print("[walker] saved, changed and loaded: %.0f cr, %d cells, %d props, the player %.2f m from "
                  "where it was saved: %s" % (state["market"].credits, game_numbers()["cells"], len(foot.props.all),
                                              moved, "passed" if load_ok else "FAILED"), flush=True)
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                screenshot(base + "_load" + ext)
            ok = (check["walk_ok"] and check["capture_ok"] and check["held_ok"] and check["punt_ok"] and check["map_ok"]
                  and check["sell_ok"] and check["pause_ok"] and load_ok)
            os._exit(0 if ok else 1)


def update_map(dt):
    """The ground around the player measured, the waypoint cleared once
    reached, and the minimap and full map drawn on foot while the menu
    is hidden."""
    view, player = state["map_view"], foot.player
    shown = player is not None and not state["menu"].visible
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


def ore_rocks():
    """(prop, body, position from the body's centre, speed, first seen) of
    each loose ore rock; each first seen now noted with its body's name and
    its direction."""
    seen, found = state["seen"], []
    for prop in foot.props.rocks:
        if prop.grade <= 0.0:
            continue
        body, position, velocity = foot.prop_state(prop)
        if id(prop) not in seen:
            seen[id(prop)] = (body.name, normalize(position))
            if prop.key is None:
                state["ore_chunks"] += 1
        found.append((prop, body, position, length(velocity), seen[id(prop)]))
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
    """Ore sold where it rests on a pad, the objectives moved on, the objective card shown."""
    hud, player = state["hud"], foot.player
    if player is None:
        return
    feet = foot.bubble.body_relative(player.feet())
    ore = ore_rocks()
    if not state["ore_seen"] and (ore_seen_near(feet) or any(length(sub(p[2], feet)) <= ORE_SEEN_RANGE
                                                               for p in ore)):
        state["ore_seen"] = 1
    held = foot.props.find(foot.gun.held) if foot.gun is not None and foot.gun.held is not None else None
    for outpost, prop, item in sale_items(ore, state["outposts"], held):
        credits, supplies = sale(outpost, [item], world.time)
        outpost.supply.update(supplies)
        state["market"].credits += credits
        state["earned"] += credits
        state["sold_kg"] += item["mass"]
        foot.props.remove(foot.bubble, prop, release=False)
        hud.toast("Sold %.0f kg of ore (grade %.2f) at %s: +%s cr"
                  % (item["mass"], item["grade"], outpost.name, format(int(round(credits)), ",")), GOOD_TOAST)
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
            ("Ore sold", "%s kg" % format(int(round(state["sold_kg"])), ","))]
    if goal is not None:
        shown, target = objectives.progress(snapshot)
        fraction = None if target == 1 else shown / target
        hud.objective(header, goal.title, goal.hint, fraction, progress_text(goal, shown, target).upper(), rows)
    else:
        hud.objective(header, "All objectives done", "The planet is yours to explore.", None, None, rows)


def game_numbers():
    return {"cells": sum(grid.measured for grid in state["map"].grids.values()),
            "waypoints": state["waypoints"], "ore_seen": state["ore_seen"], "ore_chunks": state["ore_chunks"],
            "sold_kg": state["sold_kg"], "earned": state["earned"]}


# ── The menu and the settings ──────────────────────────────────────────────

def set_paused(on, tab="System"):
    """The menu shown on `tab`, or hidden; the world stands still while it shows."""
    menu = state["menu"]
    if not on:
        menu.show(False)
        if not args.check:
            settings.save()
        return
    menu.show(True, menu.tab_named(tab))


def apply_settings():
    """The settings (config.py) put into the walker: the look, the head bob,
    the field of view, the HUD's scale, the render scale, anti-aliasing and
    the clouds."""
    foot.mouse_sensitivity = settings["mouse_sensitivity"]
    foot.invert_y = settings["invert_y"]
    foot.head_bob = settings["head_bob"]
    if world.fov != settings["fov"]:
        world.fov = settings["fov"]
        if world.camera is not None:
            engine.scene.set_camera_fov(world.camera, world.fov)
    s = settings["hud_scale"]
    for part in (state["hud"], state["map_view"], state["menu"]):
        part.set_scale(s)
    engine.render_scale = settings["render_scale"]
    world.taa = settings["anti_aliasing"]
    if world.clouds != settings["clouds"]:
        world.clouds = settings["clouds"]
        world.upload_clouds()


def setting(name):
    """(get, set) of a setting; setting it applies the settings."""
    def set_(value):
        settings.set(name, value)
        apply_settings()
    return (lambda: settings[name]), set_


def slider(label, name, show, hint, step=None):
    get, set_ = setting(name)
    lo, hi = config.RANGES[name]
    item = {"slider": label, "min": lo, "max": hi, "get": get, "set": set_, "show": show, "hint": hint}
    if step:
        item["step"] = step
    return item


def toggle(label, name, hint):
    get, set_ = setting(name)
    return {"toggle": label, "get": get, "set": set_, "hint": hint}


def settings_items():
    """The Settings tab's two columns."""
    get, set_ = setting("window")
    window = {"choice": "Window", "options": [(list(w), "%d x %d" % w) for w in config.WINDOWS], "get": get,
              "set": set_, "hint": "The window's size, taken when the walker next starts."}
    return ("columns", [
        [{"heading": "Input"},
         slider("Mouse sensitivity", "mouse_sensitivity", lambda v: "%.2f x" % v,
                "How far the view turns for the mouse's movement.", 0.05),
         toggle("Invert Y", "invert_y", "Moving the mouse or the right stick up looks down."),
         toggle("Head bob", "head_bob", "The view rises and falls a little with each step."),
         {"heading": "Game"},
         toggle("Autosave", "autosave", "Save every two minutes of play and on quitting."),
         ],
        [{"heading": "View"},
         slider("Field of view", "fov", lambda v: "%.0f deg" % v, "The view's height in degrees.", 1.0),
         slider("HUD scale", "hud_scale", lambda v: "%.2f x" % v, "The size of the HUD, the map and this menu.",
                0.05),
         {"heading": "Graphics"},
         window,
         slider("Render scale", "render_scale", lambda v: "%d %%" % round(100.0 * v),
                "The scene renders at this share of the window's size, then is scaled to it.", 0.05),
         toggle("Anti-aliasing", "anti_aliasing",
                "Temporal anti-aliasing: smooths edges and stops foliage crawling."),
         toggle("Clouds", "clouds", "The cloud layer and its shadows."),
         ],
    ])


def save_note():
    data = state["last_save"]
    return save.describe(data) if data is not None else "No save yet"


def system_items():
    """The System tab: the buttons, and a column of how the game stands."""
    def goal():
        current = state["objectives"].current()
        return current.title if current is not None else "All done"
    return ("columns", [
        [{"button": "Resume", "keys": "ESC", "do": lambda: set_paused(False), "hint": "Back to the game."},
         {"button": "Save game", "keys": "F5", "do": lambda: save_game(), "hint": "Save the game as it is now."},
         {"button": "Load game", "keys": "F9", "do": lambda: load_game(), "hint": "Go back to the last save."},
         {"button": "Quit", "do": quit_game, "hint": "Quit the walker; with autosave on, the game is saved first."},
         {"note": save_note}],
        [{"heading": "Status"},
         {"stat": "Objective", "get": goal},
         {"stat": "Objectives done", "get": lambda: "%d / %d" % (state["objectives"].index,
                                                                 len(state["objectives"].goals))},
         {"stat": "Credits", "get": lambda: "%s cr" % format(int(round(state["market"].credits)), ",")},
         {"stat": "Ore sold", "get": lambda: "%s kg" % format(int(round(state["sold_kg"])), ",")},
         {"stat": "Cells explored", "get": lambda: format(game_numbers()["cells"], ",")},
         {"stat": "Waypoints reached", "get": lambda: str(state["waypoints"])},
         {"stat": "Outposts found", "get": lambda: str(len(state["outposts"]))}],
    ])


def start_items():
    return [
        {"button": "Continue", "do": lambda: start(True), "hint": "Carry on from the save."},
        {"button": "New game", "do": lambda: start(False),
         "hint": "Start again beside the ship; the next save replaces the old one."},
        {"button": "Quit", "do": lambda: os._exit(0)},
        {"note": save_note},
    ]


def menu_tabs():
    return [("System", system_items()), ("Settings", settings_items()), ("Controls", ("controls", HELP_COLUMNS))]


def menu_subtitle():
    body = foot.bubble.body.name if foot.bubble is not None else planet.name
    return "WALKER  //  %s  //  T+%s" % (body.upper(), clock_text(world.time))


def clock_text(seconds):
    s = int(seconds)
    return "%02d:%02d:%02d" % (s // 3600, s // 60 % 60, s % 60)


def start(continue_):
    """From the start's menu: the save loaded, or a new game."""
    menu = state["menu"]
    state["starting"] = False
    menu.title, menu.close_text = "Paused", "ESC  RESUME"
    menu.set_tabs(menu_tabs())
    menu.show(False)
    if continue_:
        load_game()


def quit_game():
    if settings["autosave"] and foot.player is not None and not args.check:
        save_game()
    settings.save()
    os._exit(0)


# ── Saving and loading ─────────────────────────────────────────────────────

def save_game(base=None, quiet=False):
    """The game written to `base` (save_base when None): the clock, the
    player, the props and the taken rocks, the market, the game's numbers,
    the waypoint and the explored map. Returns the data, or None without a
    player."""
    player = foot.player
    if player is None:
        if not quiet:
            state["hud"].toast("Nothing to save until you are on foot", hud_module.WARNING)
        return None
    bubble = foot.bubble
    waypoint = state["waypoint"]
    data = {
        "terrain": planet.terrain,
        "time": world.time,
        "player": {"on": bubble.body.name, "feet": list(bubble.body_relative(player.feet())),
                   "heading": list(player.basis()[2]), "pitch": player.pitch},
        "taken": [list(key) for key in sorted(world.taken_rocks.keys)],
        "props": foot.props.records(bubble),
        "seen": [state["seen"].get(id(p)) for p in foot.props.all],
        "market": state["market"].record(state["outposts"]),
        "game": {"sold_kg": state["sold_kg"], "earned": state["earned"], "ore_chunks": state["ore_chunks"],
                 "ore_seen": state["ore_seen"], "waypoints": state["waypoints"],
                 "objective": state["objectives"].index},
        "waypoint": None if waypoint is None else [waypoint[0].name, list(waypoint[1])],
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
    it (see save_game); the player comes back once the ground under it is
    known (maybe_spawn). Returns whether there was a save to load."""
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
    set_paused(False)
    foot.drop_bubble()
    foot.props.clear(None)
    world.extra_probes = []
    world.time = data["time"]
    taken = world.taken_rocks
    taken.clear()
    for key in data["taken"]:
        taken.add(tuple(key))
    state["seen"] = {}
    for record, seen in zip(data["props"], data["seen"]):
        if record["kind"] == "crate":
            prop = foot.props.restore(record, by_name)
        else:
            prop = foot.rocks.restore(foot.physics, record, by_name)
        if seen is not None:
            state["seen"][id(prop)] = (seen[0], tuple(seen[1]))
    save.restore_grids(state["map"], arrays, by_name, Grid)
    game = data["game"]
    for name in ("sold_kg", "earned", "ore_chunks", "ore_seen", "waypoints"):
        state[name] = game[name]
    state["objectives"].index = game["objective"]
    waypoint = data["waypoint"]
    state["waypoint"] = None if waypoint is None else (by_name[waypoint[0]], tuple(waypoint[1]))
    state["market_record"] = data["market"]
    restore_market()
    p = data["player"]
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


def autosave(dt):
    """With autosave on, the game saved after AUTOSAVE_SECONDS of play."""
    if not settings["autosave"] or args.check or foot.player is None or state["menu"].visible:
        return
    state["autosave_in"] -= dt
    if state["autosave_in"] <= 0.0:
        save_game(quiet=True)
        state["hud"].toast("Autosaved", hud_module.DIM)


def ore_rock(spot, extent, grade):
    """A loose ore rock of the scatter's "rock" kind scaled by `extent`, of
    `grade`, at `spot` (planet space), level with the ground; its prop."""
    bubble = foot.bubble
    point = bubble.to_bubble(spot)
    up = bubble.up(point)
    east = normalize(cross((0.0, 1.0, 0.0), up)) if abs(up[1]) < 0.99 else (1.0, 0.0, 0.0)
    points, _ = foot.rocks.kind_data(foot.physics, "rock")
    hull = (points * np.array(extent)).astype(np.float32)
    body = foot.physics.create_convex_hull(hull, point, quat_from_basis(east, up, cross(east, up)),
                                           motion="dynamic", density=2_600.0, friction=0.8, restitution=0.05)
    colour = rock_colour(bubble.body.index, grade)
    entity, glow = foot.rocks.entity("rock", extent, colour, grade)
    radius = float(np.linalg.norm(hull, axis=1).max())
    return foot.props.add_rock(body, entity, 2.0 * radius, radius, None, ("hull", hull, foot.physics.mass(body)),
                               0.5 * extent[1], bubble.body, "rock", tuple(extent), colour, grade, glow)


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


# ── The frame ──────────────────────────────────────────────────────────────

def frame(dt):
    clock = world.time
    world.begin_frame(state["position"])
    update_outposts()
    maybe_spawn()
    paused = state["menu"].visible
    state["menu"].update(engine.input)
    elapsed = dt                 # the frame's real time; the world's stands still while paused
    if args.check:
        run_check(elapsed)
    elif foot.player is not None and not paused and not state["map_view"].open:
        foot.read_controls(engine.input)
        ignore_held_buttons()
        read_bumpers()
    if paused:
        dt = 0.0
    world.time += dt
    latency = world.probe_latency * state["frame_ms"] / 1000.0
    foot.props.fly(world.time - clock, latency)
    active = foot.player is not None and not state["map_view"].open and not paused
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
    update_hud(dt)
    update_map(dt)
    update_game()
    autosave(elapsed)
    capture_toast()
    foot.end_frame()
    state["frame_ms"] = 0.9 * state["frame_ms"] + 0.1 * elapsed * 1000.0
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
    state["menu"] = Terminal(engine, "Paused", menu_tabs(), subtitle=menu_subtitle, close_text="ESC  RESUME")
    apply_settings()
    if not args.check and save.exists(save_base):
        try:
            state["last_save"] = save.read(save_base)[0]
        except save.SaveError as error:
            print("[walker] %s" % error, flush=True)
    if state["last_save"] is not None:
        menu = state["menu"]
        state["starting"] = True
        menu.title, menu.close_text = "Walker", ""
        menu.set_tabs([("Start", start_items())])
        menu.show(True)
    _, up, _, north = world.site_frame()
    state["position"] = add(world.site_ground(), scale(up, 5.0))
    state["forward"] = north
    engine.ecs.add_system("WalkerFrame", frame, priority=-1)


def on_key(key):
    """F1 the menu on the controls, M the map, Escape the menu, P a screenshot, F5 a save, F9 a load; the
    start's menu waits for its buttons."""
    view, menu = state["map_view"], state["menu"]
    if state["starting"]:
        return
    if key == keys.F1 and foot.player is not None:
        controls = menu.tab_named("Controls")
        set_paused(not (menu.visible and menu.tab == controls), "Controls")
    elif key == keys.M and foot.player is not None and not menu.visible:
        view.set_open(not view.open)
    elif key == keys.ESCAPE and view.open:
        view.set_open(False)
    elif key == keys.P:
        take_capture()
    elif key == keys.ESCAPE and foot.player is not None:
        set_paused(not menu.visible)
    elif key == keys.F5:
        save_game()
    elif key == keys.F9:
        load_game()


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


def on_cleanup():
    """With autosave on, the game saved as the window closes; the settings written."""
    if args.check:
        return
    if settings["autosave"] and foot.player is not None:
        try:
            save_game(quiet=True)
        except Exception as error:      # the engine is shutting down: report it and close anyway
            print("[walker] the game was not saved on closing: %s" % error, flush=True)
    settings.save()


engine.set_on_init(on_init)
engine.set_on_cleanup(on_cleanup)
engine.set_on_key_pressed(on_key)
engine.input.set_on_mouse_scroll(on_scroll)
engine.input.set_on_mouse_button(on_mouse_button)
engine.run()

"""
Shoonyakasha planet: procedural terrain on a cube-sphere with quadtree level
of detail, flown over by a ship

A planet of radius 100 km and its moon, flown around from a few metres
above the ground to thousands of kilometres away, with a free camera or a
ship (flight.py) seen from its cockpit or a chase view, or on foot
(player.py). The planet, the
moon, the terrain's chunks built on the GPU, the atmosphere, the clouds,
the ocean and the shadows are shoonyakasha.planet's PlanetWorld, on the
planet pipeline; the cards, the hint bar, the controls sheet and the
settings menu are shoonyakasha.toolkit.overlay's. Positions are Python
floats (64-bit) with the planet's centre at the origin; the engine's camera
stays at its own origin and only turns. A cluster of blocks stands on the
+Z face, on a pad 5 cm above the ground.

Crates (props.py) are rigid bodies in a physics bubble around the view
(shoonyakasha.planet.bubble, on sk.PlanetPhysics), on the body under it,
falling towards that body's centre onto a mesh of the terrain around the
view (shoonyakasha.planet.collider_probes). The bubble steps at
FLIGHT_STEP in real time, whatever the time warp; it and its crates are
dropped when the view moves to the other body or more than Bubble.RADIUS
above the ground. With a player on foot, the bubble follows the player
instead, and holds it, and the ship while it is within Bubble.RADIUS (a
kinematic hull of its contact points, moved with it). On the planet the
trees and cacti within COLLIDER_RADIUS of the camera get trunks (trunks.py),
and on both bodies the rocks static hulls (rocks.py), from
shoonyakasha.planet.nearby_trees. A rock the gravity gun grabs, pulls or
punts, or a body strikes hard enough, comes loose: it leaves the scatter
and becomes a prop. Besides the fine terrain mesh (32 m) the bubble keeps a
coarse one (256 m, COARSE_SPACING apart), which only props off the fine
one stand on. Props (props.py) leave the physics beyond the bubble's
reach, or low over neither mesh, and fly on around their body in doubles
or rest where they land, coming back in when the bubble reaches them; the
time warp on foot runs while none in the physics is awake.

The player gets out of the ship with F once the ship touches the ground
at less than EXIT_SPEED relative to it, at the boarding point beside the
cockpit, and gets in with F within BOARD_REACH of it. A ship the player
left is parked: it moves with the body under it until boarded. On foot
the time warp (from the menu) runs only while no prop in the physics is
awake, and the body under the player splits to
FOOT_MAX_LEVEL instead of MAX_LEVEL, so the ground drawn near the player
follows the terrain the physics stands on more closely. On foot the
player carries a gravity gun (gun.py, drawn by viewmodel.py) that grabs,
holds, turns and punts the crates.

Keys:
    Tab                    free camera / on foot (from the ship: the free camera)
    F                      get out of the ship (landed and slow) / into it (on foot beside it)
  free camera:
    right mouse + move     look around
    W/A/S/D                fly forward, left, back, right
    E / Q                  up / down
    Shift / Ctrl           4x faster / 4x slower
    mouse wheel            speed factor
  on foot (player.py; the mouse is captured, Escape frees it):
    mouse                  look around
    W/A/S/D                walk forward, left, back, right
    Shift / Ctrl           run / crouch
    Space                  jump; held in the air, the jetpack
    right mouse            gravity gun (gun.py): grab, or pull from farther; again: drop
    left mouse             punt what it holds or points at; at the ground, a jump;
                           held while holding, a charged punt (let go to fire)
    T                      track the prop thrown last: its path and a card row
    mouse wheel, , / .     hold distance
    R + mouse              turn what it holds
  on foot, gamepad: left stick walk, right stick look, A jump and jetpack, left / right
    trigger grab / punt, bumpers hold distance, X get in, Y free camera
  ship (flight.py):
    W / S                  throttle up / down
    Backspace              throttle to 0
    arrows, right mouse    pitch and yaw
    A / D                  roll left / right
    E / Q                  thrust up / down
    L                      translation assist on / off
    R                      chase view / cockpit
    U                      the model's cabin / the generated cockpit
    , / .                  time warp down / up (1x to 16x)
  ship, gamepad (adding to the keys):
    left stick             pitch and roll
    right stick            yaw (x), thrust up / down (y)
    right / left trigger   throttle up / down
    A                      translation assist on / off
    B                      throttle to 0
    bumpers                time warp down / up
    Y / Start / Back       free camera or on foot / settings menu / the controls
    X                      chase view / cockpit; in a landed ship, out of it
  anywhere:
    1 - 5                  5 m above the blocks, 1 km, 50 km, 1,000 km, 5,000 km
    6                      above the corner where the +X, +Y and +Z faces meet
    7                      2 km up, 25 km north-west of the blocks, looking further north-west
    8                      the ship at rest beside the blocks
    9                      the ship in a circular orbit 40 km up
    Shift+8                the ship at rest on the moon, between the sun and the planet
    Shift+9                the ship in a circular orbit 10 km above the moon
    0 / Shift+0            drop a crate / a stack of 10 crates in front of the view
    N                      new terrain seed
    M / J                  mountains higher / lower
    H / G                  hills higher / lower
    Z / X                  sun lower / higher, seen from the blocks
    T / Y                  atmosphere denser / thinner
    B / V                  exposure up / down
    F3                     wireframe
    O                      ocean
    C                      colour: terrain, per face, per level, per biome
    K                      freeze the level of detail
    [ / ]                  split factor down / up
    F1                     settings menu
    F2                     the controls
    I                      debug text
    P                      screenshot into planet_captures/

    python planet.py --capture DIR     screenshots at the presets, then exit
    python planet.py --generated-cockpit   start with the generated cockpit instead of the model's cabin
    python planet.py --capture DIR --shots NAME...   only the shots whose names contain one of these
    python planet.py --verify          check the GPU's chunks against cube_sphere.chunk_mesh, then exit
    python planet.py --orbit-check     fly one orbit from preset 9 at 16x, report the drift, then exit
    python planet.py --landing-check   land the ship on a mountain near the site, report how it stands, then exit
    python planet.py --moon-landing-check   the same on the moon
    python planet.py --crack-check     measure the gaps between levels in steep terrain against the skirts, then exit
    python planet.py --morph-check     come down from 20 km, check every swap of chunks happens fully morphed, then exit
    python planet.py --prop-rest-check   drop crates on the planet and the moon, check they come to rest and stay there
    python planet.py --walk-check      walk 200 m circles on the planet and the moon, check the feet stay on the ground
    python planet.py --hold-check      hold a crate with the gravity gun while turning, walking, jumping and rebasing
    python planet.py --punt-check      punt crates and the ground, check the speeds, the recoil and the jump's height
    python planet.py --trunk-check     walk into the trees nearest the blocks, check the feet stop at their trunks
    python planet.py --promotion-check   grab the rocks near the player one by one, check each is drawn once
    python planet.py --orbit-crate-check   a crate in a low orbit of the moon comes round to where it started
"""

import argparse
import math
import os
import random
import time

import numpy as np
import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.planet import biomes, cube_sphere
from shoonyakasha.planet import settings as planet_settings
from shoonyakasha.planet import world as planet_world
from shoonyakasha.planet.bubble import Bubble
from shoonyakasha.planet.collider_probes import ColliderProbes, TerrainCollider
from shoonyakasha.planet.nearby_trees import NearbyTrees, listed_keys
from shoonyakasha.planet.vec import add, cross, dot, euler_from_basis, length, local_frame, normalize, rotate, scale, sub
from shoonyakasha.toolkit.overlay import Card, HelpOverlay, HintBar, SettingsMenu
from shoonyakasha.planet.world import (CLOUD_COVERAGE, COLOUR_MODES, EXTRA_PROBES, FLOATS_PER_VERTEX, FOV, MAX_LEVEL,
                                       MORPH_LEVELS, MORPH_START, ORBIT_POINTS, QUADS, RADIUS, SKIRT_FRACTION, SLOTS,
                                       SUN_AZIMUTH, SUN_ELEVATION, VERTICES_PER_CHUNK)

import flight
from ground import GroundPatch, SIZE as PATCH_SIZE
import cockpit
import gun as gun_module
from gun import Gun, GunControls
from instruments import Instruments
from player import Controls as FootControls, Player
from player import MASS as PLAYER_MASS, RADIUS as PLAYER_RADIUS, RUN as RUN_SPEED, WALK as WALK_SPEED
from props import Props, stack_offsets, yaw_rotation
from rocks import Rocks
from trunks import Trunks
from viewmodel import Viewmodel

HERE = os.path.dirname(os.path.abspath(__file__))
CAPTURES = os.path.join(HERE, "planet_captures")

DAY_SUN = (SUN_ELEVATION, SUN_AZIMUTH)
PIXEL_ANGLE = math.radians(FOV) / 900.0   # radians across a pixel at the window's 900 rows
LOOK_RADIANS_PER_PIXEL = 0.0025
MIN_ALTITUDE = 0.3               # the camera stays this far above the ground
# The ship (flight.py).
FLIGHT_STEP = 1.0 / 120.0        # seconds per physics step
MAX_STEPS_PER_FRAME = 64
WARPS = (1, 2, 4, 8, 16)         # time warps
SHIP_CLEARANCE = -flight.ShipParams().gear[0][1]   # metres from the ship's centre down to its gear
SHIP_REACH = max(math.sqrt(sum(c * c for c in arm))  # metres from the ship's centre to its farthest contact point
                 for arm in flight.ShipParams().gear + flight.ShipParams().hull)
# The ship's model (models/f117, the F-117 by Muhamad Mirza Arrafi, CC-BY-4.0): its units per metre,
# and the ship's centre in its units.
SHIP_MODEL = os.path.join(HERE, "models", "f117", "scene.gltf")
SHIP_MODEL_UNITS = 6.592
SHIP_MODEL_CENTRE = (0.0, 12.7, 9.9)
# In the model's units: the pilot's eye, and the cabin's two multi-function displays on its front
# panel (their centres, width and height, and the panel's up).
PILOT_EYE = (0.0, 22.05, 40.5)
MFD_CENTRES = ((1.62, 20.53, 43.89), (-1.46, 20.53, 43.89))
MFD_SIZE = (1.12, 1.095)
MFD_UP = (0.0, 1.089, -0.119)
MFD_LIFT = 0.003                 # metres the displays' canvases sit in front of the panel
COCKPIT_LOOK_DOWN = 4.0          # degrees the cockpit view looks below the ship's nose
CHASE_UP, CHASE_BACK = 7.0, 32.0  # metres above and behind the ship of the chase view's camera
CHASE_AHEAD = 20.0               # metres ahead of the ship the chase view looks at
PATCH_MARGIN = 4.0               # metres the ground patch reaches beyond the ship
THROTTLE_RATE = 0.5              # throttle per second while W or S is held
MOUSE_STICK = 0.04               # stick per pixel of mouse movement
GAMEPAD_DEAD_ZONE = 0.12         # of a stick's travel, read as 0
# Gamepad buttons and the keys they press: A assist, B throttle to 0, the bumpers time warp,
# Y ship or free camera, X chase view, Start the menu, Back the controls sheet.
GAMEPAD_KEYS = {keys.GAMEPAD_A: keys.L, keys.GAMEPAD_B: keys.BACKSPACE, keys.GAMEPAD_LEFT_BUMPER: keys.COMMA,
                keys.GAMEPAD_RIGHT_BUMPER: keys.PERIOD, keys.GAMEPAD_Y: keys.TAB, keys.GAMEPAD_START: keys.F1,
                keys.GAMEPAD_X: keys.R, keys.GAMEPAD_BACK: keys.F2}
# On foot FOOT_GAMEPAD_KEYS: X gets in, Y the free camera, the bumpers the gun's hold distance. In a landed
# ship X gets out (read_gamepad_buttons).
ORBIT_HEIGHT = 40_000.0          # preset 9's circular orbit
MOON_ORBIT_HEIGHT = 10_000.0     # Shift+9's circular orbit of the moon
MOON_HOVER_HEIGHT = 2_000.0      # Shift+8's ship waits this far above the moon's radius until its ground is known
# The physics bubble and its crates.
MAX_PHYSICS_STEPS_PER_FRAME = 8
CRATE_SIZE = 0.6                 # m along each edge
COLLIDER_RADIUS = 40.0           # m from the camera the trees, cacti and rocks get colliders (trunks.py, rocks.py)
CRATE_DENSITY = 500.0            # kg/m^3
CRATE_AHEAD = 3.0                # m in front of the view a crate is dropped
STACK_COUNT = 10
JUMP_DISTANCE = 50.0             # m the viewpoint may move in a frame and still count as moving, not jumping
STRIKE_MOMENTUM = 300.0          # kg m/s a body must strike a fixed rock with to knock it loose
COARSE_SPACING = 4.0             # m between the coarse terrain mesh's points (65 x 65, 256 m across)
# On foot.
EXIT_SPEED = 1.0                 # m/s relative to the ground the ship may move at for the player to get out
BOARD_REACH = 3.0                # m from the boarding point the player gets in from
BOARD_OUT = 3.5                  # m left of the ship's centre line the boarding point is, outside the hull
SPAWN_LIFT = 1.0                 # m above the ground a player is put down, to fall onto it
HOLD_MARGIN = 1.0                # m inside the terrain mesh's edges the player must be to move
FOOT_MAX_LEVEL = 14              # the deepest level the body under the player splits to on foot (0.6 m quads)
SHIP_HULL_POINTS = np.array(flight.ShipParams().hull + flight.ShipParams().gear, dtype=np.float32)
FOOT_GAMEPAD_KEYS = {keys.GAMEPAD_X: keys.F, keys.GAMEPAD_Y: keys.TAB, keys.GAMEPAD_START: keys.F1,
                     keys.GAMEPAD_BACK: keys.F2, keys.GAMEPAD_LEFT_BUMPER: keys.COMMA,
                     keys.GAMEPAD_RIGHT_BUMPER: keys.PERIOD}

parser = argparse.ArgumentParser(description="Planet with procedural terrain and quadtree level of detail")
parser.add_argument("--capture", metavar="DIR", help="save screenshots at the presets and exit")
parser.add_argument("--shots", nargs="+", metavar="NAME",
                    help="with --capture: only the shots whose names contain one of these")
parser.add_argument("--generated-cockpit", action="store_true",
                    help="start with the generated cockpit (cockpit.py) instead of the model's cabin")
parser.add_argument("--verify", action="store_true",
                    help="check the chunks the GPU built against cube_sphere.chunk_mesh, print the result and exit")
parser.add_argument("--orbit-check", action="store_true",
                    help="fly one orbit from preset 9 at 16x, print how far it drifted and exit")
parser.add_argument("--morph-check", action="store_true",
                    help="come down from 20 km and check every swap of chunks happens fully morphed")
parser.add_argument("--crack-check", action="store_true",
                    help="measure the gaps between chunks of different levels in steep terrain against the skirts")
parser.add_argument("--landing-check", action="store_true",
                    help="land the ship on the highest spot found around the site, print how it stands and exit")
parser.add_argument("--moon-landing-check", action="store_true",
                    help="land the ship on the highest spot found on the moon, print how it stands and exit")
parser.add_argument("--walk-check", action="store_true",
                    help="walk 200 m circles on the planet and the moon, check the feet stay on the ground, and exit")
parser.add_argument("--prop-rest-check", action="store_true",
                    help="drop crates on the planet and the moon, check they come to rest and stay there, and exit")
parser.add_argument("--hold-check", action="store_true",
                    help="hold a crate with the gravity gun while turning, walking, jumping and rebasing, and exit")
parser.add_argument("--punt-check", action="store_true",
                    help="punt crates of 5, 50 and 500 kg and the ground, check speeds, recoil and height, and exit")
parser.add_argument("--trunk-check", action="store_true",
                    help="walk into the trees nearest the blocks, check the feet stop at their trunks, and exit")
parser.add_argument("--orbit-crate-check", action="store_true",
                    help="put a crate in a low orbit of the moon, check it comes round to where it started, and exit")
parser.add_argument("--promotion-check", action="store_true",
                    help="grab the rocks near the player one by one, check each is drawn exactly once, and exit")
args = parser.parse_args()

planet_world.compile_shaders()
engine = sk.Engine(title="Shoonyakasha - planet", width=1600, height=900,
                   pipeline_json_path=str(planet_world.PIPELINE))
world = planet_world.PlanetWorld(engine, fov=FOV)
planet, bodies, pool = world.planet, world.bodies, world.pool


def distance_text(metres):
    return "%.1f m" % metres if abs(metres) < 10_000.0 else "%.1f km" % (metres / 1000.0)


# ── Meshes ─────────────────────────────────────────────────────────────────

def box():
    """A unit cube centred on the origin, as triangles with flat normals."""
    positions, normals = [], []
    for n in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)):
        u = (n[1], n[2], n[0])         # two axes with cross(u, v) == n
        v = cross(n, u)
        corners = [add(scale(n, 0.5), add(scale(u, su * 0.5), scale(v, sv * 0.5)))
                   for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        for k in (0, 1, 2, 0, 2, 3):
            positions.append(corners[k])
            normals.append(n)
    return positions, normals


# ── World ──────────────────────────────────────────────────────────────────

# Each block: (entity, offset in planet space from the site's point on the sphere).
blocks = []
state = {
    "position": (0.0, 0.0, RADIUS + 5.0),
    "forward": (0.0, 0.0, 1.0),
    "speed": 0.0,
    "speed_factor": 1.0,
    "cards": None,               # (the view's card, the debug card)
    "frame_ms": 16.0,            # frame time, smoothed
    "teleport_when_known": None,  # a preset to jump to again once the site's height is known
    "orbit_path": True,          # draw the path the ship would coast along
    "gamepad_held": set(),       # gamepad buttons held last frame
    "reentry_pass": False,       # whether the ReentryGlow pass is enabled
    "mode": "camera",            # "camera": the free camera; "ship": the view from the ship
    "ship": None,
    "controls": flight.Controls(),
    "warp": 0,                   # index into WARPS
    "accumulator": 0.0,          # seconds of flight not yet stepped
    "moon_landing": None,        # Shift+8's spot on the moon (direction from its centre) until its ground is known
    "instruments": None,
    "menu": None,
    "shown_mode": None,          # (mode, chase, generated_cockpit) show_mode last set up
    "chase": False,              # in the ship: the chase view instead of the cockpit
    "generated_cockpit": False,  # the cockpit view in the generated cockpit instead of the model's cabin
    "cockpit": None,             # the generated cockpit's mesh entity
    "cabin_screens": None,       # the displays on the model's cabin's panel (cockpit_screens)
    "ship_model": None,          # (root entity, its mesh entities): the ship's model
    "debug_text": False,
    "hint_bar": None,            # the button hints along the bottom (toolkit.overlay.HintBar)
    "hints": True,               # whether the hint bar shows
    "help": None,                # the controls sheet (toolkit.overlay.HelpOverlay)
    "input": "keys",             # "keys" or "pad": what the hints name, the device last used
    "terrain_edit": None,        # the terrain parameters the menu changes until Apply (planet.settings)
    "scripted_controls": False,  # the ship's controls are set by a check, not read from the keys
    "impact_speed": 0.0,         # m/s, the fastest a contact point reached the surface at this frame
    "bubble": None,              # the physics bubble (shoonyakasha.planet.bubble), while there is one
    "terrain_collider": None,    # its terrain mesh (collider_probes.TerrainCollider)
    "coarse_collider": None,     # its coarse terrain mesh, for far bodies
    "tracked": None,             # the prop whose path is shown (T)
    "physics_accumulator": 0.0,  # seconds of the bubble's physics not yet stepped
    "focus": None,               # the bubble's focus relative to its body last frame
    "player": None,              # the player on foot (player.Player), while there is one
    "foot_controls": FootControls(),
    "foot_basis": None,          # (right, up, forward) of the player's view
    "gun": None,                 # the player's gravity gun (gun.Gun), while there is a player
    "gun_controls": GunControls(),
    "viewmodel": None,           # the gun's viewmodel (viewmodel.Viewmodel)
    "look_delta": (0.0, 0.0),    # pixels the mouse turned the view this frame, on foot
    "punt_held": False,          # the punt button or trigger held last frame
    "trunk_plants": None,        # (terrain version, vegetation on, its density) the trunks were made under
    "head_bob": True,
    "captured": False,           # whether the frame last asked for the mouse captured
    "parked": None,              # (body, position relative to it) of a ship the player left
    "ship_body": None,           # the ship's kinematic hull in the bubble
    "ship_poses": None,          # (position, orientation) of the ship before and after this frame's flight
}
physics = sk.PlanetPhysics()
physics.set_strike_momentum(STRIKE_MOMENTUM)
collider_probes = ColliderProbes(engine)
nearby_trees = NearbyTrees(engine)
trunks = Trunks()
props = Props(engine, box(), world, on_remove=lambda prop: scatter_rocks.released(prop))
scatter_rocks = Rocks(engine, world, props)


def regenerate():
    """The planet built again with its terrain's parameters
    (PlanetWorld.regenerate), and the menu's copy of them reset."""
    world.regenerate()
    if state["terrain_edit"] is not None:
        state["terrain_edit"].reset()


def build_site():
    """Blocks around the site, in metres east, up and north of it."""
    _, up, east, north = world.site_frame()
    rotation = euler_from_basis(east, up, scale(north, -1.0))
    positions, normals = box()
    pieces = [
        # (east, up, north) centre,  (east, up, north) size,  colour
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


def cockpit_view():
    return state["mode"] == "ship" and not state["chase"]


def build_cockpit():
    """The generated cockpit's mesh (cockpit.py), in the camera's space;
    hidden until show_mode shows it."""
    positions, normals, colours = cockpit.cockpit_mesh()
    entity = engine.create_mesh(positions, normals=normals, colors=colours, name="Cockpit")
    engine.scene.set_material_vec4(entity, "baseColorFactor", (1.0, 1.0, 1.0, 1.0))
    engine.scene.set_material_float(entity, "gridStrength", 0.0)
    engine.scene.set_render_layer_mask(entity, 1)
    engine.scene.set_visible(entity, False)
    state["cockpit"] = entity


def generated_cockpit_view():
    return cockpit_view() and state["generated_cockpit"]


def show_mode():
    """What the mode shows: in the ship's cockpit view, the displays and
    the HUD, and in the generated cockpit its mesh."""
    in_cockpit = cockpit_view()
    state["instruments"].show(in_cockpit)
    engine.scene.set_visible(state["cockpit"], generated_cockpit_view())
    if not in_cockpit:
        set_reentry_pass(False)
    state["shown_mode"] = (state["mode"], state["chase"], state["generated_cockpit"])


def build_ship_model():
    """The ship's model, its meshes parented to one entity that is scaled
    from the model's units to metres, each mesh moved so that the ship's
    centre (SHIP_MODEL_CENTRE) is at the entity's origin; hidden until
    place_ship_model shows it."""
    result = engine.load_gltf_scene(SHIP_MODEL, flatten_hierarchy=True)
    if not result.success:
        print("[planet] the ship's model did not load:", result.error, flush=True)
        return
    root = engine.scene.create_entity("ShipModel")
    engine.scene.set_scale(root, (1.0 / SHIP_MODEL_UNITS,) * 3)
    glass = [entity for entity in result.entities if "Glass" in engine.scene.get_name(entity)]
    for entity in result.entities:
        engine.scene.set_parent(entity, root)
        engine.scene.set_position(entity, scale(SHIP_MODEL_CENTRE, -1.0))
        engine.scene.set_material_float(entity, "gridStrength", 0.0)
        engine.scene.set_render_layer_mask(entity, 1)
        engine.scene.set_visible(entity, False)
    state["ship_model"] = (root, result.entities, glass)


def model_to_ship(point):
    """A point in the model's units as metres (right, up, back) from the
    ship's centre."""
    x, y, z = (point[i] - SHIP_MODEL_CENTRE[i] for i in range(3))
    return (-x / SHIP_MODEL_UNITS, y / SHIP_MODEL_UNITS, -z / SHIP_MODEL_UNITS)


def cockpit_eye(ship):
    """The pilot's eye (PILOT_EYE) in the world; in the generated cockpit
    the ship's centre."""
    if state["generated_cockpit"]:
        return ship.position
    right, up, forward = ship.axes()
    r, u, b = model_to_ship(PILOT_EYE)
    return add(ship.position, add(add(scale(right, r), scale(up, u)), scale(forward, -b)))


def cockpit_screens():
    """The multi-function displays as (centre, right, up, (width, height))
    in the camera's space at the pilot's eye (right +X, up +Y, back +Z),
    each MFD_LIFT in front of the panel."""
    eye = model_to_ship(PILOT_EYE)
    _, up_y, up_back = model_to_ship(add(MFD_UP, SHIP_MODEL_CENTRE))
    up = normalize((0.0, up_y, up_back))
    normal = (0.0, -up[2], up[1])
    size = (MFD_SIZE[0] / SHIP_MODEL_UNITS, MFD_SIZE[1] / SHIP_MODEL_UNITS)
    return [(add(sub(model_to_ship(centre), eye), scale(normal, MFD_LIFT)), (1.0, 0.0, 0.0), up, size)
            for centre in MFD_CENTRES]


def place_ship_model(camera):
    """The ship's model where the ship is, relative to `camera`, turned
    with it (the model's nose is its +Z, its up +Y); shown while there is
    a ship, without its canopy glass in the cockpit view and not at all
    in the generated cockpit."""
    if state["ship_model"] is None:
        return
    root, meshes, glass = state["ship_model"]
    ship = state["ship"]
    shown = ship is not None
    for entity in meshes:
        engine.scene.set_visible(entity, shown and not generated_cockpit_view()
                                 and not (entity in glass and cockpit_view()))
    if shown:
        right, up, forward = ship.axes()
        engine.scene.set_position(root, sub(ship.position, camera))
        engine.scene.set_rotation(root, euler_from_basis(scale(right, -1.0), up, scale(forward, -1.0)))


def chase_view(ship):
    """(eye, (right, up, forward)): the chase view's camera CHASE_UP above
    and CHASE_BACK behind the ship along its own axes, looking at the
    point CHASE_AHEAD ahead of it."""
    right, up, forward = ship.axes()
    eye = add(ship.position, add(scale(up, CHASE_UP), scale(forward, -CHASE_BACK)))
    look = normalize(sub(add(ship.position, scale(forward, CHASE_AHEAD)), eye))
    side = normalize(cross(look, up))
    return eye, (side, cross(side, look), look)


def view_basis():
    """(right, up, forward) of the view: the free camera's, the chase
    view's, in the generated cockpit the ship's, or in the model's cabin
    the ship's turned COCKPIT_LOOK_DOWN below its nose."""
    if state["mode"] == "foot" and state["foot_basis"] is not None:
        return state["foot_basis"]
    if state["mode"] != "ship":
        return camera_basis()
    if state["chase"]:
        return chase_view(state["ship"])[1]
    right, up, forward = state["ship"].axes()
    if state["generated_cockpit"]:
        return right, up, forward
    c, s = math.cos(math.radians(COCKPIT_LOOK_DOWN)), math.sin(math.radians(COCKPIT_LOOK_DOWN))
    return right, add(scale(up, c), scale(forward, s)), sub(scale(forward, c), scale(up, s))


def viewpoint():
    """Where heights and the probe under the camera are measured: the ship
    in ship mode, else the camera."""
    return state["ship"].position if state["mode"] == "ship" else state["position"]


def set_reentry_pass(on):
    """The ReentryGlow pass on or off, when that changes."""
    if state["reentry_pass"] != on:
        engine.set_pass_enabled("ReentryGlow", on)
        state["reentry_pass"] = on


def build_cards():
    """The view's card at the top left, the debug card at the bottom left
    and the hint bar along the bottom (toolkit.overlay)."""
    canvas = engine.ui.create_canvas(reference_size=(1600, 900))
    state["cards"] = (Card(engine.ui, canvas, (0, 0), 12), Card(engine.ui, canvas, (0, 1), 16, margin=(24, 76)))
    state["hint_bar"] = HintBar(engine.ui, canvas)


# ── Camera ─────────────────────────────────────────────────────────────────

def camera_basis():
    """(right, up, forward), with the horizon level: up lies in the plane of
    the forward direction and the radial up of the body under the camera."""
    radial = world.radial_up(state["position"])
    forward = state["forward"]
    right = normalize(cross(forward, radial))
    return right, cross(right, forward), forward


def look(dx, dy):
    radial = world.radial_up(state["position"])
    forward = rotate(state["forward"], radial, -dx * LOOK_RADIANS_PER_PIXEL)
    right = normalize(cross(forward, radial))
    pitched = rotate(forward, right, -dy * LOOK_RADIANS_PER_PIXEL)
    if abs(dot(pitched, radial)) < 0.995:
        forward = pitched
    state["forward"] = normalize(forward)


def fly(dt):
    right, up, forward = camera_basis()
    move = (0.0, 0.0, 0.0)
    for key, direction in ((keys.W, forward), (keys.S, scale(forward, -1.0)), (keys.D, right),
                           (keys.A, scale(right, -1.0)), (keys.E, up), (keys.Q, scale(up, -1.0))):
        if engine.input.is_key_down(key):
            move = add(move, direction)
    if length(move) == 0.0:
        return
    speed = max(world.height_above_ground(viewpoint()), 2.0) * 0.5 * state["speed_factor"]
    if engine.input.is_key_down(keys.LEFT_SHIFT):
        speed *= 4.0
    if engine.input.is_key_down(keys.LEFT_CONTROL):
        speed *= 0.25
    state["position"] = add(state["position"], scale(normalize(move), speed * dt))
    state["speed"] = speed


def teleport(preset, moon=False):
    """Presets 1 - 7 move the free camera; 8 and 9 place the ship, with
    `moon` on or around the moon."""
    _, up, east, north = world.site_frame()
    ground = world.site_ground()
    state["teleport_when_known"] = preset if preset in (1, 2, 8) and world.site_height is None else None
    world.ground = None
    world.patch = None
    state["moon_landing"] = None
    if preset in (8, 9):
        remove_player()
        state["parked"] = None
    if preset in (8, 9) and moon:
        state["teleport_when_known"] = None
        place_ship_at_moon(preset)
        return
    if preset in (8, 9):
        place_ship(preset)
        return
    state["mode"] = "camera"
    if preset == 1:
        # 5 m up, 25 m south-west of the blocks, looking at them.
        state["position"] = add(ground, add(scale(up, 5.0), add(scale(east, -18.0), scale(north, -18.0))))
        state["forward"] = normalize(add(add(scale(east, 1.0), scale(north, 1.0)), scale(up, -0.2)))
    elif preset == 2:
        state["position"] = add(ground, scale(up, 1_000.0))
        state["forward"] = normalize(add(north, scale(up, -0.25)))
    elif preset == 7:
        # 2 km above the sphere, 25 km north-west of the blocks, looking further north-west.
        north_west = normalize(sub(north, east))
        spot = normalize(add(up, scale(north_west, 25_000.0 / RADIUS)))
        state["position"] = scale(spot, RADIUS + 2_000.0)
        state["forward"] = normalize(add(north_west, scale(spot, -0.15)))
    elif preset == 6:
        # 8 km above the sphere at the cube corner shared by +X, +Y and +Z, looking down at it.
        corner = normalize((1.0, 1.0, 1.0))
        corner_east, corner_north = local_frame(corner)
        state["position"] = scale(corner, RADIUS + 8_000.0)
        state["forward"] = normalize(add(scale(corner, -0.98), scale(corner_north, 0.2)))
    else:
        height = {3: 50_000.0, 4: 1_000_000.0, 5: 5_000_000.0}[preset]
        state["position"] = scale(up, RADIUS + height)
        state["forward"] = normalize(add(scale(up, -0.98), scale(north, 0.2)))


# ── Ship ───────────────────────────────────────────────────────────────────

def leave_ship():
    """Camera mode, from where the ship is, facing its way with the horizon level."""
    state["mode"] = "camera"
    _, _, forward = state["ship"].axes()
    radial = world.radial_up(state["position"])
    if abs(dot(forward, radial)) > 0.99:
        forward = local_frame(radial)[1]
    state["forward"] = normalize(forward)


def place_ship(preset):
    """8: at rest 16 m south of the blocks, facing north,
    translation assist on. 9: in a circular orbit ORBIT_HEIGHT above the
    blocks, moving east with the nose 40 degrees down, where the planet is,
    assist off."""
    _, up, east, north = world.site_frame()
    if preset == 8:
        position = add(world.site_ground(), add(scale(up, SHIP_CLEARANCE), scale(north, -16.0)))
        forward, velocity, assist = north, (0.0, 0.0, 0.0), True
    else:
        position, assist = scale(up, RADIUS + ORBIT_HEIGHT), False
        velocity = scale(east, flight.circular_speed(world.physics, ORBIT_HEIGHT))
        down = math.radians(40.0)
        forward = normalize(add(scale(east, math.cos(down)), scale(up, -math.sin(down))))
    new_ship(position, velocity, forward, up, assist)


def new_ship(position, velocity, forward, up, assist):
    """The ship at `position` with `velocity`, its nose along `forward`
    and its up as near `up` as square to it allows, in ship mode."""
    up = normalize(sub(up, scale(forward, dot(up, forward))))
    right = cross(forward, up)
    state["ship"] = flight.Ship(position=position, velocity=velocity,
                                orientation=flight.quat_from_basis(right, up, scale(forward, -1.0)))
    state["controls"] = flight.Controls(assist=assist)
    state["mode"] = "ship"
    state["position"] = position
    state["forward"] = forward


def moon_spot():
    """A direction from the moon's centre halfway between the sun and the
    planet, seen from the moon: the ground there is lit and has the planet
    in its sky (straight towards the sun when they are opposite)."""
    moon = moon_body()
    towards = add(world.sun_direction(), normalize(scale(moon.centre(), -1.0)))
    return normalize(towards) if length(towards) > 1e-3 else world.sun_direction()


def place_ship_at_moon(preset):
    """Shift+8: still above moon_spot with translation assist on, facing
    north, then set down at rest on its ground once probes have measured
    it (set_down_on_moon). Shift+9: in a circular orbit MOON_ORBIT_HEIGHT
    above the moon's radius over moon_spot, moving the way the moon goes
    round the planet, nose 60 degrees down (from there the horizon is 44
    degrees down), assist off."""
    moon = moon_body()
    up = moon_spot()
    orbit_normal = cross(moon.orbit.u, moon.orbit.v)
    if preset == 8:
        position = add(moon.centre(), scale(up, moon.radius + MOON_HOVER_HEIGHT))
        new_ship(position, moon.velocity(), local_frame(up)[1], up, True)
        # The ground under the centre and under each gear point, were the ship standing there.
        q = state["ship"].orientation
        feet = [normalize(add(scale(up, moon.radius), flight.quat_rotate(q, arm)))
                for arm in state["ship"].params.gear]
        state["moon_landing"] = up
        world.extra_probes = [(up, moon)] + [(d, moon) for d in feet]
        return
    up = normalize(sub(up, scale(orbit_normal, dot(up, orbit_normal))))
    along = cross(orbit_normal, up)
    speed = math.sqrt(moon.orbit.gm / (moon.radius + MOON_ORBIT_HEIGHT))
    down = math.radians(60.0)
    forward = normalize(add(scale(along, math.cos(down)), scale(up, -math.sin(down))))
    new_ship(add(moon.centre(), scale(up, moon.radius + MOON_ORBIT_HEIGHT)),
             add(moon.velocity(), scale(along, speed)), forward, up, False)


def set_down_on_moon():
    """Once the probes for Shift+8 are back: the ship at rest over its
    spot, moving with the moon, its centre SHIP_CLEARANCE above the highest
    of the ground under it and under its gear points, so that no point
    starts below the ground."""
    spot = state["moon_landing"]
    if spot is None or state["mode"] != "ship":
        return
    asked = world.extra_probes
    heights = world.extra_heights
    if [probe for probe, _ in heights] != asked:
        return
    body = asked[0][1]
    ground = max(height for _, height in heights)
    new_ship(add(body.centre(), scale(spot, body.radius + ground + SHIP_CLEARANCE)), body.velocity(),
             local_frame(spot)[1], spot, True)
    state["moon_landing"] = None
    world.extra_probes = []


def stick(value):
    """A gamepad axis with GAMEPAD_DEAD_ZONE around 0 cut out and the rest
    stretched back to -1 .. 1."""
    size = abs(value)
    if size <= GAMEPAD_DEAD_ZONE:
        return 0.0
    return math.copysign((min(size, 1.0) - GAMEPAD_DEAD_ZONE) / (1.0 - GAMEPAD_DEAD_ZONE), value)


def trigger(axis):
    """A gamepad trigger from 0 (released) to 1."""
    return 0.5 * (engine.input.get_gamepad_axis(axis) + 1.0)


def read_ship_controls(dt):
    """The ship's controls from the keys, the mouse and the gamepad; their
    sticks add up, each clamped to -1 .. 1."""
    controls = state["controls"]
    down = engine.input.is_key_down
    pad = engine.input.is_gamepad_connected()
    axis = (lambda a: stick(engine.input.get_gamepad_axis(a))) if pad else (lambda a: 0.0)
    throttle_up = float(down(keys.W)) + (trigger(keys.GAMEPAD_RIGHT_TRIGGER) if pad else 0.0)
    throttle_down = float(down(keys.S)) + (trigger(keys.GAMEPAD_LEFT_TRIGGER) if pad else 0.0)
    controls.throttle = max(0.0, min(1.0, controls.throttle + THROTTLE_RATE * dt * (throttle_up - throttle_down)))
    pitch = float(down(keys.UP)) - float(down(keys.DOWN)) + axis(keys.GAMEPAD_LEFT_Y)
    yaw = float(down(keys.RIGHT)) - float(down(keys.LEFT)) + axis(keys.GAMEPAD_RIGHT_X)
    if engine.input.is_mouse_button_down(keys.MOUSE_RIGHT):
        dx, dy = engine.input.get_mouse_delta()
        pitch -= dy * MOUSE_STICK
        yaw += dx * MOUSE_STICK
    clamp = lambda value: max(-1.0, min(1.0, value))
    controls.pitch, controls.yaw = clamp(pitch), clamp(yaw)
    controls.roll = clamp(float(down(keys.D)) - float(down(keys.A)) + axis(keys.GAMEPAD_LEFT_X))
    controls.vertical = clamp(float(down(keys.E)) - float(down(keys.Q)) - axis(keys.GAMEPAD_RIGHT_Y))


PAD_LOOK_PIXELS = 12.0           # pixels of mouse movement a frame at full right stick, on foot
PAD_AXES = (keys.GAMEPAD_LEFT_X, keys.GAMEPAD_LEFT_Y, keys.GAMEPAD_RIGHT_X, keys.GAMEPAD_RIGHT_Y)


def read_gamepad_buttons():
    """Gamepad buttons on the frame they are pressed, as the keys of
    GAMEPAD_KEYS (FOOT_GAMEPAD_KEYS on foot; X in a landed ship as F). A button pressed, a stick moved out of its dead zone or a
    trigger pulled makes the hints name the gamepad's controls."""
    held = set()
    mapping = FOOT_GAMEPAD_KEYS if state["mode"] == "foot" else GAMEPAD_KEYS
    if engine.input.is_gamepad_connected():
        held = {b for b in mapping if engine.input.is_gamepad_button_down(b)}
        if (held - state["gamepad_held"] or any(stick(engine.input.get_gamepad_axis(a)) for a in PAD_AXES)
                or trigger(keys.GAMEPAD_LEFT_TRIGGER) > 0.2 or trigger(keys.GAMEPAD_RIGHT_TRIGGER) > 0.2):
            state["input"] = "pad"
    for button in held - state["gamepad_held"]:
        if state["mode"] == "ship" and button == keys.GAMEPAD_X and ship_landed(state["ship"]):
            on_key(keys.F, from_pad=True)
        else:
            on_key(mapping[button], from_pad=True)
    state["gamepad_held"] = held


def ground_patch_request(ship):
    """(body, patch): a ground patch on the body under the ship, relative
    to its centre, to measure around where the ship will be when its
    heights come back: ahead along its velocity over that body's ground by
    the probes' latency (times the warp), reaching that far again either
    way beyond the ship's contact points."""
    body = world.body_under(ship.position)
    position = body.relative(ship.position)
    velocity = sub(ship.velocity, body.velocity())
    radial = normalize(position)
    along = sub(velocity, scale(radial, dot(velocity, radial)))
    ahead = world.probe_latency * state["frame_ms"] / 1000.0 * WARPS[state["warp"]]
    half = SHIP_REACH + PATCH_MARGIN + length(along) * ahead
    return body, GroundPatch(body.radius, add(position, scale(along, ahead)), 2.0 * half / (PATCH_SIZE - 1),
                             sea=body.ocean)


def ground_under_ship():
    """The ground patch's query for the ship, taking points from the
    planet's centre, or None while there is no patch."""
    if world.patch is None:
        return None
    body, patch = world.patch
    return lambda point: patch.query(body.relative(point))


def fly_ship(dt):
    """Advance the ship by this frame's time (times the warp) in fixed
    steps, against the ground patch under it."""
    ship = state["ship"]
    state["accumulator"] += dt * WARPS[state["warp"]]
    ground = ground_under_ship()
    state["impact_speed"] = 0.0
    steps = 0
    while state["accumulator"] >= FLIGHT_STEP and steps < MAX_STEPS_PER_FRAME:
        ship.step(world.physics, state["controls"], FLIGHT_STEP, ground)
        state["impact_speed"] = max(state["impact_speed"], ship.impact_speed)
        state["accumulator"] -= FLIGHT_STEP
        world.time += FLIGHT_STEP
        steps += 1
    if steps == MAX_STEPS_PER_FRAME:
        state["accumulator"] = 0.0      # too far behind: drop the rest rather than fall further behind
    state["position"] = chase_view(ship)[0] if state["chase"] else cockpit_eye(ship)
    state["forward"] = view_basis()[2]


def orbit_path_points():
    """ORBIT_POINTS points of the path the ship would coast along
    (flight.orbit_path) around the body it is reckoned around
    (flight.reference), where that body is now, or on foot the tracked
    prop's (tracked_path); none without either or with the path switched
    off."""
    if state["mode"] == "foot" and state["tracked"] is not None and state["orbit_path"]:
        return tracked_path()
    ship = state["ship"]
    if ship is None or not state["orbit_path"]:
        return []
    body, position, velocity = flight.reference(world.physics, ship.position, ship.velocity)
    centre = body.position(world.time) if body is world.physics.moon else (0.0, 0.0, 0.0)
    return [add(centre, p) for p in flight.orbit_path(body, position, velocity, count=ORBIT_POINTS)]


# ── Physics bubble ─────────────────────────────────────────────────────────

def body_gm(body):
    return world.physics.gm if body is planet else world.physics.moon.gm


def ensure_bubble(point=None):
    """The bubble on the body under `point` (the viewpoint when None), made
    around it if there is none there; a bubble on the other body is
    dropped first."""
    point = viewpoint() if point is None else point
    body = world.body_under(point)
    bubble = state["bubble"]
    if bubble is not None and bubble.body is body:
        return bubble
    drop_bubble()
    bubble = Bubble(physics, body, body_gm(body), body.relative(point))
    state["bubble"] = bubble
    state["terrain_collider"] = TerrainCollider(bubble, collider_probes)
    state["coarse_collider"] = TerrainCollider(bubble, collider_probes, spacing=COARSE_SPACING, coarse=True)
    state["physics_accumulator"] = 0.0
    state["focus"] = None
    return bubble


def drop_bubble():
    """The bubble, its trunks, fixed rocks, the player, the ship's hull and
    its terrain meshes gone; its props out of the physics, flying or
    resting where they are."""
    bubble = state["bubble"]
    if bubble is None:
        return
    props.leave_all(bubble)
    trunks.clear(bubble)
    scatter_rocks.clear(bubble)
    remove_player()
    remove_ship_body()
    state["terrain_collider"].drop()
    state["coarse_collider"].drop()
    state["bubble"] = state["terrain_collider"] = state["coarse_collider"] = None


def drop_crates(count):
    """`count` crates stacked CRATE_AHEAD in front of the view, square to
    the body's up there and turned with the view's heading; with a player,
    into the player's bubble, and none while the view is over the other body."""
    if state["player"] is not None:
        bubble = state["bubble"]
        if world.body_under(state["position"]) is not bubble.body:
            return
    else:
        bubble = ensure_bubble()
    _, _, forward = view_basis()
    p = bubble.to_bubble(add(state["position"], scale(forward, CRATE_AHEAD)))
    up = bubble.up(p)
    heading = sub(forward, scale(up, dot(forward, up)))
    heading = normalize(heading) if length(heading) > 1e-6 else local_frame(up)[1]
    right = normalize(cross(heading, up))
    rotation = flight.quat_from_basis(right, up, scale(heading, -1.0))
    for height in stack_offsets(count, CRATE_SIZE):
        props.spawn_crate(bubble, add(p, scale(up, height)), rotation, CRATE_SIZE, CRATE_DENSITY)


def step_bubble(dt):
    """The bubble following its focus, the player's feet, or without a
    player the viewpoint: without a player, dropped when the viewpoint is
    over the other body or more than Bubble.RADIUS above the ground;
    otherwise rebased to the focus once far enough from its origin, its
    terrain mesh kept around it (ahead along its velocity by the probes'
    latency; a move of JUMP_DISTANCE or more in a frame counts as no
    velocity), its trunks updated from the nearby trees that came back (all
    removed when the terrain or the vegetation changed), its crates kept
    on that mesh, and its physics stepped by
    `dt` in fixed steps of FLIGHT_STEP: each step the player (with the
    controls on foot, standing still otherwise; held where the mesh does
    not reach) and on foot its gravity gun (the gun drops what it holds
    off foot), the ship's hull moved along the ship's path this frame,
    then the bodies."""
    bubble = state["bubble"]
    if bubble is None:
        return
    player = state["player"]
    if player is not None:
        here = bubble.body_relative(player.feet())
    else:
        view = viewpoint()
        if world.body_under(view) is not bubble.body or world.height_above_ground(view) > Bubble.RADIUS:
            drop_bubble()
            return
        here = bubble.body.relative(view)
    last = state["focus"]
    velocity = (0.0, 0.0, 0.0)
    if last is not None and dt > 0.0 and length(sub(here, last)) < JUMP_DISTANCE:
        velocity = scale(sub(here, last), 1.0 / dt)
    state["focus"] = here
    bubble.maybe_rebase(sub(here, bubble.origin))
    latency = world.probe_latency * state["frame_ms"] / 1000.0
    terrain = state["terrain_collider"]
    terrain.update(sub(here, bubble.origin), velocity, latency, world.terrain_version)
    coarse = state["coarse_collider"]
    coarse.update(sub(here, bubble.origin), velocity, latency, world.terrain_version)
    plants = (world.terrain_version, world.vegetation, world.vegetation_density, world.rocks)
    if plants != state["trunk_plants"]:
        trunks.clear(bubble)
        scatter_rocks.clear(bubble)
        state["trunk_plants"] = plants
    trees = nearby_trees.read()
    if trees is not None:
        if bubble.body is planet:
            trunks.update(bubble, trees, state["position"], COLLIDER_RADIUS)
        scatter_rocks.update(bubble, trees, state["position"], COLLIDER_RADIUS)
    props.update(bubble, terrain, coarse)
    state["physics_accumulator"] += dt
    count = min(int(state["physics_accumulator"] / FLIGHT_STEP), MAX_PHYSICS_STEPS_PER_FRAME)
    on_foot = state["mode"] == "foot"
    controls = state["foot_controls"] if on_foot else FootControls()
    gun = state["gun"]
    if gun is not None and not on_foot:
        gun.drop()
    for k in range(count):
        if player is not None:
            player.bob = state["head_bob"]
            player.step(controls, FLIGHT_STEP, terrain.covers(player.feet(), HOLD_MARGIN))
            if on_foot:
                gun.step(state["gun_controls"], FLIGHT_STEP)
        move_ship_body(bubble, (k + 1) / count)
        props.drag(bubble)
        bubble.step(FLIGHT_STEP)
    for body in physics.take_struck():
        scatter_rocks.promote(bubble, body)
    note_thrown()
    state["physics_accumulator"] -= count * FLIGHT_STEP
    if count == MAX_PHYSICS_STEPS_PER_FRAME:
        state["physics_accumulator"] = 0.0


def remove_ship_body():
    if state["ship_body"] is not None:
        if physics.contains(state["ship_body"]):
            physics.remove(state["ship_body"])
        state["ship_body"] = None


def move_ship_body(bubble, fraction):
    """The ship's kinematic hull moved `fraction` of the way along the
    ship's path this frame (state["ship_poses"]), made when the ship comes
    within Bubble.RADIUS of the bubble's origin and removed when it leaves
    or crashes; put there at once after a jump of JUMP_DISTANCE or more."""
    ship, poses = state["ship"], state["ship_poses"]
    if ship is None or ship.crashed or poses is None:
        remove_ship_body()
        return
    (p0, q0), (p1, q1) = poses
    if dot(q0, q1) < 0.0:
        q1 = tuple(-c for c in q1)
    q = flight.quat_normalize(tuple(a + (b - a) * fraction for a, b in zip(q0, q1)))
    p = bubble.to_bubble(add(p0, scale(sub(p1, p0), fraction)))
    if length(p) > Bubble.RADIUS:
        remove_ship_body()
        return
    body = state["ship_body"]
    if body is None:
        state["ship_body"] = physics.create_convex_hull(SHIP_HULL_POINTS, p, q, motion="kinematic",
                                                        friction=0.8)
    elif length(sub(physics.get_transform(body)[0], p)) > JUMP_DISTANCE:
        physics.set_transform(body, p, q)
    else:
        physics.move_kinematic(body, p, q, FLIGHT_STEP)


def boarding_point(ship):
    """The point beside the cockpit, BOARD_OUT left of the ship and level
    with its gear's feet, where the player gets out and in."""
    right, up, forward = ship.axes()
    _, _, back = model_to_ship(PILOT_EYE)
    return add(ship.position, add(add(scale(right, -BOARD_OUT), scale(up, -SHIP_CLEARANCE)), scale(forward, -back)))


def ship_landed(ship):
    """The ship parked, or not crashed, slower than EXIT_SPEED relative to
    the body under it and touching the ground: a gear point touched it in
    the last step, or every gear point is within flight.GEAR_TOUCH of the
    ground patch's surface."""
    if ship is None or ship.crashed:
        return False
    if state["parked"] is not None:
        return True
    body = world.body_under(ship.position)
    if length(sub(ship.velocity, body.velocity())) >= EXIT_SPEED:
        return False
    if ship.on_ground:
        return True
    ground = ground_under_ship()
    if ground is None:
        return False
    for gear in ship.params.gear:
        contact = ground(add(ship.position, flight.quat_rotate(ship.orientation, gear)))
        if contact is None or contact[0] < -flight.GEAR_TOUCH:
            return False
    return True


def spawn_player(bubble, feet_world, heading):
    remove_player()
    state["player"] = Player(bubble, bubble.to_bubble(feet_world), heading)
    state["gun"] = Gun(state["player"], lambda: props.grabbable(physics) + scatter_rocks.grabbable(),
                       promote=lambda body: scatter_rocks.promote(state["bubble"], body))
    state["mode"] = "foot"
    state["warp"] = 0
    place_eye()


def remove_player():
    if state["player"] is not None:
        state["gun"].drop()
        state["gun"] = None
        state["player"].remove()
        state["player"] = None
        state["foot_basis"] = None


def go_on_foot():
    """From the free camera: back to the player, or a player put down
    SPAWN_LIFT above the ground under the camera, facing its way, when the
    ground there is known and within Bubble.RADIUS below."""
    if state["player"] is not None:
        state["mode"] = "foot"
        state["warp"] = 0
        return
    if world.ground is None or world.height_above_ground(state["position"]) > Bubble.RADIUS:
        return
    bubble = ensure_bubble(state["position"])
    up = normalize(bubble.body.relative(state["position"]))
    feet = add(bubble.body.centre(),
               scale(up, bubble.body.radius + world.surface_height(state["position"]) + SPAWN_LIFT))
    spawn_player(bubble, feet, state["forward"])


def get_out():
    """Out of the landed ship (ship_landed) at its boarding point,
    SPAWN_LIFT up, facing the ship's way; the ship parked."""
    ship = state["ship"]
    if not ship_landed(ship):
        return
    body = world.body_under(ship.position)
    state["parked"] = (body, body.relative(ship.position))
    bubble = ensure_bubble(ship.position)
    right, up, forward = ship.axes()
    spawn_player(bubble, add(boarding_point(ship), scale(up, SPAWN_LIFT)), forward)


def get_in():
    """Into the ship from within BOARD_REACH of its boarding point, while it is landed."""
    ship, player, bubble = state["ship"], state["player"], state["bubble"]
    if player is None or not ship_landed(ship):
        return
    if length(sub(bubble.to_world(player.feet()), boarding_point(ship))) > BOARD_REACH:
        return
    remove_player()
    if state["parked"] is not None:
        ship.velocity = state["parked"][0].velocity()
    state["parked"] = None
    state["mode"] = "ship"


def near_boarding_point():
    ship, player, bubble = state["ship"], state["player"], state["bubble"]
    return (player is not None and ship_landed(ship)
            and length(sub(bubble.to_world(player.feet()), boarding_point(ship))) <= BOARD_REACH)


def follow_parked_ship():
    """A parked ship moved with the body it is parked on, outside ship mode."""
    if state["parked"] is None or state["ship"] is None or state["mode"] == "ship":
        return
    body, relative = state["parked"]
    state["ship"].position = add(body.centre(), relative)
    state["ship"].velocity = body.velocity()


def read_foot_controls():
    """The player's and the gravity gun's controls from the keys, the mouse
    and the gamepad (left stick to walk, right stick to look, A to jump and
    jet, the left trigger to grab, the right to punt). The mouse turns the
    view while captured, or with the right button held; while it is
    captured, the right button grabs and the left punts, and with R held
    the mouse turns the held body instead of the view."""
    c, gc = state["foot_controls"], state["gun_controls"]
    down = engine.input.is_key_down
    forward = (1.0 if down(keys.W) else 0.0) - (1.0 if down(keys.S) else 0.0)
    strafe = (1.0 if down(keys.D) else 0.0) - (1.0 if down(keys.A) else 0.0)
    c.run = down(keys.LEFT_SHIFT)
    c.crouch = down(keys.LEFT_CONTROL)
    c.jet = down(keys.SPACE)
    player, gun = state["player"], state["gun"]
    captured = engine.input.is_mouse_captured()
    mouse = engine.input.is_mouse_button_down
    grab = captured and mouse(keys.MOUSE_RIGHT)
    punt = captured and mouse(keys.MOUSE_LEFT)
    look = (0.0, 0.0)
    if engine.input.is_gamepad_connected():
        forward -= stick(engine.input.get_gamepad_axis(keys.GAMEPAD_LEFT_Y))
        strafe += stick(engine.input.get_gamepad_axis(keys.GAMEPAD_LEFT_X))
        a = engine.input.is_gamepad_button_down(keys.GAMEPAD_A)
        if a and not state.get("pad_jump"):
            c.jump = True
        state["pad_jump"] = a
        c.jet = c.jet or a
        grab = grab or trigger(keys.GAMEPAD_LEFT_TRIGGER) > 0.5
        punt = punt or trigger(keys.GAMEPAD_RIGHT_TRIGGER) > 0.5
        look = (stick(engine.input.get_gamepad_axis(keys.GAMEPAD_RIGHT_X)) * PAD_LOOK_PIXELS,
                stick(engine.input.get_gamepad_axis(keys.GAMEPAD_RIGHT_Y)) * PAD_LOOK_PIXELS)
    c.forward, c.strafe = max(-1.0, min(1.0, forward)), max(-1.0, min(1.0, strafe))
    gc.grab = grab
    gc.punt_held = punt
    if punt and not state["punt_held"]:
        gc.punt = True
    state["punt_held"] = punt
    if player is not None and (captured or engine.input.is_mouse_button_down(keys.MOUSE_RIGHT)):
        dx, dy = engine.input.get_mouse_delta()
        if captured and down(keys.R) and gun is not None and gun.held is not None:
            gc.rotate = (gc.rotate[0] + dx, gc.rotate[1] + dy)
        else:
            look = (look[0] + dx, look[1] + dy)
    if player is not None:
        player.look(*look)
    state["look_delta"] = look


def place_eye():
    """The camera at the player's eye, looking its way."""
    player, bubble = state["player"], state["bubble"]
    if player is None:
        return
    state["position"] = bubble.to_world(player.eye())
    state["foot_basis"] = player.basis()
    state["forward"] = state["foot_basis"][2]


def foot_warp():
    """The time warp on foot: as set while no prop in the physics is awake, else 1x."""
    bubble = state["bubble"]
    if bubble is not None and props.awake(bubble):
        return 1
    return WARPS[state["warp"]]


def note_thrown():
    """The prop the gun punted last, while it is one (T tracks it)."""
    gun = state["gun"]
    if gun is None or gun.last_punt is None or gun.last_punt is state.get("noted_punt"):
        return
    state["noted_punt"] = gun.last_punt
    body = gun.last_punt[0]
    thrown = props.find(body) if body is not None else None
    if thrown is not None:
        state["last_thrown"] = thrown


def prop_state(prop):
    """(the world body it is reckoned around, position from its centre, velocity) of a prop."""
    bubble = state["bubble"]
    if prop.body is not None and bubble is not None:
        p = physics.get_transform(prop.body)[0]
        return bubble.body, bubble.body_relative(p), physics.get_velocity(prop.body)[0]
    return prop.on, prop.position, prop.velocity


def tracked_path():
    """ORBIT_POINTS points of the tracked prop's path around its body
    (flight.orbit_path), where the body is now; none while it rests."""
    prop = state["tracked"]
    if prop is None or prop.state == "resting" or (prop not in props.all):
        return []
    body, position, velocity = prop_state(prop)
    orbiting = world.physics.moon if body is world.moon else world.physics
    centre = body.centre()
    return [add(centre, p) for p in flight.orbit_path(orbiting, position, velocity, count=ORBIT_POINTS)]


def place_viewmodel(dt, camera):
    """The gravity gun's viewmodel on foot, its beam to what it holds; hidden off foot."""
    viewmodel, gun, player = state["viewmodel"], state["gun"], state["player"]
    on_foot = state["mode"] == "foot" and player is not None
    viewmodel.show(on_foot)
    if not on_foot:
        return
    held = None
    if gun.held is not None:
        held = sub(state["bubble"].to_world(physics.get_transform(gun.held)[0]), camera)
    viewmodel.update(dt, view_basis(), state["look_delta"], player, gun, held)


def set_max_levels():
    """FOOT_MAX_LEVEL for the body under the player on foot, MAX_LEVEL for every other tree."""
    for body in bodies:
        tree = body.tree
        if tree is None:
            continue
        foot = state["mode"] == "foot" and state["player"] is not None and state["bubble"].body is body
        wanted = FOOT_MAX_LEVEL if foot else MAX_LEVEL
        if tree.max_level != wanted:
            tree.max_level = wanted


def capture_mouse():
    """The mouse captured on foot, freed off foot and while the menu or the
    controls are open; Escape frees it until the next change."""
    wanted = (state["mode"] == "foot" and not state["menu"].visible and not state["help"].visible
              and not state["scripted_controls"])
    if wanted != state["captured"]:
        engine.input.set_mouse_captured(wanted)
        state["captured"] = wanted


# ── Per frame ──────────────────────────────────────────────────────────────

def frame(dt):
    started = world.begin_frame(viewpoint())
    set_down_on_moon()
    if started and state["teleport_when_known"]:
        teleport(state["teleport_when_known"])

    if not state["scripted_controls"]:
        read_gamepad_buttons()
    if engine.input.is_mouse_button_down(keys.MOUSE_RIGHT):
        state["input"] = "keys"
    ship = state["ship"]
    before = (ship.position, ship.orientation) if ship is not None else None
    clock = world.time
    if state["mode"] == "ship":
        if not state["scripted_controls"]:
            read_ship_controls(dt)
        fly_ship(dt)
    elif state["mode"] == "foot":
        if not state["scripted_controls"]:
            read_foot_controls()
        world.time += dt * foot_warp()
    else:
        if engine.input.is_mouse_button_down(keys.MOUSE_RIGHT):
            dx, dy = engine.input.get_mouse_delta()
            look(dx, dy)
        state["speed"] = 0.0
        fly(dt)
        state["position"] = world.keep_above_ground(state["position"], MIN_ALTITUDE)
        # The world's clock runs on (the moon moves) while the ship waits; the camera moves with the
        # body under it.
        body = world.body_under(state["position"])
        centre = body.centre()
        world.time += dt * WARPS[state["warp"]]
        state["position"] = add(state["position"], sub(body.centre(), centre))
    follow_parked_ship()
    if ship is not None and ship is state["ship"]:
        state["ship_poses"] = (before, (ship.position, ship.orientation))
    else:
        state["ship_poses"] = None
    # The flying props on to this frame's clock before the bubble takes any back.
    props.fly(world.time - clock, world.probe_latency * state["frame_ms"] / 1000.0)
    step_bubble(dt)
    props.probe(state["bubble"], EXTRA_PROBES)
    if state["mode"] == "foot":
        place_eye()
    capture_mouse()

    set_max_levels()

    # The world for the view: the level of detail, the probes (with the ground patch around the
    # ship), and everything placed relative to the camera, subtracted in double.
    camera = state["position"]
    right, up, forward = view_basis()
    ship = state["ship"]
    patch = ground_patch_request(ship) if state["mode"] == "ship" and not ship.crashed else None
    bubble = state["bubble"]
    nearby_trees.request(camera, COLLIDER_RADIUS if bubble is not None else 0.0,
                         [body.centre() for body in sorted(bodies, key=lambda b: b.index)])
    world.end_frame(camera, (right, up, forward), dt, warp=WARPS[state["warp"]], viewpoint=viewpoint(),
                    patch=patch)
    world.set_orbit_path(orbit_path_points())
    ground = world.site_ground()
    for entity, offset in blocks:
        engine.scene.set_position(entity, sub(add(ground, offset), camera))
    props.draw(state["bubble"], camera)
    place_viewmodel(dt, camera)
    collider_probes.end_frame()
    place_ship_model(camera)
    view_rotation = euler_from_basis(right, up, forward)
    if (state["mode"], state["chase"], state["generated_cockpit"]) != state["shown_mode"]:
        show_mode()
    if cockpit_view():
        update_cockpit(right, up, forward, view_rotation)
    state["menu"].update()
    state["hint_bar"].show(state["hints"])
    if state["hints"]:
        state["hint_bar"].update(current_hints())

    state["frame_ms"] = 0.9 * state["frame_ms"] + 0.1 * dt * 1000.0
    if state["cards"] is not None:
        update_cards()
    return True


def update_cockpit(right, up, forward, view_rotation):
    """The displays placed on the cabin's panel (or the generated cockpit
    turned with the view, and the displays on its dashboard), the reentry
    glow's directions, and the instruments drawn from the readings."""
    ship = state["ship"]
    controls = state["controls"]
    ship_right, ship_up, ship_forward = ship.axes()

    def to_world(v):
        return add(add(scale(ship_right, v[0]), scale(ship_up, v[1])), scale(ship_forward, -v[2]))

    if state["generated_cockpit"]:
        engine.scene.set_position(state["cockpit"], (0.0, 0.0, 0.0))
        engine.scene.set_rotation(state["cockpit"], view_rotation)
        state["instruments"].place(cockpit.SCREENS, to_world, euler_from_basis)
    else:
        state["instruments"].place(state["cabin_screens"], to_world, euler_from_basis)

    body_below = world.body_under(ship.position)
    radial = world.radial_up(ship.position)
    velocity = ship_velocity()
    speed = length(velocity)
    moving = scale(velocity, 1.0 / speed) if speed > 1.0 else None
    engine.set_custom_vec4("planet.hud.prograde", (moving or ship_forward) + (1.0 if moving else 0.0,))
    glow = flight.reentry_glow(world.physics, ship.position, ship.velocity)
    engine.set_custom_vec4("planet.hud.reentry", (glow, 0.0, 0.0, 0.0))
    set_reentry_pass(glow > 0.0)

    east, north = local_frame(radial)
    level = sub(ship_forward, scale(radial, dot(ship_forward, radial)))
    heading = math.degrees(math.atan2(dot(level, east), dot(level, north))) % 360.0
    body, periapsis, apoapsis, circular = ship_orbit()
    density = world.physics.density(ship.position)
    agl = distance_text(world.height_above_ground(viewpoint()))
    state["instruments"].update((right, up, forward, ship_forward, radial, moving), {
        "body": body,
        "speed": speed,
        "climb": dot(velocity, radial),
        "height": tuple(agl.split(" ")),
        "agl": agl,
        "datum": world.datum_name(viewpoint()),
        "datum_short": "ASL" if body_below.ocean else "ALT",
        "datum_height": distance_text(world.altitude(viewpoint())),
        "heading": heading,
        "throttle": controls.throttle,
        "assist": controls.assist,
        "warp": WARPS[state["warp"]],
        "contact": contact_text(ship),
        "periapsis": "below ground" if periapsis < 0.0 else distance_text(periapsis),
        "apoapsis": "escape" if apoapsis == math.inf else distance_text(apoapsis),
        "circular": circular,
        "density": density,
        "dynamic_pressure": 0.5 * density * speed * speed,
        "glow": glow,
    })


def ship_velocity():
    """The ship's velocity relative to the body under it."""
    return sub(state["ship"].velocity, world.body_under(state["ship"].position).velocity())


def ship_orbit():
    """The ship's orbit around the body it is reckoned around
    (flight.reference): (that body's name, periapsis and apoapsis heights,
    the circular speed at the ship's distance from it)."""
    ship = state["ship"]
    body, position, velocity = flight.reference(world.physics, ship.position, ship.velocity)
    periapsis, apoapsis = flight.orbit(body, position, velocity)
    name = "moon" if body is world.physics.moon else "planet"
    return name, periapsis, apoapsis, math.sqrt(body.gm / length(position))


def contact_text(ship):
    if ship.crashed:
        return "CRASHED (8 or 9 for a new ship)"
    return "LANDED" if ship.on_ground else ""


def update_cards():
    """The view's card (in the free camera and the chase view) and, when
    debug text is on, the debug card; neither while the controls sheet is
    open."""
    view_card, debug_card = state["cards"]
    if state["help"].visible:
        view_card.show(False)
        debug_card.show(False)
        return
    if state["mode"] == "ship" and state["chase"]:
        ship, controls = state["ship"], state["controls"]
        body, periapsis, apoapsis, _ = ship_orbit()
        velocity = ship_velocity()
        rows = [
            ("SPEED", "%.0f m/s" % length(velocity)),
            ("CLIMB", "%+.1f m/s" % dot(velocity, world.radial_up(ship.position))),
            ("ABOVE GROUND", distance_text(world.height_above_ground(viewpoint()))),
            ("ABOVE " + world.datum_name(viewpoint()).upper(), distance_text(world.altitude(viewpoint()))),
            ("THROTTLE", "%.0f %%" % (controls.throttle * 100.0)),
            ("ASSIST", "on" if controls.assist else "off"),
            ("TIME WARP", "x%d" % WARPS[state["warp"]]),
            ("ORBIT AROUND THE " + body.upper(),),
            ("PERIAPSIS", "below ground" if periapsis < 0.0 else distance_text(periapsis)),
            ("APOAPSIS", "escape" if apoapsis == math.inf else distance_text(apoapsis)),
        ]
        if contact_text(ship):
            rows.append(("STATE", contact_text(ship).split(" (")[0].lower()))
        view_card.update("CHASE VIEW", rows)
        view_card.show(True)
    elif state["mode"] == "foot" and state["player"] is not None:
        player = state["player"]
        on_planet = world.body_under(viewpoint()) is world.planet
        velocity = physics.character_velocity(player.character)
        stance = "held: no ground yet" if player.held else ("on the ground" if player.on_ground else "in the air")
        view_card.update("ON FOOT", [
            ("ABOVE " + world.datum_name(viewpoint()).upper(), distance_text(world.altitude(viewpoint()))),
        ] + (biomes.card_rows(world.ground_climate) if on_planet else []) + [
            ("SPEED", "%.1f m/s" % length(velocity)),
            ("JETPACK", "%.0f %%" % (player.jet_fuel * 100.0)),
            ("STATE", stance),
            ("GUN", gun_text()),
        ] + (tracked_rows() if state["tracked"] in props.all else [])
          + ([("SHIP", "F to get in")] if near_boarding_point() else []))
        view_card.show(True)
    elif state["mode"] != "ship":
        on_planet = world.body_under(viewpoint()) is world.planet
        view_card.update("FREE CAMERA", [
            ("ABOVE GROUND", distance_text(world.height_above_ground(viewpoint()))),
            ("ABOVE " + world.datum_name(viewpoint()).upper(), distance_text(world.altitude(viewpoint()))),
        ] + (biomes.card_rows(world.ground_climate) if on_planet else []) + [
            ("SPEED", "%.0f m/s" % state["speed"]),
            ("SPEED FACTOR", "x%.2f" % state["speed_factor"]),
            ("TIME WARP", "x%d" % WARPS[state["warp"]]),
        ])
        view_card.show(True)
    else:
        view_card.show(False)

    debug_card.show(state["debug_text"])
    tree = planet.tree
    if not state["debug_text"] or tree is None:
        return
    terrain = planet.terrain
    trees = [body.tree for body in bodies if body.tree is not None]
    selected = sum(len(t.selected) for t in trees)
    cached = sum(1 for t in trees for node in t.nodes() if node.chunk is not None)
    missing = sum(len(t.missing) for t in trees)
    deepest = max(node.level for t in trees for node in t.selected)
    debug_card.update("DEBUG", [
        ("FRAME", "%.1f ms" % state["frame_ms"]),
        ("CHUNKS DRAWN", "%d of %d" % (selected - world.chunks_culled, selected)),
        ("OCEAN CHUNKS", "%d" % world.ocean_chunks),
        ("CHUNKS BUILT", "%d" % cached),
        ("POOL", "%d / %d" % (pool.used, SLOTS)),
        ("MISSING", "%d" % missing),
        ("DEEPEST LEVEL", "%d" % deepest),
        ("SPLIT FACTOR", "%.2f%s" % (tree.split_factor, "  frozen" if world.frozen else "")),
        ("COLOUR", COLOUR_MODES[world.colour_mode]),
        ("TERRAIN",),
        ("SEED", "%.1f  %.1f  %.1f" % tuple(terrain["seed"][:3])),
        ("MOUNTAINS", "%.0f m" % terrain["mountains"][1]),
        ("HILLS", "%.0f m" % terrain["hills"][1]),
        ("SUN", "%.0f deg up at the site" % world.sun_elevation),
        ("ATMOSPHERE", "density x%.2f" % world.density),
        ("EXPOSURE", "%.3f" % world.exposure),
        ("PHYSICS",),
        ("BODIES", "%d, %d awake" % (physics.body_count, physics.active_count)),
        ("CRATES", "%d" % len(props.crates)),
        ("PROPS", "%d in the physics, %d flying, %d resting" % tuple(
            sum(1 for p in props.all if p.state == s) for s in ("jolt", "flying", "resting"))),
        ("TRUNKS", "%d" % len(trunks.trunks)),
        ("ROCKS", "%d fixed, %d loose" % (len(scatter_rocks.rocks), len(props.rocks))),
        ("TERRAIN MESHES", "%d built" % (state["terrain_collider"].builds if state["terrain_collider"] else 0)),
    ])


def gun_text():
    """The gravity gun's status for the card: what it holds or points at, and its mass."""
    gun = state["gun"]
    status = gun.status
    if status == "holding":
        return "holding %.0f kg, %.1f m" % (physics.mass(gun.held), gun.hold_distance)
    if status == "too_heavy":
        return "TOO HEAVY (%.0f kg)" % gun.target.mass
    if status == "can_grab":
        return "%.0f kg, %.1f m" % (gun.target.mass, gun.target.distance)
    return "ready"


def tracked_rows():
    """The tracked prop's rows for the card: its state and how far away, how high and how fast."""
    prop = state["tracked"]
    body, position, velocity = prop_state(prop)
    away = length(sub(add(body.centre(), position), viewpoint()))
    up = length(position) - body.radius
    what = {"jolt": "near", "flying": "flying", "resting": "resting"}[prop.state]
    return [("TRACKED", "%s, %s away" % (what, distance_text(away))),
            ("ITS HEIGHT, SPEED", "%s, %.0f m/s" % (distance_text(up), length(velocity)))]


def current_hints():
    """The hint bar's hints for what is on the screen and in control, named
    for the keyboard or the gamepad, whichever was used last."""
    pad = state["input"] == "pad"
    if state["help"].visible:
        return [(("pad:Back",) if pad else ("F2",), "Close the controls")]
    if state["menu"].visible:
        return [(("pad:Start",) if pad else ("F1",), "Close settings"), (("Wheel",), "Scroll"),
                (("pad:Back",) if pad else ("F2",), "Controls")]
    if state["mode"] == "foot":
        board = [(("pad:X",) if pad else ("F",), "Get in")] if near_boarding_point() else []
        holding = state["gun"] is not None and state["gun"].held is not None
        if pad:
            gun = [(("pad:LT",), "Drop" if holding else "Grab"), (("pad:RT",), "Punt")]
            if holding:
                gun.append((("pad:LB", "pad:RB"), "Distance"))
            return board + gun + [(("pad:LS",), "Walk"), (("pad:RS",), "Look"), (("pad:A",), "Jump, jetpack"),
                                  (("pad:Y",), "Free camera"), (("pad:Start",), "Settings"),
                                  (("pad:Back",), "Controls")]
        rest = [(("Tab",), "Free camera"), (("F1",), "Settings"), (("F2",), "Controls")]
        if holding:
            return board + [(("RMB",), "Drop"), (("LMB",), "Punt, hold: charge"), (("Wheel",), "Distance"),
                            (("R", "Mouse"), "Turn it"), (("W", "A", "S", "D"), "Walk"),
                            (("Space",), "Jump, hold: jetpack")] + rest
        return board + [(("RMB",), "Grab"), (("LMB",), "Punt"), (("Mouse",), "Look"), (("W", "A", "S", "D"), "Walk"),
                        (("Shift",), "Run"), (("Ctrl",), "Crouch"), (("Space",), "Jump, hold: jetpack"),
                        (("0",), "Drop a crate")] + rest
    if state["mode"] != "ship":
        if pad:
            return [(("pad:Y",), "On foot"), (("pad:Start",), "Settings"), (("pad:Back",), "Controls")]
        return [(("RMB",), "Look"), (("W", "A", "S", "D"), "Fly"), (("E", "Q"), "Up, down"),
                (("Shift", "Ctrl"), "Faster, slower"), (("Wheel",), "Speed"), (("1-9",), "Places"),
                (("0",), "Drop a crate"), (("Tab",), "On foot"), (("F1",), "Settings"), (("F2",), "Controls")]
    view = "Cockpit" if state["chase"] else "Chase view"
    hints = []
    if state["ship"].crashed:
        hints = [(("8",), "New ship on the pad"), (("9",), "New ship in orbit")]
    elif ship_landed(state["ship"]):
        hints = [(("pad:X",) if pad else ("F",), "Get out")]
    if pad:
        return hints + [(("pad:LS",), "Pitch, roll"), (("pad:RS",), "Yaw, up, down"),
                        (("pad:LT", "pad:RT"), "Throttle"), (("pad:A",), "Assist"), (("pad:B",), "Cut throttle"),
                        (("pad:X",), view), (("pad:LB", "pad:RB"), "Warp"), (("pad:Y",), "Free camera"),
                        (("pad:Start",), "Settings"), (("pad:Back",), "Controls")]
    return hints + [(("W", "S"), "Throttle"), (("Arrows",), "Pitch, yaw"), (("A", "D"), "Roll"),
                    (("E", "Q"), "Up, down"), (("L",), "Assist"), (("R",), view), ((",", "."), "Warp"),
                    (("Tab",), "Free camera"), (("F1",), "Settings"), (("F2",), "Controls")]


# The controls sheet (F2, toolkit.overlay.HelpOverlay): columns of (heading, rows of (caps, what they do)).
HELP_COLUMNS = [
    [("Free camera", [
        (("RMB",), "Look around"),
        (("W", "A", "S", "D"), "Fly"),
        (("E", "Q"), "Up / down"),
        (("Shift", "Ctrl"), "4x faster / slower"),
        (("Wheel",), "Speed factor"),
        (("Tab",), "On foot"),
    ]), ("On foot", [
        (("Mouse",), "Look around (Escape frees the mouse)"),
        (("W", "A", "S", "D"), "Walk"),
        (("Shift", "Ctrl"), "Run / crouch"),
        (("Space",), "Jump; held in the air: jetpack"),
        (("F",), "Into the ship, beside its cockpit"),
        (("Tab",), "Free camera"),
    ]), ("Ship", [
        (("W", "S"), "Throttle up / down"),
        (("Backspace",), "Throttle to 0"),
        (("Arrows", "RMB"), "Pitch and yaw"),
        (("A", "D"), "Roll"),
        (("E", "Q"), "Thrust up / down"),
        (("L",), "Translation assist"),
        (("R",), "Chase view / cockpit"),
        (("U",), "Cabin / generated cockpit"),
        ((",", "."), "Time warp down / up"),
        (("F",), "Out, once landed"),
        (("Tab",), "Free camera"),
    ])],
    [("Ship, gamepad", [
        (("pad:LS",), "Pitch and roll"),
        (("pad:RS",), "Yaw, thrust up / down"),
        (("pad:LT", "pad:RT"), "Throttle down / up"),
        (("pad:A",), "Translation assist"),
        (("pad:B",), "Throttle to 0"),
        (("pad:X",), "Chase view / cockpit; landed: out"),
        (("pad:LB", "pad:RB"), "Time warp down / up"),
        (("pad:Y",), "Free camera / on foot"),
        (("pad:Start",), "Settings"),
        (("pad:Back",), "These controls"),
    ]), ("Places", [
        (("1-5",), "5 m to 5,000 km up"),
        (("6",), "Over a cube corner"),
        (("7",), "Over the sea"),
        (("8", "9"), "Ship on the pad / in orbit"),
        (("Shift", "8", "9"), "Ship on / around the moon"),
    ]), ("Physics", [
        (("0",), "Drop a crate"),
        (("Shift", "0"), "Drop a stack of 10 crates"),
    ]), ("Gravity gun, on foot", [
        (("RMB", "pad:LT"), "Grab / pull; again: drop"),
        (("LMB", "pad:RT"), "Punt (at the ground: jump)"),
        (("LMB",), "Held while holding: charge"),
        (("T",), "Track the last thrown"),
        (("Wheel", "pad:LB", "pad:RB"), "Hold distance"),
        (("R", "Mouse"), "Turn what it holds"),
    ])],
    [("World", [
        (("N",), "New terrain seed"),
        (("M", "J"), "Mountains higher / lower"),
        (("H", "G"), "Hills higher / lower"),
        (("Z", "X"), "Sun lower / higher"),
        (("T", "Y"), "Air denser / thinner"),
        (("B", "V"), "Exposure up / down"),
    ]), ("View", [
        (("F1",), "Settings"),
        (("F2",), "These controls"),
        (("I",), "Debug card"),
        (("F3",), "Wireframe"),
        (("O",), "Ocean"),
        (("C",), "Colour: terrain / face / level / biome"),
        (("K",), "Freeze the level of detail"),
        (("[", "]"), "Split factor down / up"),
        (("P",), "Screenshot"),
    ])],
]


def on_init():
    world.setup()
    build_site()
    build_cards()
    build_ship_model()
    build_cockpit()
    state["viewmodel"] = Viewmodel(engine, box())
    state["generated_cockpit"] = args.generated_cockpit
    state["cabin_screens"] = cockpit_screens()
    state["instruments"] = Instruments(engine, len(state["cabin_screens"]), FOV)
    state["terrain_edit"] = planet_settings.TerrainEdit(world)
    state["menu"] = SettingsMenu(engine, "Settings", menu_items())
    state["help"] = HelpOverlay(engine, "CONTROLS", HELP_COLUMNS, footer="F2, or Back on a gamepad, to close")
    teleport(1)

    # Before the transform system (priority 0), so this frame's positions are used.
    engine.ecs.add_system("PlanetFrame", frame, priority=-1)
    if args.capture:
        engine.ecs.add_system("PlanetCapture", CaptureTour(args.capture, args.shots), priority=-2)
    elif args.verify:
        engine.ecs.add_system("PlanetVerify", Verify(), priority=-2)
    elif args.orbit_check:
        engine.ecs.add_system("PlanetOrbitCheck", OrbitCheck(), priority=-2)
    elif args.morph_check:
        engine.ecs.add_system("PlanetMorphCheck", MorphCheck(), priority=-2)
    elif args.crack_check:
        engine.ecs.add_system("PlanetCrackCheck", CrackCheck(), priority=-2)
    elif args.landing_check:
        engine.ecs.add_system("PlanetLandingCheck", LandingCheck("planet"), priority=-2)
    elif args.moon_landing_check:
        engine.ecs.add_system("PlanetLandingCheck", LandingCheck("moon"), priority=-2)
    elif args.walk_check:
        engine.ecs.add_system("PlanetWalkCheck", WalkCheck(), priority=-2)
    elif args.prop_rest_check:
        engine.ecs.add_system("PlanetPropRestCheck", PropRestCheck(), priority=-2)
    elif args.hold_check:
        engine.ecs.add_system("PlanetHoldCheck", HoldCheck(), priority=-2)
    elif args.punt_check:
        engine.ecs.add_system("PlanetPuntCheck", PuntCheck(), priority=-2)
    elif args.trunk_check:
        engine.ecs.add_system("PlanetTrunkCheck", TrunkCheck(), priority=-2)
    elif args.promotion_check:
        engine.ecs.add_system("PlanetPromotionCheck", PromotionCheck(), priority=-2)
    elif args.orbit_crate_check:
        engine.ecs.add_system("PlanetOrbitCrateCheck", OrbitCrateCheck(), priority=-2)
    print(__doc__.split("Keys:")[1].rstrip(), flush=True)


# Keys that change settings in the menu too, shown beside them.
MENU_KEYS = {"Exposure": ("B", "V"), "Atmosphere density": ("T", "Y"), "Sun elevation": ("Z", "X"),
             "Ocean": ("O",), "Wireframe": ("F3",), "Split factor": ("[", "]"),
             "Colour: terrain / face / level / biome": ("C",), "New seed": ("N",)}


def menu_items():
    """The settings menu (toolkit.overlay.SettingsMenu): the simulation's and the display's own
    settings, and planet.settings' sky, clouds, ground, view and planet,
    the planet's kept in its TerrainEdit until Apply builds it again."""
    def set_assist(on):
        state["controls"].assist = on

    return [
        {"heading": "Simulation"},
        {"slider": "Time warp", "min": 0, "max": len(WARPS) - 1, "get": lambda: state["warp"],
         "set": lambda v: state.update(warp=int(round(v))), "show": lambda v: "x%d" % WARPS[int(round(v))],
         "keys": (",", "."), "hint": "How much faster than real time the ship flies and the moon moves."},
        {"toggle": "Translation assist", "get": lambda: state["controls"].assist, "set": set_assist,
         "keys": ("L",), "hint": "The throttle asks for a speed and E / Q for a climb rate; the thrusters hold them."},
        {"toggle": "Head bob", "get": lambda: state["head_bob"], "set": lambda on: state.update(head_bob=on),
         "hint": "On foot, the view rises and falls a little with each step."},
    ] + planet_settings.sky_items(world, MENU_KEYS, site="the blocks") + planet_settings.cloud_items(world, MENU_KEYS) \
        + planet_settings.ground_items(world, MENU_KEYS) + planet_settings.climate_items(world, MENU_KEYS) + [
        {"heading": "Display"},
        {"toggle": "Button hints", "get": lambda: state["hints"], "set": lambda on: state.update(hints=on),
         "hint": "The bar of keys or gamepad buttons along the bottom of the screen."},
        {"toggle": "Orbit path", "get": lambda: state["orbit_path"], "set": lambda on: state.update(orbit_path=on),
         "hint": "The path the ship would coast along, around the body it is reckoned around."},
        {"toggle": "Debug text", "get": lambda: state["debug_text"], "set": lambda on: state.update(debug_text=on),
         "keys": ("I",), "hint": "The debug card: frame time, chunks, the pool and the terrain's settings."},
    ] + planet_settings.view_items(world, MENU_KEYS) \
        + planet_settings.planet_items(world, state["terrain_edit"], new_seed, MENU_KEYS)


def new_seed():
    world.new_seed([random.uniform(-100.0, 100.0) for _ in range(3)])
    state["terrain_edit"].reset()


def set_wireframe(on):
    world.set_wireframe(on)


def set_colour_mode(mode):
    world.colour_mode = mode


def change_terrain(name, index, factor):
    world.change_terrain(name, index, factor)
    state["terrain_edit"].reset()


def on_key(key, from_pad=False):
    """A key pressed, or the key a gamepad button stands for (`from_pad`)."""
    if not from_pad:
        state["input"] = "keys"
    tree = planet.tree
    shift = engine.input.is_key_down(keys.LEFT_SHIFT) or engine.input.is_key_down(keys.RIGHT_SHIFT)
    if key == keys.NUM_0:
        drop_crates(STACK_COUNT if shift else 1)
    elif keys.NUM_1 <= key <= keys.NUM_9:
        teleport(key - keys.NUM_0, moon=shift)
    elif key == keys.TAB:
        if state["mode"] == "ship":
            leave_ship()
        elif state["mode"] == "foot":
            state["mode"] = "camera"
        else:
            go_on_foot()
    elif key == keys.F:
        if state["mode"] == "foot":
            get_in()
        elif state["mode"] == "ship":
            get_out()
    elif key == keys.SPACE and state["mode"] == "foot":
        state["foot_controls"].jump = True
    elif key == keys.R and state["mode"] != "foot":
        state["chase"] = not state["chase"]
    elif key == keys.U:
        state["generated_cockpit"] = not state["generated_cockpit"]
    elif key == keys.L:
        state["controls"].assist = not state["controls"].assist
    elif key == keys.BACKSPACE:
        state["controls"].throttle = 0.0
    elif key in (keys.COMMA, keys.PERIOD) and state["mode"] == "foot":
        state["gun_controls"].distance += 1.0 if key == keys.PERIOD else -1.0
    elif key in (keys.COMMA, keys.PERIOD):
        state["warp"] = max(0, min(len(WARPS) - 1, state["warp"] + (1 if key == keys.PERIOD else -1)))
    elif key == keys.O:
        world.set_ocean(not world.ocean)
    elif key == keys.N:
        new_seed()
    elif key == keys.F1:
        state["menu"].show(not state["menu"].visible)
    elif key == keys.F2:
        state["help"].show(not state["help"].visible)
    elif key == keys.I:
        state["debug_text"] = not state["debug_text"]
    elif key == keys.M:
        change_terrain("mountains", 1, 1.25)
    elif key == keys.J:
        change_terrain("mountains", 1, 1 / 1.25)
    elif key == keys.H:
        change_terrain("hills", 1, 1.25)
    elif key == keys.G:
        change_terrain("hills", 1, 1 / 1.25)
    elif key in (keys.Z, keys.X):
        world.sun_elevation = max(-90.0, min(90.0, world.sun_elevation + (3.0 if key == keys.X else -3.0)))
        world.upload_sky()
    elif key == keys.T and state["mode"] == "foot":
        thrown = state.get("last_thrown")
        state["tracked"] = None if state["tracked"] is not None or thrown not in props.all else thrown
    elif key in (keys.T, keys.Y):
        world.density *= 1.25 if key == keys.T else 1 / 1.25
        world.upload_sky()
    elif key in (keys.B, keys.V):
        world.exposure *= 1.25 if key == keys.B else 1 / 1.25
        world.upload_sky()
    elif key == keys.F3:
        set_wireframe(not world.wireframe)
    elif key == keys.C:
        set_colour_mode((world.colour_mode + 1) % len(COLOUR_MODES))
    elif key == keys.K:
        world.frozen = not world.frozen
    elif key == keys.LEFT_BRACKET and tree is not None:
        tree.split_factor = max(0.5, tree.split_factor / 1.25)
    elif key == keys.RIGHT_BRACKET and tree is not None:
        tree.split_factor = min(8.0, tree.split_factor * 1.25)
    elif key == keys.P:
        os.makedirs(CAPTURES, exist_ok=True)
        path = os.path.join(CAPTURES, time.strftime("planet_%Y%m%d_%H%M%S.png"))
        if engine.capture_screenshot(path):
            print("saved", path, flush=True)


def on_scroll(x_offset, y_offset):
    state["input"] = "keys"
    if state["menu"].pointer_over:
        return
    if state["mode"] == "foot":
        state["gun_controls"].distance += y_offset
        return
    state["speed_factor"] = max(0.01, min(100.0, state["speed_factor"] * 1.25 ** y_offset))


def settled():
    """The planet's tree made, no body's chunks missing, none still to
    build again for the sun, every built chunk's heights back, and no ship
    waiting to be set down on the moon."""
    return world.settled() and state["moon_landing"] is None


def look_up(degrees):
    """The free camera's view turned `degrees` up from level, keeping its heading."""
    radial = normalize(state["position"])
    level = normalize(sub(state["forward"], scale(radial, dot(state["forward"], radial))))
    up = math.radians(degrees)
    state["forward"] = normalize(add(scale(level, math.cos(up)), scale(radial, math.sin(up))))


def overcast():
    """Clouds over 95 % of the sky."""
    world.cloud_coverage = 0.95
    world.upload_clouds()


def lift_camera(height):
    """The free camera `height` metres above the sea, where it is, looking level."""
    radial = normalize(state["position"])
    state["position"] = scale(radial, RADIUS + height)
    look_up(0.0)


def lift_ship(height):
    """The ship `height` metres above the sea, where it is, at rest in the air with assist on."""
    ship = state["ship"]
    ship.position = scale(normalize(ship.position), RADIUS + height)
    ship.velocity = (0.0, 0.0, 0.0)
    state["position"] = ship.position


def mid_reentry():
    """The ship 17 km above the blocks at 640 m/s eastwards, 8 degrees
    below level, its nose along its path: in the reentry glow."""
    _, up, east, _ = world.site_frame()
    down = math.radians(8.0)
    forward = normalize(add(scale(east, math.cos(down)), scale(up, -math.sin(down))))
    ship_up = normalize(sub(up, scale(forward, dot(up, forward))))
    position = scale(up, RADIUS + 17_000.0)
    state["ship"] = flight.Ship(position=position, velocity=scale(forward, 640.0),
                                orientation=flight.quat_from_basis(cross(forward, ship_up), ship_up,
                                                                   scale(forward, -1.0)))
    state["controls"] = flight.Controls(assist=False)
    state["mode"] = "ship"
    state["position"] = position
    state["forward"] = forward


def moon_body():
    return next(body for body in bodies if body.name == "moon")


def look_at_moon():
    """The free camera's view turned towards the moon's centre."""
    state["forward"] = normalize(sub(moon_body().centre(), state["position"]))


def near_moon(height, sun_elevation, down=0.0):
    """The free camera `height` metres above the moon's sphere where the
    sun stands `sun_elevation` degrees up, looking level across the sun's
    direction, turned `down` degrees below level."""
    moon = moon_body()
    sun = world.sun_direction()
    across = normalize(cross(sun, (0.0, 1.0, 0.0) if abs(sun[1]) < 0.9 else (1.0, 0.0, 0.0)))
    up = rotate(sun, across, math.radians(90.0 - sun_elevation))
    state["mode"] = "camera"
    state["position"] = add(moon.centre(), scale(up, moon.radius + height))
    tilt = math.radians(down)
    state["forward"] = normalize(add(scale(across, math.cos(tilt)), scale(up, -math.sin(tilt))))


def over_ground(distance, azimuth, height, down):
    """The free camera `height` metres above the site's ground level,
    `distance` metres from the site at `azimuth` degrees from east towards
    north, looking that way and `down` degrees below level (it stays above
    the ground there)."""
    _, up, east, north = world.site_frame()
    a = math.radians(azimuth)
    level = add(scale(east, math.cos(a)), scale(north, math.sin(a)))
    spot = normalize(add(up, scale(level, distance / RADIUS)))
    spot_east, spot_north = local_frame(spot)
    ahead = normalize(add(scale(spot_east, math.cos(a)), scale(spot_north, math.sin(a))))
    state["mode"] = "camera"
    state["position"] = scale(spot, RADIUS + (world.site_height or 0.0) + height)
    state["forward"] = normalize(add(ahead, scale(spot, -math.tan(math.radians(down)))))


def near_mountain(sun_elevation, sun_azimuth, top=False):
    """The free camera 2.7 km above the sphere, 600 m south-west of a peak
    60 km from the site at 67.5 degrees from east (2,460 m high with the
    default terrain), looking north-east at it and down 12 degrees; with
    `top`, 2,520 m above the sphere over the peak, looking south-west and
    down 20 degrees. The sun at `sun_elevation` and `sun_azimuth` (degrees,
    from east towards north) seen from the peak."""
    _, up, east, north = world.site_frame()
    azimuth = math.radians(67.5)
    level = add(scale(east, math.cos(azimuth)), scale(north, math.sin(azimuth)))
    peak = normalize(add(up, scale(level, 60_000.0 / RADIUS)))
    peak_east, peak_north = local_frame(peak)
    north_east = normalize(add(peak_east, peak_north))
    spot = normalize(add(peak, scale(north_east, -600.0 / RADIUS)))
    if top:
        state["position"] = scale(peak, RADIUS + 2_520.0)
        state["forward"] = normalize(add(scale(north_east, -1.0), scale(peak, -math.tan(math.radians(20.0)))))
    else:
        state["position"] = scale(spot, RADIUS + 2_700.0)
        state["forward"] = normalize(add(north_east, scale(spot, -math.tan(math.radians(12.0)))))
    elevation, azimuth = math.radians(sun_elevation), math.radians(sun_azimuth)
    sun = add(scale(peak, math.sin(elevation)),
              scale(add(scale(peak_east, math.cos(azimuth)), scale(peak_north, math.sin(azimuth))), math.cos(elevation)))
    world.sun_elevation = math.degrees(math.asin(max(-1.0, min(1.0, dot(sun, up)))))
    world.sun_azimuth = math.degrees(math.atan2(dot(sun, north), dot(sun, east))) % 360.0
    world.upload_sky()


class CaptureTour:
    """--capture: each shot in turn, a screenshot once settled (or after
    MAX_FRAMES), then exit. Waits for the site's height first."""

    MIN_FRAMES, MAX_FRAMES = 10, 600
    # (file name, preset, wireframe, colour mode, terrain change or None, sun elevation and azimuth,
    #  then any functions to call once it is set up)
    SHOTS = [("preset_%d" % p, p, False, 0, None, DAY_SUN) for p in range(1, 7)] + [
        ("preset_1_sunset", 1, False, 0, None, (3.0, 45.0)),
        ("preset_2_sunset", 2, False, 0, None, (2.0, 90.0)),
        ("preset_3_low_sun", 3, False, 0, None, (8.0, 45.0)),
        ("preset_4_terminator", 4, False, 0, None, (0.0, 200.0)),
        ("preset_7_coast", 7, False, 0, None, DAY_SUN),
        ("preset_7_sun_on_the_sea", 7, False, 0, None, (12.0, 135.0)),
        ("preset_3_ocean_sun", 3, False, 0, None, (50.0, 135.0)),
        ("preset_8_ship_on_the_pad", 8, False, 0, None, DAY_SUN),
        ("preset_9_ship_in_orbit", 9, False, 0, None, DAY_SUN),
        ("preset_8_ship_sunset", 8, False, 0, None, (6.0, 90.0)),
        ("preset_8_menu_and_throttle", 8, False, 0, None, DAY_SUN,
         lambda: state["menu"].show(True), lambda: setattr(state["controls"], "throttle", 0.6)),
        ("controls_sheet", 1, False, 0, None, DAY_SUN, lambda: state["help"].show(True)),
        ("chase_gamepad_hints", 8, False, 0, None, DAY_SUN, lambda: state.update(chase=True, input="pad")),
        ("preset_1_wireframe_levels", 1, True, 2, None, DAY_SUN, lambda: state.update(debug_text=True)),
        ("preset_2_wireframe", 2, True, 0, None, DAY_SUN),
        ("preset_2_twilight", 2, False, 0, None, (-3.0, 90.0)),
        ("orbit_from_afar", 9, False, 0, None, DAY_SUN, lambda: teleport(4)),
        ("reentry", 9, False, 0, None, DAY_SUN, mid_reentry),
        ("clouds_terminator", 3, False, 0, None, (0.0, 200.0)),
        ("clouds_below_sunset", 1, False, 0, None, (4.0, 45.0), lambda: look_up(25.0)),
        ("clouds_inside", 2, False, 0, None, DAY_SUN, overcast, lambda: lift_camera(2_500.0)),
        ("clouds_cockpit_inside", 8, False, 0, None, DAY_SUN, overcast, lambda: lift_ship(2_500.0)),
        ("clouds_from_above", 2, False, 0, None, DAY_SUN, lambda: lift_camera(6_000.0), lambda: look_up(-25.0)),
        ("clouds_level", 2, False, 0, None, DAY_SUN, lambda: lift_camera(2_500.0)),
        ("clouds_from_below", 2, False, 0, None, DAY_SUN, lambda: lift_camera(800.0), lambda: look_up(10.0)),
        ("vegetation_grassland", 1, False, 0, None, DAY_SUN, lambda: over_ground(3_000.0, 180.0, 120.0, 12.0)),
        ("vegetation_conifers", 1, False, 0, None, DAY_SUN, lambda: over_ground(6_000.0, 270.0, 1_800.0, 12.0)),
        ("vegetation_mixed_wood", 1, False, 0, None, DAY_SUN, lambda: over_ground(8_000.0, 180.0, 260.0, 10.0)),
        ("grass_meadow", 1, False, 0, None, DAY_SUN, lambda: over_ground(3_000.0, 180.0, 2.5, 6.0)),
        ("grass_wood_edge", 1, False, 0, None, DAY_SUN, lambda: over_ground(8_000.0, 180.0, 3.0, 4.0)),
        ("mountain_close", 7, False, 0, None, DAY_SUN, lambda: near_mountain(40.0, 160.0)),
        ("mountain_close_low_sun", 7, False, 0, None, DAY_SUN, lambda: near_mountain(10.0, 135.0)),
        ("mountain_top", 7, False, 0, None, DAY_SUN, lambda: near_mountain(15.0, 135.0, top=True)),
        ("mountain_top_no_detail", 7, False, 0, ("detail", 1, 0.0), DAY_SUN,
         lambda: near_mountain(15.0, 135.0, top=True)),
        ("preset_3_mountains_higher", 3, False, 0, ("mountains", 1, 1.5), DAY_SUN),
        ("moon_from_the_site", 1, False, 0, None, DAY_SUN, look_at_moon),
        ("moon_from_afar", 1, False, 0, None, DAY_SUN, lambda: near_moon(40_000.0, 30.0, down=45.0)),
        ("moon_surface", 1, False, 0, None, DAY_SUN, lambda: near_moon(300.0, 15.0, down=12.0)),
        ("moon_low_sun", 1, False, 0, None, DAY_SUN, lambda: near_moon(1_000.0, 5.0, down=25.0)),
        ("ship_on_the_moon", 1, False, 0, None, DAY_SUN, lambda: teleport(8, moon=True)),
        ("ship_around_the_moon", 1, False, 0, None, DAY_SUN, lambda: teleport(9, moon=True)),
        ("generated_cockpit_on_the_pad", 8, False, 0, None, DAY_SUN,
         lambda: state.update(generated_cockpit=True)),
        ("chase_on_the_pad", 8, False, 0, None, DAY_SUN, lambda: state.update(chase=True)),
        ("chase_in_orbit", 9, False, 0, None, DAY_SUN, lambda: state.update(chase=True)),
        ("chase_on_the_moon", 1, False, 0, None, DAY_SUN, lambda: teleport(8, moon=True),
         lambda: state.update(chase=True)),
    ]

    def __init__(self, directory, names=None):
        """`names`: only the shots whose names contain one of these."""
        os.makedirs(directory, exist_ok=True)
        self.directory = directory
        if names:
            self.SHOTS = [shot for shot in self.SHOTS if any(name in shot[0] for name in names)]
        self.shot = 0
        self.frames = 0
        self.captured = None

    def __call__(self, dt):
        if world.site_height is None:
            return True
        name, preset, wireframe, colour_mode, change, sun, *setup = self.SHOTS[self.shot]
        self.frames += 1
        if self.frames == 1:
            if change is not None:
                change_terrain(*change)
            world.sun_elevation, world.sun_azimuth = sun
            world.upload_sky()
            world.cloud_coverage, world.cloud_wind = CLOUD_COVERAGE, (0.0, 0.0, 0.0)
            world.upload_clouds()
            state["chase"] = False
            state["generated_cockpit"] = False
            state["debug_text"] = False
            teleport(preset)
            set_wireframe(wireframe)
            set_colour_mode(colour_mode)
            state["menu"].show(False)
            state["help"].show(False)
            state["input"] = "keys"
            for action in setup:
                action()
        elif self.captured is None:
            done = self.frames >= self.MIN_FRAMES and settled()
            if done or self.frames >= self.MAX_FRAMES:
                path = os.path.join(self.directory, name + ".png")
                ok = engine.capture_screenshot(path)
                selected = sum(len(body.tree.selected) for body in bodies if body.tree is not None)
                print("[planet] %s after %d frames%s, %d of %d chunks drawn, frame %.1f ms"
                      % (path if ok else "screenshot failed", self.frames, "" if done else " (not settled)",
                         selected - world.chunks_culled, selected, state["frame_ms"]), flush=True)
                self.captured = self.frames
        elif self.frames > self.captured + 2:
            self.shot += 1
            self.frames = 0
            self.captured = None
            if self.shot == len(self.SHOTS):
                os._exit(0)        # the engine has no quit call
        return True


class Verify:
    """--verify: at preset 1, once settled, read ChunkPool back and check
    every built chunk against cube_sphere.chunk_mesh:
      - directions and face coordinates equal to its, rounded to 32-bit;
      - positions equal to direction * (radius + height - skirt) - centre
        worked out from its directions and the GPU's heights, the skirt the
        larger of the job's depth and the grid's height range, to within one
        unit in the last place (ULP) of the chunk's largest coordinate;
      - normals of unit length, on the outer side;
      - each slot's lowest and highest grid heights in ChunkHeights equal to
        those of the heights in the pool.
    Prints the largest differences and exits with status 0 when all hold,
    1 otherwise."""

    MIN_FRAMES, MAX_FRAMES = 10, 1200
    TOLERANCE_ULPS = 1.0

    def __init__(self):
        self.frames = 0
        self.requested = None

    def __call__(self, dt):
        if world.site_height is None:
            return True
        self.frames += 1
        if self.frames == 1:
            teleport(1)
        elif self.requested is None:
            if (self.frames >= self.MIN_FRAMES and settled()) or self.frames >= self.MAX_FRAMES:
                engine.request_readback("ChunkPool")
                self.requested = self.frames
        else:
            readback = engine.get_readback("ChunkPool")
            if readback is not None:
                self.check(readback)
        return True

    def check(self, readback):
        gpu = np.frombuffer(readback["data"], dtype=np.float32).reshape(SLOTS, VERTICES_PER_CHUNK,
                                                                       FLOATS_PER_VERTEX)
        heights = np.frombuffer(engine.get_readback("ChunkHeights")["data"], dtype=np.float32).reshape(SLOTS, 4)
        grid = (QUADS + 1) ** 2
        worst = {"position (ULPs)": 0.0, "direction (ULPs)": 0.0, "face uv": 0.0,
                 "normal length - 1": 0.0, "lowest/highest height (m)": 0.0}
        lowest_normal_dot = 1.0
        chunks = {body.name: 0 for body in bodies}
        built_nodes = [(body, node) for body in bodies if body.tree is not None
                       for node in body.tree.nodes() if node.chunk is not None]
        for body, node in built_nodes:
            built = gpu[node.chunk]
            # The skirt's depth as the GPU works it out, in 32-bit: the job's, or the grid's height range.
            depth = max(np.float32(SKIRT_FRACTION * node.edge), built[:grid, 11].max() - built[:grid, 11].min())
            centre, positions, directions, uvs, _ = cube_sphere.chunk_mesh(
                node.face, node.level, node.x, node.y, QUADS, body.radius, skirt_depth=float(depth))
            height = built[:, 11].astype(np.float64)
            expected = (positions + directions * height[:, None]).astype(np.float32)
            ulp = float(np.spacing(np.abs(expected).max()))
            worst["position (ULPs)"] = max(worst["position (ULPs)"],
                                           float(np.abs(built[:, :3] - expected).max()) / ulp)
            expected = directions.astype(np.float32)
            worst["direction (ULPs)"] = max(worst["direction (ULPs)"], float(
                (np.abs(built[:, 8:11] - expected) / np.spacing(np.abs(expected))).max()))
            worst["face uv"] = max(worst["face uv"],
                                   float(np.abs(built[:, [3, 7]] - uvs.astype(np.float32)).max()))
            normals = built[:, 4:7].astype(np.float64)
            worst["normal length - 1"] = max(worst["normal length - 1"],
                                             float(np.abs(np.linalg.norm(normals, axis=1) - 1.0).max()))
            lowest_normal_dot = min(lowest_normal_dot, float((normals * directions).sum(axis=1).min()))
            worst["lowest/highest height (m)"] = max(
                worst["lowest/highest height (m)"],
                abs(float(heights[node.chunk, 0]) - float(built[:grid, 11].min())),
                abs(float(heights[node.chunk, 1]) - float(built[:grid, 11].max())))
            chunks[body.name] += 1
        print("[planet] checked %s against cube_sphere.chunk_mesh:"
              % ", ".join("%d chunks of the %s" % (n, name) for name, n in chunks.items()), flush=True)
        for name, value in worst.items():
            print("[planet]   largest %s difference %.3g" % (name, value), flush=True)
        print("[planet]   smallest normal . direction %.3f" % lowest_normal_dot, flush=True)
        ok = (all(n > 0 for n in chunks.values()) and worst["position (ULPs)"] <= self.TOLERANCE_ULPS and worst["direction (ULPs)"] == 0
              and worst["face uv"] == 0 and worst["normal length - 1"] < 1e-5 and lowest_normal_dot > 0.0
              and worst["lowest/highest height (m)"] == 0)
        os._exit(0 if ok else 1)


class MorphCheck:
    """--morph-check: once every chunk is built for the view 20 km above the
    site, the free camera comes down to 100 m, moving north at a third of
    its height per second of flight, in FRAMES steps (one per frame).
    Each frame it compares
    the drawn chunks with the frame before's: a chunk that appeared because
    its parent split, or that went because it merged, swaps drawn surfaces,
    which is invisible when every vertex of the chunk was fully morphed then
    (morph.glsl). The vertices lie in the node's bounding sphere, so the
    least factor is the one at the sphere's distance from the camera.
    Swaps where the children were built only after the camera came within
    the split distance are counted apart (they appear late, closer).
    Prints the swaps, the late ones and where they happened, and the least
    factor of the others, and exits with status 0 when none were late and
    that factor is at least 0.99, 1 otherwise."""

    FRAMES = 2400
    START, END = 20_000.0, 100.0

    def __init__(self):
        self.frame = -1
        self.previous = None
        self.swaps = 0
        self.late = 0
        self.least = 1.0
        self.travelled = 0.0             # metres north of the site
        self.step_length = 0.0           # metres the camera moved in the last frame
        self.late_heights = []           # the camera's height at each late swap

    def settled_at_start(self):
        """Puts the camera where the descent starts and keeps it there until
        every chunk it needs is built."""
        _, up, _, north = world.site_frame()
        state["mode"] = "camera"
        state["position"] = scale(up, RADIUS + (world.site_height or 0.0) + self.START)
        state["forward"] = normalize(add(north, scale(up, -0.3)))
        return settled()

    def factor(self, node, camera):
        if node.level == 0:
            return 1.0
        split = planet.tree.split_factor * 2.0 * node.edge
        distance = max(0.0, length(sub(camera, node.bound_centre)) - node.bound_radius)
        t = max(0.0, min(1.0, (distance - MORPH_START * split) / ((1.0 - MORPH_START) * split)))
        return t * t * (3.0 - 2.0 * t)

    def __call__(self, dt):
        tree = planet.tree
        if tree is None or not tree.selected:
            return True
        if self.frame < 0 and not self.settled_at_start():
            return True
        camera = state["position"]          # where the drawn selection was made
        drawn = {node.key(): node for node in tree.selected}
        if self.previous is not None:
            before = self.previous
            for key in drawn.keys() - before.keys():
                parent = before.get((key[0], key[1] - 1, key[2] // 2, key[3] // 2)) if key[1] > 0 else None
                if parent is None:
                    continue                 # not a split of a drawn parent
                self.swaps += 1
                parent_distance = max(0.0, length(sub(camera, parent.bound_centre)) - parent.bound_radius)
                if parent_distance < tree.split_factor * parent.edge - 2.0 * self.step_length:
                    self.late += 1
                    self.late_heights.append(length(camera) - RADIUS)
                else:
                    self.least = min(self.least, self.factor(drawn[key], camera))
            for key in before.keys() - drawn.keys():
                if key[1] == 0 or (key[0], key[1] - 1, key[2] // 2, key[3] // 2) not in drawn:
                    continue                 # not a merge into its parent
                self.swaps += 1
                self.least = min(self.least, self.factor(before[key], camera))
        self.previous = drawn
        self.frame += 1
        if self.frame == self.FRAMES:
            print("[planet] coming down from %.0f m to %.0f m in %d frames: %d swaps, %d late (children built "
                  "after the split distance), least morph factor of the others %.4f"
                  % (self.START, self.END, self.FRAMES, self.swaps, self.late, self.least), flush=True)
            if self.late_heights:
                print("[planet]   late swaps at camera heights from %.0f m to %.0f m (median %.0f m)"
                      % (min(self.late_heights), max(self.late_heights),
                         sorted(self.late_heights)[len(self.late_heights) // 2]), flush=True)
            os._exit(0 if self.least >= 0.99 and self.late == 0 else 1)
        # The next frame's camera.
        _, up, east, north = world.site_frame()
        height = self.START * (self.END / self.START) ** (self.frame / self.FRAMES)
        self.travelled += height / 3.0 / 60.0
        spot = normalize(add(up, scale(north, self.travelled / RADIUS)))
        new_position = scale(spot, RADIUS + (world.site_height or 0.0) + height)
        self.step_length = length(sub(new_position, camera))
        state["mode"] = "camera"
        state["position"] = new_position
        state["forward"] = normalize(add(north, scale(spot, -0.3)))
        return True


def morph_factor(node, point, level=1, body=None):
    """morph.glsl's factor towards the surface of a node's ancestor `level`
    levels up, for a vertex at `point` (relative to the centre of `body`,
    the planet when None) and the camera's present position; 0 where the
    node has no such ancestor."""
    body = planet if body is None else body
    if level > min(MORPH_LEVELS, node.level):
        return 0.0
    split = planet_world.parent_split_distance(node, body.tree) * 2.0 ** (level - 1)
    distance = length(sub(point, body.relative(state["position"])))
    t = max(0.0, min(1.0, (distance - MORPH_START * split) / ((1.0 - MORPH_START) * split)))
    return t * t * (3.0 - 2.0 * t)


def morphed_point(node, built, i, j, body=None):
    """Grid vertex (i, j) of a built chunk (its ChunkPool rows `built`) of
    `body` (the planet when None) relative to the body's centre, morphed as
    morph.glsl morphs it for the camera's present position: towards its
    parent's surface, then its grandparent's, and so on."""
    row = QUADS + 1
    vertex = lambda ij: add(node.centre, tuple(float(c) for c in built[ij[1] * row + ij[0], :3]))
    own = vertex((i, j))
    point = own
    for level in range(1, MORPH_LEVELS + 1):
        f = morph_factor(node, own, level, body)
        if f <= 0.0:
            break
        surface = (0.0, 0.0, 0.0)
        for ij, w in cube_sphere.stride_weights(i, j, 1 << level, QUADS):
            surface = add(surface, scale(vertex(ij), w))
        point = add(point, scale(sub(surface, point), f))
    return point


def drawn_height(body, gpu, point):
    """(metres above `body`'s radius, level of the chunk) of its surface as
    drawn (its selected chunks, morphed for the camera's present position,
    each quad split from its lowest to its highest corner) where the line
    from its centre through `point` (relative to its centre) meets it, from
    the ChunkPool rows `gpu`; None where no selected chunk holds that
    direction."""
    d = normalize(point)
    face, u, v = cube_sphere.face_uv(d)
    for node in body.tree.selected:
        if node.face != face or node.chunk is None:
            continue
        u0, v0, size = cube_sphere.chunk_bounds(node.level, node.x, node.y)
        if not (u0 <= u <= u0 + size and v0 <= v <= v0 + size):
            continue
        x, y = (u - u0) / size * QUADS, (v - v0) / size * QUADS
        i, j = min(int(x), QUADS - 1), min(int(y), QUADS - 1)
        fi, fj = x - i, y - j
        corner = lambda di, dj: np.array(morphed_point(node, gpu[node.chunk], i + di, j + dj, body))
        a, c = corner(0, 0), corner(1, 1)
        b = corner(1, 0) if fi >= fj else corner(0, 1)
        n = np.cross(b - a, c - a)
        return float(np.dot(a, n) / np.dot(np.array(d), n)) - body.radius, node.level
    return None


class CrackCheck:
    """--crack-check: at each view in VIEWS, once settled, freezes the level
    of detail, reads ChunkPool back and, for every drawn chunk's edge whose
    neighbour is coarser by k levels, measures the gap at each of the
    chunk's edge vertices between it and the neighbour's edge, the straight
    line between the vertices the two share (every 2^k-th). A gap where the
    finer edge is higher must be covered by the finer chunk's skirt, one
    where it is lower by the coarser chunk's. Prints per view the edges with
    a coarser neighbour, the largest gap, and the largest gap as a share of
    the skirt that covers it; exits with status 0 when no share is above 1,
    1 otherwise."""

    MIN_FRAMES, MAX_FRAMES = 10, 1200
    VIEWS = [
        ("preset 1", lambda: teleport(1)),
        ("preset 2", lambda: teleport(2)),
        ("preset 6, the cube's corner", lambda: teleport(6)),
        ("beside the peak", lambda: (teleport(7), near_mountain(40.0, 160.0))),
        ("over the peak", lambda: (teleport(7), near_mountain(40.0, 160.0, top=True))),
        ("over the peak, mountains twice as high",
         lambda: (change_terrain("mountains", 1, 2.0), teleport(7), near_mountain(40.0, 160.0, top=True))),
    ]

    def __init__(self):
        self.view = 0
        self.frames = 0
        self.requested = False
        self.worst = 0.0

    def __call__(self, dt):
        if planet.tree is None:
            return True
        self.frames += 1
        if self.frames == 1:
            world.frozen = False
            self.VIEWS[self.view][1]()
        elif not self.requested:
            if (self.frames >= self.MIN_FRAMES and settled()) or self.frames >= self.MAX_FRAMES:
                world.frozen = True
                previous = engine.get_readback("ChunkPool")
                self.after = previous["frame"] if previous is not None else -1
                engine.request_readback("ChunkPool")
                self.requested = True
        else:
            readback = engine.get_readback("ChunkPool")
            if readback is not None and readback["frame"] > self.after:
                self.measure(np.frombuffer(readback["data"], dtype=np.float32).reshape(
                    SLOTS, VERTICES_PER_CHUNK, FLOATS_PER_VERTEX))
                self.view += 1
                self.frames = 0
                self.requested = False
                if self.view == len(self.VIEWS):
                    os._exit(0 if self.worst <= 1.0 else 1)
        return True

    def measure(self, gpu):
        selected = planet.tree.selected
        by_key = {(n.face, n.level, n.x, n.y): n for n in selected}
        row = QUADS + 1

        def selected_at(d):
            face, u, v = cube_sphere.face_uv(d)
            for level in range(MAX_LEVEL + 1):
                cells = 1 << level
                x = min(max(int((u + 1.0) * 0.5 * cells), 0), cells - 1)
                y = min(max(int((v + 1.0) * 0.5 * cells), 0), cells - 1)
                if (face, level, x, y) in by_key:
                    return by_key[face, level, x, y]
            return None

        edges, largest_gap, largest_share = 0, 0.0, 0.0
        largest_drawn_gap, largest_drawn_share, largest_drawn_pixels = 0.0, 0.0, 0.0
        for node in selected:
            u0, v0, size = cube_sphere.chunk_bounds(node.level, node.x, node.y)
            # Per edge: its grid indices in order, and a point just outside its middle.
            sides = [
                (list(range(row)), (u0 + 0.5 * size, v0 - 1e-4 * size)),
                ([j * row + QUADS for j in range(row)], (u0 + size + 1e-4 * size, v0 + 0.5 * size)),
                ([QUADS * row + i for i in range(row)], (u0 + 0.5 * size, v0 + size + 1e-4 * size)),
                ([j * row for j in range(row)], (u0 - 1e-4 * size, v0 + 0.5 * size)),
            ]
            built = gpu[node.chunk]
            fine_skirt = planet_world.skirt_depth(node)
            for indices, (u, v) in sides:
                # Past a face's edge the cube point leaves the cube; normalising puts it on the sphere.
                other = selected_at(normalize(cube_sphere.spherify(cube_sphere.cube_point(node.face, u, v))))
                if other is None or other.level >= node.level:
                    continue
                neighbour = other.level
                edges += 1
                step = 1 << (node.level - neighbour)
                coarse_skirt = planet_world.skirt_depth(other)
                points = [add(node.centre, tuple(float(c) for c in built[k, :3])) for k in indices]
                # As drawn: both chunks' vertices morphed (morph.glsl), the coarse ones found by position.
                drawn_points = [morphed_point(node, built, k % row, k // row) for k in indices]
                coarse = gpu[other.chunk][:row * row, :3].astype(np.float64) + np.array(other.centre)
                for i in range(row):
                    i0 = (i // step) * step
                    if i0 == i:
                        continue
                    t = (i - i0) / step
                    a, b = points[i0], points[i0 + step]
                    line = add(a, scale(sub(b, a), t))
                    gap = length(points[i]) - length(line)
                    share = gap / fine_skirt if gap > 0.0 else -gap / coarse_skirt
                    largest_gap = max(largest_gap, abs(gap))
                    if share > largest_share:
                        largest_share = share
                        where = (node.level, neighbour, gap, fine_skirt if gap > 0.0 else coarse_skirt,
                                 length(sub(points[i], state["position"])))
                    ends = []
                    for p in (a, b):
                        kc = int(np.argmin(((coarse - np.array(p)) ** 2).sum(axis=1)))
                        ends.append(morphed_point(other, gpu[other.chunk], kc % row, kc // row))
                    drawn_line = add(ends[0], scale(sub(ends[1], ends[0]), t))
                    drawn_gap = length(drawn_points[i]) - length(drawn_line)
                    distance = length(sub(drawn_points[i], state["position"]))
                    largest_drawn_share = max(largest_drawn_share,
                                              drawn_gap / fine_skirt if drawn_gap > 0.0 else -drawn_gap / coarse_skirt)
                    largest_drawn_pixels = max(largest_drawn_pixels, abs(drawn_gap) / distance / PIXEL_ANGLE)
                    if abs(drawn_gap) > largest_drawn_gap:
                        largest_drawn_gap = abs(drawn_gap)
                        drawn_where = (node.level, neighbour, morph_factor(node, points[i]),
                                       max(morph_factor(other, p) for p in (a, b)), distance)
        self.worst = max(self.worst, largest_share, largest_drawn_share)
        print("[planet] %s: %d chunks selected, %d edges with a coarser neighbour, largest gap %.3f m, "
              "largest gap / skirt %.3f" % (self.VIEWS[self.view][0], len(selected), edges, largest_gap,
                                            largest_share), flush=True)
        print("[planet]   as drawn (morphed): largest gap %.3f m, largest gap / skirt %.3f, largest gap %.2f pixels"
              % (largest_drawn_gap, largest_drawn_share, largest_drawn_pixels), flush=True)
        if largest_share > 0.0:
            print("[planet]   there: level %d beside level %d, gap %+.2f m, skirt %.2f m, %.0f m from the camera"
                  % where, flush=True)
        if largest_drawn_gap > 0.0:
            print("[planet]   largest as drawn: level %d beside level %d, the finer edge morphed %.3f, the coarser "
                  "%.3f, %.0f m from the camera" % drawn_where, flush=True)


class LandingCheck:
    """--landing-check, on the planet, and --moon-landing-check, on the
    moon: measures the ground 9 km below spots on rings around a centre
    (on the planet 10 to 60 km around the site, on the moon 2 to 12 km
    around moon_spot) with the free camera, and the slope there from
    probes 20 m east and north; of those no steeper than 10 degrees takes
    the highest on the planet and the lowest on the moon, and brings the
    ship down onto it from 500 m above with translation assist and full
    downward thrust asked for; once it touches, lets go of the controls
    for ten seconds on the planet, thirty on the moon. Prints the
    touchdown speed, the slope there, how far each gear point is below the
    ground patch's surface, how far the patch's height under the ship is
    from the probe's, each gear point's height above the terrain probed at
    its own direction, and the ship's speed. Then a ship coasts straight at
    the spot from 1.5 km up and 1.5 km away at 400 m/s and 16x warp; prints
    whether it crashed and how high its centre ended above the ground.
    Exits with status 0 when the ship touched down under half the gear's
    crash speed, at rest on its gear, uncrashed, with the patch's surface
    within 5 cm of the terrain at every gear point and, on the planet,
    every gear point within 5 cm of both; and the diving ship crashed with
    its centre at least 1 m above the ground and within reach of it; 1
    otherwise. Positions are reckoned from the body's centre and speeds
    relative to its surface."""

    MAX_SLOPE = 10.0                    # degrees: steeper spots are left out
    SLOPE_SPAN = 20.0                   # m east and north of a spot where its slope is measured
    RINGS = {"planet": (10_000.0, 20_000.0, 35_000.0, 60_000.0), "moon": (2_000.0, 4_000.0, 7_000.0, 12_000.0)}
    AZIMUTHS = 16
    FRAMES_PER_SPOT = 8                 # more than the probes' latency
    GEAR_TOLERANCE = 0.05
    REST_SECONDS = {"planet": 10.0, "moon": 30.0}
    TIMEOUT_SECONDS = 90.0
    DIVE_SPEED = 400.0
    DIVE_MIN_CENTRE_HEIGHT = 1.0        # m; half the gear's depth below the centre

    def __init__(self, body_name):
        self.body = next(body for body in bodies if body.name == body_name)
        self.rings = self.RINGS[body_name]
        self.lowest = self.body is not planet
        if self.body is planet:
            _, up, east, north = world.site_frame()
            self.around = "the site"
        else:
            up = moon_spot()
            east, north = local_frame(up)
            self.around = "the spot between the sun and the planet"
        self.spots = []
        for ring in self.rings:
            for k in range(self.AZIMUTHS):
                azimuth = 2.0 * math.pi * k / self.AZIMUTHS
                level = add(scale(east, math.cos(azimuth)), scale(north, math.sin(azimuth)))
                self.spots.append(normalize(add(up, scale(level, ring / self.body.radius))))
        self.spot = -1
        self.frames = 0
        self.best = None                # (ground height, direction)
        self.touched = None             # flight time when the ship touched
        self.diving = None              # flight time when the dive started
        self.touchdown_speed = 0.0

    def __call__(self, dt):
        if self.body.tree is None:
            return True
        if self.best is None or self.spot < len(self.spots):
            return self.search()
        if self.diving is not None:
            return self.dive()
        return self.land()

    def search(self):
        self.frames += 1
        if self.spot >= 0 and self.frames < self.FRAMES_PER_SPOT:
            return True
        if self.spot >= 0 and world.ground is not None:
            height = world.ground
            slope = self.slope()
            if slope is not None and slope <= self.MAX_SLOPE and (
                    self.best is None or (height < self.best[0] if self.lowest else height > self.best[0])):
                self.best = (height, self.spots[self.spot])
                self.best_place = (self.rings[self.spot // self.AZIMUTHS],
                                   360.0 * (self.spot % self.AZIMUTHS) / self.AZIMUTHS, slope)
        self.spot += 1
        self.frames = 0
        if self.spot < len(self.spots):
            state["mode"] = "camera"
            state["position"] = add(self.body.centre(), scale(self.spots[self.spot], self.body.radius + 9_000.0))
            world.ground = None
            world.extra_probes = self.slope_probes(self.spots[self.spot])
        else:
            world.extra_probes = []
            height, up = self.best
            east, north = local_frame(up)
            state["ship"] = flight.Ship(position=self.at(up, height + 500.0), velocity=self.body.velocity(),
                                        orientation=flight.quat_from_basis(east, up, scale(north, -1.0)))
            state["controls"] = flight.Controls(vertical=-1.0, assist=True)
            state["scripted_controls"] = True
            state["mode"] = "ship"
            state["warp"] = 0
            world.patch = None
            self.start = world.time
            print("[planet] the %s spot found on the %s with a slope of at most %.0f degrees: %.0f m, %.0f km from %s "
                  "at %.1f degrees from east, %.1f degrees steep"
                  % ("lowest" if self.lowest else "highest", self.body.name, self.MAX_SLOPE, height,
                     self.best_place[0] / 1000.0, self.around, self.best_place[1], self.best_place[2]), flush=True)
        return True

    def slope_probes(self, direction):
        """Probes at `direction` and SLOPE_SPAN east and north of it."""
        east, north = local_frame(direction)
        step = self.SLOPE_SPAN / self.body.radius
        return [(d, self.body) for d in (direction, normalize(add(direction, scale(east, step))),
                                         normalize(add(direction, scale(north, step))))]

    def slope(self):
        """The slope (degrees) at the spot from its slope_probes once their
        heights are back, else None."""
        asked = world.extra_probes
        if not asked or [probe for probe, _ in world.extra_heights] != asked:
            return None
        surface = (lambda h: max(h, 0.0)) if self.body.ocean else (lambda h: h)
        here, east, north = (surface(h) for _, h in world.extra_heights)
        rise = math.hypot(east - here, north - here) / self.SLOPE_SPAN
        return math.degrees(math.atan(rise))

    def at(self, direction, height):
        """The point `height` above the body's radius at `direction` from its centre, now."""
        return add(self.body.centre(), scale(direction, self.body.radius + height))

    def relative_speed(self, ship):
        return length(sub(ship.velocity, self.body.velocity()))

    def land(self):
        ship = state["ship"]
        now = world.time
        if self.touched is None:
            self.touchdown_speed = max(self.touchdown_speed, state["impact_speed"])
        if self.touched is None and (ship.on_ground or ship.crashed):
            self.touched = now
            state["controls"] = flight.Controls(assist=True)
        if self.touched is not None:
            q = ship.orientation
            world.extra_probes = [(normalize(self.body.relative(add(ship.position, flight.quat_rotate(q, arm)))),
                                      self.body) for arm in ship.params.gear]
        if self.touched is not None and now - self.touched >= self.REST_SECONDS[self.body.name] or now - self.start > self.TIMEOUT_SECONDS:
            self.report(ship)
        return True

    def report(self, ship):
        patch = world.patch[1] if world.patch is not None else None
        ok = patch is not None and not ship.crashed and ship.on_ground
        if patch is not None:
            centre = self.body.relative(ship.position)
            depth, normal = patch.query(centre)
            radial = normalize(centre)
            slope = math.degrees(math.acos(max(-1.0, min(1.0, dot(normal, radial)))))
            patch_height = length(centre) - self.body.radius + depth / dot(radial, normal)
            q = ship.orientation
            gear = [patch.query(add(centre, flight.quat_rotate(q, arm)))[0] for arm in ship.params.gear]
            print("[planet] touched down at %.1f m/s on a %.1f degree slope at %.0f m:"
                  % (self.touchdown_speed, slope, patch_height), flush=True)
            print("[planet]   gear points below the surface: %s m" % ", ".join("%+.3f" % d for d in gear),
                  flush=True)
            print("[planet]   patch height under the ship minus the probe's: %+.3f m"
                  % (patch_height - (world.ground or 0.0)), flush=True)
            # Each gear point's height above the terrain itself, measured at its direction.
            surface = (lambda h: max(h, 0.0)) if self.body.ocean else (lambda h: h)
            terrain = [length(add(centre, flight.quat_rotate(q, arm))) - self.body.radius - surface(h)
                       for arm, (_, h) in zip(ship.params.gear, world.extra_heights)]
            print("[planet]   gear points above the terrain at their directions: %s m"
                  % ", ".join("%+.3f" % d for d in terrain), flush=True)
            # The patch's surface against the terrain's at each gear point.
            agree = len(terrain) == len(gear) and max(abs(d + t) for d, t in zip(gear, terrain)) <= self.GEAR_TOLERANCE
            print("[planet]   gear points touching: %d of %d"
                  % (sum(1 for d in gear if d > -self.GEAR_TOLERANCE), len(gear)), flush=True)
            ok = ok and self.touchdown_speed < 0.5 * ship.params.gear_crash_speed and agree
            if self.body is planet:
                ok = (ok and max(abs(d) for d in gear) <= self.GEAR_TOLERANCE
                      and max(abs(d) for d in terrain) <= self.GEAR_TOLERANCE)
        speed = self.relative_speed(ship)
        print("[planet]   speed %.4f m/s, %s, touched %s s after the start"
              % (speed, contact_text(ship) or "in the air",
                 "never" if self.touched is None else "%.1f" % (self.touched - self.start)), flush=True)
        self.landed_ok = ok and speed < 0.05
        self.start_dive()

    def start_dive(self):
        """A ship 1.5 km above the spot and 1.5 km south of it, coasting
        straight at it at DIVE_SPEED, at the highest warp."""
        height, up = self.best
        east, north = local_frame(up)
        target = self.at(up, height)
        position = add(target, add(scale(up, 1_500.0), scale(north, -1_500.0)))
        state["ship"] = flight.Ship(position=position,
                                    velocity=add(self.body.velocity(),
                                                 scale(normalize(sub(target, position)), self.DIVE_SPEED)),
                                    orientation=flight.quat_from_basis(east, up, scale(north, -1.0)))
        state["controls"] = flight.Controls(assist=False)
        state["warp"] = len(WARPS) - 1
        world.patch = None
        world.extra_probes = []
        self.diving = world.time
        self.crash_frames = 0

    def dive(self):
        ship = state["ship"]
        if ship.crashed:
            self.crash_frames += 1
            state["warp"] = 0
        if self.crash_frames < self.FRAMES_PER_SPOT and world.time - self.diving < self.TIMEOUT_SECONDS:
            return True
        above = world.height_above_ground(viewpoint())
        print("[planet] a dive at %.0f m/s and x%d warp: %s, the centre %.2f m above the ground, impact %.0f m/s"
              % (self.DIVE_SPEED, WARPS[-1], "crashed" if ship.crashed else "not crashed", above,
                 ship.impact_speed), flush=True)
        ok = self.landed_ok and ship.crashed and self.DIVE_MIN_CENTRE_HEIGHT < above < SHIP_REACH + 2.0
        os._exit(0 if ok else 1)


class OrbitCheck:
    """--orbit-check: the ship in preset 9's circular orbit, coasting at 16x
    for one orbital period without the moon's pull (whose tide alone moves
    the orbit by about 100 m). Prints the periapsis and apoapsis at the
    start and the end and the range of heights flown, and exits with status
    0 when the heights stayed within 100 m of the orbit's, 1 otherwise."""

    TOLERANCE = 100.0

    def __init__(self):
        self.start = None
        self.low = math.inf
        self.high = -math.inf

    def __call__(self, dt):
        if planet.tree is None:
            return True
        if self.start is None:
            teleport(9)
            world.physics.moon_pulls = False
            state["warp"] = len(WARPS) - 1
            ship = state["ship"]
            self.start = world.time
            self.period = 2.0 * math.pi * math.sqrt((RADIUS + ORBIT_HEIGHT) ** 3 / world.physics.gm)
            self.orbit0 = flight.orbit(world.physics, ship.position, ship.velocity)
            return True
        ship = state["ship"]
        height = length(ship.position) - RADIUS
        self.low, self.high = min(self.low, height), max(self.high, height)
        if world.time - self.start >= self.period:
            orbit1 = flight.orbit(world.physics, ship.position, ship.velocity)
            print("[planet] one orbit (%.0f s of flight) at %d m:" % (self.period, ORBIT_HEIGHT), flush=True)
            print("[planet]   periapsis / apoapsis at the start %.1f / %.1f m, at the end %.1f / %.1f m"
                  % (self.orbit0 + orbit1), flush=True)
            print("[planet]   heights flown %.1f to %.1f m" % (self.low, self.high), flush=True)
            ok = max(abs(self.low - ORBIT_HEIGHT), abs(self.high - ORBIT_HEIGHT)) <= self.TOLERANCE
            os._exit(0 if ok else 1)
        return True


class PropRestCheck:
    """--prop-rest-check: on the planet 3 km north of the site, then on the
    moon at moon_spot, the free camera HEIGHT metres above the ground. Once
    the bubble's terrain mesh is under it, CRATES crates (CRATE_SIZE,
    CRATE_DENSITY) and SMALL boxes (SMALL_SIZE, SMALL_DENSITY) are dropped
    DROP_HEIGHT above the mesh at points of a grid around the camera where
    the mesh is no steeper than MAX_SLOPE and no fixed rock (rocks.py) is
    within ROCK_CLEAR, each square to the mesh's
    normal there and turned about it at random.
    After SETTLE_SECONDS every one must be asleep in the physics; over the
    next HOLD_SECONDS none may move more than TOLERANCE. Then the camera
    moves AWAY metres east and back until the bubble has rebased at least
    REBASES times, and back over the boxes, after RETURN_SECONDS, none may
    have moved more than TOLERANCE from where it rested. Positions are
    reckoned from the body's centre. Prints per body the slopes under the
    boxes, how many slept, the largest movements and the rebases, and exits
    with status 0 when both bodies passed, 1 otherwise."""

    HEIGHT = 6.0
    CRATES, SMALL = 20, 10
    SMALL_SIZE, SMALL_DENSITY = 0.3, 2_600.0
    DROP_HEIGHT = 0.2
    GRID_STEP = 2.0                     # m between candidate points
    GRID_REACH = 9.0                    # m from the camera candidate points reach
    MAX_SLOPE = 30.0                    # degrees
    ROCK_CLEAR = 1.0                    # m between a box and a fixed rock's sphere at least
    SETTLE_SECONDS = 10.0
    HOLD_SECONDS = 30.0
    TOLERANCE = 0.01                    # m
    AWAY = 600.0                        # m east the camera moves to make the bubble rebase
    AWAY_HEIGHT = 50.0                  # m above the ground at home while away
    MOVE_SECONDS = 1.5
    REBASES = 5
    RETURN_SECONDS = 2.0
    PLACE_FRAMES = 10                   # frames for the ground under a new place to come back

    def __init__(self):
        self.bodies = ["planet", "moon"]
        self.results = []
        self.random = random.Random(1)
        self.start_body()

    def start_body(self):
        self.stage = "place"
        self.frames = 0
        self.clock = 0.0
        self.rebases = 0
        self.moves = 0

    def __call__(self, dt):
        if planet.tree is None:
            return True
        self.frames += 1
        self.clock += dt
        getattr(self, self.stage)()
        return True

    def body(self):
        return planet if self.bodies[0] == "planet" else moon_body()

    def direction(self):
        if self.body() is planet:
            _, up, _, north = world.site_frame()
            return normalize(add(up, scale(north, 3_000.0 / RADIUS)))
        return moon_spot()

    def put_camera(self, direction, height):
        body = self.body()
        state["mode"] = "camera"
        state["position"] = add(body.centre(), scale(direction, body.radius + height))
        state["forward"] = local_frame(direction)[1]

    def next_stage(self, stage):
        self.stage, self.frames, self.clock = stage, 0, 0.0

    def place(self):
        """The camera 5 km above the spot until the ground there is known, then HEIGHT above it; the
        props of the body before gone."""
        drop_bubble()
        props.clear(None)
        self.home = self.direction()
        self.put_camera(self.home, 5_000.0)
        self.next_stage("lower")

    def lower(self):
        if self.frames < self.PLACE_FRAMES or world.ground is None:
            return
        self.ground = world.surface_height(state["position"])
        self.put_camera(self.home, self.ground + self.HEIGHT)
        self.next_stage("wait_for_mesh")

    def wait_for_mesh(self):
        if self.frames < self.PLACE_FRAMES:
            return
        bubble = ensure_bubble()
        terrain = state["terrain_collider"]
        if terrain.patch is None or not terrain.covers(bubble.to_bubble(state["position"]), self.GRID_REACH + 2.0):
            return
        self.drop(bubble, terrain.patch)
        self.next_stage("settle")

    def drop(self, bubble, patch):
        """The boxes at grid points no steeper than MAX_SLOPE, nearest the camera first."""
        here = bubble.body.relative(state["position"])
        east, north = local_frame(normalize(here))
        steps = int(self.GRID_REACH / self.GRID_STEP)
        candidates = sorted(((i * i + j * j, i, j) for i in range(-steps, steps + 1)
                             for j in range(-steps, steps + 1)))
        sizes = [(CRATE_SIZE, CRATE_DENSITY)] * self.CRATES + [(self.SMALL_SIZE, self.SMALL_DENSITY)] * self.SMALL
        self.slopes = []
        for _, i, j in candidates:
            if len(self.slopes) == len(sizes):
                break
            spot = add(here, add(scale(east, i * self.GRID_STEP), scale(north, j * self.GRID_STEP)))
            normal = patch.normal(spot)
            if normal is None:
                continue
            up = normalize(spot)
            slope = math.degrees(math.acos(max(-1.0, min(1.0, dot(normal, up)))))
            if slope > self.MAX_SLOPE:
                continue
            size, density = sizes[len(self.slopes)]
            spot_bubble = sub(spot, bubble.origin)
            if any(length(sub(physics.get_transform(r.body)[0], spot_bubble)) < r.radius + size + self.ROCK_CLEAR
                   for r in scatter_rocks.rocks.values()):
                continue
            xy = patch.offset(spot)
            mid = (patch.columns - 1) // 2
            ci = min(max(int(round(xy[0] / patch.spacing)) + mid, 0), patch.columns - 1)
            cj = min(max(int(round(xy[1] / patch.spacing)) + mid, 0), patch.columns - 1)
            ground = scale(up, bubble.body.radius + float(patch.heights[cj, ci]))
            centre = add(ground, scale(normal, self.DROP_HEIGHT + 0.5 * size))
            east_n = normalize(sub(east, scale(normal, dot(east, normal))))
            rotation = yaw_rotation(east_n, normal, cross(east_n, normal), self.random.uniform(0.0, 2.0 * math.pi))
            props.spawn_crate(bubble, sub(centre, bubble.origin), rotation, size, density)
            self.slopes.append(slope)

    def positions(self):
        return [prop_state(c)[1] for c in props.crates]

    def settle(self):
        if self.clock < self.SETTLE_SECONDS:
            return
        self.asleep = sum(1 for c in props.crates if c.body is not None and not physics.is_active(c.body))
        self.frozen = sum(1 for c in props.crates if c.body is None)
        self.rested = self.positions()
        self.next_stage("hold")

    def largest_move(self):
        return max(length(sub(a, b)) for a, b in zip(self.positions(), self.rested))

    def hold(self):
        if self.clock < self.HOLD_SECONDS:
            return
        self.held_move = self.largest_move()
        bubble = state["bubble"]
        bubble.listeners.append(self.count_rebase)
        self.next_stage("move")

    def count_rebase(self, offset):
        self.rebases += 1

    def move(self):
        if self.frames == 1:
            away = self.moves % 2 == 0
            east, _ = local_frame(self.home)
            direction = normalize(add(self.home, scale(east, self.AWAY / self.body().radius))) if away else self.home
            self.put_camera(direction, self.ground + (self.AWAY_HEIGHT if away else self.HEIGHT))
            self.moves += 1
        if self.clock < self.MOVE_SECONDS:
            return
        if self.rebases >= self.REBASES and self.moves % 2 == 0:
            self.next_stage("back")
        else:
            self.next_stage("move")

    def back(self):
        if self.clock < self.RETURN_SECONDS:
            return
        moved = self.largest_move()
        count = len(props.crates)
        ok = (count == self.CRATES + self.SMALL and self.asleep == count and self.frozen == 0
              and self.held_move <= self.TOLERANCE and moved <= self.TOLERANCE and self.rebases >= self.REBASES)
        name = self.bodies[0]
        print("[planet] prop rest on the %s: %d boxes on slopes of %.1f to %.1f degrees"
              % (name, count, min(self.slopes), max(self.slopes)), flush=True)
        print("[planet]   after %.0f s: %d asleep, %d out of the physics"
              % (self.SETTLE_SECONDS, self.asleep, self.frozen), flush=True)
        print("[planet]   largest move over the next %.0f s: %.4f m" % (self.HOLD_SECONDS, self.held_move), flush=True)
        print("[planet]   after %d rebases (%d terrain meshes built): largest move %.4f m"
              % (self.rebases, state["terrain_collider"].builds, moved), flush=True)
        if not ok:
            patch = state["terrain_collider"].patch
            for k, (crate, now, rested) in enumerate(zip(props.crates, self.positions(), self.rested)):
                active = crate.body is not None and physics.is_active(crate.body)
                if length(sub(now, rested)) > self.TOLERANCE or active:
                    normal = patch.normal(now) if patch is not None else None
                    under = (math.degrees(math.acos(max(-1.0, min(1.0, dot(normal, normalize(now))))))
                             if normal is not None else math.nan)
                    where = ", awake" if active else ""
                    where += ", on the coarse mesh" if crate.body is not None and crate.far else ""
                    where += ", " + crate.state if crate.body is None else ""
                    rocks_near = [length(sub(add(state["bubble"].origin, physics.get_transform(r.body)[0]), rested))
                                  - r.radius for r in scatter_rocks.rocks.values()]
                    if rocks_near:
                        where += ", %.2f m from a fixed rock where it rested" % max(min(rocks_near), 0.0)
                    print("[planet]   box %d (%.1f m): dropped on %.1f degrees, moved %.3f m, now on %.1f degrees%s"
                          % (k, crate.size, self.slopes[k], length(sub(now, rested)), under, where), flush=True)
        print("[planet]   %s" % ("passed" if ok else "FAILED"), flush=True)
        self.results.append(ok)
        self.bodies.pop(0)
        drop_bubble()
        if not self.bodies:
            os._exit(0 if all(self.results) else 1)
        self.start_body()


def point_triangle_distance(p, a, b, c):
    """The distance from point `p` to the triangle a, b, c (numpy arrays):
    to the nearest point of its plane inside it, else of its edges."""
    n = np.cross(b - a, c - a)
    n /= np.linalg.norm(n)
    q = p - np.dot(p - a, n) * n
    if all(np.dot(np.cross(v1 - v0, q - v0), n) >= 0.0 for v0, v1 in ((a, b), (b, c), (c, a))):
        return float(np.linalg.norm(p - q))
    best = math.inf
    for v0, v1 in ((a, b), (b, c), (c, a)):
        e = v1 - v0
        t = min(1.0, max(0.0, float(np.dot(p - v0, e) / np.dot(e, e))))
        best = min(best, float(np.linalg.norm(p - (v0 + t * e))))
    return best


class WalkCheck:
    """--walk-check: on the planet 3 km north of the site, on the planet in
    the mountains 600 m south-west of near_mountain's peak, then on the moon
    at moon_spot, a player put down from the free camera walks a circle of
    LAP metres at WALK speed, then at RUN, its heading turned each frame by
    its distance along the ground over the circle's radius, and by
    STUCK_TURN more when it covered less than STUCK_DISTANCE in the last
    second (stopped by ground too steep to climb). While it stands on
    the terrain mesh (not on a rock), the gap between its capsule's lower sphere and the nearest
    triangle of the terrain mesh around its feet is measured. The feet must stay
    within TOLERANCE of the ground and never more than BELOW under it, and
    each lap must cover at least MIN_LAP_SHARE of its length. Every
    SAMPLE_EVERY metres the player stops for SAMPLE_WAIT, the level of
    detail is frozen and ChunkPool read back, and the terrain mesh's height is compared with the
    drawn surface's (drawn_height) under the feet and at SAMPLE_POINTS
    points SAMPLE_RING metres around them, which must stay within
    MATCH_TOLERANCE of each other. Prints per body and speed the
    distance, the share of the time on the ground, the gaps' range and the
    largest differences between the mesh and the drawn surface, and exits
    with status 0 when every lap passed, 1 otherwise."""

    LAP = 200.0
    TOLERANCE = 0.05
    BELOW = 0.01
    MIN_LAP_SHARE = 0.9
    START_HEIGHT = 2.0
    SETTLE_SECONDS = 2.0
    PLACE_FRAMES = 10
    SAMPLE_EVERY = 20.0
    SAMPLE_RING = 1.0
    SAMPLE_POINTS = 8
    MATCH_TOLERANCE = 0.1
    SAMPLE_WAIT = 1.0                   # s standing before the level of detail is frozen
    STUCK_DISTANCE = 1.0
    STUCK_TURN = math.radians(45.0)

    def __init__(self):
        self.bodies = ["planet", "mountains", "moon"]
        self.results = []
        self.start_body()

    def start_body(self):
        self.stage, self.frames, self.clock = "place", 0, 0.0
        self.speeds = ["walk", "run"]

    def __call__(self, dt):
        if planet.tree is None:
            return True
        self.frames += 1
        self.clock += dt
        getattr(self, self.stage)(dt)
        return True

    def body(self):
        return moon_body() if self.bodies[0] == "moon" else planet

    def next_stage(self, stage):
        self.stage, self.frames, self.clock = stage, 0, 0.0

    def place(self, dt):
        drop_bubble()
        state["scripted_controls"] = True
        body = self.body()
        _, up, east, north = world.site_frame()
        if self.bodies[0] == "planet":
            self.home = normalize(add(up, scale(north, 3_000.0 / RADIUS)))
        elif self.bodies[0] == "mountains":
            azimuth = math.radians(67.5)
            level = add(scale(east, math.cos(azimuth)), scale(north, math.sin(azimuth)))
            peak = normalize(add(up, scale(level, 60_000.0 / RADIUS)))
            peak_east, peak_north = local_frame(peak)
            self.home = normalize(add(peak, scale(normalize(add(peak_east, peak_north)), -600.0 / RADIUS)))
        else:
            self.home = moon_spot()
        state["mode"] = "camera"
        state["position"] = add(body.centre(), scale(self.home, body.radius + 5_000.0))
        state["forward"] = local_frame(self.home)[1]
        self.next_stage("lower")

    def lower(self, dt):
        if self.frames < self.PLACE_FRAMES or world.ground is None:
            return
        body = self.body()
        ground = world.surface_height(state["position"])
        state["position"] = add(body.centre(), scale(self.home, body.radius + ground + self.START_HEIGHT))
        self.next_stage("spawn")

    def spawn(self, dt):
        if self.frames < self.PLACE_FRAMES or world.ground is None:
            return
        go_on_foot()
        if state["player"] is not None:
            self.next_stage("settle")

    def settle(self, dt):
        player = state["player"]
        if player.held or not player.on_ground:
            self.clock = 0.0
        if self.clock >= self.SETTLE_SECONDS:
            self.start_lap()

    def start_lap(self):
        self.next_stage("lap")
        self.distance = 0.0
        self.ground_time = self.time = 0.0
        self.low, self.high = math.inf, -math.inf
        self.last = state["bubble"].body_relative(state["player"].feet())
        self.next_sample = self.SAMPLE_EVERY
        self.mesh_above = self.mesh_below = 0.0     # m the mesh was above / below the drawn surface at most
        self.turns = 0
        self.levels = set()                         # levels of the chunks the drawn surface was measured on
        self.second = (0.0, 0.0)                    # (lap time, distance) a second ago

    def gap(self):
        """The gap between the capsule's lower sphere and the nearest of
        the mesh's triangles within two grid cells of the feet (negative
        where it sinks in), or None off the mesh."""
        player, bubble = state["player"], state["bubble"]
        patch = state["terrain_collider"].patch
        feet = bubble.body_relative(player.feet())
        xy = patch.offset(feet) if patch is not None else None
        if xy is None:
            return None
        mid, last = (patch.columns - 1) // 2, patch.columns - 1
        i, j = int(math.floor(xy[0] / patch.spacing)) + mid, int(math.floor(xy[1] / patch.spacing)) + mid
        if not (2 <= i <= last - 3 and 2 <= j <= last - 3):
            return None
        centre = np.asarray(add(feet, scale(normalize(feet), PLAYER_RADIUS)), dtype=np.float64)
        p = patch.points()
        nearest = math.inf
        for jj in range(j - 2, j + 3):
            for ii in range(i - 2, i + 3):
                a, b, c, d = p[jj, ii], p[jj, ii + 1], p[jj + 1, ii], p[jj + 1, ii + 1]
                nearest = min(nearest, point_triangle_distance(centre, a, b, c),
                              point_triangle_distance(centre, b, d, c))
        return nearest - PLAYER_RADIUS

    def lap(self, dt):
        player, bubble = state["player"], state["bubble"]
        controls = state["foot_controls"]
        controls.forward, controls.strafe = 1.0, 0.0
        controls.run = self.speeds[0] == "run"
        controls.crouch = controls.jet = controls.jump = False
        feet = bubble.body_relative(player.feet())
        up = normalize(feet)
        moved = sub(feet, self.last)
        along = length(sub(moved, scale(up, dot(moved, up))))
        self.last = feet
        self.distance += along
        self.time += dt
        player.heading = rotate(player.heading, up, along / (self.LAP / (2.0 * math.pi)))
        if self.time - self.second[0] >= 1.0:
            if self.distance - self.second[1] < self.STUCK_DISTANCE:
                player.heading = rotate(player.heading, up, self.STUCK_TURN)
                self.turns += 1
            self.second = (self.time, self.distance)
        if player.on_ground and not player.held:
            self.ground_time += dt
            on_terrain = physics.character_ground(player.character)[3] == state["terrain_collider"].mesh
            gap = self.gap() if on_terrain else None
            if gap is not None:
                self.low, self.high = min(self.low, gap), max(self.high, gap)
        if self.distance >= self.LAP or self.time > 4.0 * self.LAP / (RUN_SPEED if controls.run else WALK_SPEED):
            self.finish_lap()
        elif self.distance >= self.next_sample:
            self.next_sample += self.SAMPLE_EVERY
            self.stage, self.frames = "sample", 0

    def sample(self, dt):
        """Standing still with the level of detail frozen: the mesh against
        the drawn surface around the feet, once ChunkPool comes back."""
        controls = state["foot_controls"]
        controls.forward = 0.0
        if self.frames == 1:
            self.waited = 0.0
        if self.waited < self.SAMPLE_WAIT:
            self.waited += dt
            return
        if not world.frozen:
            world.frozen = True
            previous = engine.get_readback("ChunkPool")
            self.after = previous["frame"] if previous is not None else -1
            engine.request_readback("ChunkPool")
            return
        readback = engine.get_readback("ChunkPool")
        if readback is None or readback["frame"] <= self.after:
            return
        gpu = np.frombuffer(readback["data"], dtype=np.float32).reshape(SLOTS, VERTICES_PER_CHUNK,
                                                                        FLOATS_PER_VERTEX)
        bubble, patch = state["bubble"], state["terrain_collider"].patch
        feet = bubble.body_relative(state["player"].feet())
        east, north = local_frame(normalize(feet))
        points = [feet] + [add(feet, add(scale(east, self.SAMPLE_RING * math.cos(a)),
                                         scale(north, self.SAMPLE_RING * math.sin(a))))
                           for a in (2.0 * math.pi * k / self.SAMPLE_POINTS for k in range(self.SAMPLE_POINTS))]
        for p in points:
            mesh, drawn = patch.height(p), drawn_height(bubble.body, gpu, p)
            if mesh is not None and drawn is not None:
                self.mesh_above = max(self.mesh_above, mesh - drawn[0])
                self.mesh_below = max(self.mesh_below, drawn[0] - mesh)
                self.levels.add(drawn[1])
        world.frozen = False
        self.last = feet
        self.stage, self.frames = "lap", 0

    def finish_lap(self):
        name, speed = self.bodies[0], self.speeds.pop(0)
        ok = (self.distance >= self.MIN_LAP_SHARE * self.LAP and self.low >= -self.BELOW
              and self.high <= self.TOLERANCE and max(self.mesh_above, self.mesh_below) <= self.MATCH_TOLERANCE)
        print("[planet] walk on the %s, %s: %.1f m in %.1f s, %.0f %% on the ground, feet %+.3f to %+.3f m from it, "
              "the mesh up to %.3f m above and %.3f m below the drawn surface (chunks of level %s), "
              "%d turns away from steep ground: %s"
              % (name, speed, self.distance, self.time, 100.0 * self.ground_time / max(self.time, 1e-9),
                 self.low, self.high, self.mesh_above, self.mesh_below,
                 ", ".join(str(v) for v in sorted(self.levels)), self.turns, "passed" if ok else "FAILED"),
              flush=True)
        self.results.append(ok)
        if self.speeds:
            self.start_lap()
            return
        self.bodies.pop(0)
        drop_bubble()
        if not self.bodies:
            os._exit(0 if all(self.results) else 1)
        self.start_body()


class GunCheck(WalkCheck):
    """The gravity gun's checks: WalkCheck's player put down on the planet
    3 km north of the site, then on the moon, standing still, then the
    stages from `begin` on each body. Exits with status 0 when every body
    passed, 1 otherwise."""

    GRAB_TIME = 0.1                     # s the grab control is held, as a click
    GRAB_WAIT = 0.1                     # s after it a crate must be held by

    def __init__(self):
        self.bodies = ["planet", "moon"]
        self.results = []
        self.start_body()

    def start_lap(self):
        state["foot_controls"] = FootControls()
        self.begin()

    def crate_ahead(self, mass, distance=None):
        """A crate of `mass` kg on the view's line, its centre `distance`
        m from the eye (where the gun holds it when None), turned with
        the view."""
        player, gun = state["player"], state["gun"]
        right, up, forward = player.basis()
        if distance is None:
            distance = gun.hold_distance + 0.5 * math.sqrt(3.0) * CRATE_SIZE
        return props.spawn_crate(state["bubble"], add(player.eye(), scale(forward, distance)),
                                 flight.quat_from_basis(right, up, scale(forward, -1.0)), CRATE_SIZE, mass=mass)

    def grab(self):
        """The grab control held for GRAB_TIME from the stage's 3rd frame;
        True once GRAB_WAIT more has passed."""
        controls = state["gun_controls"]
        if self.frames == 3:
            self.grab_clock = self.clock
        pressed = self.clock - self.grab_clock if self.frames >= 3 else -1.0
        controls.grab = 0.0 <= pressed < self.GRAB_TIME
        return pressed >= self.GRAB_TIME + self.GRAB_WAIT

    def off_hold_point(self):
        player, gun = state["player"], state["gun"]
        p = physics.get_transform(gun.held)[0]
        return length(sub(p, gun.hold_point(player.eye(), player.basis()[2])))

    def clear(self):
        state["gun"].drop()
        props.clear(state["bubble"])

    def next_body(self, ok):
        self.results.append(ok)
        self.bodies.pop(0)
        drop_bubble()
        if not self.bodies:
            os._exit(0 if all(self.results) else 1)
        self.start_body()


class HoldCheck(GunCheck):
    """--hold-check: on each body the gun grabs a HOLD_MASS kg crate, holds
    it for STEADY seconds, then the player turns a full circle in
    TURN_TIME, walks WALK metres, jumps and lands, and the bubble is
    rebased under it. The crate must stay held and within
    GunParams.drop_distance of the hold point throughout, and move by less
    than REBASE_JUMP relative to the eye across the rebase. Then a
    HEAVY_MASS kg crate in front must be too heavy on the planet
    (HEAVY_MASS x 4.905 m/s^2 is more than 0.9 hold_force) and be grabbed
    on the moon. Prints per body how far off the hold point the crate got
    in each stage."""

    HOLD_MASS = 50.0
    HEAVY_MASS = 2_000.0
    STEADY = 1.0
    TURN_TIME = 2.0
    WALK = 30.0
    LAND_TIME = 0.5                     # s on the ground after the jump
    JUMP_TIMEOUT = 15.0
    REBASE_FRAMES = 30
    REBASE_JUMP = 0.01

    def begin(self):
        self.next_stage("pick")
        self.off = {}                   # stage: m off the hold point at most
        self.lost = None                # the stage the crate was dropped in
        self.rebase_jump = None
        self.heavy_status = None        # the gun's status aimed at the heavy crate
        self.heavy_held = False

    def pick(self, dt):
        if self.frames == 1:
            self.crate = self.crate_ahead(self.HOLD_MASS)
        if not self.grab():
            return
        if state["gun"].held != self.crate.body:
            self.lost = "pick"
            self.finish()
            return
        self.next_stage("steady")

    def measure(self):
        """The crate's distance from the hold point in this stage, or the
        stage noted when it was dropped; False once dropped."""
        if state["gun"].held != self.crate.body:
            self.lost = self.lost or self.stage
            return False
        self.off[self.stage] = max(self.off.get(self.stage, 0.0), self.off_hold_point())
        return True

    def steady(self, dt):
        if not self.measure():
            self.finish()
        elif self.clock >= self.STEADY:
            self.turned = 0.0
            self.next_stage("turn")

    def turn(self, dt):
        if not self.measure():
            self.finish()
            return
        player = state["player"]
        angle = min(2.0 * math.pi - self.turned, 2.0 * math.pi * dt / self.TURN_TIME)
        player.heading = rotate(player.heading, player.up(), angle)
        self.turned += angle
        if self.turned >= 2.0 * math.pi - 1e-9:
            self.last = state["bubble"].body_relative(player.feet())
            self.walked = 0.0
            self.next_stage("walk")

    def walk(self, dt):
        if not self.measure():
            self.finish()
            return
        controls = state["foot_controls"]
        controls.forward = 1.0
        feet = state["bubble"].body_relative(state["player"].feet())
        moved = sub(feet, self.last)
        up = normalize(feet)
        self.walked += length(sub(moved, scale(up, dot(moved, up))))
        self.last = feet
        if self.walked >= self.WALK or self.clock > 4.0 * self.WALK / WALK_SPEED:
            controls.forward = 0.0
            self.left_ground = False
            self.landed = 0.0
            self.next_stage("jump")

    def jump(self, dt):
        if not self.measure():
            self.finish()
            return
        player = state["player"]
        if self.frames == 1:
            state["foot_controls"].jump = True
        if not player.on_ground:
            self.left_ground = True
            self.landed = 0.0
        elif self.left_ground:
            self.landed += dt
        if self.landed >= self.LAND_TIME or self.clock > self.JUMP_TIMEOUT:
            self.next_stage("rebase")

    def rebase(self, dt):
        if not self.measure():
            self.finish()
            return
        player, bubble = state["player"], state["bubble"]
        offset = sub(physics.get_transform(self.crate.body)[0], player.eye())
        if self.frames == 1:
            self.before = offset
            bubble.rebase(player.feet())
        elif self.frames == 2:
            self.rebase_jump = length(sub(offset, self.before))
        elif self.frames >= self.REBASE_FRAMES:
            self.clear()
            self.next_stage("heavy")

    def heavy(self, dt):
        gun = state["gun"]
        if self.frames == 1:
            self.heavy_crate = self.crate_ahead(self.HEAVY_MASS, distance=3.0)
        if self.frames == 3:
            self.heavy_status = gun.status
        if not self.grab():
            return
        self.heavy_held = gun.held == self.heavy_crate.body
        self.finish()

    def finish(self):
        name = self.bodies[0]
        limit = state["gun"].p.drop_distance
        heavy = self.heavy_status
        heavy_ok = (heavy == "too_heavy" and not self.heavy_held) if name == "planet" else \
            (heavy == "can_grab" and self.heavy_held)
        ok = (self.lost is None and all(v <= limit for v in self.off.values())
              and self.rebase_jump is not None and self.rebase_jump < self.REBASE_JUMP and heavy_ok)
        stages = ", ".join("%s %.3f" % (stage, self.off.get(stage, math.nan))
                           for stage in ("steady", "turn", "walk", "jump", "rebase"))
        print("[planet] hold on the %s: a %.0f kg crate %s, at most m off the hold point: %s; it moved %s "
              "relative to the eye across the rebase; a %.0f kg crate %s and %s: %s"
              % (name, self.HOLD_MASS, "held throughout" if self.lost is None else "DROPPED in " + self.lost,
                 stages, "%.4f m" % self.rebase_jump if self.rebase_jump is not None else "-",
                 self.HEAVY_MASS, heavy, "grabbed" if self.heavy_held else "not grabbed",
                 "passed" if ok else "FAILED"), flush=True)
        self.clear()
        self.next_body(ok)


class PuntCheck(GunCheck):
    """--punt-check: on each body the gun grabs crates of MASSES kg in turn,
    holds each for STEADY seconds and punts it (Gun.punt) with the view
    level. Right after the punt the crate's velocity along the view must
    have grown by min(punt_speed, punt_impulse / mass) and the player's
    velocity against it by min(MAX_RECOIL, recoil_factor x the impulse /
    the player's mass), each within TOLERANCE. Then the player looks
    straight down and punts the ground with the punt control: it must rise
    within JUMP_TOLERANCE of MAX_RECOIL^2 / 2g above where it stood.
    Prints per body the speeds, the recoils and the height."""

    MASSES = (5.0, 50.0, 500.0)
    STEADY = 1.0
    TOLERANCE = 0.02
    JUMP_TOLERANCE = 0.1
    JUMP_TIMEOUT = 30.0

    def begin(self):
        self.masses = list(self.MASSES)
        self.punts = []                 # (mass, speed, wanted speed, recoil, wanted recoil)
        self.next_stage("pick")

    def pick(self, dt):
        if self.frames == 1:
            self.crate = self.crate_ahead(self.masses[0])
        if not self.grab():
            return
        if state["gun"].held != self.crate.body:
            self.punts.append((self.masses[0], math.nan, 0.0, math.nan, 0.0))
            self.after_punt()
            return
        self.next_stage("steady")

    def steady(self, dt):
        if self.clock < self.STEADY:
            return
        player, gun = state["player"], state["gun"]
        forward = player.basis()[2]
        body, character = self.crate.body, player.character
        v0, p0 = physics.get_velocity(body)[0], physics.character_velocity(character)
        gun.punt(body, forward)
        v1, p1 = physics.get_velocity(body)[0], physics.character_velocity(character)
        m = self.masses[0]
        speed = min(gun.p.punt_speed, gun.p.punt_impulse / m)
        recoil = min(gun_module.MAX_RECOIL, gun.p.recoil_factor * m * speed / PLAYER_MASS)
        self.punts.append((m, dot(sub(v1, v0), forward), speed, -dot(sub(p1, p0), forward), recoil))
        self.after_punt()

    def after_punt(self):
        self.clear()
        self.masses.pop(0)
        if self.masses:
            self.next_stage("pick")
        else:
            state["player"].pitch = -math.radians(89.0)
            self.next_stage("ground")

    def ground(self, dt):
        player, bubble = state["player"], state["bubble"]
        height = length(bubble.body_relative(player.feet()))
        if self.frames == 10:
            self.start_height = height
            self.top = height
            self.left_ground = False
            state["gun_controls"].punt = True
        elif self.frames > 10:
            self.top = max(self.top, height)
            if not player.on_ground:
                self.left_ground = True
            elif self.left_ground or self.clock > self.JUMP_TIMEOUT:
                self.finish()

    def finish(self):
        name, gun = self.bodies[0], state["gun"]
        g = length(state["bubble"].gravity(state["player"].feet()))
        wanted_height = gun_module.MAX_RECOIL ** 2 / (2.0 * g)
        rise = self.top - self.start_height
        ok = abs(rise - wanted_height) <= self.JUMP_TOLERANCE * wanted_height
        rows = []
        for m, speed, wanted, recoil, wanted_recoil in self.punts:
            ok = ok and abs(speed - wanted) <= self.TOLERANCE * wanted
            ok = ok and abs(recoil - wanted_recoil) <= self.TOLERANCE * wanted_recoil
            rows.append("%.0f kg at %.2f m/s (%.2f), recoil %.2f m/s (%.2f)" % (m, speed, wanted, recoil, wanted_recoil))
        print("[planet] punt on the %s: %s; punting the ground lifted the player %.2f m (%.2f): %s"
              % (name, "; ".join(rows), rise, wanted_height, "passed" if ok else "FAILED"), flush=True)
        self.clear()
        self.next_body(ok)


class PromotionCheck(GunCheck):
    """--promotion-check: in the mountains (WalkCheck's place) and on the
    moon, once the player has stood still with rocks around it, the gun
    grabs each of the rocks within REACH of the feet in turn (up to
    TARGETS; those too heavy are skipped): the view is turned to the rock
    and held still for STILL_FRAMES, the grab control pressed until the
    rock comes loose (Rocks.promote), the view held still STILL_FRAMES
    more, and the rock dropped. Then a STRIKE_MASS kg crate is thrown down
    at STRIKE_SPEED onto the nearest fixed rock left within REACH, which
    must come loose within STRIKE_TIMEOUT. Each frame, by the stamps of the nearby
    lists (nearby_trees: the scatter lists every rock it draws near the
    camera), the cells the scatter listed and the cells of the loose
    rocks' entities are kept: for WINDOW frames before a promotion's frame
    its rock must be listed and have no entity, and from that frame on for
    WINDOW frames have an entity and not be listed, so it is drawn exactly
    once. Prints per place the rocks promoted, and exits with status 0 when
    every place promoted at least one rock and every promotion passed, 1
    otherwise."""

    REACH = 20.0
    TARGETS = 12
    STILL_FRAMES = 12
    WINDOW = 3
    GRAB_TIMEOUT = 4.0

    def __init__(self):
        self.bodies = ["mountains", "moon"]
        self.results = []
        self.start_body()
        promote = scatter_rocks.promote

        def recorded(bubble, body):
            key = scatter_rocks.bodies.get(body)
            done = promote(bubble, body)
            if done:
                self.promotions.append((nearby_trees.stamp + 1, key))
            return done
        scatter_rocks.promote = recorded

    def begin(self):
        self.listed, self.entities, self.promotions = {}, {}, []
        self.skipped = 0
        self.struck = False
        self.next_stage("find")

    def count(self):
        """This tick's cells: the loose rocks' after the last frame (its
        nearby stamp), and the scatter's of whichever list came back."""
        self.entities[nearby_trees.stamp] = {p.key for p in props.rocks}
        readback = engine.get_readback("NearbyTrees")
        if readback is not None:
            stamp, keys = listed_keys(readback["data"])
            self.listed[stamp] = keys

    def __call__(self, dt):
        if planet.tree is not None and self.stage in ("find", "aim", "grab", "after", "strike", "strike_after"):
            self.count()
        return super().__call__(dt)

    def find(self, dt):
        if not scatter_rocks.rocks or self.clock < self.STEADY:
            if self.clock > 30.0:
                self.finish()
            return
        player, bubble = state["player"], state["bubble"]
        feet = player.feet()
        near = sorted((length(sub(physics.get_transform(r.body)[0], feet)), r.body)
                      for r in scatter_rocks.rocks.values())
        self.targets = [body for d, body in near if d <= self.REACH][:self.TARGETS]
        self.next_target()

    STEADY = 1.0
    STRIKE_MASS = 50.0
    STRIKE_SPEED = 8.0
    STRIKE_TIMEOUT = 2.0

    def strike(self, dt):
        """A crate of STRIKE_MASS thrown down at STRIKE_SPEED onto the
        nearest fixed rock within REACH, in view: it must come loose."""
        player = state["player"]
        if self.frames == 1:
            feet = player.feet()
            near = sorted((length(sub(physics.get_transform(r.body)[0], feet)), r.body)
                          for r in scatter_rocks.rocks.values())
            near = [body for d, body in near if 2.0 < d <= self.REACH]
            self.rock = near[0] if near else None
            if self.rock is None:
                self.finish()
                return
            self.aim_at()
        if self.frames == self.STILL_FRAMES:
            bubble = state["bubble"]
            p = physics.get_transform(self.rock)[0]
            up = bubble.up(p)
            right, _, forward = player.basis()
            self.strike_crate = props.spawn_crate(bubble, add(p, scale(up, 2.0)), flight.quat_from_basis(
                right, up, cross(right, up)), 0.3, mass=self.STRIKE_MASS)
            physics.set_velocity(self.strike_crate.body, scale(up, -self.STRIKE_SPEED))
            self.strike_promotions = len(self.promotions)
        if self.frames > self.STILL_FRAMES:
            self.struck = len(self.promotions) > self.strike_promotions
            if self.struck or self.clock > self.STRIKE_TIMEOUT + 0.5:
                self.next_stage("strike_after")

    def strike_after(self, dt):
        if self.frames >= self.STILL_FRAMES:
            self.finish()

    def next_target(self):
        if not self.targets:
            self.next_stage("strike")
            return
        self.rock = self.targets.pop(0)
        if not scatter_rocks.is_static_rock(self.rock):
            self.next_target()
            return
        self.next_stage("aim")

    def aim_at(self):
        player = state["player"]
        to = normalize(sub(physics.get_transform(self.rock)[0], player.eye()))
        up = player.up()
        player.heading = player.level(to, up)
        player.pitch = math.asin(max(-1.0, min(1.0, dot(to, up))))

    def aim(self, dt):
        if self.frames == 1:
            self.aim_at()
        if self.frames < self.STILL_FRAMES:
            return
        gun = state["gun"]
        if gun.status == "too_heavy" or gun.target is None or gun.target.body != self.rock:
            self.skipped += 1
            self.next_target()
            return
        self.grab_clock = 0.0
        self.next_stage("grab")

    def grab(self, dt):
        controls = state["gun_controls"]
        controls.grab = self.clock < 0.1 or (state["gun"].held is None and self.clock < self.GRAB_TIMEOUT)
        if scatter_rocks.is_static_rock(self.rock) and self.clock < self.GRAB_TIMEOUT:
            return
        controls.grab = False
        self.next_stage("after")

    def after(self, dt):
        if self.frames < self.STILL_FRAMES:
            return
        controls = state["gun_controls"]
        controls.grab = self.frames < self.STILL_FRAMES + 6
        if controls.grab:
            return
        state["gun"].drop()
        self.next_target()

    def finish(self):
        state["gun_controls"].grab = False
        name = self.bodies[0]
        rows, ok = [], bool(self.promotions)
        for p, key in self.promotions:
            stamps = range(p - self.WINDOW, p + self.WINDOW)
            if not all(s in self.listed and s in self.entities for s in stamps):
                rows.append("frame %d: lists missing" % p)
                ok = False
                continue
            # Per frame: (drawn by the scatter, drawn as an entity).
            seen = [(key in self.listed[s], key in self.entities[s]) for s in stamps]
            good = all(drawn == ((True, False) if s < p else (False, True)) for s, drawn in zip(stamps, seen))
            ok = ok and good
            rows.append("ok" if good else "frame %d: %s" % (p, seen))
        ok = ok and self.struck
        print("[planet] promotion in the %s: %d rocks promoted, %d too heavy; the thrown crate %s its rock; "
              "drawn once around each: %s: %s"
              % (name, len(self.promotions), self.skipped, "knocked loose" if self.struck else "did NOT loosen",
                 ", ".join(rows) or "-", "passed" if ok else "FAILED"), flush=True)
        self.clear()
        self.next_body(ok)


class OrbitCrateCheck(GunCheck):
    """--orbit-crate-check: on the moon (WalkCheck's place), a MASS kg crate
    is put HEIGHT above the player's feet (at least CLEAR above the moon's
    radius) moving level along the view at the circular speed there, and
    tracked, with the time warp at WARP. It must leave the physics bubble
    and fly; one period of its circular orbit after it was put there, its
    position (taken back to that moment along its velocity) must be within
    TOLERANCE of where it started; and coming round it must come back into
    the physics within TIMEOUT more seconds of the world's clock. Prints
    the period, how far the bubble's origin it got, the error after one
    period and its state at the end, and exits with status 0 when all of
    these held, 1 otherwise."""

    MASS = 10.0
    SIZE = 0.4
    HEIGHT = 1_800.0
    CLEAR = 1_700.0
    WARP = 4                            # index into WARPS: 16x
    TOLERANCE = 50.0
    TIMEOUT = 120.0

    def __init__(self):
        self.bodies = ["moon"]
        self.results = []
        self.start_body()

    def begin(self):
        player, bubble = state["player"], state["bubble"]
        feet = bubble.body_relative(player.feet())
        up = normalize(feet)
        r = max(length(feet) + self.HEIGHT, bubble.body.radius + self.CLEAR)
        self.start = scale(up, r)
        gm = world.physics.moon.gm
        speed = math.sqrt(gm / r)
        heading = player.level(player.basis()[2], up)
        self.period = 2.0 * math.pi * math.sqrt(r ** 3 / gm)
        right = normalize(cross(heading, up))
        crate = props.spawn_crate(bubble, sub(self.start, bubble.origin),
                                  flight.quat_from_basis(right, up, scale(heading, -1.0)), self.SIZE, mass=self.MASS)
        physics.set_velocity(crate.body, scale(heading, speed))
        self.crate = crate
        state["tracked"] = crate
        state["warp"] = self.WARP
        self.t0 = world.time
        self.farthest = 0.0
        self.flew = False
        self.back_in = False
        self.error = None
        self.last = None
        self.next_stage("orbit")

    def orbit(self, dt):
        crate, bubble = self.crate, state["bubble"]
        body, position, velocity = prop_state(crate)
        self.farthest = max(self.farthest, length(sub(position, bubble.origin)))
        if crate.state == "flying":
            self.flew = True
        t = world.time - self.t0
        if self.error is None and t >= self.period and self.last is not None:
            back = scale(velocity, t - self.period)
            self.error = length(sub(sub(position, back), self.start))
        self.last = t
        self.back_in = self.back_in or (self.flew and crate.body is not None)
        if (self.back_in and self.error is not None) or t > self.period + self.TIMEOUT:
            self.finish(self.back_in)

    def finish(self, back_in):
        state["warp"] = 0
        ok = self.flew and back_in and self.error is not None and self.error <= self.TOLERANCE
        print("[planet] orbit crate on the moon: period %.0f s, %s from the bubble's origin at most, %s after one "
              "period, %s: %s" % (self.period, distance_text(self.farthest),
                                  "%.1f m off" % self.error if self.error is not None else "no period",
                                  "back in the physics" if back_in else "NOT back (%s)" % self.crate.state,
                                  "passed" if ok else "FAILED"), flush=True)
        self.clear()
        self.next_body(ok)


class TrunkCheck:
    """--trunk-check: a player put down from preset 1 (5 m above the
    blocks) waits until it has stood on the ground for SETTLE seconds with
    at least one trunk in the bubble, then walks at each of the TARGETS
    trunks nearest it within REACH: put down START metres from the trunk's
    axis on the side it stood on, lifted LIFT, facing the trunk, it stands
    for SETTLE seconds and walks towards it for WALK seconds. Its feet must
    stop TOLERANCE from the trunk's radius plus its own from the axis.
    Prints each trunk's kind, radius and where the feet stopped, and exits
    with status 0 when every walk passed, 1 otherwise."""

    TARGETS = 5
    REACH = 15.0
    START = 3.0
    LIFT = 1.5
    SETTLE = 1.0
    WALK = 2.5
    TOLERANCE = 0.05
    TIMEOUT = 60.0

    def __init__(self):
        self.stage, self.clock, self.frames = "place", 0.0, 0
        self.results = []

    def __call__(self, dt):
        if planet.tree is None:
            return True
        self.frames += 1
        self.clock += dt
        getattr(self, self.stage)(dt)
        return True

    def next_stage(self, stage):
        self.stage, self.clock, self.frames = stage, 0.0, 0

    def place(self, dt):
        if not settled():
            return
        state["scripted_controls"] = True
        teleport(1)
        self.next_stage("spawn")

    def spawn(self, dt):
        if self.frames < 10 or not settled():
            return
        go_on_foot()
        if state["player"] is not None:
            self.next_stage("wait")

    def wait(self, dt):
        player = state["player"]
        if not player.on_ground or not trunks.trunks:
            self.clock = 0.0
            if self.frames > 60 * self.TIMEOUT:
                print("[planet] trunks: none came within %.0f m: FAILED" % COLLIDER_RADIUS, flush=True)
                os._exit(1)
            return
        if self.clock < self.SETTLE:
            return
        bubble = state["bubble"]
        feet = player.feet()
        near = []
        for key, trunk in trunks.trunks.items():
            base = self.base(trunk)
            d = self.across(sub(base, feet), bubble.up(base))
            if d <= self.REACH:
                near.append((d, key))
        self.targets = [key for _, key in sorted(near)[:self.TARGETS]]
        print("[planet] trunks: %d in the bubble, walking at the %d nearest" % (len(trunks.trunks), len(self.targets)),
              flush=True)
        self.next_target()

    @staticmethod
    def base(trunk):
        bubble = state["bubble"]
        return bubble.to_bubble(add(bubble.body.centre(), trunk.base))

    @staticmethod
    def across(v, up):
        """`v` without its part along `up`: its length."""
        return length(sub(v, scale(up, dot(v, up))))

    def next_target(self):
        if not self.targets:
            os._exit(0 if self.results and all(self.results) else 1)
        self.key = self.targets.pop(0)
        trunk = trunks.trunks.get(self.key)
        if trunk is None:
            print("[planet]   a trunk left the bubble before its walk: FAILED", flush=True)
            self.results.append(False)
            self.next_target()
            return
        player, bubble = state["player"], state["bubble"]
        base = self.base(trunk)
        up = bubble.up(base)
        away = sub(player.feet(), base)
        away = sub(away, scale(up, dot(away, up)))
        away = normalize(away) if length(away) > 1e-3 else local_frame(up)[0]
        physics.character_set_position(player.character, add(base, add(scale(away, self.START), scale(up, self.LIFT))))
        physics.character_set_velocity(player.character, (0.0, 0.0, 0.0))
        player.heading = player.level(scale(away, -1.0), up)
        player.pitch = 0.0
        state["foot_controls"] = FootControls()
        self.closest = math.inf
        self.next_stage("approach")

    def approach(self, dt):
        trunk = trunks.trunks.get(self.key)
        player, bubble = state["player"], state["bubble"]
        if trunk is None:
            print("[planet]   a trunk left the bubble during its walk: FAILED", flush=True)
            self.results.append(False)
            self.next_target()
            return
        if self.clock < self.SETTLE:
            return
        state["foot_controls"].forward = 1.0
        base = self.base(trunk)
        self.closest = min(self.closest, self.across(sub(player.feet(), base), bubble.up(base)))
        if self.clock < self.SETTLE + self.WALK:
            return
        state["foot_controls"].forward = 0.0
        radius = trunk.radius
        wanted = radius + PLAYER_RADIUS
        ok = abs(self.closest - wanted) <= self.TOLERANCE
        self.results.append(ok)
        print("[planet]   %s, trunk radius %.3f m: the feet stopped %.3f m from its axis (%.3f): %s"
              % (trunk.kind, radius, self.closest, wanted, "passed" if ok else "FAILED"), flush=True)
        self.next_target()


engine.set_on_init(on_init)
engine.set_on_key_pressed(on_key)
engine.input.set_on_mouse_scroll(on_scroll)
engine.run()

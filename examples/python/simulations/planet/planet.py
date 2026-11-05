"""
Shoonyakasha planet: procedural terrain on a cube-sphere with quadtree level
of detail, flown over by a ship

A planet of radius 100 km and its moon, flown around from a few metres
above the ground to thousands of kilometres away, with a free camera or a
ship (flight.py) seen from its cockpit or a chase view, or on foot
(shoonyakasha.planet.character). The planet, the
moon, the terrain's chunks built on the GPU, the atmosphere, the clouds,
the ocean and the shadows are shoonyakasha.planet's PlanetWorld, on the
planet pipeline; the cards, the hint bar, the controls sheet and the
settings menu are shoonyakasha.toolkit.overlay's. Positions are Python
floats (64-bit) with the planet's centre at the origin; the engine's camera
stays at its own origin and only turns. A cluster of blocks stands on the
+Z face, on a pad 5 cm above the ground.

Crates (shoonyakasha.planet.props) are rigid bodies in a physics bubble around the view
(shoonyakasha.planet.bubble, on sk.PlanetPhysics), on the body under it,
falling towards that body's centre onto a mesh of the terrain around the
view (shoonyakasha.planet.collider_probes). The bubble steps at
FLIGHT_STEP in real time, whatever the time warp; it and its crates are
dropped when the view moves to the other body or more than Bubble.RADIUS
above the ground. With a player on foot, the bubble follows the player
instead, and holds it, and the ship while it is within Bubble.RADIUS (a
kinematic hull of its contact points, moved with it). On the planet the
trees and cacti within on_foot.COLLIDER_RADIUS of the camera get trunks (shoonyakasha.planet.trunks),
and on both bodies the rocks static hulls (rocks.py), from
shoonyakasha.planet.nearby_trees. A rock the gravity gun grabs, pulls or
punts, or a body strikes hard enough, comes loose: it leaves the scatter
and becomes a prop. Besides the fine terrain mesh (32 m) the bubble keeps a
coarse one (256 m, on_foot.COARSE_SPACING apart), which only props off the fine
one stand on. Props (shoonyakasha.planet.props) leave the physics beyond the bubble's
reach, or low over neither mesh, and fly on around their body in doubles
or rest where they land, coming back in when the bubble reaches them; the
time warp on foot runs while none in the physics is awake. When the planet
is built again (a new seed, the terrain's keys, the menu's Apply), the
props on it are removed with its taken rocks.

F5 saves to SAVE_PATH, as JSON: the world's clock, the planet's terrain
parameters, the taken rocks, every prop as a record (shoonyakasha.planet.props), the ore
contacts, the credits and the outposts' supplies, the upgrades owned, the ship (its fuel and
cargo, and where it is parked), the mode, the free camera and the
player's feet. F9 loads it: the planet is built again when its parameters differ,
the props come back out of the physics and return to it by shoonyakasha.planet.props'
rules, and the view goes back to the free camera, the ship or the player.
The save is of SAVE_VERSION; one of version 1 is migrated as it loads
(migrate_save). With --play the game is also saved when the ship lands
on an outpost's pad after flying (autosave_on_landing).

Ore: the probes measure the ore richness and its resource (resources.glsl),
shown on the free camera's and the on-foot cards; where it is above 0.3
the scatter's rocks are ore rocks (tinted and with glowing specks in their
resource's colours), which come loose with their grade and resource. An ore boulder struck at rocks.BREAK_SPEED splits into
chunks; a strike is reported from on_foot.STRIKE_REPORTED and loosens a fixed rock
from on_foot.STRIKE_MOMENTUM. In the ship, M turns the right display to SCAN,
which scans the ground track ahead for contacts (scanner.py); below
ORE_HUD_HEIGHT the HUD marks the nearest. The contacts are saved, and
those on the planet go when it is built again.

Outposts (outposts.py): Base at the blocks, and more on flat ground of
the planet and the moon, found by a survey of seeded candidates that the
world measures before it builds the terrain; each is a flat site of the
terrain (PlanetWorld.set_sites) with a pad. When the planet is built
again its outposts are looked for again. The ship burns fuel and weighs
its fuel and cargo (flight.py). Loose ore rocks lying in the cargo net
beside the landed ship (NET_CENTRE, NET_HALF) are loaded into its cargo
when the player boards or the ship takes off, each that fits in the hold
(flight.Ship.can_take); the rest stay in the net. Landed on an outpost's
pad, Enter sells the cargo and Space fills the tanks as far as the
credits go (trade.py), shown on the outpost's card. Landed elsewhere, or
on a closed orbit with the periapsis above CALL_ORBIT_FLOOR, Space calls for fuel
from the nearest outpost on the body (trade.fuel_call), paid and put in
the tanks when it arrives if the ship is still in orbit or within
CALL_REACH of where it called from; its card shows below CALL_SHOWN of
the tanks and while the fuel is on its way.

Upgrades (upgrades.py): on an outpost's pad Page Up / Page Down turn the
outpost's card from its trade page through a page per upgrade (what it
changes, from now to after, its cost and what it needs), where Enter buys
it. The upgrades owned give the effective ship and gun parameters
(upgrades.effective, `apply_upgrades`), which the ship and the gun take
at once, as do every ship and gun made after.

The day (clock.py): with the day cycle on (the menu; on with --play)
the sun goes round with the world's clock plus the day's offset
(`day_time`; PLAY_HOUR at the start, Z / X an hour earlier / later, the
menu's Time of day), PlanetWorld.sun_elevation and sun_azimuth set from
it each frame (`update_day`), and the exposure scaled
(PlanetWorld.exposure_scale) by clock.exposure_scale for the sun's
elevation seen from the camera. The cards show the site's hour. The
save keeps the day cycle and its offset.

The player gets out of the ship with F once the ship touches the ground
at less than EXIT_SPEED relative to it, at the boarding point beside the
cockpit, and gets in with F within BOARD_REACH of it. A ship the player
left is parked: it moves with the body under it until boarded. On foot
the time warp (from the menu) runs only while no prop in the physics is
awake, and the body under the player splits to
on_foot.FOOT_MAX_LEVEL instead of MAX_LEVEL, so the ground drawn near the player
follows the terrain the physics stands on more closely. On foot the
player carries a gravity gun (shoonyakasha.planet.gravity_gun, drawn by viewmodel.py) that grabs,
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
  on foot (shoonyakasha.planet.character; the mouse is captured, Escape frees it):
    mouse                  look around
    W/A/S/D                walk forward, left, back, right
    Shift / Ctrl           run / crouch
    Space                  jump; held in the air, the jetpack
    right mouse            gravity gun (gravity_gun): grab, or pull from farther; again: drop
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
    M                      the right display: ORBIT / SCAN (ore scanning)
    Enter / Space          landed on an outpost's pad: sell the cargo / fill the tanks
    Space                  landed elsewhere or in orbit: call for fuel
    Page Up / Page Down    landed on an outpost's pad: the card's pages, trade and the upgrades
    Enter                  on an upgrade's page: buy it
    , / .                  time warp down / up (1x to 16x)
  ship, gamepad (adding to the keys):
    left stick             pitch and roll
    right stick            yaw (x), thrust up / down (y)
    right / left trigger   throttle up / down
    A                      translation assist on / off
    B                      throttle to 0
    bumpers                time warp down / up
    Y / Start / Back       free camera or on foot / settings menu / the controls
    D-pad up / down        landed on an outpost's pad: sell the cargo / fill the tanks
    D-pad down             landed elsewhere or in orbit: call for fuel
    D-pad left / right     landed on an outpost's pad: the card's pages (D-pad up buys an upgrade)
    X                  chase view / cockpit; in a landed ship, out of it
  anywhere:
    1 - 5                  5 m above the blocks, 1 km, 50 km, 1,000 km, 5,000 km
    6                      above the corner where the +X, +Y and +Z faces meet
    7                      2 km up, 25 km north-west of the blocks, looking further north-west
    8                      the ship at rest beside the blocks, on the Base outpost's pad
    9                      the ship in a circular orbit 40 km up
    Shift+8                the ship at rest on the moon, between the sun and the planet
    Shift+9                the ship in a circular orbit 10 km above the moon
    0 / Shift+0            drop a crate / a stack of 10 crates in front of the view
    N                      new terrain seed
    M / J                  mountains higher / lower (M in the ship: the right display's page)
    H / G                  hills higher / lower
    Z / X                  sun lower / higher, seen from the blocks; with the day cycle on, an hour earlier / later
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
    F5 / F9                save / load (planet_save.json)

    python planet.py --capture DIR     screenshots at the presets, then exit
    python planet.py --generated-cockpit   start with the generated cockpit instead of the model's cabin
    python planet.py --play            play: start on foot beside the ship at Base; the places (0 - 9) and the
                                       keys that change the world (N, M / J, H / G, Z / X, T / Y, B / V, the
                                       menu's planet section) are off; 8 makes a new ship at Base after a crash
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
    python planet.py --save-check      move rocks, throw a crate, save, build a new planet, load, check what came back,
                                       then load it as a save of version 1
    python planet.py --scan-check      scan for ore from orbit of the planet and the moon, check the contacts' rocks
    python planet.py --break-check     throw a crate into an ore boulder on the planet and the moon, check it splits
    python planet.py --fuel-check      burn the tanks dry from orbit, check the burn's time and the speed gained,
                                       then call for fuel in orbit
    python planet.py --cargo-check     put ore rocks in the cargo net, board and take off, check what was loaded
    python planet.py --trade-check     land on an outpost on the planet and on the moon, sell ore and buy fuel
    python planet.py --upgrade-check   buy upgrades at Base, check the ship's and the gun's parameters through a save
    python planet.py --day-check       run one day at x16, check the sun, the exposure and the horizons' rebuilds
"""

import argparse
import json
import math
import os
import random
import time

import numpy as np
import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.planet import biomes
from shoonyakasha.planet import resources
from shoonyakasha.planet import settings as planet_settings
from shoonyakasha.planet import world as planet_world
from shoonyakasha.planet.bubble import Bubble
from shoonyakasha.planet.gravity_gun import GunParams
from shoonyakasha.planet.ground_patch import GroundPatch, SIZE as PATCH_SIZE
from shoonyakasha.planet.on_foot import STEP as FOOT_STEP, OnFoot, stick
from shoonyakasha.planet.props import stack_offsets
from shoonyakasha.planet.vec import (add, cross, dot, euler_from_basis, length, local_frame, normalize, quat_from_basis,
                                     quat_normalize, quat_rotate, rotate, scale, sub)
from shoonyakasha.toolkit.overlay import Card, HelpOverlay, HintBar, SettingsMenu
from shoonyakasha.planet.world import (COLOUR_MODES, EXTRA_PROBES, FOV, MAX_LEVEL, ORBIT_POINTS, RADIUS, SLOTS,
                                       SUN_AZIMUTH, SUN_ELEVATION)

import flight
import cockpit
from instruments import Instruments
import outposts
from outposts import Pads, Survey, home_outpost
from rocks import Rocks
from scanner import CONTACT, Scanner
import clock
import trade
import upgrades
from trade import Market
from viewmodel import Viewmodel

HERE = os.path.dirname(os.path.abspath(__file__))
CAPTURES = os.path.join(HERE, "planet_captures")
SAVE_PATH = os.path.join(HERE, "planet_save.json")   # F5 / F9
SAVE_VERSION = 2

DAY_SUN = (SUN_ELEVATION, SUN_AZIMUTH)
PIXEL_ANGLE = math.radians(FOV) / 900.0   # radians across a pixel at the window's 900 rows
LOOK_RADIANS_PER_PIXEL = 0.0025
MIN_ALTITUDE = 0.3               # the camera stays this far above the ground
# The ship (flight.py).
FLIGHT_STEP = 1.0 / 120.0        # seconds per physics step
MAX_STEPS_PER_FRAME = 64
WARPS = (1, 2, 4, 8, 16)         # time warps
SHIP_CLEARANCE = -flight.ShipParams().gear[0][1]   # metres from the ship's centre down to its gear
SET_DOWN_DEPTH = 0.01            # m into the ground the presets put the ship's lowest gear, so it rests on it
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
# Gamepad buttons and the keys they press: A assist, B throttle to 0, the bumpers time warp,
# Y ship or free camera, X chase view, Start the menu, Back the controls sheet, the D-pad up and down
# sell the cargo and fill the tanks at an outpost (down calls for fuel elsewhere), left and right turn
# the outpost card's pages (up buys the upgrade shown).
GAMEPAD_KEYS = {keys.GAMEPAD_A: keys.L, keys.GAMEPAD_B: keys.BACKSPACE, keys.GAMEPAD_LEFT_BUMPER: keys.COMMA,
                keys.GAMEPAD_RIGHT_BUMPER: keys.PERIOD, keys.GAMEPAD_Y: keys.TAB, keys.GAMEPAD_START: keys.F1,
                keys.GAMEPAD_X: keys.R, keys.GAMEPAD_BACK: keys.F2, keys.GAMEPAD_DPAD_UP: keys.ENTER,
                keys.GAMEPAD_DPAD_DOWN: keys.SPACE, keys.GAMEPAD_DPAD_LEFT: keys.PAGE_UP,
                keys.GAMEPAD_DPAD_RIGHT: keys.PAGE_DOWN}
# On foot FOOT_GAMEPAD_KEYS: X gets in, Y the free camera, the bumpers the gun's hold distance. In a landed
# ship X gets out (read_gamepad_buttons).
ORBIT_HEIGHT = 40_000.0          # preset 9's circular orbit
MOON_ORBIT_HEIGHT = 10_000.0     # Shift+9's circular orbit of the moon
MOON_HOVER_HEIGHT = 2_000.0      # Shift+8's ship waits this far above the moon's radius until its ground is known
# The physics bubble and its crates.
CRATE_SIZE = 0.6                 # m along each edge
CRATE_DENSITY = 500.0            # kg/m^3
CRATE_AHEAD = 3.0                # m in front of the view a crate is dropped
STACK_COUNT = 10
JUMP_DISTANCE = 50.0             # m the viewpoint may move in a frame and still count as moving, not jumping
ORE_HUD_HEIGHT = 5_000.0         # m above the ground below which the HUD marks the nearest ore contact
# Calling for fuel (trade.fuel_call): offered on the card below CALL_SHOWN of the tanks, in orbit where
# the periapsis is above CALL_ORBIT_FLOOR of the body; a landed ship must stay within CALL_REACH of
# where it called from.
CALL_SHOWN = 0.25
CALL_ORBIT_FLOOR = {"planet": 25_000.0, "moon": 5_000.0}    # m
CALL_REACH = 50.0                # m
PLAY_HOUR = 9.0                  # the site's hour when the day starts without a save
# The cargo net beside the landed ship: its centre in the ship's body (right, up, back; up at the
# gear's feet), its half sizes along right and back, and how far below and above the gear's feet
# a loose ore rock's centre may be in it.
NET_CENTRE = (11.0, -SHIP_CLEARANCE, 1.0)
NET_HALF = (3.0, 3.5)
NET_BELOW, NET_ABOVE = 1.5, 4.0
NET_POST = (0.15, 1.2, 0.15)     # m: the posts at its corners
NET_COLOUR = (1.0, 0.72, 0.3)
# On foot.
EXIT_SPEED = 1.0                 # m/s relative to the ground the ship may move at for the player to get out
BOARD_REACH = 3.0                # m from the boarding point the player gets in from
BOARD_OUT = 3.5                  # m left of the ship's centre line the boarding point is, outside the hull
SPAWN_LIFT = 1.0                 # m above the ground a player is put down, to fall onto it
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
parser.add_argument("--play", action="store_true",
                    help="play: start on foot beside the ship at Base, without the places, the crates or the "
                         "keys that change the world")
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
parser.add_argument("--scan-check", action="store_true",
                    help="scan for ore from orbit of the planet and the moon, check the contacts and their rocks")
parser.add_argument("--break-check", action="store_true",
                    help="throw a crate into an ore boulder on the planet and the moon, check it splits, and exit")
parser.add_argument("--fuel-check", action="store_true",
                    help="burn the tanks dry from orbit, check the fuel's time and the speed gained, call for fuel "
                         "in orbit, and exit")
parser.add_argument("--cargo-check", action="store_true",
                    help="put ore rocks in the cargo net, board and take off, check what was loaded, and exit")
parser.add_argument("--trade-check", action="store_true",
                    help="land on an outpost on the planet and on the moon, sell ore and buy fuel, and exit")
parser.add_argument("--day-check", action="store_true",
                    help="run one day at x16, check the sun, the exposure and how often the horizons are measured "
                         "again, and exit")
parser.add_argument("--upgrade-check", action="store_true",
                    help="buy upgrades at Base, check the ship's and the gun's parameters through a save, and exit")
parser.add_argument("--save-check", action="store_true",
                    help="move rocks, throw a crate, save, build a new planet, load, check what came back, and exit")
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
    "tracked": None,             # the prop whose path is shown (T)
    "viewmodel": None,           # the gun's viewmodel (viewmodel.Viewmodel)
    "captured": False,           # whether the frame last asked for the mouse captured
    "parked": None,              # (body, position relative to it) of a ship the player left
    "ship_body": None,           # the ship's kinematic hull in the bubble
    "ship_poses": None,          # (position, orientation) of the ship before and after this frame's flight
    "regenerations": 0,          # world.regenerations the props were last kept under
    "page": "orbit",             # the right display's page in the ship: "orbit" or "scan" (M)
    "home": None,                # the outpost at the blocks (outposts.home_outpost)
    "outposts": [],              # every outpost, the home one first
    "survey": None,              # the outposts' survey (outposts.Survey) while the world measures it
    "outpost_regenerations": 0,  # world.regenerations the planet's outposts were found under
    "supply_pending": {},        # outpost name: supply from a save, for outposts not found yet
    "market": Market(),          # credits (trade.py)
    "trade_note": "",            # the last sale, purchase or load, for the cards
    "net_posts": [],             # the cargo net's corner posts
    "was_landed": False,         # the ship was landed (ship_landed) in the last frame in ship mode
    "airborne": False,           # the ship has flown since it last landed on an outpost's pad
    "day_cycle": args.play,      # the sun goes round with the world's clock (clock.py)
    "day_offset": None,          # s added to the world's clock for the day (clock.py); None: PLAY_HOUR at the start
    "trade_card": None,          # the outpost's card, or the fuel call's (toolkit.overlay.Card)
    "fuel_call": None,           # the fuel on its way (call_fuel), or None
    "owned": set(),              # the keys of the upgrades bought (upgrades.py)
    "params": upgrades.effective({"ship": flight.ShipParams(), "gun": GunParams()}, set()),
    "outpost_page": 0,           # the outpost card's page: 0 trade, k the k-th of upgrades.UPGRADES
    "play_start": False,         # --play: the ship and the player still to be put at Base
}
foot = OnFoot(engine, world, box(), make_rocks=lambda props: Rocks(engine, world, props))
foot.on_drop = lambda: remove_ship_body()
physics, nearby_trees, trunks, props, scatter_rocks = (foot.physics, foot.nearby_trees, foot.trunks, foot.props,
                                                       foot.rocks)
scanner = Scanner(world, EXTRA_PROBES)
pads = Pads(engine, box())


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
    if state["mode"] == "foot" and foot.view is not None:
        return foot.view
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
    state["cards"] = (Card(engine.ui, canvas, (0, 0), 16), Card(engine.ui, canvas, (0, 1), 16, margin=(24, 76)))
    state["trade_card"] = Card(engine.ui, canvas, (1, 0), 17, width=400)
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
    """8: at rest 16 m south of the blocks (its gear SET_DOWN_DEPTH into the ground), facing north,
    translation assist on. 9: in a circular orbit ORBIT_HEIGHT above the
    blocks, moving east with the nose 40 degrees down, where the planet is,
    assist off."""
    _, up, east, north = world.site_frame()
    if preset == 8:
        position = add(world.site_ground(), add(scale(up, SHIP_CLEARANCE - SET_DOWN_DEPTH), scale(north, -16.0)))
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
                                orientation=quat_from_basis(right, up, scale(forward, -1.0)),
                                params=state["params"]["ship"])
    state["controls"] = flight.Controls(assist=assist)
    state["mode"] = "ship"
    state["position"] = position
    state["forward"] = forward


def moon_body():
    return next(body for body in bodies if body.name == "moon")


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
        feet = [normalize(add(scale(up, moon.radius), quat_rotate(q, arm)))
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
    spot, moving with the moon, its centre SHIP_CLEARANCE less
    SET_DOWN_DEPTH above the highest of the ground under it and under its
    gear points, so that it starts touching the ground and rests on it."""
    spot = state["moon_landing"]
    if spot is None or state["mode"] != "ship":
        return
    asked = world.extra_probes
    heights = world.extra_heights
    if [probe for probe, _ in heights] != asked:
        return
    body = asked[0][1]
    ground = max(height for _, height in heights)
    new_ship(add(body.centre(), scale(spot, body.radius + ground + SHIP_CLEARANCE - SET_DOWN_DEPTH)), body.velocity(),
             local_frame(spot)[1], spot, True)
    state["moon_landing"] = None
    world.extra_probes = []


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

def ensure_bubble(point=None):
    """The bubble on the body under `point` (the viewpoint when None), made
    around it if there is none there; a bubble on the other body is
    dropped first (on_foot.OnFoot.ensure_bubble)."""
    return foot.ensure_bubble(viewpoint() if point is None else point)


def drop_bubble():
    """The bubble gone (on_foot.OnFoot.drop_bubble), and with it the ship's hull."""
    foot.drop_bubble()


def drop_crates(count):
    """`count` crates stacked CRATE_AHEAD in front of the view, square to
    the body's up there and turned with the view's heading; with a player,
    into the player's bubble, and none while the view is over the other body."""
    if foot.player is not None:
        bubble = foot.bubble
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
    rotation = quat_from_basis(right, up, scale(heading, -1.0))
    for height in stack_offsets(count, CRATE_SIZE):
        props.spawn_crate(bubble, add(p, scale(up, height)), rotation, CRATE_SIZE, CRATE_DENSITY)


def step_bubble(dt):
    """The bubble's frame (on_foot.OnFoot.step): the player with its
    controls and its gravity gun on foot, standing still otherwise; each
    physics step the ship's hull moved along the ship's path this frame."""
    foot.step(dt, viewpoint(), state["position"], world.probe_latency * state["frame_ms"] / 1000.0,
              active=state["mode"] == "foot", before_step=move_ship_body)
    note_thrown()


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
    q = quat_normalize(tuple(a + (b - a) * fraction for a, b in zip(q0, q1)))
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
        physics.move_kinematic(body, p, q, FOOT_STEP)


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
        contact = ground(add(ship.position, quat_rotate(ship.orientation, gear)))
        if contact is None or contact[0] < -flight.GEAR_TOUCH:
            return False
    return True


def spawn_player(bubble, feet_world, heading):
    foot.spawn(feet_world, heading)
    if foot.gun is not None:
        foot.gun.p = state["params"]["gun"]
    state["mode"] = "foot"
    state["warp"] = 0
    place_eye()


def remove_player():
    foot.remove_player()


def go_on_foot():
    """From the free camera: back to the player, or a player put down
    SPAWN_LIFT above the ground under the camera, facing its way, when the
    ground there is known and within Bubble.RADIUS below."""
    if foot.player is not None:
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
    """Into the ship from within BOARD_REACH of its boarding point, while it
    is landed, loading the ore in its cargo net (load_cargo)."""
    ship, player, bubble = state["ship"], foot.player, foot.bubble
    if player is None or not ship_landed(ship):
        return
    if length(sub(bubble.to_world(player.feet()), boarding_point(ship))) > BOARD_REACH:
        return
    load_cargo()
    remove_player()
    if state["parked"] is not None:
        ship.velocity = state["parked"][0].velocity()
    state["parked"] = None
    state["mode"] = "ship"
    state["was_landed"] = True


def near_boarding_point():
    ship, player, bubble = state["ship"], foot.player, foot.bubble
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
    """The player's and the gravity gun's controls (on_foot.OnFoot.read_controls)."""
    foot.read_controls(engine.input)


def place_eye():
    """The camera at the player's eye, looking its way."""
    eye = foot.place_eye()
    if eye is None:
        return
    state["position"] = eye
    state["forward"] = foot.view[2]


def foot_warp():
    """The time warp on foot: as set while no prop in the physics is awake, else 1x."""
    bubble = foot.bubble
    if bubble is not None and props.awake(bubble):
        return 1
    return WARPS[state["warp"]]


def note_thrown():
    """The prop the gun punted last, while it is one (T tracks it)."""
    gun = foot.gun
    if gun is None or gun.last_punt is None or gun.last_punt is state.get("noted_punt"):
        return
    state["noted_punt"] = gun.last_punt
    body = gun.last_punt[0]
    thrown = props.find(body) if body is not None else None
    if thrown is not None:
        state["last_thrown"] = thrown


def prop_state(prop):
    """(the world body it is reckoned around, position from its centre, velocity) of a prop."""
    return foot.prop_state(prop)


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
    viewmodel, gun, player = state["viewmodel"], foot.gun, foot.player
    on_foot = state["mode"] == "foot" and player is not None
    viewmodel.show(on_foot)
    if not on_foot:
        return
    held = None
    if gun.held is not None:
        held = sub(foot.bubble.to_world(physics.get_transform(gun.held)[0]), camera)
    viewmodel.update(dt, view_basis(), foot.look_delta, player, gun, held)


def set_max_levels():
    """on_foot.FOOT_MAX_LEVEL for the body under the player on foot, MAX_LEVEL for every other tree."""
    for body in bodies:
        tree = body.tree
        if tree is None:
            continue
        wanted = foot.max_level(body, state["mode"] == "foot") or MAX_LEVEL
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


# ── Outposts, cargo and trade ──────────────────────────────────────────────

def begin_survey(survey_bodies):
    """The outposts on `survey_bodies` looked for again (outposts.Survey):
    the world measures the candidates before it builds the terrain."""
    survey = Survey(survey_bodies, tuple(planet.terrain["seed"]))
    state["survey"] = survey
    world.start_survey(survey.points)


def update_outposts():
    """The home outpost at the site's height; the planet's outposts looked
    for again after it was built again; and once a survey's heights are
    back, its outposts chosen, their sites flattened and their pads drawn."""
    home = state["home"]
    home.height = world.site_height or 0.0
    if state["outpost_regenerations"] != world.regenerations:
        state["outpost_regenerations"] = world.regenerations
        state["outposts"] = [o for o in state["outposts"] if o.body is not planet]
        begin_survey([planet])
    survey = state["survey"]
    heights = world.surveyed() if survey is not None else None
    if heights is None:
        return
    surveyed = {body for body, _ in survey.candidates}
    kept = [o for o in state["outposts"] if o is not home and o.body not in surveyed]
    found = survey.choose(heights, taken=kept, home=home)
    for outpost in found:
        outpost.supply = state["supply_pending"].pop(outpost.name, {})
    state["outposts"] = [home] + sorted(kept + found, key=lambda o: (o.body.index, o.name))
    world.set_sites([o.site() for o in state["outposts"] if o.flat_radius > 0.0])
    world.end_survey()
    state["survey"] = None
    pads.build(state["outposts"])
    print("[planet] outposts: %s" % ", ".join(
        "%s on the %s (%.0f m)" % (o.name, o.body.name, o.height) for o in state["outposts"][1:]), flush=True)


def outpost_here():
    """The outpost whose pad the landed ship is on, in ship mode, or None."""
    ship = state["ship"]
    if state["mode"] != "ship" or not ship_landed(ship):
        return None
    return next((o for o in state["outposts"] if o.at(ship.position)), None)


def sell_cargo():
    outpost = outpost_here()
    if outpost is None:
        return
    kg, credits = state["market"].sell(state["ship"], outpost, world.time)
    state["trade_note"] = ("sold %s of ore for %s" % (mass_text(kg), credits_text(credits)) if kg > 0.0
                           else "no cargo to sell")


def buy_fuel():
    outpost = outpost_here()
    if outpost is None:
        return
    kg, cost = state["market"].buy_fuel(state["ship"], outpost)
    state["trade_note"] = ("bought %.0f kg of fuel for %s" % (kg, credits_text(cost)) if kg > 0.5
                           else "the tanks are full" if state["ship"].fuel >= state["ship"].params.fuel_capacity - 0.5
                           else "no credits for fuel")


def apply_upgrades(owned):
    """The upgrades `owned` (keys) and their effective parameters
    (upgrades.effective): the ship's and the gun's now, and those of any
    ship or gun made from here on."""
    state["owned"] = set(owned)
    state["params"] = upgrades.effective({"ship": flight.ShipParams(), "gun": GunParams()}, state["owned"])
    if state["ship"] is not None:
        state["ship"].params = state["params"]["ship"]
    if foot.gun is not None:
        foot.gun.p = state["params"]["gun"]


def buy_upgrade(upgrade):
    """`upgrade` bought with the credits, when upgrades.refusal allows it; the note says what happened."""
    market = state["market"]
    reason = upgrades.refusal(upgrade, state["owned"], market.credits)
    if reason is not None:
        state["trade_note"] = "%s: %s" % (upgrade.name, reason)
        return
    market.credits -= upgrade.cost
    apply_upgrades(state["owned"] | {upgrade.key})
    state["trade_note"] = "bought %s for %s" % (upgrade.name, credits_text(upgrade.cost))


def turn_page(step):
    """The outpost card's page `step` on (wrapping): trade, then each upgrade."""
    if outpost_here() is not None:
        state["outpost_page"] = (state["outpost_page"] + step) % (len(upgrades.UPGRADES) + 1)


def upgrade_rows(upgrade):
    """The outpost card's page of `upgrade`: credits, its cost, each effect
    from now to after, what it needs, whether it can be bought, the keys."""
    pad = state["input"] == "pad"
    owned = upgrade.key in state["owned"]
    now = state["params"]
    after = now if owned else upgrades.effective({"ship": flight.ShipParams(), "gun": GunParams()},
                                                 state["owned"] | {upgrade.key})
    rows = [("CREDITS", credits_text(state["market"].credits)), ("COST", credits_text(upgrade.cost)),
            ("CHANGES",)]
    for path in upgrade.effects:
        group, attribute = path.split(".")
        name, shown = upgrades.EFFECTS[path]
        if owned:
            base = getattr({"ship": flight.ShipParams(), "gun": GunParams()}[group], attribute)
            rows.append((name, "%s, was %s" % (shown(getattr(now[group], attribute)), shown(base))))
        else:
            rows.append((name, "%s to %s" % (shown(getattr(now[group], attribute)),
                                             shown(getattr(after[group], attribute)))))
    if upgrade.requires:
        rows.append(("NEEDS", ", ".join(upgrades.BY_KEY[k].name for k in upgrade.requires)))
    reason = upgrades.refusal(upgrade, state["owned"], state["market"].credits)
    rows += [("UPGRADE",),
             ("BUY (%s)" % ("D-PAD UP" if pad else "ENTER"), reason if reason is not None else "available"),
             ("PAGES (%s)" % ("D-PAD LEFT / RIGHT" if pad else "PG UP / PG DN"),
              "%d of %d" % (state["outpost_page"] + 1, len(upgrades.UPGRADES) + 1))]
    if state["trade_note"]:
        rows.append(("LAST", state["trade_note"]))
    return rows


def call_place(ship):
    """(the body, whether in orbit) of a ship fuel can be called to: landed
    off every outpost's pad, or on a closed orbit with its periapsis above
    CALL_ORBIT_FLOOR; else None."""
    if ship is None or ship.crashed:
        return None
    if ship_landed(ship):
        if any(o.at(ship.position) for o in state["outposts"]):
            return None
        return world.body_under(ship.position), False
    name, periapsis, apoapsis, _ = ship_orbit()
    if periapsis < CALL_ORBIT_FLOOR[name] or math.isinf(apoapsis):
        return None
    return (planet if name == "planet" else moon_body()), True


def fuel_call_offer():
    """(the nearest outpost on the ship's body, metres along the ground to
    it, whether in orbit) for a fuel call in ship mode (call_place), or None."""
    ship = state["ship"]
    place = call_place(ship) if state["mode"] == "ship" else None
    if place is None:
        return None
    body, in_orbit = place
    here = normalize(body.relative(ship.position))
    found = [(o, outposts.ground_distance(body.radius, o.direction, here)) for o in state["outposts"] if o.body is body]
    if not found:
        return None
    outpost, distance = min(found, key=lambda f: f[1])
    return outpost, distance, in_orbit


def call_fuel():
    """Fuel called from the nearest outpost (fuel_call_offer,
    trade.fuel_call), arriving after its delay; paid on arrival."""
    ship = state["ship"]
    if state["fuel_call"] is not None:
        state["trade_note"] = "fuel is on its way"
        return
    offer = fuel_call_offer()
    if offer is None:
        return
    if ship.fuel >= ship.params.fuel_capacity - 0.5:
        state["trade_note"] = "the tanks are full"
        return
    outpost, distance, in_orbit = offer
    price, fee, delay = trade.fuel_call(outpost, distance, in_orbit)
    if state["market"].credits <= fee:
        state["trade_note"] = "no credits for a fuel call"
        return
    body = outpost.body
    state["fuel_call"] = {"ship": ship, "outpost": outpost, "price": price, "fee": fee, "due": world.time + delay,
                          "in_orbit": in_orbit, "body": body, "spot": body.relative(ship.position)}
    state["trade_note"] = "fuel called from %s, arriving in %.0f s" % (outpost.name, delay)


def update_fuel_call():
    """The called fuel delivered once it is due, if the ship is still in
    orbit, or still landed within CALL_REACH of where it called from;
    otherwise the call is off, unpaid."""
    call = state["fuel_call"]
    if call is None or world.time < call["due"]:
        return
    state["fuel_call"] = None
    ship = call["ship"]
    place = call_place(ship) if ship is state["ship"] else None
    there = place is not None and place[1] == call["in_orbit"] and (
        call["in_orbit"] or length(sub(call["body"].relative(ship.position), call["spot"])) <= CALL_REACH)
    if not there:
        state["trade_note"] = "the fuel call missed the ship"
        return
    kg, cost = state["market"].call_fuel(ship, call["price"], call["fee"])
    state["trade_note"] = ("fuel delivered: %.0f kg for %s" % (kg, credits_text(cost)) if kg > 0.0
                           else "no credits for the called fuel")


def fuel_call_rows():
    """The fuel call's card (title, rows): while fuel is on its way, or
    where fuel can be called (fuel_call_offer) and the tanks are below
    CALL_SHOWN of their capacity; else None."""
    ship, market, call = state["ship"], state["market"], state["fuel_call"]
    if ship is None:
        return None
    fuel = ("FUEL", "%.0f / %.0f kg" % (ship.fuel, ship.params.fuel_capacity))
    if call is not None:
        rows = [("CREDITS", credits_text(market.credits)), fuel,
                ("ARRIVES IN", "%.0f s" % max(call["due"] - world.time, 0.0))]
        return "FUEL CALL FROM %s" % call["outpost"].name.upper(), rows
    offer = fuel_call_offer()
    if offer is None or ship.fuel >= CALL_SHOWN * ship.params.fuel_capacity:
        return None
    outpost, distance, in_orbit = offer
    price, fee, delay = trade.fuel_call(outpost, distance, in_orbit)
    kg = max(min(ship.params.fuel_capacity - ship.fuel, (market.credits - fee) / price), 0.0)
    rows = [("CREDITS", credits_text(market.credits)), fuel,
            ("FROM", "%s, %s" % (outpost.name, distance_text(distance))),
            ("PRICE", "%.2f cr/kg" % price), ("FEE", credits_text(fee)), ("ARRIVES IN", "%.0f s" % delay),
            ("CALL FOR FUEL (%s)" % ("D-PAD DOWN" if state["input"] == "pad" else "SPACE"),
             "%.0f kg, %s" % (kg, credits_text(fee + kg * price)) if kg > 0.0 else "no credits")]
    if state["trade_note"]:
        rows.append(("LAST", state["trade_note"]))
    return "FUEL CALL", rows


def mass_text(kg):
    return "%.0f kg" % kg if kg < 1_000.0 else "%.1f t" % (kg / 1_000.0)


def credits_text(credits):
    return "{:,.0f} cr".format(credits)


def net_frame(ship):
    """The cargo net's centre and (right, up, back) in the world."""
    right, up, forward = ship.axes()
    back = scale(forward, -1.0)
    r, u, b = NET_CENTRE
    centre = add(ship.position, add(add(scale(right, r), scale(up, u)), scale(back, b)))
    return centre, right, up, back


def in_net(ship, point):
    """Whether a point (from the planet's centre) lies in the cargo net beside the ship."""
    centre, right, up, back = net_frame(ship)
    d = sub(point, centre)
    return (abs(dot(d, right)) <= NET_HALF[0] and abs(dot(d, back)) <= NET_HALF[1]
            and -NET_BELOW <= dot(d, up) <= NET_ABOVE)


def load_cargo():
    """The loose ore rocks lying in the cargo net beside the ship (in_net_now;
    on boarding, or as it takes off) taken out of the world into its
    cargo, each that fits in the hold (flight.Ship.can_take); a rock's cell
    stays taken. Those that do not stay in the net."""
    ship, bubble = state["ship"], foot.bubble
    loaded, left = [], 0
    for prop, point in in_net_now():
        if not ship.can_take(prop.mass, prop.resource):
            left += 1
            continue
        body = prop.on
        ship.cargo.append({"body": body.name, "direction": normalize(body.relative(point)), "mass": prop.mass,
                           "grade": prop.grade, "resource": prop.resource})
        if foot.gun is not None and prop.body is not None and foot.gun.held == prop.body:
            foot.gun.drop()
        props.remove(bubble, prop, release=False)
        loaded.append(prop.mass)
    if loaded or left:
        state["trade_note"] = ("loaded %d piece%s of ore, %s" % (len(loaded), "" if len(loaded) == 1 else "s",
                                                                  mass_text(sum(loaded)))
                               + ("; %d left in the net, the hold is full" % left if left else ""))


def in_net_now():
    """(prop, its position from the planet's centre) of each loose ore rock
    in the physics or resting out of it that lies in the cargo net beside
    the ship."""
    ship, bubble = state["ship"], foot.bubble
    if ship is None or ship.crashed:
        return []
    ore = [p for p in props.rocks if p.grade > 0.0]
    placed = [(p, add(p.on.centre(), p.position)) for p in ore if p.state == "resting"]
    moving = [p for p in ore if p.state == "jolt"]
    if bubble is not None and moving:
        placed += [(p, bubble.to_world(t[0])) for p, t in zip(moving, props.transforms(bubble, moving))]
    return [(prop, point) for prop, point in placed if in_net(ship, point)]


def net_rows():
    """The card's rows of the ore in the cargo net beside the landed ship,
    which boarding loads, and of how many pieces of it will not fit in the
    hold (taken in the order load_cargo takes them)."""
    ship = state["ship"]
    pieces = [prop for prop, _ in in_net_now()] if ship_landed(ship) else []
    if not pieces:
        return []
    rows = [("CARGO NET", "%d piece%s, %s" % (len(pieces), "" if len(pieces) == 1 else "s",
                                              mass_text(sum(p.mass for p in pieces))))]
    trial = flight.Ship(ship.position, params=ship.params, cargo=list(ship.cargo))
    left = 0
    for p in pieces:
        if trial.can_take(p.mass, p.resource):
            trial.cargo.append({"mass": p.mass, "resource": p.resource})
        else:
            left += 1
    if left:
        rows.append(("WILL NOT FIT", "%d piece%s" % (left, "" if left == 1 else "s")))
    return rows


def build_net():
    positions, normals = box()
    for _ in range(4):
        entity = engine.create_mesh(positions, normals=normals, name="CargoNet")
        engine.scene.set_scale(entity, NET_POST)
        engine.scene.set_material_vec4(entity, "baseColorFactor", NET_COLOUR + (1.0,))
        engine.scene.set_material_float(entity, "gridStrength", 0.0)
        engine.scene.set_render_layer_mask(entity, 1)
        engine.scene.set_visible(entity, False)
        state["net_posts"].append(entity)


def draw_net(camera):
    """The cargo net's corner posts beside the landed ship, outside ship mode."""
    ship = state["ship"]
    shown = state["mode"] != "ship" and ship_landed(ship)
    if shown:
        centre, right, up, back = net_frame(ship)
        rotation = euler_from_basis(right, up, scale(back, -1.0))
    for k, entity in enumerate(state["net_posts"]):
        engine.scene.set_visible(entity, shown)
        if not shown:
            continue
        corner = add(scale(right, NET_HALF[0] * (1 if k & 1 else -1)), scale(back, NET_HALF[1] * (1 if k & 2 else -1)))
        engine.scene.set_position(entity, sub(add(add(centre, corner), scale(up, 0.5 * NET_POST[1])), camera))
        engine.scene.set_rotation(entity, rotation)


def cargo_value():
    """(the nearest outpost to the ship, what its cargo would fetch there), or (None, 0)."""
    ship = state["ship"]
    outpost = outposts.nearest(state["outposts"], ship.position)
    if outpost is None or not ship.cargo:
        return outpost, 0.0
    return outpost, state["market"].value(outpost, ship.cargo, world.time)


def trade_rows(outpost):
    """The outpost card's trade page: credits, cargo, fuel, its prices, the
    trades, the upgrades owned and the last trade."""
    ship, market = state["ship"], state["market"]
    pad = state["input"] == "pad"
    sample = lambda body, resource: {"body": body.name, "direction": outpost.direction, "mass": 1.0, "grade": 1.0,
                                     "resource": resource}
    found = lambda body: (range(resources.FERRITE, resources.EVAPORITE + 1) if body is planet
                          else (resources.ICE, resources.ILMENITE))
    value = market.value(outpost, ship.cargo, world.time) if ship.cargo else 0.0
    room = ship.params.fuel_capacity - ship.fuel
    rows = [
        ("CREDITS", credits_text(market.credits)),
        ("CARGO", "%s, %.1f / %.1f m3, %d piece%s" % (mass_text(ship.cargo_mass), ship.cargo_volume,
                                                       ship.params.hold_m3, len(ship.cargo),
                                                       "" if len(ship.cargo) == 1 else "s")),
        ("FUEL", "%.0f / %.0f kg" % (ship.fuel, ship.params.fuel_capacity)),
        ("PRICES",),
    ] + [("%s, GRADE 1" % resources.name(r).upper(), "%.2f cr/kg" % trade.price(outpost, sample(body, r), world.time))
         for body in bodies for r in found(body)] + [
        ("FUEL", "%.2f cr/kg" % outpost.fuel_price),
        ("TRADE",),
        ("SELL THE CARGO (%s)" % ("D-PAD UP" if pad else "ENTER"), credits_text(value)),
        ("FILL THE TANKS (%s)" % ("D-PAD DOWN" if pad else "SPACE"), credits_text(room * outpost.fuel_price)),
        ("UPGRADES (%s)" % ("D-PAD RIGHT" if pad else "PG DN"),
         "%d of %d owned" % (len(state["owned"]), len(upgrades.UPGRADES))),
    ]
    if state["trade_note"]:
        rows.append(("LAST", state["trade_note"]))
    return rows


# ── Saving and loading ─────────────────────────────────────────────────────

def forget_planet_props():
    """The props and the ore contacts on the planet removed once it was
    built again (PlanetWorld.regenerate)."""
    if state["regenerations"] == world.regenerations:
        return
    state["regenerations"] = world.regenerations
    scanner.contacts = [c for c in scanner.contacts if c.body is not planet]
    foot.forget_body(planet)


def ship_record(ship):
    if ship is None:
        return None
    return {"position": list(ship.position), "velocity": list(ship.velocity), "orientation": list(ship.orientation),
            "angular_velocity": list(ship.angular_velocity), "on_ground": ship.on_ground, "resting": ship.resting,
            "crashed": ship.crashed, "fuel": ship.fuel,
            "cargo": [dict(item, direction=list(item["direction"])) for item in ship.cargo]}


def save_game(path=None):
    """The world's changes written to `path` (SAVE_PATH when None) as JSON
    (see the module's docstring); returns what was written."""
    path = path or SAVE_PATH
    bubble, ship, player = foot.bubble, state["ship"], foot.player
    parked = state["parked"]
    data = {
        "version": SAVE_VERSION,
        "time": world.time,
        "terrain": {name: list(value) for name, value in planet.terrain.items()},
        "mode": state["mode"],
        "position": list(state["position"]),
        "forward": list(state["forward"]),
        "ship": ship_record(ship),
        "controls": {"throttle": state["controls"].throttle, "assist": state["controls"].assist},
        "parked": [parked[0].name, list(parked[1])] if parked is not None else None,
        "player": None if player is None else {"on": bubble.body.name,
                                               "feet": list(bubble.body_relative(player.feet())),
                                               "heading": list(player.basis()[2])},
        "taken": [list(key) for key in sorted(world.taken_rocks.keys)],
        "props": props.records(bubble),
        "contacts": scanner.records(),
        "market": state["market"].record(state["outposts"]),
        "upgrades": sorted(state["owned"]),
        "day": {"cycle": state["day_cycle"], "offset": day_time() - world.time},
    }
    with open(path + ".tmp", "w", encoding="utf-8") as f:
        json.dump(data, f)
    os.replace(path + ".tmp", path)
    return data


def restore_market(record):
    """The credits and the outposts' supplies from a save's record; the
    supplies of outposts not found yet, or about to be looked for again
    on a planet built again, wait in supply_pending."""
    if record is None:
        state["market"] = Market()
        return
    rebuilt = state["outpost_regenerations"] != world.regenerations
    found = {o.name for o in state["outposts"] if not (rebuilt and o.body is planet)}
    state["market"].restore(record, [o for o in state["outposts"] if o.name in found])
    state["supply_pending"] = {name: {body: tuple(value) for body, value in supplies.items()}
                               for name, supplies in record["supply"].items() if name not in found}


def day_time():
    """The day's time (clock.py) now: the world's clock plus the day's
    offset, which is set for PLAY_HOUR the first time it is asked for."""
    if state["day_offset"] is None:
        state["day_offset"] = clock.time_of(PLAY_HOUR, world.time) - world.time
    return world.time + state["day_offset"]


def set_hour(hour):
    """The day's offset set so the site's hour is `hour` now."""
    state["day_offset"] = clock.time_of(hour, day_time()) - world.time


def time_rows():
    """The cards' row of the site's hour, with the day cycle on."""
    if not state["day_cycle"]:
        return []
    hour = clock.hour(day_time())
    return [("TIME AT BASE", "%02d:%02d" % (int(hour) % 24, int(hour * 60.0) % 60))]


def update_day():
    """With the day cycle on, the sun where clock.py has it at the day's
    time (seen from the site, world.sun_elevation and sun_azimuth) and the
    exposure scaled for the sun's elevation seen from the camera
    (clock.exposure_scale); with it off, the exposure unscaled."""
    if not state["day_cycle"]:
        if world.exposure_scale != 1.0:
            world.exposure_scale = 1.0
            world.upload_sky()
        return
    _, up, east, north = world.site_frame()
    sun = clock.sun_direction(day_time(), up, east, north)
    world.sun_elevation, world.sun_azimuth = clock.site_angles(sun, up, east, north)
    body = world.body_under(state["position"])
    seen = math.degrees(math.asin(max(-1.0, min(1.0, dot(sun, normalize(body.relative(state["position"])))))))
    world.exposure_scale = clock.exposure_scale(seen)
    world.upload_sky()


def autosave_on_landing(landed):
    """With --play, the game saved to SAVE_PATH when the ship, having flown
    since the last such save, is landed on an outpost's pad."""
    if not landed:
        state["airborne"] = True
    elif state["airborne"] and outpost_here() is not None:
        state["airborne"] = False
        if args.play:
            save_game()
            state["trade_note"] = "game saved"
            print("[planet] saved on landing", SAVE_PATH, flush=True)


def migrate_save(data):
    """A save's data of version 1 made version 2, in place: each cargo item
    and each ore rock's record (grade above 0) without a resource is
    ferrite, any other prop's record resource 0, and no upgrades are
    owned."""
    ship = data.get("ship")
    for item in (ship or {}).get("cargo", []):
        item.setdefault("resource", resources.FERRITE)
    for record in data.get("props", []):
        record.setdefault("resource", resources.FERRITE if record.get("grade", 0.0) > 0.0 else resources.NONE)
    data.setdefault("upgrades", [])
    data["version"] = 2


def load_game(path=None):
    """The world put back as `path` (SAVE_PATH when None) has it: the
    planet built again with its terrain's parameters when they differ, the
    clock, the taken rocks, the props (out of the physics), the ship, and
    the free camera, the ship's view or a player where they were; a save
    of version 1 migrated first (migrate_save). Returns the data read, or
    None when there is no save to read."""
    path = path or SAVE_PATH
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError) as error:
        print("[planet] no save loaded from %s: %s" % (path, error), flush=True)
        return None
    if data.get("version") == 1:
        migrate_save(data)
        print("[planet] %s migrated from version 1" % path, flush=True)
    if data.get("version") != SAVE_VERSION:
        print("[planet] %s is not a save of version %d" % (path, SAVE_VERSION), flush=True)
        return None
    remove_player()
    drop_bubble()
    props.clear(None)
    state.update(tracked=None, last_thrown=None, teleport_when_known=None, moon_landing=None, warp=0,
                 accumulator=0.0, fuel_call=None)
    world.time = data["time"]
    day = data.get("day")
    if day is not None:
        state["day_cycle"], state["day_offset"] = day["cycle"], day["offset"]
    if data["terrain"] != planet.terrain:
        planet.terrain = {name: list(value) for name, value in data["terrain"].items()}
        world.regenerate()
        state["terrain_edit"].reset()
    state["regenerations"] = world.regenerations
    world.ground = None
    world.patch = None
    taken = world.taken_rocks
    taken.clear()
    for key in data["taken"]:
        taken.add(tuple(key))
    by_name = {body.name: body for body in bodies}
    scanner.restore(data.get("contacts", []), by_name)
    for record in data["props"]:
        if record["kind"] == "crate":
            props.restore(record, by_name)
        else:
            scatter_rocks.restore(physics, record, by_name)
    state["ship"] = None
    apply_upgrades(k for k in data.get("upgrades", []) if k in upgrades.BY_KEY)
    s = data["ship"]
    state["ship"] = None if s is None else flight.Ship(
        position=tuple(s["position"]), velocity=tuple(s["velocity"]), orientation=tuple(s["orientation"]),
        angular_velocity=tuple(s["angular_velocity"]), on_ground=s["on_ground"], resting=s["resting"],
        crashed=s["crashed"], fuel=s.get("fuel"), params=state["params"]["ship"],
        cargo=[dict(item, direction=tuple(item["direction"])) for item in s.get("cargo", [])])
    restore_market(data.get("market"))
    state["controls"] = flight.Controls(throttle=data["controls"]["throttle"], assist=data["controls"]["assist"])
    parked = data["parked"]
    state["parked"] = (by_name[parked[0]], tuple(parked[1])) if parked is not None else None
    mode = data["mode"]
    p = data["player"]
    if p is not None:
        body = by_name[p["on"]]
        feet = add(body.centre(), tuple(p["feet"]))
        spawn_player(ensure_bubble(feet), feet, tuple(p["heading"]))
    if (mode == "foot" and p is None) or (mode == "ship" and state["ship"] is None):
        mode = "camera"
    state["mode"] = mode
    if mode != "foot":
        state["position"], state["forward"] = tuple(data["position"]), tuple(data["forward"])
    return data


# ── Per frame ──────────────────────────────────────────────────────────────

def frame(dt):
    started = world.begin_frame(viewpoint())
    update_outposts()
    start_play()
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
        landed = ship_landed(state["ship"])
        if state["was_landed"] and not landed:
            load_cargo()
        state["was_landed"] = landed
        autosave_on_landing(landed)
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
    update_fuel_call()
    update_day()
    if ship is not None and ship is state["ship"]:
        state["ship_poses"] = (before, (ship.position, ship.orientation))
    else:
        state["ship_poses"] = None
    forget_planet_props()
    # The flying props on to this frame's clock before the bubble takes any back.
    props.fly(world.time - clock, world.probe_latency * state["frame_ms"] / 1000.0)
    step_bubble(dt)
    props.probe(foot.bubble, EXTRA_PROBES)
    scan(dt)
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
    foot.request_nearby(camera, bodies)
    world.end_frame(camera, (right, up, forward), dt, warp=WARPS[state["warp"]], viewpoint=viewpoint(),
                    patch=patch)
    world.set_orbit_path(orbit_path_points())
    ground = world.site_ground()
    for entity, offset in blocks:
        engine.scene.set_position(entity, sub(add(ground, offset), camera))
    pads.draw(camera)
    draw_net(camera)
    props.draw(foot.bubble, camera)
    place_viewmodel(dt, camera)
    foot.end_frame()
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
        **ship_readings(),
        **scan_readings(),
    })


def ship_readings():
    """The ship's fuel, the speed it can still gain (Ship.delta_v), and its
    cargo and what that would fetch at the nearest outpost, for the displays."""
    ship = state["ship"]
    outpost, value = cargo_value()
    return {
        "fuel": ship.fuel,
        "fuel_capacity": ship.params.fuel_capacity,
        "delta_v": ship.delta_v(),
        "cargo": "%s, %.1f m3" % (mass_text(ship.cargo_mass), ship.cargo_volume) if ship.cargo else "empty",
        "cargo_value": (outpost.name if outpost is not None else "-", credits_text(value)),
    }


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
        return "CRASHED (8 for a new ship)" if args.play else "CRASHED (8 or 9 for a new ship)"
    return "LANDED" if ship.on_ground else ""


def scan(dt):
    """The scanner (scanner.py) on while the right display shows SCAN in a ship that has not crashed."""
    ship = state["ship"]
    if state["mode"] != "ship" or state["page"] != "scan" or ship is None or ship.crashed:
        scanner.update(dt, None, None, None, None, active=False)
        return
    body = world.body_under(ship.position)
    scanner.update(dt, ship.position, ship.velocity, body, foot.gm(body))


def scan_readings():
    """The SCAN page's readings for Instruments.update: the page, its rows
    (the richness ahead, the contacts on the body below, the nearest and
    the points scanned), its strip, and below ORE_HUD_HEIGHT above the
    ground the nearest contact for the HUD (its direction from the view,
    its richness and distance)."""
    view = viewpoint()
    body = world.body_under(view)
    nearest = scanner.nearest(body, view)
    strip = list(scanner.strip)
    on_body = sum(1 for c in scanner.contacts if c.body is body)
    ore_contact = None
    if nearest is not None and world.height_above_ground(view) < ORE_HUD_HEIGHT:
        contact, distance = nearest
        ore_contact = (normalize(sub(contact.point(), view)),
                       "ORE %.2f  %s" % (contact.richness, distance_text(distance).upper()))
    return {
        "page": state["page"],
        "scan_rows": [
            ("ORE AHEAD", "%.2f" % strip[-1] if strip else "-"),
            ("CONTACTS", "%d" % on_body),
            ("NEAREST", "%s  %.2f" % (distance_text(nearest[1]), nearest[0].richness) if nearest else "-"),
            ("SCANNED", "%d" % scanner.scanned),
        ],
        "scan_strip": strip,
        "scan_mark": CONTACT,
        "ore_contact": ore_contact,
    }


def ore_rows():
    """The card's row of the ore richness under the view and its resource, once measured."""
    if world.ground_ore is None:
        return []
    if not world.ground_resource:
        return [("ORE", "%.2f" % world.ground_ore)]
    return [("ORE", "%.2f %s" % (world.ground_ore, resources.name(world.ground_resource).upper()))]


def update_cards():
    """The view's card (in the free camera and the chase view) and, when
    debug text is on, the debug card; neither while the controls sheet is
    open."""
    view_card, debug_card = state["cards"]
    trade_card = state["trade_card"]
    outpost = outpost_here()
    call = fuel_call_rows() if outpost is None else None
    if outpost is None:
        state["outpost_page"] = 0
    if outpost is not None and not state["help"].visible:
        page = state["outpost_page"]
        if page == 0:
            trade_card.update("OUTPOST %s, THE %s" % (outpost.name.upper(), outpost.body.name.upper()),
                              trade_rows(outpost))
        else:
            upgrade = upgrades.UPGRADES[page - 1]
            trade_card.update("%s: %s" % (outpost.name.upper(), upgrade.name.upper()), upgrade_rows(upgrade))
    elif call is not None and not state["help"].visible:
        trade_card.update(*call)
    trade_card.show((outpost is not None or call is not None) and not state["help"].visible)
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
        ] + time_rows() + [
            ("FUEL, DELTA-V", "%.0f kg, %.0f m/s" % (ship.fuel, ship.delta_v())),
            ("CARGO", mass_text(ship.cargo_mass) if ship.cargo else "empty"),
            ("ORBIT AROUND THE " + body.upper(),),
            ("PERIAPSIS", "below ground" if periapsis < 0.0 else distance_text(periapsis)),
            ("APOAPSIS", "escape" if apoapsis == math.inf else distance_text(apoapsis)),
        ]
        if contact_text(ship):
            rows.append(("STATE", contact_text(ship).split(" (")[0].lower()))
        view_card.update("CHASE VIEW", rows)
        view_card.show(True)
    elif state["mode"] == "foot" and foot.player is not None:
        player = foot.player
        on_planet = world.body_under(viewpoint()) is world.planet
        velocity = physics.character_velocity(player.character)
        stance = "held: no ground yet" if player.held else ("on the ground" if player.on_ground else "in the air")
        view_card.update("ON FOOT", [
            ("ABOVE " + world.datum_name(viewpoint()).upper(), distance_text(world.altitude(viewpoint()))),
        ] + time_rows() + (biomes.card_rows(world.ground_climate) if on_planet else []) + ore_rows() + [
            ("SPEED", "%.1f m/s" % length(velocity)),
            ("JETPACK", "%.0f %%" % (player.jet_fuel * 100.0)),
            ("STATE", stance),
            ("GUN", gun_text()),
            ("CREDITS", credits_text(state["market"].credits)),
        ] + (tracked_rows() if state["tracked"] in props.all else [])
          + ([("SHIP", "F to get in")] if near_boarding_point() else []) + net_rows())
        view_card.show(True)
    elif state["mode"] != "ship":
        on_planet = world.body_under(viewpoint()) is world.planet
        view_card.update("FREE CAMERA", [
            ("ABOVE GROUND", distance_text(world.height_above_ground(viewpoint()))),
            ("ABOVE " + world.datum_name(viewpoint()).upper(), distance_text(world.altitude(viewpoint()))),
        ] + time_rows() + (biomes.card_rows(world.ground_climate) if on_planet else []) + ore_rows() + [
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
        ("TERRAIN MESHES", "%d built" % (foot.fine.builds if foot.fine else 0)),
    ])


def gun_text():
    """The gravity gun's status for the card: what it holds or points at, and its mass."""
    gun = foot.gun
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
        holding = foot.gun is not None and foot.gun.held is not None
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
                        ] + ([] if args.play else [(("0",), "Drop a crate")]) + rest
    if state["mode"] != "ship":
        if pad:
            return [(("pad:Y",), "On foot"), (("pad:Start",), "Settings"), (("pad:Back",), "Controls")]
        places = [] if args.play else [(("1-9",), "Places"), (("0",), "Drop a crate")]
        return [(("RMB",), "Look"), (("W", "A", "S", "D"), "Fly"), (("E", "Q"), "Up, down"),
                (("Shift", "Ctrl"), "Faster, slower"), (("Wheel",), "Speed")] + places + [
                (("Tab",), "On foot"), (("F1",), "Settings"), (("F2",), "Controls")]
    view = "Cockpit" if state["chase"] else "Chase view"
    hints = []
    if state["ship"].crashed:
        hints = ([(("8",), "New ship at Base")] if args.play
                 else [(("8",), "New ship on the pad"), (("9",), "New ship in orbit")])
    elif ship_landed(state["ship"]):
        hints = [(("pad:X",) if pad else ("F",), "Get out")]
        if outpost_here() is None and fuel_call_rows() is not None and state["fuel_call"] is None:
            hints.append((("pad:D-pad down",) if pad else ("Space",), "Call for fuel"))
        if outpost_here() is not None:
            # On a pad: trading, and the controls to take off with.
            if state["outpost_page"] > 0:
                hints += [(("pad:D-pad up",) if pad else ("Enter",), "Buy")]
            else:
                hints += [(("pad:D-pad up",) if pad else ("Enter",), "Sell the cargo"),
                          (("pad:D-pad down",) if pad else ("Space",), "Fill the tanks")]
            hints += [(("pad:D-pad left", "pad:D-pad right") if pad else ("PgUp", "PgDn"), "Upgrades")]
            if pad:
                return hints + [(("pad:LT", "pad:RT"), "Throttle"), (("pad:RS",), "Yaw, up, down"),
                                (("pad:A",), "Assist"), (("pad:Y",), "Free camera"), (("pad:Start",), "Settings"),
                                (("pad:Back",), "Controls")]
            return hints + [(("W", "S"), "Throttle"), (("E", "Q"), "Up, down"), (("L",), "Assist"),
                            (("R",), view), (("Tab",), "Free camera"), (("F1",), "Settings"), (("F2",), "Controls")]
    if pad:
        return hints + [(("pad:LS",), "Pitch, roll"), (("pad:RS",), "Yaw, up, down"),
                        (("pad:LT", "pad:RT"), "Throttle"), (("pad:A",), "Assist"), (("pad:B",), "Cut throttle"),
                        (("pad:X",), view), (("pad:LB", "pad:RB"), "Warp"), (("pad:Y",), "Free camera"),
                        (("pad:Start",), "Settings"), (("pad:Back",), "Controls")]
    page = "Orbit page" if state["page"] == "scan" else "Scan page"
    return hints + [(("W", "S"), "Throttle"), (("Arrows",), "Pitch, yaw"), (("A", "D"), "Roll"),
                    (("E", "Q"), "Up, down"), (("L",), "Assist"), (("R",), view), (("M",), page),
                    ((",", "."), "Warp"), (("Tab",), "Free camera"), (("F1",), "Settings"), (("F2",), "Controls")]


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
        (("M",), "Right display: ORBIT / SCAN"),
        ((",", "."), "Time warp down / up"),
        (("F",), "Out, once landed"),
        (("Enter", "Space"), "At an outpost: sell the cargo / fill the tanks"),
        (("Space",), "Elsewhere or in orbit: call for fuel"),
        (("PgUp", "PgDn"), "At an outpost: upgrades (Enter buys)"),
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
        (("F5", "F9"), "Save / load"),
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
    state["home"] = home_outpost(world)
    state["outposts"] = [state["home"]]
    state["outpost_regenerations"] = world.regenerations
    begin_survey(bodies)
    build_net()
    build_cards()
    build_ship_model()
    build_cockpit()
    state["viewmodel"] = Viewmodel(engine, box())
    state["generated_cockpit"] = args.generated_cockpit
    state["cabin_screens"] = cockpit_screens()
    state["instruments"] = Instruments(engine, len(state["cabin_screens"]), FOV)
    state["terrain_edit"] = planet_settings.TerrainEdit(world)
    state["menu"] = SettingsMenu(engine, "Settings", menu_items())
    state["help"] = HelpOverlay(engine, "CONTROLS", help_columns(), footer="F2, or Back on a gamepad, to close")
    teleport(1)
    state["play_start"] = args.play

    # Before the transform system (priority 0), so this frame's positions are used.
    engine.ecs.add_system("PlanetFrame", frame, priority=-1)
    import checks
    if args.capture:
        engine.ecs.add_system("PlanetCapture", checks.CaptureTour(args.capture, args.shots), priority=-2)
    elif args.verify:
        engine.ecs.add_system("PlanetVerify", checks.Verify(), priority=-2)
    elif args.orbit_check:
        engine.ecs.add_system("PlanetOrbitCheck", checks.OrbitCheck(), priority=-2)
    elif args.morph_check:
        engine.ecs.add_system("PlanetMorphCheck", checks.MorphCheck(), priority=-2)
    elif args.crack_check:
        engine.ecs.add_system("PlanetCrackCheck", checks.CrackCheck(), priority=-2)
    elif args.landing_check:
        engine.ecs.add_system("PlanetLandingCheck", checks.LandingCheck("planet"), priority=-2)
    elif args.moon_landing_check:
        engine.ecs.add_system("PlanetLandingCheck", checks.LandingCheck("moon"), priority=-2)
    elif args.walk_check:
        engine.ecs.add_system("PlanetWalkCheck", checks.WalkCheck(), priority=-2)
    elif args.prop_rest_check:
        engine.ecs.add_system("PlanetPropRestCheck", checks.PropRestCheck(), priority=-2)
    elif args.hold_check:
        engine.ecs.add_system("PlanetHoldCheck", checks.HoldCheck(), priority=-2)
    elif args.punt_check:
        engine.ecs.add_system("PlanetPuntCheck", checks.PuntCheck(), priority=-2)
    elif args.trunk_check:
        engine.ecs.add_system("PlanetTrunkCheck", checks.TrunkCheck(), priority=-2)
    elif args.promotion_check:
        engine.ecs.add_system("PlanetPromotionCheck", checks.PromotionCheck(), priority=-2)
    elif args.orbit_crate_check:
        engine.ecs.add_system("PlanetOrbitCrateCheck", checks.OrbitCrateCheck(), priority=-2)
    elif args.save_check:
        engine.ecs.add_system("PlanetSaveCheck", checks.SaveCheck(), priority=-2)
    elif args.scan_check:
        engine.ecs.add_system("PlanetScanCheck", checks.ScanCheck(), priority=-2)
    elif args.break_check:
        engine.ecs.add_system("PlanetBreakCheck", checks.BreakCheck(), priority=-2)
    elif args.fuel_check:
        engine.ecs.add_system("PlanetFuelCheck", checks.FuelCheck(), priority=-2)
    elif args.cargo_check:
        engine.ecs.add_system("PlanetCargoCheck", checks.CargoCheck(), priority=-2)
    elif args.trade_check:
        engine.ecs.add_system("PlanetTradeCheck", checks.TradeCheck(), priority=-2)
    elif args.upgrade_check:
        engine.ecs.add_system("PlanetUpgradeCheck", checks.UpgradeCheck(), priority=-2)
    elif args.day_check:
        engine.ecs.add_system("PlanetDayCheck", checks.DayCheck(), priority=-2)
    print(__doc__.split("Keys:")[1].rstrip(), flush=True)


# Keys that change settings in the menu too, shown beside them.
MENU_KEYS = {"Exposure": ("B", "V"), "Atmosphere density": ("T", "Y"), "Sun elevation": ("Z", "X"),
             "Ocean": ("O",), "Wireframe": ("F3",), "Split factor": ("[", "]"),
             "Colour: terrain / face / level / biome": ("C",), "New seed": ("N",)}


def menu_items():
    """The settings menu (toolkit.overlay.SettingsMenu): the simulation's and the display's own
    settings, and planet.settings' sky, clouds, ground, view and planet,
    the planet's kept in its TerrainEdit until Apply builds it again; in
    play mode without the planet's, and without the keys it ignores shown."""
    keys_shown = {name: caps for name, caps in MENU_KEYS.items()
                  if not (args.play and name in ("Exposure", "Atmosphere density", "Sun elevation", "New seed"))}
    def set_assist(on):
        state["controls"].assist = on

    return [
        {"heading": "Simulation"},
        {"slider": "Time warp", "min": 0, "max": len(WARPS) - 1, "get": lambda: state["warp"],
         "set": lambda v: state.update(warp=int(round(v))), "show": lambda v: "x%d" % WARPS[int(round(v))],
         "keys": (",", "."), "hint": "How much faster than real time the ship flies and the moon moves."},
        {"toggle": "Translation assist", "get": lambda: state["controls"].assist, "set": set_assist,
         "keys": ("L",), "hint": "The throttle asks for a speed and E / Q for a climb rate; the thrusters hold them."},
        {"toggle": "Head bob", "get": lambda: foot.head_bob, "set": lambda on: setattr(foot, "head_bob", on),
         "hint": "On foot, the view rises and falls a little with each step."},
        {"toggle": "Day cycle", "get": lambda: state["day_cycle"], "set": lambda on: state.update(day_cycle=on),
         "hint": "The sun goes round once every %d minutes of the world's clock; the sky's sun settings follow it."
                 % round(clock.DAY_LENGTH / 60.0)},
        {"slider": "Time of day", "min": 0.0, "max": 24.0, "get": lambda: clock.hour(day_time()), "set": set_hour,
         "show": lambda v: "%02d:%02d" % (int(v) % 24, int(v * 60.0) % 60),
         "hint": "The site's hour, with the day cycle on (noon is 12:00).",
         **({} if args.play else {"keys": ("Z", "X")})},
    ] + planet_settings.sky_items(world, keys_shown, site="the blocks") + planet_settings.cloud_items(world, keys_shown) \
        + planet_settings.ground_items(world, keys_shown) + planet_settings.climate_items(world, keys_shown) + [
        {"heading": "Display"},
        {"toggle": "Button hints", "get": lambda: state["hints"], "set": lambda on: state.update(hints=on),
         "hint": "The bar of keys or gamepad buttons along the bottom of the screen."},
        {"toggle": "Orbit path", "get": lambda: state["orbit_path"], "set": lambda on: state.update(orbit_path=on),
         "hint": "The path the ship would coast along, around the body it is reckoned around."},
        {"toggle": "Debug text", "get": lambda: state["debug_text"], "set": lambda on: state.update(debug_text=on),
         "keys": ("I",), "hint": "The debug card: frame time, chunks, the pool and the terrain's settings."},
    ] + planet_settings.view_items(world, keys_shown) \
        + ([] if args.play else planet_settings.planet_items(world, state["terrain_edit"], new_seed, keys_shown))


# ── Play mode (--play) ─────────────────────────────────────────────────────

# The keys that change the world, which play mode ignores (but M in the ship, T on foot).
WORLD_KEYS = (keys.N, keys.M, keys.J, keys.H, keys.G, keys.Z, keys.X, keys.T, keys.Y, keys.B, keys.V)


def start_play():
    """--play: once the terrain is built, a ship at rest at Base (preset 8)
    and, once it is landed, the player out of it beside its cockpit."""
    if not state["play_start"] or planet.tree is None:
        return
    if state["ship"] is None:
        teleport(8)
    elif ship_landed(state["ship"]):
        get_out()
        state["play_start"] = False


def play_blocks(key, shift):
    """Whether play mode ignores `key`: the places and crates (0 - 9; but 8,
    a new ship at Base, once the ship has crashed) and WORLD_KEYS."""
    if keys.NUM_0 <= key <= keys.NUM_9:
        ship = state["ship"]
        return not (key == keys.NUM_8 and not shift and ship is not None and ship.crashed)
    if key == keys.M:
        return state["mode"] != "ship"
    if key == keys.T:
        return state["mode"] != "foot"
    return key in WORLD_KEYS


def help_columns():
    """HELP_COLUMNS, in play mode without the places, the crates and the
    keys that change the world."""
    if not args.play:
        return HELP_COLUMNS
    columns = []
    for column in HELP_COLUMNS:
        kept = []
        for heading, rows in column:
            if heading in ("Places", "Physics"):
                continue
            if heading == "World":
                rows = [(("8",), "New ship at Base, after a crash"), (("F5", "F9"), "Save / load")]
            kept.append((heading, rows))
        columns.append(kept)
    return columns


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
    if args.play and play_blocks(key, shift):
        return
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
        foot.controls.jump = True
    elif key == keys.SPACE and state["mode"] == "ship":
        if outpost_here() is not None:
            buy_fuel()
        else:
            call_fuel()
    elif key in (keys.ENTER, keys.KP_ENTER) and state["mode"] == "ship":
        if state["outpost_page"] > 0 and outpost_here() is not None:
            buy_upgrade(upgrades.UPGRADES[state["outpost_page"] - 1])
        else:
            sell_cargo()
    elif key in (keys.PAGE_UP, keys.PAGE_DOWN) and state["mode"] == "ship":
        turn_page(1 if key == keys.PAGE_DOWN else -1)
    elif key == keys.R and state["mode"] != "foot":
        state["chase"] = not state["chase"]
    elif key == keys.U:
        state["generated_cockpit"] = not state["generated_cockpit"]
    elif key == keys.L:
        state["controls"].assist = not state["controls"].assist
    elif key == keys.BACKSPACE:
        state["controls"].throttle = 0.0
    elif key in (keys.COMMA, keys.PERIOD) and state["mode"] == "foot":
        foot.gun_controls.distance += 1.0 if key == keys.PERIOD else -1.0
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
    elif key == keys.M and state["mode"] == "ship":
        state["page"] = "scan" if state["page"] == "orbit" else "orbit"
    elif key == keys.M:
        change_terrain("mountains", 1, 1.25)
    elif key == keys.J:
        change_terrain("mountains", 1, 1 / 1.25)
    elif key == keys.H:
        change_terrain("hills", 1, 1.25)
    elif key == keys.G:
        change_terrain("hills", 1, 1 / 1.25)
    elif key in (keys.Z, keys.X) and state["day_cycle"]:
        set_hour((clock.hour(day_time()) + (1.0 if key == keys.X else -1.0)) % 24.0)
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
    elif key == keys.F5:
        save_game()
        print("[planet] saved", SAVE_PATH, flush=True)
    elif key == keys.F9:
        if load_game() is not None:
            print("[planet] loaded", SAVE_PATH, flush=True)
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
        foot.gun_controls.distance += y_offset
        return
    state["speed_factor"] = max(0.01, min(100.0, state["speed_factor"] * 1.25 ** y_offset))


engine.set_on_init(on_init)
engine.set_on_key_pressed(on_key)
engine.input.set_on_mouse_scroll(on_scroll)
engine.run()

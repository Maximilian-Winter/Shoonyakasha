"""
Walker: the planet on foot, as a game. The player starts beside the parked
ship at Base, on the planet of the planet example (planet.py), and walks,
runs, jumps and jets with the gravity gun in hand
(shoonyakasha.planet.on_foot). The ship, a hover car, floats
HOVER_HEIGHT above the ground 16 m south of the blocks as a landmark, a
static box in the physics. The outposts on the planet and the moon are
found by the same survey as in planet.py and drawn as pads. Lakes on the
planet within 40 km of the site (shoonyakasha.planet.lakes) are surveyed
with them and kept clear of the outposts' sites and the site's pad; then
the rivers are traced over a height grid around the site
(shoonyakasha.planet.rivers), round those sites and out of the lakes,
before the terrain is built. The car's ground patch takes the lakes' and
the rivers' water as a floor, as it does the sea's.

The flying car: the ship is a hover car (hovercar.py, carview.py). F
within ENTER_REACH of it gets in; the camera then chases it, and W S A
D drive and steer it, Space and left Ctrl move the height it keeps above
the ground (from skimming to hovercar.MAX_HEIGHT), left Shift boosts and
the mouse looks around it. F gets out at its left side once it is slow
and low (hovercar.Car.can_leave); left, it settles on its cushion. Its
ground is a patch of terrain heights measured around it each frame
(shoonyakasha.planet.ground_patch), ahead along its velocity; in the
physics bubble it is a kinematic box that pushes the props, and a ray
ahead of it bumps it off trunks and fixed rocks (bump_car). The bubble
follows it while it is driven.

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
the objectives and the game's numbers, the waypoint, the car (whether it
is driven, its fuel and its cargo), the upgrades owned, the day's offset,
the suit's reserve and health, the explored map, the meteorites' craters,
the next shower's start and which rocks are hot. F9 or Load game puts it back; the player comes back LOAD_LIFT above
the ground where it stood once that ground is measured. With autosave on,
the game is saved every AUTOSAVE_SECONDS of play, on getting out of the
car parked on an outpost's pad, on Quit and as the window closes. The
save is of save.VERSION; one of version 1 is migrated as it is read. When a save exists, the walker starts on a menu with
Continue (the save loaded) and New game.

The wind (PlanetWorld.wind_at): the car flies in it at its height above
the ground (`step_car`; on its cushion it does not move it), the props are
dragged through it, and the suit's chill takes its speed CHILL_HEIGHT
above the ground at the camera.

The car's fuel (hovercar.py's CarParams and fuel): the HUD shows its tank
(FUEL) while driving; dry, it settles onto its cushion and only steers,
and a toast says so. The menu's Car tab shows the fuel, where the car is
and the credits, and fills the tank at the outpost whose pad the car is
on (`fill_tank`, trade.Market.buy_fuel) or calls fuel to it off the
pads, on its cushion, from the nearest outpost on the planet
(`call_fuel`, trade.fuel_call), put in the tank when it is due if the car
is still within CALL_REACH (`update_fuel_call`); the save keeps the fuel.

The car's hold (hovercar.Car.cargo): getting in loads the loose ore
rocks lying in the cargo net behind the parked car (CAR_NET_CENTRE,
CAR_NET_HALF, shown by four posts; `in_net`, `load_cargo`), and the
contracts' crates, each that fits (hovercar.Car.can_take), a rock with
where it was first seen; the rest stay in the net. The menu's Outpost
tab shows the outpost whose pad the car is on, its price for each
resource at grade 1, the car's cargo and what its ore would fetch, and
sells the ore (`sell_car_cargo`, trade.sale). The save keeps the cargo.

Upgrades (upgrades.py, the "car" and "gun" groups): the menu's Upgrades
tab has a button for each, bought with the car or the player's feet on
an outpost's pad (`at_outpost`, `buy_upgrade`), and a line of its cost or
why it cannot be bought and what it changes, from now to after. The
upgrades owned give the effective car and gun parameters
(`apply_upgrades`), which the car and the gun take at once, as do every
car made and every gun spawned after (OnFoot.gun_params); the save keeps
them.

Points of interest (pois.py), on the planet: `update_pois` looks for the
candidates within POI_REACH (again once the player has moved
POI_WANT_MOVE) and `probe_pois` measures them with the extra probes (the
explored map yields while any wait), which gives each its kind or none.
A found one is discovered within SCAN_RANGE, or within SEEN_RANGE in the
view's cone, shown on the compass and the map by its name (greyed once
looted); one not discovered within POI_REACH as a signal. Within
DRAW_RANGE it is drawn from boxes (pois.LAYOUTS) in a clearing of the
plants and rocks (pois.CLEAR_RADIUS); within PROP_RANGE a pod's crate and
a geode field's rocks are made. E (held for a wreck's spot) uses the spot
in reach (`usable_spot`, `use_spot`): a beacon reveals the map around it
(ExploredMap.reveal) and gives survey data, a wreck's spots and a rig's
crates give parts (a rig's ore rocks too), an arch gives survey data. A
pod's crate settled on an outpost's pad sells (`sell_pods`). Each map
cell walked (not revealed) is CELL_SURVEY of survey data; the Outpost
tab sells it (`sell_survey`, trade.survey_sale). Parts pay for upgrades
with the credits (the Upgrades tab offers WALKER_UPGRADES: the car's,
the gun's and the suit's). The save keeps the points' states, the
parts, the survey data and which crates are pods. Loose ore rocks and
pods sell only once settled (`settled`).

Contracts (contracts.py): the menu's Contracts tab shows the offers of
the outpost whose pad the car or the player is on (contracts.offers for
the day, which turns at dawn, clock.day), each with Accept, and the
active contracts (contracts.MAX_ACTIVE at most), each with Pin, and
Abandon for the pinned one. The pinned contract takes the objective's
place on the card and marks its target on the compass and the map
(`contract_target`). A delivery takes the ore of its resource and grade
that comes to rest on its outpost's pad, or that the car's cargo brings
there (`deliver`), in place of selling it. A courier's crate goes into
the car's hold when the car is on the pad, else beside the player; it is
struck by how much its velocity changes as its speed falls
(`watch_couriers`), and in the hold by the car's bumps (`strike_hold`),
and breaks at contracts.BREAK_DAMAGE, failing the contract. A recovery's
pod is a point of interest of the pod's kind at its target (`place_pod`).
A courier's crate or a pod is delivered settled on its destination's
pad, or in the hold with the car slow on that pad; either goes into the
hold through the net. A survey completes with contracts.SURVEY_SHARE of
the map's cells around its target measured (ExploredMap.share). Done, a
contract pays its reward and raises its outpost's reputation by one;
failed, past its deadline or abandoned, it lowers it by one (no lower
than MIN_REPUTATION). The reputation raises the outpost's offers' rewards
(contracts.REWARD_PER_REPUTATION) and its prices (trade.REPUTATION_PRICE).
The save keeps the active contracts, today's offers accepted, the pin,
the reputations and which crates are couriers'.

The suit (suit.py): `here_temperature` is the air's temperature where the
camera is (on the planet the climate under it, with its height, swinging
with the local hour and chilled by the wind; on the moon by the sun or
the shade). Each frame `update_suit` steps the suit's reserve and the
player's health, sheltered in the car or on an outpost's pad (where the
health also recovers). Low, the screen is tinted frost-blue or
heat-orange and the walk slowed (`suit_effects`, `slow_down`); the card
shows SUIT and TEMP. With the health gone the player comes to on the
pad of the nearest outpost, charged RECOVERY_FEE, what the gun held
dropped where it fell (`wake_at_outpost`). The save keeps the suit.

The day (clock.py): the sun goes round with the world's clock plus the
day's offset (`day_time`; START_HOUR in a new game, kept in the save),
the exposure scaled for its elevation seen from the camera
(clock.set_sun); the readout shows the hour at Base. With the sun below
lamps.LIGHTS_ON_ELEVATION the lights come on (`update_lights`): over each
outpost's pad, the car's headlights (CAR_HEADLIGHTS, CAR_HEADLIGHT_DIP
down) and on foot the suit lamp.

Meteor showers (events.py, `update_showers`): the next shower is planned
around the camera on the planet, its impacts' ground measured with the
extra probes (`probe_shower`) and, while its large impact falls in the sea,
a lake or a river, planned again. WARNING_SECONDS before it starts, a toast
gives its bearing and distance, and the compass marks its centre. Each
meteor flies its path in, drawn as glowing quads (`draw_showers`,
viewmodel.Glows): a head at least HEAD_PIXELS across and a trail of its
last TRAIL_SECONDS. As each strikes (`strike`), there is a flash over the
screen (the HUD's flash), within SHAKE_REACH the view shakes (`shaken`),
a plume of dust rises and fades, and the props in the bubble near it are
pushed away (`push`). The large impact digs a crater
(PlanetWorld.set_craters; events.keep keeps events.MAX_CRATERS) and
leaves a meteorite, an ore boulder of iridium marked on the compass while
it lies whole; each small one leaves a rock of iridium (`meteor_rock`).
They glow while hot (events.heat), and while hot the gun will not grab
them (TOO HOT, `too_hot`); a hot meteorite's chunks are as hot
(`split_hot`). The save keeps the craters ("modifiers"), the next
shower's start ("events") and which props are hot.

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
  F                      into the car near it; out of it when slow and low
  F5, F9                 save, load
  F1                     the menu on the controls

Gamepad: left stick walk, right stick look, A jump and jet, left trigger
grab, right trigger punt, the bumpers the hold distance.

    python walker.py
    python walker.py --check [--shot FILE]   walk, grab and throw a crate, set a waypoint, run the suit
                                             out in the cold, sell ore,
                                             load the car's net and sell its cargo, buy upgrades,
                                             use a point of interest of each kind, sell survey data,
                                             take and end a contract of each kind, bring down a meteor shower,
                                             pause, save, change the game and load it, drive and fly
                                             the car, hover it on a lake and a river, run it dry and
                                             call fuel, then night; report and exit
"""

import argparse
import math
import os
import random
import tempfile
import time
import zlib

import numpy as np

import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.planet import world as planet_world
from shoonyakasha.planet.bubble import Bubble
from shoonyakasha.planet.gravity_gun import GunParams
from shoonyakasha.planet.ground_patch import GroundPatch, SIZE as PATCH_SIZE
from shoonyakasha.planet.on_foot import JUMP_DISTANCE, STEP, OnFoot
from shoonyakasha.planet.vec import (add, cross, dot, euler_from_basis, length, local_frame, normalize, quat_from_basis,
                                     quat_normalize, rotate, scale, sub)
from shoonyakasha.planet import resources, rivers
from shoonyakasha.planet.lakes import LakeSurvey
from shoonyakasha.planet.world import EXTRA_PROBES, FOV, MAX_LEVEL, SITE_BLEND_RADIUS
from shoonyakasha.planet.props import stack_offsets
from shoonyakasha.planet.rocks import density as rock_density, rock_colour
import carview
import clock
import config
import contracts
import events
import hovercar
import lamps
import pois
import save
import suit
import outposts
import trade
import upgrades
from outposts import Pads, Survey, home_outpost
from rocks import Rocks
from trade import Market, sale
from explored_map import ExploredMap, Grid, MapView
import hud as hud_module
from hud import Hud, bearing, distance_text
from objectives import REST_SPEED, Objectives, progress_text, sale_items
from terminal import Terminal
from viewmodel import Glows, Viewmodel

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
EAST_OF_SHIP = 4.5               # m east of the ship's centre the player starts
SHIP_SOUTH = 16.0                # m south of the blocks the ship stands
# The ship is a hover car (models/cyber_vehicle, "Cyber Vehicle" by Alt, CC-BY-4.0), its model in
# metres with its nose along +Z and up +Y: its centre, and its half sizes (right, up, back) about it.
SHIP_MODEL = os.path.join(HERE, "models", "cyber_vehicle", "scene.gltf")
SHIP_MODEL_CENTRE = (0.0, 0.586, -1.402)
SHIP_HALF = (1.452, 0.756, 3.097)
HOVER_BOB, HOVER_PERIOD = 0.04, 3.2   # m up and down, s, while slow
SHIP_CLEARANCE = SHIP_HALF[1] + hovercar.HOVER_MIN   # m from the ship's centre down to the ground, parked
ENTER_REACH = 4.5                # m from the car's centre to the feet it can be got into from
EXIT_OUT = 1.0                   # m beyond the car's left side the player gets out
CAR_PATCH_MARGIN = 8.0           # m the car's ground patch reaches beyond its centre and its look-ahead
BUMP_REACH = 0.5                 # m beyond a step's travel the car looks for trunks and rocks
BUMP_BOUNCE, BUMP_KEEP = 0.3, 0.6
SHIP_HULL = [(x * SHIP_HALF[0], y * SHIP_HALF[1], z * SHIP_HALF[2]) for x in (-1, 1) for y in (-1, 1) for z in (-1, 1)]
CHECK_SECONDS = 10.0
SHIP_SHOT_WALK = 2.0             # s --shot walks away from the ship before its screenshot of it
DRIVE_SECONDS = 5.0              # s --check drives the car flat out, turning a little
CLIMB_SECONDS = 3.0              # s it climbs at full climb
LAND_SECONDS = 25.0              # s it waits at most for the car to come down and slow
LAKE_SECONDS = 6.0               # s --check hovers the car over a lake
LAKE_CHECK_DROP = 2.0            # m above the lake's water --check puts the car
LAKE_CLIMB_SECONDS = 8.0         # s --check --shot climbs the car over the lake
RIVER_CHECK_WIDTH = 20.0         # m wide the river --check hovers the car on is, at least
RIVER_CLIMB_SECONDS = 3.0        # s --check --shot climbs the car over the river
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
START_HOUR = 9.0                # the site's hour (clock.py) when a new game starts
CALL_REACH = 50.0               # m the car may move from where fuel was called to and still get it
# The cargo net behind the parked car: its centre from the car's (right, up, back; up at the ground),
# its half sizes along right and back, how far below and above the ground an ore rock's centre may be in
# it, and its corner posts.
CAR_NET_CENTRE = (0.0, -SHIP_CLEARANCE, 6.5)
CAR_NET_HALF = (2.0, 2.5)
CAR_NET_BELOW, CAR_NET_ABOVE = 1.0, 3.0
NET_POST = (0.12, 1.0, 0.12)    # m
NET_COLOUR = (1.0, 0.72, 0.3)
NET_SETTLE = 3.0                # s --check lets the ore it drops into the net settle
NET_CHECK_AWAY = 60.0           # m along its nose --check moves the car off Base's pad for the net
SUIT_CHECK_TEMPERATURE = -60.0  # degrees C --check stands in for the air's while the suit drains
SUIT_CHECK_RESERVE = 0.25       # the reserve --check leaves the suit with
SUIT_CHECK_HEALTH = 0.3         # and the health
SUIT_SECONDS = 30.0             # s --check waits at most for the suit to give out
POI_MEASURE_SECONDS = 20.0      # s --check waits at most for the candidates near Base to be measured
USE_CHECK_SECONDS = 2.0         # s past a spot's hold --check waits for it to be used
POD_CHECK_SECONDS = 10.0        # s --check waits at most for the pod to sell
UPGRADE_CREDITS = 10_000.0      # credits --check is given to buy UPGRADE_BUYS
UPGRADE_BUYS = ("tank_1", "hold_1", "gun_force_1")
CHECK_NET_EXTENTS = ((0.35, 0.25, 0.35), (0.45, 0.3, 0.45))   # m: the ore rocks --check drops into the net
DRY_SECONDS = 1.0               # s --check drives the dry car at full throttle
FUEL_WAIT = 2.0                 # s past the call's delay --check waits for the fuel at most
NIGHT_CHECK_HOUR = 0.5          # the hour --check sets at its end
NIGHT_SECONDS = 1.0             # s --check waits at night
CAR_HEADLIGHTS = ((-0.95, 0.0, -3.0), (0.95, 0.0, -3.0))   # m from the car's centre (right, up, back)
CAR_HEADLIGHT_DIP = 4.0         # degrees below its nose
RECOVERY_FEE = 250.0            # credits the outpost charges when the suit gave out (wake_at_outpost)
CHILL_HEIGHT = 10.0             # m above the ground the wind's chill is reckoned at
# Points of interest (pois.py), on the planet: candidates within POI_REACH measured (again once the player
# has moved POI_WANT_MOVE); discovered within SCAN_RANGE, or within SEEN_RANGE in the view's cone
# (SEEN_COS); undiscovered ones within POI_REACH shown as signals; drawn within DRAW_RANGE; a pod's and a
# geode field's props made within PROP_RANGE; their spots looked for within USE_LOOK.
POI_REACH = 3_000.0             # m
POI_WANT_MOVE = 500.0           # m
SCAN_RANGE, SEEN_RANGE, SEEN_COS = 600.0, 1_500.0, 0.85
DRAW_RANGE = 2_000.0            # m
PROP_RANGE = 120.0              # m
USE_LOOK = 60.0                 # m
CELL_SURVEY = 0.5               # credits of survey data per map cell explored
CONTRACT_MARKER = (1.0, 0.82, 0.3, 1.0)
SURVEY_EVERY = 1.0              # s between measurements of a survey contract's share explored
PICKED_UP = 20.0                # m a recovery's pod has moved from its point once picked up
MIN_REPUTATION = -5.0           # an outpost's reputation goes no lower
CONTRACT_CHECK_SECONDS = 30.0   # s --check waits at most for each contract to end
CONTRACT_CHECK_DROP = 12.0      # m/s down --check throws the fragile crate at
METEOR_MARKER = (1.0, 0.6, 0.35, 1.0)
SHOWER_TRIES = 4                # times a shower whose large impact falls in water is planned again
FALL_SPEED = 5.0                # m/s down a meteorite or rock flies from its impact until its ground is measured
FALL_LIFT = 2.0                 # m above the ground it starts at
METEOR_GLOW = (1.0, 0.92, 0.75)  # LDR, a large meteor's head
TRAIL_GLOW = (1.0, 0.62, 0.3)
HEAD_LIGHT, TRAIL_LIGHT = 3.0, 2.0   # times those: the sum is clamped at white, so the bright core widens
HEAD_SIZE = 25.0                # m across a large meteor's head
SMALL_HEAD = 0.5                # of that a small one's
HEAD_PIXELS = 24.0              # of the screen's height in pixels a head is across, at least
IGNITE_SECONDS = 1.5            # s from its entry a meteor brightens over
TRAIL_SECONDS = 2.0             # s of its path a trail shows
TRAIL_SEGMENTS = 8
TRAIL_WIDTH = 0.6               # of the head's size a trail is wide at the head
SMALL_LIGHT = 0.6               # a small meteor's light and an impact's flash and shake, of a large one's
FLASH_COLOUR = (1.0, 0.94, 0.82)
FLASH_ALPHA = 0.6
FLASH_REACH = 2_000.0           # m within which a large impact's flash is full
FLASH_SECONDS = 0.6
SHAKE_REACH = 3_000.0           # m within which an impact shakes the view
SHAKE_ANGLE = math.radians(1.5)  # at the impact, falling to none at SHAKE_REACH
SHAKE_SECONDS = 1.5
SHAKE_HZ = 11.0
PUSH_REACH = 3.0                # blast radii within which the props in the bubble are pushed
PUSH_SPEED = 6.0                # m/s, at the impact
SMALL_BLAST = 3.0               # m: a small impact's blast radius
PLUME_PUFFS, SMALL_PUFFS = 6, 2
PLUME_SECONDS = 20.0
PLUME_RISE = 6.0                # m/s
PLUME_COLOUR = (0.2, 0.17, 0.14)  # LDR, added to the image
PLUME_NEAR = 3.0                # puff sizes from the camera within which a puff fades out
HOT_GLOW = (1.0, 0.42, 0.12)
HOT_SIZE = 3.0                  # times a hot prop's radius its glow is across
METEOR_DISCS, METEOR_STREAKS = 96, 48
SHOWER_CHECK_LEAD = 12.0        # s from --check's shower being planned to its large impact
SHOWER_CHECK_CENTRE = 400.0     # m east of the feet its large impact strikes
SHOWER_CHECK_OFF_PAD = 4.0      # m beyond Base's pad's reach its small one strikes
SHOWER_CHECK_NEAR = 5.0         # m from its rock --check walks the player to
SHOWER_CHECK_MASS = 3_000.0     # kg, its meteorite
SHOWER_CHECK_ROCK = 40.0        # kg, its rock
SHOWER_LAND_SECONDS = 6.0       # s after its last impact --check lets its meteorite and rock land
POI_MARKER, LOOTED_MARKER, SIGNAL_MARKER = (1.0, 0.85, 0.35, 1.0), (0.55, 0.58, 0.62, 1.0), (0.6, 0.75, 0.85, 0.6)
WALKER_UPGRADES = upgrades.for_groups(("car", "gun", "suit"))   # the upgrades the outposts here offer

HELP_COLUMNS = [
    [("On foot", [
        (("Mouse",), "Look around"),
        (("W", "A", "S", "D"), "Walk"),
        (("Shift", "Ctrl"), "Run / crouch"),
        (("Space",), "Jump; held in the air: jetpack"),
        (("M",), "Map: wheel to zoom, left click a waypoint, right click to clear"),
        (("E",), "Use what is here: activate, salvage (hold), open, scan"),
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
        (("pad:A",), "Jump and jet; in the car, boost"),
        (("pad:LT", "pad:RT"), "Grab / punt; in the car, down / up"),
        (("pad:LB", "pad:RB"), "Hold distance"),
        (("pad:X",), "Into the car and out"),
    ])],
    [("Flying car", [
        (("F",), "Get in; get out when slow and low"),
        (("W", "S"), "Drive / brake and reverse"),
        (("A", "D"), "Steer"),
        (("Space", "Ctrl"), "Up / down"),
        (("Shift",), "Boost"),
        (("Mouse",), "Look around the car"),
    ])],
]

parser = argparse.ArgumentParser(description="The planet on foot")
parser.add_argument("--check", action="store_true",
                    help="walk 10 s north, grab a crate and throw it, set a waypoint on the map, run the suit out in the cold, "
                         "sell ore at Base, "
                         "load the car's net and sell its cargo, buy upgrades, use a point of interest of each kind "
                         "and sell survey data, take and end a contract of each kind, pause, save, change the game and load it, drive and fly the car, hover it on a lake and a river, "
                         "run it dry and call fuel, then night; report and exit")
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
    "survey": None,               # the outposts' and the lakes' surveys while the world measures them
    "ship": None,                 # (position, right, up, forward) of the parked ship, planet space
    "ship_model": None,           # (root entity, mesh entities)
    "ship_body": None,            # the car's kinematic box in the bubble
    "car": None,                  # the flying car (hovercar.Car), once the ship's pose is known
    "car_controls": hovercar.CarControls(),
    "car_poses": None,            # its ((position, rotation) before, after) this frame, planet space
    "car_glows": None,            # its engines' glow (carview.EngineGlows)
    "chase": carview.ChaseCamera(),
    "chase_basis": None,          # the chase camera's (right, up, forward) this frame
    "driving": False,
    "pad_x": False,               # the gamepad's X held last frame
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
    "day_offset": None,           # s added to the world's clock for the day (clock.py); None: START_HOUR at the start
    "fuel_call": None,            # the fuel on its way to the car (call_fuel), or None
    "car_note": "",               # the last fuel bought, called or delivered, for the Car tab
    "was_dry": False,             # the car's tank was empty last frame
    "net_posts": [],              # the car's cargo net's corner posts
    "trade_note": "",             # the last sale of the car's cargo, for the Outpost tab
    "owned": set(),               # the keys of the upgrades bought (upgrades.py)
    "params": upgrades.effective({"car": hovercar.CarParams(), "gun": GunParams(), "suit": suit.SuitParams()},
                                 set()),
    "upgrade_note": "",           # the last upgrade bought or refused, for the Upgrades tab
    "suit": suit.SuitState(),     # the suit's reserve and the player's health (suit.py)
    "suit_params": suit.SuitParams(),
    "temperature": None,          # degrees C where the camera is, once known (here_temperature)
    "woken": 0,                   # times the player woke at an outpost
    "pois": pois.Pois(),          # the points of interest known (pois.py)
    "poi_want_at": None,          # the direction the candidates were last looked for around
    "poi_asked": None,            # the extra probes asked for the candidates, and their Pois
    "poi_asking": [],
    "poi_scenes": {},             # key: [(entity, point in planet space)] of the points drawn
    "poi_cleared": [],            # the keys of the points the world's clearings are around
    "pods": {},                   # id of a cargo pod's crate: its point of interest's key
    "pods_sold": 0,
    "parts": {},                  # part (pois.PARTS): count
    "survey_data": 0.0,           # credits' worth of survey data held
    "walked_counted": None,       # the explored map's walked cells already paid as survey data
    "use_spot": None,             # (poi, spot, distance) the use key acts on now
    "use_progress": 0.0,          # s the use key has been held on it
    "use_latch": False,           # the use key must be let go before the next use
    "check_use": False,           # --check: the use key held
    "contracts": [],              # the active contracts (contracts.Contract)
    "offers_taken": set(),        # the ids of the offers accepted
    "pinned": None,               # the id of the contract on the card, the compass and the map
    "reputation": {},             # outpost name: the player's reputation there
    "contracts_done": 0,
    "contracts_failed": 0,        # failed, expired or abandoned
    "couriers": {},               # id of a courier's crate: its contract's id
    "crate_velocity": {},         # id of a courier's crate: its velocity last frame
    "survey_in": 0.0,             # s until the survey contracts' shares are measured again
    "contract_note": "",          # the last contract accepted, ended or refused, for the Contracts tab
    "contracts_shown": None,      # what the Contracts tab was last built for
    "shower": None,               # the next or current meteor shower (events.Shower)
    "shower_rng": random.Random(),
    "shower_tries": 0,            # times it was planned again for a large impact in water
    "next_shower": None,          # a loaded save's world clock for the next shower's start
    "impact_heights": {},         # the shower's impact index: the ground's height measured there (m)
    "shower_asked": None,         # the extra probes asked for its impacts, and their indices
    "shower_asking": [],
    "shower_warned": False,
    "struck": 0,                  # its impacts struck so far (they are in time order)
    "paths": {},                  # its impact index: the meteor's path (events.Path)
    "craters": [],                # (world.Crater, world clock) of the meteorites' craters, oldest first
    "hot": {},                    # id of a meteorite, its chunk or rock: (world clock it struck, cooling seconds)
    "meteorite": None,            # the last meteorite's prop
    "flash": None,                # (world clock, alpha) of the last impact's flash
    "shake": None,                # (world clock, amplitude in radians) of the last shake
    "plumes": [],                 # (point, up, world clock, blast radius, light, rise share) of each dust puff
    "meteor_glows": None,         # the meteors', plumes' and hot props' quads (viewmodel.Glows)
    "heads_drawn": 0,             # meteors' heads drawn last frame
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
    """The outposts' survey and the lakes' around the site, measured together."""
    seed = tuple(planet.terrain["seed"])
    survey = Survey(bodies, seed)
    lake_survey = LakeSurvey(planet, world.site_frame()[1], seed)
    state["survey"] = (survey, lake_survey)
    world.start_survey(survey.points + lake_survey.points)


def update_outposts():
    """The home outpost at the site's height; once the survey's heights are
    back, the outposts chosen, their sites flattened and their pads drawn,
    the lakes chosen clear of the outposts' sites and the site's pad, and
    the height grid around the site asked for (update_rivers)."""
    home = state["home"]
    home.height = world.site_height or 0.0
    surveys = state["survey"]
    if surveys is not None and surveys[0] == "rivers":
        update_rivers()
        return
    heights = world.surveyed() if surveys is not None else None
    if heights is None:
        return
    survey, lake_survey = surveys
    count = len(survey.points)
    found = survey.choose(heights[:count], taken=[], home=home)
    state["outposts"] = [home] + sorted(found, key=lambda o: (o.body.index, o.name))
    world.set_sites([o.site() for o in state["outposts"] if o.flat_radius > 0.0])
    avoid = [(home.direction, SITE_BLEND_RADIUS)] + [(o.direction, o.blend_radius) for o in found
                                                     if o.body is planet]
    lakes = lake_survey.choose(heights[count:], avoid=avoid)
    world.set_lakes(lakes)
    if lakes:
        nearest = min(planet.radius * math.acos(max(-1.0, min(1.0, dot(home.direction, normalize(k.direction)))))
                      for k in lakes)
        print("[walker] lakes: %d, %.0f to %.0f m across, the nearest %.1f km from Base"
              % (len(lakes), 2.0 * min(k.radius for k in lakes), 2.0 * max(k.radius for k in lakes),
                 nearest / 1000.0), flush=True)
    frame = rivers.GridFrame(home.direction, planet.radius)
    world.request_height_grid(frame)
    state["survey"] = ("rivers", frame, avoid)
    pads.build(state["outposts"])


def update_rivers():
    """Once the height grid around the site is back: the rivers traced over
    it (shoonyakasha.planet.rivers), round the outposts' sites and the
    site's pad and out of the lakes, set, and the survey ended."""
    _, frame, avoid = state["survey"]
    heights = world.height_grid()
    if heights is None:
        return
    found = rivers.trace(heights, frame, sea=planet.ocean, avoid=avoid, wet=world.lake_level)
    world.set_rivers(found, frame)
    print("[walker] rivers: %d, %d segments, %.0f km in all"
          % (len(found), sum(len(r.points) - 1 for r in found), river_length(found) / 1000.0), flush=True)
    world.end_survey()
    state["survey"] = None
    restore_market()


def river_spot():
    """(river, k): the point k of a river at least RIVER_CHECK_WIDTH wide
    there, nearest the site, with a point after it; (None, None) without one."""
    up = world.site_frame()[1]
    best, spot = None, (None, None)
    for river in world.rivers:
        for k in range(len(river.points) - 1):
            if river.widths[k] < RIVER_CHECK_WIDTH:
                continue
            gap = -dot(up, river.points[k])
            if best is None or gap < best:
                best, spot = gap, (river, k)
    return spot


def river_length(found):
    """Metres along the ground of all the rivers in `found`."""
    total = 0.0
    for river in found:
        for a, b in zip(river.points, river.points[1:]):
            total += planet.radius * math.acos(max(-1.0, min(1.0, dot(a, b))))
    return total


def ship_pose():
    """The parked ship: SHIP_SOUTH south of the blocks, hovering
    HOVER_HEIGHT above the ground, facing north."""
    _, up, east, north = world.site_frame()
    position = add(world.site_ground(), add(scale(up, SHIP_CLEARANCE), scale(north, -SHIP_SOUTH)))
    return position, east, up, north


def spawn_point():
    """The player's start: EAST_OF_SHIP east of the ship, on the sphere's
    radius of the site's height (the ground there comes from the probe)."""
    position, east, up, _ = ship_pose()
    return add(position, add(scale(east, EAST_OF_SHIP), scale(up, -SHIP_CLEARANCE)))


def build_ship_model():
    """The ship's model, its meshes parented to one entity, its centre
    (SHIP_MODEL_CENTRE) at the entity's origin; hidden until the ship's
    pose is known."""
    result = engine.load_gltf_scene(SHIP_MODEL, flatten_hierarchy=True)
    if not result.success:
        print("[walker] the ship's model did not load:", result.error, flush=True)
        return
    root = engine.scene.create_entity("ShipModel")
    for entity in result.entities:
        engine.scene.set_parent(entity, root)
        engine.scene.set_position(entity, scale(SHIP_MODEL_CENTRE, -1.0))
        engine.scene.set_material_float(entity, "gridStrength", 0.0)
        engine.scene.set_render_layer_mask(entity, 1)
        engine.scene.set_visible(entity, False)
    state["ship_model"] = (root, result.entities)


def place_ship(camera):
    """The ship's model relative to `camera`: the car's pose, banked and
    pitched (hovercar.Car.attitude), rising and falling HOVER_BOB once
    every HOVER_PERIOD while slow; and the engines' glow."""
    car = state["car"]
    if car is None:
        return
    centre = car_position()
    right, up, forward = car.attitude()
    if state["ship_model"] is not None:
        root, meshes = state["ship_model"]
        for entity in meshes:
            engine.scene.set_visible(entity, True)
        still = max(0.0, 1.0 - car.speed() / 10.0)
        bob = still * HOVER_BOB * math.sin(2.0 * math.pi * world.time / HOVER_PERIOD)
        engine.scene.set_position(root, sub(add(centre, scale(up, bob)), camera))
        engine.scene.set_rotation(root, euler_from_basis(scale(right, -1.0), up, scale(forward, -1.0)))
    controls = state["car_controls"]
    state["car_glows"].update(view_basis(), sub(centre, camera), (right, up, forward),
                              controls.throttle if state["driving"] else 0.0,
                              controls.boost and state["driving"], car.speed())


def remove_ship_body():
    body = state["ship_body"]
    if body is not None and foot.physics.contains(body):
        foot.physics.remove(body)
    state["ship_body"] = None


foot.on_drop = remove_ship_body


# ── The car ────────────────────────────────────────────────────────────────

def car_position():
    """The car's centre in planet space."""
    return add(planet.centre(), state["car"].position)


def make_car(position, heading, target=hovercar.HOVER_MIN):
    """The car at `position` (planet space), its nose along `heading`, with the upgrades' parameters."""
    state["car"] = hovercar.Car(planet.relative(position), heading, target, params=state["params"]["car"])
    update_ship()


def update_ship():
    """`ship` (position, right, up, forward) from the car's pose, level."""
    car = state["car"]
    up = car.up()
    state["ship"] = (car_position(), car.right(), up, car.heading)


def car_ground(point):
    """The height of the car's underside above the ground with its centre
    at `point` (relative to the planet's centre), from the ground patch;
    None where it is not known."""
    patch = world.patch
    if patch is None or patch[0] is not planet:
        return None
    answer = patch[1].query(point)
    if answer is None:
        return None
    return -answer[0] - SHIP_HALF[1]


def camera_ground(point):
    """The height above the ground of `point` (planet space), from the ground patch, or None."""
    h = car_ground(planet.relative(point))
    return None if h is None else h + SHIP_HALF[1]


def car_patch_request():
    """(planet, patch): the ground patch to measure around where the car
    will be when its heights come back, ahead along its velocity by the
    probes' latency, reaching CAR_PATCH_MARGIN and its look-ahead beyond."""
    car = state["car"]
    up = car.up()
    v = car.velocity
    along = sub(v, scale(up, dot(v, up)))
    ahead = world.probe_latency * state["frame_ms"] / 1000.0
    half = CAR_PATCH_MARGIN + length(along) * (ahead + hovercar.LOOK_AHEAD)
    centre = add(car.position, scale(along, ahead))
    return planet, GroundPatch(planet.radius, centre, 2.0 * half / (PATCH_SIZE - 1), sea=planet.ocean,
                               water=world.water_level)


def near_car():
    """Whether the player's feet are within ENTER_REACH of the car's centre."""
    player, car = foot.player, state["car"]
    if player is None or car is None:
        return False
    return length(sub(foot.bubble.to_world(player.feet()), car_position())) <= ENTER_REACH


def net_frame():
    """The car's cargo net: its centre and (right, up, back) in planet space."""
    car = state["car"]
    right, up = car.right(), car.up()
    back = scale(car.heading, -1.0)
    r, u, b = CAR_NET_CENTRE
    return add(car_position(), add(add(scale(right, r), scale(up, u)), scale(back, b))), right, up, back


def in_net():
    """(prop, its position in planet space) of each loose ore rock and each
    contract's crate (`crate_contract`), in the physics or resting out of
    it, that lies in the car's net."""
    bubble = foot.bubble
    if state["car"] is None or bubble is None:
        return []
    ore = [p for p in foot.props.rocks if p.grade > 0.0] + [p for p in foot.props.crates if crate_contract(p)]
    placed = [(p, add(p.on.centre(), p.position)) for p in ore if p.state == "resting"]
    moving = [p for p in ore if p.state == "jolt"]
    if moving:
        placed += [(p, bubble.to_world(t[0])) for p, t in zip(moving, foot.props.transforms(bubble, moving))]
    centre, right, up, back = net_frame()

    def inside(point):
        d = sub(point, centre)
        return (abs(dot(d, right)) <= CAR_NET_HALF[0] and abs(dot(d, back)) <= CAR_NET_HALF[1]
                and -CAR_NET_BELOW <= dot(d, up) <= CAR_NET_ABOVE)
    return [(prop, point) for prop, point in placed if inside(point)]


def load_cargo():
    """The loose ore rocks and the contracts' crates in the car's net
    (in_net) taken into its hold, each that fits (hovercar.Car.can_take):
    a rock with where it was first seen (or where it lies), a crate
    (`crate_item`) with its size's volume; the rest stay in the net. A
    toast says what."""
    car, bubble = state["car"], foot.bubble
    loaded, left = [], 0
    for prop, point in in_net():
        c = crate_contract(prop)
        volume = prop.size ** 3 if c is not None else None
        if not car.can_take(prop.mass, prop.resource, volume):
            left += 1
            continue
        if c is not None:
            car.cargo.append(crate_item(c, prop.mass, volume))
            forget_crate(prop)
        else:
            seen = state["seen"].pop(id(prop), None) or (prop.on.name, normalize(prop.on.relative(point)))
            car.cargo.append({"body": seen[0], "direction": tuple(seen[1]), "mass": prop.mass, "grade": prop.grade,
                              "resource": prop.resource})
        if foot.gun is not None and prop.body is not None and foot.gun.held == prop.body:
            foot.gun.drop()
        foot.props.remove(bubble, prop, release=False)
        loaded.append(prop.mass)
    if loaded or left:
        text = ("Loaded %d piece%s, %.0f kg" % (len(loaded), "" if len(loaded) == 1 else "s", sum(loaded))
                + ("; %d left in the net, the hold is full" % left if left else ""))
        state["hud"].toast(text, GOOD_TOAST if not left else hud_module.WARNING)


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
    """The net's corner posts behind the car while it is parked (not driven)."""
    shown = state["car"] is not None and not state["driving"]
    if shown:
        centre, right, up, back = net_frame()
        rotation = euler_from_basis(right, up, scale(back, -1.0))
    for k, entity in enumerate(state["net_posts"]):
        engine.scene.set_visible(entity, shown)
        if not shown:
            continue
        corner = add(scale(right, CAR_NET_HALF[0] * (1 if k & 1 else -1)),
                     scale(back, CAR_NET_HALF[1] * (1 if k & 2 else -1)))
        engine.scene.set_position(entity, sub(add(add(centre, corner), scale(up, 0.5 * NET_POST[1])), camera))
        engine.scene.set_rotation(entity, rotation)


def enter_car(anywhere=False):
    """Into the car from within ENTER_REACH of it (from anywhere with
    `anywhere`): the ore in its net loaded (load_cargo), the player gone,
    the chase camera behind the car."""
    if state["driving"] or state["car"] is None or (not anywhere and not near_car()):
        return False
    load_cargo()
    foot.remove_player()
    state["driving"] = True
    state["chase"].reset()
    state["hud"].toast("F gets out once slow and low", hud_module.DIM)
    print("[walker] in the car", flush=True)
    return True


def leave_car():
    """Out of the car, once it is slow and low enough (hovercar.Car.can_leave),
    at its left side, EXIT_OUT beyond it, facing its way; the car parks.
    Parked on an outpost's pad, with autosave on, the game is saved."""
    car = state["car"]
    if not state["driving"]:
        return False
    if not car.can_leave():
        state["hud"].toast("Slow down and come down first", hud_module.WARNING)
        return False
    right, up, forward = car.right(), car.up(), car.heading
    side = add(car.position, scale(right, -(SHIP_HALF[0] + EXIT_OUT)))
    h = car_ground(side)
    if h is None:
        return False
    feet = add(planet.centre(), add(side, scale(up, -(h + SHIP_HALF[1]) + SPAWN_LIFT)))
    bubble = foot.ensure_bubble(feet)
    foot.spawn(feet, forward)
    place_eye()
    state["driving"] = False
    set_fov(settings["fov"])
    car.target = hovercar.HOVER_MIN
    state["car_controls"] = hovercar.CarControls()
    print("[walker] out of the car", flush=True)
    outpost = car_outpost()
    if outpost is not None and settings["autosave"] and not args.check:
        if save_game(quiet=True) is not None:
            state["hud"].toast("Game saved at %s" % outpost.name, GOOD_TOAST)
    return bubble is not None


def read_car_controls():
    """The car's controls, and the mouse orbiting the chase camera."""
    state["car_controls"] = carview.read_controls(engine.input)
    if engine.input.is_mouse_captured():
        dx, dy = engine.input.get_mouse_delta()
        s = foot.mouse_sensitivity
        state["chase"].orbit(dx * s, (-dy if foot.invert_y else dy) * s)
    if engine.input.is_gamepad_connected():
        x = engine.input.is_gamepad_button_down(keys.GAMEPAD_X)
        if x and not state["pad_x"]:
            leave_car()
        state["pad_x"] = x


def step_car(dt):
    """The car moved by `dt` (driven, or parked with no controls) in the
    wind at its height above the ground (PlanetWorld.wind_at), stopped by
    trunks and fixed rocks ahead (bump_car), a bump striking the fragile
    crates in its hold (strike_hold); its poses before and after kept for
    its body in the bubble."""
    car = state["car"]
    if car is None:
        return
    before = (car_position(), car_rotation())
    controls = state["car_controls"] if state["driving"] else hovercar.CarControls()
    car.step(dt, controls, car_ground, wind=lambda point: world.wind_at(planet, point, car.height))
    impact = bump_car(dt)
    if impact > 0.0:
        strike_hold(impact)
    update_ship()
    state["car_poses"] = (before, (car_position(), car_rotation()))


def car_rotation():
    right, up, forward = state["car"].attitude()
    return quat_from_basis(right, up, scale(forward, -1.0))


def bump_car(dt):
    """A ray from the car's nose along its motion over the ground: a trunk
    or a fixed rock within this step's travel (and BUMP_REACH more) turns
    the car's motion into it back by BUMP_BOUNCE times, and it keeps
    BUMP_KEEP of its speed. Props, the ground and the car's own body are
    passed over. Returns the speed it struck with (m/s into the surface), or 0."""
    car, bubble = state["car"], foot.bubble
    if bubble is None or bubble.body is not planet:
        return 0.0
    up = car.up()
    flat = sub(car.velocity, scale(up, dot(car.velocity, up)))
    speed = length(flat)
    if speed < 0.5:
        return 0.0
    direction = scale(flat, 1.0 / speed)
    reach = SHIP_HALF[2] if abs(dot(direction, car.heading)) > 0.7 else SHIP_HALF[0]
    nose = add(car_position(), scale(direction, reach + 0.15))
    hit = foot.physics.cast_ray(bubble.to_bubble(nose), direction, speed * dt + BUMP_REACH)
    if hit is None:
        return 0.0
    body, _, normal, _ = hit
    if body == state["ship_body"] or foot.props.find(body) is not None or dot(normal, bubble.up(hit[1])) > 0.7:
        return 0.0
    n = normalize(sub(normal, scale(bubble.up(hit[1]), dot(normal, bubble.up(hit[1])))))
    into = dot(car.velocity, n)
    if into >= 0.0:
        return 0.0
    car.velocity = scale(sub(car.velocity, scale(n, (1.0 + BUMP_BOUNCE) * into)), BUMP_KEEP)
    return -into


def move_car_body(bubble, fraction):
    """The car's kinematic body in the bubble moved `fraction` of the way
    along this frame's poses (OnFoot.step's before_step), made when the car
    comes within the bubble's reach and removed beyond it."""
    poses = state["car_poses"]
    if poses is None or bubble.body is not planet:
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
    physics = foot.physics
    body = state["ship_body"]
    if body is None:
        hull = np.array(SHIP_HULL, dtype=np.float32)
        state["ship_body"] = physics.create_convex_hull(hull, p, q, motion="kinematic", friction=0.8)
    elif length(sub(physics.get_transform(body)[0], p)) > JUMP_DISTANCE:
        physics.set_transform(body, p, q)
    else:
        physics.move_kinematic(body, p, q, STEP)


def place_chase(dt):
    """The camera behind the car while driving; the field of view widening with its speed."""
    car = state["car"]
    eye, basis = state["chase"].update(dt, car_position(), car.heading, car.up(), car.speed(), camera_ground)
    state["position"], state["forward"] = eye, basis[2]
    state["chase_basis"] = basis
    set_fov(carview.fov(settings["fov"], car.speed()))


def set_fov(value):
    if world.fov != value:
        world.fov = value
        if world.camera is not None:
            engine.scene.set_camera_fov(world.camera, value)


# ── The player ─────────────────────────────────────────────────────────────

def maybe_spawn():
    """The player put down once the terrain is built, the site's height and
    the ground under the camera are known: at the start beside the ship,
    or LOAD_LIFT above the ground where a loaded save had it; not while the
    start's menu is up or in the car."""
    if (foot.player is not None or planet.tree is None or world.site_height is None or state["starting"]
            or state["driving"]):
        return
    if state["ship"] is None:
        position, _, _, north = ship_pose()
        make_car(position, north)
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
        enter_car()
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
    (contract_target), an announced shower's centre and the last
    meteorite while it lies whole on the planet."""
    found = [(o.name, o.point(), OUTPOST_MARKER) for o in state["outposts"] if o.body is body]
    if state["ship"] is not None and body is planet:
        found.append(("Ship", state["ship"][0], SHIP_MARKER))
    waypoint = state["waypoint"]
    if waypoint is not None and waypoint[0] is body:
        found.append(("Waypoint", add(body.centre(), scale(waypoint[1], body.radius)), WAYPOINT_MARKER))
    target = contract_target(pinned_contract())
    if target is not None and body is planet:
        found.append(("Contract", target, CONTRACT_MARKER))
    if body is planet and state["shower"] is not None and state["shower_warned"]:
        found.append(("Meteors", add(planet.centre(), scale(state["shower"].centre, planet.radius)), METEOR_MARKER))
    meteorite = meteorite_point()
    if body is planet and meteorite is not None:
        found.append(("Meteorite", meteorite, METEOR_MARKER))
    return found + poi_markers(body)


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
        hud.update(view_basis(), state["position"], planet, None, None, markers(planet), speed, pad=pad, dt=dt,
                   height=car.height, drive=car.can_leave(), hour=hour_text(),
                   car_fuel=car.fuel / car.params.fuel_capacity, flash=flash_tint())
        return
    body = foot.bubble.body
    targets = markers(body)
    up = foot.bubble.up(player.feet())
    v = foot.physics.character_velocity(player.character)
    speed = length(sub(v, scale(up, dot(v, up))))
    height = world.height_above_ground(state["position"]) - player.eye_height
    extra = [((("pad:X",) if pad else ("F",)), "Get in")] if near_car() else []
    spot = state["use_spot"]
    if spot is not None:
        poi, k, _ = spot
        hold = pois.SPOTS[poi.kind][k][2]
        word = "%s %s" % (pois.USE_WORDS[poi.kind], poi.name.lower())
        if hold > 0.0:
            word += " (hold, %.1f / %.0f s)" % (state["use_progress"], hold)
        extra.append((("E",), word))
    hud.update(view_basis(), state["position"], body, player, foot.gun, targets, speed, pad=pad, dt=dt,
               height=height, extra_hints=extra, hour=hour_text(), tint=suit_effects()[0], flash=flash_tint())


def run_check(dt):
    """--check, once on foot (with --shot, first walking away from the ship
    for SHIP_SHOT_WALK, turned to it and a second later a screenshot of it,
    FILE with _ship added, then turned north): walk north for
    CHECK_SECONDS, half way taking
    a screenshot into CHECK_CAPTURES as P does; then a crate of
    CHECK_CRATE kg dropped CHECK_AHEAD ahead, and once it lies still the
    view turned to it and the grab held for GRAB_SECONDS; then a punt of
    it charged for CHARGE_SECONDS and let go; PUNT_SHOT_SECONDS later the
    full map open for MAP_SECONDS and a waypoint set on it by a click, and
    the map shown half a second more; then the map closed, the
    temperature here taken, and with SUIT_CHECK_TEMPERATURE standing in for
    it and no outpost counted, the suit left SUIT_CHECK_RESERVE and the
    health SUIT_CHECK_HEALTH, waiting up to SUIT_SECONDS for the suit to
    give out: passing when the temperature had been here_temperature's,
    the card had its SUIT and TEMP rows, the suit ran low with the walk
    slowed and a tint, and the player came to at Base charged
    RECOVERY_FEE with the suit full; then an ore rock
    (CHECK_ORE_EXTENT, CHECK_ORE_GRADE) dropped 1 m above the Base pad,
    waiting up to SELL_SECONDS for it to sell and a second more; then the
    car moved NET_CHECK_AWAY along its nose off Base's pad (where ore at
    rest sells by itself), an ore rock of each CHECK_NET_EXTENTS dropped
    1 m over its net and NET_SETTLE later the car got into, the rocks gone
    from the props into its cargo with their masses, the car put back on
    the pad and its cargo sold there for trade.sale's credits, and out of
    the car; then, given UPGRADE_CREDITS, Main engine tuning tried first
    (refused for want of the Auxiliary tank) and UPGRADE_BUYS bought, the
    credits falling by their costs and the car's tank and hold and the
    gun's force 1.5 times their own; then the points of interest
    (run_poi_check): the candidates within POI_REACH measured (at most
    POI_MEASURE_SECONDS); a beacon, a wreck, a rig and an arch put by the
    player (check_poi) and used with the use key held (the beacon's survey
    data and map cells queued, a part from the wreck after its hold and
    from the rig with its ore rocks, the arch's survey data); a geode
    field's six rocks of quartz at its grade made; a pod made on Base's pad
    and sold once settled; the survey data sold for trade.survey_sale's
    credits; and Capacitor bank bought if a coil was found (else refused)
    and the Thermal liner lowering the suit's cold limit; then the
    contracts (run_contract_check): Base's offers the same when asked
    twice; a delivery taking an ore rock dropped on the pad in place of
    selling it; a courier's crate put in the car's hold on the pad and
    delivered there; with the car moved NET_CHECK_AWAY off, a courier's
    crate beside the player thrown down at CONTRACT_CHECK_DROP, breaking
    and failing; a recovery's pod made on the pad and delivered there; a
    survey around the feet completed; a contract past its deadline
    expired; a pinned one abandoned; and a survey 2 km off kept active and
    pinned, on the card and the compass, the car put back; each paying its
    reward or none and moving Base's reputation, +1 at the end; then a
    meteor shower (run_shower_check): a large impact SHOWER_CHECK_CENTRE
    east of the feet SHOWER_CHECK_LEAD on, with a meteorite of
    SHOWER_CHECK_MASS, and a small one a second later SHOWER_CHECK_OFF_PAD
    beyond Base's pad's reach (outposts.PAD_REACH) on the line from its
    centre through the feet, with a rock of SHOWER_CHECK_ROCK, both at
    4 km/s 60 degrees down travelling north; SHOWER_LAND_SECONDS after the
    last, passing when one crater was dug of the large one's size where it
    struck, its meteorite of iridium
    and its mass lies hot and at rest within 1 m of the crater's floor (the
    ground measured there before, less its depth), the rock of iridium and
    its mass is hot, the shower was announced and the meteorite told of, a
    meteor's head was drawn, and there was a flash, a shake and the large
    impact's plume; then, walked to within SHOWER_CHECK_NEAR of the rock,
    the view on it and the grab held, the gun refuses it as too hot, and
    with it cooled grabs it; then the
    menu shown for PAUSE_SECONDS; then the game saved to CHECK_SAVE_BASE,
    the credits, the ore sold and the explored map changed, and the save
    loaded, waiting up to LOAD_SECONDS for the player to come back and a
    second more. Passes when the player covered 80 % of walking speed's
    distance and stands on the ground, the screenshot was written and its
    toast shown, the gun holds the crate, the punt charged and threw it,
    at least MAP_CELLS cells are explored, the waypoint is set, the ore sold for credits, the car's cargo
    loaded and sold, the world's clock stood
    still while paused, and the load brought back the credits, the ore
    sold, the cells, the props, the contracts, the pin, the reputations,
    the craters, the hot props and the player within 2 m of where it was saved; then into the car, driven flat out turning a little for
    DRIVE_SECONDS, climbing for CLIMB_SECONDS and flying on 2 s more,
    saved and loaded there, braking and coming down until it can be left
    (at most LAND_SECONDS), and out of it, a second later passing when the
    load put the car back where it was and still driven, it went faster
    than 30 m/s and higher than 80 % of the climb and the player stands
    beside it; then into the car again, put LAKE_CHECK_DROP above the
    water of the lake nearest it at its centre and left for LAKE_SECONDS,
    passing when it hovers HOVER_MIN (within 25 cm) above the water and
    the ground under the camera lies at least 50 cm under the water; then
    put LAKE_CHECK_DROP above the water of the point nearest the site of a
    river at least RIVER_CHECK_WIDTH wide there, facing down it, and left
    for LAKE_SECONDS, passing when it hovers HOVER_MIN (within 25 cm)
    above the water there; then, the car down on its cushion and slower
    than 0.3 m/s (at most LAND_SECONDS), its tank emptied
    and DRY_SECONDS at full throttle, passing when it moved less than 1 m,
    fuel called (call_fuel) and, by FUEL_WAIT after it is due, delivered:
    the tank holding what the credits less the fee buy at its price (at
    most full) and the credits less that; then the day's hour set to NIGHT_CHECK_HOUR and
    NIGHT_SECONDS later passing with the night's exposure
    (clock.NIGHT_SCALE) and at least the car's two headlights drawn. The
    settings are the defaults and are not written. With --shot,
    screenshots while walking (SHOT_SECONDS in), while holding (FILE with
    _hold added), while charging (with _charge added), just after the punt
    (with _punt added), of the map (with _map added), with the suit low (with _suit added), after the sale (with
    _sell added), of the Upgrades tab after buying (with _upgrades added), of the shower's large meteor (with
    _meteor added), of its impact (with _impact added) and its plume 4 s on (with _plume added), of the menu
    (with _pause added), of its Car tab (with _car added), of its Outpost tab (with _outpost added), of its Contracts tab (with _contracts added), of its Settings tab
    (with _settings added), of its Controls tab (with _controls added),
    after the load (with _load added), a second
    before the end of the drive (with _drive added), after the climb (with
    _fly added), out of the car (with _out added), over the lake (with
    _lake added), after climbing for LAKE_CLIMB_SECONDS and 2 s more,
    above it (with _lake_above added), over the river (with _river added)
    and, after climbing for RIVER_CLIMB_SECONDS and 2 s more, above it
    (with _river_above added), and at night (with _night added)."""
    player = foot.player
    check = state["check"]
    if player is None and (check is None or check["stage"] not in ("reload", "drive", "fly", "land", "out", "lake", "lake_above", "to_river", "river", "river_above", "fuel", "night", "net_out")):
        return
    if check is None:
        check = state["check"] = {"stage": "ship" if args.shot else "walk", "time": 0.0,
                                  "start": foot.bubble.to_world(player.feet())}
    check["time"] += dt
    stage, t = check["stage"], check["time"]
    if stage == "ship":
        to_ship = sub(foot.bubble.to_bubble(state["ship"][0]), player.feet())
        if t < SHIP_SHOT_WALK:
            player.heading = player.level(scale(to_ship, -1.0), player.up())
            foot.controls.forward = 1.0
        else:
            foot.controls.forward = 0.0
            player.heading = player.level(to_ship, player.up())
            player.pitch = -0.08
        if t >= SHIP_SHOT_WALK + 1.0:
            base, ext = os.path.splitext(args.shot)
            screenshot(base + "_ship" + ext)
            player.heading = state["ship"][3]
            player.pitch = 0.0
            check["stage"], check["time"] = "walk", 0.0
            check["start"] = foot.bubble.to_world(player.feet())
    elif stage == "walk":
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
            check["stage"], check["time"] = "suit", 0.0
    elif stage == "suit":
        if "suit" not in check:
            here = here_temperature()
            check["suit"] = {"temperature": state["temperature"], "here": here, "rows": [r[0] for r in suit_rows()],
                             "credits": state["market"].credits, "woken": state["woken"],
                             "functions": (here_temperature, at_outpost)}
            globals()["here_temperature"] = lambda: SUIT_CHECK_TEMPERATURE
            globals()["at_outpost"] = lambda: None
            state["suit"].reserve, state["suit"].health = SUIT_CHECK_RESERVE, SUIT_CHECK_HEALTH
            return
        s = check["suit"]
        if state["woken"] == s["woken"]:
            if t > SUIT_SECONDS:
                print("[walker] the suit did not give out in %.0f s at %.0f C: FAILED" % (t, SUIT_CHECK_TEMPERATURE),
                      flush=True)
                os._exit(1)
            if "slow" not in s and state["suit"].reserve < suit.SLOW_BELOW and state["suit"].reserve > 0.0:
                s["slow"] = suit_effects()
            if args.shot and "shot" not in s and 0.0 < state["suit"].reserve < 0.1:
                s["shot"] = True
                base, ext = os.path.splitext(args.shot)
                screenshot(base + "_suit" + ext)
            return
        globals()["here_temperature"], globals()["at_outpost"] = s["functions"]
        if foot.player is None:
            if t > SUIT_SECONDS + LOAD_SECONDS:
                print("[walker] the player did not come to at an outpost: FAILED", flush=True)
                os._exit(1)
            return
        outpost = at_outpost()
        fee = s["credits"] - state["market"].credits
        tint, factor = s.get("slow", (None, 1.0))
        suit_ok = (s["temperature"] is not None and abs(s["temperature"] - s["here"]) < 1e-6
                   and s["rows"] == ["Suit", "Temp"] and outpost is state["home"]
                   and abs(fee - min(RECOVERY_FEE, s["credits"])) < 1e-6
                   and state["suit"].reserve == state["suit_params"].reserve and state["suit"].health == 1.0
                   and factor == suit.SLOW_FACTOR and tint is not None and tint[1] > 0.0)
        print("[walker] %.1f C here; at %.0f C the suit ran low (the walk x%.1f, a tint of %.2f), gave out, and the "
              "player came to at %s for %.0f cr, the suit full: %s"
              % (s["here"] if s["here"] is not None else float("nan"), SUIT_CHECK_TEMPERATURE, factor,
                 tint[1] if tint else 0.0, outpost.name if outpost is not None else "no outpost", fee,
                 "passed" if suit_ok else "FAILED"), flush=True)
        check["suit_ok"] = suit_ok
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
            check["stage"], check["time"] = "net", 0.0
    elif stage == "net":
        if "net_rocks" not in check:
            car = state["car"]
            check["parked_at"] = car.position          # on Base's pad, where ore resting sells by itself
            car.position = add(car.position, scale(car.heading, NET_CHECK_AWAY))
            update_ship()
            centre, right, up, back = net_frame()
            check["net_rocks"] = [ore_rock(add(add(centre, scale(right, side)), scale(up, 1.0)), extent,
                                           CHECK_ORE_GRADE)
                                  for side, extent in zip((-0.8, 0.8), CHECK_NET_EXTENTS)]
            return
        if t >= NET_SETTLE:
            rocks = check["net_rocks"]
            masses = sorted(round(r.mass, 3) for r in rocks)
            credits = state["market"].credits
            enter_car(anywhere=True)
            car = state["car"]
            car.position = check["parked_at"]
            update_ship()
            outpost = car_outpost()
            loaded = (not any(r in foot.props.rocks for r in rocks)
                      and sorted(round(c["mass"], 3) for c in car.cargo) == masses)
            expected = trade.sale(outpost, car.cargo, world.time)[0] if outpost is not None and car.cargo else 0.0
            sell_car_cargo()
            gained = state["market"].credits - credits
            net_ok = loaded and outpost is not None and not car.cargo and expected > 0.0 and abs(gained - expected) < 1e-6
            print("[walker] %d ore rocks in the car's net loaded on getting in (%s kg) and sold at %s for %.0f cr "
                  "(expected %.0f): %s" % (len(rocks), ", ".join("%.0f" % m for m in masses),
                                           outpost.name if outpost is not None else "no pad", gained, expected,
                                           "passed" if net_ok else "FAILED"), flush=True)
            check["sell_ok"] = check["sell_ok"] and net_ok
            check["stage"], check["time"] = "net_out", 0.0
    elif stage == "net_out":
        state["car_controls"] = hovercar.CarControls()
        if foot.player is None and not leave_car():
            if t > LAND_SECONDS:
                print("[walker] could not get out of the car back on Base's pad: FAILED", flush=True)
                os._exit(1)
            return
        check["stage"], check["time"] = "upgrades", 0.0
    elif stage == "upgrades":
        if "bought" not in check:
            state["market"].credits += UPGRADE_CREDITS
            credits = state["market"].credits
            engine = upgrades.BY_KEY["engine_1"]
            buy_upgrade(engine)
            refused = "engine_1" not in state["owned"] and state["market"].credits == credits
            for key in UPGRADE_BUYS:
                buy_upgrade(upgrades.BY_KEY[key])
            spent = credits - state["market"].credits
            base_car, base_gun = hovercar.CarParams(), GunParams()
            car, gun = state["car"].params, foot.gun.p
            check["bought"] = (refused and state["owned"] == set(UPGRADE_BUYS)
                               and abs(spent - sum(upgrades.BY_KEY[k].cost for k in UPGRADE_BUYS)) < 1e-6
                               and abs(car.fuel_capacity - 1.5 * base_car.fuel_capacity) < 1e-9
                               and abs(car.cargo_capacity - 1.5 * base_car.cargo_capacity) < 1e-9
                               and abs(gun.hold_force - 1.5 * base_gun.hold_force) < 1e-9
                               and foot.gun_params is state["params"]["gun"])
            print("[walker] at %s: Main engine tuning refused first (%s); bought %s for %.0f cr: the car's tank "
                  "%.0f kg and hold %.2f t, the gun %.1f kN: %s"
                  % (at_outpost().name if at_outpost() is not None else "no outpost",
                     "yes" if refused else "NO", ", ".join(upgrades.BY_KEY[k].name for k in UPGRADE_BUYS), spent,
                     car.fuel_capacity, car.cargo_capacity / 1000.0, gun.hold_force / 1000.0,
                     "passed" if check["bought"] else "FAILED"), flush=True)
            check["sell_ok"] = check["sell_ok"] and check["bought"]
            if args.shot:
                state["menu"].show(True, state["menu"].tab_named("Upgrades"))
            return
        if t >= 0.5 or not args.shot:
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                screenshot(base + "_upgrades" + ext)
                set_paused(False)
            check["stage"], check["time"] = "pois", 0.0
    elif stage == "pois":
        if run_poi_check(check, t):
            check["stage"], check["time"] = "contracts", 0.0
    elif stage == "contracts":
        if run_contract_check(check, t):
            check["stage"], check["time"] = "showers", 0.0
    elif stage == "showers":
        if run_shower_check(check, t):
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
                state["menu"].show(True, state["menu"].tab_named("Car"))
            check["pause_ok"] = pause_ok
            check["stage"], check["time"] = "car_tab", 0.0
    elif stage == "car_tab":
        if t >= 0.5 or not args.shot:
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                screenshot(base + "_car" + ext)
                state["menu"].show(True, state["menu"].tab_named("Outpost"))
            check["stage"], check["time"] = "outpost_tab", 0.0
    elif stage == "outpost_tab":
        if t >= 0.5 or not args.shot:
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                screenshot(base + "_outpost" + ext)
                state["menu"].show(True, state["menu"].tab_named("Contracts"))
            check["stage"], check["time"] = "contracts_tab", 0.0
    elif stage == "contracts_tab":
        if t >= 0.5 or not args.shot:
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                screenshot(base + "_contracts" + ext)
                state["menu"].show(True, state["menu"].tab_named("Settings"))
            check["stage"], check["time"] = "settings", 0.0
    elif stage == "settings":
        if t >= 0.5 or not args.shot:
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                screenshot(base + "_settings" + ext)
                state["menu"].show(True, state["menu"].tab_named("Controls"))
                check["stage"], check["time"] = "controls", 0.0
                return
            check["stage"] = "controls"
    elif stage == "controls":
        if t >= 0.5 or not args.shot:
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                screenshot(base + "_controls" + ext)
            set_paused(False)
            saved = save_game(CHECK_SAVE_BASE)
            check["saved"] = {"credits": state["market"].credits, "cells": game_numbers()["cells"],
                              "props": len(foot.props.all), "sold_kg": state["sold_kg"],
                              "feet": foot.bubble.to_world(player.feet()), "time": saved["time"],
                              "contracts": [(c.id, c.kind, c.state) for c in state["contracts"]],
                              "pinned": state["pinned"], "reputation": dict(state["reputation"]),
                              "craters": list(world.craters), "hot": sorted(state["hot"].values())}
            state["market"].credits += 999.0
            state["sold_kg"] += 99.0
            state["map"].grids = {}
            state["contracts"], state["pinned"], state["reputation"] = [], None, {}
            state["craters"], state["hot"] = [], {}
            world.set_craters([])
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
                       and len(foot.props.all) == saved["props"] and moved < 2.0
                       and [(c.id, c.kind, c.state) for c in state["contracts"]] == saved["contracts"]
                       and state["pinned"] == saved["pinned"] and state["reputation"] == saved["reputation"]
                       and list(world.craters) == saved["craters"] and sorted(state["hot"].values()) == saved["hot"])
            print("[walker] saved, changed and loaded: %.0f cr, %d cells, %d props, %d contract%s, %d crater%s, "
                  "%d hot, the player %.2f m from where it was saved: %s"
                  % (state["market"].credits, game_numbers()["cells"], len(foot.props.all), len(state["contracts"]),
                     "" if len(state["contracts"]) == 1 else "s", len(world.craters),
                     "" if len(world.craters) == 1 else "s", len(state["hot"]), moved,
                     "passed" if load_ok else "FAILED"),
                  flush=True)
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                screenshot(base + "_load" + ext)
            check["ok"] = (check["walk_ok"] and check["capture_ok"] and check["held_ok"] and check["punt_ok"]
                           and check["map_ok"] and check["suit_ok"] and check["sell_ok"] and check["pause_ok"]
                           and check["shower_ok"] and load_ok)
            check["in_car"] = enter_car(anywhere=True)
            check["top_speed"], check["top_height"] = 0.0, 0.0
            check["stage"], check["time"] = "drive", 0.0
    elif stage in ("drive", "fly", "land"):
        car = state["car"]
        check["top_speed"] = max(check["top_speed"], car.speed())
        check["top_height"] = max(check["top_height"], car.height or 0.0)
        if stage == "drive":
            state["car_controls"] = hovercar.CarControls(throttle=1.0, steer=0.25)
            if args.shot and t >= DRIVE_SECONDS - 1.0 and "drive_shot" not in check:
                check["drive_shot"] = True
                base, ext = os.path.splitext(args.shot)
                screenshot(base + "_drive" + ext)
            if t >= DRIVE_SECONDS:
                check["stage"], check["time"] = "fly", 0.0
        elif stage == "fly":
            state["car_controls"] = hovercar.CarControls(throttle=0.6, climb=1.0 if t < CLIMB_SECONDS else 0.0)
            if t >= CLIMB_SECONDS + 2.0:
                if args.shot:
                    base, ext = os.path.splitext(args.shot)
                    screenshot(base + "_fly" + ext)
                save_game(CHECK_SAVE_BASE, quiet=True)
                saved_at = car_position()
                state["car"].target = hovercar.HOVER_MIN
                loaded = load_game(CHECK_SAVE_BASE)
                moved = length(sub(car_position(), saved_at))
                check["flight_load_ok"] = loaded and state["driving"] and moved < 0.01 and state["car"].target > 10.0
                print("[walker] saved and loaded in flight: driving %s, the car %.3f m from where it was saved: %s"
                      % (state["driving"], moved, "passed" if check["flight_load_ok"] else "FAILED"), flush=True)
                check["stage"], check["time"] = "land", 0.0
        else:
            moving = car.speed() > 1.0
            state["car_controls"] = hovercar.CarControls(throttle=-1.0 if moving and dot(car.velocity, car.heading) > 0.0
                                                         else 0.0, climb=-1.0)
            if car.can_leave() or t >= LAND_SECONDS:
                left = leave_car()
                check["stage"], check["time"], check["left"] = "out", 0.0, left
    elif stage == "out":
        if t >= 1.0:
            drive_ok = (check["in_car"] and check["flight_load_ok"] and check["top_speed"] > 30.0
                        and check["top_height"] > 0.8 * CLIMB_SECONDS * hovercar.CLIMB_RATE
                        and check["left"] and player is not None and not state["driving"])
            print("[walker] drove the car to %.0f km/h, flew it %.0f m up, came down and got out: %s"
                  % (3.6 * check["top_speed"], check["top_height"], "passed" if drive_ok else "FAILED"), flush=True)
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                screenshot(base + "_out" + ext)
            check["ok"] = check["ok"] and drive_ok
            if not world.lakes:
                print("[walker] no lakes were found: FAILED", flush=True)
                os._exit(1)
            check["in_car"] = enter_car(anywhere=True)
            lake = min(world.lakes, key=lambda k: -dot(normalize(k.direction), normalize(state["car"].position)))
            car = state["car"]
            car.position = scale(normalize(lake.direction), planet.radius + lake.level + LAKE_CHECK_DROP)
            car.velocity = (0.0, 0.0, 0.0)
            car.heading = car._level(car.heading)
            car.target = hovercar.HOVER_MIN
            check["lake"] = lake
            check["stage"], check["time"] = "lake", 0.0
    elif stage == "lake":
        state["car_controls"] = hovercar.CarControls()
        if t >= LAKE_SECONDS:
            car, lake = state["car"], check["lake"]
            above_water = length(car.position) - planet.radius - lake.level - SHIP_HALF[1]
            bed = world.ground
            lake_ok = (check["in_car"] and car.height is not None and abs(car.height - hovercar.HOVER_MIN) < 0.25
                       and abs(above_water - car.height) < 0.25 and bed is not None and bed < lake.level - 0.5)
            print("[walker] the car over a lake %.0f m across: %.2f m above its water, the ground %s m under it: %s"
                  % (2.0 * lake.radius, above_water, "?" if bed is None else "%.1f" % (lake.level - bed),
                     "passed" if lake_ok else "FAILED"), flush=True)
            check["ok"] = check["ok"] and lake_ok
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                screenshot(base + "_lake" + ext)
            check["stage"], check["time"] = "lake_above" if args.shot else "to_river", 0.0
    elif stage == "lake_above":
        state["car_controls"] = hovercar.CarControls(climb=1.0 if t < LAKE_CLIMB_SECONDS else 0.0)
        if t >= LAKE_CLIMB_SECONDS + 2.0:
            base, ext = os.path.splitext(args.shot)
            screenshot(base + "_lake_above" + ext)
            check["stage"], check["time"] = "to_river", 0.0
    elif stage == "to_river":
        river, k = river_spot()
        if river is None:
            print("[walker] no river at least %.0f m wide was found: FAILED" % RIVER_CHECK_WIDTH, flush=True)
            os._exit(1)
        car = state["car"]
        car.position = scale(normalize(river.points[k]), planet.radius + river.levels[k] + LAKE_CHECK_DROP)
        car.velocity = (0.0, 0.0, 0.0)
        car.heading = car._level(sub(river.points[k + 1], river.points[k]))
        car.target = hovercar.HOVER_MIN
        check["river"] = (river, k)
        check["stage"], check["time"] = "river", 0.0
    elif stage == "river":
        state["car_controls"] = hovercar.CarControls()
        if t >= LAKE_SECONDS:
            car = state["car"]
            river, k = check["river"]
            level = world.water_level(normalize(car.position))
            above_water = None if level is None else length(car.position) - planet.radius - level - SHIP_HALF[1]
            river_ok = (car.height is not None and above_water is not None
                        and abs(car.height - hovercar.HOVER_MIN) < 0.25 and abs(above_water - car.height) < 0.25)
            print("[walker] the car over a river %.0f m wide: %s m above its water: %s"
                  % (river.widths[k], "?" if above_water is None else "%.2f" % above_water,
                     "passed" if river_ok else "FAILED"), flush=True)
            check["ok"] = check["ok"] and river_ok
            if not args.shot:
                check["stage"], check["time"] = "fuel", 0.0
                return
            base, ext = os.path.splitext(args.shot)
            screenshot(base + "_river" + ext)
            check["stage"], check["time"] = "river_above", 0.0
    elif stage == "river_above":
        state["car_controls"] = hovercar.CarControls(climb=1.0 if t < RIVER_CLIMB_SECONDS else 0.0)
        if t >= RIVER_CLIMB_SECONDS + 2.0:
            base, ext = os.path.splitext(args.shot)
            screenshot(base + "_river_above" + ext)
            check["stage"], check["time"] = "fuel", 0.0
    elif stage == "fuel":
        car = state["car"]
        if "dry_from" not in check:
            state["car_controls"] = hovercar.CarControls(climb=-1.0)
            if car.height is None or car.height > hovercar.LEAVE_HEIGHT or car.speed() > 0.3:
                if t > LAND_SECONDS:
                    print("[walker] the car did not come down to call fuel: FAILED", flush=True)
                    os._exit(1)
                return
            car.fuel = 0.0
            check["dry_from"] = car.position
            check["time"] = 0.0
            return
        if "call" not in check:
            state["car_controls"] = hovercar.CarControls(throttle=1.0)        # dry: it must not drive
            if t >= DRY_SECONDS:
                state["car_controls"] = hovercar.CarControls()
                check["dry_moved"] = length(sub(car.position, check["dry_from"]))
                check["credits_before"] = state["market"].credits
                call_fuel()
                check["call"] = state["fuel_call"]
                if check["call"] is None:
                    print("[walker] no fuel call could be made (%s): FAILED" % state["car_note"], flush=True)
                    os._exit(1)
            return
        state["car_controls"] = hovercar.CarControls()
        call = check["call"]
        if state["fuel_call"] is None or world.time > call["due"] + FUEL_WAIT:
            kg = min(car.params.fuel_capacity, (check["credits_before"] - call["fee"]) / call["price"])
            spent = check["credits_before"] - state["market"].credits
            fuel_ok = (check["dry_moved"] < 1.0 and state["fuel_call"] is None and abs(car.fuel - kg) < 1e-6
                       and abs(spent - (call["fee"] + kg * call["price"])) < 1e-6)
            print("[walker] the car dry moved %.2f m at full throttle; fuel called from %s (%.2f cr/kg, a %.0f cr "
                  "fee) came: %.1f kg for %.0f cr: %s"
                  % (check["dry_moved"], call["outpost"].name, call["price"], call["fee"], car.fuel, spent,
                     "passed" if fuel_ok else "FAILED"), flush=True)
            check["ok"] = check["ok"] and fuel_ok
            check["stage"], check["time"] = "night", 0.0
    elif stage == "night":
        state["car_controls"] = hovercar.CarControls()
        if "hour" not in check:
            check["hour"] = clock.hour(day_time())
            state["day_offset"] = clock.time_of(NIGHT_CHECK_HOUR, day_time()) - world.time
        if t >= NIGHT_SECONDS:
            night_ok = (abs(world.exposure_scale - clock.NIGHT_SCALE) < 1e-6 and world.lights_drawn >= 2
                        and abs(clock.hour(day_time()) - NIGHT_CHECK_HOUR) < 0.1)
            print("[walker] the day's hour %s, then night at %s: the exposure x%.1f, %d lights drawn: %s"
                  % ("%05.2f" % check["hour"], hour_text(), world.exposure_scale, world.lights_drawn,
                     "passed" if night_ok else "FAILED"), flush=True)
            check["ok"] = check["ok"] and night_ok
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                screenshot(base + "_night" + ext)
            os._exit(0 if check["ok"] else 1)


def check_poi(kind, n, from_feet=None):
    """--check: a point of interest of `kind` put beside the player (its
    first spot at the feet, or `from_feet` (east, north) metres from them),
    measured, of quartz; its key off the faces (face -1, `n`)."""
    feet = foot.bubble.to_world(foot.player.feet())
    up = normalize(planet.relative(feet))
    east, north = local_frame(up)
    offset = from_feet if from_feet is not None else (
        (-pois.SPOTS[kind][0][0][0], -pois.SPOTS[kind][0][0][2]) if kind in pois.SPOTS else (0.0, 0.0))
    point = add(feet, add(scale(east, offset[0]), scale(north, offset[1])))
    relative = planet.relative(point)
    key = (planet.index, -1, n, 0)
    poi = pois.Poi(key, normalize(relative), 0.0, measured=True, height=length(relative) - planet.radius,
                   climate=(10.0, 0.5), ore=0.5, resource=resources.QUARTZ, kind=kind, valid=True)
    state["pois"].pois[key] = poi
    return poi


def run_poi_check(check, t):
    """--check's points of interest, on Base's pad, a step at a time (see
    run_check); True once done."""
    steps = ["natural", "beacon", "wreck", "rig", "arch", "geode", "pod", "survey", "buy"]
    c = check.setdefault("poi", {"step": 0, "t0": t, "lines": [], "ok": True,
                                 "props": {id(p) for p in foot.props.all}})
    if c["step"] >= len(steps):
        # The test's points (off the faces) and the props they made taken away again.
        for key in [k for k in state["pois"].pois if k[1] < 0]:
            del state["pois"].pois[key]
            for entity, _ in state["poi_scenes"].pop(key, []):
                engine.scene.destroy_entity(entity)
        for prop in [p for p in foot.props.all if id(p) not in c["props"]]:
            foot.props.remove(foot.bubble, prop)
        state["poi_cleared"] = None
        print("[walker] points of interest:\n" + "\n".join("[walker]   " + line for line in c["lines"])
              + "\n[walker]   %s" % ("passed" if c["ok"] else "FAILED"), flush=True)
        check["sell_ok"] = check["sell_ok"] and c["ok"]
        return True
    step, since = steps[c["step"]], t - c["t0"]

    def done(ok, line):
        c["ok"] = c["ok"] and ok
        c["lines"].append("%s: %s" % (line, "ok" if ok else "FAILED"))
        c["step"] += 1
        c["t0"] = t
        c.pop("poi", None)
        state["check_use"] = False

    p = state["pois"]
    if step == "natural":
        near = [q for q in p.pois.values() if q.key[1] >= 0]
        if p.queue and since < POI_MEASURE_SECONDS:
            return False
        found = sorted(q.name for q in p.found() if q.key[1] >= 0)
        done(near and not p.queue, "%d candidates within %s measured, found %s"
             % (len(near), distance_text(POI_REACH), ", ".join(found) or "none"))
    elif step in ("beacon", "wreck", "rig", "arch"):
        if "poi" not in c:
            c["poi"] = check_poi(step, c["step"])
            c["before"] = (state["survey_data"], dict(state["parts"]), len(foot.props.rocks), len(state["map"].revealing))
            return False
        poi = c["poi"]
        state["check_use"] = True
        hold = pois.SPOTS[step][0][2]
        if 0 not in poi.done and since < hold + USE_CHECK_SECONDS:
            return False
        survey, parts, rocks, revealing = c["before"]
        gained = state["survey_data"] - survey
        new_parts = {k: v - parts.get(k, 0) for k, v in state["parts"].items() if v != parts.get(k, 0)}
        ok = 0 in poi.done and poi.discovered
        if step in ("beacon", "arch"):
            ok = ok and abs(gained - pois.survey_value(poi)) < 1e-6
            if step == "beacon":
                ok = ok and len(state["map"].revealing) > revealing
        else:
            ok = ok and sum(new_parts.values()) == 1 and since >= hold
            if step == "rig":
                ok = ok and len(foot.props.rocks) == rocks + pois.RIG_ROCKS
        extra = ""
        if step == "beacon":
            extra = ", %d map cells queued" % (len(state["map"].revealing) - revealing)
        elif step == "rig":
            extra = ", %d ore rocks more" % (len(foot.props.rocks) - rocks)
        done(ok, "%s used after %.1f s: survey data +%.0f cr, parts %s%s"
             % (poi.name, since, gained, upgrades.parts_text(new_parts) or "none", extra))
    elif step == "geode":
        if "poi" not in c:
            c["poi"] = check_poi("geode", c["step"], from_feet=(60.0, 0.0))
            c["before"] = len(foot.props.rocks)
            return False
        poi = c["poi"]
        if not poi.done and since < USE_CHECK_SECONDS:
            return False
        rocks = [r for r in foot.props.rocks[c["before"]:]]
        ok = (poi.looted and len(rocks) == pois.GEODE_ROCKS
              and all(r.grade == pois.GEODE_GRADE and r.resource == resources.QUARTZ for r in rocks))
        done(ok, "geode field: %d rocks of quartz at grade %.2f" % (len(rocks), rocks[0].grade if rocks else 0.0))
    elif step == "pod":
        if "poi" not in c:
            c["poi"] = check_poi("pod", c["step"], from_feet=(6.0, 0.0))
            c["sold"] = state["pods_sold"]
            return False
        poi = c["poi"]
        if not poi.looted and since < POD_CHECK_SECONDS:
            return False
        done(poi.looted and state["pods_sold"] == c["sold"] + 1,
             "cargo pod made on Base's pad, at rest sold for %.0f cr after %.1f s" % (pois.POD_VALUE, since))
    elif step == "survey":
        held, credits = state["survey_data"], state["market"].credits
        outpost = at_outpost()
        expected = trade.survey_sale(outpost, held, world.time)[0] if outpost is not None else 0.0
        sell_survey()
        gained = state["market"].credits - credits
        done(outpost is not None and held > 0.0 and state["survey_data"] == 0.0 and abs(gained - expected) < 1e-6,
             "survey data worth %.0f cr sold at %s for %.0f cr" % (held, outpost.name if outpost else "-", gained))
    elif step == "buy":
        bank, liner = upgrades.BY_KEY["gun_charge_1"], upgrades.BY_KEY["suit_1"]
        coils = state["parts"].get("coil", 0)
        cold = state["suit_params"].cold_limit
        buy_upgrade(bank)
        bank_ok = ("gun_charge_1" in state["owned"]) == (coils >= 1)
        buy_upgrade(liner)
        liner_ok = "suit_1" in state["owned"] and abs(state["suit_params"].cold_limit - (cold - 10.0)) < 1e-9
        done(bank_ok and liner_ok, "Capacitor bank with %d coil%s: %s; Thermal liner: the suit's cold limit %.0f C"
             % (coils, "" if coils == 1 else "s", "bought" if "gun_charge_1" in state["owned"] else "refused",
                state["suit_params"].cold_limit))
    return False


def run_contract_check(check, t):
    """--check's contracts, on Base's pad, a step at a time (see
    run_check); True once done. Each but the offers is a contract of Base
    made for the step (check_contract) and accepted there."""
    steps = ["offers", "delivery", "courier", "fragile", "recovery", "survey", "expire", "abandon", "keep"]
    c = check.setdefault("contracts", {"step": 0, "t0": t, "lines": [], "ok": True})
    home, car = state["home"], state["car"]
    if c["step"] >= len(steps):
        rep = state["reputation"].get(home.name, 0.0)
        ok = c["ok"] and rep == 1.0 and home.reputation == 1.0
        print("[walker] contracts:\n" + "\n".join("[walker]   " + line for line in c["lines"])
              + "\n[walker]   Base's reputation %+.0f, its prices x%.2f: %s"
              % (rep, 1.0 + trade.REPUTATION_PRICE * home.reputation, "passed" if ok else "FAILED"), flush=True)
        check["sell_ok"] = check["sell_ok"] and ok
        return True
    step, since = steps[c["step"]], t - c["t0"]

    def done(ok, line):
        c["ok"] = c["ok"] and ok
        c["lines"].append("%s: %s" % (line, "ok" if ok else "FAILED"))
        c["step"] += 1
        c["t0"] = t
        c.pop("contract", None)

    def begin(contract):
        c["contract"] = contract
        c["before"] = (state["market"].credits, state["reputation"].get(home.name, 0.0), state["sold_kg"])
        return accept_contract(contract)

    def ended(contract, how):
        credits, rep, _ = c["before"]
        paid = state["market"].credits - credits
        return (contract.state == how and contract not in state["contracts"]
                and abs(paid - (contract.reward if how == "completed" else 0.0)) < 1e-6
                and state["reputation"].get(home.name, 0.0) == rep + (1.0 if how == "completed" else -1.0))

    feet = foot.bubble.to_world(foot.player.feet())
    up = normalize(planet.relative(feet))
    east, north = local_frame(up)
    if step == "offers":
        first, again = contract_offers(home), contract_offers(home)
        ok = (at_outpost() is home and len(first) == contracts.OFFERS
              and [contracts.record(o) for o in first] == [contracts.record(o) for o in again])
        done(ok, "%d offers at %s on day %d, the same when asked again: %s"
             % (len(first), home.name, contract_day(), ", ".join("%s %.0f cr" % (o.kind, o.reward) for o in first)))
    elif step == "delivery":
        if "contract" not in c:
            rock = ore_rock(add(world.site_ground(), scale(up, 1.0)), CHECK_ORE_EXTENT, CHECK_ORE_GRADE)
            c["rock"] = rock
            begin(check_contract("delivery", c["step"], resource=resources.FERRITE, quantity=rock.mass,
                                 min_grade=0.5))
            return False
        contract = c["contract"]
        if contract.state == "active" and since < CONTRACT_CHECK_SECONDS:
            return False
        done(ended(contract, "completed") and c["rock"] not in foot.props.rocks and state["sold_kg"] == c["before"][2],
             "delivery of %.0f kg of ferrite: the rock on the pad delivered, not sold, after %.1f s, +%.0f cr"
             % (contract.quantity, since, contract.reward))
    elif step == "courier":
        if "contract" not in c:
            contract = c["contract"] = check_contract("courier", c["step"])
            c["accepted"] = car_outpost() is home and begin(contract)
            c["in_hold"] = any(i.get("contract") == contract.id for i in car.cargo)
            return False
        contract = c["contract"]
        if contract.state == "active" and since < CONTRACT_CHECK_SECONDS:
            return False
        done(c["accepted"] and c["in_hold"] and ended(contract, "completed")
             and not any("contract" in i for i in car.cargo),
             "courier accepted with the car on the pad: its crate into the hold, delivered there after %.1f s" % since)
    elif step == "fragile":
        if "contract" not in c:
            c["parked_at"] = car.position
            car.position = add(car.position, scale(car.heading, NET_CHECK_AWAY))
            update_ship()
            begin(check_contract("courier", c["step"], destination="Elsewhere"))
            return False
        contract = c["contract"]
        crate = next((p for p in foot.props.crates if state["couriers"].get(id(p)) == contract.id), None)
        if "thrown" not in c and crate is not None and crate.body is not None and since >= 0.5:
            p, q = foot.physics.get_transform(crate.body)
            lift = foot.bubble.up(p)
            foot.physics.set_transform(crate.body, add(p, scale(lift, 3.0)), q)
            foot.physics.set_velocity(crate.body, scale(lift, -CONTRACT_CHECK_DROP), (0.0, 0.0, 0.0))
            c["thrown"] = True
            return False
        if contract.state == "active" and since < CONTRACT_CHECK_SECONDS:
            return False
        done("thrown" in c and ended(contract, "failed") and crate is None and contract.damage >= 1.0,
             "courier's crate made beside the player and thrown down at %.0f m/s: damage %.2f, broken, failed"
             % (CONTRACT_CHECK_DROP, contract.damage))
    elif step == "recovery":
        if "contract" not in c:
            target = normalize(planet.relative(add(feet, scale(east, 6.0))))
            begin(check_contract("recovery", c["step"], target=target))
            return False
        contract = c["contract"]
        if contract.state == "active" and since < CONTRACT_CHECK_SECONDS:
            return False
        done(ended(contract, "completed") and pod_key(contract) not in state["pois"].pois,
             "recovery: the pod made at its point on the pad and, settled, delivered after %.1f s" % since)
    elif step == "survey":
        if "contract" not in c:
            begin(check_contract("survey", c["step"], target=up))
            c["queued"] = state["map"].reveal(planet, up, contracts.SURVEY_RADIUS)
            return False
        contract = c["contract"]
        if contract.state == "active" and since < CONTRACT_CHECK_SECONDS:
            return False
        done(ended(contract, "completed") and contract.explored >= contracts.SURVEY_SHARE,
             "survey around the feet (%d cells queued): %d %% explored after %.1f s"
             % (c["queued"], round(100.0 * contract.explored), since))
    elif step == "expire":
        if "contract" not in c:
            begin(check_contract("survey", c["step"], target=up, deadline=contract_day() - 1))
            return False
        contract = c["contract"]
        done(ended(contract, "expired"), "a contract past its deadline expired")
    elif step == "abandon":
        contract = check_contract("survey", c["step"], target=normalize(planet.relative(add(feet,
                                                                                           scale(north, 2_000.0)))))
        begin(contract)
        pin_contract(contract)
        abandon_pinned()
        done(ended(contract, "abandoned"), "a pinned contract abandoned")
    elif step == "keep":
        if "contract" not in c:
            begin(check_contract("survey", c["step"], target=normalize(planet.relative(add(feet,
                                                                                          scale(east, 2_000.0))))))
            car.position = c["parked_at"]
            update_ship()
            return False
        contract = c["contract"]
        if since < 0.5:
            return False
        card = state["hud"].card
        marked = any(name == "Contract" for name, _, _ in markers(planet))
        done(contract in state["contracts"] and state["pinned"] == contract.id and card[0].startswith("CONTRACT")
             and card[1] == contract_title(contract) and marked,
             "a survey 2 km off kept active and pinned: on the card (%s) and the compass" % card[0])
    return False


def run_shower_check(check, t):
    """--check's meteor shower by Base (see run_check); True once done."""
    s = check.setdefault("shower", {"step": "plan"})
    player = foot.player
    step = s["step"]
    if step == "plan":
        feet = foot.bubble.to_world(player.feet())
        here = normalize(planet.relative(feet))
        centre = events.along_ground(here, 90.0, SHOWER_CHECK_CENTRE, planet.radius)
        pad = state["home"].direction
        out = bearing(pad, sub(here, pad))
        if out is None or events.ground_distance(pad, here, planet.radius) < 1.0:
            out = bearing(here, player.heading) or 0.0
        ahead = events.along_ground(pad, out, outposts.PAD_REACH + SHOWER_CHECK_OFF_PAD, planet.radius)
        now = world.time + SHOWER_CHECK_LEAD
        large = events.Impact(now, centre, SHOWER_CHECK_MASS, 0.6, 4_000.0, 60.0, 0.0,
                              events.crater_of(SHOWER_CHECK_MASS))
        small = events.Impact(now + 1.0, ahead, SHOWER_CHECK_ROCK, 0.6, 4_000.0, 60.0, 0.0)
        set_shower(events.Shower(now, centre, [large, small]))
        s.update(step="fly", craters=list(world.craters), large=large, small=small, heads=0, flash=0.0,
                 shake=0.0, plumes=0, natural=None, t0=t)
        return False
    large, small = s["large"], s["small"]
    if step == "fly":
        s["heads"] = max(s["heads"], state["heads_drawn"])
        tint = flash_tint()
        s["flash"] = max(s["flash"], tint[1] if tint is not None else 0.0)
        if state["shake"] is not None:
            s["shake"] = max(s["shake"], state["shake"][1])
        s["plumes"] = max(s["plumes"], len(state["plumes"]))
        for toast in state["hud"].toasts:
            for key, start in (("warned", "Meteor shower inbound"), ("down", "Meteorite down")):
                if toast[0].startswith(start):
                    s[key] = True
        if s["natural"] is None and 0 in state["impact_heights"]:
            s["natural"] = state["impact_heights"][0]
        up = player.up()
        path, before = state["paths"].get(0), large.time - world.time
        aim = None
        if args.shot and path is not None and 2.0 < before <= min(5.0, path.duration):
            aim = add(planet.centre(), path.at(before))
        elif args.shot and -5.0 < before <= 2.0:
            aim = add(planet.centre(), scale(large.direction, planet.radius + (s["natural"] or 0.0)))
        if aim is not None:
            to = sub(foot.bubble.to_bubble(aim), player.eye())
            player.heading = player.level(to, up)
            player.pitch = math.asin(max(-1.0, min(1.0, dot(normalize(to), up))))
        if args.shot and before <= 3.0 and "meteor_shot" not in s and state["heads_drawn"] > 0:
            s["meteor_shot"] = True
            base, ext = os.path.splitext(args.shot)
            screenshot(base + "_meteor" + ext)
        if args.shot and before <= -0.2 and "impact_shot" not in s:
            s["impact_shot"] = True
            base, ext = os.path.splitext(args.shot)
            screenshot(base + "_impact" + ext)
        if args.shot and before <= -4.0 and "plume_shot" not in s:
            s["plume_shot"] = True
            base, ext = os.path.splitext(args.shot)
            screenshot(base + "_plume" + ext)
        if state["shower"] is None and world.time >= small.time + SHOWER_LAND_SECONDS:
            s["step"], s["t0"] = "measure", t
        elif t - s["t0"] > SHOWER_CHECK_LEAD + 30.0:
            print("[walker] the shower did not strike in %.0f s: FAILED" % (t - s["t0"]), flush=True)
            os._exit(1)
        return False
    if step == "measure":
        meteorite = state["meteorite"]
        rock = next((p for p in foot.props.rocks if p.resource == resources.IRIDIUM and p is not meteorite), None)
        dug = [c for c in world.craters if c not in s["craters"]]
        crater_ok = (len(dug) == 1 and abs(dug[0].radius - large.crater[0]) < 1e-9
                     and length(sub(dug[0].direction, large.direction)) < 1e-9)
        lies = None
        if meteorite is not None:
            body, position, _ = foot.prop_state(meteorite)
            lies = length(position) - planet.radius - meteorite.rest
        floor = (s["natural"] or 0.0) - large.crater[1]
        meteorite_ok = (meteorite is not None and meteorite.resource == resources.IRIDIUM
                        and abs(meteorite.mass - large.mass) < 1.0 and settled(meteorite)
                        and lies is not None and abs(lies - floor) < 1.0 and id(meteorite) in state["hot"])
        rock_ok = rock is not None and abs(rock.mass - small.mass) < 0.5 and id(rock) in state["hot"]
        warned, down = s.get("warned", False), s.get("down", False)
        seen_ok = (s["heads"] > 0 and s["flash"] > 0.0 and s["shake"] > 0.0 and s["plumes"] >= PLUME_PUFFS
                   and warned and down)
        s["lines"] = [
            "the crater dug, %.0f m across: %s" % (2.0 * large.crater[0], "ok" if crater_ok else "FAILED"),
            "the %.0f kg meteorite of iridium hot, at rest %s m from the crater's floor: %s"
            % (meteorite.mass if meteorite is not None else 0.0,
               "%.2f" % (lies - floor) if lies is not None else "-", "ok" if meteorite_ok else "FAILED"),
            "a %.0f kg rock of iridium hot off the pad: %s" % (rock.mass if rock is not None else 0.0,
                                                          "ok" if rock_ok else "FAILED"),
            "warned and told, %d meteor%s drawn at once, a flash of %.2f, a shake of %.2f degrees, %d puffs: %s"
            % (s["heads"], "" if s["heads"] == 1 else "s", s["flash"], math.degrees(s["shake"]), s["plumes"],
               "ok" if seen_ok else "FAILED")]
        s["ok"] = crater_ok and meteorite_ok and rock_ok and seen_ok
        s["rock"] = rock
        s["step"], s["t0"] = "approach", t
        return False
    rock = s["rock"]
    if step == "approach":
        if rock is None:
            s["step"] = "done"
            return False
        body, position, _ = foot.prop_state(rock)
        to = sub(add(body.centre(), position), foot.bubble.to_world(player.feet()))
        up = player.up()
        along = sub(to, scale(up, dot(to, up)))
        if length(along) <= SHOWER_CHECK_NEAR or t - s["t0"] > 15.0:
            foot.controls.forward = 0.0
            s["step"], s["t0"] = "too_hot", t
        else:
            player.heading = player.level(along, up)
            foot.controls.forward = 1.0
        return False
    if step == "too_hot":
        if rock.body is None:
            if t - s["t0"] > 3.0:
                s["lines"].append("the rock came into the physics: FAILED")
                s["ok"], s["step"] = False, "done"
            return False
        to = sub(foot.physics.get_transform(rock.body)[0], player.eye())
        up = player.up()
        player.heading = player.level(to, up)
        player.pitch = math.asin(max(-1.0, min(1.0, dot(normalize(to), up))))
        foot.gun_controls.grab = True
        if t - s["t0"] >= 0.5:
            status, held = foot.gun.status, foot.gun.held
            ok = status == "too_hot" and held is None
            s["lines"].append("the gun refuses the hot rock (%s): %s" % (status, "ok" if ok else "FAILED"))
            s["ok"] = s["ok"] and ok
            del state["hot"][id(rock)]
            s["step"], s["t0"] = "cooled", t
        return False
    if step == "cooled":
        foot.gun_controls.grab = True
        if t - s["t0"] >= 1.0:
            ok = foot.gun.held == rock.body
            s["lines"].append("the gun grabs it cooled: %s" % ("ok" if ok else "FAILED"))
            s["ok"] = s["ok"] and ok
            foot.gun_controls.grab = False
            foot.gun.drop()
            s["step"] = "done"
        return False
    print("[walker] meteor shower:\n" + "\n".join("[walker]   " + line for line in s["lines"])
          + "\n[walker]   %s" % ("passed" if s["ok"] else "FAILED"), flush=True)
    check["shower_ok"] = s["ok"]
    return True


def check_contract(kind, n, **fields):
    """--check: a contract of `kind` from Base to Base for 100 cr, due in contracts.DEADLINE_DAYS days."""
    home = state["home"].name
    values = dict({"deadline": contract_day() + contracts.DEADLINE_DAYS}, **fields)
    return contracts.Contract("check/%d" % n, kind, home, values.pop("destination", home), 100.0, **values)


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
        body, feet = planet, state["car"].position
    else:
        body = foot.bubble.body
        feet = foot.bubble.body_relative(player.feet())
    explored = state["map"]
    explored.probe(body, feet, EXTRA_PROBES,
                   yield_to=(foot.bubble is not None and bool(foot.props.needs_ground(foot.bubble)))
                   or bool(state["pois"].queue) or shower_waiting())
    probe_pois()
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
    """Ore sold where it rests on a pad (or delivered to a delivery
    contract that wants it there), the pods sold and the contracts' crates
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
    for outpost, prop, item in sale_items(ore, state["outposts"], held):
        if deliver(outpost, item):
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
    sell_pods(held)
    deliver_couriers(held)
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
            ("Ore sold", "%s kg" % format(int(round(state["sold_kg"])), ","))] + suit_rows()
    c = pinned_contract()
    if c is not None:
        k = next(n for n, a in enumerate(state["contracts"]) if a is c)
        fraction = contracts.progress(c) if c.kind in ("delivery", "survey") else None
        hud.objective("CONTRACT  %d / %d" % (k + 1, len(state["contracts"])), contract_title(c), contracts.task(c),
                      fraction, contract_progress_text(c).upper(), rows)
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
         {"stat": "Contracts done / failed", "get": lambda: "%d / %d" % (state["contracts_done"],
                                                                         state["contracts_failed"])},
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


# ── Points of interest ─────────────────────────────────────────────────────

def poi_seed():
    return tuple(planet.terrain["seed"])


def poi_point(poi):
    """A point of interest's point on the ground, planet space."""
    return add(planet.centre(), scale(poi.direction, planet.radius + poi.height))


def poi_at(poi, offset):
    """A point `offset` (east, up, north, metres) from a point of interest's point, planet space."""
    up = poi.direction
    east, north = local_frame(up)
    return add(poi_point(poi), add(add(scale(east, offset[0]), scale(up, offset[1])), scale(north, offset[2])))


def player_here():
    """The player's feet (or the car), planet space, on the planet; None elsewhere or without either."""
    if state["driving"]:
        return car_position()
    if foot.player is None or foot.bubble.body is not planet:
        return None
    return foot.bubble.to_world(foot.player.feet())


def poi_distance(poi, here):
    """Metres along the ground from `here` (planet space) to a point of interest."""
    return planet.radius * math.acos(max(-1.0, min(1.0, dot(poi.direction, normalize(planet.relative(here))))))


def probe_pois():
    """The candidates waiting to be measured (pois.Pois.to_measure) measured
    with the world's extra probes, as the explored map does: only while
    nothing else uses them and no meteor shower's impact waits for its
    ground (shower_waiting); each with its water (PlanetWorld.water_level)."""
    p, asked = state["pois"], state["poi_asked"]
    if world.extra_probes and world.extra_probes != asked:
        return
    if asked is not None:
        if [probe for probe, _ in world.extra_heights] != asked:
            return
        for poi, (_, h), (_, climate), (_, ore), (_, resource) in zip(state["poi_asking"], world.extra_heights,
                                                                       world.extra_climates, world.extra_ores,
                                                                       world.extra_resources):
            p.measured(poi.key, h, climate, ore, resource, world.water_level(poi.direction))
        state["poi_asked"], state["poi_asking"] = None, []
        world.extra_probes = []
    if shower_waiting():
        return
    batch = p.to_measure(EXTRA_PROBES)
    if batch:
        asked = [(poi.direction, planet) for poi in batch]
        world.extra_probes = asked
        state["poi_asked"], state["poi_asking"] = asked, batch


def update_pois(dt):
    """The candidates within POI_REACH looked for once the player has moved
    POI_WANT_MOVE; the found ones discovered within SCAN_RANGE, or within
    SEEN_RANGE in the view's cone; their scenes and props; the use key; and
    the explored map's newly walked cells paid as survey data."""
    explored = state["map"]
    if state["walked_counted"] is None:
        state["walked_counted"] = explored.walked
    if explored.walked > state["walked_counted"]:
        state["survey_data"] += CELL_SURVEY * (explored.walked - state["walked_counted"])
        state["walked_counted"] = explored.walked
    here = player_here()
    if here is None:
        update_poi_scenes(None)
        state["use_spot"] = None
        return
    p = state["pois"]
    direction = normalize(planet.relative(here))
    last = state["poi_want_at"]
    if last is None or planet.radius * math.acos(max(-1.0, min(1.0, dot(last, direction)))) > POI_WANT_MOVE:
        p.want(planet.index, direction, POI_REACH, planet.radius, poi_seed())
        state["poi_want_at"] = direction
    view = view_basis()[2]
    for poi in p.found():
        if poi.discovered:
            continue
        d = poi_distance(poi, here)
        towards = normalize(sub(poi_point(poi), state["position"]))
        if d <= SCAN_RANGE or (d <= SEEN_RANGE and dot(towards, view) >= SEEN_COS):
            poi.discovered = True
            state["hud"].toast("Found: %s, %s" % (poi.name, distance_text(d)), POI_MARKER)
    update_poi_scenes(here)
    make_poi_props(here)
    update_use(dt)


def update_poi_scenes(here):
    """The boxes of the found points within DRAW_RANGE of `here` made
    (pois.LAYOUTS), those beyond removed; the plants and rocks cleared
    around each (pois.CLEAR_RADIUS, PlanetWorld.set_clearings) as the set
    changes."""
    scenes = state["poi_scenes"]
    near = [] if here is None else [poi for poi in state["pois"].found() if poi_distance(poi, here) <= DRAW_RANGE]
    keys = sorted(poi.key for poi in near)
    if keys != state["poi_cleared"]:
        state["poi_cleared"] = keys
        nearest = sorted(near, key=lambda poi: poi_distance(poi, here))[:planet_world.MAX_CLEARINGS]
        world.set_clearings([planet_world.Clearing(planet.index, poi.direction, radius=pois.CLEAR_RADIUS[poi.kind],
                                                   edge=pois.CLEAR_EDGE,
                                                   clears=planet_world.CLEAR_PLANTS | planet_world.CLEAR_ROCKS)
                             for poi in nearest])
    wanted = {poi.key: poi for poi in near if pois.LAYOUTS.get(poi.kind)}
    for key in [k for k in scenes if k not in wanted]:
        for entity, _ in scenes.pop(key):
            engine.scene.destroy_entity(entity)
    positions, normals = box()
    for key, poi in wanted.items():
        if key in scenes:
            continue
        up = poi.direction
        east, north = local_frame(up)
        rotation = euler_from_basis(east, up, scale(north, -1.0))
        pieces = []
        for offset, size, colour in pois.LAYOUTS[poi.kind]:
            entity = engine.create_mesh(positions, normals=normals, name="Poi")
            engine.scene.set_rotation(entity, rotation)
            engine.scene.set_scale(entity, size)
            engine.scene.set_material_vec4(entity, "baseColorFactor", colour + (1.0,))
            engine.scene.set_material_float(entity, "gridStrength", 0.0)
            engine.scene.set_render_layer_mask(entity, 1)
            pieces.append((entity, poi_at(poi, (offset[0], offset[1] - pois.SINK, offset[2]))))
        scenes[key] = pieces


def draw_poi_scenes(camera):
    for pieces in state["poi_scenes"].values():
        for entity, point in pieces:
            engine.scene.set_position(entity, sub(point, camera))


def make_poi_props(here):
    """A pod's crate (a recovery's of its contract's size), and a geode
    field's rocks, made once the player's bubble is within PROP_RANGE of
    their point (each once: `done`)."""
    bubble = foot.bubble
    if bubble is None or bubble.body is not planet:
        return
    for poi in state["pois"].found():
        if poi.kind not in ("pod", "geode") or poi.done or poi.looted or poi_distance(poi, here) > PROP_RANGE:
            continue
        up = poi.direction
        east, north = local_frame(up)
        if poi.kind == "pod":
            size, colour, mass = ((contracts.POD_SIZE, contracts.POD_COLOUR, contracts.POD_MASS) if poi.fixed_kind
                                  else (pois.POD_SIZE, pois.POD_COLOUR, pois.POD_MASS))
            spot = bubble.to_bubble(poi_at(poi, (0.0, 0.5 * size + 0.3, 0.0)))
            crate = foot.props.spawn_crate(bubble, spot, quat_from_basis(east, up, scale(north, -1.0)), size,
                                           colour=colour, mass=mass)
            state["pods"][id(crate)] = poi.key
        else:
            resource = poi.resource or resources.FERRITE
            for k in range(pois.GEODE_ROCKS):
                angle = 2.0 * math.pi * k / pois.GEODE_ROCKS
                r = pois.GEODE_SPREAD * (0.4 + 0.6 * ((k * 7) % 5) / 4.0)
                rock = ore_rock(poi_at(poi, (r * math.cos(angle), 0.6, r * math.sin(angle))),
                                CHECK_ORE_EXTENT, pois.GEODE_GRADE, resource)
                state["seen"][id(rock)] = (planet.name, poi.direction)
            poi.looted = True
        poi.done = [0]


def usable_spot():
    """(poi, spot index, distance) of the nearest spot (pois.SPOTS) not used
    yet within its reach of the player's feet, on foot; or None."""
    if foot.player is None or foot.bubble.body is not planet:
        return None
    feet = foot.bubble.to_world(foot.player.feet())
    best = None
    for poi in state["pois"].found():
        if poi.looted or poi.kind not in pois.SPOTS or poi_distance(poi, feet) > USE_LOOK:
            continue
        for k, (offset, reach, _) in enumerate(pois.SPOTS[poi.kind]):
            if k in poi.done:
                continue
            d = length(sub(poi_at(poi, offset), feet))
            if d <= reach and (best is None or d < best[2]):
                best = (poi, k, d)
    return best


def update_use(dt):
    """The use key (E, or --check's) held on the usable spot for its time
    (pois.SPOTS) uses it (use_spot); let go between uses."""
    spot = state["use_spot"] = usable_spot()
    held = engine.input.is_key_down(keys.E) or state["check_use"]
    if not held:
        state["use_latch"] = False
    if spot is None or not held or state["use_latch"]:
        state["use_progress"] = 0.0
        return
    poi, k, _ = spot
    state["use_progress"] += dt
    if state["use_progress"] >= pois.SPOTS[poi.kind][k][2]:
        state["use_progress"] = 0.0
        state["use_latch"] = True
        use_spot(poi, k)


def use_spot(poi, k):
    """Spot `k` of `poi` used: a beacon reveals the map around it
    (ExploredMap.reveal) and gives survey data; a wreck's spot a part; a
    rig's crate RIG_ROCKS ore rocks and a part; an arch survey data. The
    point is looted once every spot is used."""
    poi.done.append(k)
    poi.discovered = True
    hud = state["hud"]
    if poi.kind in ("beacon", "arch"):
        value = pois.survey_value(poi)
        state["survey_data"] += value
        if poi.kind == "beacon":
            cells = state["map"].reveal(planet, poi.direction, pois.REVEAL)
            hud.toast("Beacon activated: the map within %s revealed (%d cells), survey data worth %.0f cr"
                      % (distance_text(pois.REVEAL), cells, value), GOOD_TOAST)
        else:
            hud.toast("Arch scanned: survey data worth %.0f cr" % value, GOOD_TOAST)
    else:
        part = pois.parts_for(poi, k)
        state["parts"][part] = state["parts"].get(part, 0) + 1
        text = "%s: a %s" % (poi.name, pois.PART_NAMES[part].lower())
        if poi.kind == "rig":
            offset = pois.SPOTS["rig"][k][0]
            for n in range(pois.RIG_ROCKS):
                rock = ore_rock(poi_at(poi, (offset[0] + 0.6 * n, 1.6, offset[2])), CHECK_ORE_EXTENT,
                                pois.RIG_GRADE, poi.resource or resources.FERRITE)
                state["seen"][id(rock)] = (planet.name, poi.direction)
            text += " and %d ore rocks" % pois.RIG_ROCKS
        hud.toast(text, GOOD_TOAST)
    if len(poi.done) == len(pois.SPOTS[poi.kind]):
        poi.looted = True


def pad_under(crate, held):
    """The outpost on whose pad `crate` has settled (slower than
    objectives.REST_SPEED), not `held`; or None."""
    if crate is held:
        return None
    body, position, velocity = foot.prop_state(crate)
    if not settled(crate) or length(velocity) >= REST_SPEED:
        return None
    return next((o for o in state["outposts"] if o.body is body and o.at(add(body.centre(), position))), None)


def sell_pods(held):
    """Each cargo pod's crate settled on an outpost's pad (pad_under), not
    `held`, sold for pois.POD_VALUE, its point looted; a recovery's pod
    only on its contract's destination's pad, which completes it."""
    for crate in list(foot.props.crates):
        key = state["pods"].get(id(crate))
        if key is None:
            continue
        outpost = pad_under(crate, held)
        if outpost is None:
            continue
        c = crate_contract(crate)
        if c is not None:
            if outpost.name == c.destination:
                forget_crate(crate)
                foot.props.remove(foot.bubble, crate, release=False)
                complete_contract(c)
            continue
        state["market"].credits += pois.POD_VALUE
        state["earned"] += pois.POD_VALUE
        state["pods_sold"] += 1
        del state["pods"][id(crate)]
        foot.props.remove(foot.bubble, crate, release=False)
        poi = state["pois"].pois.get(key)
        if poi is not None:
            poi.looted = True
        state["hud"].toast("Cargo pod delivered to %s: +%s cr" % (outpost.name, format(int(pois.POD_VALUE), ",")),
                           GOOD_TOAST)


def poi_markers(body):
    """The compass's and the map's markers of the points of interest on
    `body` (the planet): each discovered by its name (greyed once looted),
    each found but not discovered within POI_REACH as a signal."""
    here = player_here()
    if body is not planet or here is None:
        return []
    found = []
    for poi in state["pois"].found():
        if poi.discovered:
            found.append((poi.name, poi_point(poi), LOOTED_MARKER if poi.looted else POI_MARKER))
        elif poi_distance(poi, here) <= POI_REACH:
            found.append(("Signal", poi_point(poi), SIGNAL_MARKER))
    return found


def car_outpost():
    """The outpost whose pad the car is on, or None."""
    car = state["car"]
    if car is None:
        return None
    return next((o for o in state["outposts"] if o.at(car_position())), None)


def fuel_offer():
    """(the nearest outpost on the planet, metres along the ground to it)
    for a fuel call to the car, off every pad and on its cushion
    (hovercar.LEAVE_HEIGHT), or None."""
    car = state["car"]
    if car is None or car_outpost() is not None or car.height is None or car.height > hovercar.LEAVE_HEIGHT:
        return None
    here = normalize(car.position)
    found = [(o, outposts.ground_distance(planet.radius, o.direction, here))
             for o in state["outposts"] if o.body is planet]
    return min(found, key=lambda f: f[1]) if found else None


def car_note(text, colour=GOOD_TOAST):
    state["car_note"] = text
    state["hud"].toast(text[0].upper() + text[1:], colour)


def fill_tank():
    """The car's tank filled at the outpost whose pad it is on, as far as the credits go."""
    outpost, car = car_outpost(), state["car"]
    if outpost is None:
        car_note("the car is not on an outpost's pad", hud_module.WARNING)
        return
    kg, cost = state["market"].buy_fuel(car, outpost)
    if kg > 0.5:
        car_note("bought %.0f kg of fuel at %s for %s cr" % (kg, outpost.name, format(int(round(cost)), ",")))
    elif car.fuel >= car.params.fuel_capacity - 0.5:
        car_note("the tank is full")
    else:
        car_note("no credits for fuel", hud_module.WARNING)


def call_fuel():
    """Fuel called to the car from the nearest outpost (fuel_offer,
    trade.fuel_call), arriving after its delay and paid then."""
    car = state["car"]
    if state["fuel_call"] is not None:
        car_note("fuel is on its way")
        return
    offer = fuel_offer()
    if offer is None:
        car_note("fuel can be called to the car off the pads, on its cushion", hud_module.WARNING)
        return
    if car.fuel >= car.params.fuel_capacity - 0.5:
        car_note("the tank is full")
        return
    outpost, distance = offer
    price, fee, delay = trade.fuel_call(outpost, distance, False)
    if state["market"].credits <= fee:
        car_note("no credits for a fuel call", hud_module.WARNING)
        return
    state["fuel_call"] = {"outpost": outpost, "price": price, "fee": fee, "due": world.time + delay,
                          "spot": car.position}
    car_note("fuel called from %s, arriving in %.0f s" % (outpost.name, delay))


def update_fuel_call():
    """The called fuel put in the car once it is due, if the car is still
    within CALL_REACH of where it was called to; otherwise the call is off,
    unpaid. A toast when the car runs dry."""
    car = state["car"]
    if car is not None:
        if car.dry() and not state["was_dry"]:
            state["hud"].toast("The car is out of fuel: call fuel from the menu (Escape, Car)", hud_module.WARNING)
        state["was_dry"] = car.dry()
    call = state["fuel_call"]
    if call is None or world.time < call["due"]:
        return
    state["fuel_call"] = None
    if car is None or length(sub(car.position, call["spot"])) > CALL_REACH:
        car_note("the fuel call missed the car", hud_module.WARNING)
        return
    kg, cost = state["market"].call_fuel(car, call["price"], call["fee"])
    if kg > 0.0:
        car_note("fuel delivered from %s: %.0f kg for %s cr" % (call["outpost"].name, kg,
                                                              format(int(round(cost)), ",")))
    else:
        car_note("no credits for the called fuel", hud_module.WARNING)


def car_items():
    """The Car tab: its fuel, where it is, filling the tank on a pad and calling fuel off them."""
    def fuel():
        car = state["car"]
        return "-" if car is None else "%.0f / %.0f kg" % (car.fuel, car.params.fuel_capacity)

    def where():
        outpost = car_outpost()
        if outpost is not None:
            return "on %s's pad" % outpost.name
        offer = fuel_offer()
        return "%.1f km from %s" % (offer[1] / 1000.0, offer[0].name) if offer is not None else "in the air"

    def quote():
        call = state["fuel_call"]
        if call is not None:
            return "Fuel from %s arrives in %.0f s." % (call["outpost"].name, max(0.0, call["due"] - world.time))
        offer = fuel_offer()
        if offer is None:
            return ""
        price, fee, delay = trade.fuel_call(offer[0], offer[1], False)
        return "A call costs %.2f cr/kg and a %s cr fee, and takes %.0f s." % (price, format(int(round(fee)), ","),
                                                                              delay)
    return [
        {"heading": "Car"},
        {"stat": "Fuel", "get": fuel},
        {"stat": "Cargo", "get": cargo_text},
        {"stat": "Where", "get": where},
        {"stat": "Credits", "get": lambda: "%s cr" % format(int(round(state["market"].credits)), ",")},
        {"button": "Fill the tank", "do": fill_tank,
         "hint": "With the car on an outpost's pad: its tank filled at the outpost's fuel price."},
        {"button": "Call fuel", "do": call_fuel,
         "hint": "With the car off the pads, on its cushion: fuel flown in from the nearest outpost, at %d times "
                 "its price and a fee by the distance, paid when it arrives." % round(trade.CALL_FACTOR)},
        {"note": quote},
        {"note": lambda: state["car_note"]},
    ]


def cargo_text():
    car = state["car"]
    if car is None or not car.cargo:
        return "empty"
    n = len(car.cargo)
    return "%.0f kg, %.2f / %.1f m3, %d piece%s" % (car.cargo_mass, car.cargo_volume, car.params.hold_m3, n,
                                                     "" if n == 1 else "s")


def hold_ore():
    """The ore in the car's hold: its items but the contracts' crates."""
    car = state["car"]
    return [] if car is None else [item for item in car.cargo if "contract" not in item]


def sell_car_cargo():
    """The ore in the car's hold at the outpost whose pad it is on: each
    item a delivery contract wants there delivered (`deliver`), the rest
    sold (trade.sale); the contracts' crates stay."""
    outpost, car = car_outpost(), state["car"]
    if outpost is None:
        state["trade_note"] = "the car is not on an outpost's pad"
        state["hud"].toast("The car is not on an outpost's pad", hud_module.WARNING)
        return
    ore = [item for item in hold_ore() if not deliver(outpost, item)]
    car.cargo = [item for item in car.cargo if "contract" in item]
    if not ore:
        state["trade_note"] = "no ore to sell"
        return
    credits, supplies = sale(outpost, ore, world.time)
    outpost.supply.update(supplies)
    state["market"].credits += credits
    kg = sum(item["mass"] for item in ore)
    state["sold_kg"] += kg
    state["earned"] += credits
    state["trade_note"] = "sold %.0f kg of ore at %s for %s cr" % (kg, outpost.name, format(int(round(credits)), ","))
    state["hud"].toast(state["trade_note"][0].upper() + state["trade_note"][1:], GOOD_TOAST)


def outpost_items():
    """The Outpost tab: the outpost whose pad the car is on, its price for
    each resource at grade 1 (iridium from the planet's meteorites), the
    car's cargo, what its ore would fetch, and selling it."""
    def here():
        outpost = car_outpost()
        return "%s, the %s" % (outpost.name, outpost.body.name) if outpost is not None else "the car is not on a pad"

    def price(body, resource):
        def text():
            outpost = car_outpost()
            if outpost is None:
                return "-"
            item = {"body": body.name, "direction": outpost.direction, "mass": 1.0, "grade": 1.0,
                    "resource": resource}
            return "%.2f cr/kg" % trade.price(outpost, item, world.time)
        return text

    def value():
        outpost, ore = car_outpost(), hold_ore()
        if outpost is None or not ore:
            return "-"
        return "%s cr" % format(int(round(state["market"].value(outpost, ore, world.time))), ",")

    planet_resources = range(resources.FERRITE, resources.EVAPORITE + 1)
    moon = next(b for b in bodies if b is not planet)
    prices = ([{"stat": resources.name(r) + ", grade 1", "get": price(planet, r)} for r in planet_resources]
              + [{"stat": resources.name(r) + " (moon), grade 1", "get": price(moon, r)}
                 for r in (resources.ICE, resources.ILMENITE)]
              + [{"stat": "Iridium (meteorites), grade 1", "get": price(planet, resources.IRIDIUM)}])
    return ("columns", [
        [{"heading": "Outpost"},
         {"stat": "Here", "get": here},
         {"stat": "Credits", "get": lambda: "%s cr" % format(int(round(state["market"].credits)), ",")},
         {"stat": "Car's cargo", "get": cargo_text},
         {"stat": "It would fetch", "get": value},
         {"button": "Sell the car's cargo", "do": sell_car_cargo,
          "hint": "With the car on an outpost's pad: the ore in its hold sold there, or delivered to a "
                  "contract that wants it."},
         {"note": lambda: state["trade_note"]}],
        [{"heading": "Prices"}] + prices + [
         {"heading": "Survey data"},
         {"stat": "Held", "get": lambda: "worth %s cr" % format(int(round(state["survey_data"])), ",")},
         {"button": "Sell survey data", "do": sell_survey,
          "hint": "With the car or you on an outpost's pad: the survey data sold there, its price falling with "
                  "the data the outpost has bought."}],
    ])


def sell_survey():
    """The survey data held sold at the outpost whose pad the car or the player is on (trade.Market.sell_survey)."""
    outpost = at_outpost()
    if outpost is None:
        state["trade_note"] = "not on an outpost's pad"
        return
    value = state["survey_data"]
    credits = state["market"].sell_survey(outpost, value, world.time)
    if value <= 0.0:
        state["trade_note"] = "no survey data to sell"
        return
    state["survey_data"] = 0.0
    state["earned"] += credits
    state["trade_note"] = "sold survey data at %s for %s cr" % (outpost.name, format(int(round(credits)), ","))
    state["hud"].toast(state["trade_note"][0].upper() + state["trade_note"][1:], GOOD_TOAST)


def base_params():
    return {"car": hovercar.CarParams(), "gun": GunParams(), "suit": suit.SuitParams()}


def apply_upgrades(owned):
    """The upgrades `owned` (keys) and their effective parameters
    (upgrades.effective, the car's, the gun's and the suit's): the car's,
    the gun's and the suit's now, and those of every car made and every
    gun spawned after."""
    state["owned"] = set(owned)
    state["params"] = upgrades.effective(base_params(), state["owned"])
    state["suit_params"] = state["params"]["suit"]
    foot.gun_params = state["params"]["gun"]
    if foot.gun is not None:
        foot.gun.p = state["params"]["gun"]
    if state["car"] is not None:
        state["car"].params = state["params"]["car"]


def at_outpost():
    """The outpost whose pad the car or the player's feet are on, or None."""
    outpost = car_outpost()
    if outpost is None and foot.player is not None:
        feet = foot.bubble.to_world(foot.player.feet())
        outpost = next((o for o in state["outposts"] if o.at(feet)), None)
    return outpost


def buy_upgrade(upgrade):
    """`upgrade` bought at an outpost (at_outpost) with the credits, when upgrades.refusal allows it."""
    market = state["market"]
    reason = "not at an outpost" if at_outpost() is None else upgrades.refusal(upgrade, state["owned"],
                                                                              market.credits, state["parts"])
    if reason is not None:
        state["upgrade_note"] = "%s: %s" % (upgrade.name, reason)
        state["hud"].toast(state["upgrade_note"], hud_module.WARNING)
        return
    market.credits -= upgrade.cost
    for part, n in upgrade.parts.items():
        state["parts"][part] -= n
    apply_upgrades(state["owned"] | {upgrade.key})
    state["upgrade_note"] = "bought %s for %s cr" % (upgrade.name, format(int(round(upgrade.cost)), ","))
    state["hud"].toast(state["upgrade_note"][0].upper() + state["upgrade_note"][1:], GOOD_TOAST)


def upgrade_text(upgrade):
    """A line on `upgrade`: its cost or state, and each effect from now to after."""
    now = state["params"]
    owned = upgrade.key in state["owned"]
    after = now if owned else upgrades.effective(base_params(), state["owned"] | {upgrade.key})
    changes = []
    for path in upgrades.effects_for(upgrade, now):
        group, attribute = path.split(".")
        name, shown = upgrades.EFFECTS[path]
        value = shown(getattr(now[group], attribute))
        changes.append("%s %s" % (name.lower(), value if owned else "%s to %s" % (value,
                                                                                shown(getattr(after[group],
                                                                                              attribute)))))
    reason = upgrades.refusal(upgrade, state["owned"], state["market"].credits, state["parts"])
    cost = format(int(round(upgrade.cost)), ",") + " cr" + (", " + upgrades.parts_text(upgrade.parts)
                                                            if upgrade.parts else "")
    head = "owned" if owned else cost + ("" if reason is None else " (" + reason + ")")
    return "%s; %s" % (head, "; ".join(changes))


def parts_held():
    held = {p: n for p, n in state["parts"].items() if n > 0}
    return upgrades.parts_text(held) if held else "none"


def upgrade_items():
    """The Upgrades tab: where, the credits and the parts held, and for
    each upgrade the walker offers (WALKER_UPGRADES) a button to buy it and
    a line of what it costs and changes, the gun's and the suit's in one
    column and the car's in another."""
    left = [{"heading": "At an outpost"},
            {"stat": "Where", "get": lambda: at_outpost().name if at_outpost() is not None else "not on a pad"},
            {"stat": "Credits", "get": lambda: "%s cr" % format(int(round(state["market"].credits)), ",")},
            {"stat": "Parts", "get": parts_held},
            {"note": lambda: "" if at_outpost() is not None else "Bring the car or yourself onto a pad."},
            {"note": lambda: state["upgrade_note"]}]

    def column(heading, groups):
        items = [{"heading": heading}]
        for upgrade in WALKER_UPGRADES:
            if not upgrades.effects_for(upgrade, groups):
                continue
            items += [{"button": upgrade.name, "do": lambda u=upgrade: buy_upgrade(u),
                       "hint": "Buy %s at the outpost whose pad the car or you are on%s." % (
                           upgrade.name, "" if not upgrade.requires else
                           "; it needs " + ", ".join(upgrades.BY_KEY[k].name for k in upgrade.requires))},
                      {"note": lambda u=upgrade: upgrade_text(u)}]
        return items
    return ("columns", [left, column("Gun and suit", ("gun", "suit")), column("Car", ("car",))])


# ── Contracts ──────────────────────────────────────────────────────────────

def contract_day():
    """The contracts' day: clock.day of the day's time."""
    return clock.day(day_time())


def contract_offers(outpost):
    """The offers of `outpost` on the planet today (contracts.offers, with
    its reputation), less those accepted; none elsewhere."""
    if outpost is None or outpost.body is not planet:
        return []
    found = contracts.offers((outpost.name, outpost.direction), contract_day(), poi_seed(),
                             [(o.name, o.direction) for o in state["outposts"] if o.body is planet], planet.radius,
                             state["reputation"].get(outpost.name, 0.0))
    return [c for c in found if c.id not in state["offers_taken"]]


def outpost_named(name):
    return next((o for o in state["outposts"] if o.name == name), None)


def active_contract(contract_id):
    return next((c for c in state["contracts"] if c.id == contract_id), None)


def pinned_contract():
    return active_contract(state["pinned"])


def pod_key(c):
    """The key of a recovery's pod's point of interest (pois.Pois.place), off the cube's faces."""
    return (planet.index, -2, zlib.crc32(c.id.encode()), 0)


def place_pod(c, done=False):
    """A recovery's pod as a point of interest at its target, of the pod's kind wherever it is."""
    poi = pois.Poi(pod_key(c), tuple(c.target), 0.0, fixed_kind="pod", done=[0] if done else [])
    state["pois"].place(poi)


def crate_contract(crate):
    """The active contract a crate prop belongs to (a courier's crate or a recovery's pod), or None."""
    contract_id = state["couriers"].get(id(crate))
    if contract_id is not None:
        return active_contract(contract_id)
    key = state["pods"].get(id(crate))
    if key is None:
        return None
    return next((c for c in state["contracts"] if c.kind == "recovery" and pod_key(c) == key), None)


def forget_crate(crate):
    state["couriers"].pop(id(crate), None)
    state["crate_velocity"].pop(id(crate), None)
    state["pods"].pop(id(crate), None)


def crate_item(c, mass, volume):
    """A contract's crate as an item of the car's hold."""
    origin = outpost_named(c.origin)
    return {"body": planet.name, "direction": tuple(origin.direction) if origin is not None else (0.0, 1.0, 0.0),
            "mass": mass, "grade": 0.0, "resource": resources.NONE, "volume": volume, "contract": c.id}


def contract_title(c):
    return "%s for %s" % (c.kind.capitalize(), c.origin)


def hours_left(c):
    """Game hours (clock.py) until the contract's deadline: the dawn after its last day."""
    end = (c.deadline + 1) * clock.DAY_LENGTH - 0.25 * clock.DAY_LENGTH
    return max(0.0, (end - day_time()) * 24.0 / clock.DAY_LENGTH)


def contract_progress_text(c):
    if c.kind == "delivery":
        done = "%.0f / %.0f kg" % (min(c.delivered, c.quantity), c.quantity)
    elif c.kind == "survey":
        done = "%d / %d %%" % (round(100.0 * c.explored), round(100.0 * contracts.SURVEY_SHARE))
    elif c.kind == "courier":
        done = "damage %d %%" % round(100.0 * c.damage / contracts.BREAK_DAMAGE)
    else:
        done = "to %s" % c.destination
    return "%s, %.0f h left" % (done, hours_left(c))


def contract_target(c):
    """Where the contract's marker stands (planet space): a delivery's and
    a courier's destination; a recovery's pod's point until the pod is in
    the hold or PICKED_UP from it, then its destination; a survey's
    target. None without a contract."""
    if c is None:
        return None
    if c.kind == "recovery":
        crate = next((p for p in foot.props.crates if crate_contract(p) is c), None)
        in_hold = state["car"] is not None and any(i.get("contract") == c.id for i in state["car"].cargo)
        moved = False
        if crate is not None:
            body, position, _ = foot.prop_state(crate)
            moved = body is planet and poi_distance_to(c.target, add(body.centre(), position)) > PICKED_UP
        if not in_hold and not moved:
            return add(planet.centre(), scale(c.target, planet.radius))
    if c.kind == "survey":
        return add(planet.centre(), scale(c.target, planet.radius))
    outpost = outpost_named(c.destination)
    return outpost.point() if outpost is not None else None


def poi_distance_to(direction, point):
    """Metres along the ground from `point` (planet space) to the unit `direction`."""
    return planet.radius * math.acos(max(-1.0, min(1.0, dot(direction, normalize(planet.relative(point))))))


def contract_note(text, colour=GOOD_TOAST):
    state["contract_note"] = text
    state["hud"].toast(text[0].upper() + text[1:], colour)


def accept_contract(c):
    """An offer accepted at its outpost (at_outpost), while fewer than
    contracts.MAX_ACTIVE are active, and pinned when none is: a courier's
    crate put in the car's hold when the car is on the pad and it fits,
    else beside the player on foot (refused otherwise); a recovery's pod
    placed at its target."""
    outpost = at_outpost()
    if outpost is None or outpost.name != c.origin:
        contract_note("accept a contract on its outpost's pad", hud_module.WARNING)
        return False
    if len(state["contracts"]) >= contracts.MAX_ACTIVE:
        contract_note("at most %d contracts at once" % contracts.MAX_ACTIVE, hud_module.WARNING)
        return False
    if c.kind == "courier" and not give_courier_crate(c, outpost):
        contract_note("no room for the crate: bring the car onto the pad, or yourself", hud_module.WARNING)
        return False
    c.state = "active"
    state["contracts"].append(c)
    state["offers_taken"].add(c.id)
    if state["pinned"] is None:
        state["pinned"] = c.id
    if c.kind == "recovery":
        place_pod(c)
    contract_note("accepted: %s" % contracts.task(c))
    return True


def give_courier_crate(c, outpost):
    """A courier's crate into the car's hold when the car is on `outpost`'s
    pad and it fits, else 2 m ahead of the player on foot. Whether it was."""
    car = state["car"]
    volume = contracts.COURIER_SIZE ** 3
    if car_outpost() is outpost and car.can_take(contracts.COURIER_MASS, None, volume):
        car.cargo.append(crate_item(c, contracts.COURIER_MASS, volume))
        return True
    player, bubble = foot.player, foot.bubble
    if player is None or state["driving"] or bubble.body is not planet:
        return False
    up = player.up()
    forward = player.level(player.heading, up)
    right = normalize(cross(forward, up))
    spot = add(player.feet(), add(scale(forward, 2.0), scale(up, 0.5 * contracts.COURIER_SIZE + 0.3)))
    crate = foot.props.spawn_crate(bubble, spot, quat_from_basis(right, up, scale(forward, -1.0)),
                                   contracts.COURIER_SIZE, colour=contracts.COURIER_COLOUR,
                                   mass=contracts.COURIER_MASS)
    state["couriers"][id(crate)] = c.id
    return True


def end_contract(c, how):
    """`c` ended "completed", "failed", "expired" or "abandoned": its
    outpost's reputation up one when completed, else down one (no lower
    than MIN_REPUTATION); its crate, pod and point of interest gone; the
    pin moved to the next active contract."""
    crates = [p for p in foot.props.crates if crate_contract(p) is c]
    c.state = how
    state["contracts"] = [a for a in state["contracts"] if a is not c]
    rep = state["reputation"].get(c.origin, 0.0) + (1.0 if how == "completed" else -1.0)
    state["reputation"][c.origin] = max(MIN_REPUTATION, rep)
    if how == "completed":
        state["contracts_done"] += 1
    else:
        state["contracts_failed"] += 1
    for crate in crates:
        forget_crate(crate)
        if foot.gun is not None and crate.body is not None and foot.gun.held == crate.body:
            foot.gun.drop()
        foot.props.remove(foot.bubble, crate, release=False)
    if state["car"] is not None:
        state["car"].cargo = [i for i in state["car"].cargo if i.get("contract") != c.id]
    if c.kind == "recovery":
        state["pois"].pois.pop(pod_key(c), None)
    if state["pinned"] == c.id:
        state["pinned"] = state["contracts"][0].id if state["contracts"] else None


def complete_contract(c):
    """`c` done: its reward paid, then ended (end_contract)."""
    state["market"].credits += c.reward
    state["earned"] += c.reward
    end_contract(c, "completed")
    contract_note("contract complete: %s, +%s cr" % (contract_title(c), format(int(round(c.reward)), ",")))


def fail_contract(c, how, why):
    end_contract(c, how)
    contract_note("contract %s: %s (%s)" % (how, contract_title(c), why), hud_module.WARNING)


def pin_contract(c):
    state["pinned"] = c.id


def abandon_pinned():
    c = pinned_contract()
    if c is None:
        contract_note("no contract pinned", hud_module.WARNING)
        return
    fail_contract(c, "abandoned", "%s's reputation -1" % c.origin)


def deliver(outpost, item):
    """An ore item at `outpost` taken by the first active delivery that
    wants it there (contracts.wants), completing it once it has its
    quantity. Whether one took it."""
    c = next((a for a in state["contracts"] if contracts.wants(a, item, outpost.name)), None)
    if c is None:
        return False
    c.delivered += item["mass"]
    if c.delivered >= c.quantity:
        complete_contract(c)
    else:
        state["hud"].toast("Delivered %.0f kg of %s: %.0f / %.0f kg" % (item["mass"], resources.name(c.resource).lower(),
                                                                     c.delivered, c.quantity), GOOD_TOAST)
    return True


def deliver_couriers(held):
    """Each courier's crate settled on its destination's pad (pad_under) delivered."""
    for crate in list(foot.props.crates):
        if id(crate) not in state["couriers"]:
            continue
        c = crate_contract(crate)
        outpost = pad_under(crate, held)
        if c is not None and outpost is not None and outpost.name == c.destination:
            forget_crate(crate)
            foot.props.remove(foot.bubble, crate, release=False)
            complete_contract(c)


def strike_crate(c, speed):
    """A courier's crate struck at `speed` (contracts.strike); broken, its contract fails. Whether it broke."""
    if not contracts.strike(c, speed):
        return False
    fail_contract(c, "failed", "the crate broke")
    return True


def strike_hold(speed):
    """The car's bump at `speed` striking each courier's crate in its hold."""
    car = state["car"]
    for contract_id in [i["contract"] for i in car.cargo if "contract" in i]:
        c = active_contract(contract_id)
        if c is not None and c.kind == "courier":
            strike_crate(c, speed)


def watch_couriers():
    """Each courier's crate struck by how much its velocity changed from
    last frame while its speed fell (a hit, not a throw)."""
    seen = state["crate_velocity"]
    for crate in list(foot.props.crates):
        contract_id = state["couriers"].get(id(crate))
        if contract_id is None:
            continue
        _, _, velocity = foot.prop_state(crate)
        before = seen.get(id(crate))
        seen[id(crate)] = velocity
        c = active_contract(contract_id)
        if c is None or before is None or length(velocity) >= length(before):
            continue
        strike_crate(c, length(sub(before, velocity)))


def update_contracts(dt):
    """The outposts' reputations put on them (trade's prices); each active
    contract past its deadline expired; the survey contracts' shares
    measured every SURVEY_EVERY (ExploredMap.share), completing at
    contracts.SURVEY_SHARE; the couriers' crates watched; with the car on a
    pad, the crates in its hold for there delivered; the Contracts tab
    rebuilt when what it shows changes."""
    for o in state["outposts"]:
        o.reputation = state["reputation"].get(o.name, 0.0)
    if foot.player is None and not state["driving"]:
        return
    day = contract_day()
    for c in list(state["contracts"]):
        if day > c.deadline:
            fail_contract(c, "expired", "past its deadline")
    state["survey_in"] -= dt
    if state["survey_in"] <= 0.0:
        state["survey_in"] = SURVEY_EVERY
        for c in [a for a in state["contracts"] if a.kind == "survey"]:
            c.explored = state["map"].share(planet, c.target, contracts.SURVEY_RADIUS)
            if c.explored >= contracts.SURVEY_SHARE:
                complete_contract(c)
    if foot.bubble is not None and foot.bubble.body is planet:
        watch_couriers()
    car, outpost = state["car"], car_outpost()
    if car is not None and outpost is not None and car.speed() < 1.0:
        for item in [i for i in car.cargo if "contract" in i]:
            c = active_contract(item["contract"])
            if c is not None and c.kind in ("courier", "recovery") and c.destination == outpost.name:
                car.cargo.remove(item)
                complete_contract(c)
    refresh_contracts_tab()


def refresh_contracts_tab():
    """The Contracts tab's items built again when the outpost here, its offers, the active contracts or the pin changed."""
    menu = state["menu"]
    if state["starting"] or not menu.visible:
        return
    here = at_outpost()
    shown = (here.name if here is not None else None, tuple(c.id for c in contract_offers(here)),
             tuple(c.id for c in state["contracts"]), state["pinned"])
    if shown == state["contracts_shown"]:
        return
    state["contracts_shown"] = shown
    k = menu.tab_named("Contracts")
    menu.tabs[k] = ("Contracts", contract_items())


def offer_line(c):
    return "%s  //  %s cr  //  %.0f h" % (c.kind.upper(), format(int(round(c.reward)), ","), hours_left(c))


def contract_items():
    """The Contracts tab: the offers of the outpost whose pad the car or
    the player is on, each with Accept; the active contracts, each with Pin;
    and Abandon for the pinned one."""
    here = at_outpost()
    left = [{"heading": "Offers"},
            {"stat": "Here", "get": lambda: at_outpost().name if at_outpost() is not None else "not on a pad"},
            {"stat": "Reputation", "get": lambda: "%+.0f" % state["reputation"].get(
                at_outpost().name if at_outpost() is not None else "", 0.0)}]
    offers = contract_offers(here)
    if here is None:
        left.append({"note": lambda: "Bring the car or yourself onto a pad."})
    elif not offers:
        left.append({"note": lambda: "No more offers here today; new ones at dawn."})
    for c in offers:
        left += [{"note": lambda c=c: offer_line(c)}, {"note": lambda c=c: contracts.task(c)},
                 {"button": "Accept", "do": lambda c=c: accept_contract(c),
                  "hint": "Take this contract: %d at most at once; done, it raises %s's reputation and its prices, "
                          "failed or abandoned it lowers them." % (contracts.MAX_ACTIVE, c.origin)}]
    right = [{"heading": "Active  %d / %d" % (len(state["contracts"]), contracts.MAX_ACTIVE)}]
    for c in state["contracts"]:
        right += [{"note": lambda c=c: "%s  //  %s cr" % (contract_title(c).upper(),
                                                          format(int(round(c.reward)), ","))},
                  {"note": lambda c=c: contract_progress_text(c)},
                  {"button": "Pinned" if c.id == state["pinned"] else "Pin",
                   "do": lambda c=c: pin_contract(c),
                   "hint": "Show this contract on the card, the compass and the map."}]
    if state["contracts"]:
        right.append({"button": "Abandon the pinned", "do": abandon_pinned,
                      "hint": "Give up the pinned contract: its outpost's reputation falls by one."})
    right.append({"note": lambda: state["contract_note"]})
    return ("columns", [left, right])


def menu_tabs():
    return [("System", system_items()), ("Car", car_items()), ("Outpost", outpost_items()),
            ("Contracts", contract_items()), ("Upgrades", upgrade_items()), ("Settings", settings_items()),
            ("Controls", ("controls", HELP_COLUMNS))]


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


# ── Meteor showers ─────────────────────────────────────────────────────────

def shower_here():
    """The unit direction under the camera on the planet, or None at the start's menu or with neither a player
    nor the car driven."""
    if state["starting"] or (foot.player is None and not state["driving"]):
        return None
    return normalize(planet.relative(state["position"]))


def set_shower(shower):
    """`shower` (events.Shower) the next or current one, nothing of it measured, warned of or struck yet."""
    if state["shower_asked"] is not None and world.extra_probes == state["shower_asked"]:
        world.extra_probes = []
    state["shower"] = shower
    state["impact_heights"], state["paths"] = {}, {}
    state["shower_asked"], state["shower_asking"] = None, []
    state["shower_warned"], state["struck"] = False, 0


def probe_shower():
    """The ground under the shower's impacts not yet struck measured with
    the world's extra probes, as the points of interest are: only while
    nothing else uses them."""
    shower, asked = state["shower"], state["shower_asked"]
    if shower is None or (world.extra_probes and world.extra_probes != asked):
        return
    if asked is not None:
        if [probe for probe, _ in world.extra_heights] != asked:
            return
        for k, (_, h) in zip(state["shower_asking"], world.extra_heights):
            state["impact_heights"][k] = h
        state["shower_asked"], state["shower_asking"] = None, []
        world.extra_probes = []
    wanted = [k for k in range(state["struck"], len(shower.impacts)) if k not in state["impact_heights"]]
    if wanted:
        asked = [(shower.impacts[k].direction, planet) for k in wanted[:EXTRA_PROBES]]
        world.extra_probes = asked
        state["shower_asked"], state["shower_asking"] = asked, wanted[:EXTRA_PROBES]


def shower_waiting():
    """Whether the shower has impacts not yet struck whose ground is not yet measured."""
    shower = state["shower"]
    return shower is not None and any(k not in state["impact_heights"]
                                      for k in range(state["struck"], len(shower.impacts)))


def on_land(direction, height):
    """Whether the ground `height` m up at the unit `direction` on the planet is above the sea and any lake or river."""
    level = world.water_level(direction)
    return height > 0.0 and (level is None or height > level)


def update_showers():
    """The meteor showers (events.py): the next planned around the camera
    (at a loaded save's next_shower, at least WARNING_SECONDS on, when
    there is one), its impacts' ground measured (probe_shower) and,
    while its large impact falls in water, planned again with the same
    start up to SHOWER_TRIES times; announced WARNING_SECONDS before its
    start once that is known, with a toast and a marker on the compass;
    each meteor's path flown back from where it strikes; and each impact
    struck at its time (`strike`). Once all have struck, the next is
    planned. Under --check none is planned; the check gives its own. The
    props that have cooled or are gone leave `hot`."""
    present = {id(p) for p in foot.props.rocks}
    state["hot"] = {i: hot for i, hot in state["hot"].items()
                    if i in present and events.heat(world.time - hot[0], hot[1]) > 0.0}
    here, shower = shower_here(), state["shower"]
    if here is None or (args.check and shower is None):
        return
    if shower is None:
        start, state["next_shower"] = state["next_shower"], None
        if start is not None:
            start = max(start, world.time + events.WARNING_SECONDS)
        set_shower(events.schedule(world.time, state["shower_rng"], here, planet.radius, start))
        state["shower_tries"] = 0
        shower = state["shower"]
    probe_shower()
    heights = state["impact_heights"]
    large = next(k for k, i in enumerate(shower.impacts) if i.large)
    known = large in heights or state["struck"] > large
    if (large in heights and not state["shower_warned"] and state["shower_tries"] < SHOWER_TRIES
            and not on_land(shower.impacts[large].direction, heights[large])):
        state["shower_tries"] += 1
        set_shower(events.schedule(world.time, state["shower_rng"], here, planet.radius, shower.start))
        return
    if known and not state["shower_warned"] and world.time >= shower.start - events.WARNING_SECONDS:
        state["shower_warned"] = True
        centre = add(planet.centre(), scale(shower.centre, planet.radius))
        towards = bearing(here, sub(centre, state["position"]))
        state["hud"].toast("Meteor shower inbound: bearing %03.0f, %s"
                           % (towards or 0.0, distance_text(events.ground_distance(here, shower.centre,
                                                                                   planet.radius))), METEOR_MARKER)
    for k, impact in enumerate(shower.impacts):
        if k in heights and k not in state["paths"]:
            point = scale(impact.direction, planet.radius + heights[k])
            state["paths"][k] = events.Path(point, events.velocity(impact, impact.direction), world.physics.gm,
                                            planet.radius)
    while state["struck"] < len(shower.impacts) and shower.impacts[state["struck"]].time <= world.time:
        strike(state["struck"])
        state["struck"] += 1
    if state["struck"] == len(shower.impacts):
        state["shower"] = None


def meteor_rock(impact, ground):
    """The meteorite (an ore boulder) or the rock of `impact`, of iridium at
    its grade and mass, flying down at FALL_SPEED from FALL_LIFT above the
    ground at its point (`ground` m above the planet's radius, as measured
    before it struck) until its ground is measured; its prop, noted as
    first seen there."""
    kind = "boulder" if impact.large else "rock"
    points, unit_volume = foot.rocks.kind_data(foot.physics, kind)
    s = (impact.mass / (rock_density(resources.IRIDIUM) * unit_volume)) ** (1.0 / 3.0)
    radius = float(np.linalg.norm(points * s, axis=1).max())
    up = impact.direction
    east, _ = local_frame(up)
    record = {"kind": kind, "on": planet.name, "state": "flying",
              "position": list(scale(up, planet.radius + ground + 0.5 * s + FALL_LIFT)),
              "rotation": list(quat_from_basis(east, up, cross(east, up))),
              "velocity": list(scale(up, -FALL_SPEED)), "spin": [0.0, 0.0, 0.0], "size": 2.0 * radius,
              "radius": radius, "rest": 0.5 * s, "mass": impact.mass, "extent": [s, s, s],
              "colour": list(rock_colour(planet.index, impact.grade, resources.IRIDIUM)), "key": None,
              "ground": None, "grade": impact.grade, "resource": resources.IRIDIUM}
    prop = foot.rocks.restore(foot.physics, record, {planet.name: planet})
    state["seen"][id(prop)] = (planet.name, up)
    return prop


def strike(k):
    """The shower's impact `k` strikes, at the ground measured there (the
    top of the planet's terrain when it is not yet known): the large one digs its crater
    (events.keep, PlanetWorld.set_craters) and leaves its meteorite, a small
    one its rock (meteor_rock), hot (events.LARGE_COOLING or
    SMALL_COOLING); the props in the bubble within PUSH_REACH times its
    blast's radius (the crater's, or SMALL_BLAST) pushed away (`push`); a
    flash of FLASH_ALPHA, full within FLASH_REACH and falling with the
    square of the distance beyond, and within SHAKE_REACH the view shaken by
    up to SHAKE_ANGLE, both SMALL_LIGHT as strong for a small impact; and a
    plume of dust (PLUME_PUFFS, or SMALL_PUFFS) at its point. The large
    one's meteorite is marked on the compass and announced by a toast."""
    impact = state["shower"].impacts[k]
    ground = state["impact_heights"].get(k, planet.heights[1])
    up = impact.direction
    point = add(planet.centre(), scale(up, planet.radius + ground))
    if impact.large:
        radius, depth, rim = impact.crater
        crater = planet_world.Crater(planet.index, up, radius, depth, rim)
        state["craters"] = events.keep(state["craters"], crater, world.time, shower_here() or up, planet.radius)
        world.set_craters([c for c, _ in state["craters"]])
    prop = meteor_rock(impact, ground)
    state["hot"][id(prop)] = (world.time, events.LARGE_COOLING if impact.large else events.SMALL_COOLING)
    blast = impact.crater[0] if impact.large else SMALL_BLAST
    push(point, PUSH_REACH * blast)
    light = 1.0 if impact.large else SMALL_LIGHT
    distance = length(sub(point, state["position"]))
    state["flash"] = (world.time, FLASH_ALPHA * light * min(1.0, (FLASH_REACH / max(distance, 1.0)) ** 2))
    if distance < SHAKE_REACH:
        state["shake"] = (world.time, SHAKE_ANGLE * light * (1.0 - distance / SHAKE_REACH))
    east, north = local_frame(up)
    puffs = PLUME_PUFFS if impact.large else SMALL_PUFFS
    for n in range(puffs):
        a = 2.0 * math.pi * n / puffs
        offset = scale(add(scale(east, math.cos(a)), scale(north, math.sin(a))), 0.3 * blast)
        state["plumes"].append((add(point, offset), up, world.time, blast, light, 0.5 + 0.5 * n / puffs))
    if impact.large:
        state["meteorite"] = prop
        here = shower_here() or up
        state["hud"].toast("Meteorite down: bearing %03.0f, %s"
                           % (bearing(here, sub(point, state["position"])) or 0.0,
                              distance_text(events.ground_distance(here, up, planet.radius))), METEOR_MARKER)


def push(point, reach):
    """The props in the bubble within `reach` m of `point` (planet space)
    pushed away from it and up, at up to PUSH_SPEED at it and none at
    `reach`."""
    bubble = foot.bubble
    if bubble is None:
        return
    for prop in foot.props.in_physics():
        p = foot.physics.get_transform(prop.body)[0]
        away = sub(bubble.to_world(p), point)
        d = length(away)
        if d >= reach:
            continue
        up = bubble.up(p)
        out = normalize(add(scale(away, 1.0 / d) if d > 1e-6 else up, up))
        foot.physics.add_impulse(prop.body, scale(out, prop.mass * PUSH_SPEED * (1.0 - d / reach)))


def too_hot(body):
    """Whether the physics body `body` is a prop still hot from its impact (the gun's TOO HOT)."""
    prop = foot.props.find(body) if body is not None else None
    hot = state["hot"].get(id(prop)) if prop is not None else None
    return hot is not None and events.heat(world.time - hot[0], hot[1]) > 0.0


def split_hot(boulder, chunks):
    """The chunks of a hot meteorite as hot as it was (Rocks.on_split)."""
    hot = state["hot"].get(id(boulder)) if boulder is not None else None
    if hot is not None:
        for chunk in chunks:
            state["hot"][id(chunk)] = hot
    if boulder is not None and boulder is state["meteorite"]:
        state["meteorite"] = None


def meteorite_point():
    """The meteorite still lying whole, planet space, or None."""
    meteorite = state["meteorite"]
    if meteorite is None or not any(p is meteorite for p in foot.props.rocks):
        return None
    body, position, _ = foot.prop_state(meteorite)
    return add(body.centre(), position)


def flash_tint():
    """The impact's flash over the screen ((r, g, b), alpha), fading over FLASH_SECONDS, or None."""
    flash = state["flash"]
    if flash is None or not 0.0 <= world.time - flash[0] < FLASH_SECONDS:
        return None
    return FLASH_COLOUR, flash[1] * (1.0 - (world.time - flash[0]) / FLASH_SECONDS)


def shaken(basis):
    """The view's `basis` (right, up, forward) turned by the impact's shake, fading over SHAKE_SECONDS."""
    shake = state["shake"]
    if shake is None or not 0.0 <= world.time - shake[0] < SHAKE_SECONDS:
        return basis
    age = world.time - shake[0]
    amplitude = shake[1] * (1.0 - age / SHAKE_SECONDS)
    yaw = amplitude * math.sin(2.0 * math.pi * SHAKE_HZ * age)
    pitch = amplitude * math.sin(2.0 * math.pi * 1.3 * SHAKE_HZ * age + 1.0)
    right, up, forward = basis
    right, forward = rotate(right, up, yaw), rotate(forward, up, yaw)
    return right, rotate(up, right, pitch), rotate(forward, right, pitch)


def draw_showers(camera, basis):
    """The meteors in flight, their dust plumes and the hot props, as
    glowing quads (viewmodel.Glows) relative to `camera`, the view's
    `basis`:
    - each meteor's head a disc HEAD_SIZE across (SMALL_HEAD of that for a
      small one), at least HEAD_PIXELS of the screen, HEAD_LIGHT times
      METEOR_GLOW (SMALL_LIGHT of it for a small one) brightening over
      IGNITE_SECONDS from its entry; and its trail, TRAIL_SEGMENTS
      streaks along the last TRAIL_SECONDS of its path, TRAIL_WIDTH of the
      head wide, TRAIL_LIGHT times TRAIL_GLOW, narrowing and fading to its
      end;
    - each plume's puffs discs rising at up to PLUME_RISE, growing from
      the blast's radius to three times it and fading in PLUME_COLOUR
      over PLUME_SECONDS, and fading out within PLUME_NEAR of their size
      from the camera;
    - each hot prop a disc HOT_SIZE times its radius across in HOT_GLOW
      times its heat."""
    glows = state["meteor_glows"]
    glows.begin(basis)
    now, centre = world.time, planet.centre()
    pixel = math.radians(settings["fov"]) / max(1, engine.framebuffer_size[1])
    heads = 0
    shower = state["shower"]
    for k in range(state["struck"], len(shower.impacts) if shower is not None else 0):
        impact, path = shower.impacts[k], state["paths"].get(k)
        before = impact.time - now
        if path is None or not 0.0 < before <= path.duration:
            continue
        light = (1.0 if impact.large else SMALL_LIGHT) * min(1.0, (path.duration - before) / IGNITE_SECONDS)
        head = add(centre, path.at(before))
        size = max(HEAD_SIZE * (1.0 if impact.large else SMALL_HEAD), HEAD_PIXELS * pixel * length(sub(head, camera)))
        glows.disc(sub(head, camera), size, scale(METEOR_GLOW, HEAD_LIGHT * light))
        heads += 1
        last = head
        for j in range(1, TRAIL_SEGMENTS + 1):
            point = add(centre, path.at(before + TRAIL_SECONDS * j / TRAIL_SEGMENTS))
            fade = 1.0 - (j - 0.5) / TRAIL_SEGMENTS
            glows.streak(sub(last, camera), sub(point, camera), TRAIL_WIDTH * size * fade,
                         scale(TRAIL_GLOW, TRAIL_LIGHT * light * fade))
            last = point
    state["plumes"] = [p for p in state["plumes"] if now - p[2] < PLUME_SECONDS]
    for point, up, since, blast, light, rise in state["plumes"]:
        age = now - since
        if age < 0.0:
            continue
        at = sub(add(point, scale(up, PLUME_RISE * rise * age + 0.5 * blast)), camera)
        size = blast * (1.0 + 2.0 * age / PLUME_SECONDS)
        fade = (1.0 - age / PLUME_SECONDS) * min(1.0, age / 0.3) * min(1.0, length(at) / (PLUME_NEAR * size))
        glows.disc(at, size, scale(PLUME_COLOUR, light * fade))
    for prop in foot.props.rocks:
        hot = state["hot"].get(id(prop))
        heat = events.heat(now - hot[0], hot[1]) if hot is not None else 0.0
        if heat > 0.0:
            body, position, _ = foot.prop_state(prop)
            glows.disc(sub(add(body.centre(), position), camera), HOT_SIZE * prop.radius, scale(HOT_GLOW, heat))
    glows.end()
    state["heads_drawn"] = heads


# ── Saving and loading ─────────────────────────────────────────────────────

def save_game(base=None, quiet=False):
    """The game written to `base` (save_base when None): the clock, the
    player (None while driving), the car, the props and the taken rocks,
    the market, the game's numbers, the waypoint, the day's offset
    (clock.py), the contracts (the active ones, today's offers accepted,
    the pin, the reputations and the counts; which crates are couriers'),
    the explored map, the meteorites' craters ("modifiers"), the next
    shower's start ("events"; a shower under way, its start) and which
    props are hot and since when, and which is the last meteorite.
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
                "driving": state["driving"], "fuel": car.fuel,
                "cargo": [dict(item, direction=list(item["direction"])) for item in car.cargo]},
        "taken": [list(key) for key in sorted(world.taken_rocks.keys)],
        "props": foot.props.records(bubble) if bubble is not None else [],
        "seen": [state["seen"].get(id(p)) for p in foot.props.all],
        "pods": [list(state["pods"][id(p)]) if id(p) in state["pods"] else None for p in foot.props.all],
        "couriers": [state["couriers"].get(id(p)) for p in foot.props.all],
        "contracts": {"active": [contracts.record(c) for c in state["contracts"]],
                      "taken": sorted(i for i in state["offers_taken"]
                                      if i.rsplit("/", 2)[1] == str(contract_day())),
                      "pinned": state["pinned"], "reputation": dict(state["reputation"]),
                      "done": state["contracts_done"], "failed": state["contracts_failed"]},
        "market": state["market"].record(state["outposts"]),
        "game": {"sold_kg": state["sold_kg"], "earned": state["earned"], "ore_chunks": state["ore_chunks"],
                 "ore_seen": state["ore_seen"], "waypoints": state["waypoints"],
                 "objective": state["objectives"].index},
        "waypoint": None if waypoint is None else [waypoint[0].name, list(waypoint[1])],
        "day_offset": day_time() - world.time,
        "upgrades": sorted(state["owned"]),
        "suit": {"reserve": state["suit"].reserve, "health": state["suit"].health},
        "pois": state["pois"].record(),
        "parts": dict(state["parts"]),
        "survey_data": state["survey_data"],
        "modifiers": [events.crater_record(c, t, planet.name) for c, t in state["craters"]],
        "events": {"next_shower": state["shower"].start if state["shower"] is not None else state["next_shower"]},
        "hot": [list(state["hot"][id(p)]) if id(p) in state["hot"] else None for p in foot.props.all],
        "meteorite": next((n for n, p in enumerate(foot.props.all) if p is state["meteorite"]), None),
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
    set_paused(False)
    state["driving"] = False
    set_fov(settings["fov"])
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
    state["craters"] = [events.crater_from_record(r, planet.index, planet_world.Crater)
                        for r in data.get("modifiers", []) if r.get("type") == "crater" and r.get("body") == planet.name]
    world.set_craters([c for c, _ in state["craters"]])
    set_shower(None)
    state["next_shower"] = (data.get("events") or {}).get("next_shower")
    state["flash"], state["shake"], state["plumes"] = None, None, []
    state["pois"].restore(data.get("pois", []), poi_seed())
    state["poi_want_at"], state["poi_asked"], state["poi_asking"] = None, None, []
    state["parts"] = dict(data.get("parts", {}))
    state["survey_data"] = data.get("survey_data", 0.0)
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
    apply_upgrades(k for k in data.get("upgrades", []) if k in upgrades.BY_KEY)
    record = data.get("car")
    if record is not None:
        state["car"] = hovercar.Car(tuple(record["position"]), tuple(record["heading"]), record["target"],
                                    params=state["params"]["car"], fuel=record.get("fuel"))
        state["car"].cargo = [dict(item, direction=tuple(item["direction"])) for item in record.get("cargo", [])]
        update_ship()
    else:
        position, _, _, north = ship_pose()
        make_car(position, north)
    kept = data.get("contracts") or {}
    state["contracts"] = [contracts.restore(r) for r in kept.get("active", [])]
    state["offers_taken"] = set(kept.get("taken", []))
    state["pinned"] = kept.get("pinned")
    state["reputation"] = dict(kept.get("reputation", {}))
    state["contracts_done"], state["contracts_failed"] = kept.get("done", 0), kept.get("failed", 0)
    held = {i.get("contract") for i in state["car"].cargo}
    for c in state["contracts"]:
        if c.kind == "recovery":
            place_pod(c, done=pod_key(c) in state["pods"].values() or c.id in held)
    state["contracts_shown"] = None
    state["car_poses"] = None
    p = data["player"]
    if p is None or (record is not None and record["driving"]):
        state["loading"] = None
        state["driving"] = True
        state["chase"].reset()
        place_chase(0.0)
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


def autosave(dt):
    """With autosave on, the game saved after AUTOSAVE_SECONDS of play."""
    if not settings["autosave"] or args.check or foot.player is None or state["menu"].visible:
        return
    state["autosave_in"] -= dt
    if state["autosave_in"] <= 0.0:
        save_game(quiet=True)
        state["hud"].toast("Autosaved", hud_module.DIM)


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
    elif state["driving"] and not paused and not state["map_view"].open:
        read_car_controls()
    elif foot.player is not None and not paused and not state["map_view"].open:
        foot.read_controls(engine.input)
        slow_down()
        ignore_held_buttons()
        read_bumpers()
        read_pad_enter()
    if state["driving"] and (paused or state["map_view"].open):
        state["car_controls"] = hovercar.CarControls()
    if paused:
        dt = 0.0
    world.time += dt
    latency = world.probe_latency * state["frame_ms"] / 1000.0
    foot.props.fly(world.time - clock, latency)
    active = foot.player is not None and not state["map_view"].open and not paused
    step_car(dt)
    viewpoint = state["position"]
    if state["driving"]:
        viewpoint = car_position()
        if foot.bubble is None and (state["car"].height or 0.0) < 0.5 * Bubble.RADIUS:
            foot.ensure_bubble(viewpoint)
    foot.step(dt, viewpoint, state["position"], latency, active=active, before_step=move_car_body)
    update_showers()
    foot.props.probe(foot.bubble, EXTRA_PROBES)
    place_eye()
    if state["driving"]:
        place_chase(elapsed)
    capture_mouse()
    set_max_levels()

    camera = state["position"]
    basis = shaken(view_basis())
    update_lights(camera)
    foot.request_nearby(camera, bodies)
    world.end_frame(camera, basis, dt, viewpoint=camera,
                    patch=car_patch_request() if state["car"] is not None else None)
    ground = world.site_ground()
    for entity, offset in blocks:
        engine.scene.set_position(entity, sub(add(ground, offset), camera))
    pads.draw(camera)
    draw_net(camera)
    draw_poi_scenes(camera)
    draw_showers(camera, basis)
    foot.props.draw(foot.bubble, camera)
    place_ship(camera)
    place_viewmodel(dt, camera)
    update_fuel_call()
    update_suit(dt)
    update_pois(dt)
    update_contracts(dt)
    update_hud(dt)
    update_map(dt)
    update_game()
    autosave(elapsed)
    capture_toast()
    foot.end_frame()
    state["frame_ms"] = 0.9 * state["frame_ms"] + 0.1 * elapsed * 1000.0
    return True


def day_time():
    """The day's time (clock.py): the world's clock plus the day's offset,
    set for START_HOUR the first time it is asked for."""
    if state["day_offset"] is None:
        state["day_offset"] = clock.time_of(START_HOUR, world.time) - world.time
    return world.time + state["day_offset"]


def hour_text():
    hour = clock.hour(day_time())
    return "%02d:%02d" % (int(hour) % 24, int(hour * 60.0) % 60)


def here_temperature():
    """The air's temperature (degrees C) where the camera is: on the planet
    suit.local_temperature of the climate under it (with its height) at
    the local hour and the speed of the wind there CHILL_HEIGHT above the
    ground (PlanetWorld.wind_at); on the moon suit.moon_temperature
    of the sun's elevation there; None while the ground is not known."""
    camera = state["position"]
    body = world.body_under(camera)
    radial = normalize(body.relative(camera))
    if body is not planet:
        return suit.moon_temperature(dot(world.sun_direction(), radial))
    climate = world.ground_climate
    if climate is None:
        return None
    _, up, east, north = world.site_frame()
    hour = clock.local_hour(radial, day_time(), up, east, north)
    wind = length(world.wind_at(planet, body.relative(camera), CHILL_HEIGHT))
    return suit.local_temperature(climate[0], climate[1], hour, wind)


def update_suit(dt):
    """The suit stepped by `dt` seconds (suit.step_suit) at the temperature
    here, sheltered in the car or on an outpost's pad; with its health
    out, the player wakes at the nearest outpost (`wake_at_outpost`)."""
    if foot.player is None and not state["driving"]:
        return
    temperature = here_temperature()
    state["temperature"] = temperature
    if temperature is None:
        return
    outpost = None if state["driving"] else at_outpost()
    sheltered = state["driving"] or outpost is not None
    if suit.step_suit(state["suit"], state["suit_params"], temperature, sheltered, outpost is not None, dt):
        wake_at_outpost()


def wake_at_outpost():
    """The player, its health gone, woken on the pad of the outpost
    nearest it on its body for RECOVERY_FEE (as far as the credits go):
    what the gun held dropped where it fell, the suit full again; it comes
    back once the ground there is known (maybe_spawn)."""
    feet = foot.bubble.to_world(foot.player.feet())
    body = foot.bubble.body
    outpost = outposts.nearest([o for o in state["outposts"] if o.body is body], feet)
    if outpost is None:
        return
    fee = min(RECOVERY_FEE, state["market"].credits)
    state["market"].credits -= fee
    foot.drop_bubble()
    up = normalize(sub(outpost.point(), body.centre()))
    state["position"] = add(outpost.point(), scale(up, 2.0))
    state["forward"] = local_frame(up)[1]
    state["loading"] = {"on": body.name, "pitch": 0.0,
                        "toast": "You came to at %s; the outpost's medics charged %s cr"
                                 % (outpost.name, format(int(round(fee)), ","))}
    world.ground = None
    state["suit"] = suit.SuitState(reserve=state["suit_params"].reserve)
    state["woken"] += 1
    print("[walker] the suit gave out; woke at %s for %.0f cr" % (outpost.name, fee), flush=True)


def suit_effects():
    """suit.effects at the temperature here: (the tint, the movement's factor); none while it is not known."""
    if state["temperature"] is None:
        return None, 1.0
    return suit.effects(state["suit"], state["suit_params"], state["temperature"])


def slow_down():
    """The walk scaled by the suit's movement factor, no running below it."""
    factor = suit_effects()[1]
    if factor < 1.0:
        controls = foot.controls
        controls.forward *= factor
        controls.strafe *= factor
        controls.run = False


def suit_rows():
    """The objective card's rows of the suit and the temperature here."""
    s, temperature = state["suit"], state["temperature"]
    rows = [("Suit", "%d %%" % round(100.0 * s.reserve / state["suit_params"].reserve)
             + ("" if s.health >= 0.995 else ", health %d %%" % round(100.0 * s.health)))]
    if temperature is not None:
        rows.append(("Temp", "%.0f C" % temperature))
    return rows


def update_lights(camera):
    """The sun where clock.py has it at the day's time, the exposure for
    it seen from `camera` (clock.set_sun), and while lamps.lights_on the
    local lights: each outpost's, the car's headlights (CAR_HEADLIGHTS,
    CAR_HEADLIGHT_DIP down) and on foot the suit lamp."""
    clock.set_sun(world, day_time(), camera)
    lights = []
    if lamps.lights_on(world, camera):
        lights += lamps.outpost_lights(state["outposts"])
        if state["car"] is not None:
            right, up, forward = state["car"].attitude()
            lights += lamps.headlights(car_position(), right, up, forward, CAR_HEADLIGHTS, CAR_HEADLIGHT_DIP)
        if not state["driving"] and foot.player is not None:
            lights.append(lamps.suit_lamp(camera, *view_basis()))
    world.lights = lights


def on_init():
    world.setup()
    build_site()
    state["home"] = home_outpost(world)
    state["outposts"] = [state["home"]]
    begin_survey()
    build_ship_model()
    build_net()
    state["viewmodel"] = Viewmodel(engine, box())
    state["car_glows"] = carview.EngineGlows(engine)
    state["meteor_glows"] = Glows(engine, METEOR_DISCS, METEOR_STREAKS)
    foot.too_hot = too_hot
    foot.rocks.on_split = split_hot
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
    """F1 the menu on the controls, M the map, Escape the menu, P a screenshot, F the car (in or out),
    F5 a save, F9 a load; the start's menu waits for its buttons."""
    view, menu = state["map_view"], state["menu"]
    if state["starting"]:
        return
    if key == keys.F1 and (foot.player is not None or state["driving"]):
        controls = menu.tab_named("Controls")
        set_paused(not (menu.visible and menu.tab == controls), "Controls")
    elif key == keys.M and (foot.player is not None or state["driving"]) and not menu.visible:
        view.set_open(not view.open)
    elif key == keys.ESCAPE and view.open:
        view.set_open(False)
    elif key == keys.P:
        take_capture()
    elif key == keys.F and not menu.visible and not view.open:
        if state["driving"]:
            leave_car()
        else:
            enter_car()
    elif key == keys.ESCAPE and (foot.player is not None or state["driving"]):
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
    elif foot.player is not None and not state["driving"]:
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

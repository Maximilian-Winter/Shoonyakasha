"""
What every part of the walker (walker.py, the play package) shares: the
command line (`args`), the settings, the engine, the planet's world and
its bodies, the player on foot (`foot`), the outposts' pads, the Base
blocks, the game's `state` (one dict: each key's comment says what it
holds), and the constants more than one module uses. Importing it parses
the command line and makes the engine and the world.
"""

import argparse
import os
import random
import tempfile

import shoonyakasha as sk
from shoonyakasha.planet import world as planet_world
from shoonyakasha.planet.gravity_gun import GunParams
from shoonyakasha.planet.on_foot import OnFoot
from shoonyakasha.planet.vec import add, cross, scale
from shoonyakasha.planet.world import FOV

import carview
import config
import fieldslate as fs
import flight
import hovercar
import pois
import suit
import upgrades
from outposts import Pads
from rocks import Rocks
from trade import Market
from objectives import Objectives

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # the planet example's folder
CONFIG_PATH = os.path.join(HERE, "walker_config.json")
SAVE_BASE = os.path.join(HERE, "walker_save")          # walker_save.json and walker_save.npz
CHECK_SAVE_BASE = os.path.join(tempfile.gettempdir(), "walker_check_save")
AUTOSAVE_SECONDS = 120.0         # s of play between autosaves
SPAWN_LIFT = 1.0                 # m above the ground the player is put down
SHIP_HALF = (1.452, 0.756, 3.097)   # m: the ship model's half sizes (right, up, back) about its centre
SHIP_CLEARANCE = SHIP_HALF[1] + hovercar.HOVER_MIN   # m from the ship's centre down to the ground, parked
RIVER_CHECK_WIDTH = 20.0         # m wide the river --check hovers the car on is, at least
CRATE_SIZE = 0.6                # m
GOOD_TOAST = (0.55, 1.0, 0.62, 1.0)
CHECK_ORE_EXTENT = (0.4, 0.3, 0.4)   # m: the ore rock --check drops on the pad
RECOVERY_FEE = 250.0            # credits the outpost charges when the suit gave out (wake_at_outpost)
POI_REACH = 3_000.0             # m: points of interest within it are measured and shown (exploring.py)
METEOR_MARKER = fs.AMBER
PLUME_PUFFS, SMALL_PUFFS = 6, 2
STORM_WARNING = 300.0           # s before a storm covers an outpost it is announced
RAIN_DROPS = 160
SPACE_HEIGHT = 250.0            # m above the ground the car must be for space mode
HOVER_BACK = 300.0              # m above the ground within which space mode goes back to the hover car
WARP_HEIGHT = 5_000.0           # m above the ground below which the time warp is 1
parser = argparse.ArgumentParser(description="The planet on foot")
parser.add_argument("--check", action="store_true",
                    help="walk 10 s north, grab a crate and throw it, set a waypoint on the map, run the suit out in the cold, "
                         "sell ore at Base, "
                         "load the car's net and sell its cargo, buy upgrades, use a point of interest of each kind "
                         "and sell survey data, take and end a contract of each kind, bring down a meteor shower, "
                         "stand in a storm, pause, save, change the game and load it, drive and fly the car, hover it "
                         "on a lake and a river, run it dry and call fuel, fly it in space mode and land it on the "
                         "moon, then night; report and exit")
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
    "space": None,                # the car in space mode (flight.Ship), or None
    "space_controls": flight.Controls(),
    "ship_time": 0.0,             # the world's clock the space mode's steps have reached
    "warp": 0,                    # index into WARPS
    "hold": None,                 # the nose hold in space mode: None, "prograde" or "retrograde"
    "car_body": None,             # the world body the car is reckoned around; None: the planet
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
    "shower_body": None,          # the world body it falls on; None: the planet
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
    "storm_rng": random.Random(),
    "storm_warned": set(),        # names of the outposts the storm has been announced for
    "storm_closed": set(),        # names of the outposts it covers
    "in_storm": False,            # the camera was in it last frame
    "rain_glows": None,           # the rain's streaks (viewmodel.Glows)
    "rain_drawn": 0,              # rain streaks drawn last frame
}

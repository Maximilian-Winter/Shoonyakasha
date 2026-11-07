"""
What every part of the walker (walker.py, the play package) shares: the
command line (`args`), the settings, the engine, the planet's world and
its bodies, the player on foot (`foot`), the outposts' pads and their
parts in the physics (`outpost_bodies`), the Base
blocks, the game's `state` (play.state.State: a part for each system,
each field's comment saying what it holds), and the constants more than one module uses. Importing it parses
the command line and makes the engine and the world.
"""

import argparse
import os
import tempfile

import shoonyakasha as sk
from shoonyakasha.planet import world as planet_world
from shoonyakasha.planet.on_foot import OnFoot
from shoonyakasha.planet.vec import add, cross, scale
from shoonyakasha.planet.world import FOV

import config
import fieldslate as fs
import hovercar
from outpost_bodies import OutpostBodies
from outposts import Pads
from rocks import Rocks
from play.state import State

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # the planet example's folder
CONFIG_PATH = os.path.join(HERE, "walker_config.json")
SAVE_BASE = os.path.join(HERE, "walker_save")          # walker_save.json and walker_save.npz
CHECK_SAVE_BASE = os.path.join(tempfile.gettempdir(), "walker_check_save")
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
                         "moon, then night, then quit to the main menu and start a new game; report and exit")
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
outpost_bodies = OutpostBodies(engine, pads)
blocks = []                      # (entity, offset in planet space from the site's point on the ground)
state = State()

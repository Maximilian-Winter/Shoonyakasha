"""
Shoonyakasha planet: procedural terrain on a cube-sphere with quadtree level
of detail

A planet of radius 100 km, flown around from a few metres above its ground
to thousands of kilometres away. Positions are Python floats (64-bit) with
the planet's centre at the origin. The engine's camera stays at its own
origin and only turns; every frame each chunk and entity is placed at its
position minus the camera's, so the GPU sees only small numbers. The
projection is reverse-Z with an infinite far plane.

The planet is a cube-sphere: six cube faces mapped onto the sphere with
`spherify`, each the root of a quadtree (quadtree.py). Each frame the tree
picks the chunks to draw, finer near the camera, and up to 16 missing
chunks, those in view first and nearest first, are built on the GPU into
slots of a buffer pool (pipeline/shaders/chunk_build.comp), raised by the
terrain function in pipeline/shaders/terrain.glsl. Each chunk's lowest and
highest heights come back to Python and tighten its node's bounds. All the
drawn chunks are one indirect draw call. Every chunk is 16 x 16 quads with
its vertices relative to its own centre, and skirts along its edges hide
the cracks between levels. A cluster of blocks stands on the +Z face, on a
pad 5 cm above the ground.

The scene is lit by sunlight that comes through the atmosphere and drawn
in HDR; a fullscreen pass then adds the atmosphere in front of it, single
scattering by air and haze raymarched along each pixel's view ray
(pipeline/shaders/atmosphere.glsl), and tone maps the result.

Keys:
    Tab                    free camera / ship
  free camera:
    right mouse + move     look around
    W/A/S/D                fly forward, left, back, right
    E / Q                  up / down
    Shift / Ctrl           4x faster / 4x slower
    mouse wheel            speed factor
  ship (flight.py):
    W / S                  throttle up / down
    Backspace              throttle to 0
    arrows, right mouse    pitch and yaw
    A / D                  roll left / right
    E / Q                  thrust up / down
    L                      translation assist on / off
    , / .                  time warp down / up (1x to 16x)
  ship, gamepad (adding to the keys):
    left stick             pitch and roll
    right stick            yaw (x), thrust up / down (y)
    right / left trigger   throttle up / down
    A                      translation assist on / off
    B                      throttle to 0
    bumpers                time warp down / up
    Y / Start              free camera or ship / settings menu
  anywhere:
    1 - 5                  5 m above the blocks, 1 km, 50 km, 1,000 km, 5,000 km
    6                      above the corner where the +X, +Y and +Z faces meet
    7                      2 km up, 25 km north-west of the blocks, looking further north-west
    8                      the ship at rest on the pad
    9                      the ship in a circular orbit 40 km up
    N                      new terrain seed
    M / J                  mountains higher / lower
    H / G                  hills higher / lower
    Z / X                  sun lower / higher, seen from the blocks
    T / Y                  atmosphere denser / thinner
    B / V                  exposure up / down
    F                      wireframe
    O                      ocean
    C                      colour: terrain, per face, per level
    K                      freeze the level of detail
    [ / ]                  split factor down / up
    F1                     settings menu
    I                      debug text
    P                      screenshot into planet_captures/

    python planet.py --capture DIR     screenshots at the presets, then exit
    python planet.py --verify          check the GPU's chunks against cube_sphere.chunk_mesh, then exit
    python planet.py --orbit-check     fly one orbit from preset 9 at 16x, report the drift, then exit
    python planet.py --landing-check   land the ship on a mountain near the site, report how it stands, then exit
    python planet.py --crack-check     measure the gaps between levels in steep terrain against the skirts, then exit
    python planet.py --morph-check     come down from 20 km, check every swap of chunks happens fully morphed, then exit
"""

import argparse
import math
import os
import random
import time

import numpy as np
import shoonyakasha as sk
from shoonyakasha import keys

import cockpit
import cube_sphere
import flight
import shadows
from ground import GroundPatch, SIZE as PATCH_SIZE
from instruments import Instruments
from menu import SettingsMenu
from quadtree import QuadTree, hidden_by_horizon

HERE = os.path.dirname(os.path.abspath(__file__))
PIPELINE = os.path.join(HERE, "pipeline", "pipeline.json")
CAPTURES = os.path.join(HERE, "planet_captures")

RADIUS = 100_000.0               # metres
QUADS = 16                       # quads along each side of a chunk
MAX_LEVEL = 12                   # a level-12 chunk is 38 m across, its quads 2.4 m
SPLIT_FACTOR = 1.25              # split when nearer than this many chunk edges
MORPH_START = 0.8                # morphing starts at this share of the parent's split distance; matches morph.glsl
MORPH_LEVELS = 4                 # ancestors a vertex morphs towards at most; matches morph.glsl
# The sun's shadow cascades (shadows.py): where they split the view (m), the map's size per side,
# and how far towards the sun beyond a cascade casters count (m).
SHADOW_SPLITS = (0.05, 12.0, 150.0, 2_000.0, 20_000.0)
SHADOW_RESOLUTION = 2048         # matches sunShadowMap in pipeline.json
SHADOW_REACH = 5_000.0
SKIRT_FRACTION = 0.05            # skirt depth, as a fraction of the chunk's edge
HEIGHT_PAD = 0.05                # added to a node's height range, as a fraction of its edge
ROOT_HEIGHTS = (-2000.0, 6000.0)  # the roots' height range until their chunks are measured
# The GPU chunk pool; these match pipeline.json and chunk_build.comp.
SLOTS = 2048                     # chunks the pool holds
MAX_JOBS = 16                    # chunks BuildChunks builds per frame at most
PROBES = 128                     # directions BuildChunks evaluates the terrain at
EXTRA_PROBES = 4                 # the last probes, for directions a check asks for
VERTICES_PER_CHUNK = (QUADS + 1) ** 2 + 4 * (QUADS + 1)
FLOATS_PER_VERTEX = 12           # position and u, normal and v, direction and height
DRAWS_OFFSET = 16                # bytes before the draw commands in ChunkDraws: the count, then padding
# The terrain's parameters (terrain.glsl), the defaults in pipeline.json.
TERRAIN = {
    "continent": [1.5, 0.6, 0.05, 800.0],   # frequency, warp, sea level bias, height scale (m)
    "mountains": [6.0, 3000.0, 2.0, 8.0],   # frequency, height (m), mask frequency, octaves
    "hills": [40.0, 150.0, 1500.0, 6.0],    # frequency, height (m), ocean depth (m), octaves
    "seed": [0.0, 0.0, 0.0, 0.0],           # offset added to the noise input
    "detail": [80.0, 15.0, 4.0, 0.5],       # wavelength (m), height (m), octaves, erosion
}
SITE_FACE = 4                    # +Z
SITE_UV = 1.0 / 64.0             # along u and v from the face's centre: a vertex of every chunk from level 3 down
SITE_FLAT_RADIUS = 150.0         # metres of flat pad around the site
SITE_BLEND_RADIUS = 600.0        # metres from the site where the terrain is its own again
TERRAIN_COLOUR = (-1.0, 0.0, 0.0)  # tells terrain.frag to colour by height, slope and latitude
FACE_COLOURS = ((0.62, 0.30, 0.25), (0.25, 0.50, 0.55), (0.55, 0.55, 0.30),
                (0.40, 0.30, 0.60), (0.30, 0.55, 0.30), (0.60, 0.42, 0.25))
COLOUR_MODES = ("terrain", "face", "level")
SUN_ELEVATION, SUN_AZIMUTH = 40.0, 200.0   # degrees, seen from the site; azimuth from east towards north
DAY_SUN = (SUN_ELEVATION, SUN_AZIMUTH)
# The atmosphere (atmosphere.glsl), the defaults in pipeline.json: a top 20 km up, scale heights of
# 2.5 km (air) and 400 m (haze), and Earth's coefficients times 3.2, which keeps Earth's optical
# depth straight up for the shorter scale heights.
ATMOSPHERE_TOP = 20_000.0
RAYLEIGH_SCALE_HEIGHT, MIE_SCALE_HEIGHT = 2_500.0, 400.0
RAYLEIGH = (1.857e-5, 4.339e-5, 1.0592e-4)          # scattering per metre at height 0
MIE_SCATTERING, MIE_ABSORPTION, MIE_G = 1.279e-5, 1.408e-5, 0.8
# Ozone: Earth's absorption at its peak times 3.2, peaking at 25 km / 3.2, a tent 15 km / 3.2 to each side.
OZONE_ABSORPTION = (2.08e-6, 6.02e-6, 2.72e-7)
OZONE_PEAK, OZONE_HALF_WIDTH = 7_800.0, 4_700.0
SUN_ILLUMINANCE = 20.0
EXPOSURE = 0.4
NEAR_PLANE = 0.05
FOV = 70.0
PIXEL_ANGLE = math.radians(FOV) / 900.0   # radians across a pixel at the window's 900 rows
LOOK_RADIANS_PER_PIXEL = 0.0025
MIN_ALTITUDE = 0.3               # the camera stays this far above the ground
# The ship (flight.py).
FLIGHT_STEP = 1.0 / 120.0        # seconds per physics step
MAX_STEPS_PER_FRAME = 64
WARPS = (1, 2, 4, 8, 16)         # time warps
SHIP_CLEARANCE = 2.0             # metres from the ship's centre down to its gear
SHIP_REACH = 8.0                 # metres from the ship's centre to its farthest contact point
PATCH_MARGIN = 4.0               # metres the ground patch reaches beyond the ship
THROTTLE_RATE = 0.5              # throttle per second while W or S is held
MOUSE_STICK = 0.04               # stick per pixel of mouse movement
GAMEPAD_DEAD_ZONE = 0.12         # of a stick's travel, read as 0
# Gamepad buttons and the keys they press: A assist, B throttle to 0, the bumpers time warp,
# Y ship or free camera, Start the menu.
GAMEPAD_KEYS = {keys.GAMEPAD_A: keys.L, keys.GAMEPAD_B: keys.BACKSPACE, keys.GAMEPAD_LEFT_BUMPER: keys.COMMA,
                keys.GAMEPAD_RIGHT_BUMPER: keys.PERIOD, keys.GAMEPAD_Y: keys.TAB, keys.GAMEPAD_START: keys.F1}
ORBIT_HEIGHT = 40_000.0          # preset 9's circular orbit
ORBIT_POINTS = 256               # points of the drawn orbit path; matches OrbitPoints in pipeline.json

parser = argparse.ArgumentParser(description="Planet with procedural terrain and quadtree level of detail")
parser.add_argument("--capture", metavar="DIR", help="save screenshots at the presets and exit")
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
args = parser.parse_args()

sk.shaders.compile_dir(os.path.join(HERE, "pipeline", "shaders"))
engine = sk.Engine(title="Shoonyakasha - planet", width=1600, height=900, pipeline_json_path=PIPELINE)


# ── Vectors (tuples of Python floats) ──────────────────────────────────────

def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def length(a):
    return math.sqrt(dot(a, a))


def normalize(a):
    return scale(a, 1.0 / length(a))


def rotate(v, axis, angle):
    """`v` turned by `angle` radians about the unit vector `axis` (Rodrigues)."""
    c, s = math.cos(angle), math.sin(angle)
    return add(add(scale(v, c), scale(cross(axis, v), s)), scale(axis, dot(axis, v) * (1.0 - c)))


def local_frame(up):
    """(east, north) at the point of the sphere whose direction is `up`."""
    east = cross((0.0, 1.0, 0.0), up)
    if length(east) < 1e-9:
        east = (1.0, 0.0, 0.0)
    east = normalize(east)
    return east, cross(up, east)


def euler_from_basis(right, up, forward):
    """The engine's Euler rotation (x pitch, y yaw, z roll; applied Y, then
    X, then Z) whose rotation matrix has columns right, up and -forward."""
    back = scale(forward, -1.0)
    pitch = math.asin(max(-1.0, min(1.0, -back[1])))
    yaw = math.atan2(back[0], back[2])
    roll = math.atan2(right[1], up[1])
    return (pitch, yaw, roll)


def level_colour(level):
    """A colour per quadtree level, round the hue circle."""
    hue = level * 0.61803398875
    return tuple(0.45 + 0.30 * math.cos(2.0 * math.pi * (hue - offset)) for offset in (0.0, 1 / 3, 2 / 3))


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
    "camera": None,
    "position": (0.0, 0.0, RADIUS + 5.0),
    "forward": (0.0, 0.0, 1.0),
    "speed": 0.0,
    "speed_factor": 1.0,
    "hud": None,
    "wireframe": False,
    "ocean": True,
    "ocean_chunks": 0,           # chunks the ocean draws
    "chunks_culled": 0,          # selected chunks not drawn: out of view or behind the horizon
    "colour_mode": 0,
    "frozen": False,
    "frame": 0,
    "frame_ms": 16.0,            # frame time, smoothed
    "tree": None,                # made once the site's height is known
    "terrain_version": 0,        # counts terrain changes; probe results carry the version they were made with
    "ground": None,              # terrain height under the camera, from the probes
    "site_height": None,         # natural terrain height at the site, the pad's height, from the probes
    "teleport_when_known": None,  # a preset to jump to again once the site's height is known
    "terrain": {name: list(value) for name, value in TERRAIN.items()},
    "sun_elevation": SUN_ELEVATION,
    "sun_azimuth": SUN_AZIMUTH,
    "density": 1.0,              # times the atmosphere's coefficients
    "ozone": True,               # the ozone layer's absorption
    "orbit_path": True,          # draw the path the ship would coast along
    "shadows": True,             # the sun's shadow cascades
    "gamepad_held": set(),       # gamepad buttons held last frame
    "reentry_pass": False,       # whether the ReentryGlow pass is enabled
    "multiple_scattering": True,  # light scattered more than once, from the multiple scattering table
    "exposure": EXPOSURE,
    "mode": "camera",            # "camera": the free camera; "ship": the view from the ship
    "ship": None,
    "controls": flight.Controls(),
    "warp": 0,                   # index into WARPS
    "accumulator": 0.0,          # seconds of flight not yet stepped
    "flight_time": 0.0,          # seconds of flight stepped
    "patch": None,               # the ground patch around the ship (ground.py), from the probes
    "probe_requests": {},        # stamp: (terrain version, ground patch or None, extra directions) not read back yet
    "extra_probes": [],          # directions a check asks the terrain's height at
    "extra_heights": [],         # (direction, height) for the extra probes last read back
    "probe_latency": 3.0,        # frames from asking for probes to using their heights
    "cockpit": None,
    "instruments": None,
    "menu": None,
    "shown_mode": None,          # the mode show_mode last set up
    "debug_text": True,
    "planet_edit": None,         # terrain parameters the menu changes until Apply
    "scripted_controls": False,  # the ship's controls are set by a check, not read from the keys
    "impact_speed": 0.0,         # m/s, the fastest a contact point reached the surface at this frame
}
flight_planet = flight.Planet(radius=RADIUS, scale_height=RAYLEIGH_SCALE_HEIGHT)


class ChunkPool:
    """Slots of the GPU's ChunkPool buffer, the chunks to build into them
    this frame, and the built chunks whose heights have not come back yet."""

    def __init__(self):
        self.free = list(range(SLOTS - 1, -1, -1))
        self.jobs = []           # (jobA, jobB) per chunk to build this frame
        self.pending = {}        # slot: (node, stamp) until the slot's heights come back
        self.stamp = 0

    def make(self, node):
        """A slot for `node` with a job to build it, or None when the pool is
        full or this frame's jobs are taken."""
        if not self.free or len(self.jobs) == MAX_JOBS:
            return None
        slot = self.free.pop()
        self.stamp += 1
        self.jobs.append(((float(node.face), float(node.level), float(node.x), float(node.y)),
                          (float(slot), SKIRT_FRACTION * node.edge, float(self.stamp), 0.0)))
        self.pending[slot] = (node, float(self.stamp))
        return slot

    def release(self, node):
        self.pending.pop(node.chunk, None)
        self.free.append(node.chunk)

    def submit_jobs(self, probe_directions):
        """Hand this frame's jobs and the probes (direction and a value
        their results carry back) to BuildChunks, and start the next frame's
        list."""
        padded = self.jobs + [((0.0,) * 4, (0.0,) * 4)] * (MAX_JOBS - len(self.jobs))
        engine.set_custom_vec4_array("planet.jobA", [a for a, _ in padded])
        engine.set_custom_vec4_array("planet.jobB", [b for _, b in padded])
        engine.set_custom_uint("planet.jobCount", len(self.jobs))
        engine.set_custom_vec4_array("planet.probe", probe_directions)
        self.jobs = []

    def take_heights(self, heights):
        """Give each pending node whose job's heights arrived (rows of lowest,
        highest, stamp per slot) its height range."""
        for slot, (node, stamp) in list(self.pending.items()):
            if heights[slot, 2] == stamp:
                node.set_heights(float(heights[slot, 0]), float(heights[slot, 1]))
                del self.pending[slot]

    @property
    def used(self):
        return SLOTS - len(self.free)


pool = ChunkPool()


def new_tree():
    return QuadTree(RADIUS, pool.make, pool.release, max_level=MAX_LEVEL, split_factor=SPLIT_FACTOR,
                    heights=ROOT_HEIGHTS, height_pad=HEIGHT_PAD)


def site_frame():
    """The site's point on the sphere (height 0), its up, east and north."""
    up = cube_sphere.direction(SITE_FACE, SITE_UV, SITE_UV)
    east, north = local_frame(up)
    return scale(up, RADIUS), up, east, north


def site_ground():
    """The site's point on the ground, once its height is known (else on the sphere)."""
    origin, up, _, _ = site_frame()
    return add(origin, scale(up, state["site_height"] or 0.0))


def chunk_colour(node):
    """(r, g, b, grid strength) of a chunk in the current colour mode."""
    mode = COLOUR_MODES[state["colour_mode"]]
    if mode == "face":
        return FACE_COLOURS[node.face] + (1.0,)
    if mode == "level":
        return level_colour(node.level) + (1.0,)
    return TERRAIN_COLOUR + (0.0,)


def upload_chunk_indices():
    """The index list every chunk shares (grid, then skirts drawn from both
    sides), as cube_sphere.chunk_mesh makes it."""
    _, _, _, _, indices = cube_sphere.chunk_mesh(4, 0, 0, 0, QUADS, RADIUS, skirt_depth=1.0)
    state["index_count"] = len(indices)
    engine.set_buffer("ChunkIndices", indices)


def upload_terrain():
    """The terrain's parameters, and the pad around the site once its height is known."""
    for name, value in state["terrain"].items():
        engine.set_custom_vec4("planet.terrain." + name, tuple(value))
    _, up, _, _ = site_frame()
    known = state["site_height"] is not None
    engine.set_custom_vec4("planet.terrain.site", up + (state["site_height"] if known else 0.0,))
    engine.set_custom_vec4("planet.terrain.siteShape",
                           (SITE_FLAT_RADIUS, SITE_BLEND_RADIUS, RADIUS, 1.0 if known else 0.0))


def sun_direction():
    """Towards the sun, from the sun's elevation and azimuth seen from the site."""
    _, up, east, north = site_frame()
    elevation, azimuth = math.radians(state["sun_elevation"]), math.radians(state["sun_azimuth"])
    level = add(scale(east, math.cos(azimuth)), scale(north, math.sin(azimuth)))
    return normalize(add(scale(up, math.sin(elevation)), scale(level, math.cos(elevation))))


def upload_sky():
    """The sun's direction and the atmosphere's parameters."""
    engine.set_custom_vec4("planet.sunDirection", sun_direction() + (0.0,))
    density = state["density"]
    engine.set_custom_vec4("planet.atmosphere.radii",
                           (RADIUS, RADIUS + ATMOSPHERE_TOP, RAYLEIGH_SCALE_HEIGHT, MIE_SCALE_HEIGHT))
    engine.set_custom_vec4("planet.atmosphere.rayleigh", scale(RAYLEIGH, density) + (MIE_G,))
    engine.set_custom_vec4("planet.atmosphere.mie", (MIE_SCATTERING * density, MIE_ABSORPTION * density,
                                                     SUN_ILLUMINANCE, state["exposure"]))
    ozone = density if state["ozone"] else 0.0
    engine.set_custom_vec4("planet.atmosphere.ozone", scale(OZONE_ABSORPTION, ozone) + (OZONE_PEAK,))
    engine.set_custom_vec4("planet.atmosphere.settings",
                           (OZONE_HALF_WIDTH, 1.0 if state["multiple_scattering"] else 0.0, 0.0, 0.0))


def skirt_depth(node):
    """How far a built chunk's skirts reach below its edges, as
    chunk_build.comp makes them: SKIRT_FRACTION of its edge, or its grid's
    height range."""
    return max(SKIRT_FRACTION * node.edge, node.high - node.low)


def parent_split_distance(node):
    """The distance from its bounding sphere at which a node's parent splits
    (the parent's edge is twice the node's); 0 for the roots."""
    return state["tree"].split_factor * 2.0 * node.edge if node.level > 0 else 0.0


def terrain_floor():
    """Metres above the radius that no terrain goes below: the seabed's
    depth, or on land the continent's edge plus the hills' and the detail's
    heights, both with 10 % to spare (terrain.glsl's noise stays within
    [-1, 1])."""
    terrain = state["terrain"]
    return -1.1 * max(terrain["hills"][2], 0.05 * terrain["continent"][3] + terrain["hills"][1] + terrain["detail"][1])


def visible(node, occluder):
    return (in_view(node.bound_centre, node.bound_radius)
            and not hidden_by_horizon(node.bound_centre, node.bound_radius, state["position"], occluder))


def upload_drawn_chunks():
    """One indexed indirect draw per chunk the tree selected that is in
    view and not behind the horizon, and each drawn slot's centre relative
    to the camera (subtracted in double), colour, and its parent's split
    distance and the levels above it, for morph.glsl. The shadow cascades
    draw every selected chunk within their reach, in view or not."""
    all_selected = state["tree"].selected if state["tree"] is not None else []
    occluder = RADIUS + terrain_floor()
    selected = [node for node in all_selected if visible(node, occluder)]
    state["chunks_culled"] = len(all_selected) - len(selected)
    camera = state["position"]
    instances = np.zeros((SLOTS, 3, 4), dtype=np.float32)
    for node in all_selected:
        slot = node.chunk
        instances[slot, 0, :3] = sub(node.centre, camera)
        instances[slot, 1] = chunk_colour(node)
        instances[slot, 2, :2] = (parent_split_distance(node), node.level)
    engine.set_buffer("ChunkInstances", instances)
    # indexCount, instanceCount, firstIndex, vertexOffset, firstInstance
    draw = lambda node: (state["index_count"], 1, 0, node.chunk * VERTICES_PER_CHUNK, node.chunk)
    commands = np.array([draw(node) for node in selected], dtype=np.uint32).reshape(-1, 5)
    reach = SHADOW_SPLITS[-1] + SHADOW_REACH
    casters = [node for node in all_selected
               if length(sub(node.bound_centre, camera)) - node.bound_radius < reach]
    engine.set_buffer("ShadowDraws", np.uint32([len(casters)]))
    if casters:
        engine.set_buffer("ShadowDraws", np.array([draw(node) for node in casters], dtype=np.uint32),
                          offset=DRAWS_OFFSET)
    engine.set_buffer("ChunkDraws", np.uint32([len(selected)]))
    if len(selected):
        engine.set_buffer("ChunkDraws", commands, offset=DRAWS_OFFSET)
    # The ocean redraws the chunks whose height range reaches below sea level.
    wet = commands[[row for row, node in enumerate(selected) if node.low < 0.0]]
    state["ocean_chunks"] = len(wet)
    engine.set_buffer("OceanDraws", np.uint32([len(wet)]))
    if len(wet):
        engine.set_buffer("OceanDraws", wet, offset=DRAWS_OFFSET)


def upload_shadow_cascades(forward):
    """The sun's shadow cascades (shadows.py) for the view along `forward`,
    switched off with the menu's Shadows or while the sun is below the
    horizon at the camera."""
    sun = sun_direction()
    on = state["shadows"] and dot(sun, normalize(state["position"])) > -0.1
    width, height = engine.framebuffer_size
    aspect = width / height if width > 0 and height > 0 else 16.0 / 9.0
    cascades = shadows.fit_cascades(state["position"], forward, sun, FOV, aspect, SHADOW_SPLITS,
                                    SHADOW_RESOLUTION, SHADOW_REACH)
    engine.set_custom_mat4_array("planet.shadow.viewProj", [c.matrix for c in cascades])
    engine.set_custom_vec4("planet.shadow.splits", tuple(c.far for c in cascades))
    engine.set_custom_vec4("planet.shadow.texel", tuple(c.texel for c in cascades))
    engine.set_custom_vec4("planet.shadow.settings", (1.0 if on else 0.0, float(SHADOW_RESOLUTION), 0.0, 0.0))


def upload_orbit_path():
    """The path the ship would coast along (flight.orbit_path), relative to
    the camera (subtracted in double), as one indexed indirect draw for
    OrbitPath; none without a ship, with the path switched off, or when
    there is no path."""
    ship = state["ship"]
    points = []
    if ship is not None and state["orbit_path"]:
        points = flight.orbit_path(flight_planet, ship.position, ship.velocity, count=ORBIT_POINTS)
    if points:
        camera = state["position"]
        relative = np.zeros((len(points), 4), dtype=np.float32)
        relative[:, :3] = [sub(p, camera) for p in points]
        engine.set_buffer("OrbitPoints", relative)
        # count, padding, then indexCount, instanceCount, firstIndex, vertexOffset, firstInstance
        engine.set_buffer("OrbitDraws", np.uint32([1, 0, 0, 0, len(points), 1, 0, 0, 0]))
    else:
        engine.set_buffer("OrbitDraws", np.uint32([0]))


def read_back_heights():
    """Chunk heights into the tree's nodes, and the probes' heights when
    they were made with the current terrain version: the ground under the
    camera, the site's natural height, and the ground patch around the
    ship. Each probe result carries the stamp of the frame that asked for
    it, which also gives the probes' latency."""
    readback = engine.get_readback("ChunkHeights")
    if readback is not None:
        pool.take_heights(np.frombuffer(readback["data"], dtype=np.float32).reshape(SLOTS, 4))
    readback = engine.get_readback("ProbeHeights")
    if readback is None:
        return
    probes = np.frombuffer(readback["data"], dtype=np.float32).reshape(PROBES, 4)
    stamp = float(probes[0, 1])
    requests = state["probe_requests"]
    request = requests.get(stamp)
    for old in [s for s in requests if s <= stamp]:
        del requests[old]
    if request is None or request[0] != state["terrain_version"]:
        return
    state["probe_latency"] = state["frame"] + 1 - stamp
    state["ground"] = float(probes[0, 0])
    if state["site_height"] is None:
        state["site_height"] = float(probes[1, 0])
    _, patch, extra = request
    if patch is not None:
        patch.set_heights(probes[2:2 + PATCH_SIZE * PATCH_SIZE, 0])
        state["patch"] = patch
    first = PROBES - EXTRA_PROBES
    state["extra_heights"] = [(d, float(h)) for d, h in zip(extra, probes[first:first + len(extra), 0])]


def start_terrain():
    """Once the site's natural height is known: the pad at that height, and
    a tree to build the planet with it."""
    upload_terrain()
    state["terrain_version"] += 1
    state["ground"] = None
    state["patch"] = None
    state["tree"] = new_tree()


def regenerate():
    """Apply changed terrain parameters: every chunk is released, the
    site's height is measured again, and the planet is built again."""
    if state["tree"] is not None:
        for node in state["tree"].nodes():
            if node.chunk is not None:
                pool.release(node)
                node.chunk = None
    state["tree"] = None
    state["site_height"] = None
    state["ground"] = None
    state["patch"] = None
    state["terrain_version"] += 1
    state["planet_edit"] = {name: list(value) for name, value in state["terrain"].items()}
    upload_terrain()


def build_site():
    """Blocks around the site, in metres east, up and north of it."""
    _, up, east, north = site_frame()
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


def build_cockpit():
    """The cockpit mesh (cockpit.py), in the camera's space; hidden until ship mode."""
    positions, normals, colours = cockpit.cockpit_mesh()
    entity = engine.create_mesh(positions, normals=normals, colors=colours, name="Cockpit")
    engine.scene.set_material_vec4(entity, "baseColorFactor", (1.0, 1.0, 1.0, 1.0))
    engine.scene.set_material_float(entity, "gridStrength", 0.0)
    engine.scene.set_render_layer_mask(entity, 1)
    engine.scene.set_visible(entity, False)
    state["cockpit"] = entity


def show_mode():
    """What the mode shows: in the ship, the cockpit, its displays, the HUD."""
    ship = state["mode"] == "ship"
    engine.scene.set_visible(state["cockpit"], ship)
    state["instruments"].show(ship)
    engine.set_pass_enabled("HudSymbols", ship)
    if not ship:
        set_reentry_pass(False)
    state["shown_mode"] = state["mode"]


def set_reentry_pass(on):
    """The ReentryGlow pass on or off, when that changes."""
    if state["reentry_pass"] != on:
        engine.set_pass_enabled("ReentryGlow", on)
        state["reentry_pass"] = on


def build_hud():
    ui = engine.ui
    canvas = ui.create_canvas(reference_size=(1600, 900))
    lines = []
    for row in range(3):
        text = ui.create_text(canvas, "", size=22, color=(0.92, 0.92, 0.88, 1.0))
        ui.set_rect(text, anchor_min=(0, 0), anchor_max=(1, 0), pivot=(0, 0),
                    position=(20, 16 + 30 * row), size=(-40, 30))
        lines.append(text)
    state["hud"] = lines


# ── Camera ─────────────────────────────────────────────────────────────────

def altitude():
    """Height above the sphere of radius RADIUS (sea level)."""
    return length(state["position"]) - RADIUS


def surface_height():
    """The height of what is under the camera: the ground, or the sea where
    the ground is below sea level."""
    return max(state["ground"] or 0.0, 0.0)


def height_above_ground():
    return altitude() - surface_height()


def keep_above_ground():
    floor = RADIUS + surface_height() + MIN_ALTITUDE
    if length(state["position"]) < floor:
        state["position"] = scale(normalize(state["position"]), floor)


def camera_basis():
    """(right, up, forward), with the horizon level: up lies in the plane of
    the forward direction and the planet's radial up."""
    radial = normalize(state["position"])
    forward = state["forward"]
    right = normalize(cross(forward, radial))
    return right, cross(right, forward), forward


def look(dx, dy):
    radial = normalize(state["position"])
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
    speed = max(height_above_ground(), 2.0) * 0.5 * state["speed_factor"]
    if engine.input.is_key_down(keys.LEFT_SHIFT):
        speed *= 4.0
    if engine.input.is_key_down(keys.LEFT_CONTROL):
        speed *= 0.25
    state["position"] = add(state["position"], scale(normalize(move), speed * dt))
    state["speed"] = speed


def teleport(preset):
    """Presets 1 - 7 move the free camera; 8 and 9 place the ship."""
    _, up, east, north = site_frame()
    ground = site_ground()
    state["teleport_when_known"] = preset if preset in (1, 2, 8) and state["site_height"] is None else None
    state["ground"] = None
    state["patch"] = None
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

def board_ship():
    """Ship mode; a ship at rest where the camera is, facing its way, if there is none yet."""
    if state["ship"] is None:
        right, up, forward = camera_basis()
        state["ship"] = flight.Ship(position=state["position"],
                                    orientation=flight.quat_from_basis(right, up, scale(forward, -1.0)))
    state["mode"] = "ship"


def leave_ship():
    """Camera mode, from where the ship is, facing its way with the horizon level."""
    state["mode"] = "camera"
    _, _, forward = state["ship"].axes()
    radial = normalize(state["position"])
    if abs(dot(forward, radial)) > 0.99:
        forward = local_frame(radial)[1]
    state["forward"] = normalize(forward)


def place_ship(preset):
    """8: at rest on the pad 7 m south of the blocks, facing north,
    translation assist on. 9: in a circular orbit ORBIT_HEIGHT above the
    blocks, moving east with the nose 40 degrees down, where the planet is,
    assist off."""
    _, up, east, north = site_frame()
    if preset == 8:
        position = add(site_ground(), add(scale(up, SHIP_CLEARANCE), scale(north, -7.0)))
        forward, velocity, assist = north, (0.0, 0.0, 0.0), True
    else:
        position, assist = scale(up, RADIUS + ORBIT_HEIGHT), False
        velocity = scale(east, flight.circular_speed(flight_planet, ORBIT_HEIGHT))
        down = math.radians(40.0)
        forward = normalize(add(scale(east, math.cos(down)), scale(up, -math.sin(down))))
    up = normalize(sub(up, scale(forward, dot(up, forward))))
    right = cross(forward, up)
    state["ship"] = flight.Ship(position=position, velocity=velocity,
                                orientation=flight.quat_from_basis(right, up, scale(forward, -1.0)))
    state["controls"] = flight.Controls(assist=assist)
    state["mode"] = "ship"
    state["position"] = position
    state["forward"] = forward


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


def read_gamepad_buttons():
    """Gamepad buttons on the frame they are pressed, as the keys of
    GAMEPAD_KEYS."""
    held = set()
    if engine.input.is_gamepad_connected():
        held = {b for b in GAMEPAD_KEYS if engine.input.is_gamepad_button_down(b)}
    for button in held - state["gamepad_held"]:
        on_key(GAMEPAD_KEYS[button])
    state["gamepad_held"] = held


def ground_patch_request(ship):
    """A ground patch to measure around where the ship will be when its
    heights come back: ahead along its velocity over the ground by the
    probes' latency (times the warp), reaching that far again either way
    beyond the ship's contact points."""
    radial = normalize(ship.position)
    along = sub(ship.velocity, scale(radial, dot(ship.velocity, radial)))
    ahead = state["probe_latency"] * state["frame_ms"] / 1000.0 * WARPS[state["warp"]]
    half = SHIP_REACH + PATCH_MARGIN + length(along) * ahead
    return GroundPatch(RADIUS, add(ship.position, scale(along, ahead)), 2.0 * half / (PATCH_SIZE - 1))


def ground_under_ship():
    """The ground patch's query for the ship, or None while there is no patch."""
    patch = state["patch"]
    return patch.query if patch is not None else None


def fly_ship(dt):
    """Advance the ship by this frame's time (times the warp) in fixed
    steps, against the ground patch under it."""
    ship = state["ship"]
    state["accumulator"] += dt * WARPS[state["warp"]]
    ground = ground_under_ship()
    state["impact_speed"] = 0.0
    steps = 0
    while state["accumulator"] >= FLIGHT_STEP and steps < MAX_STEPS_PER_FRAME:
        ship.step(flight_planet, state["controls"], FLIGHT_STEP, ground)
        state["impact_speed"] = max(state["impact_speed"], ship.impact_speed)
        state["accumulator"] -= FLIGHT_STEP
        state["flight_time"] += FLIGHT_STEP
        steps += 1
    if steps == MAX_STEPS_PER_FRAME:
        state["accumulator"] = 0.0      # too far behind: drop the rest rather than fall further behind
    state["position"] = ship.position
    state["forward"] = ship.axes()[2]


def in_view(centre, radius):
    """Whether a sphere around `centre` (planet space) is roughly in view: a
    cone around the camera's forward direction that holds the screen."""
    to_centre = sub(centre, state["position"])
    distance = length(to_centre)
    if distance <= radius:
        return True
    width, height = engine.framebuffer_size
    aspect = width / height if width > 0 and height > 0 else 16.0 / 9.0
    half_diagonal = math.atan(math.tan(math.radians(FOV) * 0.5) * math.sqrt(1.0 + aspect * aspect))
    angle = math.acos(max(-1.0, min(1.0, dot(to_centre, state["forward"]) / distance)))
    return angle - math.asin(min(1.0, radius / distance)) < half_diagonal


# ── Per frame ──────────────────────────────────────────────────────────────

def update_level_of_detail():
    tree = state["tree"]
    state["frame"] += 1
    if tree is not None and not state["frozen"]:
        tree.update(state["position"], state["frame"])
        tree.build(MAX_JOBS,
                   priority=lambda distance, node: (not in_view(node.bound_centre, node.bound_radius), distance))
        tree.evict(state["frame"])
    # The probes: the ground under the camera, the site, and the ground patch around the ship.
    _, site_up, _, _ = site_frame()
    ship = state["ship"]
    patch = ground_patch_request(ship) if state["mode"] == "ship" and not ship.crashed else None
    probes = [normalize(state["position"]), site_up] + (patch.directions if patch is not None else [])
    extra = list(state["extra_probes"][:EXTRA_PROBES])
    probes += [(0.0, 1.0, 0.0)] * (PROBES - EXTRA_PROBES - len(probes))
    probes += extra + [(0.0, 1.0, 0.0)] * (EXTRA_PROBES - len(extra))
    stamp = float(state["frame"])
    state["probe_requests"][stamp] = (state["terrain_version"], patch, extra)
    pool.submit_jobs([tuple(d) + (stamp,) for d in probes])


def frame(dt):
    read_back_heights()
    if state["tree"] is None and state["site_height"] is not None:
        start_terrain()
        if state["teleport_when_known"]:
            teleport(state["teleport_when_known"])

    if not state["scripted_controls"]:
        read_gamepad_buttons()
    if state["mode"] == "ship":
        if not state["scripted_controls"]:
            read_ship_controls(dt)
        fly_ship(dt)
    else:
        if engine.input.is_mouse_button_down(keys.MOUSE_RIGHT):
            dx, dy = engine.input.get_mouse_delta()
            look(dx, dy)
        state["speed"] = 0.0
        fly(dt)
        keep_above_ground()
    update_level_of_detail()

    # Camera-relative placement: subtract in double, then hand floats over.
    camera = state["position"]
    engine.set_custom_vec4("planet.cameraFromCentre", camera + (0.0,))
    upload_drawn_chunks()
    upload_orbit_path()
    ground = site_ground()
    for entity, offset in blocks:
        engine.scene.set_position(entity, sub(add(ground, offset), camera))
    right, up, forward = state["ship"].axes() if state["mode"] == "ship" else camera_basis()
    view_rotation = euler_from_basis(right, up, forward)
    engine.scene.set_rotation(state["camera"], view_rotation)
    upload_shadow_cascades(forward)
    if state["mode"] != state["shown_mode"]:
        show_mode()
    if state["mode"] == "ship":
        update_cockpit(right, up, forward, view_rotation)
    state["menu"].update()

    state["frame_ms"] = 0.9 * state["frame_ms"] + 0.1 * dt * 1000.0
    if state["hud"] is not None:
        update_hud()
    return True


def update_cockpit(right, up, forward, view_rotation):
    """The cockpit and its displays turned with the view, the HUD's
    directions, and the instruments' readings."""
    ship = state["ship"]
    controls = state["controls"]
    engine.scene.set_position(state["cockpit"], (0.0, 0.0, 0.0))
    engine.scene.set_rotation(state["cockpit"], view_rotation)

    def to_world(v):
        return add(add(scale(right, v[0]), scale(up, v[1])), scale(forward, -v[2]))

    state["instruments"].place(to_world, euler_from_basis)

    radial = normalize(ship.position)
    speed = length(ship.velocity)
    engine.set_custom_vec4("planet.hud.up", radial + (0.0,))
    prograde = scale(ship.velocity, 1.0 / speed) if speed > 1.0 else forward
    engine.set_custom_vec4("planet.hud.prograde", prograde + (1.0 if speed > 1.0 else 0.0,))
    glow = flight.reentry_glow(flight_planet, ship.position, ship.velocity)
    engine.set_custom_vec4("planet.hud.reentry", (glow, 0.0, 0.0, 0.0))
    set_reentry_pass(glow > 0.0)

    east, north = local_frame(radial)
    level = sub(forward, scale(radial, dot(forward, radial)))
    heading = math.degrees(math.atan2(dot(level, east), dot(level, north))) % 360.0
    periapsis, apoapsis = flight.orbit(flight_planet, ship.position, ship.velocity)
    density = flight_planet.density(ship.position)
    state["instruments"].update({
        "speed": speed,
        "climb": dot(ship.velocity, radial),
        "height_text": distance_text(height_above_ground()),
        "agl_text": "%11s" % distance_text(height_above_ground()),
        "asl_text": "%11s" % distance_text(altitude()),
        "heading": heading,
        "throttle": controls.throttle,
        "assist": controls.assist,
        "warp": WARPS[state["warp"]],
        "contact": contact_text(ship),
        "periapsis_text": "%11s" % ("below ground" if periapsis < 0.0 else distance_text(periapsis)),
        "apoapsis_text": "%11s" % ("escape" if apoapsis == math.inf else distance_text(apoapsis)),
        "circular": flight.circular_speed(flight_planet, max(altitude(), 0.0)),
        "density": density,
        "dynamic_pressure": 0.5 * density * speed * speed,
        "glow": glow,
    })


def contact_text(ship):
    if ship.crashed:
        return "CRASHED (8 or 9 for a new ship)"
    return "LANDED" if ship.on_ground else ""


def update_hud():
    tree = state["tree"]
    if tree is None:
        return
    for line in state["hud"]:
        engine.ui.set_visible(line, state["debug_text"])
    terrain = state["terrain"]
    cached = sum(1 for node in tree.nodes() if node.chunk is not None)
    deepest = max(node.level for node in tree.selected)
    if state["mode"] == "ship":
        ship, controls = state["ship"], state["controls"]
        periapsis, apoapsis = flight.orbit(flight_planet, ship.position, ship.velocity)
        climb = dot(ship.velocity, normalize(ship.position))
        engine.ui.set_text(state["hud"][0], "SHIP    throttle %3.0f%%    assist %s    speed %.0f m/s    climb %+.0f m/s    "
                           "above ground %s    above sea level %s    periapsis %s    apoapsis %s    warp x%d%s    "
                           "frame %.1f ms"
                           % (controls.throttle * 100.0, "on" if controls.assist else "off", length(ship.velocity),
                              climb, distance_text(height_above_ground()), distance_text(altitude()),
                              "below ground" if periapsis < 0.0 else distance_text(periapsis),
                              "escape" if apoapsis == math.inf else distance_text(apoapsis),
                              WARPS[state["warp"]], "    " + contact_text(ship), state["frame_ms"]))
    else:
        engine.ui.set_text(state["hud"][0], "CAMERA    above ground %s    above sea level %s    speed %.0f m/s    "
                           "speed factor %.2f    frame %.1f ms"
                           % (distance_text(height_above_ground()), distance_text(altitude()), state["speed"],
                              state["speed_factor"], state["frame_ms"]))
    engine.ui.set_text(state["hud"][1], "chunks drawn %d of %d (ocean %d)    built %d    missing %d    "
                       "pool %d / %d    deepest level %d    split factor %.2f%s    colour: %s"
                       % (len(tree.selected) - state["chunks_culled"], len(tree.selected), state["ocean_chunks"],
                          cached, len(tree.missing), pool.used, SLOTS,
                          deepest,
                          tree.split_factor, "    LOD FROZEN" if state["frozen"] else "",
                          COLOUR_MODES[state["colour_mode"]]))
    engine.ui.set_text(state["hud"][2], "seed %.1f %.1f %.1f    mountains %.0f m    hills %.0f m    "
                       "sun %.0f deg up at the site    atmosphere density x%.2f    exposure %.3f"
                       % (terrain["seed"][0], terrain["seed"][1], terrain["seed"][2],
                          terrain["mountains"][1], terrain["hills"][1], state["sun_elevation"],
                          state["density"], state["exposure"]))


def on_init():
    camera = engine.scene.create_entity("Camera")
    engine.scene.add_component(camera, "Camera")
    engine.scene.set_camera_main(camera, True)
    engine.scene.set_camera_fov(camera, FOV)
    engine.scene.set_camera_near(camera, NEAR_PLANE)
    engine.scene.set_camera_reverse_z(camera, True)
    state["camera"] = camera

    upload_sky()
    engine.set_custom_float("planet.radius", RADIUS)
    upload_terrain()
    upload_chunk_indices()
    engine.set_buffer("OrbitIndices", np.arange(ORBIT_POINTS, dtype=np.uint32))
    build_site()
    build_hud()
    build_cockpit()
    state["instruments"] = Instruments(engine, cockpit.SCREENS)
    state["planet_edit"] = {name: list(value) for name, value in state["terrain"].items()}
    state["menu"] = SettingsMenu(engine, "Settings   (F1 to hide)", menu_items())
    teleport(1)

    # Before the transform system (priority 0), so this frame's positions are used.
    engine.ecs.add_system("PlanetFrame", frame, priority=-1)
    if args.capture:
        engine.ecs.add_system("PlanetCapture", CaptureTour(args.capture), priority=-2)
    elif args.verify:
        engine.ecs.add_system("PlanetVerify", Verify(), priority=-2)
    elif args.orbit_check:
        engine.ecs.add_system("PlanetOrbitCheck", OrbitCheck(), priority=-2)
    elif args.morph_check:
        engine.ecs.add_system("PlanetMorphCheck", MorphCheck(), priority=-2)
    elif args.crack_check:
        engine.ecs.add_system("PlanetCrackCheck", CrackCheck(), priority=-2)
    elif args.landing_check:
        engine.ecs.add_system("PlanetLandingCheck", LandingCheck(), priority=-2)
    print(__doc__.split("Keys:")[1].rstrip(), flush=True)


def menu_items():
    """The settings menu (menu.py): the simulation's settings take effect at
    once; the planet's are kept in planet_edit until Apply builds it again."""
    edit = lambda name, index: (lambda: state["planet_edit"][name][index])

    def edit_set(name, index):
        def setter(value):
            state["planet_edit"][name][index] = value
        return setter

    def set_and(key, then):
        def setter(value):
            state[key] = value
            then()
        return setter

    def set_density(value):
        state["density"] = value
        flight_planet.density_factor = value
        upload_sky()

    def set_ocean(on):
        state["ocean"] = on
        engine.set_pass_enabled("Ocean", on)

    def set_assist(on):
        state["controls"].assist = on

    def set_split(value):
        if state["tree"] is not None:
            state["tree"].split_factor = value

    def apply_planet():
        state["terrain"] = {name: list(value) for name, value in state["planet_edit"].items()}
        regenerate()

    return [
        {"heading": "Simulation"},
        {"slider": "Time warp", "min": 0, "max": len(WARPS) - 1, "get": lambda: state["warp"],
         "set": lambda v: state.update(warp=int(round(v))), "show": lambda v: "x%d" % WARPS[int(round(v))]},
        {"toggle": "Translation assist", "get": lambda: state["controls"].assist, "set": set_assist},
        {"slider": "Exposure", "min": 0.05, "max": 2.0, "get": lambda: state["exposure"],
         "set": set_and("exposure", upload_sky), "format": "%.2f"},
        {"slider": "Atmosphere density", "min": 0.25, "max": 4.0, "get": lambda: state["density"],
         "set": set_density, "format": "x%.2f"},
        {"toggle": "Multiple scattering", "get": lambda: state["multiple_scattering"],
         "set": set_and("multiple_scattering", upload_sky)},
        {"toggle": "Ozone", "get": lambda: state["ozone"], "set": set_and("ozone", upload_sky)},
        {"slider": "Sun elevation", "min": -20.0, "max": 90.0, "get": lambda: state["sun_elevation"],
         "set": set_and("sun_elevation", upload_sky), "format": "%.0f deg"},
        {"slider": "Sun azimuth", "min": 0.0, "max": 360.0, "get": lambda: state["sun_azimuth"],
         "set": set_and("sun_azimuth", upload_sky), "format": "%.0f deg"},
        {"slider": "Split factor", "min": 0.5, "max": 4.0,
         "get": lambda: state["tree"].split_factor if state["tree"] is not None else SPLIT_FACTOR,
         "set": set_split, "format": "%.2f"},
        {"toggle": "Ocean", "get": lambda: state["ocean"], "set": set_ocean},
        {"toggle": "Orbit path", "get": lambda: state["orbit_path"], "set": lambda on: state.update(orbit_path=on)},
        {"toggle": "Shadows", "get": lambda: state["shadows"], "set": lambda on: state.update(shadows=on)},
        {"toggle": "Wireframe", "get": lambda: state["wireframe"], "set": set_wireframe},
        {"toggle": "Debug text", "get": lambda: state["debug_text"], "set": lambda on: state.update(debug_text=on)},
        {"button": "Colour: terrain / face / level",
         "do": lambda: set_colour_mode((state["colour_mode"] + 1) % len(COLOUR_MODES))},
        {"heading": "Planet"},
        {"button": "New seed", "do": new_seed},
        {"slider": "Mountain height", "min": 0.0, "max": 8000.0, "get": edit("mountains", 1),
         "set": edit_set("mountains", 1), "format": "%.0f m"},
        {"slider": "Hill height", "min": 0.0, "max": 600.0, "get": edit("hills", 1),
         "set": edit_set("hills", 1), "format": "%.0f m"},
        {"slider": "Continent frequency", "min": 0.5, "max": 4.0, "get": edit("continent", 0),
         "set": edit_set("continent", 0), "format": "%.2f"},
        {"slider": "Sea level bias", "min": -0.4, "max": 0.4, "get": edit("continent", 2),
         "set": edit_set("continent", 2), "format": "%+.2f"},
        {"slider": "Ocean depth", "min": 0.0, "max": 4000.0, "get": edit("hills", 2),
         "set": edit_set("hills", 2), "format": "%.0f m"},
        {"slider": "Detail height", "min": 0.0, "max": 30.0, "get": edit("detail", 1),
         "set": edit_set("detail", 1), "format": "%.1f m"},
        {"slider": "Erosion", "min": 0.0, "max": 4.0, "get": edit("detail", 3),
         "set": edit_set("detail", 3), "format": "%.2f"},
        {"button": "Apply: build the planet again", "do": apply_planet},
    ]


def new_seed():
    state["terrain"]["seed"] = [random.uniform(-100.0, 100.0) for _ in range(3)] + [0.0]
    regenerate()


def set_wireframe(on):
    state["wireframe"] = on
    engine.set_pass_enabled("Wireframe", on)


def set_colour_mode(mode):
    state["colour_mode"] = mode


def change_terrain(name, index, factor):
    state["terrain"][name][index] *= factor
    regenerate()


def on_key(key):
    tree = state["tree"]
    if keys.NUM_1 <= key <= keys.NUM_9:
        teleport(key - keys.NUM_0)
    elif key == keys.TAB:
        if state["mode"] == "ship":
            leave_ship()
        else:
            board_ship()
    elif key == keys.L:
        state["controls"].assist = not state["controls"].assist
    elif key == keys.BACKSPACE:
        state["controls"].throttle = 0.0
    elif key in (keys.COMMA, keys.PERIOD):
        state["warp"] = max(0, min(len(WARPS) - 1, state["warp"] + (1 if key == keys.PERIOD else -1)))
    elif key == keys.O:
        state["ocean"] = not state["ocean"]
        engine.set_pass_enabled("Ocean", state["ocean"])
    elif key == keys.N:
        new_seed()
    elif key == keys.F1:
        state["menu"].show(not state["menu"].visible)
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
        state["sun_elevation"] = max(-90.0, min(90.0, state["sun_elevation"] + (3.0 if key == keys.X else -3.0)))
        upload_sky()
    elif key in (keys.T, keys.Y):
        state["density"] *= 1.25 if key == keys.T else 1 / 1.25
        flight_planet.density_factor = state["density"]
        upload_sky()
    elif key in (keys.B, keys.V):
        state["exposure"] *= 1.25 if key == keys.B else 1 / 1.25
        upload_sky()
    elif key == keys.F:
        set_wireframe(not state["wireframe"])
    elif key == keys.C:
        set_colour_mode((state["colour_mode"] + 1) % len(COLOUR_MODES))
    elif key == keys.K:
        state["frozen"] = not state["frozen"]
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
    state["speed_factor"] = max(0.01, min(100.0, state["speed_factor"] * 1.25 ** y_offset))


def settled():
    """The tree made, no chunks missing, and every built chunk's heights back."""
    return state["tree"] is not None and not state["tree"].missing and not pool.pending


def mid_reentry():
    """The ship 17 km above the blocks at 640 m/s eastwards, 8 degrees
    below level, its nose along its path: in the reentry glow."""
    _, up, east, _ = site_frame()
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


def near_mountain(sun_elevation, sun_azimuth, top=False):
    """The free camera 2.7 km above the sphere, 600 m south-west of a peak
    60 km from the site at 67.5 degrees from east (2,460 m high with the
    default terrain), looking north-east at it and down 12 degrees; with
    `top`, 2,520 m above the sphere over the peak, looking south-west and
    down 20 degrees. The sun at `sun_elevation` and `sun_azimuth` (degrees,
    from east towards north) seen from the peak."""
    _, up, east, north = site_frame()
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
    state["sun_elevation"] = math.degrees(math.asin(max(-1.0, min(1.0, dot(sun, up)))))
    state["sun_azimuth"] = math.degrees(math.atan2(dot(sun, north), dot(sun, east))) % 360.0
    upload_sky()


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
        ("preset_1_wireframe_levels", 1, True, 2, None, DAY_SUN),
        ("preset_2_wireframe", 2, True, 0, None, DAY_SUN),
        ("preset_2_twilight", 2, False, 0, None, (-3.0, 90.0)),
        ("orbit_from_afar", 9, False, 0, None, DAY_SUN, lambda: teleport(4)),
        ("reentry", 9, False, 0, None, DAY_SUN, mid_reentry),
        ("mountain_close", 7, False, 0, None, DAY_SUN, lambda: near_mountain(40.0, 160.0)),
        ("mountain_close_low_sun", 7, False, 0, None, DAY_SUN, lambda: near_mountain(10.0, 135.0)),
        ("mountain_top", 7, False, 0, None, DAY_SUN, lambda: near_mountain(15.0, 135.0, top=True)),
        ("mountain_top_no_detail", 7, False, 0, ("detail", 1, 0.0), DAY_SUN,
         lambda: near_mountain(15.0, 135.0, top=True)),
        ("preset_3_mountains_higher", 3, False, 0, ("mountains", 1, 1.5), DAY_SUN),
    ]

    def __init__(self, directory):
        os.makedirs(directory, exist_ok=True)
        self.directory = directory
        self.shot = 0
        self.frames = 0
        self.captured = None

    def __call__(self, dt):
        if state["site_height"] is None:
            return True
        name, preset, wireframe, colour_mode, change, sun, *setup = self.SHOTS[self.shot]
        self.frames += 1
        if self.frames == 1:
            if change is not None:
                change_terrain(*change)
            state["sun_elevation"], state["sun_azimuth"] = sun
            upload_sky()
            teleport(preset)
            set_wireframe(wireframe)
            set_colour_mode(colour_mode)
            state["menu"].show(False)
            for action in setup:
                action()
        elif self.captured is None:
            done = self.frames >= self.MIN_FRAMES and settled()
            if done or self.frames >= self.MAX_FRAMES:
                path = os.path.join(self.directory, name + ".png")
                ok = engine.capture_screenshot(path)
                selected = len(state["tree"].selected)
                print("[planet] %s after %d frames%s, %d of %d chunks drawn, frame %.1f ms"
                      % (path if ok else "screenshot failed", self.frames, "" if done else " (not settled)",
                         selected - state["chunks_culled"], selected, state["frame_ms"]), flush=True)
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
        if state["site_height"] is None:
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
        chunks = 0
        for node in state["tree"].nodes():
            if node.chunk is None:
                continue
            built = gpu[node.chunk]
            # The skirt's depth as the GPU works it out, in 32-bit: the job's, or the grid's height range.
            depth = max(np.float32(SKIRT_FRACTION * node.edge), built[:grid, 11].max() - built[:grid, 11].min())
            centre, positions, directions, uvs, _ = cube_sphere.chunk_mesh(
                node.face, node.level, node.x, node.y, QUADS, RADIUS, skirt_depth=float(depth))
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
            chunks += 1
        print("[planet] checked %d chunks against cube_sphere.chunk_mesh:" % chunks, flush=True)
        for name, value in worst.items():
            print("[planet]   largest %s difference %.3g" % (name, value), flush=True)
        print("[planet]   smallest normal . direction %.3f" % lowest_normal_dot, flush=True)
        ok = (chunks > 0 and worst["position (ULPs)"] <= self.TOLERANCE_ULPS and worst["direction (ULPs)"] == 0
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
        _, up, _, north = site_frame()
        state["mode"] = "camera"
        state["position"] = scale(up, RADIUS + (state["site_height"] or 0.0) + self.START)
        state["forward"] = normalize(add(north, scale(up, -0.3)))
        return settled()

    def factor(self, node, camera):
        if node.level == 0:
            return 1.0
        split = state["tree"].split_factor * 2.0 * node.edge
        distance = max(0.0, length(sub(camera, node.bound_centre)) - node.bound_radius)
        t = max(0.0, min(1.0, (distance - MORPH_START * split) / ((1.0 - MORPH_START) * split)))
        return t * t * (3.0 - 2.0 * t)

    def __call__(self, dt):
        tree = state["tree"]
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
        _, up, east, north = site_frame()
        height = self.START * (self.END / self.START) ** (self.frame / self.FRAMES)
        self.travelled += height / 3.0 / 60.0
        spot = normalize(add(up, scale(north, self.travelled / RADIUS)))
        new_position = scale(spot, RADIUS + (state["site_height"] or 0.0) + height)
        self.step_length = length(sub(new_position, camera))
        state["mode"] = "camera"
        state["position"] = new_position
        state["forward"] = normalize(add(north, scale(spot, -0.3)))
        return True


def morph_factor(node, point, level=1):
    """morph.glsl's factor towards the surface of a node's ancestor `level`
    levels up, for a vertex at `point` (planet space) and the camera's
    present position; 0 where the node has no such ancestor."""
    if level > min(MORPH_LEVELS, node.level):
        return 0.0
    split = parent_split_distance(node) * 2.0 ** (level - 1)
    distance = length(sub(point, state["position"]))
    t = max(0.0, min(1.0, (distance - MORPH_START * split) / ((1.0 - MORPH_START) * split)))
    return t * t * (3.0 - 2.0 * t)


def morphed_point(node, built, i, j):
    """Grid vertex (i, j) of a built chunk (its ChunkPool rows `built`) in
    planet space, morphed as morph.glsl morphs it for the camera's present
    position: towards its parent's surface, then its grandparent's, and so
    on."""
    row = QUADS + 1
    vertex = lambda ij: add(node.centre, tuple(float(c) for c in built[ij[1] * row + ij[0], :3]))
    own = vertex((i, j))
    point = own
    for level in range(1, MORPH_LEVELS + 1):
        f = morph_factor(node, own, level)
        if f <= 0.0:
            break
        surface = (0.0, 0.0, 0.0)
        for ij, w in cube_sphere.stride_weights(i, j, 1 << level, QUADS):
            surface = add(surface, scale(vertex(ij), w))
        point = add(point, scale(sub(surface, point), f))
    return point


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
        if state["tree"] is None:
            return True
        self.frames += 1
        if self.frames == 1:
            state["frozen"] = False
            self.VIEWS[self.view][1]()
        elif not self.requested:
            if (self.frames >= self.MIN_FRAMES and settled()) or self.frames >= self.MAX_FRAMES:
                state["frozen"] = True
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
        selected = state["tree"].selected
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
            fine_skirt = skirt_depth(node)
            for indices, (u, v) in sides:
                # Past a face's edge the cube point leaves the cube; normalising puts it on the sphere.
                other = selected_at(normalize(cube_sphere.spherify(cube_sphere.cube_point(node.face, u, v))))
                if other is None or other.level >= node.level:
                    continue
                neighbour = other.level
                edges += 1
                step = 1 << (node.level - neighbour)
                coarse_skirt = skirt_depth(other)
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
    """--landing-check: measures the ground 9 km below spots on rings 10 to
    60 km around the site with the free camera, takes the highest, and
    brings the ship down onto it from 500 m above with translation assist
    and full downward thrust asked for; once it touches, lets go of the
    controls for five seconds. Prints the touchdown speed, the slope there,
    how far each gear point is below the ground patch's surface, how far
    the patch's height under the ship is from the probe's, each gear
    point's height above the terrain probed at its own direction, and the
    ship's speed. Then a ship coasts straight at the spot from 1.5 km up
    and 1.5 km away at 400 m/s and 16x warp; prints whether it crashed and
    how high its centre ended above the ground. Exits with status 0 when
    the ship touched down under half the gear's crash speed and stood on
    its gear (every gear point within 5 cm of both the patch's surface and
    the terrain), at rest, uncrashed, and the diving ship crashed with its
    centre at least 1 m above the ground and within reach of it; 1
    otherwise."""

    RINGS = (10_000.0, 20_000.0, 35_000.0, 60_000.0)
    AZIMUTHS = 16
    FRAMES_PER_SPOT = 8                 # more than the probes' latency
    GEAR_TOLERANCE = 0.05
    REST_SECONDS = 5.0
    TIMEOUT_SECONDS = 90.0
    DIVE_SPEED = 400.0
    DIVE_MIN_CENTRE_HEIGHT = 1.0        # m; half the gear's depth below the centre

    def __init__(self):
        _, up, east, north = site_frame()
        self.spots = []
        for ring in self.RINGS:
            for k in range(self.AZIMUTHS):
                azimuth = 2.0 * math.pi * k / self.AZIMUTHS
                level = add(scale(east, math.cos(azimuth)), scale(north, math.sin(azimuth)))
                self.spots.append(normalize(add(up, scale(level, ring / RADIUS))))
        self.spot = -1
        self.frames = 0
        self.best = None                # (ground height, direction)
        self.touched = None             # flight time when the ship touched
        self.diving = None              # flight time when the dive started
        self.touchdown_speed = 0.0

    def __call__(self, dt):
        if state["tree"] is None:
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
        if self.spot >= 0 and state["ground"] is not None:
            height = state["ground"]
            if self.best is None or height > self.best[0]:
                self.best = (height, self.spots[self.spot])
                self.best_place = (self.RINGS[self.spot // self.AZIMUTHS],
                                   360.0 * (self.spot % self.AZIMUTHS) / self.AZIMUTHS)
        self.spot += 1
        self.frames = 0
        if self.spot < len(self.spots):
            state["mode"] = "camera"
            state["position"] = scale(self.spots[self.spot], RADIUS + 9_000.0)
            state["ground"] = None
        else:
            height, up = self.best
            east, north = local_frame(up)
            state["ship"] = flight.Ship(position=scale(up, RADIUS + height + 500.0),
                                        orientation=flight.quat_from_basis(east, up, scale(north, -1.0)))
            state["controls"] = flight.Controls(vertical=-1.0, assist=True)
            state["scripted_controls"] = True
            state["mode"] = "ship"
            state["warp"] = 0
            state["patch"] = None
            self.start = state["flight_time"]
            print("[planet] the highest spot found: %.0f m, %.0f km from the site at %.1f degrees from east"
                  % (height, self.best_place[0] / 1000.0, self.best_place[1]), flush=True)
        return True

    def land(self):
        ship = state["ship"]
        now = state["flight_time"]
        if self.touched is None:
            self.touchdown_speed = max(self.touchdown_speed, state["impact_speed"])
        if self.touched is None and (ship.on_ground or ship.crashed):
            self.touched = now
            state["controls"] = flight.Controls(assist=True)
        if self.touched is not None:
            q = ship.orientation
            state["extra_probes"] = [normalize(add(ship.position, flight.quat_rotate(q, arm)))
                                     for arm in ship.params.gear]
        if self.touched is not None and now - self.touched >= self.REST_SECONDS or now - self.start > self.TIMEOUT_SECONDS:
            self.report(ship)
        return True

    def report(self, ship):
        patch = state["patch"]
        ok = patch is not None and not ship.crashed and ship.on_ground
        if patch is not None:
            depth, normal = patch.query(ship.position)
            radial = normalize(ship.position)
            slope = math.degrees(math.acos(max(-1.0, min(1.0, dot(normal, radial)))))
            patch_height = length(ship.position) - RADIUS + depth / dot(radial, normal)
            q = ship.orientation
            gear = [patch.query(add(ship.position, flight.quat_rotate(q, arm)))[0] for arm in ship.params.gear]
            print("[planet] touched down at %.1f m/s on a %.1f degree slope at %.0f m:"
                  % (self.touchdown_speed, slope, patch_height), flush=True)
            print("[planet]   gear points below the surface: %s m" % ", ".join("%+.3f" % d for d in gear),
                  flush=True)
            print("[planet]   patch height under the ship minus the probe's: %+.3f m"
                  % (patch_height - (state["ground"] or 0.0)), flush=True)
            # Each gear point's height above the terrain itself, measured at its direction.
            terrain = [length(add(ship.position, flight.quat_rotate(q, arm))) - RADIUS - max(h, 0.0)
                       for arm, (_, h) in zip(ship.params.gear, state["extra_heights"])]
            print("[planet]   gear points above the terrain at their directions: %s m"
                  % ", ".join("%+.3f" % d for d in terrain), flush=True)
            ok = (ok and self.touchdown_speed < 0.5 * ship.params.gear_crash_speed
                  and max(abs(d) for d in gear) <= self.GEAR_TOLERANCE and len(terrain) == len(gear)
                  and max(abs(d) for d in terrain) <= self.GEAR_TOLERANCE)
        speed = length(ship.velocity)
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
        target = scale(up, RADIUS + height)
        position = add(target, add(scale(up, 1_500.0), scale(north, -1_500.0)))
        state["ship"] = flight.Ship(position=position,
                                    velocity=scale(normalize(sub(target, position)), self.DIVE_SPEED),
                                    orientation=flight.quat_from_basis(east, up, scale(north, -1.0)))
        state["controls"] = flight.Controls(assist=False)
        state["warp"] = len(WARPS) - 1
        state["patch"] = None
        state["extra_probes"] = []
        self.diving = state["flight_time"]
        self.crash_frames = 0

    def dive(self):
        ship = state["ship"]
        if ship.crashed:
            self.crash_frames += 1
            state["warp"] = 0
        if self.crash_frames < self.FRAMES_PER_SPOT and state["flight_time"] - self.diving < self.TIMEOUT_SECONDS:
            return True
        above = height_above_ground()
        print("[planet] a dive at %.0f m/s and x%d warp: %s, the centre %.2f m above the ground, impact %.0f m/s"
              % (self.DIVE_SPEED, WARPS[-1], "crashed" if ship.crashed else "not crashed", above,
                 ship.impact_speed), flush=True)
        ok = self.landed_ok and ship.crashed and self.DIVE_MIN_CENTRE_HEIGHT < above < SHIP_REACH + 2.0
        os._exit(0 if ok else 1)


class OrbitCheck:
    """--orbit-check: the ship in preset 9's circular orbit, coasting at 16x
    for one orbital period. Prints the periapsis and apoapsis at the start
    and the end and the range of heights flown, and exits with status 0 when
    the heights stayed within 100 m of the orbit's, 1 otherwise."""

    TOLERANCE = 100.0

    def __init__(self):
        self.start = None
        self.low = math.inf
        self.high = -math.inf

    def __call__(self, dt):
        if state["tree"] is None:
            return True
        if self.start is None:
            teleport(9)
            state["warp"] = len(WARPS) - 1
            ship = state["ship"]
            self.start = state["flight_time"]
            self.period = 2.0 * math.pi * math.sqrt((RADIUS + ORBIT_HEIGHT) ** 3 / flight_planet.gm)
            self.orbit0 = flight.orbit(flight_planet, ship.position, ship.velocity)
            return True
        ship = state["ship"]
        height = length(ship.position) - RADIUS
        self.low, self.high = min(self.low, height), max(self.high, height)
        if state["flight_time"] - self.start >= self.period:
            orbit1 = flight.orbit(flight_planet, ship.position, ship.velocity)
            print("[planet] one orbit (%.0f s of flight) at %d m:" % (self.period, ORBIT_HEIGHT), flush=True)
            print("[planet]   periapsis / apoapsis at the start %.1f / %.1f m, at the end %.1f / %.1f m"
                  % (self.orbit0 + orbit1), flush=True)
            print("[planet]   heights flown %.1f to %.1f m" % (self.low, self.high), flush=True)
            ok = max(abs(self.low - ORBIT_HEIGHT), abs(self.high - ORBIT_HEIGHT)) <= self.TOLERANCE
            os._exit(0 if ok else 1)
        return True


engine.set_on_init(on_init)
engine.set_on_key_pressed(on_key)
engine.input.set_on_mouse_scroll(on_scroll)
engine.run()

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
    right mouse + move     look around
    W/A/S/D                fly forward, left, back, right
    E / Q                  up / down
    Shift / Ctrl           4x faster / 4x slower
    mouse wheel            speed factor
    1 - 5                  5 m above the blocks, 1 km, 50 km, 1,000 km, 5,000 km
    6                      above the corner where the +X, +Y and +Z faces meet
    N                      new terrain seed
    M / J                  mountains higher / lower
    H / G                  hills higher / lower
    Z / X                  sun lower / higher, seen from the blocks
    T / Y                  atmosphere denser / thinner
    B / V                  exposure up / down
    F                      wireframe
    C                      colour: terrain, per face, per level
    K                      freeze the level of detail
    [ / ]                  split factor down / up
    P                      screenshot into planet_captures/

    python planet.py --capture DIR     screenshots at the presets, then exit
    python planet.py --verify          check the GPU's chunks against cube_sphere.chunk_mesh, then exit
"""

import argparse
import math
import os
import random
import time

import numpy as np
import shoonyakasha as sk
from shoonyakasha import keys

import cube_sphere
from quadtree import QuadTree

HERE = os.path.dirname(os.path.abspath(__file__))
PIPELINE = os.path.join(HERE, "pipeline", "pipeline.json")
CAPTURES = os.path.join(HERE, "planet_captures")

RADIUS = 100_000.0               # metres
QUADS = 16                       # quads along each side of a chunk
MAX_LEVEL = 12                   # a level-12 chunk is 38 m across, its quads 2.4 m
SPLIT_FACTOR = 1.25              # split when nearer than this many chunk edges
SKIRT_FRACTION = 0.05            # skirt depth, as a fraction of the chunk's edge
HEIGHT_PAD = 0.05                # added to a node's height range, as a fraction of its edge
ROOT_HEIGHTS = (-2000.0, 6000.0)  # the roots' height range until their chunks are measured
# The GPU chunk pool; these match pipeline.json and chunk_build.comp.
SLOTS = 2048                     # chunks the pool holds
MAX_JOBS = 16                    # chunks BuildChunks builds per frame at most
PROBES = 4                       # directions BuildChunks evaluates the terrain at
VERTICES_PER_CHUNK = (QUADS + 1) ** 2 + 4 * (QUADS + 1)
FLOATS_PER_VERTEX = 12           # position and u, normal and v, direction and height
DRAWS_OFFSET = 16                # bytes before the draw commands in ChunkDraws: the count, then padding
# The terrain's parameters (terrain.glsl), the defaults in pipeline.json.
TERRAIN = {
    "continent": [1.5, 0.6, 0.05, 800.0],   # frequency, warp, sea level bias, height scale (m)
    "mountains": [6.0, 3000.0, 2.0, 8.0],   # frequency, height (m), mask frequency, octaves
    "hills": [40.0, 150.0, 1500.0, 6.0],    # frequency, height (m), ocean depth (m), octaves
    "seed": [0.0, 0.0, 0.0, 0.0],           # offset added to the noise input
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
SUN_ILLUMINANCE = 20.0
EXPOSURE = 0.4
NEAR_PLANE = 0.05
FOV = 70.0
LOOK_RADIANS_PER_PIXEL = 0.0025
MIN_ALTITUDE = 0.3               # the camera stays this far above the ground

parser = argparse.ArgumentParser(description="Planet with procedural terrain and quadtree level of detail")
parser.add_argument("--capture", metavar="DIR", help="save screenshots at the presets and exit")
parser.add_argument("--verify", action="store_true",
                    help="check the chunks the GPU built against cube_sphere.chunk_mesh, print the result and exit")
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
    "exposure": EXPOSURE,
}


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


def upload_drawn_chunks():
    """One indexed indirect draw per chunk the tree selected, and each drawn
    slot's centre relative to the camera (subtracted in double) and colour."""
    selected = state["tree"].selected if state["tree"] is not None else []
    camera = state["position"]
    instances = np.zeros((SLOTS, 2, 4), dtype=np.float32)
    commands = np.zeros((len(selected), 5), dtype=np.uint32)
    for row, node in enumerate(selected):
        slot = node.chunk
        instances[slot, 0, :3] = sub(node.centre, camera)
        instances[slot, 1] = chunk_colour(node)
        # indexCount, instanceCount, firstIndex, vertexOffset, firstInstance
        commands[row] = (state["index_count"], 1, 0, slot * VERTICES_PER_CHUNK, slot)
    engine.set_buffer("ChunkInstances", instances)
    engine.set_buffer("ChunkDraws", np.uint32([len(selected)]))
    if len(selected):
        engine.set_buffer("ChunkDraws", commands, offset=DRAWS_OFFSET)


def read_back_heights():
    """Chunk heights into the tree's nodes, and the probes' heights made
    with the current terrain version: the ground under the camera, and the
    site's natural height."""
    readback = engine.get_readback("ChunkHeights")
    if readback is not None:
        pool.take_heights(np.frombuffer(readback["data"], dtype=np.float32).reshape(SLOTS, 4))
    readback = engine.get_readback("ProbeHeights")
    if readback is not None:
        probes = np.frombuffer(readback["data"], dtype=np.float32).reshape(PROBES, 4)
        version = float(state["terrain_version"])
        if probes[0, 1] == version:
            state["ground"] = float(probes[0, 0])
        if probes[1, 1] == version and state["site_height"] is None:
            state["site_height"] = float(probes[1, 0])


def start_terrain():
    """Once the site's natural height is known: the pad at that height, and
    a tree to build the planet with it."""
    upload_terrain()
    state["terrain_version"] += 1
    state["ground"] = None
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
    state["terrain_version"] += 1
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
        blocks.append((entity, add(scale(east, e), add(scale(up, u), scale(north, n)))))


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


def height_above_ground():
    return altitude() - (state["ground"] or 0.0)


def keep_above_ground():
    floor = RADIUS + (state["ground"] or 0.0) + MIN_ALTITUDE
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
    _, up, east, north = site_frame()
    ground = site_ground()
    state["teleport_when_known"] = preset if preset in (1, 2) and state["site_height"] is None else None
    if preset == 1:
        # 5 m up, 25 m south-west of the blocks, looking at them.
        state["position"] = add(ground, add(scale(up, 5.0), add(scale(east, -18.0), scale(north, -18.0))))
        state["forward"] = normalize(add(add(scale(east, 1.0), scale(north, 1.0)), scale(up, -0.2)))
    elif preset == 2:
        state["position"] = add(ground, scale(up, 1_000.0))
        state["forward"] = normalize(add(north, scale(up, -0.25)))
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
    state["ground"] = None


def in_view(centre, radius):
    """Whether a sphere around `centre` (planet space) is roughly in view: a
    cone around the camera's forward direction that holds the screen."""
    to_centre = sub(centre, state["position"])
    distance = length(to_centre)
    if distance <= radius:
        return True
    aspect = 16.0 / 9.0
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
    _, site_up, _, _ = site_frame()
    version = float(state["terrain_version"])
    probes = [normalize(state["position"]), site_up] + [(0.0, 1.0, 0.0)] * (PROBES - 2)
    pool.submit_jobs([tuple(d) + (version,) for d in probes])


def frame(dt):
    read_back_heights()
    if state["tree"] is None and state["site_height"] is not None:
        start_terrain()
        if state["teleport_when_known"]:
            teleport(state["teleport_when_known"])

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
    ground = site_ground()
    for entity, offset in blocks:
        engine.scene.set_position(entity, sub(add(ground, offset), camera))
    right, up, forward = camera_basis()
    engine.scene.set_rotation(state["camera"], euler_from_basis(right, up, forward))

    state["frame_ms"] = 0.9 * state["frame_ms"] + 0.1 * dt * 1000.0
    if state["hud"] is not None:
        update_hud()
    return True


def update_hud():
    tree = state["tree"]
    if tree is None:
        return
    terrain = state["terrain"]
    cached = sum(1 for node in tree.nodes() if node.chunk is not None)
    deepest = max(node.level for node in tree.selected)
    engine.ui.set_text(state["hud"][0], "above ground %s    above sea level %s    speed %.0f m/s    "
                       "speed factor %.2f    frame %.1f ms"
                       % (distance_text(height_above_ground()), distance_text(altitude()), state["speed"],
                          state["speed_factor"], state["frame_ms"]))
    engine.ui.set_text(state["hud"][1], "chunks drawn %d    built %d    missing %d    pool %d / %d    "
                       "deepest level %d    split factor %.2f%s    colour: %s"
                       % (len(tree.selected), cached, len(tree.missing), pool.used, SLOTS, deepest,
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
    build_site()
    build_hud()
    teleport(1)

    # Before the transform system (priority 0), so this frame's positions are used.
    engine.ecs.add_system("PlanetFrame", frame, priority=-1)
    if args.capture:
        engine.ecs.add_system("PlanetCapture", CaptureTour(args.capture), priority=-2)
    elif args.verify:
        engine.ecs.add_system("PlanetVerify", Verify(), priority=-2)
    print(__doc__.split("Keys:")[1].rstrip(), flush=True)


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
    if keys.NUM_1 <= key <= keys.NUM_6:
        teleport(key - keys.NUM_0)
    elif key == keys.N:
        state["terrain"]["seed"] = [random.uniform(-100.0, 100.0) for _ in range(3)] + [0.0]
        regenerate()
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


class CaptureTour:
    """--capture: each shot in turn, a screenshot once settled (or after
    MAX_FRAMES), then exit. Waits for the site's height first."""

    MIN_FRAMES, MAX_FRAMES = 10, 600
    # (file name, preset, wireframe, colour mode, terrain change or None, sun elevation and azimuth)
    SHOTS = [("preset_%d" % p, p, False, 0, None, DAY_SUN) for p in range(1, 7)] + [
        ("preset_1_sunset", 1, False, 0, None, (3.0, 45.0)),
        ("preset_2_sunset", 2, False, 0, None, (2.0, 90.0)),
        ("preset_3_low_sun", 3, False, 0, None, (8.0, 45.0)),
        ("preset_4_terminator", 4, False, 0, None, (0.0, 200.0)),
        ("preset_1_wireframe_levels", 1, True, 2, None, DAY_SUN),
        ("preset_2_wireframe", 2, True, 0, None, DAY_SUN),
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
        name, preset, wireframe, colour_mode, change, sun = self.SHOTS[self.shot]
        self.frames += 1
        if self.frames == 1:
            if change is not None:
                change_terrain(*change)
            state["sun_elevation"], state["sun_azimuth"] = sun
            upload_sky()
            teleport(preset)
            set_wireframe(wireframe)
            set_colour_mode(colour_mode)
        elif self.captured is None:
            done = self.frames >= self.MIN_FRAMES and settled()
            if done or self.frames >= self.MAX_FRAMES:
                path = os.path.join(self.directory, name + ".png")
                ok = engine.capture_screenshot(path)
                print("[planet] %s after %d frames%s" % (path if ok else "screenshot failed", self.frames,
                                                         "" if done else " (not settled)"), flush=True)
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
        worked out from its directions and the GPU's heights, to within one
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
            centre, positions, directions, uvs, _ = cube_sphere.chunk_mesh(
                node.face, node.level, node.x, node.y, QUADS, RADIUS, skirt_depth=SKIRT_FRACTION * node.edge)
            built = gpu[node.chunk]
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


engine.set_on_init(on_init)
engine.set_on_key_pressed(on_key)
engine.input.set_on_mouse_scroll(on_scroll)
engine.run()

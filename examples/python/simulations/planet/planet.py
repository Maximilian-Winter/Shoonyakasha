"""
Shoonyakasha planet: a cube-sphere

A sphere of radius 100 km, flown around from a few metres above its surface
to thousands of kilometres away. Positions are Python floats (64-bit) with
the planet's centre at the origin. The engine's camera stays at its own
origin and only turns; every frame each entity is placed at its position
minus the camera's, so the GPU sees only small numbers. The projection is
reverse-Z with an infinite far plane.

The sphere is a cube-sphere: each of the six cube faces split into 8 x 8
chunks of 16 x 16 quads, mapped onto the sphere with `spherify`. Each chunk
holds its vertices relative to its own centre. Lines on the ground follow
the face coordinates, at every power-of-two subdivision of a face. A cluster
of blocks stands on a vertex near the centre of the +Z face, on a pad 5 cm
above the ground.

Keys:
    right mouse + move     look around
    W/A/S/D                fly forward, left, back, right
    E / Q                  up / down
    Shift / Ctrl           4x faster / 4x slower
    mouse wheel            speed factor
    1 - 5                  5 m above the blocks, 1 km, 50 km, 1,000 km, 5,000 km
    6                      above the corner where the +X, +Y and +Z faces meet
    F                      wireframe
    C                      a colour per face
    P                      screenshot into planet_captures/

    python planet.py --capture DIR     save a screenshot at each of 1 - 6 and exit
"""

import argparse
import math
import os
import time

import shoonyakasha as sk
from shoonyakasha import keys

import cube_sphere

HERE = os.path.dirname(os.path.abspath(__file__))
PIPELINE = os.path.join(HERE, "pipeline", "pipeline.json")
CAPTURES = os.path.join(HERE, "planet_captures")

RADIUS = 100_000.0               # metres
LEVEL = 3                        # every face is 2**LEVEL x 2**LEVEL chunks
QUADS = 16                       # quads along each side of a chunk
SITE_FACE = 4                    # +Z
SITE_UV = 2.0 / (QUADS << LEVEL)  # one quad from the face's centre along u and v: a vertex
GROUND_COLOUR = (0.30, 0.40, 0.27)
FACE_COLOURS = ((0.62, 0.30, 0.25), (0.25, 0.50, 0.55), (0.55, 0.55, 0.30),
                (0.40, 0.30, 0.60), (0.30, 0.55, 0.30), (0.60, 0.42, 0.25))
SUN = (0.48, 0.42, 0.77)
NEAR_PLANE = 0.05
FOV = 70.0
LOOK_RADIANS_PER_PIXEL = 0.0025
MIN_ALTITUDE = 0.3               # the camera stays this far above the sphere

parser = argparse.ArgumentParser(description="Planet precision sandbox")
parser.add_argument("--capture", metavar="DIR", help="save a screenshot at each preset and exit")
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

# Each placed entity: (entity, position in planet space). Rebased every frame.
placed = []
# (entity, face) of every chunk.
chunks = []
state = {
    "camera": None,
    "position": (0.0, 0.0, RADIUS + 5.0),
    "forward": (0.0, 0.0, 1.0),
    "speed_factor": 1.0,
    "hud": None,
    "wireframe": False,
    "face_colours": False,
}


def site_frame():
    up = cube_sphere.direction(SITE_FACE, SITE_UV, SITE_UV)
    east, north = local_frame(up)
    return scale(up, RADIUS), up, east, north


def build_planet():
    for face in range(6):
        for y in range(1 << LEVEL):
            for x in range(1 << LEVEL):
                centre, positions, normals, uvs, indices = cube_sphere.chunk_mesh(face, LEVEL, x, y, QUADS, RADIUS)
                entity = engine.create_mesh(positions, indices, normals=normals, uvs=uvs, name="Chunk")
                engine.scene.set_material_float(entity, "gridStrength", 1.0)
                placed.append((entity, centre))
                chunks.append((entity, face))
    colour_chunks()


def colour_chunks():
    for entity, face in chunks:
        colour = FACE_COLOURS[face] if state["face_colours"] else GROUND_COLOUR
        engine.scene.set_material_vec4(entity, "baseColorFactor", colour + (1.0,))


def build_site():
    """Blocks around the site, in metres east, up and north of it."""
    origin, up, east, north = site_frame()
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
        placed.append((entity, add(origin, add(scale(east, e), add(scale(up, u), scale(north, n))))))


def build_hud():
    ui = engine.ui
    canvas = ui.create_canvas(reference_size=(1600, 900))
    text = ui.create_text(canvas, "", size=22, color=(0.92, 0.92, 0.88, 1.0))
    ui.set_rect(text, anchor_min=(0, 0), anchor_max=(1, 0), pivot=(0, 0), position=(20, 16), size=(-40, 30))
    state["hud"] = text


# ── Camera ─────────────────────────────────────────────────────────────────

def altitude():
    return length(state["position"]) - RADIUS


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
    speed = max(altitude(), 2.0) * 0.5 * state["speed_factor"]
    if engine.input.is_key_down(keys.LEFT_SHIFT):
        speed *= 4.0
    if engine.input.is_key_down(keys.LEFT_CONTROL):
        speed *= 0.25
    position = add(state["position"], scale(normalize(move), speed * dt))
    if length(position) < RADIUS + MIN_ALTITUDE:
        position = scale(normalize(position), RADIUS + MIN_ALTITUDE)
    state["position"] = position
    state["speed"] = speed


def teleport(preset):
    origin, up, east, north = site_frame()
    if preset == 1:
        # 5 m up, 25 m south-west of the blocks, looking at them.
        state["position"] = add(origin, add(scale(up, 5.0), add(scale(east, -18.0), scale(north, -18.0))))
        state["forward"] = normalize(add(add(scale(east, 1.0), scale(north, 1.0)), scale(up, -0.2)))
    elif preset == 2:
        state["position"] = add(origin, scale(up, 1_000.0))
        state["forward"] = normalize(add(north, scale(up, -0.25)))
    elif preset == 6:
        # 3 km above the cube corner shared by +X, +Y and +Z, looking down at it.
        corner = normalize((1.0, 1.0, 1.0))
        corner_east, corner_north = local_frame(corner)
        state["position"] = scale(corner, RADIUS + 3_000.0)
        state["forward"] = normalize(add(scale(corner, -0.98), scale(corner_north, 0.2)))
    else:
        height = {3: 50_000.0, 4: 1_000_000.0, 5: 5_000_000.0}[preset]
        state["position"] = scale(up, RADIUS + height)
        state["forward"] = normalize(add(scale(up, -0.98), scale(north, 0.2)))


# ── Per frame ──────────────────────────────────────────────────────────────

def frame(dt):
    if engine.input.is_mouse_button_down(keys.MOUSE_RIGHT):
        dx, dy = engine.input.get_mouse_delta()
        look(dx, dy)
    state["speed"] = 0.0
    fly(dt)

    # Camera-relative placement: subtract in double, then hand floats over.
    camera = state["position"]
    for entity, position in placed:
        engine.scene.set_position(entity, sub(position, camera))
    right, up, forward = camera_basis()
    engine.scene.set_rotation(state["camera"], euler_from_basis(right, up, forward))

    if state["hud"] is not None:
        h = altitude()
        shown = "%.1f m" % h if h < 10_000.0 else "%.1f km" % (h / 1000.0)
        engine.ui.set_text(state["hud"], "altitude %s    speed %.0f m/s    speed factor %.2f"
                           % (shown, state["speed"], state["speed_factor"]))
    return True


def on_init():
    camera = engine.scene.create_entity("Camera")
    engine.scene.add_component(camera, "Camera")
    engine.scene.set_camera_main(camera, True)
    engine.scene.set_camera_fov(camera, FOV)
    engine.scene.set_camera_near(camera, NEAR_PLANE)
    engine.scene.set_camera_reverse_z(camera, True)
    state["camera"] = camera

    engine.set_custom_vec4("planet.sunDirection", normalize(SUN) + (0.0,))
    build_planet()
    build_site()
    build_hud()
    teleport(1)

    # Before the transform system (priority 0), so this frame's positions are used.
    engine.ecs.add_system("PlanetFrame", frame, priority=-1)
    if args.capture:
        engine.ecs.add_system("PlanetCapture", CaptureTour(args.capture), priority=-2)
    print(__doc__.split("Keys:")[1].rstrip(), flush=True)


def set_wireframe(on):
    state["wireframe"] = on
    engine.set_pass_enabled("Wireframe", on)


def on_key(key):
    if keys.NUM_1 <= key <= keys.NUM_6:
        teleport(key - keys.NUM_0)
    elif key == keys.F:
        set_wireframe(not state["wireframe"])
    elif key == keys.C:
        state["face_colours"] = not state["face_colours"]
        colour_chunks()
    elif key == keys.P:
        os.makedirs(CAPTURES, exist_ok=True)
        path = os.path.join(CAPTURES, time.strftime("planet_%Y%m%d_%H%M%S.png"))
        if engine.capture_screenshot(path):
            print("saved", path, flush=True)


def on_scroll(x_offset, y_offset):
    state["speed_factor"] = max(0.01, min(100.0, state["speed_factor"] * 1.25 ** y_offset))


class CaptureTour:
    """--capture: each shot in turn, a screenshot once it has settled for a
    few frames, then exit."""

    SETTLE_FRAMES = 30
    # (file name, preset, wireframe, a colour per face)
    SHOTS = [("preset_%d" % p, p, False, False) for p in range(1, 7)] + [
        ("preset_1_wireframe", 1, True, False),
        ("preset_6_wireframe_faces", 6, True, True),
        ("preset_4_faces", 4, False, True),
    ]

    def __init__(self, directory):
        os.makedirs(directory, exist_ok=True)
        self.directory = directory
        self.shot = 0
        self.frames = 0

    def __call__(self, dt):
        name, preset, wireframe, face_colours = self.SHOTS[self.shot]
        self.frames += 1
        if self.frames == 1:
            teleport(preset)
            set_wireframe(wireframe)
            if state["face_colours"] != face_colours:
                state["face_colours"] = face_colours
                colour_chunks()
        elif self.frames == self.SETTLE_FRAMES:
            path = os.path.join(self.directory, name + ".png")
            print("[planet]", path if engine.capture_screenshot(path) else "screenshot failed", flush=True)
        elif self.frames > self.SETTLE_FRAMES + 2:
            self.shot += 1
            self.frames = 0
            if self.shot == len(self.SHOTS):
                os._exit(0)        # the engine has no quit call
        return True


engine.set_on_init(on_init)
engine.set_on_key_pressed(on_key)
engine.input.set_on_mouse_scroll(on_scroll)
engine.run()

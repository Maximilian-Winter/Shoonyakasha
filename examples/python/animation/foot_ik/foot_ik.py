"""
Shoonyakasha: two-bone IK

The Mixamo dancer dances across a step, and Y Bot on a slope reaches for a
glowing ball circling in front of it. Each frame, Humanoid.place_feet puts the
feet at their animated height above the ground under them, tilted to the
ground, and lowers the hips so the lower foot reaches; Humanoid.reach puts
Y Bot's right hand on the ball.

Models: assets/models/rumba-dancing-mixamo/gltf/Rumba-Dancing.gltf and
assets/models/rumba-dancing-mixamo/gltf-Y-Bot/Rumba-Dancing-Y-Bot.gltf.

Keys:
    I              foot placement on or off
    H              hand reach on or off
    SPACE          pause or resume
    P              screenshot into foot_ik_captures/
    W/A/S/D/Q/E    move the camera; right mouse looks around

Stills with IK on and off, then quit:
    python foot_ik.py --screenshots shots/
"""

import argparse
import math
import os
import time

import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.character import load_humanoid

DANCER = "models/rumba-dancing-mixamo/gltf/Rumba-Dancing.gltf"
YBOT = "models/rumba-dancing-mixamo/gltf-Y-Bot/Rumba-Dancing-Y-Bot.gltf"

STEP_X = -1.0            # the step edge, under the dancer
STEP = 0.18              # its height
SLOPE = math.radians(15.0)
SLOPE_START = 0.4        # the slope rises toward +X from here


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    parser.add_argument("--screenshots", metavar="DIR", help="save a still of each step into DIR and quit")
    parser.add_argument("--width", type=int, default=1600)
    parser.add_argument("--height", type=int, default=900)
    return parser.parse_args()


args = parse_args()


def save_screenshot():
    """P: the frame on screen into foot_ik_captures/ in the working directory."""
    directory = os.path.join(os.getcwd(), "foot_ik_captures")
    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, time.strftime("foot_ik_%Y%m%d_%H%M%S.png"))
    print("[foot_ik] screenshot ->", path if engine.capture_screenshot(path) else "failed")


class Stills:
    """--screenshots: for each (name, action) step, run the action, wait
    SETTLE seconds for fades and IK to settle, and save <name>.png; quit after
    the last."""

    SETTLE = 1.5

    def __init__(self, directory, steps):
        self.directory = directory
        self.steps = steps
        self.index = 0
        self.wait = None
        os.makedirs(directory, exist_ok=True)

    def update(self, dt):
        if self.index >= len(self.steps):
            return
        name, action = self.steps[self.index]
        if self.wait is None:
            action()
            self.wait = self.SETTLE
            return
        self.wait -= dt
        if self.wait > 0.0:
            return
        path = os.path.join(self.directory, name + ".png")
        print("[foot_ik] still ->", path if engine.capture_screenshot(path) else "failed")
        self.index += 1
        self.wait = None
        if self.index == len(self.steps):
            print("[foot_ik] %d stills in %s" % (len(self.steps), self.directory))
            engine.request_quit()


stills = None


def ground(x, z):
    """Ground height and normal at (x, z)."""
    if x < SLOPE_START:
        return (STEP if x > STEP_X else 0.0), (0.0, 1.0, 0.0)
    rise = math.tan(SLOPE)
    return STEP + (x - SLOPE_START) * rise, (-math.sin(SLOPE), math.cos(SLOPE), 0.0)


def ground_mesh(x0=-3.0, x1=3.0, z0=-1.5, z1=1.5, cell=0.02):
    """Triangles of the ground surface on a grid of `cell` metres."""
    nx, nz = int((x1 - x0) / cell) + 1, int((z1 - z0) / cell) + 1
    positions = []
    for i in range(nx):
        x = x0 + i * cell
        for j in range(nz):
            z = z0 + j * cell
            positions.append((x, ground(x, z)[0], z))
    indices = []
    for i in range(nx - 1):
        for j in range(nz - 1):
            a, b = i * nz + j, (i + 1) * nz + j
            indices += [a, a + 1, b, b, a + 1, b + 1]
    return positions, indices


def cube(half):
    positions, indices = [], []
    for axis in range(3):
        for sign in (-1.0, 1.0):
            u, v = (axis + 1) % 3, (axis + 2) % 3
            base = len(positions)
            for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                p = [0.0, 0.0, 0.0]
                p[axis], p[u], p[v] = sign * half, a * half, b * half * sign
                positions.append(tuple(p))
            indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    return positions, indices


engine = sk.Engine(
    title="Shoonyakasha - Two-bone IK",
    width=args.width, height=args.height,
    hdr_environment_path="env/charolettenbrunn_park_1k.hdr",
)

state = {"dancer": None, "ybot": None, "ball": None, "feet": True, "hand": True, "paused": False, "t": 0.0}


def ball_position(t):
    x, y, z = engine.scene.get_position(state["ybot"].root)
    return (x - 0.3 + 0.1 * math.cos(1.5 * t), y + 1.3 + 0.1 * math.sin(1.5 * t), z + 0.3)


def on_init():
    engine.create_camera(pos=(-0.2, 1.2, 3.6), fov=50.0, speed=3.0, near_plane=0.05, far_plane=200.0)
    engine.create_directional_light(direction=(-0.4, -1.0, -0.5), color=(1.0, 0.96, 0.9), intensity=3.0)

    positions, indices = ground_mesh()
    floor = engine.create_mesh(positions, indices, name="ground")
    engine.scene.set_material_vec4(floor, "baseColorFactor", (0.45, 0.42, 0.38, 1.0))

    # The dancer's root on the upper step; Y Bot's on the slope.
    state["dancer"] = load_humanoid(engine, DANCER, position=(STEP_X + 0.04, STEP, 0.0))
    ybot_x = 1.2
    state["ybot"] = load_humanoid(engine, YBOT, position=(ybot_x, ground(ybot_x, 0.0)[0], 0.0))
    for hero in (state["dancer"], state["ybot"]):
        hero.play(hero.clips[0])

    positions, indices = cube(0.05)
    state["ball"] = engine.create_mesh(positions, indices, name="ball")
    engine.scene.set_material_vec4(state["ball"], "emissiveFactor", (0.3, 2.5, 4.0, 0.0))
    print(__doc__[__doc__.index("Keys:"):__doc__.index("Stills")].rstrip())

    global stills
    if args.screenshots:
        def ik_off():
            on_key_pressed(ord("I"))
            on_key_pressed(ord("H"))
        stills = Stills(args.screenshots, [("ik_on", lambda: None), ("ik_off", ik_off)])


def on_update(dt):
    if state["dancer"] is None:
        return
    if stills:
        stills.update(dt)
    state["t"] += 0.0 if state["paused"] else dt
    if state["feet"]:
        for hero in (state["dancer"], state["ybot"]):
            hero.place_feet(ground)
    target = ball_position(state["t"])
    engine.scene.set_position(state["ball"], target)
    if state["hand"]:
        state["ybot"].reach("RightHand", target)


def on_key_pressed(key):
    if state["dancer"] is None:
        return
    if key == ord("I"):
        state["feet"] = not state["feet"]
        if not state["feet"]:
            for hero in (state["dancer"], state["ybot"]):
                hero.release_feet()
        print("Foot placement:", "on" if state["feet"] else "off")
    elif key == ord("H"):
        state["hand"] = not state["hand"]
        if not state["hand"]:
            state["ybot"].release("RightHand")
        print("Hand reach:", "on" if state["hand"] else "off")
    elif key == keys.SPACE:
        state["paused"] = not state["paused"]
        for hero in (state["dancer"], state["ybot"]):
            engine.animation.set_speed(hero.root, 0.0 if state["paused"] else 1.0)
    elif key == keys.P:
        save_screenshot()


engine.set_on_init(on_init)
engine.set_on_update(on_update)
engine.set_on_key_pressed(on_key_pressed)
engine.run()

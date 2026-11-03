"""
Walker: the planet on foot, as a game. The player starts beside the parked
ship at Base, on the planet of the planet example (planet.py), and walks,
runs, jumps and jets with the gravity gun in hand
(shoonyakasha.planet.on_foot). The ship stands on its gear 16 m south of
the blocks as a landmark, a static hull in the physics. The outposts on
the planet and the moon are found by the same survey as in planet.py and
drawn as pads.

The player is put down SPAWN_LIFT above the ground EAST_OF_SHIP east of
the ship, facing north, once the terrain is built and the ground there is
measured. Until then the camera waits 2 m above that spot.

Keys:
  W A S D                walk; left Shift run; left Ctrl crouch
  Space                  jump on the ground, jet in the air
  mouse                  look (captured; Escape frees it, a click captures it again)
  right mouse            gravity gun: grab, or pull from farther; again: drop
  left mouse             punt what is held or in front; hold while holding to charge
  R + mouse              turn the held body
  mouse wheel            the hold distance
  F1                     the controls

Gamepad: left stick walk, right stick look, A jump and jet, left trigger
grab, right trigger punt, the bumpers the hold distance.

    python walker.py
    python walker.py --check    put the player down, walk 10 s north, report and exit
"""

import argparse
import os

import numpy as np

import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.planet import world as planet_world
from shoonyakasha.planet.bubble import Bubble
from shoonyakasha.planet.on_foot import OnFoot
from shoonyakasha.planet.vec import (add, cross, euler_from_basis, length, normalize, quat_from_basis, scale,
                                     sub)
from shoonyakasha.planet.world import EXTRA_PROBES, FOV, MAX_LEVEL
from shoonyakasha.toolkit.overlay import HelpOverlay

import flight
from outposts import Pads, Survey, home_outpost
from rocks import Rocks
from viewmodel import Viewmodel

HERE = os.path.dirname(os.path.abspath(__file__))
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

HELP_COLUMNS = [
    [("On foot", [
        (("Mouse",), "Look around (Escape frees the mouse, a click takes it back)"),
        (("W", "A", "S", "D"), "Walk"),
        (("Shift", "Ctrl"), "Run / crouch"),
        (("Space",), "Jump; held in the air: jetpack"),
        (("F1",), "These controls"),
    ])],
    [("Gravity gun", [
        (("RMB",), "Grab, or pull from farther; again: drop"),
        (("LMB",), "Punt; hold while holding to charge"),
        (("R",), "With the mouse: turn the held body"),
        (("Wheel",), "Hold distance"),
    ])],
]

parser = argparse.ArgumentParser(description="The planet on foot")
parser.add_argument("--check", action="store_true",
                    help="put the player down, walk 10 s north, report how far it got and exit")
parser.add_argument("--shot", metavar="FILE", help="with --check: a screenshot after SHOT_SECONDS of walking")
args = parser.parse_args()

planet_world.compile_shaders()
engine = sk.Engine(title="Shoonyakasha - walker", width=1600, height=900,
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
    "help": None,
    "captured": False,            # whether the mouse was last asked to be captured
    "bumpers": set(),             # the gamepad bumpers held last frame
    "check": None,                # --check: (feet when it started walking, seconds walked)
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
    the ground under the camera are known."""
    if foot.player is not None or planet.tree is None or world.site_height is None:
        return
    if state["ship"] is None:
        state["ship"] = ship_pose()
        state["position"] = add(spawn_point(), scale(state["ship"][2], 2.0))
        state["forward"] = state["ship"][3]
        world.ground = None
        return
    if world.ground is None or world.height_above_ground(state["position"]) > Bubble.RADIUS:
        return
    bubble = foot.ensure_bubble(state["position"])
    up = normalize(bubble.body.relative(state["position"]))
    feet = add(bubble.body.centre(),
               scale(up, bubble.body.radius + world.surface_height(state["position"]) + SPAWN_LIFT))
    foot.spawn(feet, state["forward"])
    place_eye()
    print("[walker] on foot at Base", flush=True)


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
    """The mouse captured on foot, freed while the controls show; after
    Escape freed it, captured again by a click."""
    wanted = foot.player is not None and not state["help"].visible
    if wanted != state["captured"]:
        engine.input.set_mouse_captured(wanted)
        state["captured"] = wanted
    elif wanted and not engine.input.is_mouse_captured() and engine.input.is_mouse_button_down(keys.MOUSE_LEFT):
        engine.input.set_mouse_captured(True)


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
    viewmodel.update(dt, view_basis(), foot.look_delta, player, gun, held)


def run_check(dt):
    """--check: walk north for CHECK_SECONDS once on foot, then report and exit."""
    if foot.player is None:
        return
    if state["check"] is None:
        state["check"] = (foot.bubble.to_world(foot.player.feet()), 0.0)
    start, walked = state["check"]
    walked += dt
    state["check"] = (start, walked)
    foot.controls.forward = 1.0
    if args.shot and walked >= SHOT_SECONDS and not state["shot"]:
        state["shot"] = True
        print("[walker] screenshot: %s" % (args.shot if engine.capture_screenshot(args.shot) else "failed"),
              flush=True)
    if walked >= CHECK_SECONDS:
        moved = length(sub(foot.bubble.to_world(foot.player.feet()), start))
        ok = moved > 0.8 * CHECK_SECONDS * 4.0 and foot.player.on_ground
        print("[walker] walked %.1f m in %.0f s, on the ground: %s: %s"
              % (moved, walked, foot.player.on_ground, "passed" if ok else "FAILED"), flush=True)
        os._exit(0 if ok else 1)


# ── The frame ──────────────────────────────────────────────────────────────

def frame(dt):
    clock = world.time
    world.begin_frame(state["position"])
    update_outposts()
    maybe_spawn()
    if foot.player is not None:
        if args.check:
            run_check(dt)
        else:
            foot.read_controls(engine.input)
            read_bumpers()
    world.time += dt
    latency = world.probe_latency * state["frame_ms"] / 1000.0
    foot.props.fly(world.time - clock, latency)
    foot.step(dt, state["position"], state["position"], latency, active=foot.player is not None)
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
    foot.end_frame()
    state["frame_ms"] = 0.9 * state["frame_ms"] + 0.1 * dt * 1000.0
    return True


def on_init():
    world.setup()
    build_site()
    state["home"] = home_outpost(world)
    state["outposts"] = [state["home"]]
    begin_survey()
    build_ship_model()
    state["viewmodel"] = Viewmodel(engine, box())
    state["help"] = HelpOverlay(engine, "CONTROLS", HELP_COLUMNS, footer="F1 to close")
    state["help"].show(False)
    _, up, _, north = world.site_frame()
    state["position"] = add(world.site_ground(), scale(up, 5.0))
    state["forward"] = north
    engine.ecs.add_system("WalkerFrame", frame, priority=-1)


def on_key(key):
    if key == keys.F1:
        state["help"].show(not state["help"].visible)
    elif key == keys.ESCAPE and state["help"].visible:
        state["help"].show(False)


def on_scroll(x_offset, y_offset):
    if foot.player is not None:
        foot.gun_controls.distance += y_offset


engine.set_on_init(on_init)
engine.set_on_key_pressed(on_key)
engine.input.set_on_mouse_scroll(on_scroll)
engine.run()

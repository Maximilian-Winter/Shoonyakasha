"""
The hover car (hovercar.py, carview.py): making it, its ground patch,
getting in and out, driving it, its body in the physics bubble and its
bumps, the chase camera, and the cargo net behind it and loading it.
"""

import numpy as np
from shoonyakasha import keys
from shoonyakasha.planet.bubble import Bubble
from shoonyakasha.planet.ground_patch import GroundPatch, SIZE as PATCH_SIZE
from shoonyakasha.planet.on_foot import JUMP_DISTANCE, STEP
from shoonyakasha.planet.vec import (add, dot, euler_from_basis, length, normalize, quat_from_basis, quat_normalize,
                                     scale, sub)

import carview
import hovercar
import hud as hud_module
from play.core import (GOOD_TOAST, SHIP_CLEARANCE, SHIP_HALF, SPAWN_LIFT, args, box, engine, foot, planet, settings,
                       state, world)
from play import contracting, economy, saving, spaceflight, storms, walking

ENTER_REACH = 4.5                # m from the car's centre to the feet it can be got into from
EXIT_OUT = 1.0                   # m beyond the car's left side the player gets out
CAR_PATCH_MARGIN = 8.0           # m the car's ground patch reaches beyond its centre and its look-ahead
BUMP_REACH = 0.5                 # m beyond a step's travel the car looks for trunks and rocks
BUMP_BOUNCE, BUMP_KEEP = 0.3, 0.6
SHIP_HULL = [(x * SHIP_HALF[0], y * SHIP_HALF[1], z * SHIP_HALF[2]) for x in (-1, 1) for y in (-1, 1) for z in (-1, 1)]
# The cargo net behind the parked car: its centre from the car's (right, up, back; up at the ground),
# its half sizes along right and back, how far below and above the ground an ore rock's centre may be in
# it, and its corner posts.
CAR_NET_CENTRE = (0.0, -SHIP_CLEARANCE, 6.5)
CAR_NET_HALF = (2.0, 2.5)
CAR_NET_BELOW, CAR_NET_ABOVE = 1.0, 3.0
NET_POST = (0.12, 1.0, 0.12)    # m
NET_COLOUR = (1.0, 0.72, 0.3)


def remove_ship_body():
    body = state["ship_body"]
    if body is not None and foot.physics.contains(body):
        foot.physics.remove(body)
    state["ship_body"] = None


foot.on_drop = remove_ship_body


def car_position():
    """The car's centre in planet space."""
    return add(spaceflight.car_body().centre(), state["car"].position)


def make_car(position, heading, target=hovercar.HOVER_MIN):
    """The car at `position` (planet space), its nose along `heading`, with the upgrades' parameters."""
    state["car"] = hovercar.Car(planet.relative(position), heading, target, params=state["params"]["car"])
    state["car_body"], state["space"] = None, None
    update_ship()


def update_ship():
    """`ship` (position, right, up, forward) from the car's pose, level."""
    car = state["car"]
    up = car.up()
    state["ship"] = (car_position(), car.right(), up, car.heading)


def car_ground(point):
    """The height of the car's underside above the ground with its centre
    at `point` (relative to the centre of the body it is over), from the
    ground patch; None where it is not known."""
    patch = world.patch
    if patch is None or patch[0] is not spaceflight.car_body():
        return None
    answer = patch[1].query(point)
    if answer is None:
        return None
    return -answer[0] - SHIP_HALF[1]


def camera_ground(point):
    """The height above the ground of `point` (planet space), from the ground patch, or None."""
    h = car_ground(spaceflight.car_body().relative(point))
    return None if h is None else h + SHIP_HALF[1]


def car_patch_request():
    """(body, patch): the ground patch to measure around where the car, on the body it is over,
    will be when its heights come back, ahead along its velocity by the
    probes' latency, reaching CAR_PATCH_MARGIN and its look-ahead beyond."""
    car = state["car"]
    up = car.up()
    v = car.velocity
    along = sub(v, scale(up, dot(v, up)))
    ahead = world.probe_latency * state["frame_ms"] / 1000.0
    half = CAR_PATCH_MARGIN + length(along) * (ahead + hovercar.LOOK_AHEAD)
    centre = add(car.position, scale(along, ahead))
    body = spaceflight.car_body()
    return body, GroundPatch(body.radius, centre, 2.0 * half / (PATCH_SIZE - 1), sea=body.ocean,
                             water=world.water_level if body is planet else None)


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
    ore = [p for p in foot.props.rocks if p.grade > 0.0] + [p for p in foot.props.crates if contracting.crate_contract(p)]
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
        c = contracting.crate_contract(prop)
        volume = prop.size ** 3 if c is not None else None
        if not car.can_take(prop.mass, prop.resource, volume):
            left += 1
            continue
        if c is not None:
            car.cargo.append(contracting.crate_item(c, prop.mass, volume))
            contracting.forget_crate(prop)
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
    if state["space"] is not None:
        state["hud"].toast("Land first", hud_module.WARNING)
        return False
    if not car.can_leave():
        state["hud"].toast("Slow down and come down first", hud_module.WARNING)
        return False
    right, up, forward = car.right(), car.up(), car.heading
    side = add(car.position, scale(right, -(SHIP_HALF[0] + EXIT_OUT)))
    h = car_ground(side)
    if h is None:
        return False
    feet = add(spaceflight.car_body().centre(), add(side, scale(up, -(h + SHIP_HALF[1]) + SPAWN_LIFT)))
    bubble = foot.ensure_bubble(feet)
    foot.spawn(feet, forward)
    walking.place_eye()
    state["driving"] = False
    set_fov(settings["fov"])
    car.target = hovercar.HOVER_MIN
    state["car_controls"] = hovercar.CarControls()
    print("[walker] out of the car", flush=True)
    outpost = economy.car_outpost()
    if outpost is not None and settings["autosave"] and not args.check:
        if saving.save_game(quiet=True) is not None:
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
    wind at its height above the ground (PlanetWorld.wind_at) and buffeted
    in a strong one (buffet_car), or in space mode by step_space; stopped by
    trunks and fixed rocks ahead (bump_car), a bump striking the fragile
    crates in its hold (strike_hold); its poses before and after kept for
    its body in the bubble."""
    car = state["car"]
    if car is None:
        return
    before = (car_position(), car_rotation())
    controls = state["car_controls"] if state["driving"] else hovercar.CarControls()
    if state["space"] is not None:
        spaceflight.step_space(dt)
    else:
        car.step(dt, controls, car_ground, wind=lambda point: world.wind_at(spaceflight.car_body(), point, car.height))
        storms.buffet_car(dt)
    impact = bump_car(dt)
    if impact > 0.0:
        contracting.strike_hold(impact)
    update_ship()
    state["car_poses"] = (before, (car_position(), car_rotation()))


def car_rotation():
    right, up, forward = spaceflight.car_attitude()
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
    if poses is None or bubble.body is not spaceflight.car_body():
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
    heading, up = car.heading, car.up()
    if state["space"] is not None:
        _, up, heading = state["space"].axes()
    eye, basis = state["chase"].update(dt, car_position(), heading, up, car.speed(), camera_ground)
    state["position"], state["forward"] = eye, basis[2]
    state["chase_basis"] = basis
    set_fov(carview.fov(settings["fov"], car.speed()))


def set_fov(value):
    if world.fov != value:
        world.fov = value
        if world.camera is not None:
            engine.scene.set_camera_fov(world.camera, value)

"""
The car's space mode (flight.py): going into it and back to the hover
car, stepping the flight, the body the car is reckoned around, the time
warp, the nose hold, and the lines the HUD shows about the flight.
"""

import math

from shoonyakasha.planet.vec import add, dot, length, normalize, quat_from_basis, scale, sub

import flight
import hovercar
import hud as hud_module
from play.core import GOOD_TOAST, HOVER_BACK, SHIP_HALF, SPACE_HEIGHT, WARP_HEIGHT, planet, state, world

FLIGHT_STEP = 1.0 / 120.0       # s: space mode's steps
MAX_FLIGHT_STEPS = 256          # a frame, at most
WARPS = (1, 4, 16, 64)          # space mode's time warps
LANDED_SPEED = 2.0              # m/s: touching down slower, space mode ends
HOLD_GAIN = 1.5                 # 1/s: the nose hold's turn rate per radian left


def car_body():
    """The world body the car is reckoned around: the one it hovers over or flew nearest."""
    return state.vehicle.body or planet


def car_attitude():
    """(right, up, forward) of the car as drawn: the ship's axes in space mode, else hovercar.Car.attitude."""
    space = state.space.ship
    return space.axes() if space is not None else state.vehicle.car.attitude()


def time_warp():
    """How many times faster the world's clock runs: WARPS' in space mode, else 1."""
    return WARPS[state.space.warp] if state.space.ship is not None else 1


def space_ground(body):
    """The ground patch's query, (depth, normal) for a point from the planet's centre, on `body`; or None."""
    patch = world.patch
    if patch is None or patch[0] is not body:
        return None
    return lambda point: patch[1].query(body.relative(point))


def space_wind(point):
    """The wind (m/s) at a point from the planet's centre over the planet, none elsewhere."""
    body = world.body_under(point)
    return world.wind_at(planet, planet.relative(point)) if body is planet else (0.0, 0.0, 0.0)


def enter_space():
    """The car into space mode (G), at least SPACE_HEIGHT above the ground:
    a flight.Ship (hovercar.space_params) where it is, at its velocity,
    turned as drawn, with its tank and cargo, translation assist on."""
    car, hud = state.vehicle.car, state.ui.hud
    if not state.vehicle.driving or state.space.ship is not None:
        return False
    if car.height is None or car.height < SPACE_HEIGHT:
        hud.toast("Climb to %.0f m for space mode" % SPACE_HEIGHT, hud_module.WARNING)
        return False
    body = car_body()
    right, up, forward = car.attitude()
    state.space.ship = flight.Ship(add(body.centre(), car.position), add(body.velocity(), car.velocity),
                                 quat_from_basis(right, up, scale(forward, -1.0)),
                                 params=hovercar.space_params(state.economy.params["car"], SHIP_HALF), fuel=car.fuel,
                                 cargo=car.cargo)
    state.space.controls = flight.Controls(assist=True)
    state.space.time, state.space.warp = world.time, 0
    hud.toast("Space mode: G back to the hover car within %.0f m of the ground" % HOVER_BACK)
    print("[walker] space mode", flush=True)
    return True


def space_height(ship, body):
    """m from the ship's centre down to the ground under it, from the ground patch; None where it is not known."""
    ground = space_ground(body)
    answer = ground(ship.position) if ground is not None else None
    return -answer[0] if answer is not None else None


def leave_space(landed=False):
    """The car back to hovering (G), within HOVER_BACK of the ground, or
    `landed`: over the body under it, at its velocity relative to that
    body, its nose levelled, keeping its height; the time warp back to 1."""
    ship, car = state.space.ship, state.vehicle.car
    if ship is None:
        return False
    body = world.body_under(ship.position)
    height = space_height(ship, body)
    if not landed and (height is None or height > HOVER_BACK):
        state.ui.hud.toast("Come within %.0f m of the ground for the hover car" % HOVER_BACK, hud_module.WARNING)
        return False
    sync_car(ship, body, height)
    car.velocity = sub(ship.velocity, body.velocity()) if not ship.crashed else (0.0, 0.0, 0.0)
    car.target = max(hovercar.HOVER_MIN, min(hovercar.MAX_HEIGHT, height if height is not None else 0.0))
    car.turn = car.bank = car.pitch = 0.0
    state.space.ship, state.space.warp, state.space.hold = None, 0, None
    state.vehicle.controls = hovercar.CarControls()
    print("[walker] hover car over the %s" % body.name, flush=True)
    return True


def sync_car(ship, body, height):
    """The hover car's state from the ship's, over `body`: its position and
    velocity relative to it, its nose levelled, its height, its fuel."""
    car = state.vehicle.car
    car.position = body.relative(ship.position)
    car.velocity = sub(ship.velocity, body.velocity())
    up = normalize(car.position)
    _, ship_up, forward = ship.axes()
    for along in (forward, scale(ship_up, -1.0)):
        level = sub(along, scale(up, dot(along, up)))
        if length(level) > 1e-3:
            car.heading = normalize(level)
            break
    car.height = height
    car.fuel = ship.fuel
    state.vehicle.body = None if body is planet else body


def step_space(dt):
    """The car in space mode moved on by `dt` seconds of the world's clock
    (already moved on) in steps of FLIGHT_STEP, at most MAX_FLIGHT_STEPS a
    frame, each at its own time on the clock, against the ground patch
    under it and in the wind; the hover car's state kept with it
    (`sync_car`). Touching the ground slower than LANDED_SPEED, or
    crashing, it is the hover car again; below WARP_HEIGHT the time warp
    goes back to 1."""
    ship = state.space.ship
    hold_nose()
    end, t = world.time, state.space.time
    body = world.body_under(ship.position)
    ground = space_ground(body)
    steps = 0
    while t + FLIGHT_STEP <= end and steps < MAX_FLIGHT_STEPS:
        world.time = t
        ship.step(world.physics, state.space.controls, FLIGHT_STEP, ground, wind=space_wind)
        t += FLIGHT_STEP
        steps += 1
    world.time = end
    state.space.time = t if steps < MAX_FLIGHT_STEPS else end
    body = world.body_under(ship.position)
    height = space_height(ship, body)
    sync_car(ship, body, height)
    above = height if height is not None else length(body.relative(ship.position)) - body.radius
    if above < WARP_HEIGHT:
        state.space.warp = 0
    relative = length(sub(ship.velocity, body.velocity()))
    if ship.crashed:
        state.ui.hud.toast("Hard landing", hud_module.WARNING)
        leave_space(landed=True)
    elif ship.on_ground and relative < LANDED_SPEED:
        state.ui.hud.toast("Landed on the %s" % body.name, GOOD_TOAST)
        leave_space(landed=True)


def hold_nose():
    """With a nose hold (H), the space mode's pitch and yaw turning the nose
    along the velocity relative to the body the orbit is reckoned around
    (prograde) or against it (retrograde), at HOLD_GAIN (flight.point_controls)."""
    ship, hold = state.space.ship, state.space.hold
    if ship is None or hold is None:
        return
    _, _, velocity = flight.reference(world.physics, ship.position, ship.velocity)
    if length(velocity) < 1.0:
        return
    direction = normalize(velocity) if hold == "prograde" else scale(normalize(velocity), -1.0)
    controls = state.space.controls
    controls.pitch, controls.yaw = flight.point_controls(ship.axes(), direction, ship.params.max_rates, HOLD_GAIN)


def set_warp(step):
    """The time warp one step up or down WARPS, in space mode WARP_HEIGHT or more above the ground."""
    ship = state.space.ship
    if ship is None:
        return
    body = world.body_under(ship.position)
    height = space_height(ship, body)
    above = height if height is not None else length(body.relative(ship.position)) - body.radius
    if step > 0 and above < WARP_HEIGHT:
        state.ui.hud.toast("Time warp from %.0f km up" % (WARP_HEIGHT / 1000.0), hud_module.WARNING)
        return
    state.space.warp = max(0, min(len(WARPS) - 1, state.space.warp + step))


def space_readout():
    """How the flight stands, for the HUD: a dict of "body" ("planet" or
    "moon", the one the orbit is reckoned around), "assist", "throttle" (0
    to 1), "warp", "hold" (None, "prograde" or "retrograde"), "periapsis"
    and "apoapsis" (m above its radius, orbits.orbit; inf escaping), "dv"
    (the space drive's delta-v left, m/s), "speed" (m/s relative to the
    body), "prograde" (the unit velocity relative to it, or None below 1
    m/s), "moon" (its centre, planet space, and its radius; None without a
    moon), and "transfer": around the planet, the transfer planner, a dict
    of "lead" (the angle the moon must lead by for a Hohmann transfer from
    this radius, flight.hohmann_plan), "now" (the angle it leads by now,
    flight.phase_angle), "wait" (s until the burn, at the difference of
    their rates round; inf when it never comes), "burn" (its delta-v) and
    "from" (the radius's height above the planet); else None."""
    ship, controls = state.space.ship, state.space.controls
    body, position, velocity = flight.reference(world.physics, ship.position, ship.velocity)
    peri, apo = flight.orbit(body, position, velocity)
    moon = world.physics.moon
    car = state.vehicle.car
    speed = length(velocity)
    readout = {"body": "moon" if body is moon else "planet", "assist": controls.assist,
               "throttle": controls.throttle, "warp": time_warp(), "hold": state.space.hold, "periapsis": peri,
               "apoapsis": apo, "dv": hovercar.delta_v(car.params, car.fuel, car.cargo_mass), "speed": speed,
               "prograde": normalize(velocity) if speed >= 1.0 else None,
               "moon": (moon.position(world.time), moon.radius) if moon is not None else None, "transfer": None}
    if body is not moon and moon is not None:
        r1 = length(position)
        dv, _, lead = flight.hohmann_plan(body.gm, r1, moon.distance, moon.rate)
        now = flight.phase_angle(position, velocity, moon.position(world.time))
        rate = math.sqrt(body.gm / r1 ** 3) - moon.rate
        wait = ((now - lead) % (2.0 * math.pi)) / rate if rate > 0.0 else math.inf
        readout["transfer"] = {"lead": lead, "now": now, "wait": wait, "burn": dv, "from": r1 - body.radius}
    return readout


def space_lines():
    """The space readout (`space_readout`) as lines of text: the body and
    the assist, the throttle, the warp and the nose hold, the periapsis and
    apoapsis, the delta-v left, and around the planet the transfer planner's
    lead, the moon's angle now, the time until the burn and its delta-v
    against the delta-v left."""
    r = space_readout()

    def km(h):
        return "ESC" if math.isinf(h) else "%.1f km" % (h / 1000.0)
    lines = ["SPACE  %s  ASSIST %s" % (r["body"].upper(), "ON" if r["assist"] else "OFF"),
             "THR %3d %%  WARP x%d  %s" % (round(100.0 * r["throttle"]), r["warp"], (r["hold"] or "no hold").upper()),
             "PE %s  AP %s" % (km(r["periapsis"]), km(r["apoapsis"])),
             "DV %.0f m/s" % r["dv"]]
    t = r["transfer"]
    if t is not None:
        lines += ["MOON LEAD %3.0f  NOW %3.0f" % (math.degrees(t["lead"]), math.degrees(t["now"])),
                  "BURN IN %s  DV %.0f / %.0f" % (hud_module.wait_text(t["wait"]), t["burn"], r["dv"])]
    return lines

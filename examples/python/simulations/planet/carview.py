"""
The walker's flying car seen and driven (walker.py, hovercar.py): the
chase camera, the car's controls from the keys and the gamepad, and the
glow of its engines.

`ChaseCamera` trails the car: CHASE_DISTANCE behind it (longer by
CHASE_STRETCH a m/s of speed) and CHASE_PITCH above its level, looking at
LOOK_LIFT over it. Its offset from the car eases to the wanted one over
CHASE_EASE, so the camera lags the car's turns, not its speed. `orbit`
turns the camera about the car with the mouse (ORBIT_PER_PIXEL, its
height between MIN_PITCH and MAX_PITCH); ORBIT_RETURN seconds after the
last turn it eases back behind the car over RETURN_EASE. It stays
CLEARANCE above the ground where that is known. `fov(speed)` is the view's
field of view, wider by up to FOV_GAIN degrees at top speed.

`read_controls(input)` gives hovercar.CarControls: W and S the throttle,
A and D the steering, Space up, left Ctrl down, left Shift the boost; on
a gamepad the left stick drives and steers, the right trigger climbs, the
left one descends and A boosts.

`EngineGlows` draws the engines' glow as glowing discs (viewmodel.Glows,
the Glow pass) at NOZZLES, the car's engine nozzles in its model's
metres from its centre (right, up, back): IDLE_GLOW strong when idle,
brighter with the throttle, and with the boost a streak behind each,
longer with the speed.
"""

import math

from shoonyakasha import keys
from shoonyakasha.planet.vec import add, cross, dot, normalize, rotate, scale, sub

import hovercar
from viewmodel import Glows

CHASE_DISTANCE = 11.0            # m behind the car
CHASE_STRETCH = 0.04             # m more a m/s of speed
CHASE_PITCH = math.radians(14.0)
LOOK_LIFT = 1.2                  # m above the car's centre the camera looks at
CHASE_EASE = 0.12                # s
ORBIT_PER_PIXEL = 0.004          # rad
MIN_PITCH, MAX_PITCH = math.radians(-5.0), math.radians(70.0)
ORBIT_RETURN = 1.5               # s after the last orbit
RETURN_EASE = 0.6                # s
CLEARANCE = 1.0                  # m above the ground
FOV_GAIN = 12.0                  # degrees at top speed
DEAD_ZONE = 0.12                 # of a stick's travel

NOZZLES = ((-1.3, 0.38, 2.85), (1.3, 0.38, 2.85))   # m from the car's centre: right, up, back
NOZZLE_GLOW = (0.35, 0.85, 1.0)  # LDR
BOOST_GLOW = (1.0, 0.55, 0.2)
IDLE_GLOW = 0.25
GLOW_SIZE = 0.55                 # m across
STREAK_TIME = 0.08               # s of the car's speed a boost streak is long
STREAK_WIDTH = 0.35              # m


def ease(value, wanted, dt, time):
    return wanted + (value - wanted) * math.exp(-dt / time) if time > 0.0 else wanted


def ease_vector(value, wanted, dt, time):
    return tuple(ease(a, b, dt, time) for a, b in zip(value, wanted))


class ChaseCamera:
    def __init__(self):
        self.offset = None        # the eye from the car's centre, planet space
        self.yaw = 0.0            # rad about the up, from behind the car, to its right positive
        self.pitch = CHASE_PITCH
        self.since_orbit = math.inf

    def reset(self):
        self.offset = None
        self.yaw, self.pitch = 0.0, CHASE_PITCH
        self.since_orbit = math.inf

    def orbit(self, dx, dy):
        if dx == 0.0 and dy == 0.0:
            return
        self.yaw += dx * ORBIT_PER_PIXEL
        self.pitch = max(MIN_PITCH, min(MAX_PITCH, self.pitch + dy * ORBIT_PER_PIXEL))
        self.since_orbit = 0.0

    def wanted_offset(self, heading, up, speed):
        back = rotate(scale(heading, -1.0), up, -self.yaw)
        distance = CHASE_DISTANCE + CHASE_STRETCH * speed
        return add(scale(back, distance * math.cos(self.pitch)), scale(up, distance * math.sin(self.pitch)))

    def update(self, dt, centre, heading, up, speed, ground=None):
        """(eye, (right, up, forward)) for the car at `centre` (planet
        space) with its level `heading` and `up`; `ground(point)` the
        height above the ground at a point, or None."""
        self.since_orbit += dt
        if self.since_orbit > ORBIT_RETURN:
            self.yaw = ease(self.yaw, 0.0, dt, RETURN_EASE)
            self.pitch = ease(self.pitch, CHASE_PITCH, dt, RETURN_EASE)
        wanted = self.wanted_offset(heading, up, speed)
        self.offset = wanted if self.offset is None else ease_vector(self.offset, wanted, dt, CHASE_EASE)
        eye = add(centre, self.offset)
        if ground is not None:
            h = ground(eye)
            if h is not None and h < CLEARANCE:
                eye = add(eye, scale(up, CLEARANCE - h))
        look = normalize(sub(add(centre, scale(up, LOOK_LIFT)), eye))
        right = normalize(cross(look, up))
        return eye, (right, cross(right, look), look)


def fov(base, speed):
    return base + FOV_GAIN * min(1.0, max(0.0, speed / (hovercar.MAX_SPEED * hovercar.BOOST)))


def stick(value):
    if abs(value) < DEAD_ZONE:
        return 0.0
    return math.copysign((abs(value) - DEAD_ZONE) / (1.0 - DEAD_ZONE), value)


def trigger(input, axis):
    """A trigger's pull, 0 to 1 (its axis runs -1 to 1)."""
    return max(0.0, 0.5 * (input.get_gamepad_axis(axis) + 1.0))


def read_controls(input):
    down = input.is_key_down
    c = hovercar.CarControls()
    c.throttle = (1.0 if down(keys.W) else 0.0) - (1.0 if down(keys.S) else 0.0)
    c.steer = (1.0 if down(keys.D) else 0.0) - (1.0 if down(keys.A) else 0.0)
    c.climb = (1.0 if down(keys.SPACE) else 0.0) - (1.0 if down(keys.LEFT_CONTROL) else 0.0)
    c.boost = down(keys.LEFT_SHIFT)
    if input.is_gamepad_connected():
        c.throttle -= stick(input.get_gamepad_axis(keys.GAMEPAD_LEFT_Y))
        c.steer += stick(input.get_gamepad_axis(keys.GAMEPAD_LEFT_X))
        c.climb += trigger(input, keys.GAMEPAD_RIGHT_TRIGGER) - trigger(input, keys.GAMEPAD_LEFT_TRIGGER)
        c.boost = c.boost or input.is_gamepad_button_down(keys.GAMEPAD_A)
    c.throttle = max(-1.0, min(1.0, c.throttle))
    c.steer = max(-1.0, min(1.0, c.steer))
    c.climb = max(-1.0, min(1.0, c.climb))
    return c


class EngineGlows:
    def __init__(self, engine):
        self.glows = Glows(engine, len(NOZZLES), len(NOZZLES))

    def hide(self):
        self.glows.begin(None)
        self.glows.end()

    def update(self, view, centre, attitude, throttle, boost, speed):
        """The glows for the car at `centre` (relative to the camera) turned
        by `attitude` (right, up, forward), the view's basis `view`."""
        right, up, forward = attitude
        light = IDLE_GLOW + (1.0 - IDLE_GLOW) * max(0.0, throttle)
        colour = tuple(c * light for c in (BOOST_GLOW if boost else NOZZLE_GLOW))
        self.glows.begin(view)
        for r, u, b in NOZZLES:
            at = add(centre, add(add(scale(right, r), scale(up, u)), scale(forward, -b)))
            self.glows.disc(at, GLOW_SIZE * (1.3 if boost else 1.0), colour)
            if boost and speed > 1.0:
                tail = add(at, scale(forward, -max(1.0, speed * STREAK_TIME)))
                self.glows.streak(at, tail, STREAK_WIDTH, tuple(0.7 * c for c in colour))
        self.glows.end()


def level_heading(forward, up):
    """`forward` made level with the ground at `up`."""
    return normalize(sub(forward, scale(up, dot(forward, up))))

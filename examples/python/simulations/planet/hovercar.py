"""
The walker's flying car (walker.py): a hover car that skims the ground on
a cushion and climbs into the sky, flying like a car. Plain Python, no
engine: positions and velocities are metres and m/s relative to the
centre of the body it is over, its up the direction from that centre.

`Car.step(dt, controls, ground)` moves it by `dt` seconds in steps of at
most STEP, with `controls` (CarControls) and `ground(point)` the car's
height above the ground at a point (a position relative to the body's
centre), or None where it is not known:

- **Height:** `target` is the height above the ground it keeps, moved by
  `climb` at CLIMB_RATE between HOVER_MIN and MAX_HEIGHT. A spring and
  damper (HOVER_FREQUENCY, HOVER_DAMPING) pull the car towards it,
  against the higher of the ground under it and the ground LOOK_AHEAD
  seconds ahead along its velocity, so it rises before a hill; the
  vertical acceleration is held within LIFT and SINK, the vertical speed
  within MAX_CLIMB. Where the ground is not known it holds its height,
  its vertical speed dying away. It never sinks below FLOOR above the
  ground.
- **Drive:** `throttle` pushes along the nose with THRUST (BOOST times
  more with `boost`), against a drag that makes MAX_SPEED (times BOOST)
  its top speed forward and REVERSE_SHARE of it backward; pushing
  against its motion brakes with BRAKE as well.
- **Steering:** `steer` turns the nose about the up at up to TURN_RATE,
  less at low speed (TURN_IN_PLACE when still, the full rate from
  TURN_SPEED), the turn rate easing to the wanted one over TURN_EASE.
- **Grip:** the sideways speed dies away at GRIP_GROUND near the ground
  and GRIP_AIR from AIR_HEIGHT up, so it drifts more in the air.

`attitude()` gives the (right, up, forward) to draw it with: banked into
turns by BANK_GAIN of its turn rate times its speed, at most MAX_BANK, and
pitched by its acceleration along the nose (PITCH_GAIN) and its climb
(CLIMB_PITCH), at most MAX_PITCH, each easing over ATTITUDE_EASE.
`can_leave()` says whether it is slow enough (LEAVE_SPEED) and low
enough (LEAVE_HEIGHT) to get out of.

- **Fuel:** the car has `params` (CarParams) and `fuel` kg in its tank
  (full at the start). Each step burns `burn_rate(controls)`: the
  throttle's share of drive_burn (boost_burn times that with boost), and
  holding height, air_burn from AIR_HEIGHT up, less below it, none on
  the cushion. With the tank empty (`dry()`) it drives and climbs no
  more, its target height drops to HOVER_MIN so it settles onto its
  cushion, and it still steers.
"""

import math
from dataclasses import dataclass

from shoonyakasha.planet.vec import add, cross, dot, length, normalize, rotate, scale, sub

STEP = 1.0 / 120.0               # s
HOVER_MIN = 0.45                 # m above the ground, the cushion
MAX_HEIGHT = 300.0               # m above the ground
CLIMB_RATE = 12.0                # m/s the target height moves at full climb
HOVER_FREQUENCY = 2.2            # rad/s of the height's spring
HOVER_DAMPING = 0.9              # of critical
LIFT, SINK = 18.0, 14.0          # m/s^2 up and down, at most
MAX_CLIMB = 25.0                 # m/s up or down
LOOK_AHEAD = 0.35                # s along the velocity the ground ahead is looked at
FLOOR = 0.2                      # m above the ground the car never goes below
HOLD_DAMPING = 2.0               # 1/s the vertical speed dies away at where the ground is not known
THRUST = 20.0                    # m/s^2 at full throttle
MAX_SPEED = 70.0                 # m/s forward
BOOST = 1.6                      # thrust and top speed with boost, times
REVERSE_SHARE = 0.35
BRAKE = 25.0                     # m/s^2 more against the motion
TURN_RATE = 1.6                  # rad/s
TURN_IN_PLACE = 0.6              # rad/s when still
TURN_SPEED = 12.0                # m/s from which it turns at TURN_RATE
TURN_EASE = 0.15                 # s
GRIP_GROUND, GRIP_AIR = 6.0, 1.8   # 1/s
AIR_HEIGHT = 20.0                # m above the ground from which the grip is GRIP_AIR
BANK_GAIN = 0.012                # rad per (rad/s times m/s)
MAX_BANK = math.radians(25.0)
PITCH_GAIN = 0.01                # rad per m/s^2 along the nose
CLIMB_PITCH = 0.012              # rad per m/s of climb
MAX_PITCH = math.radians(12.0)
ATTITUDE_EASE = 0.25             # s
LEAVE_SPEED = 3.0                # m/s
LEAVE_HEIGHT = 3.0               # m above the ground


@dataclass
class CarParams:
    fuel_capacity: float = 100.0         # kg
    drive_burn: float = 0.05             # kg/s at full throttle
    boost_burn: float = 2.5              # times drive_burn with boost
    air_burn: float = 0.03               # kg/s holding height from AIR_HEIGHT up (less below, none on the cushion)


@dataclass
class CarControls:
    throttle: float = 0.0        # -1 (back) to 1 (forward)
    steer: float = 0.0           # -1 (left) to 1 (right)
    climb: float = 0.0           # -1 (down) to 1 (up)
    boost: bool = False


def ease(value, wanted, dt, time):
    """`value` moved towards `wanted` over `time` seconds (exponentially)."""
    return wanted + (value - wanted) * math.exp(-dt / time) if time > 0.0 else wanted


class Car:
    def __init__(self, position, heading, target=HOVER_MIN, params=None, fuel=None):
        """At `position` (from the body's centre), its nose along `heading`
        (made level), keeping `target` metres above the ground, with
        `params` (CarParams) and `fuel` kg (full when None)."""
        self.params = params or CarParams()
        self.fuel = self.params.fuel_capacity if fuel is None else fuel
        self.fuel_used = 0.0     # kg burnt in the last step
        self.position = tuple(position)
        self.velocity = (0.0, 0.0, 0.0)
        self.heading = self._level(heading)
        self.target = target
        self.height = None       # m above the ground, last known
        self.turn = 0.0          # rad/s about the up, right positive
        self.bank = 0.0          # rad, right side down positive
        self.pitch = 0.0         # rad, nose up positive
        self.accel = 0.0         # m/s^2 along the nose, last step

    def up(self):
        return normalize(self.position)

    def _level(self, v):
        up = self.up()
        level = sub(v, scale(up, dot(v, up)))
        if length(level) < 1e-9:
            level = cross(up, (1.0, 0.0, 0.0)) if abs(up[0]) < 0.9 else cross(up, (0.0, 1.0, 0.0))
        return normalize(level)

    def right(self):
        return normalize(cross(self.heading, self.up()))

    def speed(self):
        return length(self.velocity)

    def can_leave(self):
        return self.speed() < LEAVE_SPEED and self.height is not None and self.height < LEAVE_HEIGHT

    def dry(self):
        return self.fuel <= 0.0

    def burn_rate(self, controls):
        """kg/s the car burns with `controls`: drive_burn at full throttle
        (boost_burn times that with boost), and air_burn from AIR_HEIGHT up,
        less below it, none on the cushion."""
        p = self.params
        drive = abs(controls.throttle) * p.drive_burn * (p.boost_burn if controls.boost else 1.0)
        height = self.height if self.height is not None else 0.0
        air = min(1.0, max(0.0, (height - HOVER_MIN) / (AIR_HEIGHT - HOVER_MIN)))
        return drive + p.air_burn * air

    def step(self, dt, controls, ground):
        """Moved by `dt` seconds; fuel burnt at burn_rate. Dry, it drives and
        climbs no more and settles onto its cushion; it still steers."""
        self.fuel_used = 0.0
        while dt > 1e-9:
            h = min(dt, STEP)
            c = controls
            if self.dry():
                c = CarControls(steer=controls.steer)
                self.target = HOVER_MIN
            else:
                burnt = min(self.fuel, self.burn_rate(c) * h)
                self.fuel -= burnt
                self.fuel_used += burnt
            self._step(h, c, ground)
            dt -= h

    def _step(self, dt, c, ground):
        up = self.up()
        right = self.right()
        v = self.velocity
        vertical = dot(v, up)
        flat = sub(v, scale(up, vertical))
        forward_speed = dot(flat, self.heading)
        side_speed = dot(flat, right)
        speed = length(flat)

        # Steering.
        share = min(1.0, speed / TURN_SPEED)
        wanted_turn = c.steer * (TURN_IN_PLACE + (TURN_RATE - TURN_IN_PLACE) * share)
        if forward_speed < -0.5:
            wanted_turn = -wanted_turn
        self.turn = ease(self.turn, wanted_turn, dt, TURN_EASE)
        self.heading = self._level(rotate(self.heading, up, -self.turn * dt))
        right = self.right()

        # Drive along the nose.
        boost = BOOST if c.boost else 1.0
        top = MAX_SPEED * boost
        thrust = THRUST * boost
        accel = c.throttle * thrust - forward_speed * THRUST / MAX_SPEED
        if c.throttle * forward_speed < 0.0:
            accel -= math.copysign(BRAKE, forward_speed) * abs(c.throttle)
        new_forward = forward_speed + accel * dt
        new_forward = max(-REVERSE_SHARE * MAX_SPEED, min(top, new_forward))
        self.accel = (new_forward - forward_speed) / dt
        # The sideways speed, the old heading's forward speed carried into the new frame.
        height = self.height if self.height is not None else 0.0
        air = min(1.0, max(0.0, (height - HOVER_MIN) / (AIR_HEIGHT - HOVER_MIN)))
        grip = GRIP_GROUND + (GRIP_AIR - GRIP_GROUND) * air
        side_speed *= math.exp(-grip * dt)

        # Height.
        self.target = max(HOVER_MIN, min(MAX_HEIGHT, self.target + c.climb * CLIMB_RATE * dt))
        here = ground(self.position)
        if here is not None:
            ahead = ground(add(self.position, scale(flat, LOOK_AHEAD)))
            above = here if ahead is None else min(here, ahead)
            self.height = here
            w = HOVER_FREQUENCY
            lift = w * w * (self.target - above) - 2.0 * HOVER_DAMPING * w * vertical
            vertical += max(-SINK, min(LIFT, lift)) * dt
        else:
            vertical *= math.exp(-HOLD_DAMPING * dt)
        vertical = max(-MAX_CLIMB, min(MAX_CLIMB, vertical))

        self.velocity = add(add(scale(self.heading, new_forward), scale(right, side_speed)), scale(up, vertical))
        self.position = add(self.position, scale(self.velocity, dt))
        now = ground(self.position)
        if now is not None:
            self.height = now
            if now < FLOOR:
                self.position = add(self.position, scale(up, FLOOR - now))
                self.velocity = add(self.velocity, scale(up, max(0.0, -vertical)))
        self.heading = self._level(self.heading)

        # Attitude.
        bank = max(-MAX_BANK, min(MAX_BANK, BANK_GAIN * self.turn * max(0.0, forward_speed)))
        pitch = max(-MAX_PITCH, min(MAX_PITCH, -PITCH_GAIN * self.accel + CLIMB_PITCH * vertical))
        self.bank = ease(self.bank, bank, dt, ATTITUDE_EASE)
        self.pitch = ease(self.pitch, pitch, dt, ATTITUDE_EASE)

    def attitude(self):
        """(right, up, forward) of the car's body, banked and pitched."""
        up, forward = self.up(), self.heading
        right = normalize(cross(forward, up))
        forward = add(scale(forward, math.cos(self.pitch)), scale(up, math.sin(self.pitch)))
        up = normalize(cross(right, forward))
        right = add(scale(right, math.cos(self.bank)), scale(up, -math.sin(self.bank)))
        up = normalize(cross(right, forward))
        return normalize(right), up, normalize(forward)

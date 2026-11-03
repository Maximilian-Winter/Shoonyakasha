"""
A first-person character on foot: a character in the physics bubble
(shoonyakasha.planet.bubble, sk.PlanetPhysics' characters) walking on the
planet or the moon, seen from its eye.

The character's up is the way away from the body's centre at its feet,
set every step. The view is a heading (a unit vector along the ground,
carried along as the up turns: each step it is made square to the new up
again) and a pitch above it.

`Player.step` moves it for one fixed step: the velocity along the ground
(relative to what it stands on) is brought towards the wanted one (WALK,
RUN or CROUCH speed along the heading and to the right) by at most
ACCEL_GROUND, or ACCEL_AIR off the ground, times the step. On the ground
the velocity along the up is one step of gravity downwards, which keeps
the character on the floor over crests, or JUMP_SPEED when jumping, the
same on every body, so the jump goes higher where gravity is weaker; for
JUMP_GRACE after a jump it counts as off the ground. Off the ground
gravity pulls along the up. While jetting it climbs at JET_ACCEL,
burning JET_BURN of its fuel (0 to 1) a second; on the ground the fuel
refills at JET_REFILL a second. A character over ground the terrain mesh
does not cover is held where it is. `Player.push` adds to its velocity;
a push upwards lifts it off the ground as a jump does.

The eye is EYE_HEIGHT above the feet (CROUCH_EYE_HEIGHT crouched,
reached over EYE_SMOOTHING seconds); a step up of more than STEP_JUMP in
one step lowers the eye by as much, which comes back over EYE_SMOOTHING
seconds, so steps up do not jolt the view. With `bob`, the eye moves up
and down by BOB_AMPLITUDE once every BOB_STRIDE metres walked on the
ground, fading in and out over EYE_SMOOTHING seconds as it lands and
leaves the ground.
"""

import math
from dataclasses import dataclass

from shoonyakasha.planet.vec import add, cross, dot, length, local_frame, normalize, rotate, scale, sub

RADIUS, HEIGHT, MASS = 0.3, 1.8, 80.0      # m, m, kg
MAX_SLOPE = 50.0                           # degrees it walks up
WALK, RUN, CROUCH = 4.0, 8.0, 2.0          # m/s
ACCEL_GROUND, ACCEL_AIR = 40.0, 6.0        # m/s^2 towards the wanted velocity along the ground
JUMP_SPEED = 3.3                           # m/s: 1.1 m high on the planet, 5.5 m on the moon
JET_ACCEL, JET_BURN, JET_REFILL = 9.0, 0.25, 0.15   # m/s^2; fuel per second
EYE_HEIGHT, CROUCH_EYE_HEIGHT = 1.65, 1.0  # m above the feet
EYE_SMOOTHING = 0.1                        # s
JUMP_GRACE = 0.2                           # s after a jump it counts as off the ground
PUSH_LIFT = 1.0                            # m/s up a push must have to lift it off the ground
STEP_JUMP = 0.05                           # m the feet may rise in a step without lowering the eye
BOB_AMPLITUDE = 0.025                      # m
BOB_STRIDE = 1.6                           # m walked per bob
MAX_PITCH = math.radians(89.0)
LOOK_RADIANS_PER_PIXEL = 0.0025


@dataclass
class Controls:
    forward: float = 0.0         # -1 to 1
    strafe: float = 0.0          # -1 (left) to 1 (right)
    run: bool = False
    crouch: bool = False
    jump: bool = False           # asked to jump; taken by the next step on the ground, else dropped
    jet: bool = False            # held


class Player:
    def __init__(self, bubble, feet, heading):
        """A character with its feet at bubble coordinates `feet`, facing
        `heading` (made square to the up there)."""
        self.bubble = bubble
        self.physics = bubble.physics
        up = bubble.up(feet)
        self.character = self.physics.create_character(feet, up, RADIUS, HEIGHT, MASS, MAX_SLOPE)
        self.heading = self.level(heading, up)
        self.pitch = 0.0
        self.jet_fuel = 1.0
        self.crouching = False
        self.on_ground = False
        self.jump_grace = 0.0        # s left of JUMP_GRACE
        self.held = False            # held in place: no terrain mesh under it
        self.eye_height = EYE_HEIGHT
        self.eye_lag = 0.0           # m the eye is below where it goes, after a step up
        self.bob_distance = 0.0
        self.bob_weight = 0.0        # 0 in the air, 1 on the ground
        self.bob = True

    @staticmethod
    def level(heading, up):
        """`heading` made square to `up`, or the north there when it is along `up`."""
        h = sub(heading, scale(up, dot(heading, up)))
        return normalize(h) if length(h) > 1e-6 else local_frame(up)[1]

    def feet(self):
        """Bubble coordinates of the feet."""
        return self.physics.character_position(self.character)

    def up(self):
        return self.bubble.up(self.feet())

    def look(self, dx, dy):
        """Turned by a mouse movement of (dx, dy) pixels."""
        up = self.up()
        self.heading = self.level(rotate(self.heading, up, -dx * LOOK_RADIANS_PER_PIXEL), up)
        self.pitch = max(-MAX_PITCH, min(MAX_PITCH, self.pitch - dy * LOOK_RADIANS_PER_PIXEL))

    def basis(self):
        """(right, up, forward) of the view."""
        up = self.up()
        heading = self.level(self.heading, up)
        right = normalize(cross(heading, up))
        forward = add(scale(heading, math.cos(self.pitch)), scale(up, math.sin(self.pitch)))
        return right, cross(right, forward), forward

    def step(self, controls, dt, covered=True):
        """One fixed step of `dt` seconds with `controls`; held in place
        while not `covered` by the terrain mesh."""
        physics, c = self.physics, self.character
        before = self.feet()
        up = self.bubble.up(before)
        physics.character_set_up(c, up)
        self.heading = self.level(self.heading, up)
        if not covered:
            physics.character_set_velocity(c, (0.0, 0.0, 0.0))
            self.held = True
            controls.jump = False
            return
        self.held = False
        g = self.bubble.gravity(before)
        state, _, ground_v, _ = physics.character_ground(c)
        self.crouching = controls.crouch

        right = normalize(cross(self.heading, up))
        wish = add(scale(self.heading, controls.forward), scale(right, controls.strafe))
        if length(wish) > 1.0:
            wish = normalize(wish)
        speed = CROUCH if self.crouching else (RUN if controls.run else WALK)
        wanted = scale(wish, speed)

        v = sub(physics.character_velocity(c), ground_v)
        v_up = dot(v, up)
        # On the ground, unless it jumped within JUMP_GRACE: the ground is still under it then.
        self.jump_grace = max(0.0, self.jump_grace - dt)
        self.on_ground = state == "on_ground" and self.jump_grace == 0.0
        v_along = sub(v, scale(up, v_up))
        change = sub(wanted, v_along)
        most = (ACCEL_GROUND if self.on_ground else ACCEL_AIR) * dt
        if length(change) > most:
            change = scale(normalize(change), most)
        v_along = add(v_along, change)

        if self.on_ground:
            # One step of gravity into the ground, so that the floor holds it down over crests.
            v_up = -length(g) * dt
            if controls.jump:
                v_up = JUMP_SPEED
                self.jump_grace = JUMP_GRACE
            self.jet_fuel = min(1.0, self.jet_fuel + JET_REFILL * dt)
        else:
            v_up -= length(g) * dt
        controls.jump = False
        if controls.jet and not self.on_ground and self.jet_fuel > 0.0:
            v_up += JET_ACCEL * dt
            self.jet_fuel = max(0.0, self.jet_fuel - JET_BURN * dt)

        physics.character_set_velocity(c, add(ground_v, add(v_along, scale(up, v_up))))
        physics.character_update(c, dt, g)

        after = self.feet()
        rise = dot(sub(after, before), up)
        if self.on_ground and rise > STEP_JUMP:
            self.eye_lag += rise
        fade = math.exp(-dt / EYE_SMOOTHING)
        self.eye_lag *= fade
        target = CROUCH_EYE_HEIGHT if self.crouching else EYE_HEIGHT
        self.eye_height = target + (self.eye_height - target) * fade
        self.bob_weight = (1.0 if self.on_ground else 0.0) + (self.bob_weight - (1.0 if self.on_ground else 0.0)) * fade
        if self.on_ground:
            self.bob_distance += length(v_along) * dt

    def push(self, dv):
        """`dv` (m/s) added to the velocity; with more than PUSH_LIFT of it
        along the up, off the ground for JUMP_GRACE, as after a jump."""
        c = self.character
        self.physics.character_set_velocity(c, add(self.physics.character_velocity(c), dv))
        if dot(dv, self.up()) > PUSH_LIFT:
            self.jump_grace = JUMP_GRACE

    def eye(self):
        """Bubble coordinates of the eye."""
        feet = self.feet()
        height = self.eye_height - self.eye_lag
        if self.bob:
            height += BOB_AMPLITUDE * self.bob_weight * math.sin(2.0 * math.pi * self.bob_distance / BOB_STRIDE)
        return add(feet, scale(self.bubble.up(feet), height))

    def remove(self):
        self.physics.remove_character(self.character)

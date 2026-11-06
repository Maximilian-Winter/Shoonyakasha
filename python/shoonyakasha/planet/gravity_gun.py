"""
A gravity gun: from a character's eye (shoonyakasha.planet.character) it grabs, pulls, holds,
turns and punts the dynamic bodies of the physics bubble
(shoonyakasha.planet.bubble), and punts off anything else.

`Gun.step` runs once per physics step, after the player's. The bodies it
can grab are those `objects()` gives, with their radius and mass; one of
them that is not dynamic (a rock still in the scatter) is made dynamic by
`promote` before the gun grabs, pulls or punts it. Its target is what the
view's ray hits first within GunParams.pull_range (passing through the
player), or, when that is not a body it can grab, the one of those within
pull_cone_deg of the view's forward closest to it in angle that the ray
towards it reaches.

- **Grab and pull** (the grab control held): a grabbable target within
  grab_range of the eye is grabbed; one farther away is pulled towards
  the eye at up to 12 m/s (2 m/s per metre away), by at most pull_force.
  While grab stays held, a body being pulled stays the target as long as
  it is within PULL_KEEP_DEG of the view. A body whose weight is more
  than 0.9 hold_force is too heavy for either, and one `too_hot(body)`
  says is too hot is not grabbed or pulled either. Pressing grab while
  holding drops the body; a body is not grabbed again until grab is let
  go.
- **Hold:** the body is pulled towards the hold point, hold distance
  plus the body's radius in front of the eye, and its weight cancelled,
  by at most hold_force. The hold point moves with the player's velocity
  plus its motion relative to the eye smoothed over TURN_SMOOTHING (the
  view turns once a frame, which can span several steps); the pull aims
  at where it was at the start of the step, since the player has made the
  step already. The force brings the body's velocity, at a rate of twice
  the hold's angular frequency, towards the hold point's plus an approach
  of half that frequency times the distance (with the velocity term, a
  critically damped spring of hold_frequency), the approach no faster than
  the body can stop from in that distance with the force left over its
  weight (at least MIN_SPARE), so heavy bodies do not swing past. The body's angular
  velocity is brought towards turning it to the rotation it had
  relative to the view when grabbed (turned since by the rotate control)
  at hold_torque_gain. It passes through the player while held, and is
  dropped once it has stayed more than drop_distance from the hold point
  for SNAG_TIME (snagged), after first coming within it (or after
  REACH_TIME). The hold
  distance runs from MIN_HOLD to MAX_HOLD.
- **Punt:** the held body (once the punt control is let go, see the
  charged punt), or a target within punt_range, gets an impulse along
  the view of its mass times min(punt_speed,
  punt_impulse / mass); a target that cannot move (static or kinematic)
  takes punt_impulse. The player gets recoil_factor of that impulse
  back, over its mass, at most MAX_RECOIL m/s: punting the ground pushes
  the player off it.
- **Charged punt:** while holding, pressing punt starts a charge, which
  fires when the punt control is let go: within CLICK_TIME, or within
  charge_cooldown of the last charged punt, a plain punt; else the body
  gets at least charged_speed times the charge (its time held over
  charge_time, at most 1) times min(1, charged_mass / its mass).
"""

import math
from dataclasses import dataclass

from shoonyakasha.planet.character import MASS as PLAYER_MASS
from shoonyakasha.planet.vec import (add, dot, length, normalize, quat_conjugate, quat_from_basis, quat_multiply,
                                     quat_normalize, scale, sub)


MIN_HOLD, MAX_HOLD = 1.5, 6.0        # m, the hold distance's range
HOLD_STEP = 0.25                     # m the hold distance changes per wheel notch
MAX_RECOIL = 8.0                     # m/s
REACH_TIME = 3.0                     # s a grabbed body has to come within drop_distance of the hold point
TURN_SMOOTHING = 0.03                # s the hold point's velocity relative to the eye is smoothed over
PULL_KEEP_DEG = 30.0                 # degrees off the view a body being pulled stays the target within
MIN_SPARE = 0.5                      # m/s^2: the least acceleration the hold is taken to have to stop with
SNAG_TIME = 0.25                     # s a held body may stay more than drop_distance off before it is dropped
CLICK_TIME = 0.25                    # s the punt control may be held and still punt plainly
ROTATE_RADIANS_PER_PIXEL = 0.005
TOO_HEAVY_SHARE = 0.9                # of hold_force a body's weight may be


@dataclass
class GunParams:
    grab_range: float = 4.0          # m: grab directly
    pull_range: float = 18.0         # m: pull towards the player within the cone
    pull_cone_deg: float = 6.0
    pull_force: float = 6_000.0      # N, at most
    hold_distance: float = 2.2       # m in front of the eye (+ the object's radius)
    hold_force: float = 9_000.0      # N, at most, including holding against gravity
    hold_frequency: float = 6.0      # Hz, the hold spring's natural frequency
    hold_torque_gain: float = 12.0   # 1/s
    punt_range: float = 6.0          # m: punt what is not held
    punt_impulse: float = 1_600.0    # N s, at most
    punt_speed: float = 30.0         # m/s, at most from a punt
    recoil_factor: float = 0.6       # share of the punt's impulse back into the player
    drop_distance: float = 1.2       # m: drop if the object is this far off the hold point
    charge_time: float = 1.5         # s to full charge
    charged_speed: float = 240.0     # m/s at full charge, for light objects
    charged_mass: float = 30.0       # kg: full charged speed up to this mass
    charge_cooldown: float = 2.0     # s after a charged punt before the next


@dataclass
class GunControls:
    grab: bool = False               # held: grab or pull; pressed while holding: drop
    punt: bool = False               # asked to punt; taken by the next step
    punt_held: bool = False          # the punt control held (charging while holding something)
    rotate: tuple = (0.0, 0.0)       # pixels of mouse movement turning the held body, taken by the next step
    distance: float = 0.0            # wheel notches changing the hold distance, taken by the next step


@dataclass
class Target:
    body: int
    point: tuple                     # bubble coordinates
    distance: float                  # m from the eye
    grabbable: bool                  # one of the gun's objects, dynamic
    mass: float                      # kg, 0 when it cannot move


def view_rotation(right, up, forward):
    """The quaternion of the view's basis: X right, Y up, Z back."""
    return quat_from_basis(right, up, scale(forward, -1.0))


def axis_rotation(axis, angle):
    s = math.sin(0.5 * angle)
    return (math.cos(0.5 * angle), axis[0] * s, axis[1] * s, axis[2] * s)


class Gun:
    def __init__(self, player, objects, params=None, promote=None, too_hot=None):
        """The gun of `player`; `objects()` gives (body, radius, mass) of
        each body it may grab; `promote(body)` turns one of those that is not
        dynamic (a rock) dynamic before the gun acts on it; `too_hot(body)`
        whether one of them is too hot to grab (none is when None)."""
        self.player = player
        self.too_hot = too_hot
        self.physics = player.physics
        self.bubble = player.bubble
        self.objects = objects
        self.promote = promote
        self.p = params or GunParams()
        self.hold_distance = self.p.hold_distance
        self.target = None
        self.held = None
        self.held_radius = 0.0
        self.relative = (1.0, 0.0, 0.0, 0.0)   # the held body's rotation relative to the view
        self.last_offset = None              # hold point minus eye, last step
        self.turning = (0.0, 0.0, 0.0)       # m/s the hold point moves relative to the eye, smoothed
        self.reached = False                 # the held body came within drop_distance of the hold point
        self.held_time = 0.0
        self.off_time = 0.0                  # s the held body has been more than drop_distance off
        self.latch = False                   # grab must be let go before the next grab
        self.pulling = None                  # the body being pulled
        self.grab_was = False
        self.since_punt = math.inf           # s
        self.since_charged = math.inf        # s since the last charged punt
        self.charging = None                 # s the punt control has been held while holding, or None
        self.last_punt = None                # (body or None, speed given, recoil velocity)

    @property
    def status(self):
        """"holding", "too_heavy", "too_hot", "can_grab" (a grabbable target), or "idle"."""
        if self.held is not None:
            return "holding"
        t = self.target
        if t is not None and t.grabbable:
            if self.too_heavy(t):
                return "too_heavy"
            return "too_hot" if self.hot(t) else "can_grab"
        return "idle"

    def too_heavy(self, target):
        g = length(self.bubble.gravity(target.point))
        return target.mass * g > TOO_HEAVY_SHARE * self.p.hold_force

    def hot(self, target):
        return self.too_hot is not None and self.too_hot(target.body)

    def aim(self, eye, forward):
        """What the view points at (see the module's docstring), or None."""
        physics, character = self.physics, self.player.character
        radii = {body: (radius, mass) for body, radius, mass in self.objects()}
        hit = physics.cast_ray(eye, forward, self.p.pull_range, skip_character=character)
        if hit is not None:
            target = self.target_of(hit[0], hit[1], hit[3] * self.p.pull_range, radii)
            if target.grabbable:
                return target
        cone = math.cos(math.radians(self.p.pull_cone_deg))
        best, best_cos = None, cone
        for body, (radius, _) in radii.items():
            to = sub(physics.get_transform(body)[0], eye)
            d = length(to)
            if d < 1e-6 or d - radius > self.p.pull_range:
                continue
            c = dot(to, forward) / d
            if c < best_cos:
                continue
            seen = physics.cast_ray(eye, scale(to, 1.0 / d), d + radius, skip_character=character)
            if seen is not None and seen[0] == body:
                best, best_cos = self.target_of(body, seen[1], seen[3] * (d + radius), radii), c
        if best is not None:
            return best
        return None if hit is None else self.target_of(hit[0], hit[1], hit[3] * self.p.pull_range, radii)

    def target_of(self, body, point, distance, radii):
        if body in radii:
            return Target(body, point, distance, True, radii[body][1])
        dynamic = self.physics.motion(body) == "dynamic"
        return Target(body, point, distance, False, self.physics.mass(body) if dynamic else 0.0)

    def loose(self, body):
        """`body` made dynamic by `promote` when it is not."""
        if self.promote is not None and self.physics.motion(body) != "dynamic":
            self.promote(body)

    def step(self, controls, dt):
        """One physics step of `dt` seconds with `controls`."""
        player, physics = self.player, self.physics
        eye = player.eye()
        right, up, forward = player.basis()
        self.since_punt += dt
        self.since_charged += dt
        if self.held is not None and (not physics.contains(self.held) or physics.motion(self.held) != "dynamic"):
            self.drop()
        pressed = controls.grab and not self.grab_was
        self.grab_was = controls.grab
        if not controls.grab:
            self.latch = False
        self.hold_distance = max(MIN_HOLD, min(MAX_HOLD, self.hold_distance + HOLD_STEP * controls.distance))
        controls.distance = 0.0

        if self.held is not None:
            self.target = None
            dx, dy = controls.rotate
            if dx or dy:
                turn = quat_multiply(axis_rotation((0.0, 1.0, 0.0), dx * ROTATE_RADIANS_PER_PIXEL),
                                     axis_rotation((1.0, 0.0, 0.0), dy * ROTATE_RADIANS_PER_PIXEL))
                self.relative = quat_normalize(quat_multiply(turn, self.relative))
            controls.rotate = (0.0, 0.0)
            if self.charging is not None:
                self.charging += dt
                if not controls.punt_held:
                    self.release(forward)
                else:
                    self.hold(eye, right, up, forward, dt)
            elif controls.punt:
                self.charging = 0.0
                self.hold(eye, right, up, forward, dt)
            elif pressed:
                self.drop()
                self.latch = True
            else:
                self.hold(eye, right, up, forward, dt)
        else:
            controls.rotate = (0.0, 0.0)
            self.target = t = self.pulled(eye, forward) if controls.grab else None
            if t is None:
                self.pulling = None
                self.target = t = self.aim(eye, forward)
            if controls.punt:
                if t is not None and t.distance <= self.p.punt_range:
                    if t.grabbable:
                        self.loose(t.body)
                    self.punt(t.body if t.mass > 0.0 else None, forward)
            elif (controls.grab and not self.latch and t is not None and t.grabbable and not self.too_heavy(t)
                  and not self.hot(t)):
                self.loose(t.body)
                if t.distance <= self.p.grab_range:
                    self.grab(t.body, right, up, forward)
                else:
                    self.pull(t.body, eye)
                    self.pulling = t.body
        controls.punt = False

    def pulled(self, eye, forward):
        """The body being pulled as the target, while it is dynamic, within
        pull_range and within PULL_KEEP_DEG of `forward`; else None."""
        body, physics = self.pulling, self.physics
        if body is None or not physics.contains(body) or physics.motion(body) != "dynamic":
            return None
        p = physics.get_transform(body)[0]
        to = sub(p, eye)
        d = length(to)
        radius = self.radius_of(body)
        if radius is None or d < 1e-6 or dot(to, forward) < d * math.cos(math.radians(PULL_KEEP_DEG)):
            return None
        distance = max(0.0, d - radius)
        if distance > self.p.pull_range:
            return None
        return Target(body, p, distance, True, physics.mass(body))

    def radius_of(self, body):
        return next((radius for b, radius, _ in self.objects() if b == body), None)

    def grab(self, body, right, up, forward):
        self.held = body
        self.held_radius = self.radius_of(body) or 0.0
        q = self.physics.get_transform(body)[1]
        self.relative = quat_multiply(quat_conjugate(view_rotation(right, up, forward)), q)
        self.last_offset = None
        self.turning = (0.0, 0.0, 0.0)
        self.reached = False
        self.held_time = 0.0
        self.off_time = 0.0
        self.latch = True
        self.physics.set_pass_through(body, self.player.character)

    def drop(self):
        if self.held is not None and self.physics.contains(self.held):
            self.physics.set_pass_through(self.held, None)
        self.held = None
        self.last_offset = None
        self.charging = None

    def pull(self, body, eye):
        physics = self.physics
        p = physics.get_transform(body)[0]
        to_eye = sub(eye, p)
        d = length(to_eye)
        v = physics.get_velocity(body)[0]
        approach = dot(v, scale(to_eye, 1.0 / d))
        wanted = min(12.0, d * 2.0)
        force = scale(to_eye, physics.mass(body) * (wanted - approach) * 4.0 / d)
        if length(force) > self.p.pull_force:
            force = scale(normalize(force), self.p.pull_force)
        physics.add_force(body, force)

    def hold_point(self, eye, forward):
        return add(eye, scale(forward, self.hold_distance + self.held_radius))

    def hold(self, eye, right, up, forward, dt):
        physics, body = self.physics, self.held
        p, q = physics.get_transform(body)
        v, w = physics.get_velocity(body)
        m = physics.mass(body)
        target = self.hold_point(eye, forward)
        offset = sub(target, eye)
        if self.last_offset is not None:
            blend = dt / (TURN_SMOOTHING + dt)
            turning = scale(sub(offset, self.last_offset), 1.0 / dt)
            self.turning = add(self.turning, scale(sub(turning, self.turning), blend))
        self.last_offset = offset
        v_target = add(physics.character_velocity(self.player.character), self.turning)
        omega = 2.0 * math.pi * self.p.hold_frequency
        g = self.bubble.gravity(p)
        # The player has made this step already: the hold point at its start, where the body is now.
        to = sub(sub(target, scale(v_target, dt)), p)
        d = length(to)
        spare = max(self.p.hold_force / m - length(g), MIN_SPARE)
        approach = min(0.5 * omega * d, math.sqrt(2.0 * spare * d))
        wanted = add(v_target, scale(to, approach / d)) if d > 1e-9 else v_target
        force = sub(scale(sub(wanted, v), 2.0 * m * omega), scale(g, m))
        if length(force) > self.p.hold_force:
            force = scale(normalize(force), self.p.hold_force)
        physics.add_force(body, force)

        self.held_time += dt
        off = length(sub(target, p))
        if off <= self.p.drop_distance:
            self.reached = True
            self.off_time = 0.0
        elif self.reached or self.held_time > REACH_TIME:
            self.off_time += dt
            if self.off_time >= SNAG_TIME:
                self.drop()
                return

        q_target = quat_multiply(view_rotation(right, up, forward), self.relative)
        error = quat_multiply(q_target, quat_conjugate(q))
        if error[0] < 0.0:
            error = tuple(-e for e in error)
        w_wanted = scale(error[1:], 2.0 * self.p.hold_torque_gain)
        blend = min(1.0, 10.0 * dt)
        physics.set_velocity(body, v, add(w, scale(sub(w_wanted, w), blend)))

    @property
    def charge(self):
        """0 to 1: how far the punt being held has charged."""
        if self.charging is None or self.charging < CLICK_TIME or self.since_charged < self.p.charge_cooldown:
            return 0.0
        return min(1.0, self.charging / self.p.charge_time)

    def release(self, forward):
        """The punt control let go while charging: a charged punt, or a plain one after a click."""
        charge = self.charge
        self.charging = None
        if self.held is None:
            return
        self.punt(self.held, forward, charge)
        if charge > 0.0:
            self.since_charged = 0.0

    def punt(self, body, forward, charge=0.0):
        """`body` (None: something that cannot move) punted along `forward`,
        the player recoiling; with `charge`, faster for light bodies."""
        if body is None:
            impulse, speed = self.p.punt_impulse, None
        else:
            m = self.physics.mass(body)
            speed = min(self.p.punt_speed, self.p.punt_impulse / m)
            if charge > 0.0:
                speed = max(speed, self.p.charged_speed * charge * min(1.0, self.p.charged_mass / m))
            impulse = m * speed
            if body == self.held:
                self.drop()
            self.physics.add_impulse(body, scale(forward, impulse))
        recoil = min(MAX_RECOIL, self.p.recoil_factor * impulse / PLAYER_MASS)
        back = scale(forward, -recoil)
        self.player.push(back)
        self.since_punt = 0.0
        self.last_punt = (body, speed, back)

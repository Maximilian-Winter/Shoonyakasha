"""
A ship's flight over the planet, in Python floats (64-bit), with the
planet's centre at the origin.

The ship is a rigid body: position and velocity, an orientation (a unit
quaternion from the ship's body to the world) and an angular velocity in
the body. Its body axes are those of a camera: forward is -Z, up +Y,
right +X.

Forces: gravity falling off with the square of the distance from the
centre; drag against the velocity, and lift along the ship's up axis for
air flowing along its forward axis, both with the air's density, which
falls off exponentially with height; and the thrusters. `Ship.step`
advances it by one time step with semi-implicit Euler: the velocity
first, then the position with the new velocity.

The planet, its moon and the orbits around them are
`shoonyakasha.planet.orbits`'s (`Planet`, `Moon`, `reference`, `orbit`,
`orbit_path`, `circular_speed`), imported here.

Flight assist: the stick asks for rotation rates, which the ship's
torque holds (always). With translation assist on, the throttle asks for
a forward speed and the vertical thrusters for a climb rate; the
thrusters push towards that velocity and hold the ship against gravity,
drag and lift. With it off, the throttle drives the main engine and the
vertical thrusters push up or down. Over known ground, translation
assist descends no faster than it can brake to `touchdown_speed` at the
gear's height above the ground, decelerating at `assist_brake`:
sqrt(touchdown_speed^2 + 2 assist_brake height).

Ground contact: the ship's shape is a few points in its body, the landing
gear and the hull (nose, tail, wing tips, roof). `Ship.step` takes a
`ground` function that gives, for a point in the world, how far it is
below the surface along the surface's normal, and that normal. Each step,
the points that touch the surface or would reach it within the step get
impulses along the normal, which stop them moving into it and push them
out of it, and friction impulses along it, up to `friction` times the
normal impulse; both turn the ship as well as move it, so it settles on
its gear on a slope. They are solved together over a few iterations, with
the impulses summed per point. A point reaching the surface faster than
its limit (the gear's, or the hull's) crashes the ship: it stops where it
is and no longer flies.

When its gear touches the surface with the throttle at zero, no upward
thrust and the stick let go, the ship rests: no thrust and no torque, so
it settles on the ground under gravity. It rests until one of those
controls moves.
"""

import math
from dataclasses import dataclass, field

from shoonyakasha.planet.orbits import Moon, Planet, circular_speed, orbit, orbit_path, reference  # noqa: F401
from shoonyakasha.planet.vec import add, cross, dot, length, normalize, scale, sub


# ── Quaternions (tuples) ───────────────────────────────────────────────────

def quat_multiply(a, b):
    """The Hamilton product a b of quaternions (w, x, y, z)."""
    aw, ax, ay, az = a
    bw, bx, by, bz = b
    return (aw * bw - ax * bx - ay * by - az * bz,
            aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw)


def quat_rotate(q, v):
    """`v` turned by the unit quaternion `q`."""
    w, x, y, z = q
    u = (x, y, z)
    t = scale(cross(u, v), 2.0)
    return add(add(v, scale(t, w)), cross(u, t))


def quat_conjugate(q):
    """The inverse of the unit quaternion `q`."""
    return (q[0], -q[1], -q[2], -q[3])


def quat_normalize(q):
    n = math.sqrt(sum(c * c for c in q))
    return tuple(c / n for c in q)


def quat_from_basis(right, up, back):
    """The unit quaternion turning the body axes X, Y, Z onto `right`, `up`
    and `back` (orthonormal, right-handed)."""
    m00, m10, m20 = right
    m01, m11, m21 = up
    m02, m12, m22 = back
    trace = m00 + m11 + m22
    if trace > 0.0:
        s = math.sqrt(trace + 1.0) * 2.0
        q = (0.25 * s, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s)
    elif m00 > m11 and m00 > m22:
        s = math.sqrt(1.0 + m00 - m11 - m22) * 2.0
        q = ((m21 - m12) / s, 0.25 * s, (m01 + m10) / s, (m02 + m20) / s)
    elif m11 > m22:
        s = math.sqrt(1.0 + m11 - m00 - m22) * 2.0
        q = ((m02 - m20) / s, (m01 + m10) / s, 0.25 * s, (m12 + m21) / s)
    else:
        s = math.sqrt(1.0 + m22 - m00 - m11) * 2.0
        q = ((m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, 0.25 * s)
    return quat_normalize(q)


# ── The ship ───────────────────────────────────────────────────────────────

@dataclass
class ShipParams:
    mass: float = 20_000.0                              # kg
    inertia: tuple = (4e5, 5e5, 3e5)                    # kg m^2 about the body's X, Y, Z
    main_thrust: float = 6e5                            # N, forward
    rcs_thrust: float = 1.5e5                           # N, along each other direction
    max_torque: float = 2e5                             # N m about each axis
    drag_area: float = 12.0                             # m^2, drag coefficient times area
    lift_area: float = 8.0                              # m^2, lift coefficient times area
    max_rates: tuple = (0.8, 0.6, 1.5)                  # rad/s of pitch, yaw, roll at full stick
    rotation_gain: float = 4.0                          # 1/s
    assist_max_speed: float = 1_500.0                   # m/s forward at full throttle, assisted
    assist_climb_rate: float = 60.0                     # m/s up at full vertical stick, assisted
    assist_gain: float = 1.5                            # 1/s
    touchdown_speed: float = 3.0                        # m/s: assist's descent at the ground, at most
    assist_brake: float = 1.5                           # m/s^2: assist's deceleration over the ground
    # Contact points in the body (right, up, back), metres from the centre, where the F-117 model
    # (models/f117) has them: the nose wheel and the two main wheels; the nose, the tail, the wing
    # tips, the cockpit's roof and the two fins' tops.
    gear: tuple = ((0.0, -1.95, -4.35), (-2.14, -1.95, 1.15), (2.14, -1.95, 1.15))
    hull: tuple = ((0.0, 0.0, -9.5), (0.0, -0.3, 8.7), (-6.6, 0.0, 6.9), (6.6, 0.0, 6.9), (0.0, 1.9, -5.3),
                   (-1.7, 2.1, 9.9), (1.7, 2.1, 9.9))
    gear_crash_speed: float = 10.0                      # m/s into the surface
    hull_crash_speed: float = 3.0                       # m/s into the surface
    friction: float = 0.6                               # friction impulse per normal impulse, at most


CONTACT_ITERATIONS = 8
CONTACT_STIFFNESS = 0.2         # share of a point's depth pushed out per step
CONTACT_SLOP = 0.01             # m of depth left alone
GEAR_TOUCH = 0.05               # m: a gear point this close to the surface touches it


@dataclass
class Controls:
    throttle: float = 0.0       # 0 .. 1
    pitch: float = 0.0          # -1 .. 1, nose up
    yaw: float = 0.0            # -1 .. 1, nose right
    roll: float = 0.0           # -1 .. 1, right wing down
    vertical: float = 0.0       # -1 .. 1, up
    assist: bool = True         # translation assist


@dataclass
class Ship:
    position: tuple
    velocity: tuple = (0.0, 0.0, 0.0)
    orientation: tuple = (1.0, 0.0, 0.0, 0.0)       # body to world
    angular_velocity: tuple = (0.0, 0.0, 0.0)       # rad/s, in the body
    params: ShipParams = field(default_factory=ShipParams)
    thrust: tuple = (0.0, 0.0, 0.0)                 # N, in the body, the last step's
    on_ground: bool = False                         # a gear point touched the surface in the last step
    resting: bool = False                           # thrusters and torque off until the controls move
    crashed: bool = False
    impact_speed: float = 0.0                       # m/s, the fastest a point reached the surface at in the last step

    def axes(self):
        """(right, up, forward) in the world."""
        q = self.orientation
        return quat_rotate(q, (1.0, 0.0, 0.0)), quat_rotate(q, (0.0, 1.0, 0.0)), quat_rotate(q, (0.0, 0.0, -1.0))

    def aerodynamic_force(self, planet):
        """Drag and lift (N, in the world)."""
        rho = planet.density(self.position)
        v = self.velocity
        speed = length(v)
        if speed < 1e-6 or rho == 0.0:
            return (0.0, 0.0, 0.0)
        drag = scale(v, -0.5 * rho * speed * self.params.drag_area)
        _, up, forward = self.axes()
        along = dot(v, forward)
        lift = scale(up, 0.5 * rho * along * abs(along) * self.params.lift_area)
        return add(drag, lift)

    def descent_limit(self, height):
        """The fastest assisted descent (m/s) with the gear `height` metres above the ground."""
        p = self.params
        return math.sqrt(p.touchdown_speed * p.touchdown_speed + 2.0 * p.assist_brake * max(height, 0.0))

    def _thrust(self, planet, controls, gravity, aero, gear_height=None, surface=None):
        """The thrusters' force in the body: (right, up, back) components.
        `gear_height`: the gear's height above the ground, when known.
        `surface`: (velocity, acceleration, ...) of the surface the assist's
        velocities are relative to; still when None."""
        p = self.params
        right, up, forward = self.axes()
        if controls.assist:
            surface_velocity, surface_acceleration = surface[:2] if surface else ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0))
            climb = controls.vertical * p.assist_climb_rate
            if gear_height is not None:
                climb = max(climb, -self.descent_limit(gear_height))
            wanted = add(scale(forward, controls.throttle * p.assist_max_speed), scale(up, climb))
            accel = sub(scale(sub(wanted, sub(self.velocity, surface_velocity)), p.assist_gain),
                        sub(add(gravity, scale(aero, 1.0 / p.mass)), surface_acceleration))
            force = scale(accel, p.mass)
            body = (dot(force, right), dot(force, up), -dot(force, forward))
        else:
            body = (0.0, controls.vertical * p.rcs_thrust, -controls.throttle * p.main_thrust)
        return (max(-p.rcs_thrust, min(p.rcs_thrust, body[0])),
                max(-p.rcs_thrust, min(p.rcs_thrust, body[1])),
                max(-p.main_thrust, min(p.rcs_thrust, body[2])))

    @staticmethod
    def idle(controls):
        """The throttle at zero, no upward thrust and the stick let go."""
        return (controls.throttle == 0.0 and controls.vertical <= 0.0
                and controls.pitch == 0.0 and controls.yaw == 0.0 and controls.roll == 0.0)

    def step(self, planet, controls, dt, ground=None):
        """Advance by `dt` seconds; `ground(point)` gives (depth, normal) of
        the surface under a world point, or None where it is not known. The
        surface moves as planet.surface_motion gives it: the assist, the
        contacts and a crashed ship go by velocities relative to it, and
        the position moves by the velocity relative to the surface plus the
        surface's own displacement over the step."""
        surface = planet.surface_motion(self.position, dt)
        if self.crashed:
            self.velocity = surface[0]
            self.position = add(self.position, surface[2])
            return
        p = self.params
        if not self.idle(controls):
            self.resting = False
        elif self.on_ground:
            self.resting = True
        resting = self.resting
        # The ground under the centre: the gear's height above it, and whether a contact point can reach it.
        under = ground(self.position) if ground is not None else None
        gear_height = None
        near = False
        if under is not None:
            above = -under[0]
            gear_height = above - max(-arm[1] for arm in p.gear)
            reach = max(length(arm) for arm in p.gear + p.hull)
            near = above <= reach + length(sub(self.velocity, surface[0])) * dt + CONTACT_SLOP
        gravity = planet.gravity(self.position)
        aero = self.aerodynamic_force(planet)
        self.thrust = ((0.0, 0.0, 0.0) if resting
                       else self._thrust(planet, controls, gravity, aero, gear_height, surface))
        right, up, forward = self.axes()
        thrust_world = add(add(scale(right, self.thrust[0]), scale(up, self.thrust[1])),
                           scale(forward, -self.thrust[2]))
        accel = add(gravity, scale(add(thrust_world, aero), 1.0 / p.mass))
        self.velocity = add(self.velocity, scale(accel, dt))
        self._spin(controls, dt, resting)
        self.on_ground = False
        self.impact_speed = 0.0
        if near:
            self._contact(ground, dt, surface[0])
            if self.crashed:
                return
        self.position = add(self.position, add(scale(sub(self.velocity, surface[0]), dt), surface[2]))
        w = self.angular_velocity
        dq = quat_multiply(self.orientation, (0.0, w[0], w[1], w[2]))
        self.orientation = quat_normalize(tuple(self.orientation[i] + 0.5 * dt * dq[i] for i in range(4)))

    def _spin(self, controls, dt, resting):
        """The angular velocity after this step's torque."""
        p = self.params
        wanted = (controls.pitch * p.max_rates[0], -controls.yaw * p.max_rates[1], -controls.roll * p.max_rates[2])
        w = self.angular_velocity
        inertia = p.inertia
        torque = (0.0, 0.0, 0.0) if resting else tuple(
            max(-p.max_torque, min(p.max_torque, inertia[i] * (wanted[i] - w[i]) * p.rotation_gain))
            for i in range(3))
        spin = cross(w, tuple(inertia[i] * w[i] for i in range(3)))   # the gyroscopic term, w x (I w)
        self.angular_velocity = tuple(w[i] + (torque[i] - spin[i]) / inertia[i] * dt for i in range(3))

    # ── Contacts ──

    def _point_velocity(self, arm_body):
        """World velocity of the body point at `arm_body` from the centre."""
        return add(self.velocity, quat_rotate(self.orientation, cross(self.angular_velocity, arm_body)))

    def _inverse_mass_along(self, arm_body, direction_body):
        """1 / (the mass an impulse along `direction_body` at `arm_body` meets)."""
        arm_cross = cross(arm_body, direction_body)
        inertia = self.params.inertia
        return 1.0 / self.params.mass + sum(arm_cross[i] * arm_cross[i] / inertia[i] for i in range(3))

    def _apply_impulse(self, arm_body, impulse_world):
        q = self.orientation
        self.velocity = add(self.velocity, scale(impulse_world, 1.0 / self.params.mass))
        impulse_body = quat_rotate(quat_conjugate(q), impulse_world)
        turn = cross(arm_body, impulse_body)
        inertia = self.params.inertia
        self.angular_velocity = tuple(self.angular_velocity[i] + turn[i] / inertia[i] for i in range(3))

    def _contact(self, ground, dt, surface_velocity=(0.0, 0.0, 0.0)):
        """Impulses at the contact points touching the surface (moving at
        `surface_velocity`), and whether the ship crashed."""
        p = self.params
        relative = lambda arm: sub(self._point_velocity(arm), surface_velocity)
        q = self.orientation
        inverse = quat_conjugate(q)
        contacts = []
        for arm, is_gear in [(a, True) for a in p.gear] + [(a, False) for a in p.hull]:
            hit = ground(add(self.position, quat_rotate(q, arm)))
            if hit is None:
                continue
            depth, normal = hit
            approach = -dot(relative(arm), normal)
            if depth + max(approach, 0.0) * dt <= 0.0:
                continue                         # stays clear of the surface this step
            if is_gear and depth > -GEAR_TOUCH:
                self.on_ground = True
            if approach > 0.0:
                self.impact_speed = max(self.impact_speed, approach)
                if approach > (p.gear_crash_speed if is_gear else p.hull_crash_speed):
                    self.crashed = True
            if depth > CONTACT_SLOP:
                target = CONTACT_STIFFNESS * (depth - CONTACT_SLOP) / dt
            elif depth < 0.0:
                target = depth / dt              # may close the gap within the step, no faster
            else:
                target = 0.0
            normal_body = quat_rotate(inverse, normal)
            contacts.append({"arm": arm, "normal": normal, "target": target,
                             "mass": 1.0 / self._inverse_mass_along(arm, normal_body),
                             "normal_impulse": 0.0, "friction_impulse": (0.0, 0.0, 0.0)})
        if self.crashed:
            self.velocity = surface_velocity
            self.angular_velocity = (0.0, 0.0, 0.0)
            return
        for _ in range(CONTACT_ITERATIONS):
            for c in contacts:
                arm, normal = c["arm"], c["normal"]
                along = dot(relative(arm), normal)
                total = max(c["normal_impulse"] + (c["target"] - along) * c["mass"], 0.0)
                self._apply_impulse(arm, scale(normal, total - c["normal_impulse"]))
                c["normal_impulse"] = total

                v = relative(arm)
                sliding = sub(v, scale(normal, dot(v, normal)))
                speed = length(sliding)
                if speed < 1e-9:
                    continue
                direction = scale(sliding, 1.0 / speed)
                mass = 1.0 / self._inverse_mass_along(arm, quat_rotate(inverse, direction))
                friction = sub(c["friction_impulse"], scale(direction, speed * mass))
                limit = p.friction * c["normal_impulse"]
                size = length(friction)
                if size > limit:
                    friction = scale(friction, limit / size)
                self._apply_impulse(arm, sub(friction, c["friction_impulse"]))
                c["friction_impulse"] = friction


def smoothstep(edge0, edge1, x):
    t = max(0.0, min(1.0, (x - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def reentry_glow(planet, position, velocity):
    """How strongly the air ahead of the ship glows, 0 to 1. Not a physical
    temperature: the product of the speed's rise from 0.6 to 0.85 of the
    circular speed at the surface, and the air density's rise, in powers of
    ten, from 1e-5 to 1e-3 of its density at sea level. A ship coming down
    from orbit glows from about 25 km down to where the air has slowed it."""
    speed = length(velocity) / circular_speed(planet, 0.0)
    density = planet.density(position) / planet.air_density
    if density <= 0.0:
        return 0.0
    return smoothstep(0.6, 0.85, speed) * smoothstep(-5.0, -3.0, math.log10(density))



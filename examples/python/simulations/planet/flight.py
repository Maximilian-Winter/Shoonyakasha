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


# ── Vectors and quaternions (tuples) ───────────────────────────────────────

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


# ── The planet and the ship ────────────────────────────────────────────────

@dataclass
class Planet:
    radius: float = 100_000.0           # m
    surface_gravity: float = 4.905      # m/s^2 at the radius
    air_density: float = 1.225          # kg/m^3 at the radius
    scale_height: float = 2_500.0       # m; the atmosphere's Rayleigh scale height
    density_factor: float = 1.0         # times the air's density

    @property
    def gm(self):
        return self.surface_gravity * self.radius * self.radius

    def gravity(self, position):
        r = length(position)
        return scale(position, -self.gm / (r * r * r))

    def density(self, position):
        height = max(length(position) - self.radius, -2_000.0)
        return self.air_density * self.density_factor * math.exp(-height / self.scale_height)


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
    # Contact points in the body (right, up, back), metres from the centre.
    gear: tuple = ((0.0, -2.0, -4.0), (-2.5, -2.0, 2.5), (2.5, -2.0, 2.5))
    hull: tuple = ((0.0, -0.5, -7.0), (0.0, 0.0, 6.0), (-6.0, -0.8, 2.5), (6.0, -0.8, 2.5), (0.0, 1.5, 0.0))
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

    def _thrust(self, planet, controls, gravity, aero, gear_height=None):
        """The thrusters' force in the body: (right, up, back) components.
        `gear_height`: the gear's height above the ground, when known."""
        p = self.params
        right, up, forward = self.axes()
        if controls.assist:
            climb = controls.vertical * p.assist_climb_rate
            if gear_height is not None:
                climb = max(climb, -self.descent_limit(gear_height))
            wanted = add(scale(forward, controls.throttle * p.assist_max_speed), scale(up, climb))
            accel = sub(scale(sub(wanted, self.velocity), p.assist_gain),
                        add(gravity, scale(aero, 1.0 / p.mass)))
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
        the surface under a world point, or None where it is not known."""
        if self.crashed:
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
            near = above <= reach + length(self.velocity) * dt + CONTACT_SLOP
        gravity = planet.gravity(self.position)
        aero = self.aerodynamic_force(planet)
        self.thrust = (0.0, 0.0, 0.0) if resting else self._thrust(planet, controls, gravity, aero, gear_height)
        right, up, forward = self.axes()
        thrust_world = add(add(scale(right, self.thrust[0]), scale(up, self.thrust[1])),
                           scale(forward, -self.thrust[2]))
        accel = add(gravity, scale(add(thrust_world, aero), 1.0 / p.mass))
        self.velocity = add(self.velocity, scale(accel, dt))
        self._spin(controls, dt, resting)
        self.on_ground = False
        self.impact_speed = 0.0
        if near:
            self._contact(ground, dt)
            if self.crashed:
                return
        self.position = add(self.position, scale(self.velocity, dt))
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

    def _contact(self, ground, dt):
        p = self.params
        q = self.orientation
        inverse = quat_conjugate(q)
        contacts = []
        for arm, is_gear in [(a, True) for a in p.gear] + [(a, False) for a in p.hull]:
            hit = ground(add(self.position, quat_rotate(q, arm)))
            if hit is None:
                continue
            depth, normal = hit
            approach = -dot(self._point_velocity(arm), normal)
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
            self.velocity = (0.0, 0.0, 0.0)
            self.angular_velocity = (0.0, 0.0, 0.0)
            return
        for _ in range(CONTACT_ITERATIONS):
            for c in contacts:
                arm, normal = c["arm"], c["normal"]
                along = dot(self._point_velocity(arm), normal)
                total = max(c["normal_impulse"] + (c["target"] - along) * c["mass"], 0.0)
                self._apply_impulse(arm, scale(normal, total - c["normal_impulse"]))
                c["normal_impulse"] = total

                v = self._point_velocity(arm)
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


def orbit(planet, position, velocity):
    """(periapsis, apoapsis) heights above the radius of the path the ship
    would coast along around the planet; apoapsis is inf when it would
    escape."""
    r = length(position)
    v2 = dot(velocity, velocity)
    energy = 0.5 * v2 - planet.gm / r
    h = length(cross(position, velocity))
    e = math.sqrt(max(0.0, 1.0 + 2.0 * energy * h * h / (planet.gm * planet.gm)))
    if energy >= 0.0:
        periapsis = h * h / (planet.gm * (1.0 + e))
        return periapsis - planet.radius, math.inf
    a = -planet.gm / (2.0 * energy)
    return a * (1.0 - e) - planet.radius, a * (1.0 + e) - planet.radius


def orbit_path(planet, position, velocity, count=256, reach=10.0):
    """`count` points along the path the ship would coast along, from its
    position onwards in the direction it moves: a whole ellipse, evenly
    spaced in eccentric anomaly; or, on an escape, the open branch out to
    `reach` times its present distance from the centre. An empty list when
    it moves straight towards or away from the centre (no plane to draw
    in)."""
    mu = planet.gm
    r = length(position)
    h = cross(position, velocity)
    if length(h) < 1e-9 * r * max(length(velocity), 1e-9):
        return []
    eccentricity_vector = sub(scale(cross(velocity, h), 1.0 / mu), scale(position, 1.0 / r))
    e = length(eccentricity_vector)
    # Perifocal frame: P towards periapsis (or the position on a circle), Q a quarter turn on.
    along = normalize(eccentricity_vector) if e > 1e-9 else scale(position, 1.0 / r)
    normal = normalize(h)
    across = cross(normal, along)
    p = dot(h, h) / mu                                   # the conic's semi-latus rectum
    nu0 = math.atan2(dot(position, across), dot(position, along))   # the ship's true anomaly
    points = []
    if e < 1.0:
        a = p / (1.0 - e * e)
        b = a * math.sqrt(1.0 - e * e)
        e0 = 2.0 * math.atan(math.sqrt((1.0 - e) / (1.0 + e)) * math.tan(nu0 / 2.0))
        for i in range(count):
            angle = e0 + 2.0 * math.pi * i / (count - 1)
            x, y = a * (math.cos(angle) - e), b * math.sin(angle)
            points.append(add(scale(along, x), scale(across, y)))
        return points
    # Open: from the ship's true anomaly to where the distance reaches `reach` times r.
    limit = math.acos(max(-1.0, min(1.0, (p / (reach * r) - 1.0) / e)))
    end = max(limit, nu0)
    for i in range(count):
        nu = nu0 + (end - nu0) * i / (count - 1)
        radius = p / (1.0 + e * math.cos(nu))
        points.append(add(scale(along, radius * math.cos(nu)), scale(across, radius * math.sin(nu))))
    return points


def circular_speed(planet, height):
    return math.sqrt(planet.gm / (planet.radius + height))
